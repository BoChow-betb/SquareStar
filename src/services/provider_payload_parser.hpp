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

struct FinnhubQuote {
    double currentPrice = 0.0;
    double previousClose = 0.0;
    double dayHigh = 0.0;
    double dayLow = 0.0;
    double openPrice = 0.0;
    std::time_t timestamp = 0;
};

// Parses and validates one Yahoo chart response without any network, GUI, or
// application-state dependency. The destination is changed only after a usable
// series with at least two chronological price samples has been decoded.
bool ApplyYahooChartPayload(std::string payload,
                            bool aggregateLatestTradingDayVolume,
                            squarestar::market::StockData& destination);

bool YahooChartPayloadHasUsableSeries(std::string payload);

std::vector<YahooQuoteSnapshot> ParseYahooQuoteBatchPayload(std::string payload);

// Decodes the latest positive FX quote from a Yahoo chart response. The
// regular-market value is preferred because it is the same quote snapshot used
// by the stock header; the newest close is a fallback for sparse FX payloads.

std::optional<FinnhubQuote> ParseFinnhubQuotePayload(std::string_view payload);
bool FinnhubQuotePayloadHasPrice(std::string_view payload);

// Finnhub reports marketCapitalization in millions of the quote currency.
// Return the normalized whole-currency value used by StockData/ScreenerData.
std::optional<double> ParseFinnhubMetricMarketCap(std::string_view payload);

} // namespace squarestar::providers
