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


using squarestar::market::TradingStatusExchangeLabel;
using squarestar::application::RequestGuiRedraw;
using squarestar::application::UiRounding;
using squarestar::application::AppState;
using squarestar::application::StockContext;
using squarestar::presentation::EllipsizeTextBinary;
using squarestar::presentation::CleanExchangeLabel;
using squarestar::presentation::CleanCompanyDisplayName;

static void RenderTickerIdentityRow(AppState& state, StockContext& ctx) {
    const std::string stockSubtitle =
        CleanCompanyDisplayName(ctx.RawData().companyName) + " \xE2\x80\xA2 " +
        TradingStatusExchangeLabel(CleanExchangeLabel(ctx.RawData().exchange),
                                   ctx.RawData().tradingStatus);
    const float titleRowWidth = ImGui::GetContentRegionAvail().x;
    const float tickerWidth =
        state.render.fontLarge
            ->CalcTextSizeA(state.config.theme.fontTitle,
                            std::numeric_limits<float>::max(),
                            0.0f,
                            ctx.navigation.ticker)
            .x;
    const float maxSubtitleWidth = std::max(80.0f, titleRowWidth - tickerWidth - 50.0f);
    ImGui::PushFont(state.render.fontLarge);
    ImGui::TextUnformatted(ctx.navigation.ticker);
    ImGui::PopFont();
    ImGui::SameLine(0.0f, 10.0f);
    const std::string shownSubtitle = EllipsizeTextBinary(stockSubtitle, maxSubtitleWidth);
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", shownSubtitle.c_str());
    ImGui::SameLine(0.0f, 8.0f);

    const bool saved = IsTickerInWatchlist(state, ctx.navigation.ticker);
    const ImVec2 buttonPos = ImGui::GetCursorScreenPos();
    const ImVec2 buttonSize(24.0f, 24.0f);
    const bool pressed = ImGui::InvisibleButton("##TickerWatchlistToggle", buttonSize);
    const bool hovered = ImGui::IsItemHovered();
    if (hovered)
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

    const ImVec4 ink =
        saved ? ThemeVec(state.config.theme.text, 0.88f)
              : ThemeVec(state.config.theme.textDisabled, hovered ? 0.78f : 0.52f);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 center(buttonPos.x + buttonSize.x * 0.5f,
                        buttonPos.y + buttonSize.y * 0.5f);
    if (hovered) {
        draw->AddRectFilled(buttonPos,
                            ImVec2(buttonPos.x + buttonSize.x, buttonPos.y + buttonSize.y),
                            ImGui::GetColorU32(ImGuiCol_ButtonHovered),
                            UiRounding(state, 6.0f));
    }
    const ImU32 iconColor = ImGui::ColorConvertFloat4ToU32(ink);
    if (saved) {
        draw->AddLine(ImVec2(center.x - 5.0f, center.y),
                      ImVec2(center.x - 1.0f, center.y + 4.0f),
                      iconColor,
                      2.1f);
        draw->AddLine(ImVec2(center.x - 1.0f, center.y + 4.0f),
                      ImVec2(center.x + 6.0f, center.y - 5.0f),
                      iconColor,
                      2.1f);
    } else {
        draw->AddLine(ImVec2(center.x - 5.0f, center.y),
                      ImVec2(center.x + 5.0f, center.y),
                      iconColor,
                      1.8f);
        draw->AddLine(ImVec2(center.x, center.y - 5.0f),
                      ImVec2(center.x, center.y + 5.0f),
                      iconColor,
                      1.8f);
    }
    if (pressed) {
        if (saved)
            RemoveTickerFromWatchlist(state, ctx.navigation.ticker);
        else
            AddTickerToWatchlist(state, ctx.navigation.ticker);
    }
}

static void UpdateQuoteFlash(AppState& state, StockContext& ctx, float dt) {
    if (std::abs(ctx.RawData().currentPrice - ctx.render.lastTrackedPrice) > 1e-12 &&
        ctx.render.lastTrackedPrice > 0.0) {
        ctx.render.priceFlashAnim = 1.0f;
        ctx.render.flashColor = ctx.RawData().currentPrice < ctx.render.lastTrackedPrice
                                    ? ThemeVec(state.config.theme.negative)
                                    : ThemeVec(state.config.theme.positive);
    }
    ctx.render.lastTrackedPrice = ctx.RawData().currentPrice;
    if (ctx.render.priceFlashAnim <= 0.0f)
        return;

    ctx.render.priceFlashAnim -= state.UiAnimationsEnabled() ? dt * 5.0f : 1.0f;
    ctx.render.priceFlashAnim = std::max(0.0f, ctx.render.priceFlashAnim);
    if (state.UiAnimationsEnabled() && ctx.render.priceFlashAnim > 0.001f)
        RequestGuiRedraw();
}

