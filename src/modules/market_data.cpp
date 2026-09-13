#include "modules/market_data.hpp"
#include "modules/core.hpp"

#include <chrono>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>

#include "services/config_save_queue.hpp"
#include "application/app_limits.hpp"
#include "application/screener_controller.hpp"
#include "application/stock_request_tracker.hpp"
#include "application/stock_request_intent.hpp"
#include "application/stock_request_state.hpp"
#include "domain/chart_ranges.hpp"
#include "domain/market_symbol.hpp"
#include "domain/screener_routes.hpp"
#include "domain/stock_data.hpp"
#include "services/api_key_store.hpp"
#include "services/network_runtime.hpp"
#include "services/stock_data_service.hpp"
namespace squarestar::shell {

using squarestar::application::ApplicationRuntime;
using squarestar::market::MarketSymbol;
using squarestar::market::TIME_RANGES;
using squarestar::market::kScreenerRoutes;
using squarestar::application::RequestGuiRedraw;
using squarestar::application::AppState;
using squarestar::application::FetchStartResult;
using squarestar::application::StockRequestChannel;
using squarestar::application::PumpQueuedStockRequest;
using squarestar::application::RequestManualStockRefreshIntent;
using squarestar::application::UserFeedbackType;
using squarestar::application::StockContext;
using squarestar::application::IsLightGuiTheme;
using squarestar::application::StockFetchMetrics;
using squarestar::application::StockFetchProfile;
using squarestar::application::StockFetchNews;
using squarestar::application::StockFetchAll;
using squarestar::application::HasAnyMarketMetricData;
using squarestar::application::MergeStockFetchPatch;
using squarestar::secrets::HasFinnhubApiKey;
using squarestar::market::FetchKind;
using squarestar::market::StockData;
using squarestar::marketdata::FetchStockData;
using squarestar::text::UppercaseInPlace;

void RenderLoadingSpinner(const AppState& state, const char* label, float radius, int thickness, const ImU32& color) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems)
        return;
    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImVec2 size((radius)*2, (radius + style.FramePadding.y) * 2);
    ImGui::ItemSize(size, style.FramePadding.y);
    if (!ImGui::ItemAdd(ImRect(pos, ImVec2(pos.x + size.x, pos.y + size.y)), 0))
        return;
    window->DrawList->PathClear();
    int num_segments = 30;
    float start =
        state.UiAnimationsEnabled() ? (float)g.Time * 10.0f : 0.0f;
    ImVec2 center = ImVec2(pos.x + radius, pos.y + radius + style.FramePadding.y);
    const float segmentStep = IM_PI * 2.0f / static_cast<float>(num_segments);
    for (int i = 0; i < num_segments; i++) {
        const float a = start + static_cast<float>(i) * segmentStep;
        float offset_x = cosf(a) * radius;
        float offset_y = sinf(a) * radius;
        window->DrawList->PathLineTo(ImVec2(center.x + offset_x, center.y + offset_y));
    }
    window->DrawList->PathStroke(color, static_cast<float>(thickness));
    if (label) {
        ImGui::SameLine();
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (radius - ImGui::GetFontSize() / 2.0f));
        ImGui::Text("%s", label);
    }
}

