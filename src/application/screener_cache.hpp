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


std::size_t MergeScreenerRefreshData(const std::vector<ScreenerItem>& cached,
                                     std::vector<ScreenerItem>& live);
std::size_t MergeScreenerTrendData(const std::vector<ScreenerItem>& source,
                                   std::size_t firstIndex,
                                   std::size_t count,
                                   std::vector<ScreenerItem>& destination,
                                   bool includeAttemptedFailures = false);

}
