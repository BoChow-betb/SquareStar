#include "application/app_limits.hpp"
#include "application/screener_item.hpp"
#include "services/screener_service.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <future>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

void Require(bool condition, const char* message) {
    if (condition)
        return;
    std::cerr << "FAILED: " << message << '\n';
    std::exit(EXIT_FAILURE);
}

std::future<squarestar::http::HttpResponse> ReadyChartResponse() {
    squarestar::http::HttpResponse response;
    response.statusCode = 200;
    response.body =
        R"({"chart":{"result":[{"meta":{"symbol":"TEST","regularMarketPrice":11.0,"chartPreviousClose":10.0},"indicators":{"quote":[{"close":[10.0,10.2,10.4,10.7,11.0]}]}}],"error":null}})";
    std::promise<squarestar::http::HttpResponse> ready;
    ready.set_value(std::move(response));
    return ready.get_future();
}

std::future<squarestar::http::HttpResponse> ReadyFailedResponse() {
    std::promise<squarestar::http::HttpResponse> ready;
    ready.set_value(squarestar::http::HttpResponse{
        {}, 503, squarestar::http::HttpError::HttpStatus});
    return ready.get_future();
}

std::future<squarestar::http::HttpResponse> ExceptionalResponse() {
    std::promise<squarestar::http::HttpResponse> failed;
    failed.set_exception(
        std::make_exception_ptr(std::runtime_error("simulated request failure")));
    return failed.get_future();
}

std::future<squarestar::http::HttpResponse> ReadyScreenerResponse() {
    squarestar::http::HttpResponse response;
    response.statusCode = 200;
    response.body =
        R"({"finance":{"result":[{"quotes":[{"symbol":"FAST","shortName":"Fast Path","regularMarketPrice":25.0,"regularMarketChange":1.0,"regularMarketChangePercent":4.1667}]}]}})";
    std::promise<squarestar::http::HttpResponse> ready;
    ready.set_value(std::move(response));
    return ready.get_future();
}

std::future<squarestar::http::HttpResponse> ReadyQuoteResponse() {
    squarestar::http::HttpResponse response;
    response.statusCode = 200;
    response.body =
        R"({"quoteResponse":{"result":[{"symbol":"FAST","shortName":"Fast Path","regularMarketPrice":25.0,"regularMarketChange":1.0,"regularMarketChangePercent":4.1667}],"error":null}})";
    std::promise<squarestar::http::HttpResponse> ready;
    ready.set_value(std::move(response));
    return ready.get_future();
}

std::future<squarestar::http::HttpResponse> ReadyTrendingResponse() {
    squarestar::http::HttpResponse response;
    response.statusCode = 200;
    response.body =
        R"({"finance":{"result":[{"quotes":[{"symbol":"BTC-USD"},{"symbol":"FAST"},{"symbol":"ETH-USD"},{"symbol":"LATE"}]}]}})";
    std::promise<squarestar::http::HttpResponse> ready;
    ready.set_value(std::move(response));
    return ready.get_future();
}

std::future<squarestar::http::HttpResponse> ReadyTrendingQuoteResponse() {
    squarestar::http::HttpResponse response;
    response.statusCode = 200;
    response.body =
        R"({"quoteResponse":{"result":[{"symbol":"BTC-USD","quoteType":"CRYPTOCURRENCY","regularMarketPrice":60000},{"symbol":"FAST","quoteType":"EQUITY","shortName":"Fast Equity","regularMarketPrice":25},{"symbol":"ETH-USD","quoteType":"CRYPTOCURRENCY","regularMarketPrice":3000},{"symbol":"LATE","quoteType":"EQUITY","shortName":"Late Equity","regularMarketPrice":15}],"error":null}})";
    std::promise<squarestar::http::HttpResponse> ready;
    ready.set_value(std::move(response));
    return ready.get_future();
}

} // namespace

