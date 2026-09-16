#pragma once

#include <chrono>
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "application/screener_item.hpp"

namespace squarestar::application {

struct AppState;

using ScreenerCancelCheck = std::function<bool()>;
using ScreenerProgressCallback =
    std::function<bool(const std::vector<ScreenerItem>&)>;

class ScreenerDataSource {
  public:
    virtual ~ScreenerDataSource() = default;

    virtual bool FetchMarketScreener(
        const std::string& screenerId,
        const std::vector<std::string>& watchlist,
        std::size_t limit,
        const ScreenerCancelCheck& cancelled,
        const ScreenerProgressCallback& progress,
        std::vector<ScreenerItem>& items) = 0;

    virtual bool EnrichSparklines(
        std::vector<ScreenerItem>& items,
        std::size_t firstIndex,
        std::size_t count,
        const ScreenerCancelCheck& cancelled,
        const ScreenerProgressCallback& progress) = 0;

    [[nodiscard]] virtual bool MarketOpen() const noexcept = 0;

    virtual bool LoadPersistentCache(
        const std::string& cacheId,
        std::chrono::system_clock::time_point& fetchedAt,
        std::vector<ScreenerItem>& items) = 0;

    virtual bool StorePersistentCache(
        const std::string& cacheId,
        std::chrono::system_clock::time_point fetchedAt,
        const std::vector<ScreenerItem>& items) = 0;
};

ScreenerDataSource& DefaultScreenerDataSource();

bool StartScreenerTrendFetch(AppState& state,
                             const std::string& screenerId,
                             std::size_t firstIndex,
                             std::size_t count);
void StartScreenerFetch(AppState& state, const std::string& screenerId);

bool StartScreenerTrendFetch(AppState& state,
                             const std::string& screenerId,
                             std::size_t firstIndex,
                             std::size_t count,
                             ScreenerDataSource& source);
void StartScreenerFetch(AppState& state,
                        const std::string& screenerId,
                        ScreenerDataSource& source);

void ResetScreenerFetch(AppState& state);

}
