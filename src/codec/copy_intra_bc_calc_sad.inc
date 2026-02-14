        int sad = 0;
        for (int y = 0; y < 8; y++) {
            const uint8_t* row = img + (ry + y) * stride + rx;
            for (int x = 0; x < 8; x++) {
                sad += std::abs((int)cur[y * 8 + x] - (int)row[x]);
            }
        }
        return sad;
