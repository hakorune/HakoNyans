#include <iostream>
#include <fstream>
#include <vector>
#include <cmath>
#include <string>
#include <cstdint>

void save_ppm(const std::string& filename, int w, int h, const std::vector<uint8_t>& rgb) {
    std::ofstream ofs(filename, std::ios::binary);
    ofs << "P6\n" << w << " " << h << "\n255\n";
    ofs.write(reinterpret_cast<const char*>(rgb.data()), rgb.size());
    std::cout << "Generated: " << filename << " (" << w << "x" << h << ")" << std::endl;
}

int main() {
    const int W = 1920;
    const int H = 1080; // Full HD
    
    // 1. Natural-like Gradient
    {
        std::vector<uint8_t> rgb(W * H * 3);
        for (int y = 0; y < H; y++) {
            for (int x = 0; x < W; x++) {
                int i = (y * W + x) * 3;
                rgb[i+0] = (uint8_t)(std::sin(x * 0.01) * 127 + 128);
                rgb[i+1] = (uint8_t)(std::cos(y * 0.01) * 127 + 128);
                rgb[i+2] = (uint8_t)((x + y) % 256);
            }
        }
        save_ppm("test_images/kodak/hd_01.ppm", W, H, rgb);
    }
    return 0;
}
