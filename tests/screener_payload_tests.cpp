#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>

#include "services/screener_payload.hpp"

namespace {

bool Near(float left, float right) {
    return std::fabs(left - right) < 0.001f;
}

} // namespace

int main() {
    squarestar::application::ScreenerItem item;
    item.symbol = "SPCX";
    const std::string chartPayload =
        R"({"chart":{"result":[{"meta":{"symbol":"SPCX","regularMarketPrice":9.5,"chartPreviousClose":10.0},"indicators":{"quote":[{"close":[10.0,9.9,9.8,null,9.7,9.6,9.5,9.4,9.3]}]}}],"error":null}})";
    if (!squarestar::providers::ApplyYahooScreenerChartPayload(chartPayload, item, true)) {
        std::cerr << "chart payload was not accepted\n";
        return 1;
    }
    if (item.sparkline.size() <= 5 || !Near(item.sparkline.back(), 9.3f)) {
        std::cerr << "full five-day intraday series was not preserved: size="
                  << item.sparkline.size();
        if (!item.sparkline.empty())
            std::cerr << " first=" << item.sparkline.front() << " last=" << item.sparkline.back();
        std::cerr << '\n';
        return 1;
    }
    if (!item.hasPrice || !item.hasChangePercent || item.changePercent >= 0.0) {
        std::cerr << "chart metadata did not populate the row quote\n";
        return 1;
    }

    std::vector<squarestar::application::ScreenerItem> quoteItems(1);
    quoteItems[0].symbol = "SPCX";
    squarestar::providers::ScreenerItemIndex quoteIndex{{"SPCX", 0}};
    const std::size_t quoteApplied =
        squarestar::providers::ApplyYahooQuoteResponsePayload(
            R"({"quoteResponse":{"result":[{"symbol":"SPCX","regularMarketPrice":10.25,"regularMarketTime":1700000100}]}})",
            quoteIndex,
            quoteItems);
    if (quoteApplied != 1 || !quoteItems[0].hasPrice ||
        quoteItems[0].lastMarketTime != 1700000100) {
        std::cerr << "screener quote price/timestamp was not preserved\n";
        return 1;
    }

    quoteItems[0].price = 10.25;
    quoteItems[0].hasPrice = true;
    quoteItems[0].lastMarketTime = 1700000100;
    const std::size_t timestampOnlyApplied =
        squarestar::providers::ApplyYahooQuoteResponsePayload(
            R"({"quoteResponse":{"result":[{"symbol":"SPCX","regularMarketPrice":0,"regularMarketTime":1700009999}]}})",
            quoteIndex,
            quoteItems);
    if (timestampOnlyApplied != 1 || !Near(static_cast<float>(quoteItems[0].price), 10.25f) ||
        quoteItems[0].lastMarketTime != 1700000100) {
        std::cerr << "timestamp-only screener payload made a cached price look fresh\n";
        return 1;
    }

    std::string densePayload =
        R"({"chart":{"result":[{"meta":{},"indicators":{"quote":[{"close":[)";
    for (std::size_t index = 0; index <
                                squarestar::providers::kMaxScreenerSparklineSamples + 25;
         ++index) {
        if (index != 0)
            densePayload.push_back(',');
        densePayload += std::to_string(index + 1);
    }
    densePayload += R"(]}]}}]}})";
    squarestar::application::ScreenerItem denseItem;
    if (!squarestar::providers::ApplyYahooScreenerChartPayload(
            std::move(densePayload), denseItem, true) ||
        denseItem.sparkline.size() !=
            squarestar::providers::kMaxScreenerSparklineSamples) {
        std::cerr << "dense screener sparkline exceeded its resource limit\n";
        return 1;
    }
    squarestar::application::ScreenerItem hugeItem;
    const std::string hugePayload =
        R"({"chart":{"result":[{"meta":{},"indicators":{"quote":[{"close":[10.0,1e300,11.0]}]}}]}})";
    if (!squarestar::providers::ApplyYahooScreenerChartPayload(
            hugePayload, hugeItem, true) ||
        hugeItem.sparkline.size() != 2 || !Near(hugeItem.sparkline.front(), 10.0f) ||
        !Near(hugeItem.sparkline.back(), 11.0f) ||
        !std::all_of(hugeItem.sparkline.begin(), hugeItem.sparkline.end(),
                     [](float value) { return std::isfinite(value); })) {
        std::cerr << "sparkline retained a value that overflowed float range\n";
        return 1;
    }
    return 0;
}
