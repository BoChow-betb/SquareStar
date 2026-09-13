#pragma once

#include <chrono>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "application/screener_item.hpp"

namespace squarestar::application {

std::string BuildScreenerCacheKey(const std::string& screenerId,
                                  const std::vector<std::string>& watchlist);

// A hit keeps rows visible during a live refresh; it never skips that refresh.
class ScreenerCache final {
  public:
    ScreenerCache();
    ~ScreenerCache();

    ScreenerCache(const ScreenerCache&) = delete;
    ScreenerCache& operator=(const ScreenerCache&) = delete;
    ScreenerCache(ScreenerCache&&) = delete;
    ScreenerCache& operator=(ScreenerCache&&) = delete;

    [[nodiscard]] bool LoadForRefresh(const std::string& cacheKey,
                                      std::vector<ScreenerItem>& items) const;
    [[nodiscard]] bool LoadForRefresh(
        const std::string& cacheKey,
        std::vector<ScreenerItem>& items,
        std::chrono::steady_clock::duration maximumDisplayAge) const;

    // fetchedAt uses a wall clock so a validated disk snapshot can retain its
    // original age after restart. Runtime expiry still uses steady_clock.
    [[nodiscard]] bool Store(
        const std::string& cacheKey,
        const std::vector<ScreenerItem>& items,
        std::chrono::system_clock::time_point fetchedAt =
            std::chrono::system_clock::now());
    [[nodiscard]] std::optional<std::chrono::system_clock::time_point>
    UpdateTrends(const std::string& cacheKey,
                 const std::vector<ScreenerItem>& items);
    void Clear();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Fill only fields that have not arrived in a partial live update. Fresh fields
// always win; cached values disappear when the final live snapshot publishes.
std::size_t MergeScreenerRefreshData(const std::vector<ScreenerItem>& cached,
                                     std::vector<ScreenerItem>& live);
std::size_t MergeScreenerTrendData(const std::vector<ScreenerItem>& source,
                                   std::size_t firstIndex,
                                   std::size_t count,
                                   std::vector<ScreenerItem>& destination,
                                   bool includeAttemptedFailures = false);

} // namespace squarestar::application
