#pragma once

#include <cstddef>
#include <cstdint>

namespace squarestar::application {

inline constexpr int kDefaultGuiWindowWidth = 1280;
inline constexpr int kDefaultGuiWindowHeight = 800;
inline constexpr std::size_t kMaxActiveStockTabs = 16;
inline constexpr std::size_t kMaxLiteMonitorStockTabs = 4;
inline constexpr std::size_t kMinMonitorStockTiles = 2;
inline constexpr std::size_t kMaxMonitorStockTiles = 9;
inline constexpr std::size_t kWindowedOverviewRows = 12;
inline constexpr std::size_t kMaximizedOverviewRows = 18;
inline constexpr std::size_t kScreenerListRowLimit = 96;
inline constexpr std::size_t kMaxWatchlistItems = 256;
inline constexpr std::size_t kMaxPriceAlerts = 128;

} // namespace squarestar::application
