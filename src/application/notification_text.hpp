#pragma once

#include <chrono>
#include <cstddef>
#include <ctime>
#include <string>
#include <string_view>
#include <vector>

namespace squarestar::application {

// A grouped card shows at most five detail rows, but it can absorb a much
// larger short burst and summarize the remainder instead of splitting one
// 1.5-second burst into multiple cards.
inline constexpr std::size_t kNotificationMaxStackRows = 5;
inline constexpr std::size_t kNotificationMaxGroupedRows = 64;
inline constexpr std::size_t kNotificationMaxVisibleBlocks = 5;
inline constexpr std::size_t kNotificationCenterMaxEntries = 20;
inline constexpr auto kNotificationGroupingWindow = std::chrono::milliseconds(1500);
inline constexpr auto kNotificationHoldDuration = std::chrono::seconds(5);

struct StockMoveNotification {
    std::string ticker;
    double before = 0.0;
    double after = 0.0;
    bool priceAlert = false;
    double alertThreshold = 0.0;
    std::string currency;
};

struct BackgroundNotificationText {
    std::string title;
    std::string message;
    std::string accentText;
    int accentDirection = 0;
};

std::string FormatStockMovePrices(double before,
                                  double after,
                                  std::string_view currency);
std::string FormatStockMoveDelta(double before, double after);
BackgroundNotificationText FormatBackgroundStockMoves(
    const std::vector<StockMoveNotification>& rows,
    std::size_t maximumMessageBytes = 1024);
} // namespace squarestar::application
