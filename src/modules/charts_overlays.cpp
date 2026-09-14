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


using squarestar::platform::Win32AppRuntime;
using squarestar::format::FormatDouble;
using squarestar::market::TIME_RANGES;
using squarestar::market::CachedMarketOpen;
using squarestar::market::TradingStatusExchangeLabel;
using squarestar::market::IsMarketOpeningWindow;
using squarestar::application::RequestGuiRedraw;
using squarestar::application::RequestGuiWakeAt;
using squarestar::application::UiRounding;
using squarestar::presentation::GuiShellRuntime;
using squarestar::application::EaseOutCubic;
using squarestar::application::AppState;
using squarestar::application::UserFeedbackType;
using squarestar::application::CanEnterStockComparison;
using squarestar::application::StockContext;
using squarestar::application::TerminalAction;
using squarestar::application::IsLightGuiTheme;
using squarestar::application::StockFetchMetrics;
using squarestar::application::StockFetchProfile;
using squarestar::application::StockFetchNews;
using squarestar::presentation::NotificationCardWidth;
using squarestar::presentation::DrawStablePrevCloseLine;
using squarestar::presentation::EllipsizeTextBinary;
using squarestar::presentation::ChartVisualType;
using squarestar::presentation::ChartExportMethod;
using squarestar::presentation::EnsureConfiguredTimeAxisTicks;
using squarestar::presentation::UpdateStableChartAxisLock;
using squarestar::presentation::ResetChartRenderLod;
using squarestar::presentation::BuildChartRenderLod;
using squarestar::presentation::NearestSortedIndex;
using squarestar::presentation::EnsureVisiblePriceTicks;
using squarestar::presentation::ResolveVisibleReferenceLineY;
using squarestar::presentation::CleanExchangeLabel;
using squarestar::presentation::CleanCompanyDisplayName;
using squarestar::presentation::CrosshairDotColor;

