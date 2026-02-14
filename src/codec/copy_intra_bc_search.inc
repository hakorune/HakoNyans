        int best_sad = 255 * 64 * 64; // Max possible SAD
        out_params.dx = 0;
        out_params.dy = 0;

        // Block position in pixels
        int cur_x = bx * 8;
        int cur_y = by * 8;

        // Extract current block for faster comparison
        uint8_t cur_block[64];
        for (int y = 0; y < 8; y++) {
            for (int x = 0; x < 8; x++) {
                cur_block[y * 8 + x] = full_img[(cur_y + y) * stride + (cur_x + x)];
            }
        }

        // Valid search area:
        // We can copy from any pixel (rx, ry) such that:
        // 1. (rx, ry) is within image bounds (0..width-1, 0..height-1)
        // 2. The 8x8 block at (rx, ry) is "already decoded".
        //    In sequential decode, this means (ry < cur_y) OR (ry == cur_y && rx < cur_x).
        //    Actually, we need the whole 8x8 block to be available.
        //    So (ry + 7 < cur_y) is strictly safe?
        //    No, usually we define "available" by top-left corner causality + Delay?
        //    For standard IntraBC:
        //    The reference block vectors must point to pixels that have been reconstructed.
        //    Since we decode block-by-block, if we copy from (rx, ry), we need pixels at (rx..rx+7, ry..ry+7) to be ready.
        //    If we limit search to "Completed Blocks", i.e. Reference Block Index < Current Block Index.
        //    Let's use a simpler constraint:
        //    Search center (rx, ry) within [cur_x - radius, cur_x + radius] x [cur_y - radius, cur_y].
        //    AND check if the block index of (rx, ry) is < current.
        //    Actually, simple "Left and Top" search is enough for text.

        // Search loops
        // Optimization: Spiral search? Or just raster scan in window.
        // Search window:
        //   y: [cur_y - search_radius, cur_y]
        //   x: [cur_x - search_radius, cur_x + search_radius] (but limited by y)

        int start_y = std::max(0, cur_y - search_radius);
        int end_y = cur_y; // Can go up to cur_y?
                           // If ry == cur_y, rx must be < cur_x - some_delay?
                           // Let's assume we can copy from strictly left pixels?
                           // rx <= cur_x - 8 is safe (previous block).

        // For simplicity and safety in V1:
        // Only search completely "past" blocks.
        // Top area: y <= cur_y - 8
        // Left area: y == cur_y, x <= cur_x - 8

        // 1. Scan Top area
        for (int ry = std::max(0, cur_y - search_radius); ry <= cur_y - 8; ry++) {
             for (int rx = std::max(0, cur_x - search_radius); rx < std::min((int)stride - 8, cur_x + search_radius); rx++) {
                 int sad = calc_sad(cur_block, full_img, stride, rx, ry);
                 if (sad < best_sad) {
                     best_sad = sad;
                     out_params.dx = (int16_t)(rx - cur_x);
                     out_params.dy = (int16_t)(ry - cur_y);
                     if (sad == 0) return 0; // Perfect match found
                 }
             }
        }

        // 2. Scan Left area (Same row)
        // Only scan if left is available
        int ry = cur_y;
        for (int rx = std::max(0, cur_x - search_radius); rx <= cur_x - 8; rx++) {
             int sad = calc_sad(cur_block, full_img, stride, rx, ry);
             if (sad < best_sad) {
                 best_sad = sad;
                 out_params.dx = (int16_t)(rx - cur_x);
                 out_params.dy = (int16_t)(ry - cur_y);
                 if (sad == 0) return 0;
             }
        }

        return best_sad;
