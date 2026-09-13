#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "domain/news_text.hpp"
#include "domain/trading_status.hpp"

namespace {

void Require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

} // namespace

int main() {
    using namespace squarestar::market;

    Require(squarestar::news::NormalizeNewsText(
                "Santander â€™ completes &amp; closes 260$a??265 deal") ==
                "Santander ’ completes & closes deal",
            "news display text repairs mojibake, entities, and MARC debris");
    Require(squarestar::news::NormalizeNewsText("合法 UTF-8 新闻") ==
                "合法 UTF-8 新闻",
            "valid international UTF-8 remains unchanged");
    const std::string invalidUtf8 = std::string("clean") + static_cast<char>(0xFF) + "text";
    Require(squarestar::news::NormalizeNewsText(invalidUtf8) == "clean text",
            "invalid UTF-8 bytes cannot leak replacement-symbol debris into ImGui");
    const auto oneDay = ResolveProgressiveChartRange(true, true, true, true);
    const auto fiveDay = ResolveProgressiveChartRange(false, true, true, true);
    const auto oneMonth = ResolveProgressiveChartRange(false, false, true, true);
    const auto allHistory = ResolveProgressiveChartRange(false, false, false, true);
    const auto missing = ResolveProgressiveChartRange(false, false, false, false);
    Require(oneDay.rangeIndex == 0 &&
                oneDay.availability == ChartAvailability::ActiveSelectedRange,
            "usable 1D stays selected");
    Require(fiveDay.rangeIndex == 1 &&
                fiveDay.availability == ChartAvailability::NoTradingToday,
            "missing 1D falls back to 5D without changing company nature");
    Require(oneMonth.rangeIndex == 2 &&
                oneMonth.defaultCorporateAction ==
                    CorporateActionStatus::SuspectedStopped,
            "missing 1D and 5D falls back to 1M and starts conservative diagnosis");
    Require(allHistory.rangeIndex == ALL_TIME_RANGE_INDEX &&
                allHistory.availability == ChartAvailability::TradingStoppedWithHistory &&
                allHistory.defaultCorporateAction ==
                    CorporateActionStatus::SuspectedStopped,
            "missing 1D, 5D, and 1M falls back to All history and keeps diagnosis "
            "separate from company nature");
    Require(std::string(TIME_RANGES[ALL_TIME_RANGE_INDEX].label) == "All" &&
                std::string(TIME_RANGES[ALL_TIME_RANGE_INDEX].rangeStr) == "max",
            "All fallback resolves to Yahoo's maximum available history");
    Require(missing.rangeIndex == -1 &&
                missing.availability == ChartAvailability::NoChartHistory,
            "four empty ranges remain unavailable");

    std::vector<NewsItem> acquiredNews = {{
        "Santander completes acquisition of Webster Financial", "Reuters", "https://example.com",
        "The merger closed today.", 0}};
    std::string evidence;
    Require(ClassifyCorporateActionNews(acquiredNews, &evidence) ==
                CorporateActionStatus::Acquired &&
                evidence == acquiredNews.front().headline,
            "explicit completed-acquisition news upgrades the local diagnosis");
    std::vector<NewsItem> ordinaryNews = {{
        "Webster comments on regional lending", "Wire", "https://example.com", "", 0}};
    Require(ClassifyCorporateActionNews(ordinaryNews) == CorporateActionStatus::None,
            "ordinary news cannot create a delisting status");

    TradingStatus suspended;
    suspended.corporateAction = CorporateActionStatus::SuspectedStopped;
    Require(HasInactiveTradingStatus(suspended),
            "stopped trading state marks the stock tab for attention");
    Require(TradingStatusExchangeLabel("NASDAQ", suspended) ==
                "Delisted / suspended from NASDAQ",
            "stopped trading status replaces the separate banner beside the exchange");
    TradingStatus active;
    Require(!HasInactiveTradingStatus(active) &&
                TradingStatusExchangeLabel("NYSE", active) == "NYSE",
            "active stocks keep the ordinary exchange label and tab color");

    return EXIT_SUCCESS;
}