static ImVec4 QuoteFlashColor(const StockContext& ctx) {
    ImVec4 color = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    if (ctx.render.priceFlashAnim <= 0.0f)
        return color;
    color.x += (ctx.render.flashColor.x - color.x) * ctx.render.priceFlashAnim;
    color.y += (ctx.render.flashColor.y - color.y) * ctx.render.priceFlashAnim;
    color.z += (ctx.render.flashColor.z - color.z) * ctx.render.priceFlashAnim;
    color.w += (ctx.render.flashColor.w - color.w) * ctx.render.priceFlashAnim;
    return color;
}

static void RenderStockQuoteRow(AppState& state,
                                StockContext& ctx,
                                float dt,
                                double currentPrice,
                                double referencePrice,
                                double change,
                                double changePercent,
                                const ImVec4& changeColor) {
    const std::time_t quoteTimestamp =
        ctx.RawData().quoteTimestamp > 0
            ? ctx.RawData().quoteTimestamp
            : (ctx.RawData().timestamps.empty()
                   ? ctx.requests.lastFetchTime
                   : static_cast<std::time_t>(ctx.RawData().timestamps.back()));
    const std::string asOfText = squarestar::platform::FormatAsOfTime(
        quoteTimestamp, state.config.graphTimeZone == 1);
    ImGui::Dummy(ImVec2(0.0f, 1.0f));
    UpdateQuoteFlash(state, ctx, dt);

    char priceText[32];
    std::snprintf(priceText, sizeof(priceText), "%.2f", currentPrice);
    constexpr const char* currencyText = "USD";
    ImFont* priceFont = state.render.fontQuote;
    float priceFontSize = state.config.theme.fontQuote;
    if (strlen(priceText) >= 12 && state.render.fontLarge) {
        priceFont = state.render.fontLarge;
        priceFontSize = state.config.theme.fontTitle;
    } else if (strlen(priceText) >= 9 && state.render.fontGiant) {
        priceFont = state.render.fontGiant;
        priceFontSize = state.config.theme.fontHero;
    }

    char changeText[96]{};
    if (std::isfinite(referencePrice) && referencePrice > 0.0) {
        std::snprintf(changeText,
                      sizeof(changeText),
                      "%c%.2f (%.2f%%)",
                      change > 0 ? '+' : (change < 0 ? '-' : ' '),
                      std::abs(change),
                      std::abs(changePercent));
    } else {
        std::snprintf(changeText, sizeof(changeText), "-- (--)");
    }

    const float priceWidth = priceFont
                                 ->CalcTextSizeA(priceFontSize,
                                                 std::numeric_limits<float>::max(),
                                                 0.0f,
                                                 priceText)
                                 .x;
    const float currencyWidth = ImGui::CalcTextSize(currencyText).x;
    const float changeWidth = state.render.fontLarge
                                  ->CalcTextSizeA(state.config.theme.fontTitle,
                                                  std::numeric_limits<float>::max(),
                                                  0.0f,
                                                  changeText)
                                  .x;
    const float asOfWidth = ImGui::CalcTextSize(asOfText.c_str()).x;
    const float available = ImGui::GetContentRegionAvail().x;
    const bool stackChange = priceWidth + currencyWidth + changeWidth + asOfWidth + 130.0f >
                             available;
    const bool stackAsOf = changeWidth + asOfWidth + 54.0f > available;

    ImGui::PushFont(priceFont, priceFontSize);
    if (ctx.render.priceFlashAnim > 0.0f)
        ImGui::TextColored(QuoteFlashColor(ctx), "%s", priceText);
    else
        ImGui::Text("%s", priceText);
    ImGui::PopFont();

    const bool priceHovered = ImGui::IsItemHovered();
    const bool priceClicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
    const ImVec2 priceFocusMin = ImGui::GetItemRectMin();
    const ImVec2 priceFocusMax = ImGui::GetItemRectMax();
    if (priceHovered) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        ImGui::GetWindowDrawList()->AddLine(
            ImVec2(priceFocusMin.x, priceFocusMax.y + 1.0f),
            ImVec2(priceFocusMax.x, priceFocusMax.y + 1.0f),
            ImGui::GetColorU32(ImGuiCol_Text),
            1.0f);
    }
    DrawObjectFocusOutline(state, priceFocusMin, priceFocusMax, priceHovered, 4);
    RenderPriceAlertEditor(state,
                           ctx,
                           priceClicked,
                           priceFocusMin,
                           priceFocusMax,
                           currentPrice,
                           currencyText);

    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, ThemeVec(state.config.theme.textDisabled));
    ImGui::TextUnformatted(currencyText);
    ImGui::PopStyleColor();
    if (stackChange)
        ImGui::Dummy(ImVec2(0.0f, 2.0f));
    else
        ImGui::SameLine(0, 25);
    ImGui::PushFont(state.render.fontLarge);
    ImGui::TextColored(changeColor, "%s", changeText);
    ImGui::PopFont();
    if (stackAsOf)
        ImGui::Dummy(ImVec2(0.0f, 1.0f));
    else
        ImGui::SameLine(0.0f, 14.0f);
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", asOfText.c_str());
}

