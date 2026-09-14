#include "services/screener_service.hpp"

#include "application/screener_controller.hpp"
#include "domain/market_runtime.hpp"
#include "services/network_runtime.hpp"
#include "services/screener_cache_persistence.hpp"

namespace squarestar::providers {
namespace {

ScreenerFetchDependencies DefaultScreenerFetchDependencies() {
    return {
        .publishPartial = {},
        .queueHttpGet = QueueScreenerHttpGet,
        .queueBatchHttpGet = QueueBatchHttpGet,
        .queueYahooAuthenticatedGet = QueueYahooAuthenticatedGet,
        .queueYahooScreenerPost = QueueYahooScreenerPost,
    };
}

} // namespace
} // namespace squarestar::providers

namespace squarestar::application {
namespace {

class ProductionScreenerDataSource final : public ScreenerDataSource {
  public:
    bool FetchMarketScreener(
        const std::string& screenerId,
        const std::vector<std::string>& watchlist,
        std::size_t limit,
        const ScreenerCancelCheck& cancelled,
        const ScreenerProgressCallback& progress,
        std::vector<ScreenerItem>& items) override {
        auto dependencies = squarestar::providers::DefaultScreenerFetchDependencies();
        dependencies.publishPartial = progress;
        return squarestar::providers::FetchMarketScreener(
            screenerId, watchlist, limit, cancelled, dependencies, items);
    }

    bool EnrichSparklines(
        std::vector<ScreenerItem>& items,
        std::size_t firstIndex,
        std::size_t count,
        const ScreenerCancelCheck& cancelled,
        const ScreenerProgressCallback& progress) override {
        auto dependencies = squarestar::providers::DefaultScreenerFetchDependencies();
        dependencies.publishPartial = progress;
        return squarestar::providers::EnrichMarketScreenerSparklines(
            items, firstIndex, count, cancelled, dependencies);
    }

    [[nodiscard]] bool MarketOpen() const noexcept override {
        return squarestar::market::CachedMarketOpen();
    }

    bool LoadPersistentCache(
        const std::string& cacheId,
        std::chrono::system_clock::time_point& fetchedAt,
        std::vector<ScreenerItem>& items) override {
        return squarestar::providers::LoadPersistentScreenerCache(
            cacheId, fetchedAt, items);
    }

    bool StorePersistentCache(
        const std::string& cacheId,
        std::chrono::system_clock::time_point fetchedAt,
        const std::vector<ScreenerItem>& items) override {
        return squarestar::providers::QueuePersistentScreenerCache(
            cacheId, fetchedAt, items);
    }
};

} // namespace

ScreenerDataSource& DefaultScreenerDataSource() {
    static ProductionScreenerDataSource source;
    return source;
}

bool StartScreenerTrendFetch(AppState& state,
                             const std::string& screenerId,
                             std::size_t firstIndex,
                             std::size_t count) {
    return StartScreenerTrendFetch(
        state, screenerId, firstIndex, count, DefaultScreenerDataSource());
}

void StartScreenerFetch(AppState& state, const std::string& screenerId) {
    StartScreenerFetch(state, screenerId, DefaultScreenerDataSource());
}

} // namespace squarestar::application
