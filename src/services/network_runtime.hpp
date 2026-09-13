#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <future>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

#include "application/main_loop_signal.hpp"
#include "application/settings_runtime_state.hpp"
#include "domain/stock_data.hpp"
#include "services/http_client.hpp"

enum class ExecutorPriority : std::size_t { High, Normal, Low, Count };

class ExecutorRejected final : public std::runtime_error {
  public:
    explicit ExecutorRejected(const char* reason)
        : std::runtime_error(reason ? reason : "executor rejected a task") {}
};

class PersistentExecutor final {
  public:
    explicit PersistentExecutor(
        std::size_t workerCount,
        std::string_view diagnosticName = "worker",
        std::optional<std::chrono::milliseconds> idleRetireAfter =
            std::chrono::seconds(30))
        : workerCount_(std::max<std::size_t>(1, workerCount)),
          idleRetireAfter_(idleRetireAfter),
          diagnosticName_(diagnosticName) {
        if (idleRetireAfter_)
            *idleRetireAfter_ =
                std::max(*idleRetireAfter_, std::chrono::milliseconds(100));
        workers_.reserve(workerCount_);
        std::lock_guard<std::mutex> lock(mutex_);
        StartWorkersLocked();
    }

    ~PersistentExecutor() { Shutdown(); }
    PersistentExecutor(const PersistentExecutor&) = delete;
    PersistentExecutor& operator=(const PersistentExecutor&) = delete;

    template <typename Work>
    auto Submit(Work&& work,
                ExecutorPriority priority = ExecutorPriority::Normal)
        -> std::future<std::invoke_result_t<std::decay_t<Work>&>> {
        using WorkType = std::decay_t<Work>;
        using Result = std::invoke_result_t<WorkType&>;

        std::packaged_task<Result()> resultTask(std::forward<Work>(work));
        std::future<Result> future = resultTask.get_future();
        std::packaged_task<void()> queuedTask(
            [task = std::move(resultTask)]() mutable { task(); });

        const char* rejectionReason = nullptr;
        std::size_t rejectedQueueDepth = 0;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopping_) {
                rejectionReason = "executor is shutting down";
                rejectedQueueDepth = queued_;
            } else if (queued_ >= kMaxQueuedTasks) {
                rejectionReason = "queue capacity reached";
                rejectedQueueDepth = queued_;
            } else {
                // Executors that opt into idle retirement can respawn here.
                // Latency-sensitive executors pass std::nullopt and remain parked
                // on the condition variable instead of destroying thread-local state.
                if (liveWorkers_ == 0)
                    StartWorkersLocked();
                tasks_[static_cast<std::size_t>(priority)].push_back(
                    std::move(queuedTask));
                ++queued_;
            }
        }
        if (rejectionReason) {
            ReportRejectedSubmission(rejectionReason, rejectedQueueDepth);
            std::promise<Result> rejected;
            std::future<Result> rejectedFuture = rejected.get_future();
            if constexpr (std::is_same_v<Result, squarestar::http::HttpResponse>) {
                rejected.set_value(squarestar::http::HttpResponse{
                    {}, 0, squarestar::http::HttpError::ExecutorRejected});
            } else {
                rejected.set_exception(
                    std::make_exception_ptr(ExecutorRejected(rejectionReason)));
            }
            return rejectedFuture;
        }
        cv_.notify_one();
        return future;
    }

    [[nodiscard]] std::uint64_t RejectedSubmissionCount() const noexcept {
        return rejectedSubmissions_.load(std::memory_order_relaxed);
    }

    void RequestShutdown() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopping_)
                return;
            stopping_ = true;
            // Shutdown is a cancellation boundary, not a request to drain the
            // entire backlog. Destroyed packaged tasks make their futures ready
            // with broken_promise, so dependents cannot wait forever.
            for (auto& queue : tasks_)
                queue.clear();
            queued_ = 0;
        }
        cv_.notify_all();
    }

    void Join() { workers_.clear(); }

    void Shutdown() {
        RequestShutdown();
        Join();
    }

  private:
    static constexpr std::size_t kMaxQueuedTasks = 256;
    static constexpr std::size_t kPriorityBurst = 4;

    void StartWorkersLocked() {
        if (stopping_ || liveWorkers_ != 0)
            return;
        // Every retained jthread is known to have exited once liveWorkers_
        // reaches zero. Destroying them here joins already-finished threads
        // and releases their small bookkeeping allocations before respawn.
        workers_.clear();
        workers_.reserve(workerCount_);
        liveWorkers_ = workerCount_;
        for (std::size_t i = 0; i < workerCount_; ++i) {
            workers_.emplace_back([this] {
                WorkerMain();
            });
        }
    }

    void WorkerMain() {
        for (;;) {
            std::packaged_task<void()> task;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                if (idleRetireAfter_) {
                    const bool awakened = cv_.wait_for(
                        lock,
                        *idleRetireAfter_,
                        [this] { return stopping_ || queued_ != 0; });
                    if (!awakened && queued_ == 0 && !stopping_) {
                        --liveWorkers_;
                        return;
                    }
                } else {
                    // A persistent worker consumes no CPU while idle. It sleeps
                    // in the condition variable and wakes immediately on Submit().
                    cv_.wait(lock, [this] { return stopping_ || queued_ != 0; });
                }
                if (stopping_ && queued_ == 0) {
                    --liveWorkers_;
                    return;
                }

                const std::size_t queueIndex = SelectQueueIndexLocked();
                auto& queue = tasks_[queueIndex];
                task = std::move(queue.front());
                queue.pop_front();
                --queued_;
            }
            task();
        }
    }

    std::size_t SelectQueueIndexLocked() noexcept {
        constexpr std::size_t high = static_cast<std::size_t>(ExecutorPriority::High);
        constexpr std::size_t normal = static_cast<std::size_t>(ExecutorPriority::Normal);
        constexpr std::size_t low = static_cast<std::size_t>(ExecutorPriority::Low);
        const bool lowerThanHighPending = !tasks_[normal].empty() || !tasks_[low].empty();
        if (!tasks_[high].empty() &&
            (highBurst_ < kPriorityBurst || !lowerThanHighPending)) {
            ++highBurst_;
            return high;
        }
        highBurst_ = 0;

        if (!tasks_[normal].empty() &&
            (normalBurst_ < kPriorityBurst || tasks_[low].empty())) {
            ++normalBurst_;
            return normal;
        }
        normalBurst_ = 0;

        if (!tasks_[low].empty())
            return low;
        if (!tasks_[normal].empty()) {
            ++normalBurst_;
            return normal;
        }
        ++highBurst_;
        return high;
    }

    void ReportRejectedSubmission(const char* reason,
                                  std::size_t queueDepth) noexcept {
        const std::uint64_t rejected =
            rejectedSubmissions_.fetch_add(1, std::memory_order_relaxed) + 1;
        if (rejected != 1 && (rejected & (rejected - 1)) != 0)
            return;
        std::fprintf(stderr,
                     "[SquareStar] %s executor rejected a task: %s "
                     "(%zu/%zu queued, rejected=%llu)\n",
                     diagnosticName_.c_str(),
                     reason,
                     queueDepth,
                     kMaxQueuedTasks,
                     static_cast<unsigned long long>(rejected));
        std::fflush(stderr);
    }

    std::mutex mutex_;
    std::condition_variable cv_;
    std::array<std::deque<std::packaged_task<void()>>,
               static_cast<std::size_t>(ExecutorPriority::Count)>
        tasks_;
    std::size_t queued_ = 0;
    std::size_t highBurst_ = 0;
    std::size_t normalBurst_ = 0;
    std::size_t workerCount_ = 1;
    std::size_t liveWorkers_ = 0;
    std::optional<std::chrono::milliseconds> idleRetireAfter_{
        std::chrono::seconds(30)};
    std::vector<std::jthread> workers_;
    std::atomic<std::uint64_t> rejectedSubmissions_{0};
    std::string diagnosticName_;
    bool stopping_ = false;
};

