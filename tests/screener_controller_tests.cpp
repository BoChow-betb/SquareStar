#include "application/app_state.hpp"
#include "application/app_limits.hpp"
#include "application/main_loop_signal.hpp"
#include "application/screener_cache.hpp"
#include "application/screener_controller.hpp"
#include "application/screener_executor.hpp"
#include "domain/market_runtime.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <ctime>
#include <functional>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using squarestar::application::AppState;
using squarestar::application::ScreenerDataSource;
using squarestar::application::ScreenerItem;
using squarestar::application::ScreenerLoadState;

class FakeScreenerDataSource final : public ScreenerDataSource {
  public:
    std::function<bool(const std::string&,
                       const std::vector<std::string>&,
                       std::size_t,
                       const squarestar::application::ScreenerCancelCheck&,
                       const squarestar::application::ScreenerProgressCallback&,
                       std::vector<ScreenerItem>&)>
        fetchMarketScreener;
    std::function<bool(std::vector<ScreenerItem>&,
                       std::size_t,
                       std::size_t,
                       const squarestar::application::ScreenerCancelCheck&,
                       const squarestar::application::ScreenerProgressCallback&)>
        enrichSparklines;
    std::optional<bool> marketOpenOverride;
    std::function<bool(const std::string&,
                       std::chrono::system_clock::time_point&,
                       std::vector<ScreenerItem>&)>
        loadPersistentCache;
    std::function<bool(const std::string&,
                       std::chrono::system_clock::time_point,
                       const std::vector<ScreenerItem>&)>
        storePersistentCache;

    bool FetchMarketScreener(
        const std::string& screenerId,
        const std::vector<std::string>& watchlist,
        std::size_t limit,
        const squarestar::application::ScreenerCancelCheck& cancelled,
        const squarestar::application::ScreenerProgressCallback& progress,
        std::vector<ScreenerItem>& items) override {
        return fetchMarketScreener &&
               fetchMarketScreener(
                   screenerId, watchlist, limit, cancelled, progress, items);
    }

    bool EnrichSparklines(
        std::vector<ScreenerItem>& items,
        std::size_t firstIndex,
        std::size_t count,
        const squarestar::application::ScreenerCancelCheck& cancelled,
        const squarestar::application::ScreenerProgressCallback& progress) override {
        return enrichSparklines &&
               enrichSparklines(items, firstIndex, count, cancelled, progress);
    }

    [[nodiscard]] bool MarketOpen() const noexcept override {
        return marketOpenOverride.value_or(squarestar::market::CachedMarketOpen());
    }

    bool LoadPersistentCache(
        const std::string& cacheId,
        std::chrono::system_clock::time_point& fetchedAt,
        std::vector<ScreenerItem>& items) override {
        return loadPersistentCache && loadPersistentCache(cacheId, fetchedAt, items);
    }

    bool StorePersistentCache(
        const std::string& cacheId,
        std::chrono::system_clock::time_point fetchedAt,
        const std::vector<ScreenerItem>& items) override {
        return !storePersistentCache || storePersistentCache(cacheId, fetchedAt, items);
    }
};

void Require(bool condition, const char* message) {
    if (condition)
        return;
    std::cerr << "FAILED: " << message << '\n';
    std::cerr.flush();
    std::_Exit(EXIT_FAILURE);
}

template <typename Predicate>
bool WaitUntil(Predicate predicate,
               std::chrono::milliseconds timeout = std::chrono::seconds(3)) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return predicate();
}

