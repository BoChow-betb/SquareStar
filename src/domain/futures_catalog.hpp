#pragma once

#include "symbol_search.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace squarestar::market {

struct YahooFuturesInstrument {
    std::string_view symbol;
    std::string_view description;
};

// Common Yahoo continuous/front-month futures symbols. Direct entry also accepts
// valid Yahoo-style alphanumeric *=F symbols that are not listed here.
inline constexpr std::array<YahooFuturesInstrument, 48> kCommonYahooFutures{{
    {"ES=F", "E-mini S&P 500 (SP500) Futures"},
    {"NQ=F", "E-mini Nasdaq-100 Futures"},
    {"YM=F", "Mini Dow Jones Futures"},
    {"RTY=F", "E-mini Russell 2000 Futures"},
    {"MES=F", "Micro E-mini S&P 500 (SP500) Futures"},
    {"MNQ=F", "Micro E-mini Nasdaq-100 Futures"},
    {"M2K=F", "Micro E-mini Russell 2000 Futures"},
    {"MYM=F", "Micro E-mini Dow Jones Futures"},

    {"GC=F", "Gold Futures"},
    {"MGC=F", "Micro Gold Futures"},
    {"SI=F", "Silver Futures"},
    {"SIL=F", "Micro Silver Futures"},
    {"HG=F", "Copper Futures"},
    {"PL=F", "Platinum Futures"},
    {"PA=F", "Palladium Futures"},

    {"CL=F", "WTI Crude Oil Futures"},
    {"MCL=F", "Micro WTI Crude Oil Futures"},
    {"BZ=F", "Brent Crude Oil Futures"},
    {"NG=F", "Natural Gas Futures"},
    {"RB=F", "RBOB Gasoline Futures"},
    {"HO=F", "Heating Oil Futures"},

    {"ZC=F", "Corn Futures"},
    {"ZW=F", "Chicago SRW Wheat Futures"},
    {"KE=F", "KC HRW Wheat Futures"},
    {"ZS=F", "Soybean Futures"},
    {"ZM=F", "Soybean Meal Futures"},
    {"ZL=F", "Soybean Oil Futures"},
    {"ZO=F", "Oats Futures"},
    {"ZR=F", "Rough Rice Futures"},

    {"LE=F", "Live Cattle Futures"},
    {"GF=F", "Feeder Cattle Futures"},
    {"HE=F", "Lean Hogs Futures"},
    {"CC=F", "Cocoa Futures"},
    {"KC=F", "Coffee Futures"},
    {"CT=F", "Cotton Futures"},
    {"SB=F", "Sugar Futures"},
    {"OJ=F", "Orange Juice Futures"},

    {"ZB=F", "U.S. Treasury Bond Futures"},
    {"ZN=F", "10-Year U.S. Treasury Note Futures"},
    {"ZF=F", "5-Year U.S. Treasury Note Futures"},
    {"ZT=F", "2-Year U.S. Treasury Note Futures"},

    {"6E=F", "Euro FX Futures"},
    {"6B=F", "British Pound FX Futures"},
    {"6J=F", "Japanese Yen FX Futures"},
    {"6C=F", "Canadian Dollar FX Futures"},
    {"6A=F", "Australian Dollar FX Futures"},
    {"6S=F", "Swiss Franc FX Futures"},
    {"DX=F", "U.S. Dollar Index Futures"},
}};

inline std::vector<std::pair<std::string, std::string>> SearchCommonYahooFutures(
    const squarestar::search::QueryInfo& query,
    std::size_t maxResults = 10) {
    struct RankedMatch {
        std::string_view symbol;
        std::string_view description;
        int score = 0;
    };

    std::vector<RankedMatch> ranked;
    ranked.reserve(kCommonYahooFutures.size());
    for (const auto& instrument : kCommonYahooFutures) {
        const int score = squarestar::search::RankMatch(
            query, instrument.symbol, instrument.description, "Futures", 0);
        if (score > 0)
            ranked.push_back({instrument.symbol, instrument.description, score});
    }

    const std::size_t count = std::min(maxResults, ranked.size());
    const auto better = [](const RankedMatch& left, const RankedMatch& right) {
        if (left.score != right.score)
            return left.score > right.score;
        if (left.symbol.size() != right.symbol.size())
            return left.symbol.size() < right.symbol.size();
        return left.symbol < right.symbol;
    };
    std::partial_sort(ranked.begin(),
                      ranked.begin() + static_cast<std::ptrdiff_t>(count),
                      ranked.end(),
                      better);

    std::vector<std::pair<std::string, std::string>> matches;
    matches.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        matches.emplace_back(std::string(ranked[index].symbol),
                             std::string(ranked[index].description));
    }
    return matches;
}

inline std::vector<std::pair<std::string, std::string>> SearchCommonYahooFutures(
    std::string_view query,
    std::size_t maxResults = 10) {
    return SearchCommonYahooFutures(squarestar::search::ParseQuery(query), maxResults);
}

} // namespace squarestar::market
