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


std::string BuildYahooFiftyTwoWeekScreenerBody(bool gainers, std::size_t limit);


bool EnrichMarketScreenerSparklines(
    std::vector<squarestar::application::ScreenerItem>& items,
    std::size_t firstIndex,
    std::size_t count,
    const ScreenerCancelCheck& cancelled,
    const ScreenerFetchDependencies& dependencies);


bool FetchMarketScreener(const std::string& guiId,
                         const std::vector<std::string>& watchlist,
                         std::size_t limit,
                         const ScreenerCancelCheck& cancelled,
                         const ScreenerFetchDependencies& dependencies,
                         std::vector<squarestar::application::ScreenerItem>& items);

}
