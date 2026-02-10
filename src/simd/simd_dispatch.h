#pragma once

#include <cstdint>
#include <cstdlib>

namespace hakonyans {

/**
 * SIMD 機能検出とディスパッチ
 */
struct SIMDCapabilities {
    bool avx2 = false;
    bool avx512f = false;
    bool neon = false;
    bool force_scalar = false;  // HAKONYANS_FORCE_SCALAR 環境変数
};

inline SIMDCapabilities detect_simd() {
    SIMDCapabilities caps;
    
    // 環境変数チェック
    caps.force_scalar = (std::getenv("HAKONYANS_FORCE_SCALAR") != nullptr);
    
#if defined(__x86_64__) || defined(_M_X64)
    // CPUID で AVX2/AVX-512 検出
    uint32_t eax, ebx, ecx, edx;
    
    // CPUID leaf 7: Extended Features
    __asm__ __volatile__(
        "cpuid"
        : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
        : "a"(7), "c"(0));
    
    caps.avx2 = (ebx >> 5) & 1;       // bit 5 of EBX
    caps.avx512f = (ebx >> 16) & 1;   // bit 16 of EBX
    
#elif defined(__aarch64__) || defined(_M_ARM64)
    caps.neon = true;  // AArch64 always has NEON
#endif
    
    return caps;
}

/**
 * 使うべきデコードパスを判定
 */
enum class DecodePath {
    Scalar,
    ScalarLUT,
    AVX2,
    AVX512,
    NEON,
};

inline DecodePath select_decode_path() {
    auto caps = detect_simd();
    
    if (caps.force_scalar) return DecodePath::Scalar;
    
#ifdef __AVX2__
    if (caps.avx2) return DecodePath::AVX2;
#endif
    
    // LUT はどのプラットフォームでも使える
    return DecodePath::ScalarLUT;
}

inline const char* decode_path_name(DecodePath path) {
    switch (path) {
        case DecodePath::Scalar:    return "Scalar";
        case DecodePath::ScalarLUT: return "Scalar+LUT";
        case DecodePath::AVX2:      return "AVX2";
        case DecodePath::AVX512:    return "AVX-512";
        case DecodePath::NEON:      return "NEON";
    }
    return "Unknown";
}

} // namespace hakonyans