int main() {
    using squarestar::application::ScreenerItem;
    using squarestar::application::kMaximizedOverviewRows;
    using squarestar::providers::EnrichMarketScreenerSparklines;
    using squarestar::providers::ScreenerFetchDependencies;

    static_assert(squarestar::application::kWindowedOverviewRows == 12);
    static_assert(kMaximizedOverviewRows == 18);
    std::vector<ScreenerItem> items(kMaximizedOverviewRows + 4);
    for (std::size_t index = 0; index < items.size(); ++index)
        items[index].symbol = "ROW" + std::to_string(index);

    std::size_t requestCount = 0;
    ScreenerFetchDependencies dependencies;
    dependencies.queueBatchHttpGet = [&](std::string url, squarestar::http::HttpCancelCheck) {
        Require(url.find("range=5d") != std::string::npos,
                "overview trends must request five-day chart data");
        ++requestCount;
        return ReadyChartResponse();
    };

    Require(EnrichMarketScreenerSparklines(
                items, 0, items.size(), [] { return false; }, dependencies),
            "the visible-page trend pass should complete");
    Require(requestCount == kMaximizedOverviewRows,
            "the trend pass must fetch every configured visible maximized row");
    for (std::size_t index = 0; index < kMaximizedOverviewRows; ++index) {
        Require(items[index].sparklineAttempted && items[index].sparkline.size() >= 2,
                "every visible row should publish its 5D trend");
    }
    for (std::size_t index = kMaximizedOverviewRows; index < items.size(); ++index) {
        Require(!items[index].sparklineAttempted && items[index].sparkline.empty(),
                "off-page rows must remain deferred");
    }

    std::vector<ScreenerItem> cancelledItems(kMaximizedOverviewRows);
    for (std::size_t index = 0; index < cancelledItems.size(); ++index)
        cancelledItems[index].symbol = "CANCEL" + std::to_string(index);
    bool cancelled = false;
    std::size_t cancellableRequests = 0;
    ScreenerFetchDependencies cancellableDependencies;
    cancellableDependencies.queueBatchHttpGet =
        [&](std::string, squarestar::http::HttpCancelCheck transferCancelled) {
            Require(static_cast<bool>(transferCancelled),
                    "trend cancellation must reach the HTTP transfer");
            ++cancellableRequests;
            if (cancellableRequests == kMaximizedOverviewRows)
                cancelled = true;
            return ReadyChartResponse();
        };
    Require(!EnrichMarketScreenerSparklines(
                cancelledItems,
                0,
                cancelledItems.size(),
                [&] { return cancelled; },
                cancellableDependencies),
            "a stale visible-page request should stop promptly");
    Require(cancellableRequests == kMaximizedOverviewRows,
            "all maximized visible trend requests must be queued in one bounded wave");

    std::size_t slowTrendPrimaryRequests = 0;
    std::size_t slowTrendSecondaryRequests = 0;
    ScreenerFetchDependencies slowTrendDependencies;
    std::size_t trendProgressPublications = 0;
    slowTrendDependencies.publishPartial =
        [&](const std::vector<ScreenerItem>& progressItems) {
            if (!progressItems.empty() &&
                progressItems.front().sparkline.size() >= 2) {
                ++trendProgressPublications;
            }
            return true;
        };
    slowTrendDependencies.queueBatchHttpGet =
        [&](std::string url, squarestar::http::HttpCancelCheck) {
            if (url.find("query1.finance.yahoo.com") != std::string::npos) {
                ++slowTrendPrimaryRequests;
                return std::async(std::launch::async, [] {
                    std::this_thread::sleep_for(std::chrono::milliseconds(250));
                    return ReadyChartResponse().get();
                });
            }
            ++slowTrendSecondaryRequests;
            return ReadyChartResponse();
        };
    std::vector<ScreenerItem> slowTrendItems(1);
    slowTrendItems.front().symbol = "SLOW";
    const auto slowTrendStarted = std::chrono::steady_clock::now();
    Require(EnrichMarketScreenerSparklines(
                slowTrendItems,
                0,
                1,
                [] { return false; },
                slowTrendDependencies),
            "a slow successful primary trend host should still resolve");
    const auto slowTrendElapsed =
        std::chrono::steady_clock::now() - slowTrendStarted;
    Require(slowTrendPrimaryRequests == 1 && slowTrendSecondaryRequests == 0 &&
                trendProgressPublications >= 1 &&
                slowTrendItems.front().sparkline.size() >= 2 &&
                slowTrendElapsed >= std::chrono::milliseconds(200),
            "slow trend requests must not launch speculative duplicate hosts");

    // A completed row may publish while another row is still in flight, but
    // the in-flight row must remain in the loading state rather than flashing
    // a false N/A before its 5D chart arrives.
    std::vector<ScreenerItem> progressiveItems(2);
    progressiveItems[0].symbol = "FIRST";
    progressiveItems[1].symbol = "SECOND";
    std::size_t progressiveRequests = 0;
    bool sawFirstOnlyPublication = false;
    bool sawPrematureAttempt = false;
    ScreenerFetchDependencies progressiveDependencies;
    progressiveDependencies.publishPartial =
        [&](const std::vector<ScreenerItem>& progressItems) {
            if (progressItems.size() >= 2 &&
                progressItems[0].sparklineAttempted &&
                progressItems[0].sparkline.size() >= 2 &&
                !progressItems[1].sparklineAttempted) {
                sawFirstOnlyPublication = true;
            }
            if (progressItems.size() >= 2 &&
                progressItems[1].sparklineAttempted &&
                progressItems[1].sparkline.size() < 2) {
                sawPrematureAttempt = true;
            }
            return true;
        };
    progressiveDependencies.queueBatchHttpGet =
        [&](std::string, squarestar::http::HttpCancelCheck) {
            ++progressiveRequests;
            if (progressiveRequests == 1)
                return ReadyChartResponse();
            return std::async(std::launch::async, [] {
                std::this_thread::sleep_for(std::chrono::milliseconds(80));
                return ReadyChartResponse().get();
            });
        };
    Require(EnrichMarketScreenerSparklines(
                progressiveItems,
                0,
                progressiveItems.size(),
                [] { return false; },
                progressiveDependencies),
            "progressive 5D publication should complete");
    Require(sawFirstOnlyPublication && !sawPrematureAttempt &&
                progressiveItems[1].sparklineAttempted &&
                progressiveItems[1].sparkline.size() >= 2,
            "in-flight trend rows must not be published as failed placeholders");

    std::size_t failedTrendPrimaryRequests = 0;
    std::size_t failedTrendSecondaryRequests = 0;
    ScreenerFetchDependencies failedTrendDependencies;
    failedTrendDependencies.queueBatchHttpGet =
        [&](std::string url, squarestar::http::HttpCancelCheck) {
            if (url.find("query1.finance.yahoo.com") != std::string::npos) {
                ++failedTrendPrimaryRequests;
                return ReadyFailedResponse();
            }
            ++failedTrendSecondaryRequests;
            return ReadyChartResponse();
        };
    std::vector<ScreenerItem> failedTrendItems(1);
    failedTrendItems.front().symbol = "FAILOVER";
    Require(EnrichMarketScreenerSparklines(
                failedTrendItems,
                0,
                1,
                [] { return false; },
                failedTrendDependencies),
            "a failed primary trend host should fall back to the secondary host");
    Require(failedTrendPrimaryRequests == 1 && failedTrendSecondaryRequests == 1 &&
                failedTrendItems.front().sparkline.size() >= 2,
            "trend failover must occur only after the primary host fails");

    std::vector<ScreenerItem> exceptionalItems(6);
    for (std::size_t index = 0; index < exceptionalItems.size(); ++index)
        exceptionalItems[index].symbol = "EXCEPTION" + std::to_string(index);
    std::size_t exceptionalRequests = 0;
    ScreenerFetchDependencies exceptionalDependencies;
    exceptionalDependencies.queueBatchHttpGet = [&](std::string, squarestar::http::HttpCancelCheck) {
        ++exceptionalRequests;
        return exceptionalRequests == 1 ? ExceptionalResponse()
                                        : ReadyChartResponse();
    };
    Require(EnrichMarketScreenerSparklines(
                exceptionalItems,
                0,
                exceptionalItems.size(),
                [] { return false; },
                exceptionalDependencies),
            "one failed HTTP future must not abort the remaining trend wave");
    Require(exceptionalRequests == 7 &&
                exceptionalItems.front().sparklineAttempted &&
                exceptionalItems.front().sparkline.size() >= 2 &&
                exceptionalItems.back().sparkline.size() >= 2,
            "exceptional primaries should fail over without aborting later trend rows");

    std::vector<std::string> largeWatchlist(96);
    for (std::size_t index = 0; index < largeWatchlist.size(); ++index)
        largeWatchlist[index] = "WATCH" + std::to_string(index);
    std::size_t quoteRequests = 0;
    std::size_t chartFallbackRequests = 0;
    ScreenerFetchDependencies recoveryDependencies;
    recoveryDependencies.queueYahooAuthenticatedGet = [&](std::string, squarestar::http::HttpCancelCheck) {
        ++quoteRequests;
        return ReadyFailedResponse();
    };
    recoveryDependencies.queueBatchHttpGet = [&](std::string url, squarestar::http::HttpCancelCheck) {
        Require(url.find("finnhub.io") == std::string::npos,
                "watchlist recovery must not fan out into Finnhub metric calls");
        ++chartFallbackRequests;
        return ReadyFailedResponse();
    };
    std::vector<ScreenerItem> recoveredItems;
    Require(squarestar::providers::FetchMarketScreener(
                "watchlist",
                largeWatchlist,
                largeWatchlist.size(),
                [] { return false; },
                recoveryDependencies,
                recoveredItems),
            "watchlist route should resolve even when providers reject recovery requests");
    Require(quoteRequests == 4 && chartFallbackRequests == 0,
            "watchlist refresh must not fan out into per-symbol chart recovery");

    std::size_t watchlistPartialPublications = 0;
    ScreenerFetchDependencies progressiveWatchlistDependencies;
    progressiveWatchlistDependencies.publishPartial =
        [&](const std::vector<ScreenerItem>&) {
            ++watchlistPartialPublications;
            return true;
        };
    progressiveWatchlistDependencies.queueYahooAuthenticatedGet =
        [](std::string, squarestar::http::HttpCancelCheck) { return ReadyQuoteResponse(); };
    std::vector<ScreenerItem> progressiveWatchlistItems;
    Require(squarestar::providers::FetchMarketScreener(
                "watchlist",
                {"FAST"},
                1,
                [] { return false; },
                progressiveWatchlistDependencies,
                progressiveWatchlistItems) &&
                progressiveWatchlistItems.size() == 1 &&
                progressiveWatchlistItems.front().hasPrice &&
                progressiveWatchlistItems.front().name == "Fast Path",
            "watchlist live quote and company name should resolve");
    Require(watchlistPartialPublications == 0,
            "watchlist must publish atomically instead of flashing symbol-only rows");

    std::size_t trendingPartialPublications = 0;
    bool trendingPartialStayedEquityOnly = true;
    ScreenerFetchDependencies trendingDependencies;
    trendingDependencies.publishPartial =
        [&](const std::vector<ScreenerItem>& progressItems) {
            ++trendingPartialPublications;
            for (const ScreenerItem& item : progressItems) {
                if (item.symbol == "BTC-USD" || item.symbol == "ETH-USD")
                    trendingPartialStayedEquityOnly = false;
            }
            return true;
        };
    trendingDependencies.queueHttpGet =
        [](std::string, squarestar::http::HttpCancelCheck) { return ReadyTrendingResponse(); };
    trendingDependencies.queueYahooAuthenticatedGet =
        [](std::string, squarestar::http::HttpCancelCheck) { return ReadyTrendingQuoteResponse(); };
    std::vector<ScreenerItem> trendingItems;
    Require(squarestar::providers::FetchMarketScreener(
                "trending_now",
                {},
                20,
                [] { return false; },
                trendingDependencies,
                trendingItems) &&
                trendingItems.size() == 2 &&
                trendingItems[0].symbol == "FAST" &&
                trendingItems[1].symbol == "LATE",
            "Trending Now must retain equity ranking while removing crypto rows");
    Require(trendingPartialPublications >= 1 && trendingPartialStayedEquityOnly,
            "Trending Now progress must publish only confirmed equities");

    std::size_t publicListRequests = 0;
    std::size_t authenticatedListRequests = 0;
    ScreenerFetchDependencies publicListDependencies;
    publicListDependencies.queueHttpGet = [&](std::string, squarestar::http::HttpCancelCheck) {
        ++publicListRequests;
        return ReadyScreenerResponse();
    };
    publicListDependencies.queueYahooAuthenticatedGet = [&](std::string, squarestar::http::HttpCancelCheck) {
        ++authenticatedListRequests;
        return ReadyFailedResponse();
    };
    std::vector<ScreenerItem> publicListItems;
    Require(squarestar::providers::FetchMarketScreener(
                "day_gainers",
                {},
                20,
                [] { return false; },
                publicListDependencies,
                publicListItems) &&
                publicListItems.size() == 1,
            "public Yahoo screener response should populate the list");
    Require(publicListRequests == 1 && authenticatedListRequests == 0,
            "public screeners must avoid the cookie and crumb bootstrap on success");

    std::size_t slowListPrimaryRequests = 0;
    std::size_t slowListSecondaryRequests = 0;
    ScreenerFetchDependencies slowListDependencies;
    slowListDependencies.queueHttpGet =
        [&](std::string url, squarestar::http::HttpCancelCheck) {
            if (url.find("query1.finance.yahoo.com") != std::string::npos) {
                ++slowListPrimaryRequests;
                return std::async(std::launch::async, [] {
                    std::this_thread::sleep_for(std::chrono::milliseconds(250));
                    return ReadyScreenerResponse().get();
                });
            }
            ++slowListSecondaryRequests;
            return ReadyScreenerResponse();
        };
    std::vector<ScreenerItem> slowListItems;
    const auto slowListStarted = std::chrono::steady_clock::now();
    Require(squarestar::providers::FetchMarketScreener(
                "day_gainers",
                {},
                20,
                [] { return false; },
                slowListDependencies,
                slowListItems) &&
                slowListItems.size() == 1,
            "a slow successful primary list host should still resolve");
    const auto slowListElapsed =
        std::chrono::steady_clock::now() - slowListStarted;
    Require(slowListPrimaryRequests == 1 && slowListSecondaryRequests == 0 &&
                slowListElapsed >= std::chrono::milliseconds(200),
            "slow list requests must not launch speculative duplicate hosts");

    std::size_t failedListPrimaryRequests = 0;
    std::size_t failedListSecondaryRequests = 0;
    ScreenerFetchDependencies failedListDependencies;
    failedListDependencies.queueHttpGet =
        [&](std::string url, squarestar::http::HttpCancelCheck) {
            if (url.find("query1.finance.yahoo.com") != std::string::npos) {
                ++failedListPrimaryRequests;
                return ReadyFailedResponse();
            }
            ++failedListSecondaryRequests;
            return ReadyScreenerResponse();
        };
    std::vector<ScreenerItem> failedListItems;
    Require(squarestar::providers::FetchMarketScreener(
                "day_gainers",
                {},
                20,
                [] { return false; },
                failedListDependencies,
                failedListItems) &&
                failedListItems.size() == 1,
            "a failed primary list host should fall back to the secondary host");
    Require(failedListPrimaryRequests == 1 && failedListSecondaryRequests == 1,
            "list failover must occur only after the primary host fails");

    std::size_t exceptionalPublicRequests = 0;
    std::size_t exceptionalAuthenticatedRequests = 0;
    ScreenerFetchDependencies exceptionalListDependencies;
    exceptionalListDependencies.queueHttpGet = [&](std::string, squarestar::http::HttpCancelCheck) {
        ++exceptionalPublicRequests;
        return ExceptionalResponse();
    };
    exceptionalListDependencies.queueYahooAuthenticatedGet = [&](std::string, squarestar::http::HttpCancelCheck) {
        ++exceptionalAuthenticatedRequests;
        return ExceptionalResponse();
    };
    std::vector<ScreenerItem> exceptionalListItems;
    Require(squarestar::providers::FetchMarketScreener(
                "day_gainers",
                {},
                20,
                [] { return false; },
                exceptionalListDependencies,
                exceptionalListItems) &&
                exceptionalListItems.empty(),
            "provider future exceptions must degrade to an empty resolved screener");
    Require(exceptionalPublicRequests == 2 &&
                exceptionalAuthenticatedRequests == 0,
            "public list failures must not enter the authenticated crumb bootstrap path");

    std::cout << "All screener service tests passed\n";
    return EXIT_SUCCESS;
}
