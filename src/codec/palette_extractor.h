#pragma once

#include "palette_types.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <vector>

namespace hakonyans {

class PaletteExtractor {
public:
    static Palette extract(const int16_t* block, int max_colors = 8) {
        std::map<int16_t, int> counts;
        for (int i = 0; i < 64; i++) {
            counts[block[i]]++;
        }
        
        if (counts.size() > (size_t)max_colors) {
            // Too many colors, return empty palette to signal failure (fallback to DCT)
            // Or implement simple clustering (e.g. k-means) here if we want lossy palette
            // For now, strict lossless palette extraction
            return Palette(); 
        }

        Palette p;
        p.size = (uint8_t)counts.size();
        int idx = 0;
        // Sort by frequency (descending) for better compression?
        // Or sort by value for better delta coding?
        // Let's sort by frequency for now.
        std::vector<std::pair<int16_t, int>> sorted_counts(counts.begin(), counts.end());
        std::sort(sorted_counts.begin(), sorted_counts.end(), [](const auto& a, const auto& b) {
            return a.second > b.second;
        });

        for (const auto& kv : sorted_counts) {
            p.colors[idx++] = kv.first;
        }
        return p;
    }
    
    static std::vector<uint8_t> map_indices(const int16_t* block, const Palette& p) {
        std::vector<uint8_t> indices(64);
        for (int i = 0; i < 64; i++) {
            int16_t val = block[i];
            int best_idx = 0;
            int min_dist = 1 << 30;
            
            for (int k = 0; k < p.size; k++) {
                if (p.colors[k] == val) {
                    best_idx = k;
                    break;
                }
                // Fallback for lossy (if we implement clustering later)
                int dist = std::abs((int)p.colors[k] - (int)val);
                if (dist < min_dist) {
                    min_dist = dist;
                    best_idx = k;
                }
            }
            indices[i] = (uint8_t)best_idx;
        }
        return indices;
    }
    static uint32_t estimate_palette_local_cost(const Palette& p, const std::vector<uint8_t>& idx) {
        // 1. Delta cost of palette colors: sum(|c[i] - c[i-1]|)
        uint32_t delta_cost = 0;
        if (p.size > 0) {
            delta_cost += (uint32_t)std::abs((int)p.colors[0]); // first color cost (assume prev is 0)
            for (int i = 1; i < p.size; i++) {
                int d = (int)p.colors[i] - (int)p.colors[i-1];
                delta_cost += std::abs(d);
            }
        }
        
        // 2. Index transition cost: count of idx[i] != idx[i-1]
        // This approximates LZ or RLE difficulty.
        uint32_t transition_cost = 0;
        if (!idx.empty()) {
            transition_cost++; // first symbol
            for (size_t i = 1; i < idx.size(); i++) {
                if (idx[i] != idx[i-1]) transition_cost++;
            }
        }
        
        // 3. Simple entropy estimate (frequency distribution)
        // Weighted less than transitions/deltas for now?
        // Actually, just transitions + delta is a good enough proxy for now.
        // Let's weight them:
        // Delta implies palette storage cost.
        // Transitions implies index storage cost.
        // 1 delta unit ~ 1 byte? No, probably bits.
        // 1 transition ~ 1 new LZ/RLE token ~ several bits.
        // Heuristic weights:
        return delta_cost + (transition_cost * 4); 
    }

    static void reorder_palette_and_indices(Palette& p, std::vector<uint8_t>& idx, const int* new_order, size_t new_order_size) {
        if ((size_t)p.size != new_order_size) return;
        
        Palette new_p;
        new_p.size = p.size;
        
        // map old_idx -> new_idx (palette size is <= 8)
        uint8_t map[8] = {0};
        
        for (int new_i = 0; new_i < p.size; new_i++) {
            int old_i = new_order[new_i];
            if (old_i < 0 || old_i >= p.size) return;
            new_p.colors[new_i] = p.colors[old_i];
            map[old_i] = (uint8_t)new_i;
        }
        
        for (size_t i = 0; i < idx.size(); i++) {
            if (idx[i] < p.size) {
                idx[i] = map[idx[i]];
            }
        }
        p = new_p;
    }

    static void optimize_palette_order(Palette& p, std::vector<uint8_t>& idx, int& trials, int& adopted) {
        if (p.size <= 1) return;

        // Base cost (current freq-sorted order)
        uint32_t best_cost = estimate_palette_local_cost(p, idx);
        Palette best_p = p;
        std::vector<uint8_t> best_idx = idx;
        bool changed = false;
        
        trials++;

        // Candidate 2: Value Ascending
        {
            std::array<int, 8> order = {0, 1, 2, 3, 4, 5, 6, 7};
            std::sort(order.begin(), order.begin() + p.size, [&](int a, int b){
                return p.colors[a] < p.colors[b];
            });
            
            Palette cand_p = p;
            std::vector<uint8_t> cand_idx = idx;
            reorder_palette_and_indices(cand_p, cand_idx, order.data(), (size_t)p.size);
            uint32_t cost = estimate_palette_local_cost(cand_p, cand_idx);
            if (cost < best_cost) {
                best_cost = cost;
                best_p = cand_p;
                best_idx = cand_idx;
                changed = true;
            }
        }

        // Candidate 3: Value Descending
        {
            std::array<int, 8> order = {0, 1, 2, 3, 4, 5, 6, 7};
            std::sort(order.begin(), order.begin() + p.size, [&](int a, int b){
                return p.colors[a] > p.colors[b];
            });
            
            Palette cand_p = p;
            std::vector<uint8_t> cand_idx = idx;
            reorder_palette_and_indices(cand_p, cand_idx, order.data(), (size_t)p.size);
            uint32_t cost = estimate_palette_local_cost(cand_p, cand_idx);
            if (cost < best_cost) {
                best_cost = cost;
                best_p = cand_p;
                best_idx = cand_idx;
                changed = true;
            }
        }
        
        // Full search for small sizes
        if (p.size == 3) {
             std::array<int, 3> order = {0, 1, 2};
             do {
                 Palette cand_p = p;
                 std::vector<uint8_t> cand_idx = idx;
                 reorder_palette_and_indices(cand_p, cand_idx, order.data(), 3);
                 uint32_t cost = estimate_palette_local_cost(cand_p, cand_idx);
                 if (cost < best_cost) {
                     best_cost = cost;
                     best_p = cand_p;
                     best_idx = cand_idx;
                     changed = true;
                 }
             } while (std::next_permutation(order.begin(), order.end()));
        } else if (p.size == 4) {
             std::array<int, 4> order = {0, 1, 2, 3};
             do {
                 Palette cand_p = p;
                 std::vector<uint8_t> cand_idx = idx;
                 reorder_palette_and_indices(cand_p, cand_idx, order.data(), 4);
                 uint32_t cost = estimate_palette_local_cost(cand_p, cand_idx);
                 if (cost < best_cost) {
                     best_cost = cost;
                     best_p = cand_p;
                     best_idx = cand_idx;
                     changed = true;
                 }
             } while (std::next_permutation(order.begin(), order.end()));
        }

        // Commit best
        if (changed) {
            p = best_p;
            idx = best_idx;
            adopted++;
        }
    }
};

} // namespace hakonyans
