#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <chrono>
#include <stdexcept>
#include "../src/codec/encode.h"
#include "../src/codec/decode.h"

using namespace hakonyans;

/**
 * Load PPM P6 (binary RGB)
 */
std::vector<uint8_t> load_ppm(const char* path, int& w, int& h) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("Failed to open PPM for reading");
    
    std::string magic;
    f >> magic;
    if (magic != "P6") throw std::runtime_error("Not a PPM P6 file");
    
    // Skip whitespace and comments
    char c = f.get();
    while (isspace(c)) c = f.get();
    if (c == '#') {
        while (c != '\n' && c != EOF) c = f.get();
        // After comment, there might be more whitespace or another comment
        return load_ppm(path, w, h); // Recursively call to skip more (not efficient but simple)
        // Actually let's just use a better skip logic
    }
    f.unget();
    
    f >> w >> h;
    int maxval;
    f >> maxval;
    f.get(); // skip newline
    
    if (maxval != 255) throw std::runtime_error("Only 8-bit PPM supported");
    
    std::vector<uint8_t> rgb(w * h * 3);
    f.read(reinterpret_cast<char*>(rgb.data()), w * h * 3);
    return rgb;
}

// Improved skip logic for PPM
void skip_ppm_comments(std::ifstream& f) {
    char c;
    while (f.get(c)) {
        if (isspace(c)) continue;
        if (c == '#') {
            while (f.get(c) && c != '\n');
            continue;
        }
        f.unget();
        break;
    }
}

std::vector<uint8_t> load_ppm_fixed(const char* path, int& w, int& h) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("Failed to open PPM for reading");
    
    std::string magic;
    f >> magic;
    if (magic != "P6") throw std::runtime_error("Not a PPM P6 file");
    
    skip_ppm_comments(f);
    f >> w;
    skip_ppm_comments(f);
    f >> h;
    skip_ppm_comments(f);
    int maxval;
    f >> maxval;
    f.get(); // skip single whitespace after maxval
    
    if (maxval != 255) throw std::runtime_error("Only 8-bit PPM supported");
    
    std::vector<uint8_t> rgb(w * h * 3);
    f.read(reinterpret_cast<char*>(rgb.data()), w * h * 3);
    return rgb;
}

/**
 * Save PPM P6
 */
void save_ppm(const char* path, const uint8_t* rgb, int w, int h) {
    std::ofstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("Failed to open PPM for writing");
    f << "P6\n" << w << " " << h << "\n255\n";
    f.write(reinterpret_cast<const char*>(rgb), w * h * 3);
}

void print_usage() {
    std::cout << "HakoNyans CLI v0.2\n"
              << "Usage:\n"
              << "  hakonyans encode <input.ppm> <output.hkn> [quality]\n"
              << "  hakonyans decode <input.hkn> <output.ppm>\n"
              << "  hakonyans info <input.hkn>\n";
}

int main(int argc, char** argv) {
    if (argc < 3) {
        print_usage();
        return 1;
    }
    
    std::string cmd = argv[1];
    
    try {
        if (cmd == "encode") {
            int w, h;
            int quality = (argc > 4) ? std::stoi(argv[4]) : 75;
            
            std::cout << "Loading " << argv[2] << "..." << std::endl;
            auto rgb = load_ppm_fixed(argv[2], w, h);
            
            std::cout << "Encoding (" << w << "x" << h << ", Q=" << quality << ")..." << std::endl;
            auto start = std::chrono::high_resolution_clock::now();
            auto hkn = GrayscaleEncoder::encode_color(rgb.data(), w, h, quality);
            auto end = std::chrono::high_resolution_clock::now();
            
            auto ms = std::chrono::duration<double, std::milli>(end - start).count();
            std::cout << "Encoded in " << ms << " ms (" << (w*h*3 / (ms/1000.0) / (1024*1024)) << " MiB/s)" << std::endl;
            std::cout << "Saving to " << argv[3] << " (" << hkn.size() << " bytes)..." << std::endl;
            
            std::ofstream out(argv[3], std::ios::binary);
            out.write(reinterpret_cast<const char*>(hkn.data()), hkn.size());
            
        } else if (cmd == "decode") {
            std::ifstream in(argv[2], std::ios::binary | std::ios::ate);
            if (!in) throw std::runtime_error("Failed to open HKN for reading");
            size_t size = in.tellg();
            in.seekg(0, std::ios::beg);
            std::vector<uint8_t> hkn(size);
            in.read(reinterpret_cast<char*>(hkn.data()), size);
            
            std::cout << "Decoding " << argv[2] << "..." << std::endl;
            int w, h;
            auto start = std::chrono::high_resolution_clock::now();
            auto rgb = GrayscaleDecoder::decode_color(hkn, w, h);
            auto end = std::chrono::high_resolution_clock::now();
            
            auto ms = std::chrono::duration<double, std::milli>(end - start).count();
            std::cout << "Decoded in " << ms << " ms (" << (w*h*3 / (ms/1000.0) / (1024*1024)) << " MiB/s)" << std::endl;
            
            std::cout << "Saving to " << argv[3] << "..." << std::endl;
            save_ppm(argv[3], rgb.data(), w, h);
            
        } else if (cmd == "info") {
            std::ifstream in(argv[2], std::ios::binary);
            if (!in) throw std::runtime_error("Failed to open HKN for reading");
            uint8_t head[48];
            in.read(reinterpret_cast<char*>(head), 48);
            auto header = FileHeader::read(head);
            
            if (!header.is_valid()) throw std::runtime_error("Invalid HKN file");
            
            std::cout << "HKN File Info: " << argv[2] << "\n"
                      << "  Dimensions: " << header.width << "x" << header.height << "\n"
                      << "  Channels:   " << (int)header.num_channels << "\n"
                      << "  Quality:    " << (int)header.quality << "\n"
                      << "  Colorspace: " << (int)header.colorspace << " (0:YCbCr, 2:Gray)\n";
        } else {
            print_usage();
            return 1;
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}