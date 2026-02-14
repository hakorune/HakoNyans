#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <chrono>
#include <stdexcept>
#include <cctype>
#include "../src/codec/encode.h"
#include "../src/codec/decode.h"

using namespace hakonyans;

// ... load_ppm, skip_ppm_comments, load_ppm_fixed remain same ...
void skip_ppm_comments(std::ifstream& f) {
    char c;
    while (f.get(c)) {
        if (std::isspace(static_cast<unsigned char>(c))) continue;
        if (c == '#') { while (f.get(c) && c != '\n'); continue; }
        f.unget(); break;
    }
}

std::vector<uint8_t> load_ppm_fixed(const char* path, int& w, int& h) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("Failed to open PPM for reading");
    std::string magic; f >> magic;
    if (magic != "P6") throw std::runtime_error("Not a PPM P6 file");
    skip_ppm_comments(f); f >> w;
    skip_ppm_comments(f); f >> h;
    skip_ppm_comments(f);
    int maxval; f >> maxval; f.get();
    if (maxval != 255) throw std::runtime_error("Only 8-bit PPM supported");
    std::vector<uint8_t> rgb(w * h * 3);
    f.read(reinterpret_cast<char*>(rgb.data()), w * h * 3);
    return rgb;
}

void save_ppm(const char* path, const uint8_t* rgb, int w, int h) {
    std::ofstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("Failed to open PPM for writing");
    f << "P6\n" << w << " " << h << "\n255\n";
    f.write(reinterpret_cast<const char*>(rgb), w * h * 3);
}

void print_usage() {
    std::cout << "HakoNyans CLI v0.4 (Phase 7c)\n"
              << "Usage:\n"
              << "  hakonyans encode <in.ppm> <out.hkn> [quality] [subsampling: 0=444, 1=420] [cfl: 0, 1] [screen_prof: 0, 1]\n"
              << "  hakonyans encode-lossless <in.ppm> <out.hkn> [preset: fast|balanced|max]\n"
              << "  hakonyans decode <in.hkn> <out.ppm>\n"
              << "  hakonyans info <in.hkn>\n"
              << "  hakonyans compare <in.ppm> <out_dir> - encode with/without screen profile and compare\n";
}

bool parse_lossless_preset(const std::string& raw, GrayscaleEncoder::LosslessPreset& out) {
    std::string s = raw;
    for (char& ch : s) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    if (s == "fast" || s == "0") {
        out = GrayscaleEncoder::LosslessPreset::FAST;
        return true;
    }
    if (s == "balanced" || s == "1") {
        out = GrayscaleEncoder::LosslessPreset::BALANCED;
        return true;
    }
    if (s == "max" || s == "2") {
        out = GrayscaleEncoder::LosslessPreset::MAX;
        return true;
    }
    return false;
}

