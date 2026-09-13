#include "modules/stock_request_runtime.hpp"

#include "application/app_state.hpp"
#include "application/debug_diagnostics.hpp"
#include "application/main_loop_signal.hpp"
#include "application/price_alert_policy.hpp"
#include "application/runtime_state.hpp"
#include "application/stock_data_merge.hpp"
#include "application/stock_request_intent.hpp"
#include "application/stock_tab_policy.hpp"
#include "domain/number_format.hpp"
#include "modules/app_services.hpp"
#include "modules/core.hpp"
#include "modules/market_data.hpp"
#include "services/config_save_queue.hpp"
#include "application/stock_request_tracker.hpp"
#include "domain/stock_data.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <future>
#include <string>
#include <utility>

namespace squarestar::shell {

using squarestar::application::ApplicationRuntime;
using squarestar::application::AppState;
using squarestar::application::AppUiMode;
using squarestar::application::ConfiguredPriceAlert;
using squarestar::application::HasResolvedCompanyName;
using squarestar::application::MergeStockFetchPatch;
using squarestar::application::RequestGuiRedraw;
using squarestar::application::StockContext;
using squarestar::application::StockFetchAll;
using squarestar::application::StockFetchMetrics;
using squarestar::application::StockFetchProfile;
using squarestar::application::StockRequestChannel;
using squarestar::application::UserFeedbackType;
using squarestar::format::PriceChangedAtDisplayPrecision;
using squarestar::market::FetchKind;
using squarestar::market::StockData;
using squarestar::market::StockFetchResult;

static bool FutureReady(const std::future<StockFetchResult>& request) {
    return request.valid() &&
           request.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}

static void PumpCompletedDetailRequest(AppState& state, StockContext& ctx) {
    if (!FutureReady(ctx.requests.pendingDetailsRequest))
        return;

    const uint32_t attemptedDetailMask = ctx.requests.requestedDetailMask & StockFetchAll;
    ctx.requests.requestedDetailMask &= ~attemptedDetailMask;
    StockFetchResult detailsResult;
    try {
        detailsResult = ctx.requests.pendingDetailsRequest.get();
    } catch (...) {
        squarestar::application::ReportBackgroundFailure(
            "stock details completion");
        detailsResult.success = false;
    }
    StockData details = std::move(detailsResult.marketData);
    const uint32_t resolvedAttemptMask = details.resolvedDetailMask & attemptedDetailMask;
    const uint32_t unresolvedAttemptMask = attemptedDetailMask & ~resolvedAttemptMask;

    if (detailsResult.success) {
        ctx.PublishRawDataAtRevision(
            MergeStockFetchPatch(ctx.RawData(),
                                 std::move(details),
                                 FetchKind::Details,
                                 ctx.navigation.displayedTimeRangeIndex),
            ctx.marketData.dataRevision + 1);
        if (HasResolvedCompanyName(ctx.RawData().companyName)) {
            const auto existingName =
                state.config.searchHistoryNames.find(ctx.navigation.ticker);
            if (existingName == state.config.searchHistoryNames.end() ||
                existingName->second != ctx.RawData().companyName) {
                state.config.searchHistoryNames[ctx.navigation.ticker] =
                    ctx.RawData().companyName;
                squarestar::config::RequestConfigSave();
            }
        }
    }

    ctx.requests.detailRetryMask &= ~resolvedAttemptMask;
    if (unresolvedAttemptMask != 0) {
        ctx.requests.detailRetryMask |= unresolvedAttemptMask;
        ctx.requests.detailRetryAttempts =
            std::min(ctx.requests.detailRetryAttempts + 1, 8);
        ctx.requests.nextDetailRetryTime =
            std::time(nullptr) +
            squarestar::application::DetailRetryDelaySeconds(
                ctx.requests.detailRetryAttempts, detailsResult.rateLimited);
    } else if (ctx.requests.detailRetryMask == 0) {
        ctx.requests.detailRetryAttempts = 0;
        ctx.requests.nextDetailRetryTime = 0;
    }

    if ((attemptedDetailMask & StockFetchMetrics) != 0 &&
        (ctx.RawData().resolvedDetailMask & StockFetchProfile) == 0) {
        TriggerDetailsFetch(state, ctx, StockFetchProfile);
    }
    RequestGuiRedraw();
}

static void PumpDetailRetry(AppState& state, StockContext& ctx) {
    const std::time_t now = std::time(nullptr);
    if (ctx.RawData().success && ctx.requests.detailRetryMask != 0 &&
        ctx.requests.nextDetailRetryTime > 0 && now >= ctx.requests.nextDetailRetryTime &&
        !ctx.requests.pendingDetailsRequest.valid()) {
        TriggerDetailsFetch(state, ctx, ctx.requests.detailRetryMask);
    }
}

static bool MainStockRequestReady(const StockContext& ctx) {
    return FutureReady(ctx.requests.pendingRequest);
}

static void PumpStockLoadingSound(const AppState& state,
                                  StockContext& ctx,
                                  bool requestReady) {
    if (ctx.requests.isLoading && !ctx.render.soundPanPlayed &&
        !ctx.requests.isBackgroundFetching && !requestReady &&
        IsStockInteractionSurfaceAudible(ctx)) {
        PlayUISound("loading.wav", state);
        ctx.render.soundPanPlayed = true;
    }
}

struct StockRequestCompletion {
    StockFetchResult result;
    FetchKind kind = FetchKind::Full;
    StockRequestChannel channel = StockRequestChannel::Refresh;
    uint64_t generation = 0;
    bool generationWasDesired = false;
    bool wasBackground = false;
    bool hadPrevious = false;
    int rangeIndex = 0;
    double previousPrice = 0.0;
    double previousDisplayPrice = 0.0;
};

static StockRequestCompletion TakeStockRequestCompletion(StockContext& ctx) {
    RequestGuiRedraw();
    StockRequestCompletion completion;
    try {
        completion.result = ctx.requests.pendingRequest.get();
    } catch (...) {
        squarestar::application::ReportBackgroundFailure(
            "stock request completion");
        completion.result.success = false;
        completion.result.errorMessage = "The stock-data request ended unexpectedly";
    }
    completion.kind = ctx.requests.pendingFetchKind;
    completion.channel = ctx.requests.pendingChannel;
    completion.generation = ctx.requests.pendingGeneration;
    completion.generationWasDesired =
        ctx.requests.tracker.CanApply(completion.channel, completion.generation);
    (void)ctx.requests.tracker.Complete(
        completion.channel, completion.generation, completion.result.success);
    completion.wasBackground = ctx.requests.isBackgroundFetching;
    completion.hadPrevious = ctx.RawData().success;
    completion.rangeIndex = completion.result.resolvedTimeRangeIndex >= 0
                                ? completion.result.resolvedTimeRangeIndex
                                : ctx.requests.pendingFetchRangeIndex;
    completion.previousPrice = ctx.RawData().currentPrice;
    completion.previousDisplayPrice = completion.previousPrice;
    return completion;
}

static void FinishStockRequest(AppState& state, StockContext& ctx) {
    ctx.requests.isLoading = false;
    ctx.requests.isBackgroundFetching = false;
    PumpStockRequestQueue(state, ctx);
}

static bool CompletionStillDesired(const StockContext& ctx,
                                   const StockRequestCompletion& completion) {
    if (!completion.generationWasDesired)
        return false;
    if (completion.kind == FetchKind::Chart &&
        completion.rangeIndex != ctx.navigation.selectedTimeRangeIndex) {
        return false;
    }
    return true;
}

static std::string InitialStockFailureMessage(
    const StockContext& ctx,
    const StockRequestCompletion& completion) {
    const std::string& reason = completion.result.errorMessage;
    if (reason == "Ticker not found" ||
        reason == "Stock unavailable / unlisted ticker") {
        return std::string(ctx.navigation.ticker) +
               " could not be found. Check the ticker and try again.";
    }
    if (reason == "This symbol or market is not supported") {
        return std::string(ctx.navigation.ticker) +
               " is not a supported US market symbol.";
    }
    if (!reason.empty())
        return reason;
    return std::string("No active public quote was returned for ") +
           ctx.navigation.ticker + ".";
}

static void HandleFailedStockCompletion(AppState& state,
                                        StockContext& ctx,
                                        const StockRequestCompletion& completion) {
    ctx.requests.isLoading = false;
    ctx.requests.isBackgroundFetching = false;
    if (completion.result.rateLimited && completion.wasBackground &&
        (completion.kind == FetchKind::LiveQuote ||
         completion.kind == FetchKind::AlertQuote)) {
        ctx.requests.autoRefreshTimer = -60.0f;
    }
    ctx.requests.tracker.CancelDesired(completion.channel);
    if (completion.kind == FetchKind::Chart &&
        completion.rangeIndex != ctx.navigation.displayedTimeRangeIndex) {
        ctx.navigation.selectedTimeRangeIndex = ctx.navigation.displayedTimeRangeIndex;
        ctx.render.lockedAxisRangeIndex = -1;
        PublishUserFeedback(
            state,
            UserFeedbackType::Warning,
            "Chart unavailable",
            "The selected range could not be loaded; the previous chart was kept.");
    }
    if (!completion.wasBackground && !completion.hadPrevious) {
        squarestar::application::RestoreNavigationAfterFailedStockOpen(state, ctx);
        PublishUserFeedback(
            state,
            UserFeedbackType::Warning,
            "Stock unavailable",
            InitialStockFailureMessage(ctx, completion));
        return;
    } else if (!completion.wasBackground && completion.kind == FetchKind::Full &&
               completion.hadPrevious) {
        PublishUserFeedback(state,
                            UserFeedbackType::Error,
                            "Refresh failed",
                            completion.result.errorMessage.empty()
                                ? "Previous stock data was kept."
                                : completion.result.errorMessage);
    }
    PumpStockRequestQueue(state, ctx);
}

static bool StockPayloadChanged(const StockContext& ctx,
                                const StockRequestCompletion& completion,
                                const StockData& data) {
    if (completion.kind == FetchKind::LiveQuote) {
        return !completion.hadPrevious ||
               std::abs(data.currentPrice - ctx.RawData().currentPrice) > 1e-12 ||
               (data.previousClose > 0.0 &&
                std::abs(data.previousClose - ctx.RawData().previousClose) > 1e-12);
    }
    if (completion.kind == FetchKind::Chart) {
        return data.timestamps.size() != ctx.RawData().timestamps.size() ||
               data.closes.size() != ctx.RawData().closes.size() ||
               std::abs(data.chartPreviousClose - ctx.RawData().chartPreviousClose) > 1e-12 ||
               (!data.timestamps.empty() &&
                (ctx.RawData().timestamps.empty() ||
                 std::abs(data.timestamps.back() - ctx.RawData().timestamps.back()) > 0.0 ||
                 std::abs(data.closes.back() - ctx.RawData().closes.back()) > 1e-12));
    }
    return true;
}

struct StockPublicationResult {
    bool changed = false;
    bool switchedChartRange = false;
    bool displayedPriceChanged = false;
};

static StockPublicationResult PublishStockCompletion(
    AppState& state,
    StockContext& ctx,
    StockRequestCompletion& completion) {
    StockData newData = std::move(completion.result.marketData);
    StockPublicationResult publication;
    publication.changed = StockPayloadChanged(ctx, completion, newData);
    publication.switchedChartRange =
        completion.kind == FetchKind::Chart &&
        completion.rangeIndex != ctx.navigation.displayedTimeRangeIndex;

    const bool advanceRawRevision = completion.kind == FetchKind::Full ||
                                    completion.kind == FetchKind::Chart ||
                                    publication.changed;
    const uint64_t nextRawRevision =
        ctx.marketData.dataRevision + (advanceRawRevision ? 1u : 0u);
    if (completion.kind == FetchKind::LiveQuote ||
        completion.kind == FetchKind::AlertQuote) {
        (void)ctx.ApplyQuotePatchAtRevision(
            newData, completion.rangeIndex, nextRawRevision);
    } else {
        StockData merged = MergeStockFetchPatch(
            ctx.RawData(), std::move(newData), completion.kind, completion.rangeIndex);
        merged.currency = "USD";
        ctx.PublishRawDataAtRevision(std::move(merged), nextRawRevision);
    }

    publication.displayedPriceChanged =
        completion.hadPrevious &&
        PriceChangedAtDisplayPrecision(
            completion.previousPrice,
            ctx.RawData().currentPrice);
    if (completion.kind == FetchKind::Full)
        TriggerDetailsFetch(state, ctx, StockFetchMetrics | StockFetchProfile);

    ctx.requests.isLoading = false;
    ctx.requests.isBackgroundFetching = false;
    if (completion.kind == FetchKind::Full || completion.kind == FetchKind::Chart)
        ctx.navigation.displayedTimeRangeIndex = completion.rangeIndex;
    if (completion.kind == FetchKind::Full &&
        ctx.navigation.selectedTimeRangeIndex != completion.rangeIndex) {
        // Full-load progressive fallback is authoritative for both the normal
        // terminal and LiteGUI because both surfaces share this context.
        ctx.navigation.selectedTimeRangeIndex = completion.rangeIndex;
        ctx.requests.pendingFetchRangeIndex = completion.rangeIndex;
        ctx.render.lockedAxisRangeIndex = -1;
    }
    if (completion.kind == FetchKind::Full || completion.kind == FetchKind::Chart ||
        (publication.changed && completion.kind == FetchKind::LiveQuote &&
         completion.rangeIndex == 0)) {
        ctx.marketData.needsPlotDataUpdate = true;
    }
    if ((!completion.wasBackground && completion.kind == FetchKind::Full) ||
        publication.switchedChartRange) {
        ctx.render.dataJustLoaded = true;
    }
    if (!completion.hadPrevious && completion.kind == FetchKind::Full) {
        ctx.navigation.failureReturnTicker.clear();
        ctx.navigation.failureReturnSidebarTab = squarestar::application::SidebarTab::Stock;
    }
    return publication;
}

static void PersistCompletedCompanyName(AppState& state,
                                        const StockContext& ctx,
                                        FetchKind kind) {
    if (kind != FetchKind::Full ||
        !HasResolvedCompanyName(ctx.RawData().companyName)) {
        return;
    }
    const auto existingName = state.config.searchHistoryNames.find(ctx.navigation.ticker);
    if (existingName == state.config.searchHistoryNames.end() ||
        existingName->second != ctx.RawData().companyName) {
        state.config.searchHistoryNames[ctx.navigation.ticker] = ctx.RawData().companyName;
        squarestar::config::RequestConfigSave();
    }
}

static void PublishBackgroundStockCompletion(
    AppState& state,
    StockContext& ctx,
    const StockRequestCompletion& completion,
    const StockPublicationResult& publication,
    bool priceAlertReached,
    bool windowSuspended) {
    if (!completion.wasBackground || completion.kind != FetchKind::LiveQuote ||
        !completion.hadPrevious ||
        (!publication.displayedPriceChanged && !priceAlertReached)) {
        return;
    }

    const bool backgroundNotificationMode = UseBackgroundNotificationBlock();
    const bool watchedWhileHidden = ShouldWatchHiddenStockContext(state, ctx);
    const bool liteGuiNotificationMode =
        ApplicationRuntime().CurrentUiMode() == AppUiMode::LiteGui;
    const bool foregroundMoveDestination =
        UseForegroundNotificationBlocks() && ctx.navigation.open;
    const bool backgroundMoveDestination =
        backgroundNotificationMode &&
        ((liteGuiNotificationMode && ctx.navigation.open) || watchedWhileHidden);
    const bool marketMoveQualified =
        publication.displayedPriceChanged && !priceAlertReached &&
        (foregroundMoveDestination || backgroundMoveDestination) &&
        ShouldPublishMarketMoveNotification(
            state, ctx, completion.previousPrice, ctx.RawData().currentPrice);

    if (marketMoveQualified && (!windowSuspended || watchedWhileHidden)) {
        if (ctx.RawData().currentPrice > completion.previousPrice)
            PlayUISound("gain.wav", state);
        else if (ctx.RawData().currentPrice < completion.previousPrice)
            PlayUISound("loss.wav", state);
    }
    if (foregroundMoveDestination && marketMoveQualified) {
        PublishForegroundStockMove(
            state,
            ctx.navigation.ticker,
            completion.previousDisplayPrice,
            ctx.RawData().currentPrice,
            "USD");
    }
    if (backgroundNotificationMode &&
        ((liteGuiNotificationMode && ctx.navigation.open) || watchedWhileHidden ||
         priceAlertReached) &&
        (marketMoveQualified || priceAlertReached)) {
        const auto alert = ConfiguredPriceAlert(state.alerts, ctx);
        QueueTrayPriceMove(
            state,
            ctx.navigation.ticker,
            completion.previousDisplayPrice,
            ctx.RawData().currentPrice,
            priceAlertReached,
            alert.value_or(0.0),
            "USD");
    }
}

static void RunStockCompletionSideEffects(
    AppState& state,
    StockContext& ctx,
    const StockRequestCompletion& completion,
    const StockPublicationResult& publication,
    bool windowSuspended) {
    if (!ctx.RawData().success)
        return;
    const bool wasPriceAlertTriggered = ctx.alerts.priceAlertTriggered;
    EvaluateStockPriceAlert(state, ctx);
    const bool priceAlertReached =
        !wasPriceAlertTriggered && ctx.alerts.priceAlertTriggered;
    PersistCompletedCompanyName(state, ctx, completion.kind);

    PublishBackgroundStockCompletion(
        state, ctx, completion, publication, priceAlertReached, windowSuspended);
}

static void ProcessCompletedStockRequest(AppState& state,
                                         StockContext& ctx,
                                         bool windowSuspended) {
    StockRequestCompletion completion = TakeStockRequestCompletion(ctx);
    if (!CompletionStillDesired(ctx, completion)) {
        FinishStockRequest(state, ctx);
        return;
    }
    if (!completion.result.success) {
        HandleFailedStockCompletion(state, ctx, completion);
        return;
    }
    const StockPublicationResult publication =
        PublishStockCompletion(state, ctx, completion);
    RunStockCompletionSideEffects(
        state, ctx, completion, publication, windowSuspended);
    PumpStockRequestQueue(state, ctx);
}

static void RetireClosedLiteContexts(AppState& state) {
    if (ApplicationRuntime().CurrentUiMode() != AppUiMode::LiteGui)
        return;

    bool hasOpenStock = false;
    bool retiredAny = false;
    for (auto it = state.marketData.activeContexts.begin();
         it != state.marketData.activeContexts.end();) {
        if (!*it) {
            it = state.marketData.activeContexts.erase(it);
            continue;
        }
        if ((*it)->navigation.open) {
            hasOpenStock = true;
            ++it;
            continue;
        }
        SilencePriceAlertForClosedTab(state, **it);
        state.marketData.retiredLiteContexts.push_back(std::move(*it));
        it = state.marketData.activeContexts.erase(it);
        retiredAny = true;
    }
    if (retiredAny && !hasOpenStock)
        state.navigation.liteSearch.focusRequested = true;
}

void PumpCompletedStockRequests(AppState& state, bool windowSuspended) {
    for (auto& candidate : state.marketData.activeContexts) {
        if (!candidate)
            continue;
        StockContext& ctx = *candidate;
        PumpCompletedDetailRequest(state, ctx);
        PumpDetailRetry(state, ctx);
        const bool requestReady = MainStockRequestReady(ctx);
        PumpStockLoadingSound(state, ctx, requestReady);
        if (requestReady)
            ProcessCompletedStockRequest(state, ctx, windowSuspended);
    }
    RetireClosedLiteContexts(state);
    FlushTrayPriceMoves();
}


} // namespace squarestar::shell
