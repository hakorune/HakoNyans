template <typename T>
T median_value(std::vector<T> v) {
    if (v.empty()) return T{};
    std::sort(v.begin(), v.end());
    size_t n = v.size();
    if ((n & 1u) != 0u) return v[n / 2];
    return (T)((v[n / 2 - 1] + v[n / 2]) / (T)2);
}

template <>
inline double median_value(std::vector<double> v) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    size_t n = v.size();
    if ((n & 1u) != 0u) return v[n / 2];
    return 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

inline double ns_to_ms(uint64_t ns) {
    return (double)ns / 1000000.0;
}

inline bool parse_int_arg(const std::string& s, int* out) {
    try {
        size_t idx = 0;
        int v = std::stoi(s, &idx);
        if (idx != s.size()) return false;
        *out = v;
        return true;
    } catch (...) {
        return false;
    }
}

inline bool parse_lossless_preset_arg(
    const std::string& s, hakonyans::GrayscaleEncoder::LosslessPreset* out
) {
    std::string lower;
    lower.reserve(s.size());
    for (char c : s) {
        lower.push_back((char)std::tolower((unsigned char)c));
    }
    if (lower == "fast") {
        *out = hakonyans::GrayscaleEncoder::LosslessPreset::FAST;
        return true;
    }
    if (lower == "balanced") {
        *out = hakonyans::GrayscaleEncoder::LosslessPreset::BALANCED;
        return true;
    }
    if (lower == "max") {
        *out = hakonyans::GrayscaleEncoder::LosslessPreset::MAX;
        return true;
    }
    return false;
}

inline Args parse_args(int argc, char** argv) {
    Args args;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--base-dir" && i + 1 < argc) {
            args.base_dir = argv[++i];
        } else if (a == "--out" && i + 1 < argc) {
            args.out_csv = argv[++i];
        } else if (a == "--baseline" && i + 1 < argc) {
            args.baseline_csv = argv[++i];
        } else if (a == "--runs" && i + 1 < argc) {
            int v = 0;
            if (!parse_int_arg(argv[++i], &v) || v <= 0) {
                throw std::runtime_error("--runs must be a positive integer");
            }
            args.runs = v;
        } else if (a == "--warmup" && i + 1 < argc) {
            int v = 0;
            if (!parse_int_arg(argv[++i], &v) || v < 0) {
                throw std::runtime_error("--warmup must be a non-negative integer");
            }
            args.warmup = v;
        } else if (a == "--preset" && i + 1 < argc) {
            hakonyans::GrayscaleEncoder::LosslessPreset preset{};
            if (!parse_lossless_preset_arg(argv[++i], &preset)) {
                throw std::runtime_error("--preset must be one of: fast, balanced, max");
            }
            args.preset = preset;
        } else if (a == "--help" || a == "-h") {
            std::cout << "Usage: " << argv[0]
                      << " [--base-dir DIR] [--out CSV] [--baseline CSV] [--runs N] [--warmup N] [--preset fast|balanced|max]\n";
            std::exit(0);
        } else {
            throw std::runtime_error("Unknown argument: " + a);
        }
    }
    return args;
}

