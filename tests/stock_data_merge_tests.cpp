#include "application/stock_data_merge.hpp"
#include "domain/chart_ranges.hpp"
#include "domain/stock_data.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {

void Require(bool condition, const char* message) {
    if (condition)
        return;
    std::cerr << "FAILED: " << message << '\n';
    std::exit(EXIT_FAILURE);
}

} // namespace

int main() {
    using squarestar::application::MergeStockFetchPatch;
    using squarestar::market::FetchKind;
    using squarestar::market::StockData;
    using squarestar::market::TIME_RANGE_COUNT;

    StockData session;
    session.success = true;
    session.previousClose = 218.25;
    session.currentPrice = 220.78;
    session.openPrice = 219.10;
    session.dayHigh = 221.40;
    session.dayLow = 217.80;

    for (int rangeIndex = 0; rangeIndex < TIME_RANGE_COUNT; ++rangeIndex) {
        StockData chartPatch;
        chartPatch.success = true;
        chartPatch.previousClose = 100.0 + rangeIndex;
        chartPatch.chartPreviousClose = 150.0 + rangeIndex;
        chartPatch.currentPrice = 220.78;
        chartPatch.openPrice = 110.0 + rangeIndex;
        chartPatch.dayHigh = 230.0 + rangeIndex;
        chartPatch.dayLow = 90.0 + rangeIndex;
        chartPatch.timestamps = {100.0, 200.0};
        chartPatch.opens = {219.10, 220.00};
        chartPatch.highs = {220.20, 221.40};
        chartPatch.lows = {217.80, 219.70};
        chartPatch.closes = {219.80, 220.78};
        chartPatch.volumes = {1000.0, 1200.0};

        const StockData merged =
            MergeStockFetchPatch(session, std::move(chartPatch), FetchKind::Chart, rangeIndex);

        Require(std::abs(merged.previousClose - session.previousClose) < 1e-9,
                "chart patches preserve the session previous close");
        Require(std::abs(merged.chartPreviousClose - (150.0 + rangeIndex)) < 1e-9,
                "chart patches publish the selected range reference close");
        Require(std::abs(merged.openPrice - session.openPrice) < 1e-9 &&
                    std::abs(merged.dayHigh - session.dayHigh) < 1e-9 &&
                    std::abs(merged.dayLow - session.dayLow) < 1e-9,
                "chart patches preserve session OHLC fields");
        Require(merged.timestamps.size() == 2 && merged.closes.size() == 2,
                "chart patches replace chart series");
    }
}