int main(int argc, char** argv) {
    if (argc < 3) { print_usage(); return 1; }
    std::string cmd = argv[1];
    try {
        if (cmd == "encode") {
            int w, h;
            int quality = (argc > 4) ? std::stoi(argv[4]) : 75;
            bool use_420 = (argc > 5) ? (std::stoi(argv[5]) == 1) : true;
            bool use_cfl = (argc > 6) ? (std::stoi(argv[6]) == 1) : true;
            bool enable_screen_profile = (argc > 7) ? (std::stoi(argv[7]) == 1) : false;
            auto rgb = load_ppm_fixed(argv[2], w, h);
            std::cout << "Encoding (" << w << "x" << h << ", Q=" << quality << ", 420=" << use_420 << ", CfL=" << use_cfl << ", ScreenProf=" << enable_screen_profile << ")..." << std::endl;
            auto start = std::chrono::high_resolution_clock::now();
            auto hkn = GrayscaleEncoder::encode_color(rgb.data(), w, h, quality, use_420, use_cfl, enable_screen_profile);
            auto end = std::chrono::high_resolution_clock::now();
            auto ms = std::chrono::duration<double, std::milli>(end - start).count();
            std::cout << "Encoded in " << ms << " ms (" << (w*h*3 / (ms/1000.0) / (1024*1024)) << " MiB/s)" << std::endl;
            std::cout << "Saving to " << argv[3] << " (" << hkn.size() << " bytes)..." << std::endl;
            std::ofstream out(argv[3], std::ios::binary);
            out.write(reinterpret_cast<const char*>(hkn.data()), hkn.size());
        } else if (cmd == "encode-lossless") {
            if (argc < 4) { print_usage(); return 1; }
            int w, h;
            GrayscaleEncoder::LosslessPreset preset = GrayscaleEncoder::LosslessPreset::BALANCED;
            if (argc > 4) {
                if (std::string(argv[4]) == "--preset") {
                    if (argc < 6) {
                        throw std::runtime_error("Missing value for --preset (fast|balanced|max)");
                    }
                    if (!parse_lossless_preset(argv[5], preset)) {
                        throw std::runtime_error("Invalid preset. Use fast|balanced|max");
                    }
                } else {
                    if (!parse_lossless_preset(argv[4], preset)) {
                        throw std::runtime_error("Invalid preset. Use fast|balanced|max");
                    }
                }
            }

            auto rgb = load_ppm_fixed(argv[2], w, h);
            std::cout << "Lossless encoding (" << w << "x" << h
                      << ", preset=" << GrayscaleEncoder::lossless_preset_name(preset)
                      << ")..." << std::endl;
            auto start = std::chrono::high_resolution_clock::now();
            auto hkn = GrayscaleEncoder::encode_color_lossless(
                rgb.data(), static_cast<uint32_t>(w), static_cast<uint32_t>(h), preset
            );
            auto end = std::chrono::high_resolution_clock::now();
            auto ms = std::chrono::duration<double, std::milli>(end - start).count();
            std::cout << "Encoded in " << ms << " ms ("
                      << (w*h*3 / (ms/1000.0) / (1024*1024))
                      << " MiB/s)" << std::endl;
            std::cout << "Saving to " << argv[3] << " (" << hkn.size() << " bytes)..." << std::endl;
            std::ofstream out(argv[3], std::ios::binary);
            out.write(reinterpret_cast<const char*>(hkn.data()), hkn.size());
        } else if (cmd == "decode") {
            std::ifstream in(argv[2], std::ios::binary | std::ios::ate);
            if (!in) throw std::runtime_error("Failed to open HKN for reading");
            size_t size = in.tellg(); in.seekg(0, std::ios::beg);
            std::vector<uint8_t> hkn(size); in.read(reinterpret_cast<char*>(hkn.data()), size);
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
            uint8_t head[48]; in.read(reinterpret_cast<char*>(head), 48);
            auto header = FileHeader::read(head);
            if (!header.is_valid()) throw std::runtime_error("Invalid HKN file");
            std::cout << "HKN File Info: " << argv[2] << "\n"
                      << "  Dimensions:  " << header.width << "x" << header.height << "\n"
                      << "  Channels:    " << (int)header.num_channels << "\n"
                      << "  Quality:     " << (int)header.quality << "\n"
                      << "  Subsampling: " << ((header.subsampling == 1) ? "4:2:0" : "4:4:4") << "\n"
                      << "  Flags:       " << header.flags << " (CfL=" << ((header.flags & 2) ? "on" : "off") << ")\n";
        } else if (cmd == "compare") {
            if (argc < 4) { print_usage(); return 1; }
            int w, h;
            auto rgb = load_ppm_fixed(argv[2], w, h);
            std::string out_dir = argv[3];

            // Create output directory
            std::string mkdir_cmd = "mkdir -p \"" + out_dir + "\"";
            system(mkdir_cmd.c_str());

            // Extract base name from input path
            std::string in_path = argv[2];
            size_t last_slash = in_path.find_last_of("/\\");
            std::string base_name = (last_slash != std::string::npos) ? in_path.substr(last_slash + 1) : in_path;
            size_t dot_pos = base_name.find_last_of(".");
            if (dot_pos != std::string::npos) base_name = base_name.substr(0, dot_pos);

            // Encode baseline
            std::cout << "=== Encoding baseline (Screen Profile disabled) ===" << std::endl;
            auto hkn_baseline = GrayscaleEncoder::encode_color(rgb.data(), w, h, 75, true, true, false);
            std::string hkn_baseline_path = out_dir + "/" + base_name + "_baseline.hkn";
            std::ofstream out_baseline(hkn_baseline_path, std::ios::binary);
            out_baseline.write(reinterpret_cast<const char*>(hkn_baseline.data()), hkn_baseline.size());
            std::cout << "Saved: " << hkn_baseline_path << " (" << hkn_baseline.size() << " bytes)" << std::endl;

            // Decode baseline
            int dec_w, dec_h;
            auto rgb_baseline = GrayscaleDecoder::decode_color(hkn_baseline, dec_w, dec_h);
            std::string ppm_baseline_path = out_dir + "/" + base_name + "_baseline.ppm";
            save_ppm(ppm_baseline_path.c_str(), rgb_baseline.data(), dec_w, dec_h);
            std::cout << "Saved: " << ppm_baseline_path << std::endl;

            // Encode with Screen Profile
            std::cout << "\n=== Encoding with Screen Profile ===" << std::endl;
            auto hkn_screen = GrayscaleEncoder::encode_color(rgb.data(), w, h, 75, true, true, true);
            std::string hkn_screen_path = out_dir + "/" + base_name + "_screen.hkn";
            std::ofstream out_screen(hkn_screen_path, std::ios::binary);
            out_screen.write(reinterpret_cast<const char*>(hkn_screen.data()), hkn_screen.size());
            std::cout << "Saved: " << hkn_screen_path << " (" << hkn_screen.size() << " bytes)" << std::endl;

            // Decode screen profile
            auto rgb_screen = GrayscaleDecoder::decode_color(hkn_screen, dec_w, dec_h);
            std::string ppm_screen_path = out_dir + "/" + base_name + "_screen.ppm";
            save_ppm(ppm_screen_path.c_str(), rgb_screen.data(), dec_w, dec_h);
            std::cout << "Saved: " << ppm_screen_path << std::endl;

            // Summary
            double size_ratio = 100.0 * ((double)hkn_screen.size() / hkn_baseline.size() - 1.0);
            std::cout << "\n=== Summary ===" << std::endl;
            std::cout << "Baseline:    " << hkn_baseline.size() << " bytes" << std::endl;
            std::cout << "Screen Prof: " << hkn_screen.size() << " bytes (" << (size_ratio >= 0 ? "+" : "") << size_ratio << "%)" << std::endl;

            // Try to convert to PNG if ImageMagick is available
            std::cout << "\n=== Converting to PNG ===" << std::endl;
            std::string png_baseline_path = out_dir + "/" + base_name + "_baseline.png";
            std::string png_screen_path = out_dir + "/" + base_name + "_screen.png";
            int rc1 = system(("convert \"" + ppm_baseline_path + "\" \"" + png_baseline_path + "\" 2>/dev/null || magick convert \"" + ppm_baseline_path + "\" \"" + png_baseline_path + "\" 2>/dev/null").c_str());
            int rc2 = system(("convert \"" + ppm_screen_path + "\" \"" + png_screen_path + "\" 2>/dev/null || magick convert \"" + ppm_screen_path + "\" \"" + png_screen_path + "\" 2>/dev/null").c_str());

            if (rc1 == 0 && rc2 == 0) {
                std::cout << "PNG files created:" << std::endl;
                std::cout << "  " << png_baseline_path << std::endl;
                std::cout << "  " << png_screen_path << std::endl;
            } else {
                std::cout << "ImageMagick not available. PPM files saved instead." << std::endl;
                std::cout << "To convert manually: convert " << ppm_baseline_path << " " << png_baseline_path << std::endl;
            }
        } else { print_usage(); return 1; }
    } catch (const std::exception& e) { std::cerr << "Error: " << e.what() << std::endl; return 1; }
    return 0;
}
