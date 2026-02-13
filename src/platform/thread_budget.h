#pragma once

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <thread>

namespace hakonyans::thread_budget {

inline bool env_bool(const char* key, bool fallback = false) {
    const char* env = std::getenv(key);
    if (!env || env[0] == '\0') return fallback;
    const char c = env[0];
    if (c == '0' || c == 'f' || c == 'F' || c == 'n' || c == 'N') return false;
    if (c == '1' || c == 't' || c == 'T' || c == 'y' || c == 'Y') return true;
    return fallback;
}

inline unsigned int env_uint(const char* key, unsigned int fallback, unsigned int min_v, unsigned int max_v) {
    const char* env = std::getenv(key);
    if (!env || env[0] == '\0') return fallback;
    char* end = nullptr;
    long parsed = std::strtol(env, &end, 10);
    if (end == env || parsed < (long)min_v) return fallback;
    if (parsed > (long)max_v) parsed = (long)max_v;
    return (unsigned int)parsed;
}

inline unsigned int hardware_threads() {
    unsigned int n = std::thread::hardware_concurrency();
    if (n == 0) n = 4;
    return n;
}

inline unsigned int configured_threads() {
    static const unsigned int kConfigured = []() {
        unsigned int n = hardware_threads();
        const unsigned int env_threads = env_uint("HAKONYANS_THREADS", 0, 1, 256);
        if (env_threads > 0) n = env_threads;

        // Opt-in batch mode:
        // split total CPU budget by outer worker count to avoid oversubscription.
        // Example:
        //   HAKONYANS_AUTO_INNER_THREADS=1 HAKONYANS_OUTER_WORKERS=8
        if (env_bool("HAKONYANS_AUTO_INNER_THREADS", false)) {
            const unsigned int outer_workers = env_uint("HAKONYANS_OUTER_WORKERS", 1, 1, 256);
            if (outer_workers > 1) {
                n = std::max(1u, n / outer_workers);
            }
            const unsigned int inner_cap = env_uint("HAKONYANS_INNER_THREADS_CAP", 0, 1, 256);
            if (inner_cap > 0) {
                n = std::min(n, inner_cap);
            }
        }

        return n;
    }();
    return kConfigured;
}

inline unsigned int max_threads(unsigned int cap = 0) {
    unsigned int n = configured_threads();
    if (cap > 0) n = std::min(n, cap);
    return std::max(1u, n);
}

inline int max_worker_tokens() {
    const unsigned int threads = max_threads();
    if (threads <= 1) return 0;
    return (int)(threads - 1);
}

inline std::atomic<int>& available_worker_tokens() {
    static std::atomic<int> kAvailable(max_worker_tokens());
    return kAvailable;
}

inline int available_tokens() {
    return std::max(0, available_worker_tokens().load(std::memory_order_relaxed));
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

class ScopedThreadTokens {
public:
    ScopedThreadTokens() = default;
    ~ScopedThreadTokens() { release(); }

    ScopedThreadTokens(ScopedThreadTokens&& other) noexcept : tokens_(other.tokens_) {
        other.tokens_ = 0;
    }
    ScopedThreadTokens& operator=(ScopedThreadTokens&& other) noexcept {
        if (this == &other) return *this;
        release();
        tokens_ = other.tokens_;
        other.tokens_ = 0;
        return *this;
    }

    ScopedThreadTokens(const ScopedThreadTokens&) = delete;
    ScopedThreadTokens& operator=(const ScopedThreadTokens&) = delete;

    static ScopedThreadTokens try_acquire_exact(unsigned int needed_threads) {
        if (needed_threads == 0) return {};
        auto& pool = available_worker_tokens();
        int need = (int)needed_threads;
        int cur = pool.load(std::memory_order_relaxed);
        while (cur >= need) {
            if (pool.compare_exchange_weak(
                    cur,
                    cur - need,
                    std::memory_order_acq_rel,
                    std::memory_order_relaxed)) {
                return ScopedThreadTokens(needed_threads);
            }
        }
        return {};
    }

    static ScopedThreadTokens try_acquire_up_to(
        unsigned int max_needed_threads,
        unsigned int min_needed_threads = 1
    ) {
        if (max_needed_threads == 0) return {};
        if (min_needed_threads == 0) min_needed_threads = 1;
        if (max_needed_threads < min_needed_threads) return {};

        auto& pool = available_worker_tokens();
        int cur = pool.load(std::memory_order_relaxed);
        while (true) {
            int want = std::min<int>((int)max_needed_threads, cur);
            if (want < (int)min_needed_threads) return {};
            if (pool.compare_exchange_weak(
                    cur,
                    cur - want,
                    std::memory_order_acq_rel,
                    std::memory_order_relaxed)) {
                return ScopedThreadTokens((unsigned int)want);
            }
        }
    }

    bool acquired() const { return tokens_ > 0; }
    unsigned int count() const { return tokens_; }

private:
    explicit ScopedThreadTokens(unsigned int tokens) : tokens_(tokens) {}

    void release() {
        if (tokens_ == 0) return;
        available_worker_tokens().fetch_add((int)tokens_, std::memory_order_acq_rel);
        tokens_ = 0;
    }

    unsigned int tokens_ = 0;
};

inline bool can_spawn(unsigned int needed_threads = 2) {
    if (needed_threads <= 1) return true;
    return available_tokens() >= (int)needed_threads;
}

}  // namespace hakonyans::thread_budget
