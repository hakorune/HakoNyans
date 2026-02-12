#pragma once

#include <algorithm>
#include <atomic>
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
