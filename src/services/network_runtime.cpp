#include "services/network_runtime.hpp"

#include "application/debug_diagnostics.hpp"

#include "domain/market_calendar.hpp"
#include "domain/text.hpp"
#include "services/json_access.hpp"
#include "services/provider_payload_parser.hpp"
#include "services/secret_protection.hpp"
#include "services/stock_data_service.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <functional>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <vector>

using squarestar::application::ApiKeyValidation;
using squarestar::application::RequestGuiRedraw;
using squarestar::http::HttpResponse;
using squarestar::http::PerformHttpRequest;
using squarestar::http::ShutdownHttpClient;
using squarestar::http::UrlEncode;
using squarestar::json::Document;
using squarestar::json::JsonNumber;
using squarestar::json::JsonPath;
using squarestar::json::JsonString;
using squarestar::json::ParseJsonInSitu;
using squarestar::market::IsMarketOpenAt;
using squarestar::market::NextMarketOpenAt;
using squarestar::market::StockFetchResult;
using squarestar::text::UppercaseInPlace;

using YyjsonDoc = Document;
size_t ConfiguredHttpWorkerCount() {
#ifdef _MSC_VER
    char* value = nullptr;
    std::size_t valueSize = 0;
    if (_dupenv_s(&value, &valueSize, "SQUARESTAR_HTTP_WORKERS") == 0 &&
        value != nullptr) {
        char* end = nullptr;
        const long parsed = std::strtol(value, &end, 10);
        const bool valid = end != nullptr && *end == '\0' &&
                           parsed >= 1 && parsed <= 16;
        std::free(value);
        if (valid)
            return static_cast<size_t>(parsed);
    } else {
        std::free(value);
    }
#else
    if (const char* value = std::getenv("SQUARESTAR_HTTP_WORKERS")) {
        char* end = nullptr;
        const long parsed = std::strtol(value, &end, 10);
        if (end != nullptr && *end == '\0' && parsed >= 1 && parsed <= 16)
            return static_cast<size_t>(parsed);
    }
#endif
    // Two persistent HTTP lanes prevent a seconds-long background/provider
    // transfer from blocking an interactive quote or chart refresh. Each worker
    // owns its curl handle and Yahoo authentication session, so the concurrency
    // does not share cookie/crumb state across threads. Set
    // SQUARESTAR_HTTP_WORKERS=1 to trade latency isolation for minimum memory.
    return 2;
}
namespace {

struct NetworkExecutorSnapshot {
    std::shared_ptr<PersistentExecutor> http;
    std::shared_ptr<PersistentExecutor> background;
    std::shared_ptr<PersistentExecutor> search;
};

class NetworkExecutors final {
  public:
    std::shared_ptr<PersistentExecutor> Http() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!http_)
            http_ = std::make_shared<PersistentExecutor>(
                ConfiguredHttpWorkerCount(), "http", std::nullopt);
        return http_;
    }


    std::shared_ptr<PersistentExecutor> Background() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!background_)
            background_ = std::make_shared<PersistentExecutor>(
                2, "background", std::nullopt);
        return background_;
    }

    std::shared_ptr<PersistentExecutor> Search() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!search_)
            search_ = std::make_shared<PersistentExecutor>(
                1, "search", std::nullopt);
        return search_;
    }

    NetworkExecutorSnapshot Snapshot() {
        std::lock_guard<std::mutex> lock(mutex_);
        return {http_, background_, search_};
    }

    void Reset(const NetworkExecutorSnapshot& snapshot) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (http_ == snapshot.http)
            http_.reset();
        if (background_ == snapshot.background)
            background_.reset();
        if (search_ == snapshot.search)
            search_.reset();
    }

  private:
    std::mutex mutex_;
    std::shared_ptr<PersistentExecutor> http_;
    std::shared_ptr<PersistentExecutor> background_;
    std::shared_ptr<PersistentExecutor> search_;
};

NetworkExecutors& Executors() {
    static NetworkExecutors executors;
    return executors;
}

std::shared_ptr<PersistentExecutor> GetHttpWorkerPool() {
    return Executors().Http();
}

} // namespace
static std::future<HttpResponse>
QueueHttpGetWithPriority(std::string url, ExecutorPriority priority) {
    const auto pool = GetHttpWorkerPool();
    return pool->Submit([url = std::move(url)]() mutable {
        squarestar::secrets::ScopedSecureClear clearUrl(url);
        return PerformHttpRequest(url);
    }, priority);
}
std::future<HttpResponse> QueueHttpGet(std::string url) {
    return QueueHttpGetWithPriority(std::move(url), ExecutorPriority::Normal);
}
std::future<HttpResponse> QueueRealtimeHttpGet(std::string url) {
    return QueueHttpGetWithPriority(std::move(url), ExecutorPriority::High);
}
static std::mutex g_QuoteSingleFlightMutex;
static std::unordered_map<std::string, std::shared_future<HttpResponse>>
    g_QuoteSingleFlights;

