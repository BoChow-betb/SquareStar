#include "services/stock_data_service.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <exception>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <utility>

#include "application/runtime_state.hpp"
#include "application/main_loop_signal.hpp"
#include "application/stock_data_merge.hpp"
#include "domain/chart_ranges.hpp"
#include "domain/market_runtime.hpp"
#include "domain/market_symbol.hpp"
#include "domain/trading_status.hpp"
#include "services/api_key_store.hpp"
#include "services/diagnostic_log.hpp"
#include "services/http_client.hpp"
#include "services/network_runtime.hpp"
#include "services/provider_payload_parser.hpp"
#include "services/secret_protection.hpp"
#include "services/stock_data_cache.hpp"
#include "services/stock_provider_merge.hpp"

namespace squarestar::marketdata {
namespace {

using squarestar::application::AppUiMode;
using squarestar::application::HasAnyMarketMetricData;
using squarestar::application::HasResolvedCompanyName;
using squarestar::application::StockFetchMetrics;
using squarestar::application::StockFetchNews;
using squarestar::application::StockFetchProfile;
using squarestar::application::ApplicationRuntime;
using squarestar::http::HttpError;
using squarestar::http::HttpErrorUserMessage;
using squarestar::http::HttpResponse;
using squarestar::http::UrlEncode;
using squarestar::market::ALL_TIME_RANGE_INDEX;
using squarestar::market::FetchKind;
using squarestar::market::MarketSymbol;
using squarestar::market::InstrumentNature;
using squarestar::market::StockData;
using squarestar::market::StockFetchResult;
using squarestar::market::TIME_RANGES;
using squarestar::market::ChartAvailability;
using squarestar::market::CorporateActionStatus;
using squarestar::market::ResolveChartRange;
using squarestar::providers::ApplyYahooChartPayload;
using squarestar::secrets::ApiKeyRevision;
using squarestar::secrets::IsApiKeyRevisionCurrent;
using squarestar::secrets::GetFinnhubApiKey;
using squarestar::secrets::GetFinnhubApiKeySnapshot;
using squarestar::secrets::HasFinnhubApiKey;
using squarestar::secrets::SecureClear;

struct HttpFailureSummary {
    HttpError error = HttpError::None;
    long statusCode = 0;
    bool rateLimited = false;
};
static void RememberHttpFailure(const HttpResponse& response,
                                HttpFailureSummary& summary) noexcept {
    summary.rateLimited = summary.rateLimited || response.statusCode == 429;
    if (response.error == HttpError::None)
        return;
    // A transport failure is more actionable than an HTTP status from a
    // different fallback provider. Otherwise retain the first concrete cause.
    if (summary.error == HttpError::None ||
        (summary.error == HttpError::HttpStatus && response.error != HttpError::HttpStatus)) {
        summary.error = response.error;
        summary.statusCode = response.statusCode;
    }
}
static const char* HttpErrorDiagnosticCategory(HttpError error) noexcept {
    switch (error) {
    case HttpError::None:
        return "http";
    case HttpError::InvalidUrl:
        return "invalid-url";
    case HttpError::RuntimeUnavailable:
        return "runtime";
    case HttpError::DnsFailure:
        return "dns";
    case HttpError::ConnectFailure:
        return "connect";
    case HttpError::Timeout:
        return "timeout";
    case HttpError::TlsFailure:
        return "tls";
    case HttpError::ResponseTooLarge:
        return "response-size";
    case HttpError::Cancelled:
        return "cancelled";
    case HttpError::ExecutorRejected:
        return "executor";
    case HttpError::TransferFailure:
        return "transfer";
    case HttpError::HttpStatus:
        return "http-status";
    }
    return "http";
}

static std::string TakeBody(HttpResponse response,
                            HttpFailureSummary& failures,
                            const char* provider,
                            const char* operation) {
    if (!response.IsSuccess()) {
        RememberHttpFailure(response, failures);
        // 404 is normal negative ticker evidence and cancellation is normal
        // lifecycle control; neither belongs in a release failure log.
        if (response.statusCode != 404 && response.error != HttpError::Cancelled) {
            squarestar::diagnostics::WriteDiagnosticEvent(
                {provider,
                 HttpErrorDiagnosticCategory(response.error),
                 operation,
                 response.statusCode});
        }
        return {};
    }
    return std::move(response.body);
}
static const char* StockFetchHttpFailureMessage(const HttpFailureSummary& failure) noexcept {
    if (failure.error == HttpError::HttpStatus) {
        if (failure.statusCode == 429)
            return "Market-data provider rate limit reached";
        if (failure.statusCode >= 500)
            return "Market-data provider unavailable";
        if (failure.statusCode == 401 || failure.statusCode == 403)
            return "Market-data provider rejected the request";
        // A 404 is valid negative ticker evidence, not a network diagnosis.
        if (failure.statusCode == 404)
            return "";
    }
    return HttpErrorUserMessage(failure.error);
}

std::string CompanyNewsUrl(const std::string& ticker,
                           const std::string& encodedApiKey,
                           int lookbackDays) {
    const std::time_t now = std::time(nullptr);
    const std::time_t then = now - static_cast<std::time_t>(lookbackDays) * 24 * 3600;
    char to[16]{}, from[16]{};
    struct tm tmNow {}, tmThen {};
#if defined(_WIN32)
    gmtime_s(&tmNow, &now);
    gmtime_s(&tmThen, &then);
#else
    gmtime_r(&now, &tmNow);
    gmtime_r(&then, &tmThen);
#endif
    std::strftime(to, sizeof(to), "%Y-%m-%d", &tmNow);
    std::strftime(from, sizeof(from), "%Y-%m-%d", &tmThen);
    return "https://finnhub.io/api/v1/company-news?symbol=" + ticker +
           "&from=" + from + "&to=" + to + "&token=" + encodedApiKey;
}

} // namespace

namespace {

struct AlertQuoteWaiter {
    std::string yahooSymbol;
    std::promise<StockFetchResult> promise;
    std::chrono::steady_clock::time_point deadline{};
};

std::mutex g_AlertQuoteBatchMutex;
std::vector<std::shared_ptr<AlertQuoteWaiter>> g_AlertQuotePending;
bool g_AlertQuoteBatchScheduled = false;
bool g_AlertQuoteBatchStopping = false;
std::atomic_bool g_AlertQuoteBatchCancelInFlight{false};
constexpr std::size_t kMaximumYahooSymbolsPerQuoteBatch = 50;
constexpr auto kAlertQuoteCollectionWindow = std::chrono::milliseconds(8);
constexpr auto kAlertQuoteMaximumAge = std::chrono::seconds(9);

void SetAlertQuoteFailure(const std::shared_ptr<AlertQuoteWaiter>& waiter,
                          std::string message,
                          bool rateLimited) noexcept {
    if (!waiter)
        return;
    StockFetchResult result;
    result.success = false;
    result.rateLimited = rateLimited;
    result.errorMessage = std::move(message);
    result.marketData.errorMessage = result.errorMessage;
    try {
        waiter->promise.set_value(std::move(result));
    } catch (...) {
    }
}

void SetAlertQuoteExpired(const std::shared_ptr<AlertQuoteWaiter>& waiter) noexcept {
    SetAlertQuoteFailure(
        waiter, "Alert quote expired before provider delivery", false);
}

StockFetchResult MakeAlertQuoteResult(
    const squarestar::providers::YahooQuoteSnapshot& quote) {
    StockFetchResult result;
    result.marketData.currentPrice = quote.currentPrice;
    result.marketData.previousClose = quote.previousClose;
    result.marketData.openPrice = quote.openPrice;
    result.marketData.dayHigh = quote.dayHigh;
    result.marketData.dayLow = quote.dayLow;
    result.marketData.quoteTimestamp = quote.timestamp;
    result.marketData.currency = "USD";
    result.marketData.instrumentNature = InstrumentNature::PublicMarketSecurity;
    result.marketData.success = quote.currentPrice > 0.0;
    result.FinalizeFromMarketData(false);
    return result;
}

StockFetchResult RunAlertQuoteBatch() {
    std::this_thread::sleep_for(kAlertQuoteCollectionWindow);
    for (;;) {
        std::vector<std::shared_ptr<AlertQuoteWaiter>> batch;
        std::vector<std::shared_ptr<AlertQuoteWaiter>> expired;
        std::vector<std::string> symbols;
        {
            std::lock_guard<std::mutex> lock(g_AlertQuoteBatchMutex);
            if (g_AlertQuoteBatchStopping) {
                g_AlertQuoteBatchScheduled = false;
                return {};
            }

            const auto now = std::chrono::steady_clock::now();
            std::unordered_set<std::string> selected;
            const std::size_t symbolLimit = std::min<std::size_t>(
                kMaximumYahooSymbolsPerQuoteBatch, g_AlertQuotePending.size());
            selected.reserve(symbolLimit);
            symbols.reserve(symbolLimit);
            batch.reserve(g_AlertQuotePending.size());

            std::vector<std::shared_ptr<AlertQuoteWaiter>> pending;
            pending.reserve(g_AlertQuotePending.size());
            for (auto& waiter : g_AlertQuotePending) {
                if (!waiter)
                    continue;
                if (now >= waiter->deadline) {
                    expired.push_back(std::move(waiter));
                    continue;
                }

                const bool alreadySelected = selected.contains(waiter->yahooSymbol);
                if (alreadySelected || selected.size() < kMaximumYahooSymbolsPerQuoteBatch) {
                    if (!alreadySelected) {
                        selected.insert(waiter->yahooSymbol);
                        symbols.push_back(waiter->yahooSymbol);
                    }
                    batch.push_back(std::move(waiter));
                } else {
                    pending.push_back(std::move(waiter));
                }
            }
            g_AlertQuotePending = std::move(pending);
            if (batch.empty() && g_AlertQuotePending.empty())
                g_AlertQuoteBatchScheduled = false;
        }

        for (const auto& waiter : expired)
            SetAlertQuoteExpired(waiter);
        if (batch.empty())
            return {};

        std::size_t joinedBytes = symbols.empty() ? 0 : symbols.size() - 1;
        for (const std::string& symbol : symbols)
            joinedBytes += symbol.size();
        std::string joinedSymbols;
        joinedSymbols.reserve(joinedBytes);
        for (const std::string& symbol : symbols) {
            if (!joinedSymbols.empty())
                joinedSymbols.push_back(',');
            joinedSymbols += symbol;
        }
        const std::string url =
            "https://query1.finance.yahoo.com/v7/finance/quote?symbols=" +
            UrlEncode(joinedSymbols);
        HttpResponse response = QueueYahooAuthenticatedGetWithPriority(
            url,
            [] {
                return g_AlertQuoteBatchCancelInFlight.load(
                    std::memory_order_acquire);
            },
            ExecutorPriority::Normal).get();
        const bool rateLimited = response.statusCode == 429;
        std::vector<squarestar::providers::YahooQuoteSnapshot> quotes;
        if (response.IsSuccess())
            quotes = squarestar::providers::ParseYahooQuoteBatchPayload(
                std::move(response.body));
        std::unordered_map<std::string, squarestar::providers::YahooQuoteSnapshot>
            bySymbol;
        bySymbol.reserve(quotes.size());
        for (auto& quote : quotes)
            bySymbol.insert_or_assign(quote.symbol, std::move(quote));

        const auto completedAt = std::chrono::steady_clock::now();
        for (const auto& waiter : batch) {
            if (!waiter)
                continue;
            if (completedAt >= waiter->deadline) {
                SetAlertQuoteExpired(waiter);
                continue;
            }
            const auto found = bySymbol.find(waiter->yahooSymbol);
            if (found == bySymbol.end()) {
                SetAlertQuoteFailure(
                    waiter,
                    rateLimited ? "Market-data provider rate limit reached"
                                : response.IsSuccess()
                                      ? "Yahoo did not return this alert symbol"
                                      : HttpErrorUserMessage(response.error),
                    rateLimited);
                continue;
            }
            try {
                waiter->promise.set_value(
                    MakeAlertQuoteResult(found->second));
            } catch (...) {
            }
        }
        squarestar::application::RequestGuiRedraw();
    }
}

void FailPendingAlertQuoteBatch(std::exception_ptr error) noexcept {
    std::vector<std::shared_ptr<AlertQuoteWaiter>> waiters;
    {
        std::lock_guard<std::mutex> lock(g_AlertQuoteBatchMutex);
        waiters = std::move(g_AlertQuotePending);
        g_AlertQuotePending.clear();
        g_AlertQuoteBatchScheduled = false;
    }
    for (const auto& waiter : waiters) {
        if (!waiter)
            continue;
        try {
            waiter->promise.set_exception(error);
        } catch (...) {
        }
    }
}

} // namespace

std::future<StockFetchResult> QueueAlertQuoteRequest(const std::string& ticker) {
    auto waiter = std::make_shared<AlertQuoteWaiter>();
    std::future<StockFetchResult> future = waiter->promise.get_future();
    waiter->deadline =
        std::chrono::steady_clock::now() + kAlertQuoteMaximumAge;
    const std::optional<MarketSymbol> symbol = MarketSymbol::Parse(ticker);
    if (!symbol || !symbol->HasSupportedUsClassSuffix()) {
        SetAlertQuoteFailure(waiter, "Ticker not found", false);
        return future;
    }
    waiter->yahooSymbol = symbol->Yahoo();

    bool schedule = false;
    {
        std::lock_guard<std::mutex> lock(g_AlertQuoteBatchMutex);
        if (g_AlertQuoteBatchStopping) {
            try {
                waiter->promise.set_exception(std::make_exception_ptr(
                    ExecutorRejected("alert quote scheduler is shutting down")));
            } catch (...) {
            }
            return future;
        }
        g_AlertQuotePending.push_back(waiter);
        if (!g_AlertQuoteBatchScheduled) {
            g_AlertQuoteBatchScheduled = true;
            schedule = true;
        }
    }
    if (schedule) {
        auto batchTask = QueueBackgroundStockTask(
            [] { return RunAlertQuoteBatch(); },
            ExecutorPriority::Normal);
        if (batchTask.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            try {
                (void)batchTask.get();
            } catch (...) {
                FailPendingAlertQuoteBatch(std::current_exception());
            }
        }
    }
    return future;
}

void ShutdownAlertQuoteBatchScheduler() noexcept {
    std::vector<std::shared_ptr<AlertQuoteWaiter>> waiters;
    {
        std::lock_guard<std::mutex> lock(g_AlertQuoteBatchMutex);
        g_AlertQuoteBatchStopping = true;
        g_AlertQuoteBatchCancelInFlight.store(true, std::memory_order_release);
        waiters = std::move(g_AlertQuotePending);
        g_AlertQuotePending.clear();
        g_AlertQuoteBatchScheduled = false;
    }
    for (const auto& waiter : waiters) {
        if (!waiter)
            continue;
        try {
            waiter->promise.set_exception(std::make_exception_ptr(
                ExecutorRejected("alert quote scheduler is shutting down")));
        } catch (...) {
        }
    }
}

void ResetAlertQuoteBatchScheduler() noexcept {
    std::lock_guard<std::mutex> lock(g_AlertQuoteBatchMutex);
    g_AlertQuotePending.clear();
    g_AlertQuoteBatchScheduled = false;
    g_AlertQuoteBatchCancelInFlight.store(false, std::memory_order_release);
    g_AlertQuoteBatchStopping = false;
}

namespace {

bool ChartAnswered(const HttpResponse& response) noexcept {
    return (response.statusCode >= 200 && response.statusCode < 300) ||
           response.statusCode == 404;
}

struct ChartTry {
    bool applied = false;
    bool answered = false;
    bool missing = false;
};

std::string YahooChartUrl(const char* host,
                          const std::string& yahooTicker,
                          int timeRangeIndex) {
    return std::string("https://") + host + "/v8/finance/chart/" + yahooTicker +
           "?interval=" + TIME_RANGES[timeRangeIndex].intervalStr +
           "&range=" + TIME_RANGES[timeRangeIndex].rangeStr +
           "&includePrePost=false&events=div%2Csplits";
}

ChartTry FetchYahooChart(const char* host,
                         const std::string& yahooTicker,
                         int timeRangeIndex,
                         HttpFailureSummary& httpFailures,
                         const char* operation,
                         StockData& result) {
    HttpResponse response =
        QueueRealtimeHttpGet(YahooChartUrl(host, yahooTicker, timeRangeIndex)).get();
    ChartTry state;
    state.answered = ChartAnswered(response);
    state.missing = response.statusCode == 404;
    std::string raw = TakeBody(
        std::move(response), httpFailures, "yahoo", operation);
    state.applied = ApplyYahooChartPayload(
        std::move(raw), timeRangeIndex <= 1, result);
    return state;
}

ChartTry TryYahooChart(const std::string& yahooTicker,
                       int timeRangeIndex,
                       HttpFailureSummary& httpFailures,
                       StockData& result) {
    ChartTry state = FetchYahooChart("query1.finance.yahoo.com",
                                    yahooTicker,
                                    timeRangeIndex,
                                    httpFailures,
                                    "stock-chart-fallback",
                                    result);
    if (state.applied || state.missing)
        return state;

    ChartTry retry = FetchYahooChart("query2.finance.yahoo.com",
                                     yahooTicker,
                                     timeRangeIndex,
                                     httpFailures,
                                     "stock-chart-fallback",
                                     result);
    retry.answered = retry.answered || state.answered;
    return retry;
}

bool RecoverInitialChartRange(const std::string& yahooTicker,
                              bool requestedChartRangeAnswered,
                              HttpFailureSummary& httpFailures,
                              StockFetchResult& fetch) {
    StockData& result = fetch.marketData;

    const ChartTry fiveDay = TryYahooChart(yahooTicker, 1, httpFailures, result);
    if (fiveDay.applied) {
        const auto decision = ResolveChartRange(false, true, false, false);
        fetch.resolvedTimeRangeIndex = decision.rangeIndex;
        result.tradingStatus.chartAvailability =
            requestedChartRangeAnswered ? decision.availability
                                        : ChartAvailability::ActiveSelectedRange;
        return true;
    }

    const ChartTry oneMonth = TryYahooChart(yahooTicker, 2, httpFailures, result);
    if (oneMonth.applied) {
        const auto decision = ResolveChartRange(false, false, true, false);
        fetch.resolvedTimeRangeIndex = decision.rangeIndex;
        const bool completeNegativeEvidence =
            requestedChartRangeAnswered && fiveDay.answered;
        result.tradingStatus.chartAvailability =
            completeNegativeEvidence ? decision.availability
                                     : ChartAvailability::ActiveSelectedRange;
        result.tradingStatus.corporateAction =
            completeNegativeEvidence ? decision.defaultCorporateAction
                                     : CorporateActionStatus::None;
        return true;
    }

    const ChartTry allHistory =
        TryYahooChart(yahooTicker, ALL_TIME_RANGE_INDEX, httpFailures, result);
    if (allHistory.applied) {
        const auto decision = ResolveChartRange(false, false, false, true);
        fetch.resolvedTimeRangeIndex = decision.rangeIndex;
        const bool completeNegativeEvidence = requestedChartRangeAnswered &&
                                              fiveDay.answered && oneMonth.answered;
        result.tradingStatus.chartAvailability =
            completeNegativeEvidence ? decision.availability
                                     : ChartAvailability::ActiveSelectedRange;
        result.tradingStatus.corporateAction =
            completeNegativeEvidence ? decision.defaultCorporateAction
                                     : CorporateActionStatus::None;
        return true;
    }

    result.tradingStatus.chartAvailability =
        requestedChartRangeAnswered && fiveDay.answered && oneMonth.answered &&
                allHistory.answered
            ? ResolveChartRange(false, false, false, false).availability
            : ChartAvailability::Unknown;
    return false;
}


constexpr std::time_t kHistoricalTradingStatusProbeAgeSeconds =
    static_cast<std::time_t>(7 * 24 * 60 * 60);

bool HasRecentYahooQuoteSnapshot(const std::string& yahooQuoteRaw,
                                 std::time_t now) {
    if (yahooQuoteRaw.empty())
        return false;
    const auto quotes =
        squarestar::providers::ParseYahooQuoteBatchPayload(yahooQuoteRaw);
    for (const auto& quote : quotes) {
        if (quote.currentPrice <= 0.0 || quote.timestamp <= 0)
            continue;
        const std::time_t age = now - quote.timestamp;
        if (age >= -24 * 60 * 60 && age <= kHistoricalTradingStatusProbeAgeSeconds)
            return true;
    }
    return false;
}

void ProbeHistoricalTradingStatus(const std::string& yahooTicker,
                                  HttpFailureSummary& httpFailures,
                                  StockData& result) {
    StockData probe;
    const ChartTry oneDay = TryYahooChart(yahooTicker, 0, httpFailures, probe);
    if (oneDay.applied) {
        result.tradingStatus.chartAvailability =
            ChartAvailability::ActiveSelectedRange;
        return;
    }

    probe = StockData{};
    const ChartTry fiveDay = TryYahooChart(yahooTicker, 1, httpFailures, probe);
    if (fiveDay.applied) {
        result.tradingStatus.chartAvailability = ChartAvailability::NoTradingToday;
        return;
    }

    // Only classify a stopped symbol when both recent-range requests produced
    // authoritative answers. A transport outage must never look like a delist.
    if (oneDay.answered && fiveDay.answered) {
        result.tradingStatus.chartAvailability =
            ChartAvailability::TradingStoppedWithHistory;
        result.tradingStatus.corporateAction =
            CorporateActionStatus::SuspectedStopped;
    }
}

void SetFullLoadFailureMessage(StockData& result,
                               const StockProviderMergeResult& providerMerge,
                               const HttpFailureSummary& httpFailures) {
    if (result.success)
        return;

    const bool isUSMarket = providerMerge.finnhubProfileCountry.empty() ||
                            providerMerge.finnhubProfileCountry == "US";
    if (!isUSMarket) {
        result.errorMessage = "This symbol or market is not supported";
    } else if (result.tradingStatus.chartAvailability ==
                   ChartAvailability::NoChartHistory &&
               result.errorMessage.empty()) {
        result.errorMessage = "Stock unavailable / unlisted ticker";
    } else if (providerMerge.finnhubProfileExists && result.errorMessage.empty()) {
        result.errorMessage = "Stock unavailable / unlisted ticker";
    } else if (result.errorMessage.empty()) {
        const char* networkMessage = StockFetchHttpFailureMessage(httpFailures);
        result.errorMessage =
            networkMessage[0] != '\0' ? networkMessage : "Ticker not found";
    }
}

} // namespace

StockFetchResult FetchStockData(
    const std::string& ticker,
    int timeRangeIndex,
    bool isBackground,
    bool tolerateOpeningNoChart,
    uint32_t detailMask,
    FetchKind kind) {
    HttpFailureSummary httpFailures;
    StockFetchResult fetch;
    StockData& result = fetch.marketData;
    fetch.resolvedTimeRangeIndex = timeRangeIndex;
    const auto finish = [&]() {
        fetch.FinalizeFromMarketData(httpFailures.rateLimited);
        return std::move(fetch);
    };
    switch (ValidateStockFetchInputs(ticker, timeRangeIndex, kind)) {
    case StockFetchInputError::InvalidTickerOrRange:
        result.errorMessage = "Ticker not found";
        return finish();
    case StockFetchInputError::UnsupportedMarket:
        result.errorMessage = "This symbol or market is not supported";
        return finish();
    case StockFetchInputError::None:
        break;
    }
    const std::optional<MarketSymbol> symbol = MarketSymbol::Parse(ticker);
    if (!symbol) {
        result.errorMessage = "Ticker not found";
        return finish();
    }
    const std::string& canonicalTicker = symbol->Canonical();
    const std::string yahooTicker = symbol->Yahoo();
    const std::string finnhubTicker = symbol->Finnhub();
    const bool yahooOnlyInstrument = symbol->IsYahooFutures();
    result.instrumentNature = yahooOnlyInstrument
                                  ? InstrumentNature::DerivativeContract
                                  : InstrumentNature::Unknown;
    const std::string cacheKey = canonicalTicker + ":" + std::to_string(timeRangeIndex) + ":" +
                                 (tolerateOpeningNoChart ? "1" : "0") + ":" +
                                 std::to_string(detailMask);
    if (kind == FetchKind::Full && !isBackground) {
        if (auto cached = ConsumeStockMemoryCache(
                cacheKey, yahooOnlyInstrument || squarestar::market::CachedMarketOpen()))
            return std::move(*cached);
    }
    const bool fullLoad = kind == FetchKind::Full;
    const bool detailsLoad = kind == FetchKind::Details;
    // A lean full load gets the chart series from Yahoo Chart and the session
    // quote from Yahoo Quote. Finnhub stays off the normal critical path and is
    // retained only as a fallback when the authoritative quote request fails.
    const bool leanCoreLoad = fullLoad && detailMask == 0;
    const bool needChart = fullLoad || kind == FetchKind::Chart;
    const bool needLiveQuote = kind == FetchKind::LiveQuote ||
                               kind == FetchKind::AlertQuote ||
                               (fullLoad && !leanCoreLoad);
    const bool wantMetrics =
        (detailsLoad || fullLoad) && (detailMask & StockFetchMetrics) != 0;
    const bool wantProfile =
        (detailsLoad || fullLoad) && (detailMask & StockFetchProfile) != 0;
    const bool wantNews =
        (detailsLoad || fullLoad) && (detailMask & StockFetchNews) != 0;
    auto apiKeySnapshot = GetFinnhubApiKeySnapshot();
    const bool hasFinnhubKey = !apiKeySnapshot.value.empty();
    const std::uint64_t apiKeyRevision = apiKeySnapshot.revision;
    std::string apiKey =
        hasFinnhubKey ? UrlEncode(apiKeySnapshot.value) : std::string{};
    const squarestar::secrets::ScopedSecureClear clearEncodedApiKey(apiKey);
    SecureClear(apiKeySnapshot.value);
    std::future<HttpResponse> chartFuture, fhMetricsFuture, fhProfileFuture, newsFuture;
    std::shared_future<HttpResponse> fhQuoteFuture, yQuoteFuture;
    if (needChart) {
        std::string url = YahooChartUrl(
            "query1.finance.yahoo.com", yahooTicker, timeRangeIndex);
        chartFuture = !isBackground ? QueueRealtimeHttpGet(std::move(url))
                                    : QueueHttpGet(std::move(url));
    }
    if (hasFinnhubKey && needLiveQuote && !yahooOnlyInstrument &&
        kind != FetchKind::AlertQuote) {
        std::string url = "https://finnhub.io/api/v1/quote?symbol=" + finnhubTicker +
                          "&token=" + apiKey;
        fhQuoteFuture =
            (kind == FetchKind::LiveQuote || kind == FetchKind::AlertQuote || !isBackground)
                ? QueueSingleFlightQuoteHttpGet(
                      "finnhub:" + finnhubTicker + ":" +
                          std::to_string(apiKeyRevision),
                      std::move(url))
                : QueueHttpGet(std::move(url)).share();
    }
    if (hasFinnhubKey && wantMetrics && !yahooOnlyInstrument) {
        fhMetricsFuture =
            QueueHttpGet("https://finnhub.io/api/v1/stock/metric?symbol=" + finnhubTicker +
                         "&metric=all&token=" + apiKey);
    }
    if (hasFinnhubKey && wantProfile && !yahooOnlyInstrument) {
        fhProfileFuture =
            QueueHttpGet("https://finnhub.io/api/v1/stock/profile2?symbol=" + finnhubTicker +
                         "&token=" + apiKey);
    }
    // Session quote fields come from the quote endpoint, never from chart-range
    // metadata. Full loads request it concurrently with the chart.
    const bool needYahooQuote =
        fullLoad || needLiveQuote || (detailsLoad && wantMetrics);
    if (needYahooQuote) {
        std::string url =
            "https://query1.finance.yahoo.com/v7/finance/quote?symbols=" + yahooTicker;
        // Yahoo's quote endpoint requires the cookie/crumb session. Prefer
        // correctness over sharing this request through the generic single-flight
        // transport; Finnhub still retains single-flight deduplication.
        yQuoteFuture = QueueYahooAuthenticatedGet(std::move(url)).share();
    }
    if (hasFinnhubKey && wantNews && !yahooOnlyInstrument) {
        newsFuture = QueueHttpGet(CompanyNewsUrl(finnhubTicker, apiKey, 7));
    }
    std::string chartRaw, fhQuoteRaw, fhMetricsRaw, fhProfileRaw, yQuoteRaw, newsRaw;
    ChartTry requestedChart;
    if (chartFuture.valid()) {
        HttpResponse response = chartFuture.get();
        requestedChart.answered = ChartAnswered(response);
        requestedChart.missing = response.statusCode == 404;
        chartRaw = TakeBody(
            std::move(response), httpFailures, "yahoo", "stock-chart");
    }
    if (fhQuoteFuture.valid())
        fhQuoteRaw = TakeBody(
            fhQuoteFuture.get(), httpFailures, "finnhub", "stock-quote");
    if (fhMetricsFuture.valid())
        fhMetricsRaw = TakeBody(
            fhMetricsFuture.get(), httpFailures, "finnhub", "stock-metrics");
    if (fhProfileFuture.valid())
        fhProfileRaw = TakeBody(
            fhProfileFuture.get(), httpFailures, "finnhub", "stock-profile");
    if (yQuoteFuture.valid())
        yQuoteRaw = TakeBody(
            yQuoteFuture.get(), httpFailures, "yahoo", "stock-quote");
    if (newsFuture.valid())
        newsRaw = TakeBody(
            newsFuture.get(), httpFailures, "finnhub", "stock-news");
    // A clear or replacement invalidates responses started with the previous
    // credential. Yahoo data remains usable, but stale Finnhub payloads must
    // not be published after the key changes.
    if (!IsApiKeyRevisionCurrent(apiKeyRevision)) {
        fhQuoteRaw.clear();
        fhMetricsRaw.clear();
        fhProfileRaw.clear();
        newsRaw.clear();
    }
    bool chartApplied = false;
    if (needChart) {
        requestedChart.applied =
            ApplyYahooChartPayload(std::move(chartRaw), timeRangeIndex <= 1, result);
        if (!requestedChart.applied && !requestedChart.missing) {
            ChartTry retry = FetchYahooChart(
                "query2.finance.yahoo.com",
                yahooTicker,
                timeRangeIndex,
                httpFailures,
                "stock-chart-retry",
                result);
            requestedChart.applied = retry.applied;
            requestedChart.answered = requestedChart.answered || retry.answered;
            requestedChart.missing = retry.missing;
        }
        chartApplied = requestedChart.applied;
        // A missing intraday series is not enough evidence to classify a symbol
        // as unavailable. Full initial loads try progressively wider ranges.
        if (!chartApplied && fullLoad && timeRangeIndex == 0) {
            chartApplied = RecoverInitialChartRange(
                yahooTicker, requestedChart.answered, httpFailures, fetch);
        } else if (chartApplied) {
            result.tradingStatus.chartAvailability =
                ChartAvailability::ActiveSelectedRange;
            // A saved 1M/1Y/5Y/All tab can still render historical data after
            // the company stops trading. If Yahoo's quote is missing or stale,
            // probe 1D and 5D without replacing the user's selected chart. This
            // keeps corporate-action detection alive after restarts and range
            // changes instead of only detecting delists from an initial 1D load.
            if (fullLoad && timeRangeIndex >= 2 && !yahooOnlyInstrument &&
                !HasRecentYahooQuoteSnapshot(yQuoteRaw, std::time(nullptr))) {
                ProbeHistoricalTradingStatus(
                    yahooTicker, httpFailures, result);
            }
        }
        if (chartApplied && !yahooOnlyInstrument)
            result.instrumentNature = InstrumentNature::PublicMarketSecurity;
        if (kind == FetchKind::Chart) {
            result.success = chartApplied;
            if (!result.success)
                result.errorMessage = "Unable to refresh chart";
            return finish();
        }
    }

    const bool needsCorporateActionEvidence =
        result.tradingStatus.chartAvailability ==
        ChartAvailability::TradingStoppedWithHistory;
    if (needsCorporateActionEvidence && newsRaw.empty() && hasFinnhubKey &&
        !yahooOnlyInstrument && IsApiKeyRevisionCurrent(apiKeyRevision)) {
        // A slightly wider window than the normal news surface catches a
        // completed transaction followed by a holiday/weekend while remaining
        // bounded and cheap. Classification still requires explicit wording.
        newsRaw = TakeBody(
            QueueHttpGet(CompanyNewsUrl(finnhubTicker, apiKey, 14)).get(),
            httpFailures,
            "finnhub",
            "corporate-action-news");
        if (!IsApiKeyRevisionCurrent(apiKeyRevision))
            newsRaw.clear();
    }

    // If Yahoo's authoritative quote failed, retain the existing Finnhub
    // fallback for lean full loads. Avoid issuing both quote requests when Yahoo
    // already answered successfully.
    if (leanCoreLoad && !yahooOnlyInstrument && yQuoteRaw.empty() &&
        (!chartApplied || result.previousClose <= 0.0) && fhQuoteRaw.empty() &&
        hasFinnhubKey && IsApiKeyRevisionCurrent(apiKeyRevision)) {
        fhQuoteRaw = TakeBody(
            QueueRealtimeHttpGet("https://finnhub.io/api/v1/quote?symbol=" +
                                 finnhubTicker + "&token=" + apiKey)
                .get(),
            httpFailures,
            "finnhub",
            "stock-quote-fallback");
        if (!IsApiKeyRevisionCurrent(apiKeyRevision))
            fhQuoteRaw.clear();
    }

    StockProviderPayloads providerPayloads;
    providerPayloads.yahooQuote = std::move(yQuoteRaw);
    providerPayloads.finnhubQuote = std::move(fhQuoteRaw);
    providerPayloads.finnhubMetrics = std::move(fhMetricsRaw);
    providerPayloads.finnhubProfile = std::move(fhProfileRaw);
    providerPayloads.finnhubNews = std::move(newsRaw);
    const StockProviderMergeResult providerMerge = ApplyStockProviderPayloads(
        std::move(providerPayloads),
        StockProviderMergeOptions{kind,
                                  timeRangeIndex,
                                  detailsLoad,
                                  wantMetrics,
                                  wantNews,
                                  needsCorporateActionEvidence},
        fetch);
    if (kind == FetchKind::LiveQuote || kind == FetchKind::AlertQuote) {
        if (!result.success)
            result.errorMessage = "Unable to refresh quote";
        return finish();
    }
    if (detailsLoad) {
        // A Yahoo quote can still provide the core market-cap / P-E / volume /
        // 52-week metrics when Finnhub's optional metric endpoint times out.
        // Keep the Finnhub detail bit unresolved in that case so the caller can
        // retry it later for fields such as beta and dividend yield.
        const bool hasMetricFallback = wantMetrics && HasAnyMarketMetricData(result);
        result.success = result.resolvedDetailMask != 0 || hasMetricFallback;
        if (!result.success)
            result.errorMessage = "Unable to refresh optional stock details";
        return finish();
    }
    SetFullLoadFailureMessage(result, providerMerge, httpFailures);
    result.currency = "USD";
    fetch.FinalizeFromMarketData(httpFailures.rateLimited);
    const bool cacheableCompany = !wantProfile || HasResolvedCompanyName(result.companyName);
    if (kind == FetchKind::Full && !isBackground && result.success && cacheableCompany &&
        ApplicationRuntime().CurrentUiMode() != AppUiMode::LiteGui) {
        StoreStockMemoryCache(cacheKey, fetch);
    }
    return fetch;
}

} // namespace squarestar::marketdata
