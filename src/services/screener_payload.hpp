#pragma once

#include <cstddef>
#include <ctime>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "application/screener_item.hpp"

namespace squarestar::providers {

inline constexpr std::size_t kMaxScreenerSparklineSamples = 512;

using ScreenerItem = squarestar::application::ScreenerItem;
using ScreenerItemIndex = std::unordered_map<std::string, std::size_t>;


std::size_t ApplyYahooQuoteResponsePayload(std::string payload,
                                           const ScreenerItemIndex& itemIndex,
                                           std::vector<ScreenerItem>& items);
std::size_t ApplyYahooEquityQuoteResponsePayload(
    std::string payload,
    const ScreenerItemIndex& itemIndex,
    std::vector<ScreenerItem>& items,
    std::unordered_set<std::string>& equitySymbols);
std::vector<std::string> ParseYahooTrendingSymbols(std::string payload, std::size_t limit);
bool ApplyYahooScreenerChartPayload(std::string payload,
                                    ScreenerItem& item,
                                    bool includeSparkline);
void AppendYahooScreenerPayload(std::string payload,
                                std::size_t limit,
                                std::vector<ScreenerItem>& items);

void MarkInactiveScreenerItems(std::vector<ScreenerItem>& items,
                               std::time_t now = std::time(nullptr)) noexcept;

}