std::function<bool()> RunLifecycleTest() {
    using squarestar::application::ResetScreenerFetch;
    using squarestar::application::ShutdownLatestScreenerJobExecutor;
    using squarestar::application::StartScreenerFetch;
    using squarestar::application::StartScreenerTrendFetch;
    std::function<bool()> retainedCancellation;
    AppState state;
    std::mutex gateMutex;
    std::condition_variable gate;
    bool firstRequestStarted = false;

    FakeScreenerDataSource dependencies;
    dependencies.fetchMarketScreener =
        [&](const std::string& screenerId,
            const std::vector<std::string>&,
            std::size_t,
            const squarestar::application::ScreenerCancelCheck& cancelled,
            const squarestar::application::ScreenerProgressCallback&,
            std::vector<ScreenerItem>& items) {
            if (screenerId == "first") {
                {
                    std::lock_guard<std::mutex> lock(gateMutex);
                    retainedCancellation = cancelled;
                    firstRequestStarted = true;
                }
                gate.notify_all();
                while (!cancelled())
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            ScreenerItem item;
            item.symbol = screenerId == "first" ? "STALE" : "LATEST";
            item.hasPrice = true;
            items.push_back(std::move(item));
            return true;
        };
    dependencies.enrichSparklines =
        [](std::vector<ScreenerItem>&,
           std::size_t,
           std::size_t,
           const squarestar::application::ScreenerCancelCheck&,
           const squarestar::application::ScreenerProgressCallback&) {
            return true;
        };

    StartScreenerFetch(state, "first", dependencies);
    {
        std::unique_lock<std::mutex> lock(gateMutex);
        Require(gate.wait_for(lock,
                              std::chrono::seconds(2),
                              [&] { return firstRequestStarted; }),
                "the first request should start");
    }

    StartScreenerFetch(state, "second", dependencies);
    Require(retainedCancellation && retainedCancellation(),
            "switching must cancel the previous request-owned session");
    const auto switched = state.marketData.LoadScreenerSnapshot();
    Require(switched && switched->screenerId == "second" &&
                switched->loadState == ScreenerLoadState::Loading &&
                !switched->HasRows(),
            "switching must atomically clear the previous route and rows");

    Require(WaitUntil([&] {
                const auto snapshot = state.marketData.LoadScreenerSnapshot();
                return snapshot && snapshot->screenerId == "second" &&
                       snapshot->loadState == ScreenerLoadState::Ready &&
                       snapshot->HasRows();
            }),
            "the latest request should publish");
    const auto completed = state.marketData.LoadScreenerSnapshot();
    Require(completed->items->size() == 1 &&
                completed->items->front().symbol == "LATEST",
            "a cancelled request must never republish stale rows");

    // A burst of selections leaves only one pending latest job. This exercises
    // the generation guard and the executor's replacement path together.
    for (int index = 0; index < 64; ++index)
        StartScreenerFetch(state, "burst_" + std::to_string(index), dependencies);
    Require(WaitUntil([&] {
                const auto snapshot = state.marketData.LoadScreenerSnapshot();
                return snapshot && snapshot->screenerId == "burst_63" &&
                       snapshot->loadState == ScreenerLoadState::Ready &&
                       snapshot->HasRows();
            }),
            "rapid switching should settle on the final route without a stale publish");

    // A list refresh owns publication while it is active. Trend enrichment is
    // deferred so the two workers cannot race to replace rows.
    const auto latest = state.marketData.LoadScreenerSnapshot();
    squarestar::application::ScreenerSnapshot refreshing;
    refreshing.generation = latest->generation;
    refreshing.screenerId = latest->screenerId;
    refreshing.loadState = ScreenerLoadState::Refreshing;
    refreshing.items = latest->items;
    state.marketData.PublishScreenerSnapshot(std::move(refreshing));
    Require(!StartScreenerTrendFetch(state, "burst_63", 0, 1, dependencies),
            "trend work must not overlap a list refresh");

    std::mutex phaseMutex;
    std::vector<std::pair<std::size_t, std::size_t>> trendPhases;
    FakeScreenerDataSource progressiveDependencies;
    progressiveDependencies.fetchMarketScreener =
        [](const std::string&,
           const std::vector<std::string>&,
           std::size_t,
           const squarestar::application::ScreenerCancelCheck&,
           const squarestar::application::ScreenerProgressCallback&,
           std::vector<ScreenerItem>& items) {
            items.clear();
            for (std::size_t index = 0;
                 index < squarestar::application::kMaximizedOverviewRows;
                 ++index) {
                ScreenerItem item;
                item.symbol = "PROGRESS_" + std::to_string(index);
                item.hasPrice = true;
                items.push_back(std::move(item));
            }
            return true;
        };
    progressiveDependencies.enrichSparklines =
        [&](std::vector<ScreenerItem>& items,
            std::size_t firstIndex,
            std::size_t count,
            const squarestar::application::ScreenerCancelCheck& cancelled,
            const squarestar::application::ScreenerProgressCallback& progress) {
            {
                std::lock_guard<std::mutex> lock(phaseMutex);
                trendPhases.emplace_back(firstIndex, count);
            }
            for (std::size_t index = firstIndex; index < firstIndex + count; ++index) {
                items[index].sparklineAttempted = true;
                items[index].sparkline = {10.0f, 11.0f};
            }
            if (progress && !progress(items))
                return false;
            return !cancelled();
        };

    StartScreenerFetch(state, "progressive", progressiveDependencies);
    Require(WaitUntil([&] {
                const auto snapshot = state.marketData.LoadScreenerSnapshot();
                return snapshot && snapshot->screenerId == "progressive" &&
                       snapshot->loadState == ScreenerLoadState::Ready &&
                       snapshot->items &&
                       snapshot->items->size() ==
                           squarestar::application::kMaximizedOverviewRows;
            }),
            "the progressive trend fixture should publish its list");
    const auto trendRedrawRevision =
        squarestar::application::GuiRedrawRevision();
    Require(StartScreenerTrendFetch(state,
                                    "progressive",
                                    0,
                                    squarestar::application::kMaximizedOverviewRows,
                                    progressiveDependencies),
            "the maximized visible trend request should start");
    Require(WaitUntil([&] {
                const auto snapshot = state.marketData.LoadScreenerSnapshot();
                return snapshot && snapshot->items &&
                       std::all_of(snapshot->items->begin(),
                                   snapshot->items->end(),
                                   [](const ScreenerItem& item) {
                                       return item.sparkline.size() >= 2;
                                   });
            }),
            "all visible trends should publish from one controller pass");
    Require(WaitUntil([&] {
                return !state.requests.screenerTrendRequest.load(
                           std::memory_order_acquire) &&
                       squarestar::application::GuiRedrawRevision() >
                           trendRedrawRevision;
            }),
            "trend completion should retire its session and request a redraw");
    {
        std::lock_guard<std::mutex> lock(phaseMutex);
        Require(trendPhases.size() == 1 &&
                    trendPhases[0] ==
                        std::pair<std::size_t, std::size_t>{
                            0, squarestar::application::kMaximizedOverviewRows},
                "maximized trend work must cover all visible rows in one pass");
    }

    std::vector<ScreenerItem> cachedLiveRows(1);
    cachedLiveRows[0].symbol = "LIVE";
    cachedLiveRows[0].price = 100.0;
    cachedLiveRows[0].hasPrice = true;
    cachedLiveRows[0].changePercent = 2.0;
    cachedLiveRows[0].hasChangePercent = true;
    Require(state.screenerCache.Store("live_refresh", cachedLiveRows),
            "the live-refresh fixture should enter the app-owned cache");
    std::mutex refreshMutex;
    std::condition_variable refreshGate;
    bool refreshFetchStarted = false;
    bool releasePartial = false;
    bool partialPublished = false;
    bool releaseFinal = false;
    std::atomic_int refreshFetchCalls{0};
    FakeScreenerDataSource liveRefreshDependencies;
    // Force market-closed behavior here. Otherwise this cached-row test changes
    // behavior with the real trading session.
    liveRefreshDependencies.marketOpenOverride = false;
    liveRefreshDependencies.fetchMarketScreener =
        [&](const std::string&,
            const std::vector<std::string>&,
            std::size_t,
            const squarestar::application::ScreenerCancelCheck& cancelled,
            const squarestar::application::ScreenerProgressCallback& progress,
            std::vector<ScreenerItem>& items) {
            const int call = refreshFetchCalls.fetch_add(1, std::memory_order_acq_rel) + 1;
            if (call == 1) {
                {
                    std::unique_lock<std::mutex> lock(refreshMutex);
                    refreshFetchStarted = true;
                    refreshGate.notify_all();
                    refreshGate.wait(lock, [&] {
                        return releasePartial || cancelled();
                    });
                }
                ScreenerItem partial;
                partial.symbol = "LIVE";
                partial.price = 101.0;
                partial.hasPrice = true;
                if (!progress(std::vector<ScreenerItem>{partial}))
                    return true;
                {
                    std::unique_lock<std::mutex> lock(refreshMutex);
                    partialPublished = true;
                    refreshGate.notify_all();
                    refreshGate.wait(lock, [&] {
                        return releaseFinal || cancelled();
                    });
                }
            }
            ScreenerItem finalItem;
            finalItem.symbol = "LIVE";
            finalItem.price = call == 1 ? 102.0 : 103.0;
            finalItem.hasPrice = true;
            items = {std::move(finalItem)};
            return true;
        };
    liveRefreshDependencies.enrichSparklines =
        [](std::vector<ScreenerItem>&,
           std::size_t,
           std::size_t,
           const squarestar::application::ScreenerCancelCheck&,
           const squarestar::application::ScreenerProgressCallback&) {
            return true;
        };

    StartScreenerFetch(state, "live_refresh", liveRefreshDependencies);
    {
        std::unique_lock<std::mutex> lock(refreshMutex);
        Require(refreshGate.wait_for(lock,
                                     std::chrono::seconds(2),
                                     [&] { return refreshFetchStarted; }),
                "a cache hit must still start a live request");
    }
    const auto cachedRefreshSnapshot = state.marketData.LoadScreenerSnapshot();
    Require(cachedRefreshSnapshot && cachedRefreshSnapshot->items &&
                cachedRefreshSnapshot->loadState == ScreenerLoadState::Refreshing &&
                std::abs(cachedRefreshSnapshot->items->front().price - 100.0) < 1e-9,
            "cached rows should remain visible only while live refresh is pending");
    {
        std::lock_guard<std::mutex> lock(refreshMutex);
        releasePartial = true;
    }
    refreshGate.notify_all();
    {
        std::unique_lock<std::mutex> lock(refreshMutex);
        Require(refreshGate.wait_for(lock,
                                     std::chrono::seconds(2),
                                     [&] { return partialPublished; }),
                "the partial live row should publish before final completion");
    }
    const auto partialRefreshSnapshot = state.marketData.LoadScreenerSnapshot();
    Require(partialRefreshSnapshot && partialRefreshSnapshot->items &&
                partialRefreshSnapshot->loadState == ScreenerLoadState::Refreshing &&
                std::abs(partialRefreshSnapshot->items->front().price - 101.0) < 1e-9 &&
                partialRefreshSnapshot->items->front().hasChangePercent,
            "fresh partial fields must replace cache while cached gaps stay visible");
    {
        std::lock_guard<std::mutex> lock(refreshMutex);
        releaseFinal = true;
    }
    refreshGate.notify_all();
    Require(WaitUntil([&] {
                const auto snapshot = state.marketData.LoadScreenerSnapshot();
                return snapshot && snapshot->items &&
                       snapshot->loadState == ScreenerLoadState::Ready &&
                       std::abs(snapshot->items->front().price - 102.0) < 1e-9 &&
                       !snapshot->items->front().hasChangePercent;
            }),
            "the final live snapshot must replace all presentation-only cache fields");
    Require(state.requests.screenerRefreshAfterEpochSeconds.load(
                std::memory_order_acquire) > static_cast<std::int64_t>(std::time(nullptr)),
            "a completed live request should schedule the next refresh");

    StartScreenerFetch(state, "live_refresh", liveRefreshDependencies);
    Require(WaitUntil([&] {
                const auto snapshot = state.marketData.LoadScreenerSnapshot();
                return refreshFetchCalls.load(std::memory_order_acquire) >= 2 &&
                       snapshot && snapshot->items &&
                       snapshot->loadState == ScreenerLoadState::Ready &&
                       std::abs(snapshot->items->front().price - 103.0) < 1e-9;
            }),
            "even a fresh cache hit must revalidate and publish changed table data");

    // A newly entered route during market hours waits for live data. Periodic
    // refreshes keep the rows already on screen.
    AppState marketOpenEntryState;
    std::vector<ScreenerItem> staleEntryRows(1);
    staleEntryRows[0].symbol = "OLD";
    staleEntryRows[0].price = 77.0;
    staleEntryRows[0].hasPrice = true;
    Require(marketOpenEntryState.screenerCache.Store("market_open_entry", staleEntryRows),
            "the market-open entry fixture should enter the app-owned cache");
    std::mutex entryMutex;
    std::condition_variable entryGate;
    bool entryFetchStarted = false;
    bool releaseEntryFetch = false;
    FakeScreenerDataSource marketOpenEntryDependencies;
    marketOpenEntryDependencies.marketOpenOverride = true;
    marketOpenEntryDependencies.fetchMarketScreener =
        [&](const std::string&,
            const std::vector<std::string>&,
            std::size_t,
            const squarestar::application::ScreenerCancelCheck& cancelled,
            const squarestar::application::ScreenerProgressCallback&,
            std::vector<ScreenerItem>& items) {
            {
                std::unique_lock<std::mutex> lock(entryMutex);
                entryFetchStarted = true;
                entryGate.notify_all();
                entryGate.wait(lock, [&] { return releaseEntryFetch || cancelled(); });
            }
            ScreenerItem live;
            live.symbol = "LIVE";
            live.price = 78.0;
            live.hasPrice = true;
            items = {std::move(live)};
            return true;
        };
    marketOpenEntryDependencies.enrichSparklines =
        [](std::vector<ScreenerItem>&,
           std::size_t,
           std::size_t,
           const squarestar::application::ScreenerCancelCheck&,
           const squarestar::application::ScreenerProgressCallback&) {
            return true;
        };
    StartScreenerFetch(
        marketOpenEntryState, "market_open_entry", marketOpenEntryDependencies);
    {
        std::unique_lock<std::mutex> lock(entryMutex);
        Require(entryGate.wait_for(lock,
                                   std::chrono::seconds(2),
                                   [&] { return entryFetchStarted; }),
                "market-open entry should immediately start its live request");
    }
    const auto marketOpenLoading = marketOpenEntryState.marketData.LoadScreenerSnapshot();
    Require(marketOpenLoading &&
                marketOpenLoading->loadState == ScreenerLoadState::Loading &&
                !marketOpenLoading->HasRows(),
            "market-open entry must not flash stale cache before live data arrives");
    {
        std::lock_guard<std::mutex> lock(entryMutex);
        releaseEntryFetch = true;
    }
    entryGate.notify_all();
    Require(WaitUntil([&] {
                const auto snapshot =
                    marketOpenEntryState.marketData.LoadScreenerSnapshot();
                return snapshot && snapshot->loadState == ScreenerLoadState::Ready &&
                       snapshot->items && snapshot->items->size() == 1 &&
                       snapshot->items->front().symbol == "LIVE";
            }),
            "market-open entry should atomically reveal only the live snapshot");

    std::mutex restartMutex;
    std::condition_variable restartGate;
    bool restartFetchStarted = false;
    bool releaseRestartFetch = false;
    std::atomic_int persistentLoads{0};
    std::atomic_int persistentStores{0};
    FakeScreenerDataSource restartDependencies;
    restartDependencies.loadPersistentCache =
        [&](const std::string& cacheKey,
            std::chrono::system_clock::time_point& fetchedAt,
            std::vector<ScreenerItem>& items) {
            ++persistentLoads;
            if (cacheKey != "trending_now")
                return false;
            fetchedAt = std::chrono::system_clock::now() -
                        std::chrono::minutes(2);
            ScreenerItem cached;
            cached.symbol = "DISK";
            cached.price = 88.0;
            cached.hasPrice = true;
            items = {std::move(cached)};
            return true;
        };
    restartDependencies.storePersistentCache =
        [&](const std::string& cacheKey,
            std::chrono::system_clock::time_point,
            const std::vector<ScreenerItem>& items) {
            if (cacheKey == "trending_now" && !items.empty() &&
                items.front().symbol == "LIVE_DISK") {
                ++persistentStores;
            }
            return true;
        };
    restartDependencies.marketOpenOverride = false;
    restartDependencies.fetchMarketScreener =
        [&](const std::string&,
            const std::vector<std::string>&,
            std::size_t,
            const squarestar::application::ScreenerCancelCheck& cancelled,
            const squarestar::application::ScreenerProgressCallback&,
            std::vector<ScreenerItem>& items) {
            {
                std::unique_lock<std::mutex> lock(restartMutex);
                restartFetchStarted = true;
                restartGate.notify_all();
                restartGate.wait(lock, [&] {
                    return releaseRestartFetch || cancelled();
                });
            }
            ScreenerItem live;
            live.symbol = "LIVE_DISK";
            live.price = 89.0;
            live.hasPrice = true;
            items = {std::move(live)};
            return true;
        };
    restartDependencies.enrichSparklines =
        [](std::vector<ScreenerItem>&,
           std::size_t,
           std::size_t,
           const squarestar::application::ScreenerCancelCheck&,
           const squarestar::application::ScreenerProgressCallback&) {
            return true;
        };

    const auto restartRedrawRevision =
        squarestar::application::GuiRedrawRevision();
    StartScreenerFetch(state, "trending_now", restartDependencies);
    {
        std::unique_lock<std::mutex> lock(restartMutex);
        Require(restartGate.wait_for(lock,
                                     std::chrono::seconds(2),
                                     [&] { return restartFetchStarted; }),
                "restart revalidation should begin in the background");
    }
    const auto restored = state.marketData.LoadScreenerSnapshot();
    Require(restored && restored->loadState == ScreenerLoadState::Refreshing &&
                restored->items && restored->items->size() == 1 &&
                restored->items->front().symbol == "DISK" &&
                persistentLoads.load(std::memory_order_acquire) == 1,
            "a restart should show the validated disk snapshot without waiting for HTTP");
    {
        std::lock_guard<std::mutex> lock(restartMutex);
        releaseRestartFetch = true;
    }
    restartGate.notify_all();
    Require(WaitUntil([&] {
                const auto snapshot = state.marketData.LoadScreenerSnapshot();
                return snapshot && snapshot->loadState == ScreenerLoadState::Ready &&
                       snapshot->items &&
                       snapshot->items->front().symbol == "LIVE_DISK" &&
                       persistentStores.load(std::memory_order_acquire) == 1;
            }),
            "the live result should atomically replace and persist the restart snapshot");
    Require(squarestar::application::GuiRedrawRevision() >=
                restartRedrawRevision + 2,
            "the controller should request redraws for initial and live publication");
    Require(state.requests.screenerRefreshAfterEpochSeconds.load(
                std::memory_order_acquire) >=
                static_cast<std::int64_t>(std::time(nullptr)) + 4 * 60,
            "closed-market Trending Now should retain its quiet refresh cadence");

    ResetScreenerFetch(state);
    ShutdownLatestScreenerJobExecutor();
    return retainedCancellation;
}

} // namespace

int main() {
    const std::function<bool()> retainedCancellation = RunLifecycleTest();
    Require(retainedCancellation && retainedCancellation(),
            "HTTP cancellation callbacks must remain safe after AppState destruction");
    std::cout << "All screener controller lifecycle tests passed\n";
    return EXIT_SUCCESS;
}