namespace {

class QuoteSingleFlightCleanup final {
  public:
    explicit QuoteSingleFlightCleanup(std::string key) : key_(std::move(key)) {}
    ~QuoteSingleFlightCleanup() noexcept {
        std::lock_guard<std::mutex> lock(g_QuoteSingleFlightMutex);
        g_QuoteSingleFlights.erase(key_);
    }

    QuoteSingleFlightCleanup(const QuoteSingleFlightCleanup&) = delete;
    QuoteSingleFlightCleanup& operator=(const QuoteSingleFlightCleanup&) = delete;

  private:
    std::string key_;
};

} // namespace

std::shared_future<HttpResponse> QueueSingleFlightQuoteHttpGet(
    std::string flightKey,
    std::string url) {
    const auto pool = GetHttpWorkerPool();
    std::lock_guard<std::mutex> lock(g_QuoteSingleFlightMutex);
    const auto existing = g_QuoteSingleFlights.find(flightKey);
    if (existing != g_QuoteSingleFlights.end()) {
        if (existing->second.valid() &&
            existing->second.wait_for(std::chrono::seconds(0)) !=
                std::future_status::ready) {
            return existing->second;
        }
        g_QuoteSingleFlights.erase(existing);
    }
    // Hold the map mutex through Submit+insert. If the worker completes very
    // quickly, its cleanup blocks here until the entry exists, then removes it.
    // That keeps the map strictly in-flight-only instead of retaining completed
    // shared_future<HttpResponse> bodies until the same symbol is requested again.
    auto future =
        pool->Submit([requestUrl = std::move(url), cleanupKey = flightKey]() mutable {
                squarestar::secrets::ScopedSecureClear clearUrl(requestUrl);
                QuoteSingleFlightCleanup cleanup(std::move(cleanupKey));
                return PerformHttpRequest(requestUrl);
            }, ExecutorPriority::High)
            .share();
    // A saturated/stopping executor returns an already-ready rejection without
    // running the worker lambda, so there will be no worker-side cleanup. Do
    // not publish such completed futures into the in-flight registry. A real
    // request cannot become ready here because its cleanup must acquire the
    // mutex currently held by this function before the task can return.
    if (future.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        return future;
    g_QuoteSingleFlights.insert_or_assign(std::move(flightKey), future);
    return future;
}
std::future<HttpResponse> QueueBatchHttpGet(
    std::string url,
    squarestar::http::HttpCancelCheck cancelled) {
    const auto pool = GetHttpWorkerPool();
    return pool->Submit(
        [url = std::move(url), cancelled = std::move(cancelled)]() mutable {
            squarestar::secrets::ScopedSecureClear clearUrl(url);
            return PerformHttpRequest(url, cancelled);
        });
}
std::future<HttpResponse> QueueScreenerHttpGet(
    std::string url,
    squarestar::http::HttpCancelCheck cancelled) {
    const auto pool = GetHttpWorkerPool();
    return pool->Submit(
        [url = std::move(url), cancelled = std::move(cancelled)]() mutable {
            squarestar::secrets::ScopedSecureClear clearUrl(url);
            return PerformHttpRequest(url, cancelled);
        },
        ExecutorPriority::Normal);
}
std::future<HttpResponse> QueueYahooAuthenticatedGetWithPriority(
    std::string url,
    squarestar::http::HttpCancelCheck cancelled,
    ExecutorPriority priority) {
    const auto pool = GetHttpWorkerPool();
    return pool->Submit(
        [url = std::move(url), cancelled = std::move(cancelled)]() mutable {
            squarestar::secrets::ScopedSecureClear clearUrl(url);
            return squarestar::http::PerformYahooAuthenticatedGet(url, cancelled);
        },
        priority);
}
std::future<HttpResponse> QueueYahooAuthenticatedGet(
    std::string url,
    squarestar::http::HttpCancelCheck cancelled) {
    return QueueYahooAuthenticatedGetWithPriority(
        std::move(url), std::move(cancelled), ExecutorPriority::High);
}
std::future<HttpResponse> QueueYahooScreenerPost(
    std::string body,
    squarestar::http::HttpCancelCheck cancelled) {
    const auto pool = GetHttpWorkerPool();
    return pool->Submit(
        [body = std::move(body), cancelled = std::move(cancelled)] {
            return squarestar::http::PerformYahooScreenerPost(body, cancelled);
        },
        ExecutorPriority::High);
}
std::shared_ptr<PersistentExecutor> GetBackgroundWorkerPool() {
    return Executors().Background();
}

std::shared_ptr<PersistentExecutor> GetSearchWorkerPool() {
    return Executors().Search();
}

namespace {

struct QuoteWaiter {
    std::promise<StockFetchResult> promise;
    std::chrono::steady_clock::time_point deadline{};
};

struct QuoteFlight {
    std::string key;
    std::vector<std::shared_ptr<QuoteWaiter>> waiters;
};

std::mutex g_QuoteTaskMutex;
std::unordered_map<std::string, std::shared_ptr<QuoteFlight>> g_QuoteFlights;
bool g_QuoteSchedulerStopping = false;

ExecutorPriority PriorityForQuotePurpose(QuoteRequestPurpose purpose) noexcept {
    return purpose == QuoteRequestPurpose::Foreground
               ? ExecutorPriority::High
               : ExecutorPriority::Normal;
}

StockFetchResult ExpiredQuoteResult() {
    StockFetchResult result;
    result.success = false;
    result.errorMessage = "Quote request expired before provider admission";
    result.marketData.errorMessage = result.errorMessage;
    return result;
}

void FulfillExpiredWaiter(const std::shared_ptr<QuoteWaiter>& waiter) noexcept {
    if (!waiter)
        return;
    try {
        waiter->promise.set_value(ExpiredQuoteResult());
    } catch (...) {
        // Promise satisfaction can race with shutdown.
    }
}

void FailQuoteFlight(const std::shared_ptr<QuoteFlight>& flight,
                     std::exception_ptr error) noexcept {
    if (!flight)
        return;
    std::vector<std::shared_ptr<QuoteWaiter>> waiters;
    {
        std::lock_guard<std::mutex> lock(g_QuoteTaskMutex);
        const auto found = g_QuoteFlights.find(flight->key);
        if (found == g_QuoteFlights.end() || found->second != flight)
            return;
        waiters = std::move(flight->waiters);
        g_QuoteFlights.erase(found);
    }
    for (const auto& waiter : waiters) {
        if (!waiter)
            continue;
        try {
            waiter->promise.set_exception(error);
        } catch (...) {
        }
    }
    RequestGuiRedraw();
}

StockFetchResult RunQuoteFlight(
    const std::shared_ptr<QuoteFlight>& flight,
    std::function<StockFetchResult()> work) {
    std::vector<std::shared_ptr<QuoteWaiter>> expired;
    bool hasActiveWaiters = false;
    {
        std::lock_guard<std::mutex> lock(g_QuoteTaskMutex);
        const auto found = g_QuoteFlights.find(flight->key);
        if (found == g_QuoteFlights.end() || found->second != flight)
            return {};
        const auto now = std::chrono::steady_clock::now();
        auto& waiters = flight->waiters;
        waiters.erase(
            std::remove_if(waiters.begin(), waiters.end(), [&](const auto& waiter) {
                if (!waiter)
                    return true;
                if (now < waiter->deadline)
                    return false;
                expired.push_back(waiter);
                return true;
            }),
            waiters.end());
        hasActiveWaiters = !waiters.empty();
        if (!hasActiveWaiters)
            g_QuoteFlights.erase(found);
    }
    for (const auto& waiter : expired)
        FulfillExpiredWaiter(waiter);
    if (!hasActiveWaiters)
        return {};

    StockFetchResult fetched;
    try {
        fetched = work ? work() : StockFetchResult{};
    } catch (...) {
        FailQuoteFlight(flight, std::current_exception());
        return {};
    }

    std::vector<std::shared_ptr<QuoteWaiter>> waiters;
    {
        std::lock_guard<std::mutex> lock(g_QuoteTaskMutex);
        const auto found = g_QuoteFlights.find(flight->key);
        if (found == g_QuoteFlights.end() || found->second != flight)
            return {};
        waiters = std::move(flight->waiters);
        g_QuoteFlights.erase(found);
    }
    const auto completedAt = std::chrono::steady_clock::now();
    for (const auto& waiter : waiters) {
        if (!waiter)
            continue;
        if (completedAt >= waiter->deadline) {
            FulfillExpiredWaiter(waiter);
            continue;
        }
        StockFetchResult delivered = fetched;
        try {
            waiter->promise.set_value(std::move(delivered));
        } catch (...) {
        }
    }
    RequestGuiRedraw();
    return {};
}

} // namespace

std::future<StockFetchResult> QueueBackgroundStockTask(
    std::function<StockFetchResult()> work,
    ExecutorPriority priority) {
    const auto pool = GetBackgroundWorkerPool();
    return pool->Submit(
        [work = std::move(work)]() mutable {
            try {
                StockFetchResult result = work ? work() : StockFetchResult{};
                RequestGuiRedraw();
                return result;
            } catch (...) {
                RequestGuiRedraw();
                throw;
            }
        },
        priority);
}

std::future<StockFetchResult> QueueStockQuoteTask(
    std::string symbolKey,
    QuoteRequestPurpose purpose,
    std::chrono::milliseconds maximumQueueAge,
    std::function<StockFetchResult()> work) {
    auto waiter = std::make_shared<QuoteWaiter>();
    std::future<StockFetchResult> future = waiter->promise.get_future();
    const auto now = std::chrono::steady_clock::now();
    if (maximumQueueAge <= std::chrono::milliseconds::zero())
        maximumQueueAge = std::chrono::milliseconds(1);
    waiter->deadline = now + maximumQueueAge;

    std::shared_ptr<QuoteFlight> flight;
    bool leader = false;
    {
        std::lock_guard<std::mutex> lock(g_QuoteTaskMutex);
        if (g_QuoteSchedulerStopping) {
            try {
                waiter->promise.set_exception(std::make_exception_ptr(
                    ExecutorRejected("quote scheduler is shutting down")));
            } catch (...) {
            }
            return future;
        }
        const auto found = g_QuoteFlights.find(symbolKey);
        if (found != g_QuoteFlights.end()) {
            flight = found->second;
            flight->waiters.push_back(waiter);
            return future;
        }
        flight = std::make_shared<QuoteFlight>();
        flight->key = symbolKey;
        flight->waiters.push_back(waiter);
        g_QuoteFlights.insert_or_assign(std::move(symbolKey), flight);
        leader = true;
    }

    if (leader) {
        const auto pool = GetBackgroundWorkerPool();
        auto accepted = pool->Submit(
            [flight, work = std::move(work)]() mutable {
                return RunQuoteFlight(flight, std::move(work));
            },
            PriorityForQuotePurpose(purpose));
        if (accepted.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            try {
                (void)accepted.get();
            } catch (...) {
                FailQuoteFlight(flight, std::current_exception());
            }
        }
    }
    return future;
}

void PrimeNetworkRuntimeWithoutIo() {
    // Create every user-facing lane before the first interaction. Persistent
    // workers park on condition variables, so this costs no idle CPU while
    // avoiding thread creation and libcurl easy-handle setup on the first click.
    (void)GetBackgroundWorkerPool();
    (void)GetSearchWorkerPool();
    const auto httpPool = GetHttpWorkerPool();

    struct PrimeGate {
        std::mutex mutex;
        std::condition_variable cv;
        std::size_t ready = 0;
        bool release = false;
    };
    const auto primePool = [](const std::shared_ptr<PersistentExecutor>& pool,
                              std::size_t workerCount) {
        if (!pool || workerCount == 0)
            return;
        auto gate = std::make_shared<PrimeGate>();
        std::vector<std::future<bool>> futures;
        futures.reserve(workerCount);
        for (std::size_t i = 0; i < workerCount; ++i) {
            futures.push_back(pool->Submit([gate] {
                const bool initialized =
                    squarestar::http::InitializeHttpRuntimeWithoutNetwork();
                std::unique_lock<std::mutex> lock(gate->mutex);
                ++gate->ready;
                gate->cv.notify_all();
                gate->cv.wait(lock, [&] { return gate->release; });
                return initialized;
            }));
        }
        {
            std::unique_lock<std::mutex> lock(gate->mutex);
            gate->cv.wait(lock, [&] { return gate->ready == workerCount; });
            gate->release = true;
        }
        gate->cv.notify_all();
        for (auto& future : futures) {
            try {
                (void)future.get();
            } catch (...) {
            }
        }
    };

    primePool(httpPool, ConfiguredHttpWorkerCount());
}

void PrimeNetworkRuntimeWithoutIoForBenchmark() {
    PrimeNetworkRuntimeWithoutIo();
}

void ShutdownNetworkRuntime() {
    ShutdownNetworkWorkerPools();
    ShutdownHttpClient();
}

void ShutdownNetworkWorkerPools() {
    squarestar::marketdata::ShutdownAlertQuoteBatchScheduler();
    std::vector<std::shared_ptr<QuoteWaiter>> abandonedQuoteWaiters;
    {
        std::lock_guard<std::mutex> lock(g_QuoteSingleFlightMutex);
        g_QuoteSingleFlights.clear();
    }
    {
        std::lock_guard<std::mutex> lock(g_QuoteTaskMutex);
        g_QuoteSchedulerStopping = true;
        for (auto& [key, flight] : g_QuoteFlights) {
            (void)key;
            if (!flight)
                continue;
            for (auto& waiter : flight->waiters) {
                if (waiter)
                    abandonedQuoteWaiters.push_back(std::move(waiter));
            }
        }
        g_QuoteFlights.clear();
    }
    for (const auto& waiter : abandonedQuoteWaiters) {
        if (!waiter)
            continue;
        try {
            waiter->promise.set_exception(std::make_exception_ptr(
                ExecutorRejected("quote scheduler is shutting down")));
        } catch (...) {
        }
    }

    const NetworkExecutorSnapshot executors = Executors().Snapshot();
    const auto& httpPool = executors.http;
    const auto& backgroundPool = executors.background;
    const auto& searchPool = executors.search;

    // Stop dependencies first. Background jobs may be waiting on HTTP futures,
    // so signal every queue before joining any worker.
    if (httpPool)
        httpPool->RequestShutdown();
    if (backgroundPool)
        backgroundPool->RequestShutdown();
    if (searchPool)
        searchPool->RequestShutdown();

    if (searchPool)
        searchPool->Join();
    if (backgroundPool)
        backgroundPool->Join();
    if (httpPool)
        httpPool->Join();

    Executors().Reset(executors);
    {
        std::lock_guard<std::mutex> lock(g_QuoteTaskMutex);
        g_QuoteSchedulerStopping = false;
    }
    squarestar::marketdata::ResetAlertQuoteBatchScheduler();
}

std::future<ApiKeyValidation> QueueFinnhubApiKeyValidation(std::string key) {
    squarestar::secrets::ScopedSecureClear clearKey(key);
    std::string encodedKey = UrlEncode(key);
    squarestar::secrets::ScopedSecureClear clearEncodedKey(encodedKey);
    std::string url =
        "https://finnhub.io/api/v1/quote?symbol=AAPL&token=" + encodedKey;

    const auto pool = GetHttpWorkerPool();
    return pool->Submit(
        [url = std::move(url)]() mutable {
            squarestar::secrets::ScopedSecureClear clearUrl(url);
            HttpResponse response = PerformHttpRequest(url);
            const ApiKeyValidation validation =
                ClassifyFinnhubApiKeyValidationResponse(std::move(response));
            RequestGuiRedraw();
            return validation;
        },
        ExecutorPriority::High);
}

ApiKeyValidation ClassifyFinnhubApiKeyValidationResponse(HttpResponse response) {
    if (response.statusCode == 401 || response.statusCode == 403)
        return ApiKeyValidation::Forbidden;
    if (response.statusCode == 0 || response.statusCode == 429 || response.body.empty())
        return ApiKeyValidation::NetworkError;
    YyjsonDoc doc = ParseJsonInSitu(response.body);
    yyjson_val* root = doc ? yyjson_doc_get_root(doc.get()) : nullptr;
    std::string error;
    if (JsonString(root, "error", error)) {
        UppercaseInPlace(error);
        return error.find("FORBIDDEN") != std::string::npos ||
                       error.find("ACCESS") != std::string::npos
                   ? ApiKeyValidation::Forbidden
                   : ApiKeyValidation::Invalid;
    }
    double current = 0.0;
    return JsonNumber(root, "c", current) && current > 0.0 ? ApiKeyValidation::Valid
                                                           : ApiKeyValidation::Invalid;
}
std::string GetNextMarketOpenString() {
    const std::time_t now = std::time(nullptr);
    if (IsMarketOpenAt(now))
        return "Market is OPEN";
    const std::time_t targetUtc = NextMarketOpenAt(now);
    const int targetSeconds =
        static_cast<int>(std::max<std::time_t>(0, targetUtc - now));
    const int d = targetSeconds / 86400;
    const int h = (targetSeconds % 86400) / 3600;
    const int m = (targetSeconds % 3600) / 60;
    const int sec = targetSeconds % 60;
    char buf[128];
    if (d > 0)
        snprintf(buf, sizeof(buf), "Market opens in %dd %dh %dm", d, h, m);
    else if (h > 0)
        snprintf(buf, sizeof(buf), "Market opens in %dh %dm", h, m);
    else if (m > 0)
        snprintf(buf, sizeof(buf), "Market opens in %dm %ds", m, sec);
    else
        snprintf(buf, sizeof(buf), "Market opens in %ds", sec);
    return std::string(buf);
}
