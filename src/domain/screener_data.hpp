#pragma once

#include <cstdint>
#include <ctime>
#include <string>
#include <utility>
#include <vector>

namespace squarestar::market {

struct ScreenerData {
    std::vector<float> sparkline;
    uint64_t sparklineRevision = 0;
    std::string symbol, name;
    double price = 0.0, change = 0.0, changePercent = 0.0, volume = 0.0;
    double avgVol3M = 0.0, marketCap = 0.0, peRatio = 0.0, fiftyTwoWkChange = 0.0;
    bool hasPrice = false, hasChange = false, hasChangePercent = false, hasVolume = false;
    bool hasAvgVol3M = false, hasMarketCap = false, hasPeRatio = false;
    bool hasFiftyTwoWkChange = false;
    std::time_t lastMarketTime = 0;
    bool resolved = false, suspectedInactive = false;


    bool sparklineAttempted = false;
};

inline void ReplaceScreenerSparkline(ScreenerData& data, std::vector<float> values) {
    data.sparkline = std::move(values);
    ++data.sparklineRevision;
    if (data.sparklineRevision == 0)
        ++data.sparklineRevision;
}

}
