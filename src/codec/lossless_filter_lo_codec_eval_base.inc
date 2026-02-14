    ThreadPool& worker_pool = lo_codec_worker_pool();
    if (lo_bytes.empty()) return {};
    using Clock = std::chrono::steady_clock;
    auto ns_since = [](const Clock::time_point& t0, const Clock::time_point& t1) -> uint64_t {
        return (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
    };

    const unsigned int hw_threads = thread_budget::max_threads();
    thread_budget::ScopedThreadTokens base_parallel_tokens;
    if (hw_threads >= 4 && lo_bytes.size() >= 4096) {
        base_parallel_tokens = thread_budget::ScopedThreadTokens::try_acquire_exact(2);
    }
    const bool allow_parallel_base = base_parallel_tokens.acquired();

    std::vector<uint8_t> lo_legacy;
    std::vector<uint8_t> lo_lz;
    std::future<std::vector<uint8_t>> fut_legacy;
    std::future<std::vector<uint8_t>> fut_lz;

    // DOC: docs/LOSSLESS_FLOW_MAP.md#filter-lo-lz-probe
    bool evaluate_lz = true;
    if (enable_lz_probe) {
        if (stats) stats->filter_lo_lz_probe_enabled++;
        const auto& probe = get_lz_probe_runtime_params();
        if (lo_bytes.size() >= (size_t)probe.min_raw_bytes) {
            if (stats) stats->filter_lo_lz_probe_checked++;
            const size_t probe_n = std::min(lo_bytes.size(), (size_t)probe.sample_bytes);
            std::vector<uint8_t> sample(lo_bytes.begin(), lo_bytes.begin() + probe_n);
            auto sample_lz = compress_lz(sample);
            const size_t sample_wrapped = 6 + sample_lz.size();
            if (stats) {
                stats->filter_lo_lz_probe_sample_bytes_sum += probe_n;
                stats->filter_lo_lz_probe_sample_lz_bytes_sum += sample_lz.size();
                stats->filter_lo_lz_probe_sample_wrapped_bytes_sum += sample_wrapped;
            }
            if ((uint64_t)sample_wrapped * 1000ull >
                (uint64_t)probe_n * (uint64_t)probe.threshold_permille) {
                evaluate_lz = false;
                if (stats) stats->filter_lo_lz_probe_skip++;
            } else {
                if (stats) stats->filter_lo_lz_probe_pass++;
            }
        }
    }

    Clock::time_point t_mode2_eval0 = Clock::now();
    if (allow_parallel_base) {
        fut_legacy = worker_pool.submit([&]() {
            thread_budget::ScopedParallelRegion guard;
            return encode_byte_stream(lo_bytes);
        });
        if (evaluate_lz) {
            fut_lz = worker_pool.submit([&]() {
                thread_budget::ScopedParallelRegion guard;
                return compress_lz(lo_bytes);
            });
        }
    } else {
        lo_legacy = encode_byte_stream(lo_bytes);
    }

    if (stats) stats->filter_lo_raw_bytes_sum += lo_bytes.size();

    constexpr int kFilterLoModeWrapperGainPermilleDefault = 990;
    constexpr size_t kByteStreamMinEncodedBytes = 4 + (256 * 4) + 4 + 4; // cdf_size+cdf+count+rans_size

    // Mode5 runtime parameters (env-configurable)
    const auto& mode5_params = get_mode5_runtime_params();

    // Mode6 runtime parameters (env-configurable)
    const auto& mode6_params = get_mode6_runtime_params();
    const auto& mode7_params = get_mode7_runtime_params();

    std::vector<uint8_t> delta_bytes(lo_bytes.size());
    delta_bytes[0] = lo_bytes[0];
    for (size_t i = 1; i < lo_bytes.size(); i++) {
        delta_bytes[i] = (uint8_t)(lo_bytes[i] - lo_bytes[i - 1]);
    }
    auto delta_rans = encode_byte_stream(delta_bytes);
    size_t delta_wrapped = 6 + delta_rans.size();

    if (allow_parallel_base) {
        lo_legacy = fut_legacy.get();
        if (evaluate_lz) {
            lo_lz = fut_lz.get();
        }
    } else {
        if (evaluate_lz) {
            lo_lz = compress_lz(lo_bytes);
        }
    }
    if (stats) {
        stats->filter_lo_mode2_eval_ns += ns_since(t_mode2_eval0, Clock::now());
    }

    size_t legacy_size = lo_legacy.size();
    size_t lz_wrapped = std::numeric_limits<size_t>::max();
    if (evaluate_lz) {
        lz_wrapped = 6 + lo_lz.size();
    }

    std::vector<uint8_t> lo_lz_rans;
    size_t lz_rans_wrapped = std::numeric_limits<size_t>::max();
    if (evaluate_lz &&
        lo_bytes.size() >= (size_t)mode5_params.min_raw_bytes &&
        lo_lz.size() >= (size_t)mode5_params.min_lz_bytes) {
        if (stats) stats->filter_lo_mode5_candidates++;
        const auto t_mode5_eval0 = Clock::now();
        lo_lz_rans = encode_byte_stream_shared_lz(lo_lz);
        if (stats) stats->filter_lo_mode5_eval_ns += ns_since(t_mode5_eval0, Clock::now());
        lz_rans_wrapped = 6 + lo_lz_rans.size();
        if (stats) {
            stats->filter_lo_mode5_candidate_bytes_sum += lo_lz.size();
            stats->filter_lo_mode5_wrapped_bytes_sum += lz_rans_wrapped;
            stats->filter_lo_mode5_legacy_bytes_sum += legacy_size;
        }
    }

    int best_mode = 0;
    size_t best_size = legacy_size;

    if (delta_wrapped * 1000 <= legacy_size * kFilterLoModeWrapperGainPermilleDefault) {
        if (delta_wrapped < best_size) {
            best_size = delta_wrapped;
            best_mode = 1;
        }
    }

    if (evaluate_lz &&
        lz_wrapped * 1000 <= legacy_size * kFilterLoModeWrapperGainPermilleDefault) {
        if (stats) stats->filter_lo_mode2_candidate_bytes_sum += lz_wrapped;
        if (lz_wrapped < best_size) {
            best_size = lz_wrapped;
            best_mode = 2;
        }
    } else {
        if (stats) stats->filter_lo_mode2_reject_gate++;
    }

    if (lz_rans_wrapped != std::numeric_limits<size_t>::max()) {
        bool better_than_legacy = (lz_rans_wrapped * 1000 <= legacy_size * (size_t)mode5_params.gain_permille);
        bool better_than_lz = (lz_rans_wrapped * 1000 <= lz_wrapped * (size_t)mode5_params.vs_lz_permille);

        if (better_than_legacy && better_than_lz) {
            if (lz_rans_wrapped < best_size) {
                best_size = lz_rans_wrapped;
                best_mode = 5;
            } else {
                if (stats) stats->filter_lo_mode5_reject_best++;
            }
        } else {
            if (stats) stats->filter_lo_mode5_reject_gate++;
        }
    }

    #include "lossless_filter_lo_codec_mode6.inc"

