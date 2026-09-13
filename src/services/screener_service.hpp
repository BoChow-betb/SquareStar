#pragma once

#include <cstddef>
#include <functional>
#include <future>
#include <string>
#include <vector>

#include "application/screener_item.hpp"
#include "services/http_client.hpp"

namespace squarestar::providers {

using ScreenerCancelCheck = std::function<bool()>;
using ScreenerHttpGet = std::function<std::future<squarestar::http::HttpResponse>(
    std::string, squarestar::http::HttpCancelCheck)>;
using ScreenerHttpPost = std::function<std::future<squarestar::http::HttpResponse>(
    std::string, squarestar::http::HttpCancelCheck)>;

struct ScreenerFetchDependencies {
    std::function<bool(
        const std::vector<squarestar::application::ScreenerItem>&)>
        publishPartial;
    ScreenerHttpGet queueHttpGet;
    ScreenerHttpGet queueBatchHttpGet;
    ScreenerHttpGet queueYahooAuthenticatedGet;
    ScreenerHttpPost queueYahooScreenerPost;
};

// Yahoo's public 52-week stock pages are custom screeners sorted by actual
// 52-week price percentage change. Exposed for deterministic request tests.
std::string BuildYahooFiftyTwoWeekScreenerBody(bool gainers, std::size_t limit);

// Add five-day mini-charts to one visible page of resolved screener rows.
// Uses Yahoo's public chart endpoint.
bool EnrichMarketScreenerSparklines(
    std::vector<squarestar::application::ScreenerItem>& items,
    std::size_t firstIndex,
    std::size_t count,
    const ScreenerCancelCheck& cancelled,
    const ScreenerFetchDependencies& dependencies);

// Fetch one market screener using injected request scheduling. Five-day trends
// are fetched separately for the visible page, never from a list refresh.
bool FetchMarketScreener(const std::string& guiId,
                         const std::vector<std::string>& watchlist,
                         std::size_t limit,
                         const ScreenerCancelCheck& cancelled,
                         const ScreenerFetchDependencies& dependencies,
                         std::vector<squarestar::application::ScreenerItem>& items);

} // namespace squarestar::providers
