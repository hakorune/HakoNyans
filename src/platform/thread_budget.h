#pragma once

#include <algorithm>
#include <cstdlib>
#include <thread>

namespace hakonyans::thread_budget {

inline unsigned int hardware_threads() {
    unsigned int n = std::thread::hardware_concurrency();
    if (n == 0) n = 4;
    return n;
}

inline unsigned int configured_threads() {
    static const unsigned int kConfigured = []() {
        unsigned int n = hardware_threads();
        const char* env = std::getenv("HAKONYANS_THREADS");
        if (!env || env[0] == '\0') return n;

        char* end = nullptr;
        long parsed = std::strtol(env, &end, 10);
        if (end == env || parsed <= 0) return n;
        if (parsed > 256) parsed = 256;
        return (unsigned int)parsed;
    }();
    return kConfigured;
}

inline unsigned int max_threads(unsigned int cap = 0) {
    unsigned int n = configured_threads();
    if (cap > 0) n = std::min(n, cap);
    return std::max(1u, n);
}

inline thread_local unsigned int tl_parallel_depth = 0;

inline bool in_parallel_region() {
    return tl_parallel_depth > 0;
}

class ScopedParallelRegion {
public:
    ScopedParallelRegion() { tl_parallel_depth++; }
    ~ScopedParallelRegion() {
        if (tl_parallel_depth > 0) tl_parallel_depth--;
    }
    ScopedParallelRegion(const ScopedParallelRegion&) = delete;
    ScopedParallelRegion& operator=(const ScopedParallelRegion&) = delete;
};

inline bool can_spawn(unsigned int needed_threads = 2) {
    if (needed_threads <= 1) return true;
    if (in_parallel_region()) return false;
    return max_threads() >= needed_threads;
}

}  // namespace hakonyans::thread_budget
