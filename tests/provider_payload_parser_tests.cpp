#include "services/provider_payload_parser.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

namespace {

int failures = 0;

void Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        ++failures;
    }
}

bool Near(double actual, double expected, double tolerance = 0.000001) {
    return std::abs(actual - expected) <= tolerance;
}

constexpr std::string_view kYahooChartPayload = R"json(
{
  "chart": {
    "result": [{
      "meta": {
        "longName": "Acme Corp",
        "currency": "USD",
        "exchangeName": "NMS",
        "chartPreviousClose": 9.5,
        "regularMarketVolume": 1234
      },
      "timestamp": [100, 200, 200, 300],
      "indicators": {"quote": [{
        "open": [10, null, 12, 0],
        "high": [9, null, 13, 0],
        "low": [11, null, 11, 0],
        "close": [10, 11, 12, 13],
        "volume": [100, -1, 200, 300]
      }]}
    }],
    "error": null
  }
}
)json";

constexpr std::string_view kYahooVolumePayload = R"json(
{
  "chart": {"result": [{
    "timestamp": [86390, 86410, 86420],
    "indicators": {"quote": [{
      "open": [10, 11, 12],
      "high": [10, 11, 12],
      "low": [10, 11, 12],
      "close": [10, 11, 12],
      "volume": [1, 2, 3]
    }]}
  }]}
}
)json";

} // namespace

