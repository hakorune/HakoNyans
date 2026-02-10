#pragma once

#include <cstdint>
#include <cstdlib>
#include <functional>
#include <thread>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <future>

namespace hakonyans {

/**
 * シンプルなスレッドプール
 * 
 * P-Index 並列デコードで使用。
 * HAKONYANS_THREADS 環境変数でスレッド数を制御可能。
 */
class ThreadPool {
public:
    explicit ThreadPool(int num_threads = 0) {
        if (num_threads <= 0) {
            // 環境変数 or ハードウェア並列度
            const char* env = std::getenv("HAKONYANS_THREADS");
            if (env) {
                num_threads = std::atoi(env);
            }
            if (num_threads <= 0) {
                num_threads = std::thread::hardware_concurrency();
                if (num_threads <= 0) num_threads = 4;
            }
        }
        
        num_threads_ = num_threads;
        
        for (int i = 0; i < num_threads; ++i) {
            workers_.emplace_back([this] {
                while (true) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(mutex_);
                        cv_.wait(lock, [this] { return stop_ || !tasks_.empty(); });
                        if (stop_ && tasks_.empty()) return;
                        task = std::move(tasks_.front());
                        tasks_.pop();
                    }
                    task();
                }
            });
        }
    }
    
    ~ThreadPool() {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            stop_ = true;
        }
        cv_.notify_all();
        for (auto& w : workers_) w.join();
    }
    
    int num_threads() const { return num_threads_; }
    
    /**
     * タスクを投入して future を返す
     */
    template<typename F>
    auto submit(F&& f) -> std::future<decltype(f())> {
        using ReturnType = decltype(f());
        auto task = std::make_shared<std::packaged_task<ReturnType()>>(std::forward<F>(f));
        auto future = task->get_future();
        
        {
            std::unique_lock<std::mutex> lock(mutex_);
            tasks_.emplace([task] { (*task)(); });
        }
        cv_.notify_one();
        
        return future;
    }
    
    /**
     * 並列 for ループ
     */
    void parallel_for(int begin, int end, std::function<void(int)> body) {
        std::vector<std::future<void>> futures;
        for (int i = begin; i < end; ++i) {
            futures.push_back(submit([&body, i] { body(i); }));
        }
        for (auto& f : futures) f.get();
    }

private:
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool stop_ = false;
    int num_threads_;
};

} // namespace hakonyans
