#include "application/screener_executor.hpp"

#include "application/debug_diagnostics.hpp"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <memory>
#include <optional>
#include <thread>
#include <utility>

namespace squarestar::application {
namespace {

class LatestScreenerJobExecutor {
  public:
    LatestScreenerJobExecutor()
        : worker_([this](std::stop_token stopToken) { Run(stopToken); }) {}
    ~LatestScreenerJobExecutor() { Stop(); }

    bool Submit(std::function<void()> job) {
        if (!job)
            return false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopping_)
                return false;
            pending_ = std::move(job);
            busy_.store(true, std::memory_order_release);
        }
        ready_.notify_one();
        return true;
    }

    void Stop() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopping_)
                return;
            stopping_ = true;
            pending_.reset();
        }
        worker_.request_stop();
        ready_.notify_all();
        if (worker_.joinable())
            worker_.join();
        busy_.store(false, std::memory_order_release);
    }

    bool Busy() const noexcept { return busy_.load(std::memory_order_acquire); }

  private:
    void Run(std::stop_token stopToken) {
        for (;;) {
            std::function<void()> job;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                ready_.wait(lock, [&] {
                    return stopping_ || stopToken.stop_requested() || pending_.has_value();
                });
                if (stopping_ || stopToken.stop_requested()) {
                    pending_.reset();
                    busy_.store(false, std::memory_order_release);
                    return;
                }
                job = std::move(*pending_);
                pending_.reset();
            }
            try {
                job();
            } catch (...) {
                ReportBackgroundFailure("screener executor job");
            }
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (!pending_)
                    busy_.store(false, std::memory_order_release);
            }
        }
    }

    mutable std::mutex mutex_;
    std::condition_variable ready_;
    bool stopping_ = false;
    std::optional<std::function<void()>> pending_;
    std::jthread worker_;
    std::atomic_bool busy_{false};
};

std::mutex g_ExecutorMutex;
std::shared_ptr<LatestScreenerJobExecutor> g_Executor;
bool g_ExecutorShuttingDown = false;

std::shared_ptr<LatestScreenerJobExecutor> Executor() {
    std::lock_guard<std::mutex> lock(g_ExecutorMutex);
    if (g_ExecutorShuttingDown)
        return {};
    if (!g_Executor)
        g_Executor = std::make_shared<LatestScreenerJobExecutor>();
    return g_Executor;
}

}

bool SubmitLatestScreenerJob(std::function<void()> job) {
    const auto executor = Executor();
    return executor && executor->Submit(std::move(job));
}

void ShutdownLatestScreenerJobExecutor() {
    std::shared_ptr<LatestScreenerJobExecutor> executor;
    {
        std::lock_guard<std::mutex> lock(g_ExecutorMutex);
        g_ExecutorShuttingDown = true;
        executor = std::move(g_Executor);
    }
    if (executor)
        executor->Stop();
}

bool ScreenerJobBusy() noexcept {
    std::lock_guard<std::mutex> lock(g_ExecutorMutex);
    return g_Executor && g_Executor->Busy();
}

}
