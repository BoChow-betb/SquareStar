#pragma once

#include <cstdint>
#include <string>

#include "domain/chart_ranges.hpp"
#include "domain/stock_data.hpp"

namespace squarestar::marketdata {

struct StockProviderPayloads {
    std::string yahooQuote;
    std::string finnhubQuote;
    std::string finnhubMetrics;
    std::string finnhubProfile;
    std::string finnhubNews;
};

struct StockProviderMergeOptions {
    squarestar::market::FetchKind kind = squarestar::market::FetchKind::Full;
    int requestedTimeRangeIndex = 0;
    bool detailsLoad = false;
    bool wantMetrics = false;
    bool wantNews = false;
    bool needsCorporateActionEvidence = false;
};

struct StockProviderMergeResult {
    bool finnhubProfileExists = false;
    std::string finnhubProfileCountry;
};

// Decode provider payloads and apply precedence rules without performing I/O.
StockProviderMergeResult ApplyStockProviderPayloads(
    StockProviderPayloads payloads,
    const StockProviderMergeOptions& options,
    squarestar::market::StockFetchResult& destination);

} // namespace squarestar::marketdata