std::size_t ConfiguredHttpWorkerCount();
std::future<squarestar::http::HttpResponse> QueueHttpGet(std::string url);
std::future<squarestar::http::HttpResponse> QueueRealtimeHttpGet(std::string url);
// Quote payloads are tiny and frequently requested by multiple surfaces.
// Coalesce only these in-flight GETs so chart/news bodies retain move-only
// future semantics and never pay a multi-megabyte shared_future copy.
std::shared_future<squarestar::http::HttpResponse> QueueSingleFlightQuoteHttpGet(
    std::string flightKey,
    std::string url);
std::future<squarestar::http::HttpResponse> QueueBatchHttpGet(
    std::string url,
    squarestar::http::HttpCancelCheck cancelled = {});
std::future<squarestar::http::HttpResponse> QueueScreenerHttpGet(
    std::string url,
    squarestar::http::HttpCancelCheck cancelled = {});
std::future<squarestar::http::HttpResponse> QueueYahooAuthenticatedGet(
    std::string url,
    squarestar::http::HttpCancelCheck cancelled = {});
std::future<squarestar::http::HttpResponse> QueueYahooAuthenticatedGetWithPriority(
    std::string url,
    squarestar::http::HttpCancelCheck cancelled,
    ExecutorPriority priority);
std::future<squarestar::http::HttpResponse> QueueYahooScreenerPost(
    std::string body,
    squarestar::http::HttpCancelCheck cancelled = {});


enum class QuoteRequestPurpose : std::uint8_t {
    Foreground,
    BackgroundRefresh,
};

std::future<squarestar::market::StockFetchResult> QueueBackgroundStockTask(
    std::function<squarestar::market::StockFetchResult()> work,
    ExecutorPriority priority = ExecutorPriority::Normal);
std::future<squarestar::market::StockFetchResult> QueueStockQuoteTask(
    std::string symbolKey,
    QuoteRequestPurpose purpose,
    std::chrono::milliseconds maximumQueueAge,
    std::function<squarestar::market::StockFetchResult()> work);

std::shared_ptr<PersistentExecutor> GetBackgroundWorkerPool();
// User-driven search gets a tiny compute lane so parsing/ranking cannot sit
// behind stock jobs. Its HTTP transfers are routed through the shared HTTP lane.
std::shared_ptr<PersistentExecutor> GetSearchWorkerPool();
// Start the latency-sensitive executors/libcurl without making a network request.
void PrimeNetworkRuntimeWithoutIo();
void PrimeNetworkRuntimeWithoutIoForBenchmark();
void ShutdownNetworkWorkerPools();
void ShutdownNetworkRuntime();

template <typename Work>
std::future<squarestar::market::StockFetchResult> QueueStockTask(
    Work&& work,
    ExecutorPriority priority = ExecutorPriority::Normal) {
    const auto pool = GetBackgroundWorkerPool();
    return pool->Submit(
        [task = std::forward<Work>(work)]() mutable
            -> squarestar::market::StockFetchResult {
            try {
                auto result = task();
                squarestar::application::RequestGuiRedraw();
                return result;
            } catch (...) {
                squarestar::application::RequestGuiRedraw();
                throw;
            }
        },
        priority);
}

std::future<squarestar::application::ApiKeyValidation> QueueFinnhubApiKeyValidation(
    std::string key);
squarestar::application::ApiKeyValidation ClassifyFinnhubApiKeyValidationResponse(
    squarestar::http::HttpResponse response);
std::string GetNextMarketOpenString();