void RenderStockWipeOverlay(AppState& state, StockContext& ctx) {
    float displayWipeAnim = ctx.render.loadingBlockAnim;
    if (displayWipeAnim > 0.01f) {
        ImVec2 wMin = ImGui::GetWindowPos();
        ImVec2 wMax = ImVec2(wMin.x + ImGui::GetWindowSize().x, wMin.y + ImGui::GetWindowSize().y);
        ImGuiViewport* wipeViewport = ImGui::GetWindowViewport();
        if (!wipeViewport)
            wipeViewport = ImGui::GetMainViewport();
        ImDrawList* dl = ImGui::GetForegroundDrawList(wipeViewport);
        dl->PushClipRect(wMin, wMax, true);
        float wipeWidth = (wMax.x - wMin.x);
        ImVec2 pMin, pMax;
        if (ctx.requests.isLoading) {
            pMin = wMin;
            pMax = ImVec2(wMin.x + wipeWidth * displayWipeAnim, wMax.y);
        } else {
            pMin = ImVec2(wMin.x + wipeWidth * (1.0f - displayWipeAnim), wMin.y);
            pMax = wMax;
        }
        const ImVec4 wipeColor = ThemeVec(state.config.theme.wipeBg);
        dl->AddRectFilled(pMin, pMax, ImGui::ColorConvertFloat4ToU32(wipeColor));
        if (displayWipeAnim > 0.3f) {
            const char* txt = "Fetching Data";
            ImFont* mainFont = state.render.fontGiant ? state.render.fontGiant : ImGui::GetFont();
            const float mainFontSize =
                state.render.fontGiant ? state.config.theme.fontHero : ImGui::GetFontSize();
            const ImVec2 ts =
                mainFont->CalcTextSizeA(mainFontSize, std::numeric_limits<float>::max(), 0.0f, txt);
            dl->PushClipRect(pMin, pMax, true);
            const ImU32 txtCol = ImGui::ColorConvertFloat4ToU32(
                ThemeVec(state.config.theme.textDisabled));
            dl->AddText(mainFont,
                        mainFontSize,
                        ImVec2(wMin.x + (wipeWidth - ts.x) * 0.5f,
                               wMin.y + (wMax.y - wMin.y - ts.y) * 0.5f),
                        txtCol,
                        txt);
            dl->PopClipRect();
        }
        dl->PopClipRect();
        if (ctx.requests.isLoading) {
            ImGui::SetCursorScreenPos(wMin);
            ImGui::InvisibleButton("##WipeBlocker", ImVec2(wMax.x - wMin.x, wMax.y - wMin.y));
        }
    }
}
void RenderStockModeNotice(AppState& state, StockContext& ctx, float dt) {
    if (state.navigation.liteGuiActive) {
        ctx.render.transientNoticeUntil = {};
        ctx.render.transientNoticeTitle.clear();
        ctx.render.transientNoticeMessage.clear();
        ctx.render.transientNoticeAnim = 0.0f;
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    const bool holding = ctx.render.transientNoticeUntil != std::chrono::steady_clock::time_point{} &&
                         now < ctx.render.transientNoticeUntil;
    if (holding)
        RequestGuiWakeAt(ctx.render.transientNoticeUntil);
    const float animationTarget = holding ? 1.0f : 0.0f;
    const auto animation = squarestar::application::AdvanceTransientNoticeAnimation(
        ctx.render.transientNoticeAnim, holding, dt, state.UiAnimationsEnabled());
    ctx.render.transientNoticeAnim = animation.value;
    if (animation.clearExpired) {
        ctx.render.transientNoticeUntil = {};
        ctx.render.transientNoticeTitle.clear();
        ctx.render.transientNoticeMessage.clear();
        return;
    }
    if (ctx.render.transientNoticeTitle.empty() || ctx.render.transientNoticeAnim <= 0.001f)
        return;

    const float smooth = ctx.render.transientNoticeAnim * ctx.render.transientNoticeAnim *
                         (3.0f - 2.0f * ctx.render.transientNoticeAnim);
    const ImVec2 windowMin = ImGui::GetWindowPos();
    const ImVec2 windowMax(windowMin.x + ImGui::GetWindowSize().x,
                           windowMin.y + ImGui::GetWindowSize().y);
    const float availableWidth = std::max(1.0f, windowMax.x - windowMin.x);
    const float availableHeight = std::max(1.0f, windowMax.y - windowMin.y);
    const float margin = std::min(18.0f, availableWidth * 0.04f);
    const float cardWidth = std::max(
        1.0f,
        std::min(NotificationCardWidth(availableWidth), availableWidth - margin * 2.0f));
    ImFont* titleFont = state.render.fontData ? state.render.fontData : ImGui::GetFont();
    ImFont* bodyFont = state.render.fontData ? state.render.fontData : ImGui::GetFont();
    const float noticeFontSize =
        state.render.fontData ? state.config.theme.fontData : ImGui::GetFontSize();
    const float textWidth = std::max(40.0f, cardWidth - 36.0f);
    const ImVec2 titleSize = titleFont->CalcTextSizeA(noticeFontSize,
                                                     std::numeric_limits<float>::max(),
                                                     textWidth,
                                                     ctx.render.transientNoticeTitle.c_str());
    const ImVec2 bodySize = bodyFont->CalcTextSizeA(noticeFontSize,
                                                   std::numeric_limits<float>::max(),
                                                   textWidth,
                                                   ctx.render.transientNoticeMessage.c_str());
    const float desiredHeight = 16.0f + titleSize.y + 9.0f + bodySize.y + 16.0f;
    const float cardHeight =
        std::max(1.0f, std::min(desiredHeight, availableHeight - margin * 2.0f));
    const float restingX = windowMax.x - margin - cardWidth;
    const float cardX = restingX + (1.0f - smooth) * 48.0f;
    const float cardY = std::max(windowMin.y + margin, windowMax.y - margin - cardHeight);
    const ImVec2 cardMin(cardX, cardY);
    const ImVec2 cardMax(cardX + cardWidth, cardY + cardHeight);
    const ImVec2 revealMin(cardMax.x - cardWidth * smooth, cardMin.y);

    ImGuiViewport* viewport = ImGui::GetWindowViewport();
    if (!viewport)
        viewport = ImGui::GetMainViewport();
    ImDrawList* draw = ImGui::GetForegroundDrawList(viewport);
    draw->PushClipRect(windowMin, windowMax, true);
    draw->PushClipRect(revealMin, cardMax, true);
    const bool lightNotice = IsLightGuiTheme(state.config.themeModeIndex);
    ImVec4 background = lightNotice ? ImVec4(0.975f, 0.975f, 0.98f, 0.995f * smooth)
                                    : ImVec4(0.055f, 0.055f, 0.06f, 0.985f * smooth);
    ImVec4 border = lightNotice ? ImVec4(0.55f, 0.55f, 0.58f, smooth)
                                : ImVec4(0.34f, 0.34f, 0.36f, smooth);
    ImVec4 titleColor = lightNotice ? ImVec4(0.08f, 0.08f, 0.09f, smooth)
                                    : ImVec4(0.96f, 0.96f, 0.97f, smooth);
    ImVec4 bodyColor = lightNotice ? ImVec4(0.36f, 0.36f, 0.39f, smooth)
                                   : ImVec4(0.70f, 0.70f, 0.73f, smooth);
    draw->AddRectFilled(cardMin,
                        cardMax,
                        ImGui::ColorConvertFloat4ToU32(background),
                        UiRounding(state, 10.0f));
    draw->AddRect(cardMin,
                  cardMax,
                  ImGui::ColorConvertFloat4ToU32(border),
                  UiRounding(state, 10.0f),
                  0,
                  1.0f);
    draw->AddText(titleFont,
                  noticeFontSize,
                  ImVec2(cardMin.x + 18.0f, cardMin.y + 16.0f),
                  ImGui::ColorConvertFloat4ToU32(titleColor),
                  ctx.render.transientNoticeTitle.c_str(),
                  nullptr,
                  textWidth);
    draw->AddText(bodyFont,
                  noticeFontSize,
                  ImVec2(cardMin.x + 18.0f, cardMin.y + 16.0f + titleSize.y + 9.0f),
                  ImGui::ColorConvertFloat4ToU32(bodyColor),
                  ctx.render.transientNoticeMessage.c_str(),
                  nullptr,
                  textWidth);
    draw->PopClipRect();
    draw->PopClipRect();
    // The card is drawn on the viewport foreground list, so requiring the
    // underlying stock window to be hovered makes some of its text area miss
    // clicks. Hit-test the visible card itself instead.
    const bool noticeHovered = ImGui::IsMouseHoveringRect(revealMin, cardMax, false);
    if (noticeHovered)
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    if (noticeHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        // Clear the hold only; the existing animation performs the wipe-out.
        ctx.render.transientNoticeUntil = {};
        PlayUISound("click.wav", state);
        RequestGuiRedraw();
    }
    if (state.UiAnimationsEnabled() &&
        std::abs(ctx.render.transientNoticeAnim - animationTarget) > 0.001f)
        RequestGuiRedraw();
}


} // namespace squarestar::shell
