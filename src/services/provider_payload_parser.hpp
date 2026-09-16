#pragma once

#include <cstddef>
#include <ctime>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "domain/stock_data.hpp"

namespace squarestar::providers {

inline constexpr std::size_t kMaxProviderPayloadBytes = 16 * 1024 * 1024;
inline constexpr std::size_t kMaxProviderChartSamples = 100000;


struct YahooQuoteSnapshot {
    std::string symbol;
    double currentPrice = 0.0;
    double previousClose = 0.0;
    double dayHigh = 0.0;
    double dayLow = 0.0;
    double openPrice = 0.0;
    std::time_t timestamp = 0;
};

struct YahooFxRateSnapshot {
    double rate = 0.0;
    std::time_t timestamp = 0;
};

struct FinnhubQuote {
    double currentPrice = 0.0;
    double previousClose = 0.0;
    double dayHigh = 0.0;
    double dayLow = 0.0;
    double openPrice = 0.0;
    std::time_t timestamp = 0;
};


bool ApplyYahooChartPayload(std::string payload,
                            bool aggregateLatestTradingDayVolume,
                            squarestar::market::StockData& destination);

bool YahooChartPayloadHasUsableSeries(std::string payload);

std::vector<YahooQuoteSnapshot> ParseYahooQuoteBatchPayload(std::string payload);


std::optional<YahooFxRateSnapshot> ParseYahooFxRatePayload(std::string payload);

std::optional<FinnhubQuote> ParseFinnhubQuotePayload(std::string_view payload);
bool FinnhubQuotePayloadHasPrice(std::string_view payload);


std::optional<double> ParseFinnhubMetricMarketCap(std::string_view payload);

}
