    std::vector<BlockEval> evals((size_t)nb);
    const unsigned int hw_threads = thread_budget::max_threads();
    int task_count = std::min<int>((int)hw_threads, std::max(1, nb / 64));
    task_count = std::max(1, task_count);
    thread_budget::ScopedThreadTokens eval_tokens;
    if (nb >= 256 && task_count >= 2) {
        eval_tokens = thread_budget::ScopedThreadTokens::try_acquire_up_to(
            (unsigned int)task_count, 2
        );
    }
    if (eval_tokens.acquired()) {
        task_count = (int)eval_tokens.count();
        int chunk = (nb + task_count - 1) / task_count;
        std::vector<std::future<void>> futs;
        futs.reserve((size_t)task_count);
        for (int t = 0; t < task_count; t++) {
            int begin = t * chunk;
            int end = std::min(nb, begin + chunk);
            if (begin >= end) continue;
            futs.push_back(eval_pool.submit([&, begin, end]() {
                thread_budget::ScopedParallelRegion guard;
                for (int j = begin; j < end; j++) {
                    evals[(size_t)j] = evaluate_block(j);
                }
            }));
        }
        for (auto& f : futs) f.get();
    } else {
        for (int i = 0; i < nb; i++) {
            evals[(size_t)i] = evaluate_block(i);
        }
    }