FetchStartResult TriggerFetch(AppState& state,
                              StockContext& ctx,
                              bool forceUpdate,
                              bool background,
                              FetchKind kind) {
    const std::time_t now = std::time(nullptr);
    // A ready future still owns an unmerged result until the completion pump
    // consumes it. Replacing it here would silently discard that result.
    if (ctx.requests.pendingRequest.valid())
        return FetchStartResult::Busy;
    if (!forceUpdate && background && kind == FetchKind::Full && (now - ctx.requests.lastFetchTime < 900) &&
        ctx.RawData().success) {
        return FetchStartResult::SkippedFresh;
    }
    if (!background) {
        ctx.requests.isLoading = true;
        ctx.render.animProgress = 0.0f;
        ctx.render.soundPanPlayed = false;
    }
    ctx.requests.isBackgroundFetching = background;
    ctx.requests.pendingFetchKind = kind;
    const StockRequestChannel channel =
        kind == FetchKind::Chart
            ? StockRequestChannel::Chart
        : (kind == FetchKind::LiveQuote || kind == FetchKind::AlertQuote)
            ? StockRequestChannel::Quote
            : StockRequestChannel::Refresh;
    const uint64_t generation = ctx.requests.tracker.EnsureDesired(channel);
    if (!ctx.requests.tracker.Begin(channel, generation) &&
        ctx.requests.tracker.State(channel).inFlight != generation) {
        ctx.requests.isLoading = false;
        ctx.requests.isBackgroundFetching = false;
        return FetchStartResult::Busy;
    }
    ctx.requests.pendingChannel = channel;
    ctx.requests.pendingGeneration = generation;
    if (kind == FetchKind::Full) {
        ctx.requests.autoRefreshTimer = 0.0f;
        ctx.requests.chartRefreshTimer = 0.0f;
        ctx.requests.requestedDetailMask = 0;
        ctx.requests.detailRetryMask = 0;
        ctx.requests.detailRetryAttempts = 0;
        ctx.requests.nextDetailRetryTime = 0;
        ctx.requests.lastFetchTime = now;
    } else if (kind == FetchKind::Chart) {
        ctx.requests.chartRefreshTimer = 0.0f;
        ctx.requests.autoRefreshTimer = 0.0f;
    } else if (kind == FetchKind::LiveQuote || kind == FetchKind::AlertQuote) {
        ctx.requests.autoRefreshTimer = 0.0f;
    }
    const std::string ticker = ctx.navigation.ticker;
    const int rangeIndex = ctx.navigation.selectedTimeRangeIndex;
    ctx.requests.pendingFetchRangeIndex = rangeIndex;
    const bool tolerateOpeningNoChart = state.config.graphTimeZone == 1;
    ExecutorPriority requestPriority = ExecutorPriority::Normal;
    if (!background || kind == FetchKind::Chart)
        requestPriority = ExecutorPriority::High;
    auto fetchWork = [ticker,
                      rangeIndex,
                      background,
                      tolerateOpeningNoChart,
                      kind]() {
        return FetchStockData(ticker,
                              rangeIndex,
                              background,
                              tolerateOpeningNoChart,
                              0,
                              kind);
    };
    if (kind == FetchKind::AlertQuote) {
        ctx.requests.pendingRequest =
            squarestar::marketdata::QueueAlertQuoteRequest(ticker);
    } else if (kind == FetchKind::LiveQuote) {
        const QuoteRequestPurpose purpose =
            ctx.navigation.refreshSurfaceVisible
                ? QuoteRequestPurpose::Foreground
                : QuoteRequestPurpose::BackgroundRefresh;
        const std::chrono::milliseconds maximumAge =
            purpose == QuoteRequestPurpose::Foreground
                ? std::chrono::seconds(8)
                : std::chrono::seconds(9);
        ctx.requests.pendingRequest = QueueStockQuoteTask(
            ticker, purpose, maximumAge, std::move(fetchWork));
    } else {
        ctx.requests.pendingRequest =
            QueueStockTask(std::move(fetchWork), requestPriority);
    }
    return FetchStartResult::Started;
}
void TriggerDetailsFetch(AppState& state,
                                StockContext& ctx,
                                uint32_t requestedMask) {
    const std::optional<MarketSymbol> symbol = MarketSymbol::Parse(ctx.navigation.ticker);
    const bool yahooOnlyInstrument = symbol && symbol->IsYahooFutures();
    if (!HasFinnhubApiKey() && !yahooOnlyInstrument)
        return;
    requestedMask &= StockFetchAll;
    if (yahooOnlyInstrument) {
        // Futures do not have company-profile/company-news payloads. Mark those
        // surfaces resolved immediately so the UI does not retry unsupported
        // Finnhub endpoints, while still allowing Yahoo quote metrics to load.
        const uint32_t unsupportedMask = requestedMask & (StockFetchProfile | StockFetchNews);
        StockData resolved = ctx.CopyRawData();
        resolved.resolvedDetailMask |= unsupportedMask;
        ctx.PublishRawData(std::move(resolved));
        requestedMask &= ~unsupportedMask;
    }
    requestedMask &= ~ctx.RawData().resolvedDetailMask;
    requestedMask &= ~ctx.requests.requestedDetailMask;
    if (requestedMask == 0)
        return;

    // Optional provider calls occasionally time out or are rate-limited.
    // Failed bits are allowed to retry after a short bounded backoff instead
    // of becoming permanently stuck behind requestedDetailMask and showing N/A
    // for the lifetime of the tab.
    const std::time_t now = std::time(nullptr);
    if (ctx.requests.nextDetailRetryTime > now)
        requestedMask &= ~ctx.requests.detailRetryMask;
    if (requestedMask == 0)
        return;

    // Keep numeric metrics in the first detail phase and chain Profile after
    // completion. This keeps optional profile traffic from delaying the
    // metrics/Yahoo fallback on the shared HTTP worker pool.
    const bool needsMetrics = (requestedMask & StockFetchMetrics) != 0;
    const bool needsProfile = (requestedMask & StockFetchProfile) != 0;
    if (needsMetrics && needsProfile && !HasAnyMarketMetricData(ctx.RawData()))
        requestedMask = StockFetchMetrics;

    if (ctx.requests.pendingDetailsRequest.valid())
        return;

    ctx.requests.requestedDetailMask |= requestedMask;
    const std::string ticker = ctx.navigation.ticker;
    const int rangeIndex = ctx.navigation.selectedTimeRangeIndex;
    const bool tolerateOpeningNoChart = state.config.graphTimeZone == 1;
    ctx.requests.pendingDetailsRequest = QueueStockTask(
        [ticker,
         rangeIndex,
         tolerateOpeningNoChart,
         requestedMask]() {
            return FetchStockData(ticker,
                                          rangeIndex,
                                          true,
                                          tolerateOpeningNoChart,
                                          requestedMask,
                                          FetchKind::Details);
        },
        ExecutorPriority::Normal);
}
bool StockRequestIdle(const StockContext& ctx) {
    return !ctx.requests.pendingRequest.valid();
}
void ReconcilePriceAlertContexts(AppState& state) {
    std::uint64_t topologySignature = state.alerts.ThresholdRevision();
    const auto mixTopology = [&](std::uint64_t value) {
        topologySignature ^= value + 0x9e3779b97f4a7c15ULL +
                             (topologySignature << 6U) +
                             (topologySignature >> 2U);
    };
    mixTopology(state.marketData.activeContexts.size());
    for (const auto& context : state.marketData.activeContexts) {
        mixTopology(static_cast<std::uint64_t>(
            reinterpret_cast<std::uintptr_t>(context.get())));
        if (context)
            mixTopology(std::hash<std::string_view>{}(
                context->navigation.ticker));
    }
    if (state.requests.priceAlertMonitorTopologyInitialized &&
        state.requests.priceAlertMonitorTopologySignature == topologySignature)
        return;

    std::unordered_map<std::string_view, StockContext*> activeOwners;
    activeOwners.reserve(state.marketData.activeContexts.size());
    for (const auto& context : state.marketData.activeContexts) {
        if (context)
            activeOwners.insert_or_assign(
                context->navigation.ticker, context.get());
    }
    state.alerts.RemoveMonitorsIf([&](const auto& monitor) {
        if (!monitor)
            return true;
        const bool configured = state.alerts.HasThreshold(monitor->navigation.ticker);
        const auto owner = activeOwners.find(monitor->navigation.ticker);
        StockContext* activeOwner =
            owner == activeOwners.end() ? nullptr : owner->second;
        const bool activeGuiOwnsAlert = activeOwner != nullptr;
        if (activeGuiOwnsAlert) {
            activeOwner->alerts.priceAlertTriggered = monitor->alerts.priceAlertTriggered;
            activeOwner->alerts.priceAlertSoundPlaysRemaining =
                monitor->alerts.priceAlertSoundPlaysRemaining;
            activeOwner->alerts.nextPriceAlertSoundAt =
                monitor->alerts.nextPriceAlertSoundAt;
            if (monitor->RawData().success) {
                StockData quotePatch = monitor->CopyRawData();
                (void)activeOwner->ApplyQuotePatchAtRevision(
                    quotePatch,
                    activeOwner->navigation.selectedTimeRangeIndex,
                    activeOwner->marketData.dataRevision + 1);
                activeOwner->marketData.needsPlotDataUpdate = true;
            }
        }
        return !configured || activeGuiOwnsAlert;
    });

    std::unordered_set<std::string_view> monitoredTickers;
    monitoredTickers.reserve(state.alerts.Monitors().size());
    for (const auto& monitor : state.alerts.Monitors()) {
        if (monitor)
            monitoredTickers.insert(monitor->navigation.ticker);
    }
    for (const auto& [ticker, threshold] : state.alerts.Thresholds()) {
        if (!std::isfinite(threshold) || threshold <= 0.0)
            continue;
        if (activeOwners.contains(ticker))
            continue;
        if (monitoredTickers.contains(ticker))
            continue;
        auto monitor = std::make_unique<StockContext>(ticker);
        monitor->navigation.justOpened = false;
        monitor->navigation.refreshSurfaceVisible = false;
        TriggerFetch(state, *monitor, true, true, FetchKind::AlertQuote);
        state.alerts.AddMonitor(std::move(monitor));
        monitoredTickers.insert(ticker);
    }
    state.requests.priceAlertMonitorTopologySignature = topologySignature;
    state.requests.priceAlertMonitorTopologyInitialized = true;
}
FetchStartResult RequestManualStockRefresh(AppState& state, StockContext& ctx) {
    return RequestManualStockRefreshIntent(
        ctx, [&] { return TriggerFetch(state, ctx, true, false, FetchKind::Full); }, [&] {
        PublishUserFeedback(state, UserFeedbackType::Information, "Refresh queued",
                            "The latest refresh will start when the current request finishes.");
    });
}
void PumpStockRequestQueue(AppState& state, StockContext& ctx) {
    PumpQueuedStockRequest(
        ctx,
        [&] { return TriggerFetch(state, ctx, true, true, FetchKind::Chart); },
        [&] { return TriggerFetch(state, ctx, true, !ctx.navigation.refreshSurfaceVisible,
                                  FetchKind::Full); });
}
void SelectChartRange(AppState& state, StockContext& ctx, int rangeIndex) {
    rangeIndex = std::clamp(rangeIndex, 0, (int)IM_ARRAYSIZE(TIME_RANGES) - 1);
    if (rangeIndex == ctx.navigation.selectedTimeRangeIndex)
        return;
    ctx.navigation.selectedTimeRangeIndex = rangeIndex;
    (void)ctx.requests.tracker.Request(StockRequestChannel::Chart);
    ctx.render.lockedAxisRangeIndex = -1;
    ctx.render.chartRevealProgress = 1.0f; // keep the old chart stable until the replacement arrives
    TriggerFetch(state, ctx, true, true, FetchKind::Chart);
    // Publish the pending range marker immediately even when the request must
    // queue behind another operation owned by this same stock context.
    RequestGuiRedraw();
}
void SelectStockViewRange(AppState& state, StockContext& ctx, int rangeIndex) {
    SelectChartRange(state, ctx, rangeIndex);
    if (ctx.navigation.upperTabIndex != 3)
        return;
    for (auto& candidate : state.marketData.activeContexts) {
        if (!candidate || candidate.get() == &ctx || !candidate->RawData().success)
            continue;
        if (std::find_if(ctx.navigation.comparisonSymbols.begin(),
                         ctx.navigation.comparisonSymbols.end(),
                         [&](const std::string& symbol) {
                             return symbol == candidate->navigation.ticker;
                         }) == ctx.navigation.comparisonSymbols.end())
            continue;
        SelectChartRange(state, *candidate, rangeIndex);
    }
}
void RenderStockRangeButtons(AppState& state, StockContext& ctx) {
    constexpr float gap = 4.0f;
    constexpr float buttonWidth = 48.0f;
    constexpr float buttonHeight = 30.0f;
    const float available = ImGui::GetContentRegionAvail().x;
    const float rowWidth =
        buttonWidth * IM_ARRAYSIZE(TIME_RANGES) + gap * (IM_ARRAYSIZE(TIME_RANGES) - 1);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, available - rowWidth));
    for (int i = 0; i < IM_ARRAYSIZE(TIME_RANGES); ++i) {
        ImGui::PushID(i);
        const bool selected = i == ctx.navigation.displayedTimeRangeIndex;
        const bool pending = i == ctx.navigation.selectedTimeRangeIndex &&
                             ctx.navigation.selectedTimeRangeIndex != ctx.navigation.displayedTimeRangeIndex;
        if (selected) {
            const ImVec4 activeBg = ThemeVec(state.config.theme.inverseBg, 0.94f);
            ImGui::PushStyleColor(ImGuiCol_Button, activeBg);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, activeBg);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, activeBg);
            ImGui::PushStyleColor(ImGuiCol_Text, ThemeVec(state.config.theme.inverseText));
        }
        const std::string buttonLabel =
            std::string(TIME_RANGES[i].label) + (pending ? " ·" : "");
        if (AnimatedButton(state, buttonLabel.c_str(),
                           ImVec2(buttonWidth, buttonHeight),
                           selected,
                           state.UiAnimationsEnabled())) {
            PlayUISound("click.wav", state);
            if (i != ctx.navigation.selectedTimeRangeIndex)
                SelectStockViewRange(state, ctx, i);
        }
        if (selected)
            ImGui::PopStyleColor(4);
        ImGui::PopID();
        if (i + 1 < IM_ARRAYSIZE(TIME_RANGES))
            ImGui::SameLine(0.0f, gap);
    }
}
static void ReportStockTabLimit(AppState& state) {
    PublishSilentFeedback(state,
                          UserFeedbackType::Warning,
                          "16-tab limit reached",
                          "Close a stock tab before opening another symbol.");
    PlayUISound("decline.wav", state);
}
static void RecordStockSearchHistory(AppState& state,
                                     const std::string& ticker) {
    if (state.config.saveSearchHistory) {
        auto it = std::find(
            state.config.searchHistory.begin(), state.config.searchHistory.end(), ticker);
        if (it != state.config.searchHistory.end())
            state.config.searchHistory.erase(it);
        state.config.searchHistory.insert(state.config.searchHistory.begin(), ticker);
        if (state.config.searchHistory.size() > 8)
            state.config.searchHistory.resize(8);
    }
    squarestar::config::RequestConfigSave();
}
StockOpenResult OpenStock(AppState& state, const std::string& rawTicker) {
    const std::optional<MarketSymbol> symbol = MarketSymbol::Parse(rawTicker);
    if (!symbol) {
        std::string rejectedTicker = rawTicker;
        UppercaseInPlace(rejectedTicker);
        PublishUserFeedback(
            state,
            UserFeedbackType::Warning,
            "Stock unavailable",
            rejectedTicker.empty()
                ? "Enter a US ticker symbol."
                : rejectedTicker + " is not a valid US ticker symbol.");
        return StockOpenResult::InvalidSymbol;
    }
    const std::string& ticker = symbol->Canonical();
    for (auto& ctx : state.marketData.activeContexts) {
        if (ctx->navigation.ticker == ticker) {
            RecordStockSearchHistory(state, ticker);
            state.navigation.activeSidebarTab = squarestar::application::SidebarTab::Stock;
            state.navigation.lastActiveTab = ticker;
            ctx->navigation.justOpened = true;
            ctx->render.openTransitionProgress = 0.0f;
            return StockOpenResult::SelectedExisting;
        }
    }
    if (state.marketData.activeContexts.size() >= squarestar::application::kMaxActiveStockTabs) {
        ReportStockTabLimit(state);
        return StockOpenResult::TabLimitReached;
    }

    const std::string previousTicker = state.navigation.lastActiveTab;
    const auto previousSidebarTab = state.navigation.activeSidebarTab;
    RecordStockSearchHistory(state, ticker);
    state.navigation.activeSidebarTab = squarestar::application::SidebarTab::Stock;
    state.navigation.lastActiveTab = ticker;
    auto newCtx = std::make_unique<StockContext>(ticker);
    newCtx->navigation.failureReturnTicker = previousTicker;
    newCtx->navigation.failureReturnSidebarTab = previousSidebarTab;
    newCtx->navigation.justOpened = true;
    newCtx->render.openTransitionProgress = 0.0f;
    TriggerFetch(state, *newCtx, true, false);
    state.marketData.activeContexts.push_back(std::move(newCtx));
    return StockOpenResult::OpenedNew;
}
void DrawFinancialRow(const char* label,
                      const std::string& value,
                      bool underline,
                      const ImVec4* valueColor) {
    ImVec4 labelColor = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    labelColor.w *= 0.70f;
    ImGui::PushStyleColor(ImGuiCol_Text, labelColor);
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    ImGui::SameLine();
    float avail = ImGui::GetContentRegionAvail().x;
    float textW = ImGui::CalcTextSize(value.c_str()).x;
    if (avail > textW)
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - textW));
    if (valueColor)
        ImGui::PushStyleColor(ImGuiCol_Text, *valueColor);
    ImGui::TextUnformatted(value.c_str());
    if (valueColor)
        ImGui::PopStyleColor();
    if (underline) {
        ImVec4 separatorColor = ImGui::GetStyleColorVec4(ImGuiCol_Separator);
        separatorColor.w *= 0.48f;
        ImGui::PushStyleColor(ImGuiCol_Separator, separatorColor);
        ImGui::Separator();
        ImGui::PopStyleColor();
    }
}
bool NeutralCheckbox(const char* label, bool& value, const AppState& state) {
    ImGui::PushID(label);

    constexpr float height = 26.0f;
    constexpr float width = 48.0f;
    constexpr float labelGap = 9.0f;

    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImVec2 labelSize = ImGui::CalcTextSize(label);

    const bool switchPressed = ImGui::InvisibleButton("##toggle", ImVec2(width, height));
    const bool switchHovered = ImGui::IsItemHovered();

    ImGui::SameLine(0.0f, labelGap);
    const ImVec2 labelHitPos = ImGui::GetCursorScreenPos();
    const bool labelPressed =
        ImGui::InvisibleButton("##toggle_label", ImVec2(std::max(1.0f, labelSize.x), height));
    const bool labelHovered = ImGui::IsItemHovered();

    const bool pressed = switchPressed || labelPressed;
    const bool hovered = switchHovered || labelHovered;
    if (pressed)
        value = !value;

    // Use the switch ID for animation state so clicking the label and clicking
    // the track share the same animation state.
    const ImGuiID id = ImGui::GetID("##toggle_anim");
    float* animated = ImGui::GetStateStorage()->GetFloatRef(id, value ? 1.0f : 0.0f);
    const float target = value ? 1.0f : 0.0f;
    if (state.UiAnimationsEnabled()) {
        const float dt = UiFrameDelta();
        *animated += (target - *animated) * (1.0f - std::exp(-24.0f * dt));
    } else {
        *animated = target;
    }
    *animated = std::clamp(*animated, 0.0f, 1.0f);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec4 offTrack =
        hovered ? ThemeVec(state.config.theme.searchHover, 0.78f) : ThemeVec(state.config.theme.searchBg, 0.64f);
    const bool lightMode = IsLightGuiTheme(state.config.themeModeIndex);
    const ImVec4 onTrack = lightMode ? ImVec4(0.24f, 0.24f, 0.26f, hovered ? 0.96f : 0.88f)
                                     : ImVec4(0.82f, 0.82f, 0.84f, hovered ? 0.96f : 0.88f);
    const ImVec4 track(offTrack.x + (onTrack.x - offTrack.x) * *animated,
                       offTrack.y + (onTrack.y - offTrack.y) * *animated,
                       offTrack.z + (onTrack.z - offTrack.z) * *animated,
                       offTrack.w + (onTrack.w - offTrack.w) * *animated);

    const ImVec2 max(pos.x + width, pos.y + height);
    const float radius = height * 0.5f;
    if (!state.ZeroGraphicsEnabled())
        DrawOuterShadow(dl, pos, max, radius, hovered ? 0.42f : 0.24f);
    dl->AddRectFilled(pos, max, ImGui::ColorConvertFloat4ToU32(track), radius);
    dl->AddRect(pos,
                max,
                ImGui::ColorConvertFloat4ToU32(ThemeVec(state.config.theme.floatingBorder, 0.72f)),
                radius,
                0,
                1.0f);

    const float knobRadius = radius - 3.0f;
    const float knobX = pos.x + radius + (width - height) * (1.0f - *animated);
    const ImVec2 knobCenter(knobX, pos.y + radius);
    const ImVec4 offKnob = ThemeVec(state.config.theme.text, 0.94f);
    const ImVec4 onKnob = ThemeVec(state.config.theme.inverseText, 0.94f);
    const ImVec4 knobColor(offKnob.x + (onKnob.x - offKnob.x) * *animated,
                           offKnob.y + (onKnob.y - offKnob.y) * *animated,
                           offKnob.z + (onKnob.z - offKnob.z) * *animated,
                           offKnob.w + (onKnob.w - offKnob.w) * *animated);
    dl->AddCircleFilled(knobCenter, knobRadius, ImGui::ColorConvertFloat4ToU32(knobColor));

    const ImVec2 ringCenter(pos.x + width - radius, pos.y + radius);
    if (*animated > 0.38f)
        dl->AddCircle(ringCenter,
                      3.5f,
                      ImGui::ColorConvertFloat4ToU32(ThemeVec(state.config.theme.inverseText, 0.88f)),
                      0,
                      1.4f);
    if (*animated < 0.62f) {
        const ImVec2 lineCenter(pos.x + radius, pos.y + radius);
        dl->AddLine(ImVec2(lineCenter.x, lineCenter.y - 4.0f),
                    ImVec2(lineCenter.x, lineCenter.y + 4.0f),
                    ImGui::ColorConvertFloat4ToU32(ThemeVec(state.config.theme.textDisabled, 0.82f)),
                    1.5f);
    }

    // Draw the label directly at the same geometric center as the switch.
    // This avoids font-baseline/frame-padding offsets.
    const ImVec2 labelPos(labelHitPos.x,
                          pos.y + std::floor((height - labelSize.y) * 0.5f));
    dl->AddText(labelPos, ImGui::GetColorU32(ImGuiCol_Text), label);

    ImGui::PopID();
    return pressed;
}
bool IsTickerInWatchlist(const AppState& state, const std::string& ticker) {
    return std::find(state.config.watchlist.begin(), state.config.watchlist.end(), ticker) !=
           state.config.watchlist.end();
}
static void CommitWatchlistChange(AppState& state) {
    squarestar::config::RequestConfigSave();
    if (!state.navigation.liteGuiActive && state.navigation.activeSidebarTab == squarestar::application::SidebarTab::Overview &&
        state.navigation.activeScreenerIndex >= 0 &&
        state.navigation.activeScreenerIndex < (int)std::size(kScreenerRoutes) &&
        kScreenerRoutes[state.navigation.activeScreenerIndex].guiId == "watchlist")
        squarestar::application::StartScreenerFetch(
            state, "watchlist");
}
bool AddTickerToWatchlist(AppState& state, const std::string& ticker) {
    const std::optional<MarketSymbol> symbol = MarketSymbol::Parse(ticker);
    if (!symbol || IsTickerInWatchlist(state, symbol->Canonical()))
        return false;
    if (state.config.watchlist.size() >= squarestar::application::kMaxWatchlistItems) {
        PublishSilentFeedback(state,
                              UserFeedbackType::Warning,
                              "Watchlist limit",
                              "Remove a symbol before adding another watchlist entry.");
        if (state.navigation.liteGuiActive)
            PlayUISound("click.wav", state);
        return false;
    }
    state.config.watchlist.push_back(symbol->Canonical());
    CommitWatchlistChange(state);
    PlayUISound(state.navigation.liteGuiActive ? "click.wav" : "on.wav", state);
    return true;
}
bool RemoveTickerFromWatchlist(AppState& state, const std::string& ticker) {
    const auto found = std::find(state.config.watchlist.begin(), state.config.watchlist.end(), ticker);
    if (found == state.config.watchlist.end())
        return false;
    state.config.watchlist.erase(found);
    CommitWatchlistChange(state);
    PlayUISound(state.navigation.liteGuiActive ? "click.wav" : "off.wav", state);
    return true;
}

} // namespace squarestar::shell
