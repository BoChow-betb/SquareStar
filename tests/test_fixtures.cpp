#include "test_fixtures.hpp"

#include "application/stock_data_merge.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>

namespace squarestar::test {

squarestar::market::StockData BuildTestStockData(std::string_view symbol,
                                                       int variant) {
    squarestar::market::StockData data;
    constexpr size_t sampleCount = 900;
    data.timestamps.reserve(sampleCount);
    data.opens.reserve(sampleCount);
    data.highs.reserve(sampleCount);
    data.lows.reserve(sampleCount);
    data.closes.reserve(sampleCount);
    data.volumes.reserve(sampleCount);
    constexpr double firstTimestamp = 1'700'000'000.0;
    for (size_t i = 0; i < sampleCount; ++i) {
        const double variantOffset = static_cast<double>(variant) * 7.25;
        const double trend = 120.0 + variantOffset + static_cast<double>(i) * 0.035;
        const double close = trend + std::sin(static_cast<double>(i) * 0.075) * 4.5 +
                             std::cos((static_cast<double>(i) + static_cast<double>(variant * 11)) * 0.021) * 2.0;
        const double open = close - std::sin(static_cast<double>(i) * 0.13) * 1.2;
        data.timestamps.push_back(firstTimestamp + static_cast<double>(i) * 86400.0);
        data.opens.push_back(open);
        data.highs.push_back(std::max(open, close) + 1.15);
        data.lows.push_back(std::min(open, close) - 1.05);
        data.closes.push_back(close);
        data.volumes.push_back(18'000'000.0 + static_cast<double>(i % 29) * 310'000.0);
    }
    data.companyName = "SquareStar Test " + std::string(symbol);
    data.exchange = "NASDAQ";
    data.currency = "USD";
    data.industry = variant % 2 == 0 ? "Technology" : "Financial Services";
    data.weburl = "https://example.com/" + std::string(symbol);
    data.previousClose = data.closes[data.closes.size() - 2];
    data.currentPrice = data.closes.back();
    data.openPrice = data.opens.back();
    data.dayHigh = data.highs.back();
    data.dayLow = data.lows.back();
    data.marketCap = 2.4e12;
    data.peRatio = 31.4;
    data.beta = 1.12;
    data.avgVolume = 21'500'000.0;
    data.regularMarketVolume = data.volumes.back();
    data.fiftyTwoWeekHigh = data.currentPrice * 1.18;
    data.fiftyTwoWeekLow = data.currentPrice * 0.68;
    data.dividendYield = 0.0085;
    data.fiftyTwoWkChangePercent = 17.25 - static_cast<double>(variant);
    data.hasRegularMarketVolume = true;
    data.hasFiftyTwoWkChangePercent = true;
    const std::time_t articleTime =
        static_cast<std::time_t>(firstTimestamp + sampleCount * 86400.0);
    data.news = {
        {"Test company expands its current product platform",
         "Reuters",
         "https://example.com/market-update",
         "A synthetic test summary covering the news-list browser handoff.",
         articleTime},
        {"Quarterly results highlight steady customer demand",
         "MarketWatch",
         "https://example.com/quarterly-results",
         "Synthetic business-news content for test coverage.",
         articleTime - 3600},
        {"Analysts review the latest operating metrics",
         "Seeking Alpha",
         "https://seekingalpha.com/example",
         "This fixture follows the headline-to-browser rule.",
         articleTime - 7200},
    };
    data.resolvedDetailMask = squarestar::application::StockFetchAll;
    data.success = true;
    return data;
}

std::vector<squarestar::market::ScreenerData> BuildTestScreenerData(
    std::size_t count) {
    std::vector<squarestar::market::ScreenerData> items;
    items.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        squarestar::market::ScreenerData item;
        item.symbol = "TEST" + std::to_string(index + 1);
        item.name = "Synthetic test company " + std::to_string(index + 1);
        item.price = 80.0 + static_cast<double>(index) * 3.25;
        item.change = (index % 2 == 0 ? 1.0 : -1.0) *
                      (0.35 + static_cast<double>(index) * 0.04);
        item.changePercent = item.change / item.price * 100.0;
        item.volume = 8'000'000.0 + static_cast<double>(index) * 420'000.0;
        item.avgVol3M = 7'500'000.0 + static_cast<double>(index) * 360'000.0;
        item.marketCap = 4.0e9 + static_cast<double>(index) * 1.3e9;
        item.peRatio = 14.0 + static_cast<double>(index) * 0.65;
        item.fiftyTwoWkChange = (index % 2 == 0 ? 1.0 : -1.0) *
                                (8.0 + static_cast<double>(index) * 1.15);
        item.hasPrice = item.hasChange = item.hasChangePercent = true;
        item.hasVolume = item.hasAvgVol3M = item.hasMarketCap = item.hasPeRatio = true;
        item.hasFiftyTwoWkChange = true;
        item.resolved = true;
        squarestar::market::ReplaceScreenerSparkline(
            item, {0.42f, 0.48f, 0.45f, 0.58f, 0.63f});
        items.push_back(std::move(item));
    }
    return items;
}

} // namespace squarestar::test
