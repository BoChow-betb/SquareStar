#include "application/screener_controller.hpp"

#include "application/debug_diagnostics.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "application/app_limits.hpp"
#include "application/app_state.hpp"
#include "application/main_loop_signal.hpp"
#include "application/runtime_state.hpp"
#include "application/screener_cache.hpp"
#include "application/screener_executor.hpp"
#include "application/screener_item.hpp"
#include "application/screener_request_token.hpp"

namespace squarestar::application {
namespace {

using ScreenerItemsSnapshot = std::shared_ptr<const ScreenerItems>;
using RequestToken = std::shared_ptr<ScreenerRequestToken>;
using RequestTokenSlot = std::atomic<RequestToken>;

constexpr std::int64_t kLiveScreenerRefreshSeconds = 15;
constexpr std::int64_t kClosedMarketTrendingRefreshSeconds = 5 * 60;
constexpr auto kClosedMarketTrendingDisplayAge = std::chrono::hours(24 * 7);

std::int64_t ScreenerRefreshSecondsFor(std::string_view screenerId,
                                       bool marketOpen) noexcept {
    return screenerId == "trending_now" && !marketOpen
               ? kClosedMarketTrendingRefreshSeconds
               : kLiveScreenerRefreshSeconds;
}

ScreenerItemsSnapshot ShareItems(std::vector<ScreenerItem> items) {
    if (items.empty())
        return {};
    return std::make_shared<const ScreenerItems>(std::move(items));
}

RequestToken BeginRequestToken(RequestTokenSlot& slot,
                               std::uint64_t generation) {
    auto token = std::make_shared<ScreenerRequestToken>(generation);
    auto previous = slot.exchange(token, std::memory_order_acq_rel);
    if (previous)
        previous->Cancel();
    return token;
}

void CancelRequestToken(RequestTokenSlot& slot) {
    auto previous = slot.exchange({}, std::memory_order_acq_rel);
    if (previous)
        previous->Cancel();
}

void RetireRequestToken(RequestTokenSlot& slot, const RequestToken& token) {
    auto expected = token;
    if (slot.compare_exchange_strong(expected,
                                     {},
                                     std::memory_order_acq_rel,
                                     std::memory_order_acquire)) {
        token->Cancel();
    }
}

bool TokenCancelled(const RequestToken& token) {
    return !token || token->Cancelled() || ApplicationRuntime().QuitRequested();
}

void ScheduleScreenerRefresh(AppState& state,
                             const std::string& screenerId,
                             ScreenerDataSource& source) {
    const std::int64_t refreshSeconds =
        ScreenerRefreshSecondsFor(screenerId, source.MarketOpen());
    state.requests.screenerRefreshAfterEpochSeconds.store(
        static_cast<std::int64_t>(std::time(nullptr)) + refreshSeconds,
        std::memory_order_release);
}

bool PublishIfCurrent(AppState& state,
                      std::uint64_t generation,
                      const std::string& screenerId,
                      const RequestToken& requestToken,
                      ScreenerLoadState loadState,
                      ScreenerItemsSnapshot items) {
    for (;;) {
        auto current = state.marketData.LoadScreenerSnapshot();
        if (TokenCancelled(requestToken) || !current ||
            current->generation != generation ||
            current->screenerId != screenerId) {
            return false;
        }
        ScreenerSnapshot desired;
        desired.generation = generation;
        desired.screenerId = screenerId;
        desired.loadState = loadState;
        desired.items = items;
        if (state.marketData.CompareExchangeScreenerSnapshot(
                current, std::move(desired))) {
            RequestGuiRedraw();
            return true;
        }
    }
}

void FinishFailedRequest(AppState& state,
                         std::uint64_t generation,
                         const std::string& screenerId,
                         const RequestToken& requestToken,
                         ScreenerDataSource& source) {
    const auto current = state.marketData.LoadScreenerSnapshot();
    if (TokenCancelled(requestToken) || !current ||
        current->generation != generation || current->screenerId != screenerId) {
        return;
    }
    const ScreenerLoadState finalState =
        current->HasRows() ? ScreenerLoadState::Ready : ScreenerLoadState::Failed;
    if (PublishIfCurrent(state,
                         generation,
                         screenerId,
                         requestToken,
                         finalState,
                         current->items)) {
        ScheduleScreenerRefresh(state, screenerId, source);
    }
}

bool PublishScreenerProgress(AppState& state,
                             std::uint64_t generation,
                             const std::string& screenerId,
                             const RequestToken& requestToken,
                             const std::vector<ScreenerItem>& partialItems) {
    if (partialItems.empty() || TokenCancelled(requestToken))
        return !TokenCancelled(requestToken);
    auto current = state.marketData.LoadScreenerSnapshot();
    if (!current || current->generation != generation ||
        current->screenerId != screenerId) {
        return false;
    }
    std::vector<ScreenerItem> displayItems = partialItems;
    if (current->items) {
        MergeScreenerRefreshData(*current->items, displayItems);
    } else {
        std::erase_if(displayItems, [](const ScreenerItem& item) {
            return item.name.empty() ||
                   (!item.resolved && !item.hasPrice && !item.hasChange &&
                    !item.hasChangePercent && !item.hasVolume &&
                    !item.hasMarketCap);
        });
        if (displayItems.empty())
            return true;
    }
    return PublishIfCurrent(state,
                            generation,
                            screenerId,
                            requestToken,
                            ScreenerLoadState::Refreshing,
                            ShareItems(std::move(displayItems)));
}

bool LoadCachedRows(AppState& state,
                    const std::string& cacheId,
                    std::chrono::steady_clock::duration maximumDisplayAge,
                    ScreenerDataSource& source,
                    std::vector<ScreenerItem>& items) {
    if (state.screenerCache.LoadForRefresh(cacheId, items, maximumDisplayAge))
        return true;

    std::chrono::system_clock::time_point fetchedAt;
    std::vector<ScreenerItem> persistentItems;
    try {
        if (!source.LoadPersistentCache(cacheId, fetchedAt, persistentItems) ||
            persistentItems.empty()) {
            return false;
        }
    } catch (...) {
        ReportBackgroundFailure("persistent screener cache load");
        return false;
    }
    if (!state.screenerCache.Store(cacheId, persistentItems, fetchedAt))
        return false;
    return state.screenerCache.LoadForRefresh(
        cacheId, items, maximumDisplayAge);
}

void StorePersistentRows(ScreenerDataSource& source,
                         const std::string& cacheId,
                         std::chrono::system_clock::time_point fetchedAt,
                         const std::vector<ScreenerItem>& items) noexcept {
    try {
        (void)source.StorePersistentCache(cacheId, fetchedAt, items);
    } catch (...) {
        ReportBackgroundFailure("persistent screener cache write");
    }
}

void FetchScreenerDataBackground(
    AppState& state,
    const std::string& screenerId,
    const std::vector<std::string>& watchlistSnapshot,
    const std::string& cacheId,
    std::uint64_t generation,
    const RequestToken& requestToken,
    ScreenerDataSource& source) {
    // HTTP callbacks retain only the request token. It contains generation +
    // cancellation and never retains AppState.
    const auto staleRequest = [requestToken] {
        return TokenCancelled(requestToken);
    };

    const auto publishProgress =
        [&state, generation, &screenerId, &requestToken](
            const std::vector<ScreenerItem>& partialItems) {
            return PublishScreenerProgress(
                state, generation, screenerId, requestToken, partialItems);
        };

    std::vector<ScreenerItem> items;
    const bool routeResolved = source.FetchMarketScreener(
        screenerId,
        watchlistSnapshot,
        kScreenerListRowLimit,
        staleRequest,
        publishProgress,
        items);
    if (staleRequest())
        return;
    if (!routeResolved)
        items.clear();
    if (items.empty()) {
        FinishFailedRequest(
            state, generation, screenerId, requestToken, source);
        return;
    }

    // A list refresh owns publication. Stop unfinished visible-page trend work,
    // while retaining any trend data that already reached the current snapshot.
    CancelRequestToken(state.requests.screenerTrendRequest);
    const auto current = state.marketData.LoadScreenerSnapshot();
    if (current && current->generation == generation &&
        current->screenerId == screenerId && current->items) {
        MergeScreenerTrendData(
            *current->items, 0, current->items->size(), items, false);
    }
    if (staleRequest())
        return;

    const auto fetchedAt = std::chrono::system_clock::now();
    if (state.screenerCache.Store(cacheId, items, fetchedAt))
        StorePersistentRows(source, cacheId, fetchedAt, items);
    state.requests.overviewTrendPageKey.store(0, std::memory_order_release);
    if (PublishIfCurrent(state,
                         generation,
                         screenerId,
                         requestToken,
                         ScreenerLoadState::Ready,
                         ShareItems(std::move(items)))) {
        ScheduleScreenerRefresh(state, screenerId, source);
    }
}

bool PublishTrendPhase(AppState& state,
                       const std::vector<ScreenerItem>& enrichedItems,
                       std::size_t firstIndex,
                       std::size_t count,
                       const std::string& screenerId,
                       const std::string& cacheId,
                       std::uint64_t generation,
                       const RequestToken& requestToken,
                       const RequestToken& trendToken,
                       ScreenerDataSource& source) {
    const auto staleTrend = [&] {
        return TokenCancelled(requestToken) || TokenCancelled(trendToken);
    };
    for (;;) {
        auto current = state.marketData.LoadScreenerSnapshot();
        if (!current || current->generation != generation ||
            current->screenerId != screenerId ||
            current->loadState != ScreenerLoadState::Ready || !current->items ||
            staleTrend()) {
            return false;
        }
        std::vector<ScreenerItem> mergedItems = *current->items;
        const std::size_t mergedTrendCount = MergeScreenerTrendData(
            enrichedItems, firstIndex, count, mergedItems, true);
        if (mergedTrendCount == 0)
            return true;
        ScreenerItemsSnapshot mergedSnapshot = ShareItems(std::move(mergedItems));

        ScreenerSnapshot desired;
        desired.generation = generation;
        desired.screenerId = screenerId;
        desired.loadState = ScreenerLoadState::Ready;
        desired.items = mergedSnapshot;
        if (state.marketData.CompareExchangeScreenerSnapshot(
                current, std::move(desired))) {
            const auto fetchedAt =
                state.screenerCache.UpdateTrends(cacheId, *mergedSnapshot);
            if (fetchedAt)
                StorePersistentRows(source, cacheId, *fetchedAt, *mergedSnapshot);
            RequestGuiRedraw();
            return true;
        }
    }
}

} // namespace

bool StartScreenerTrendFetch(AppState& state,
                             const std::string& screenerId,
                             std::size_t firstIndex,
                             std::size_t count,
                             ScreenerDataSource& source) {
    auto snapshot = state.marketData.LoadScreenerSnapshot();
    if (!snapshot || snapshot->screenerId != screenerId ||
        snapshot->loadState != ScreenerLoadState::Ready || !snapshot->HasRows() ||
        count == 0 || firstIndex >= snapshot->items->size()) {
        return false;
    }
    const std::size_t end = std::min(snapshot->items->size(), firstIndex + count);
    const std::size_t boundedCount = end - firstIndex;
    const bool trendMissing = std::any_of(
        snapshot->items->begin() + static_cast<std::ptrdiff_t>(firstIndex),
        snapshot->items->begin() + static_cast<std::ptrdiff_t>(end),
        [](const ScreenerItem& item) { return item.sparkline.size() < 2; });
    if (!trendMissing)
        return true;

    const std::uint64_t generation = snapshot->generation;
    const RequestToken requestToken =
        state.requests.screenerRequest.load(std::memory_order_acquire);
    if (TokenCancelled(requestToken) ||
        requestToken->Generation() != generation) {
        return false;
    }
    const RequestToken trendToken =
        BeginRequestToken(state.requests.screenerTrendRequest, generation);
    const std::vector<std::string> watchlistSnapshot =
        screenerId == "watchlist" ? state.config.watchlist
                                  : std::vector<std::string>{};
    const std::string cacheId = BuildScreenerCacheKey(screenerId, watchlistSnapshot);
    AppState* statePtr = &state;
    ScreenerDataSource* sourcePtr = &source;

    const bool submitted = SubmitLatestScreenerJob(
        [statePtr,
         snapshot = std::move(snapshot),
         screenerId,
         firstIndex,
         boundedCount,
         cacheId,
         generation,
         requestToken,
         trendToken,
         sourcePtr]() mutable {
            const auto staleTrend = [requestToken, trendToken] {
                return TokenCancelled(requestToken) || TokenCancelled(trendToken);
            };
            try {
                std::vector<ScreenerItem> enrichedItems = *snapshot->items;
                const auto publishTrendProgress =
                    [statePtr,
                     firstIndex,
                     boundedCount,
                     &screenerId,
                     &cacheId,
                     generation,
                     &requestToken,
                     &trendToken,
                     sourcePtr](const std::vector<ScreenerItem>& progressItems) {
                        return PublishTrendPhase(*statePtr,
                                                 progressItems,
                                                 firstIndex,
                                                 boundedCount,
                                                 screenerId,
                                                 cacheId,
                                                 generation,
                                                 requestToken,
                                                 trendToken,
                                                 *sourcePtr);
                    };
                if (!sourcePtr->EnrichSparklines(enrichedItems,
                                                 firstIndex,
                                                 boundedCount,
                                                 staleTrend,
                                                 publishTrendProgress) ||
                    staleTrend()) {
                    RetireRequestToken(
                        statePtr->requests.screenerTrendRequest, trendToken);
                    RequestGuiRedraw();
                    return;
                }
                if (!PublishTrendPhase(*statePtr,
                                       enrichedItems,
                                       firstIndex,
                                       boundedCount,
                                       screenerId,
                                       cacheId,
                                       generation,
                                       requestToken,
                                       trendToken,
                                       *sourcePtr)) {
                    RetireRequestToken(
                        statePtr->requests.screenerTrendRequest, trendToken);
                    RequestGuiRedraw();
                    return;
                }
                RetireRequestToken(
                    statePtr->requests.screenerTrendRequest, trendToken);
                RequestGuiRedraw();
            } catch (...) {
                ReportBackgroundFailure("screener trend refresh");
                const bool activeRequest = !staleTrend();
                RetireRequestToken(
                    statePtr->requests.screenerTrendRequest, trendToken);
                if (activeRequest) {
                    statePtr->requests.overviewTrendPageKey.store(
                        0, std::memory_order_release);
                    RequestGuiRedraw();
                }
            }
        });
    if (!submitted)
        RetireRequestToken(state.requests.screenerTrendRequest, trendToken);
    return submitted;
}

void StartScreenerFetch(AppState& state,
                        const std::string& screenerId,
                        ScreenerDataSource& source) {
    const auto previousSnapshot = state.marketData.LoadScreenerSnapshot();
    const std::uint64_t generation =
        state.requests.nextScreenerGeneration.fetch_add(
            1, std::memory_order_acq_rel) + 1;
    const RequestToken requestToken =
        BeginRequestToken(state.requests.screenerRequest, generation);
    CancelRequestToken(state.requests.screenerTrendRequest);
    state.requests.overviewTrendPageKey.store(0, std::memory_order_release);

    std::vector<std::string> watchlistSnapshot =
        screenerId == "watchlist" ? state.config.watchlist
                                  : std::vector<std::string>{};
    const std::string cacheId = BuildScreenerCacheKey(screenerId, watchlistSnapshot);
    const bool marketOpen = source.MarketOpen();
    ScreenerItemsSnapshot displaySnapshot;
    if (previousSnapshot && previousSnapshot->screenerId == screenerId &&
        previousSnapshot->HasRows()) {
        displaySnapshot = previousSnapshot->items;
    } else if (!marketOpen) {
        std::vector<ScreenerItem> cachedItems;
        const bool retainClosedMarketTrending =
            screenerId == "trending_now" && !marketOpen;
        const auto maximumDisplayAge = retainClosedMarketTrending
                                           ? kClosedMarketTrendingDisplayAge
                                           : std::chrono::minutes(10);
        if (LoadCachedRows(
                state, cacheId, maximumDisplayAge, source, cachedItems)) {
            displaySnapshot = ShareItems(std::move(cachedItems));
        }
    }

    ScreenerSnapshot initial;
    initial.generation = generation;
    initial.screenerId = screenerId;
    initial.loadState = displaySnapshot ? ScreenerLoadState::Refreshing
                                        : ScreenerLoadState::Loading;
    initial.items = displaySnapshot;
    state.marketData.PublishScreenerSnapshot(std::move(initial));
    ScheduleScreenerRefresh(state, screenerId, source);
    RequestGuiRedraw();

    if (screenerId.empty() ||
        (screenerId == "watchlist" && state.config.watchlist.empty())) {
        (void)PublishIfCurrent(state,
                               generation,
                               screenerId,
                               requestToken,
                               ScreenerLoadState::Ready,
                               {});
        state.requests.screenerRefreshAfterEpochSeconds.store(
            0, std::memory_order_release);
        return;
    }

    AppState* statePtr = &state;
    ScreenerDataSource* sourcePtr = &source;
    const bool submitted = SubmitLatestScreenerJob(
        [statePtr,
         screenerId,
         watchlistSnapshot = std::move(watchlistSnapshot),
         cacheId,
         generation,
         requestToken,
         sourcePtr]() {
            try {
                FetchScreenerDataBackground(*statePtr,
                                            screenerId,
                                            watchlistSnapshot,
                                            cacheId,
                                            generation,
                                            requestToken,
                                            *sourcePtr);
            } catch (...) {
                ReportBackgroundFailure("screener data refresh");
                FinishFailedRequest(*statePtr,
                                    generation,
                                    screenerId,
                                    requestToken,
                                    *sourcePtr);
            }
        });
    if (!submitted)
        FinishFailedRequest(state, generation, screenerId, requestToken, source);
}

void ResetScreenerFetch(AppState& state) {
    CancelRequestToken(state.requests.screenerRequest);
    CancelRequestToken(state.requests.screenerTrendRequest);
    const std::uint64_t generation =
        state.requests.nextScreenerGeneration.fetch_add(
            1, std::memory_order_acq_rel) + 1;
    state.requests.overviewTrendPageKey.store(0, std::memory_order_release);
    state.requests.screenerRefreshAfterEpochSeconds.store(
        0, std::memory_order_release);

    ScreenerSnapshot idle;
    idle.generation = generation;
    state.marketData.PublishScreenerSnapshot(std::move(idle));
    RequestGuiRedraw();
}

} // namespace squarestar::application
