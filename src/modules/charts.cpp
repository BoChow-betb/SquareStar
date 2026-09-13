#include "modules/charts.hpp"
#include "modules/charts_internal.hpp"
#include "modules/core.hpp"
#include "modules/lite_gui.hpp"
#include "modules/market_data.hpp"
#include "modules/ui_focus.hpp"
#include "modules/platform.hpp"
#include "modules/stock_details.hpp"
#include "modules/chart_controls.hpp"
#include "modules/chart_footer.hpp"
#include "modules/chart_alert_editor.hpp"
#include "modules/chart_tab_bar.hpp"
#include "modules/stock_comparison.hpp"
#include "domain/trading_status.hpp"

#include "services/config_save_queue.hpp"
#include "application/navigation_state.hpp"
#include "application/main_loop_signal.hpp"
#include "domain/chart_ranges.hpp"
#include "domain/market_runtime.hpp"
#include "domain/world_clock_zones.hpp"
#include "platform/world_clock_runtime.hpp"
#include "presentation/chart_axis.hpp"
#include "presentation/chart_export.hpp"
#include "presentation/chart_lod.hpp"
#include "presentation/chart_price_axis.hpp"
#include "presentation/chart_series.hpp"
#include "presentation/chart_style.hpp"
#include "presentation/chart_types.hpp"
#include "presentation/stock_display_text.hpp"
namespace squarestar::shell {


using squarestar::application::RequestGuiRedraw;
using squarestar::application::AppState;
using squarestar::application::CanEnterStockComparison;
using squarestar::application::StockContext;
using squarestar::application::TerminalAction;
using squarestar::application::StockFetchMetrics;
using squarestar::application::StockFetchProfile;
using squarestar::application::StockFetchNews;

void RenderSingleStockWindowContent(AppState& state,
                                    StockContext& ctx,
                                    bool compactLite) {
    const float rawDt = ImGui::GetIO().DeltaTime;
    float dt = std::min(rawDt, 1.0f / 30.0f);
    const bool cleanGuiCapture = IsCleanGuiCaptureFrame();
    const ImVec2 stockCaptureMin = ImGui::GetCursorScreenPos();
    // A market quote is data, not an animation target. Interpolating the numeric
    // value makes one fixed provider quote appear as a stream of fake prices
    // (especially noticeable immediately after search while the market is closed).
    // Snap the displayed number to the published quote; priceFlashAnim below
    // still provides visual feedback when a real quote update arrives.
    ctx.render.displayPrice = ctx.RawData().currentPrice;
    float targetFetchAnim = ctx.requests.isLoading ? 1.0f : 0.0f;
    if (state.UiAnimationsEnabled()) {
        ctx.render.loadingBlockAnim += (targetFetchAnim - ctx.render.loadingBlockAnim) * dt * 14.0f;
    } else {
        ctx.render.loadingBlockAnim = targetFetchAnim;
    }
    if (ctx.render.tabFadeAnim < 1.0f) {
        ctx.render.tabFadeAnim += state.UiAnimationsEnabled() ? dt * 16.0f : 1.0f;
        if (ctx.render.tabFadeAnim > 1.0f)
            ctx.render.tabFadeAnim = 1.0f;
    }
    if (ctx.requests.isLoading ||
        (state.UiAnimationsEnabled() &&
         (std::abs(ctx.render.loadingBlockAnim - targetFetchAnim) > 0.001f ||
          ctx.render.tabFadeAnim < 0.999f)))
        RequestGuiRedraw();
    bool shortcutClock = false;
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) && !ImGui::GetIO().WantTextInput) {
        if (IsActionPressed(state.config, TerminalAction::ToggleClock))
            shortcutClock = true;
        if (IsActionPressed(state.config, TerminalAction::TimeRangePrev) &&
            ctx.navigation.selectedTimeRangeIndex > 0) {
            PlayUISound("click.wav", state);
            SelectStockViewRange(state, ctx, ctx.navigation.selectedTimeRangeIndex - 1);
        }
        if (IsActionPressed(state.config, TerminalAction::TimeRangeNext) &&
            ctx.navigation.selectedTimeRangeIndex < 6) {
            PlayUISound("click.wav", state);
            SelectStockViewRange(state, ctx, ctx.navigation.selectedTimeRangeIndex + 1);
        }
        if (!state.navigation.pureMonitorMode) {
            constexpr int upperTabCount = 4;
            if (IsActionPressed(state.config, TerminalAction::UpperTabPrev)) {
                ctx.navigation.nextUpperTab =
                    (ctx.navigation.upperTabIndex + upperTabCount - 1) % upperTabCount;
            }
            if (IsActionPressed(state.config, TerminalAction::UpperTabNext)) {
                ctx.navigation.nextUpperTab =
                    (ctx.navigation.upperTabIndex + 1) % upperTabCount;
            }
            if (IsActionPressed(state.config, TerminalAction::OpenComparison)) {
                if (!CanEnterStockComparison(state)) {
                    ShowStockModeNotice(state,
                                        ctx,
                                        "Comparison needs another tab",
                                        "Open one more stock tab to compare.");
                } else {
                    ctx.navigation.nextUpperTab = 3;
                    PrepareStockComparisonMode(state, ctx, true);
                }
            }
        }
    }
    ctx.render.animProgress =
        state.UiAnimationsEnabled() ? ctx.render.animProgress + (1.0f - ctx.render.animProgress) * 14.0f * dt : 1.0f;
    ctx.render.fadeAlpha =
        state.UiAnimationsEnabled()
            ? (ctx.render.fadeAlpha + ((ctx.requests.isLoading ? 0.0f : 1.0f) - ctx.render.fadeAlpha) * dt * 14.0f)
            : (ctx.requests.isLoading ? 0.0f : 1.0f);
    double cP = 0.0, pC = 0.0, diff = 0.0, pct = 0.0;
    const double dailyPreviousClose = ctx.RawData().previousClose;
    ImVec4 themeCol = ThemeVec(state.config.theme.textDisabled);
    if (ctx.RawData().success) {
        cP = ctx.render.displayPrice;
        pC = ctx.RawData().chartPreviousClose > 0.0
                 ? ctx.RawData().chartPreviousClose
                 : dailyPreviousClose;
        if (pC > 0.0) {
            // Keep the normal GUI and LiteGUI aligned to the currently
            // displayed chart range. Yahoo's chartPreviousClose is the close
            // immediately before that range, so the line, gain/loss label and
            // chart direction all share one range-aware baseline.
            const double rangeCurrentPrice = ctx.RawData().currentPrice;
            diff = rangeCurrentPrice - pC;
            pct = (diff / pC) * 100.0;
            const double neutralEpsilon = std::max(1e-8, std::abs(pC) * 1e-8);
            if (diff < -neutralEpsilon)
                themeCol = ThemeVec(state.config.theme.negative);
            else if (diff > neutralEpsilon)
                themeCol = ThemeVec(state.config.theme.positive);
            else
                themeCol = ThemeVec(state.config.theme.textDisabled);
        } else {
            diff = 0.0;
            pct = 0.0;
            themeCol = ThemeVec(state.config.theme.textDisabled);
        }
    }
    auto ApplyPendingUpperTab = [&]() {
        if (ctx.navigation.nextUpperTab == -1)
            return;
        if (state.navigation.liteGuiActive)
            ctx.navigation.nextUpperTab = std::clamp(ctx.navigation.nextUpperTab, 0, 3);
        if (ctx.navigation.nextUpperTab == 3 && !CanEnterStockComparison(state)) {
            ctx.navigation.nextUpperTab = -1;
            ShowStockModeNotice(state,
                                ctx,
                                "Comparison needs another tab",
                                "Open one more stock tab to compare.");
            return;
        }
        const int targetUpperTab = ctx.navigation.nextUpperTab;
        const bool tabChanged = targetUpperTab != ctx.navigation.upperTabIndex;
        if (targetUpperTab == 3 && ctx.navigation.upperTabIndex != 3)
            PrepareStockComparisonMode(state, ctx, true);
        if (tabChanged)
            PlayUISound("transition.wav", state);
        ctx.render.tabFadeAnim = state.UiAnimationsEnabled() ? 0.0f : 1.0f;
        ctx.navigation.upperTabIndex = targetUpperTab;
        ctx.navigation.nextUpperTab = -1;
        RequestGuiRedraw();
    };
    if (ctx.navigation.upperTabIndex == 3 && !CanEnterStockComparison(state)) {
        ctx.navigation.upperTabIndex = 0;
        ctx.navigation.nextUpperTab = -1;
        ctx.render.tabFadeAnim = 1.0f;
        ShowStockModeNotice(state,
                            ctx,
                            "Comparison needs another tab",
                            "Open one more stock tab to compare.");
    }
    ApplyPendingUpperTab();
    if (!ctx.RawData().success && !ctx.requests.isLoading) {
        RenderStockErrorOverlay(state, ctx);
        return;
    }
    if (compactLite) {
        RenderStockTabBar(state, ctx, cleanGuiCapture);
        // Lite mode can otherwise settle immediately after a click, leaving the
        // selected tab queued until an unrelated event wakes another frame.
        ApplyPendingUpperTab();
        ImGui::Dummy(ImVec2(0.0f, 6.0f));
    } else {
        RenderStockHeader(
            state, ctx, dt, cleanGuiCapture, cP, pC, diff, pct, themeCol);
    }
    if (!ctx.RawData().success) {
        ImGui::TextDisabled("Fetching data");
    } else {
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ctx.render.fadeAlpha * ctx.render.tabFadeAnim);
        switch (ctx.navigation.upperTabIndex) {
        case 0:
            RenderStockChart(state,
                             ctx,
                             dt,
                             shortcutClock,
                             cleanGuiCapture,
                             stockCaptureMin,
                             pC,
                             themeCol);
            break;
        case 1:
            TriggerDetailsFetch(state, ctx, StockFetchNews);
            RenderStockNews(state, ctx);
            break;
        case 2:
            TriggerDetailsFetch(
                state, ctx, StockFetchMetrics | StockFetchProfile);
            RenderStockMetrics(state, ctx, dailyPreviousClose);
            break;
        case 3:
            RenderStockComparison(state, ctx, stockCaptureMin);
            break;
        }
        ImGui::PopStyleVar();
    }
    RenderStockWipeOverlay(state, ctx);
    RenderStockModeNotice(state, ctx, dt);
}


} // namespace squarestar::shell