int main() {
    using squarestar::market::StockData;
    using namespace squarestar::providers;

    StockData chart;
    Check(ApplyYahooChartPayload(std::string(kYahooChartPayload), true, chart),
          "valid Yahoo chart payload is accepted");
    Check(chart.success && chart.closes.size() == 3,
          "duplicate timestamps are discarded without losing the series");
    Check(squarestar::market::StockDataInvariantsHold(chart),
          "accepted Yahoo chart payload satisfies the shared StockData invariant");
    Check(chart.companyName == "Acme Corp" && chart.exchange == "NMS" &&
              chart.currency == "USD",
          "Yahoo metadata is decoded into domain data");
    Check(Near(chart.currentPrice, 13.0) && Near(chart.previousClose, 9.5) &&
              Near(chart.chartPreviousClose, 9.5),
          "chart metadata preserves a range previous close while providing the quote fallback");
    Check(chart.quoteTimestamp == 300,
          "the newest chart sample timestamps a Yahoo-chart price fallback");
    Check(Near(chart.opens[1], 11.0) && Near(chart.highs[0], 10.0) &&
              Near(chart.lows[0], 10.0),
          "missing and inconsistent OHLC values are normalized");
    Check(Near(chart.volumes[1], 0.0) && Near(chart.regularMarketVolume, 1234.0),
          "invalid sample volume is sanitized while provider aggregate is retained");
    Check(YahooChartPayloadHasUsableSeries(std::string(kYahooChartPayload)),
          "Yahoo availability check uses the standalone parser");

    StockData intraday;
    Check(ApplyYahooChartPayload(std::string(kYahooVolumePayload), true, intraday),
          "chart without volume metadata remains usable");
    Check(Near(intraday.regularMarketVolume, 5.0) && intraday.hasRegularMarketVolume,
          "intraday volume aggregates samples from the latest UTC day");
    StockData historical;
    Check(ApplyYahooChartPayload(std::string(kYahooVolumePayload), false, historical) &&
              Near(historical.regularMarketVolume, 3.0),
          "historical volume uses the last positive sample");

    StockData splitDay;
    Check(ApplyYahooChartPayload(
              R"json({"chart":{"result":[{
                "meta":{"chartPreviousClose":683.27},
                "timestamp":[200,300],
                "events":{"splits":{"150":{"date":150,"numerator":2,"denominator":1}}},
                "indicators":{"quote":[{
                  "open":[341.7,342.0],"high":[342.0,343.0],"low":[341.0,341.8],
                  "close":[341.8,342.52],"volume":[100,200]
                }]}
              }]}})json",
              true,
              splitDay),
          "split-day chart is accepted");
    Check(Near(splitDay.previousClose, 341.635) &&
              Near(splitDay.chartPreviousClose, 341.635) &&
              Near(splitDay.currentPrice, 342.52),
          "split normalization keeps the chart previous-close fallback continuous");

    StockData splitSeries;
    Check(ApplyYahooChartPayload(
              R"json({"chart":{"result":[{
                "timestamp":[100,200,300],
                "events":{"splits":{"200":{"date":200,"numerator":2,"denominator":1}}},
                "indicators":{"quote":[{
                  "open":[198,100,101],"high":[202,102,103],"low":[197,99,100],
                  "close":[200,101,102],"volume":[10,20,30]
                }]}
              }]}})json",
              false,
              splitSeries),
          "series spanning a forward split is accepted");
    Check(Near(splitSeries.closes.front(), 100.0) &&
              Near(splitSeries.volumes.front(), 20.0),
          "pre-split OHLC and volume are normalized across the event boundary");

    StockData reverseSplitSeries;
    Check(ApplyYahooChartPayload(
              R"json({"chart":{"result":[{
                "timestamp":[100,200,300],
                "events":{"splits":{"200":{"date":200,"numerator":1,"denominator":10}}},
                "indicators":{"quote":[{
                  "open":[10,98,99],"high":[10,100,101],"low":[9,97,98],
                  "close":[10,99,100],"volume":[1000,120,130]
                }]}
              }]}})json",
              false,
              reverseSplitSeries),
          "series spanning a reverse split is accepted");
    Check(Near(reverseSplitSeries.closes.front(), 100.0) &&
              Near(reverseSplitSeries.volumes.front(), 100.0),
          "reverse-split price and volume normalization uses the same basis");

    StockData multiSplitSeries;
    Check(ApplyYahooChartPayload(
              R"json({"chart":{"result":[{
                "timestamp":[100,200,300,400],
                "events":{"splits":{
                  "200":{"date":200,"numerator":2,"denominator":1},
                  "300":{"date":300,"numerator":5,"denominator":1}
                }},
                "indicators":{"quote":[{
                  "open":[990,495,99,100],"high":[1010,505,101,102],
                  "low":[980,490,98,99],"close":[1000,500,100,101],
                  "volume":[10,20,30,40]
                }]}
              }]}})json",
              false,
              multiSplitSeries),
          "series spanning multiple splits is accepted");
    Check(Near(multiSplitSeries.closes[0], 100.0) &&
              Near(multiSplitSeries.closes[1], 100.0) &&
              Near(multiSplitSeries.volumes[0], 100.0) &&
              Near(multiSplitSeries.volumes[1], 100.0),
          "multiple split boundaries are normalized with one cumulative pass");

    StockData unchanged;
    unchanged.currentPrice = 77.0;
    Check(!ApplyYahooChartPayload(
              R"json({"chart":{"result":[{"timestamp":[1],"indicators":{"quote":[{"close":[10]}]}}]}})json",
              true,
              unchanged),
          "a one-sample Yahoo series is rejected");
    Check(Near(unchanged.currentPrice, 77.0) && unchanged.closes.empty(),
          "failed chart parse leaves the destination unchanged");
    Check(!YahooChartPayloadHasUsableSeries("not json"),
          "malformed Yahoo JSON is rejected");

    StockData yahooCurrencyMetadata;
    Check(ApplyYahooChartPayload(
              R"json({"chart":{"result":[{"meta":{"currency":"HKD"},"timestamp":[1700000100,1700000200],"indicators":{"quote":[{"close":[10,11]}]}}]}})json",
              false,
              yahooCurrencyMetadata) &&
              yahooCurrencyMetadata.currency == "HKD",
          "Yahoo chart metadata preserves the provider currency instead of forcing USD");

    const auto fxMetaRate = ParseYahooFxRatePayload(
        R"json({"chart":{"result":[{"meta":{"regularMarketPrice":7.8342,"regularMarketTime":1700000200},"timestamp":[1700000100],"indicators":{"quote":[{"close":[7.8]}]}}]}})json");
    Check(fxMetaRate && Near(fxMetaRate->rate, 7.8342) &&
              fxMetaRate->timestamp == 1700000200,
          "Yahoo FX parser prefers the regular-market quote snapshot");

    const auto fxCloseFallback = ParseYahooFxRatePayload(
        R"json({"chart":{"result":[{"timestamp":[1700000100,1700000200,1700000300],"indicators":{"quote":[{"close":[6.70,null,6.72]}]}}]}})json");
    Check(fxCloseFallback && Near(fxCloseFallback->rate, 6.72) &&
              fxCloseFallback->timestamp == 1700000300,
          "Yahoo FX parser falls back to the newest positive close");
    Check(!ParseYahooFxRatePayload("not json").has_value() &&
              !ParseYahooFxRatePayload(
                  R"json({"chart":{"result":[{"meta":{"regularMarketPrice":0},"indicators":{"quote":[{"close":[null,0]}]}}]}})json")
                   .has_value(),
          "malformed and non-positive Yahoo FX payloads are rejected");

    const auto batchQuotes = ParseYahooQuoteBatchPayload(
        R"json({"quoteResponse":{"result":[
          {"symbol":"AAPL","regularMarketPrice":201.5,"regularMarketPreviousClose":199,"regularMarketOpen":200,"regularMarketDayHigh":203,"regularMarketDayLow":198,"regularMarketTime":1700000100},
          {"symbol":"MSFT","regularMarketPrice":410,"regularMarketPreviousClose":405,"regularMarketTime":1700000101}
        ]}})json");
    Check(batchQuotes.size() == 2 && batchQuotes[0].symbol == "AAPL" &&
              Near(batchQuotes[0].currentPrice, 201.5) &&
              batchQuotes[0].timestamp == 1700000100 &&
              batchQuotes[1].symbol == "MSFT",
          "Yahoo batch quote parser preserves per-symbol price snapshots and timestamps");

    StockData outOfRangeTimestamps;
    Check(!ApplyYahooChartPayload(
              R"json({"chart":{"result":[{"timestamp":[1e300,1e301],"indicators":{"quote":[{"close":[10,11],"volume":[1,2]}]}}]}})json",
              true,
              outOfRangeTimestamps),
          "out-of-range Yahoo timestamps are rejected before epoch conversion");
    Check(outOfRangeTimestamps.closes.empty(),
          "invalid timestamps leave the destination unchanged");

    std::string oversizedChart =
        R"json({"chart":{"result":[{"timestamp":[)json";
    oversizedChart.reserve(2 * 1024 * 1024);
    for (std::size_t index = 0; index <= kMaxProviderChartSamples; ++index) {
        if (index != 0)
            oversizedChart.push_back(',');
        oversizedChart += std::to_string(index + 1);
    }
    oversizedChart += R"json(],"indicators":{"quote":[{"close":[)json";
    for (std::size_t index = 0; index <= kMaxProviderChartSamples; ++index) {
        if (index != 0)
            oversizedChart.push_back(',');
        oversizedChart += "10";
    }
    oversizedChart += R"json(]}]}}]}})json";
    StockData oversizedDestination;
    oversizedDestination.currentPrice = 88.0;
    Check(!ApplyYahooChartPayload(std::move(oversizedChart), false, oversizedDestination) &&
              Near(oversizedDestination.currentPrice, 88.0) &&
              oversizedDestination.closes.empty(),
          "oversized provider arrays are rejected before vector allocation");

    const auto quote =
        ParseFinnhubQuotePayload(R"json({"c":101.5,"pc":99,"h":103,"l":98,"o":100,"t":1700000000})json");
    Check(quote && Near(quote->currentPrice, 101.5) && Near(quote->previousClose, 99.0) &&
              Near(quote->dayHigh, 103.0) && Near(quote->dayLow, 98.0) &&
              Near(quote->openPrice, 100.0),
          "Finnhub quote fields are decoded without application state");
    Check(FinnhubQuotePayloadHasPrice(R"json({"c":101.5})json"),
          "positive Finnhub quote is recognized");
    Check(!FinnhubQuotePayloadHasPrice(R"json({"c":0})json") &&
              !FinnhubQuotePayloadHasPrice("[]") &&
              !ParseFinnhubQuotePayload("not json").has_value(),
          "empty, non-object, and malformed Finnhub prices are rejected");

    const auto marketCap = ParseFinnhubMetricMarketCap(
        R"json({"metric":{"marketCapitalization":2431.5}})json");
    Check(marketCap && Near(*marketCap, 2431500000.0),
          "Finnhub market capitalization is normalized from millions");
    Check(!ParseFinnhubMetricMarketCap(
               R"json({"metric":{"marketCapitalization":0}})json")
               .has_value() &&
              !ParseFinnhubMetricMarketCap(R"json({"metric":{}})json").has_value() &&
              !ParseFinnhubMetricMarketCap("not json").has_value(),
          "missing, non-positive, and malformed market capitalizations are rejected");

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All SquareStar provider payload parser tests passed\n";
    return EXIT_SUCCESS;
}
