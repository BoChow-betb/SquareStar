#include "services/screener_service.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <iterator>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "application/app_limits.hpp"
#include "domain/market_symbol.hpp"
#include "domain/screener_routes.hpp"
#include "services/screener_payload.hpp"

namespace squarestar::providers {

std::string BuildYahooFiftyTwoWeekScreenerBody(bool gainers, std::size_t limit) {
    // Match Yahoo Finance's stock 52-week screens: US equities, >= $2B market
    // cap, >= 15K volume, and the seven mainstream US listing venues shown by
    // the Yahoo screener UI. The direction and sort use actual 52-week % change.
    const std::size_t size = std::min<std::size_t>(limit, 250);
    const char* comparison = gainers ? "GTE" : "LTE";
    const char* sortType = gainers ? "DESC" : "ASC";
    std::string body;
    body.reserve(1024);
    body += "{\"offset\":0,\"size\":" + std::to_string(size) +
            ",\"sortField\":\"fiftytwowkpercentchange\",\"sortType\":\"" +
            sortType +
            "\",\"quoteType\":\"EQUITY\",\"query\":{\"operator\":\"AND\",\"operands\":[";
    body += "{\"operator\":\"" + std::string(comparison) +
            "\",\"operands\":[\"fiftytwowkpercentchange\",0]},";
    body += "{\"operator\":\"EQ\",\"operands\":[\"region\",\"us\"]},";
    body += "{\"operator\":\"GTE\",\"operands\":[\"intradaymarketcap\",2000000000]},";
    body += "{\"operator\":\"GTE\",\"operands\":[\"dayvolume\",15000]},";
    body += "{\"operator\":\"OR\",\"operands\":[";
    static constexpr const char* exchanges[] = {
        "ASE", "BTS", "NCM", "NGM", "NMS", "NYQ", "PCX"};
    for (std::size_t i = 0; i < std::size(exchanges); ++i) {
        if (i != 0)
            body.push_back(',');
        body += "{\"operator\":\"EQ\",\"operands\":[\"exchange\",\"";
        body += exchanges[i];
        body += "\"]}";
    }
    body += "]}]},\"userId\":\"\",\"userIdType\":\"guid\"}";
    return body;
}

namespace {

using squarestar::application::ScreenerItem;
using squarestar::http::HttpResponse;
using squarestar::market::FindScreenerRouteByGuiId;
using squarestar::market::ScreenerRoute;
using squarestar::market::YahooSymbolKey;

// Queue the visible fullscreen page in one wave; the batch executor still
// enforces the network concurrency limit.
constexpr std::size_t kMaximumQueuedChartRequestsPerWave =
    squarestar::application::kMaximizedOverviewRows;

bool Cancelled(const ScreenerCancelCheck& cancelled) {
    return cancelled && cancelled();
}

bool PublishPartial(const std::vector<ScreenerItem>& items,
                    const ScreenerFetchDependencies& dependencies) {
    return !dependencies.publishPartial || dependencies.publishPartial(items);
}

HttpResponse AwaitHttpResponse(std::future<HttpResponse>& future) noexcept {
    try {
        return future.get();
    } catch (...) {
        return HttpResponse{{}, 0, squarestar::http::HttpError::TransferFailure};
    }
}

HttpResponse AwaitHttpResponse(std::future<HttpResponse>&& future) noexcept {
    return AwaitHttpResponse(future);
}

std::future<HttpResponse> QueueBatchGet(
    std::string url,
    const ScreenerCancelCheck& cancelled,
    const ScreenerFetchDependencies& dependencies) {
    return dependencies.queueBatchHttpGet(std::move(url), cancelled);
}

std::future<HttpResponse> QueuePublicGet(
    std::string url,
    const ScreenerCancelCheck& cancelled,
    const ScreenerFetchDependencies& dependencies) {
    return dependencies.queueHttpGet(std::move(url), cancelled);
}

std::future<HttpResponse> QueueAuthenticatedGet(
    std::string url,
    const ScreenerCancelCheck& cancelled,
    const ScreenerFetchDependencies& dependencies) {
    return dependencies.queueYahooAuthenticatedGet(std::move(url), cancelled);
}

HttpResponse FetchPublicYahooGet(
    const std::string& url,
    const ScreenerCancelCheck& cancelled,
    const ScreenerFetchDependencies& dependencies) {
    if (!dependencies.queueHttpGet)
        return HttpResponse{{}, 0, squarestar::http::HttpError::TransferFailure};
    return AwaitHttpResponse(QueuePublicGet(url, cancelled, dependencies));
}

HttpResponse FetchPublicYahooGetWithFallback(
    const std::string& primaryUrl,
    const std::string& secondaryUrl,
    const ScreenerCancelCheck& cancelled,
    const ScreenerFetchDependencies& dependencies) {
    HttpResponse response = FetchPublicYahooGet(primaryUrl, cancelled, dependencies);
    if (response.IsSuccess() || Cancelled(cancelled))
        return response;
    return FetchPublicYahooGet(secondaryUrl, cancelled, dependencies);
}

std::vector<std::future<HttpResponse>> QueueYahooBatches(
    const std::vector<std::string>& symbols,
    std::size_t count,
    std::size_t batchSize,
    const char* prefix,
    const char* suffix,
    const ScreenerCancelCheck& cancelled,
    const ScreenerFetchDependencies& dependencies) {
    count = std::min(count, symbols.size());
    std::vector<std::future<HttpResponse>> futures;
    const bool authenticatedAvailable =
        static_cast<bool>(dependencies.queueYahooAuthenticatedGet);
    const bool batchAvailable = static_cast<bool>(dependencies.queueBatchHttpGet);
    if ((!authenticatedAvailable && !batchAvailable) || batchSize == 0)
        return futures;
    futures.reserve((count + batchSize - 1) / batchSize);
    for (std::size_t start = 0; start < count; start += batchSize) {
        if (Cancelled(cancelled))
            break;
        const std::size_t end = std::min(start + batchSize, count);
        std::string joined;
        std::size_t joinedBytes = end > start ? end - start - 1 : 0;
        for (std::size_t i = start; i < end; ++i)
            joinedBytes += symbols[i].size();
        joined.reserve(joinedBytes);
        for (std::size_t i = start; i < end; ++i) {
            if (!joined.empty())
                joined.push_back(',');
            joined += symbols[i];
        }
        std::string url = std::string(prefix) + joined + suffix;
        if (authenticatedAvailable)
            futures.push_back(QueueAuthenticatedGet(
                std::move(url), cancelled, dependencies));
        else
            futures.push_back(QueueBatchGet(
                std::move(url), cancelled, dependencies));
    }
    return futures;
}

bool FetchYahooQuoteBatches(const std::vector<std::string>& yahooSymbols,
                            const ScreenerItemIndex& itemIndex,
                            std::vector<ScreenerItem>& items,
                            const char* host,
                            const ScreenerCancelCheck& cancelled,
                            const ScreenerFetchDependencies& dependencies) {
    if (yahooSymbols.empty())
        return true;
    const std::string prefix = std::string("https://") + host + "/v7/finance/quote?symbols=";
    auto futures = QueueYahooBatches(yahooSymbols,
                                     yahooSymbols.size(),
                                     50,
                                     prefix.c_str(),
                                     "",
                                     cancelled,
                                     dependencies);
    bool cancelledDuringBatch = Cancelled(cancelled);
    for (auto& future : futures) {
        HttpResponse response = AwaitHttpResponse(future);
        cancelledDuringBatch = cancelledDuringBatch || Cancelled(cancelled);
        if (cancelledDuringBatch)
            continue;
        std::string payload = response.IsSuccess() ? std::move(response.body) : std::string{};
        ApplyYahooQuoteResponsePayload(std::move(payload), itemIndex, items);
        if (!PublishPartial(items, dependencies))
            cancelledDuringBatch = true;
    }
    return !cancelledDuringBatch;
}

bool PublishTrendingEquityProgress(
    const std::vector<ScreenerItem>& items,
    const std::unordered_set<std::string>& equitySymbols,
    std::size_t limit,
    const ScreenerFetchDependencies& dependencies) {
    if (!dependencies.publishPartial || equitySymbols.empty())
        return true;
    std::vector<ScreenerItem> equities;
    equities.reserve(std::min(limit, items.size()));
    for (const ScreenerItem& item : items) {
        if (equitySymbols.find(YahooSymbolKey(item.symbol)) == equitySymbols.end())
            continue;
        equities.push_back(item);
        if (equities.size() >= limit)
            break;
    }
    return equities.empty() || PublishPartial(equities, dependencies);
}

bool FetchYahooTrendingQuoteBatches(
    const std::vector<std::string>& yahooSymbols,
    const ScreenerItemIndex& itemIndex,
    std::vector<ScreenerItem>& items,
    const char* host,
    std::size_t limit,
    const ScreenerCancelCheck& cancelled,
    const ScreenerFetchDependencies& dependencies,
    std::unordered_set<std::string>& equitySymbols) {
    if (yahooSymbols.empty())
        return true;
    const std::string prefix =
        std::string("https://") + host + "/v7/finance/quote?symbols=";
    auto futures = QueueYahooBatches(yahooSymbols,
                                     yahooSymbols.size(),
                                     50,
                                     prefix.c_str(),
                                     "",
                                     cancelled,
                                     dependencies);
    bool cancelledDuringBatch = Cancelled(cancelled);
    for (auto& future : futures) {
        HttpResponse response = AwaitHttpResponse(future);
        cancelledDuringBatch = cancelledDuringBatch || Cancelled(cancelled);
        if (cancelledDuringBatch)
            continue;
        if (!response.IsSuccess())
            continue;
        ApplyYahooEquityQuoteResponsePayload(
            std::move(response.body), itemIndex, items, equitySymbols);
        // Publish only rows already confirmed as equities. The quote batches are
        // queued together, so the first visible page can appear as soon as the
        // first batch resolves without exposing crypto/unresolved candidates.
        if (!PublishTrendingEquityProgress(
                items, equitySymbols, limit, dependencies)) {
            cancelledDuringBatch = true;
        }
    }
    return !cancelledDuringBatch;
}

std::vector<ScreenerItem> FetchYahooWatchlist(
    const std::vector<std::string>& symbols,
    const ScreenerCancelCheck& cancelled,
    const ScreenerFetchDependencies& dependencies) {
    std::vector<ScreenerItem> items;
    items.reserve(symbols.size());
    std::vector<std::string> yahooSymbols;
    yahooSymbols.reserve(symbols.size());
    ScreenerItemIndex itemIndex;
    itemIndex.reserve(symbols.size());
    for (const std::string& symbol : symbols) {
        ScreenerItem item;
        item.symbol = symbol;
        const std::string yahooSymbol = YahooSymbolKey(symbol);
        itemIndex.try_emplace(yahooSymbol, items.size());
        yahooSymbols.push_back(yahooSymbol);
        items.push_back(std::move(item));
    }

    // Watchlist rows are user-curated and stable. Do not publish a symbol-only
    // placeholder and then replace the company column a fraction of a second
    // later. Resolve quote/name batches off-screen and publish the list as one
    // coherent snapshot. Same-route refreshes already keep the previous complete
    // snapshot visible until this replacement is ready.
    ScreenerFetchDependencies stableDependencies = dependencies;
    stableDependencies.publishPartial = {};
    if (!FetchYahooQuoteBatches(yahooSymbols,
                                itemIndex,
                                items,
                                "query1.finance.yahoo.com",
                                cancelled,
                                stableDependencies)) {
        return {};
    }
    if (Cancelled(cancelled))
        return {};
    std::vector<std::string> missingQuotes;
    missingQuotes.reserve(yahooSymbols.size());
    for (std::size_t index = 0; index < items.size(); ++index) {
        if (!items[index].resolved || !items[index].hasPrice ||
            !items[index].hasChangePercent || !items[index].hasMarketCap) {
            missingQuotes.push_back(yahooSymbols[index]);
        }
    }
    if (!FetchYahooQuoteBatches(missingQuotes,
                                itemIndex,
                                items,
                                "query2.finance.yahoo.com",
                                cancelled,
                                stableDependencies)) {
        return {};
    }
    if (Cancelled(cancelled))
        return {};
    MarkInactiveScreenerItems(items);
    return items;
}

std::vector<ScreenerItem> FetchYahooTrendingScreener(
    std::size_t limit,
    const ScreenerCancelCheck& cancelled,
    const ScreenerFetchDependencies& dependencies) {
    std::vector<ScreenerItem> items;
    if (limit == 0 ||
        (!dependencies.queueYahooAuthenticatedGet && !dependencies.queueHttpGet) ||
        (!dependencies.queueYahooAuthenticatedGet && !dependencies.queueBatchHttpGet))
        return items;

    // Yahoo's trending feed is multi-asset. The stock page keeps the feed's
    // ranking but removes non-equities. Do that from quoteType, not ticker
    // suffix heuristics (which would miss crypto and other
    // future non-stock instruments).
    const std::size_t candidateLimit = std::min<std::size_t>(
        100, std::max<std::size_t>(25, limit));
    const std::string suffix =
        "/v1/finance/trending/US?lang=en-US&region=US&count=" +
        std::to_string(candidateLimit) + "&corsDomain=finance.yahoo.com";

    HttpResponse trendingResponse = FetchPublicYahooGetWithFallback(
        "https://query1.finance.yahoo.com" + suffix,
        "https://query2.finance.yahoo.com" + suffix,
        cancelled,
        dependencies);
    std::vector<std::string> yahooSymbols = ParseYahooTrendingSymbols(
        trendingResponse.IsSuccess() ? std::move(trendingResponse.body) : std::string{},
        candidateLimit);
    if (yahooSymbols.empty() || Cancelled(cancelled))
        return items;

    items.reserve(yahooSymbols.size());
    ScreenerItemIndex itemIndex;
    itemIndex.reserve(yahooSymbols.size());
    for (const std::string& yahooSymbol : yahooSymbols) {
        ScreenerItem item;
        item.symbol = yahooSymbol;
        item.name = yahooSymbol;
        itemIndex.try_emplace(yahooSymbol, items.size());
        items.push_back(std::move(item));
    }
    std::unordered_set<std::string> equitySymbols;
    equitySymbols.reserve(yahooSymbols.size());
    // Trending is multi-asset. Resolve quoteType in batches and publish only
    // confirmed equities so crypto rows never flash in the table.
    if (!FetchYahooTrendingQuoteBatches(yahooSymbols,
                                        itemIndex,
                                        items,
                                        "query1.finance.yahoo.com",
                                        limit,
                                        cancelled,
                                        dependencies,
                                        equitySymbols)) {
        return {};
    }
    if (Cancelled(cancelled))
        return {};

    std::vector<std::string> missingQuotes;
    missingQuotes.reserve(yahooSymbols.size());
    for (std::size_t index = 0; index < items.size(); ++index) {
        if (!items[index].resolved)
            missingQuotes.push_back(yahooSymbols[index]);
    }
    if (!missingQuotes.empty()) {
        if (!FetchYahooTrendingQuoteBatches(missingQuotes,
                                            itemIndex,
                                            items,
                                            "query2.finance.yahoo.com",
                                            limit,
                                            cancelled,
                                            dependencies,
                                            equitySymbols)) {
            return {};
        }
    }
    if (Cancelled(cancelled))
        return {};

    std::vector<ScreenerItem> equities;
    equities.reserve(std::min(limit, items.size()));
    for (ScreenerItem& item : items) {
        if (equitySymbols.find(YahooSymbolKey(item.symbol)) == equitySymbols.end())
            continue;
        equities.push_back(std::move(item));
        if (equities.size() >= limit)
            break;
    }
    items = std::move(equities);
    if (items.empty())
        return items;

    MarkInactiveScreenerItems(items);
    return items;
}

std::vector<ScreenerItem> FetchYahooFiftyTwoWeekScreener(
    bool gainers,
    std::size_t limit,
    const ScreenerCancelCheck& cancelled,
    const ScreenerFetchDependencies& dependencies) {
    std::vector<ScreenerItem> items;
    if (limit == 0 ||
        !dependencies.queueYahooScreenerPost)
        return items;
    items.reserve(limit);
    const std::string body = BuildYahooFiftyTwoWeekScreenerBody(gainers, limit);
    HttpResponse response = AwaitHttpResponse(
        dependencies.queueYahooScreenerPost(body, cancelled));
    AppendYahooScreenerPayload(
        response.IsSuccess() ? std::move(response.body) : std::string{}, limit, items);
    if (items.empty() || Cancelled(cancelled))
        return items;

    MarkInactiveScreenerItems(items);
    return items;
}

std::vector<ScreenerItem> FetchYahooPredefinedScreener(
    const ScreenerRoute& route,
    std::size_t limit,
    const ScreenerCancelCheck& cancelled,
    const ScreenerFetchDependencies& dependencies) {
    std::vector<ScreenerItem> items;
    if (limit == 0 ||
        !dependencies.queueHttpGet)
        return items;
    items.reserve(limit);
    const std::string suffix =
        "/v1/finance/screener/predefined/saved?scrIds=" + std::string(route.yahooId) +
        "&count=" + std::to_string(limit);
    HttpResponse response = FetchPublicYahooGetWithFallback(
        "https://query1.finance.yahoo.com" + suffix,
        "https://query2.finance.yahoo.com" + suffix,
        cancelled,
        dependencies);
    AppendYahooScreenerPayload(
        response.IsSuccess() ? std::move(response.body) : std::string{}, limit, items);
    if (items.empty() || Cancelled(cancelled))
        return items;

    MarkInactiveScreenerItems(items);
    return items;
}

struct PendingTrendRequest {
    std::size_t index = 0;
    std::string secondaryUrl;
    std::future<HttpResponse> primary;
};

bool FetchTrendWaveWithFallback(std::vector<ScreenerItem>& items,
                                std::size_t start,
                                std::size_t end,
                                const ScreenerCancelCheck& cancelled,
                                const ScreenerFetchDependencies& dependencies) {
    std::vector<PendingTrendRequest> requests;
    requests.reserve(end - start);
    for (std::size_t index = start; index < end; ++index) {
        if (Cancelled(cancelled))
            break;
        if (items[index].sparkline.size() >= 2)
            continue;
        if (items[index].symbol.empty()) {
            items[index].sparklineAttempted = true;
            continue;
        }
        const std::string yahooSymbol = YahooSymbolKey(items[index].symbol);
        PendingTrendRequest request;
        request.index = index;
        request.secondaryUrl =
            "https://query2.finance.yahoo.com/v8/finance/chart/" + yahooSymbol +
            "?range=5d&interval=1d&includePrePost=false&events=none";
        request.primary = QueueBatchGet(
            "https://query1.finance.yahoo.com/v8/finance/chart/" + yahooSymbol +
                "?range=5d&interval=1d&includePrePost=false&events=none",
            cancelled,
            dependencies);
        requests.push_back(std::move(request));
    }

    bool publicationAccepted = true;
    for (PendingTrendRequest& request : requests) {
        HttpResponse response = AwaitHttpResponse(request.primary);
        if (Cancelled(cancelled) || !publicationAccepted)
            continue;

        // A slow primary is not a failure signal. Starting a second transfer
        // merely because 175 ms elapsed doubled Yahoo traffic during upstream
        // latency incidents. Only fail over after the primary has completed
        // unsuccessfully, so at most one host is in flight per symbol.
        if (!response.IsSuccess()) {
            response = AwaitHttpResponse(QueueBatchGet(
                request.secondaryUrl, cancelled, dependencies));
        }
        if (Cancelled(cancelled))
            continue;

        if (response.IsSuccess()) {
            ApplyYahooScreenerChartPayload(
                std::move(response.body), items[request.index], true);
        }
        // Mark the attempt complete only after the request resolves so in-flight rows
        // stay pending instead of briefly showing N/A.
        items[request.index].sparklineAttempted = true;
        publicationAccepted = PublishPartial(items, dependencies);
    }
    return !Cancelled(cancelled) && publicationAccepted;
}

} // namespace

bool EnrichMarketScreenerSparklines(
    std::vector<ScreenerItem>& items,
    std::size_t firstIndex,
    std::size_t count,
    const ScreenerCancelCheck& cancelled,
    const ScreenerFetchDependencies& dependencies) {
    if (items.empty() || count == 0 || firstIndex >= items.size() ||
        Cancelled(cancelled) ||
        !dependencies.queueBatchHttpGet)
        return !Cancelled(cancelled);

    // Match the largest Overview page exactly so every visible row receives
    // a 5D trend without spending requests on the next page.
    constexpr std::size_t kMaximumRowsPerTrendPass =
        squarestar::application::kMaximizedOverviewRows;
    const std::size_t end = std::min(
        items.size(), firstIndex + std::min(count, kMaximumRowsPerTrendPass));
    for (std::size_t start = firstIndex; start < end;
         start += kMaximumQueuedChartRequestsPerWave) {
        if (Cancelled(cancelled))
            return false;
        const std::size_t waveEnd =
            std::min(end, start + kMaximumQueuedChartRequestsPerWave);
        if (!FetchTrendWaveWithFallback(
                items, start, waveEnd, cancelled, dependencies)) {
            return false;
        }
    }
    return !Cancelled(cancelled);
}

bool FetchMarketScreener(const std::string& guiId,
                         const std::vector<std::string>& watchlist,
                         std::size_t limit,
                         const ScreenerCancelCheck& cancelled,
                         const ScreenerFetchDependencies& dependencies,
                         std::vector<ScreenerItem>& items) {
    items.clear();
    if (guiId == "watchlist") {
        const std::size_t wanted = std::min(limit, watchlist.size());
        items = FetchYahooWatchlist(
            std::vector<std::string>(watchlist.begin(), watchlist.begin() + static_cast<std::ptrdiff_t>(wanted)),
            cancelled,
            dependencies);
        return true;
    }
    const ScreenerRoute* route = FindScreenerRouteByGuiId(guiId);
    if (!route)
        return false;
    if (guiId == "trending_now") {
        items = FetchYahooTrendingScreener(
            limit, cancelled, dependencies);
        return true;
    }
    if (guiId == "52_week_gainers" || guiId == "52_week_losers") {
        items = FetchYahooFiftyTwoWeekScreener(guiId == "52_week_gainers",
                                                limit,
                                                cancelled,
                                                dependencies);
        return true;
    }
    items = FetchYahooPredefinedScreener(*route, limit, cancelled, dependencies);
    return true;
}

} // namespace squarestar::providers