static void RenderUnavailableStockHeader(const AppState& state, const StockContext& ctx) {
    ImGui::PushFont(state.render.fontLarge);
    ImGui::Text("%s", ctx.requests.isLoading ? "Fetching Data" : "Unavailable");
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::TextDisabled("%s", ctx.navigation.ticker);
    ImGui::Spacing();
    ImGui::PushFont(state.render.fontGiant);
    ImGui::Text("--.--");
    ImGui::PopFont();
}

static void RenderFullStockHeader(AppState& state,
                                  StockContext& ctx,
                                  float dt,
                                  bool cleanGuiCapture,
                                  double currentPrice,
                                  double referencePrice,
                                  double change,
                                  double changePercent,
                                  const ImVec4& changeColor) {
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(0, 0));
    if (ImGui::BeginTable("HeaderTable", 2, ImGuiTableFlags_NoBordersInBody)) {
        ImGui::TableSetupColumn("Left", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Range", ImGuiTableColumnFlags_WidthFixed, 380.0f);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::BeginGroup();
        if (ctx.RawData().success) {
            RenderTickerIdentityRow(state, ctx);
            RenderStockQuoteRow(state,
                                ctx,
                                dt,
                                currentPrice,
                                referencePrice,
                                change,
                                changePercent,
                                changeColor);
        } else {
            RenderUnavailableStockHeader(state, ctx);
        }
        ImGui::EndGroup();
        ImGui::TableNextColumn();
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 3.0f);
        RenderStockRangeButtons(state, ctx);
        ImGui::EndTable();
    }
    ImGui::PopStyleVar();
    ImGui::Dummy(ImVec2(0.0f, 4.0f));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0.0f, 4.0f));
    RenderStockTabBar(state, ctx, cleanGuiCapture);
}

static void RenderMonitorStockHeader(AppState& state,
                                     StockContext& ctx,
                                     double currentPrice,
                                     double change,
                                     double changePercent,
                                     const ImVec4& changeColor) {
    const bool dense = ImGui::GetWindowHeight() < 310.0f || ImGui::GetWindowWidth() < 360.0f;
    ImGui::SetCursorPos(dense ? ImVec2(8.0f, 6.0f) : ImVec2(12.0f, 9.0f));
    ImGui::PushFont(state.render.fontLarge ? state.render.fontLarge : ImGui::GetFont());
    ImGui::TextUnformatted(ctx.navigation.ticker);
    ImGui::PopFont();
    if (ctx.RawData().success) {
        ImGui::SameLine(0.0f, dense ? 8.0f : 12.0f);
        ImGui::PushFont(state.render.fontNormal ? state.render.fontNormal : ImGui::GetFont());
        ImGui::TextColored(ThemeVec(state.config.theme.text, 0.82f), "%.2f USD", currentPrice);
        ImGui::PopFont();
        ImGui::SameLine(0.0f, dense ? 8.0f : 12.0f);
        ImGui::PushFont(state.render.fontData ? state.render.fontData : ImGui::GetFont());
        ImGui::TextColored(changeColor,
                           "%c%.2f (%.2f%%)",
                           change > 0 ? '+' : (change < 0 ? '-' : ' '),
                           std::abs(change),
                           std::abs(changePercent));
        ImGui::PopFont();
    } else {
        ImGui::SameLine(0.0f, 10.0f);
        ImGui::TextDisabled("%s", ctx.requests.isLoading ? "Loading" : "Unavailable");
    }
    ImGui::Dummy(ImVec2(0.0f, dense ? 1.0f : 3.0f));
}

void RenderStockHeader(AppState& state,
                              StockContext& ctx,
                              float dt,
                              bool cleanGuiCapture,
                              double currentPrice,
                              double referencePrice,
                              double change,
                              double changePercent,
                              const ImVec4& changeColor) {
    if (state.navigation.pureMonitorMode) {
        RenderMonitorStockHeader(
            state, ctx, currentPrice, change, changePercent, changeColor);
        return;
    }
    RenderFullStockHeader(state,
                          ctx,
                          dt,
                          cleanGuiCapture,
                          currentPrice,
                          referencePrice,
                          change,
                          changePercent,
                          changeColor);
}


} // namespace squarestar::shell
