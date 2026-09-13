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


using squarestar::format::FormatDouble;
using squarestar::market::CachedMarketOpen;
using squarestar::market::IsMarketOpeningWindow;
using squarestar::application::RequestGuiRedraw;
using squarestar::application::UiRounding;
using squarestar::presentation::GuiShellRuntime;
using squarestar::application::EaseOutCubic;
using squarestar::application::AppState;
using squarestar::application::StockContext;
using squarestar::application::IsLightGuiTheme;
using squarestar::presentation::DrawStablePrevCloseLine;
using squarestar::presentation::ChartVisualType;
using squarestar::presentation::EnsureConfiguredTimeAxisTicks;
using squarestar::presentation::UpdateStableChartAxisLock;
using squarestar::presentation::ResetChartRenderLod;
using squarestar::presentation::BuildChartRenderLod;
using squarestar::presentation::NearestSortedIndex;
using squarestar::presentation::EnsureVisiblePriceTicks;
using squarestar::presentation::ResolveVisibleReferenceLineY;
using squarestar::presentation::CrosshairDotColor;

static void RefreshStockChartData(AppState& state,
                                  StockContext& ctx,
                                  int& count) {
    if (ctx.marketData.needsPlotDataUpdate ||
        ctx.marketData.plot_sX.size() != static_cast<size_t>(count) ||
        ctx.marketData.lastTimeZone != state.config.graphTimeZone ||
        ctx.render.lastPlotLineType != state.config.plotLineType) {
        const ChartVisualType canvasType = state.config.plotLineType == 0
                                               ? ChartVisualType::Candlestick
                                               : ChartVisualType::LineShaded;
        squarestar::presentation::ChartCanvasModel canvas =
            squarestar::presentation::BuildChartCanvasModel(ctx.RawData(),
                            canvasType,
                            state.config.graphTimeZone == 1,
                            false);
        ctx.marketData.plot_sX = std::move(canvas.x);
        ctx.marketData.plot_sC = std::move(canvas.close);
        ctx.marketData.plot_sO = std::move(canvas.open);
        ctx.marketData.plot_sH = std::move(canvas.high);
        ctx.marketData.plot_sL = std::move(canvas.low);
        ResetChartRenderLod(ctx);
        count = (int)ctx.marketData.plot_sC.size();
        ctx.marketData.plot_miY = canvas.lo;
        ctx.marketData.plot_maY = canvas.hi;
        ctx.marketData.lastTimeZone = state.config.graphTimeZone;
        ctx.render.lastPlotLineType = state.config.plotLineType;
        ctx.marketData.needsPlotDataUpdate = false;
    }
}

struct StockPlotGeometry {
    double panMinX = 0.0;
    double panMaxX = 1.0;
    double manualXAxisMin = 0.0;
    double manualXAxisMax = 1.0;
    double lowerLimitY = 0.0;
};

static StockPlotGeometry ConfigureStockPlot(AppState& state,
                                            StockContext& ctx,
                                            bool cleanGuiCapture,
                                            double previousClose,
                                            ImGuiViewport* chartViewport) {
    auto& xAxisCache = ctx.render.stockTimeAxisCache;
    auto& yAxisCache = ctx.render.priceAxisCache;

    ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Linear);
    const double firstTs = ctx.marketData.plot_sX.front();
    const double lastTs = ctx.marketData.plot_sX.back();
    const bool marketAxis = state.config.graphTimeZone == 1;
    std::time_t rawAxisAnchor = !ctx.RawData().timestamps.empty()
                                    ? (std::time_t)ctx.RawData().timestamps.back()
                                    : std::time(nullptr);
    const bool regularEquitySession =
        ctx.RawData().instrumentNature !=
        squarestar::market::InstrumentNature::DerivativeContract;
    if (ctx.navigation.displayedTimeRangeIndex == 0 &&
        regularEquitySession && CachedMarketOpen()) {
        rawAxisAnchor = std::time(nullptr);
    }

    UpdateStableChartAxisLock(ctx.render,
                              ctx.navigation.displayedTimeRangeIndex,
                              firstTs,
                              lastTs,
                              marketAxis,
                              rawAxisAnchor,
                              regularEquitySession);

    StockPlotGeometry geometry;
    geometry.panMinX = ctx.render.lockedAxisMinX;
    geometry.panMaxX = ctx.render.lockedAxisMaxX;
    const double axisXPadding =
        std::max(1.0,
                 (geometry.panMaxX - geometry.panMinX) *
                     (state.navigation.pureMonitorMode ? 0.010 : 0.006));
    geometry.manualXAxisMin = geometry.panMinX - axisXPadding;
    geometry.manualXAxisMax = geometry.panMaxX + axisXPadding;
    ImPlot::SetupAxisLimits(ImAxis_X1,
                            geometry.manualXAxisMin,
                            geometry.manualXAxisMax,
                            ImGuiCond_Always);

    const double yAxisPaddingFraction = state.navigation.pureMonitorMode ? 0.085 : 0.06;
    EnsureVisiblePriceTicks(ctx,
                            state.config.plotLineType == 0,
                            geometry.panMinX,
                            geometry.panMaxX,
                            previousClose,
                            yAxisPaddingFraction);
    geometry.lowerLimitY = yAxisCache.axisMin;
    ImPlot::SetupAxisLimits(
        ImAxis_Y1, yAxisCache.axisMin, yAxisCache.axisMax, ImGuiCond_Always);
    if (!yAxisCache.ticks.empty()) {
        ImPlot::SetupAxisTicks(ImAxis_Y1,
                               yAxisCache.ticks.data(),
                               (int)yAxisCache.ticks.size(),
                               yAxisCache.labelPtrs.data(),
                               false);
    }

    const int effectiveXAxisMode = cleanGuiCapture ? 2 : state.config.chartXAxisMode;
    EnsureConfiguredTimeAxisTicks(xAxisCache,
                                  ctx.marketData.dataRevision,
                                  ctx.marketData.plot_sX,
                                  geometry.panMinX,
                                  geometry.panMaxX,
                                  ctx.navigation.displayedTimeRangeIndex,
                                  marketAxis,
                                  effectiveXAxisMode);
    if (!xAxisCache.ticks.empty()) {
        ImPlot::SetupAxisTicks(ImAxis_X1,
                               xAxisCache.ticks.data(),
                               (int)xAxisCache.ticks.size(),
                               xAxisCache.labelPtrs.data(),
                               false);
    }

    // ImPlot getter and drawing calls lock plot setup, so the LOD query stays
    // after every Setup* call while still using the actual rendered plot width.
    const float chartFramebufferScale =
        chartViewport ? chartViewport->DpiScale
                      : ImGui::GetIO().DisplayFramebufferScale.x;
    BuildChartRenderLod(ctx,
                        state.config.plotLineType,
                        ImPlot::GetPlotSize().x,
                        chartFramebufferScale *
                            (cleanGuiCapture
                                 ? GuiShellRuntime().CleanGuiCaptureScale()
                                 : 1.0f));
    return geometry;
}

template <typename Price>
static void DrawCandlestickSeries(const std::vector<double>& x,
                                  const std::vector<Price>& open,
                                  const std::vector<Price>& high,
                                  const std::vector<Price>& low,
                                  const std::vector<Price>& close,
                                  ImU32 bullColor,
                                  ImU32 bearColor) {
    ImDrawList* drawList = ImPlot::GetPlotDrawList();
    const int candleCount =
        (int)std::min({x.size(), open.size(), high.size(), low.size(), close.size()});
    const double halfWidth = x.size() > 1 ? (x[1] - x[0]) * 0.35 : 60.0;
    ImPlot::PushPlotClipRect();
    for (int i = 0; i < candleCount; ++i) {
        const size_t index = (size_t)i;
        const double xValue = x[index];
        const double openValue = static_cast<double>(open[index]);
        const double closeValue = static_cast<double>(close[index]);
        const ImU32 color = closeValue >= openValue ? bullColor : bearColor;
        const ImVec2 highPoint =
            ImPlot::PlotToPixels(xValue, static_cast<double>(high[index]));
        const ImVec2 lowPoint =
            ImPlot::PlotToPixels(xValue, static_cast<double>(low[index]));
        drawList->AddLine(highPoint, lowPoint, color, 1.5f);
        ImVec2 bodyTop =
            ImPlot::PlotToPixels(xValue - halfWidth, std::max(openValue, closeValue));
        ImVec2 bodyBottom =
            ImPlot::PlotToPixels(xValue + halfWidth, std::min(openValue, closeValue));
        if (std::abs(bodyTop.y - bodyBottom.y) < 1.0f)
            bodyBottom.y = bodyTop.y + 1.0f;
        drawList->AddRectFilled(bodyTop, bodyBottom, color);
    }
    ImPlot::PopPlotClipRect();
}

static void RenderStockPriceSeries(AppState& state,
                                   StockContext& ctx,
                                   double lowerLimitY,
                                   const ImVec4& themeCol,
                                   float chartReveal) {
    const bool useLineRenderLod =
        !ctx.render.render_sX.empty() && !ctx.render.render_sC.empty();
    const std::vector<double>& lineX =
        useLineRenderLod ? ctx.render.render_sX : ctx.marketData.plot_sX;
    const std::vector<double>& lineC =
        useLineRenderLod ? ctx.render.render_sC : ctx.marketData.plot_sC;
    const int lineCount = (int)std::min(lineX.size(), lineC.size());
    const bool useCandleRenderLod =
        !ctx.render.renderCandle_sX.empty() && !ctx.render.renderCandle_sC.empty();
    const bool useShadeLod =
        !ctx.render.renderShade_sX.empty() && !ctx.render.renderShade_sC.empty();
    const std::vector<double>& shadeX =
        useShadeLod ? ctx.render.renderShade_sX : ctx.marketData.plot_sX;
    const std::vector<double>& shadeC =
        useShadeLod ? ctx.render.renderShade_sC : ctx.marketData.plot_sC;
    const int shadeCount = (int)std::min(shadeX.size(), shadeC.size());

    ImDrawList* seriesDrawList = ImPlot::GetPlotDrawList();
    const ImVec2 seriesPlotPos = ImPlot::GetPlotPos();
    const ImVec2 seriesPlotSize = ImPlot::GetPlotSize();
    const float revealRight =
        seriesPlotPos.x + seriesPlotSize.x * std::clamp(chartReveal, 0.0f, 1.0f);
    seriesDrawList->PushClipRect(
        seriesPlotPos,
        ImVec2(revealRight, seriesPlotPos.y + seriesPlotSize.y),
        true);

    if (state.config.plotLineType == 1) {
        const float shadeAlpha = state.config.theme.chartShadeAlpha;
        ImPlot::PlotShaded("##shade",
                           shadeX.data(),
                           shadeC.data(),
                           shadeCount,
                           lowerLimitY,
                           {ImPlotProp_FillColor,
                            ImVec4(themeCol.x, themeCol.y, themeCol.z, shadeAlpha)});
        ImPlot::PlotLine("##line",
                         lineX.data(),
                         lineC.data(),
                         lineCount,
                         {ImPlotProp_LineColor, themeCol, ImPlotProp_LineWeight, 2.7f});
    } else if (state.config.plotLineType == 0) {
        const ImU32 bullColor = ImGui::GetColorU32(ThemeVec(state.config.theme.positive));
        const ImU32 bearColor = ImGui::GetColorU32(ThemeVec(state.config.theme.negative));
        if (useCandleRenderLod) {
            DrawCandlestickSeries(ctx.render.renderCandle_sX,
                                  ctx.render.renderCandle_sO,
                                  ctx.render.renderCandle_sH,
                                  ctx.render.renderCandle_sL,
                                  ctx.render.renderCandle_sC,
                                  bullColor,
                                  bearColor);
        } else {
            DrawCandlestickSeries(ctx.marketData.plot_sX,
                                  ctx.marketData.plot_sO,
                                  ctx.marketData.plot_sH,
                                  ctx.marketData.plot_sL,
                                  ctx.marketData.plot_sC,
                                  bullColor,
                                  bearColor);
        }
    }
    seriesDrawList->PopClipRect();
}

static void UpdateStockXAxisHover(AppState& state,
                                  StockContext& ctx,
                                  bool cleanGuiCapture,
                                  bool plotHovered,
                                  float dt) {
    if (cleanGuiCapture)
        return;
    const float target = state.config.chartXAxisMode == 2 && plotHovered ? 1.0f : 0.0f;
    if (state.UiAnimationsEnabled()) {
        ctx.render.xAxisHoverAlpha +=
            (target - ctx.render.xAxisHoverAlpha) * (1.0f - std::exp(-16.0f * dt));
    } else {
        ctx.render.xAxisHoverAlpha = target;
    }
    ctx.render.xAxisHoverAlpha = std::clamp(ctx.render.xAxisHoverAlpha, 0.0f, 1.0f);
}

static void RenderPreviousCloseReference(AppState& state,
                                         StockContext& ctx,
                                         int count,
                                         double previousClose,
                                         double panMinX,
                                         double panMaxX,
                                         bool plotHovered,
                                         float dt) {
    if (previousClose <= 0.0)
        return;

    char previousCloseText[64];
    snprintf(previousCloseText,
             sizeof(previousCloseText),
             "Prev Close: %.2f %s",
             previousClose,
             "USD");
    const ImVec2 textSize = ImGui::CalcTextSize(previousCloseText);
    const ImVec2 plotPos = ImPlot::GetPlotPos();
    const ImVec2 plotMax(plotPos.x + ImPlot::GetPlotSize().x,
                         plotPos.y + ImPlot::GetPlotSize().y);
    ImVec2 lineStart = ImPlot::PlotToPixels(panMinX, previousClose);
    ImVec2 lineEnd = ImPlot::PlotToPixels(panMaxX, previousClose);
    const float visibleReferenceY = (float)ResolveVisibleReferenceLineY(
        lineEnd.y, plotPos.y, plotMax.y, textSize.y);
    lineStart.y = visibleReferenceY;
    lineEnd.y = visibleReferenceY;

    ImPlot::PushPlotClipRect();
    DrawStablePrevCloseLine(
        ImPlot::GetPlotDrawList(),
        lineStart,
        lineEnd,
        ImGui::ColorConvertFloat4ToU32(ThemeVec(state.config.theme.textDisabled, 0.70f)),
        1.5f);

    ImDrawList* drawList = ImPlot::GetPlotDrawList();
    ImVec2 textPos(lineEnd.x - textSize.x - 10.0f,
                   lineEnd.y - textSize.y - 5.0f);
    textPos.x = std::clamp(
        textPos.x,
        plotPos.x + 6.0f,
        std::max(plotPos.x + 6.0f, plotMax.x - textSize.x - 6.0f));
    textPos.y = std::clamp(
        textPos.y,
        plotPos.y + 6.0f,
        std::max(plotPos.y + 6.0f, plotMax.y - textSize.y - 6.0f));
    const ImVec2 rectMin(textPos.x - 4.0f, textPos.y - 2.0f);
    const ImVec2 rectMax(textPos.x + textSize.x + 4.0f,
                         textPos.y + textSize.y + 2.0f);

    bool labelObscuresPointer = false;
    if (plotHovered) {
        constexpr float hoverMargin = 10.0f;
        const ImVec2 mouse = ImGui::GetMousePos();
        labelObscuresPointer = mouse.x >= rectMin.x - hoverMargin &&
                               mouse.x <= rectMax.x + hoverMargin &&
                               mouse.y >= rectMin.y - hoverMargin &&
                               mouse.y <= rectMax.y + hoverMargin;
        const ImPlotPoint hoverPlot = ImPlot::GetPlotMousePos();
        const size_t nearestIndex =
            NearestSortedIndex(ctx.marketData.plot_sX, hoverPlot.x, (size_t)count);
        const ImVec2 crosshairPoint = ImPlot::PlotToPixels(
            ctx.marketData.plot_sX[nearestIndex], ctx.marketData.plot_sC[nearestIndex]);
        labelObscuresPointer =
            labelObscuresPointer ||
            (crosshairPoint.x >= rectMin.x - hoverMargin &&
             crosshairPoint.x <= rectMax.x + hoverMargin &&
             crosshairPoint.y >= rectMin.y - hoverMargin &&
             crosshairPoint.y <= rectMax.y + hoverMargin);
    }

    const float target = labelObscuresPointer ? 0.0f : 1.0f;
    if (state.UiAnimationsEnabled()) {
        ctx.render.prevCloseLabelAlpha +=
            (target - ctx.render.prevCloseLabelAlpha) * (1.0f - std::exp(-20.0f * dt));
    } else {
        ctx.render.prevCloseLabelAlpha = target;
    }
    ctx.render.prevCloseLabelAlpha =
        std::clamp(ctx.render.prevCloseLabelAlpha, 0.0f, 1.0f);
    if (ctx.render.prevCloseLabelAlpha > 0.01f) {
        const ImU32 bgColor = ImGui::ColorConvertFloat4ToU32(
            ThemeVec(state.config.theme.floatingBg,
                     0.94f * ctx.render.prevCloseLabelAlpha));
        const ImU32 textColor = ImGui::ColorConvertFloat4ToU32(
            ThemeVec(state.config.theme.textDisabled, ctx.render.prevCloseLabelAlpha));
        drawList->AddRectFilled(rectMin, rectMax, bgColor, UiRounding(state, 4.0f));
        drawList->AddText(textPos, textColor, previousCloseText);
    }
    ImPlot::PopPlotClipRect();
}

static bool RenderStockCrosshair(AppState& state,
                                 StockContext& ctx,
                                 int count,
                                 double panMinX,
                                 double panMaxX,
                                 bool plotHovered,
                                 bool hoverInteractive,
                                 float dt,
                                 const ImVec4& themeCol) {
    const ImVec2 tooltipMouse = ImGui::GetMousePos();
    const bool tooltipBoxHovered =
        ctx.render.tooltipRectVisible &&
        tooltipMouse.x >= ctx.render.tooltipRectMin.x &&
        tooltipMouse.x <= ctx.render.tooltipRectMax.x &&
        tooltipMouse.y >= ctx.render.tooltipRectMin.y &&
        tooltipMouse.y <= ctx.render.tooltipRectMax.y;
    const bool crosshairActive = plotHovered || tooltipBoxHovered;
    if (hoverInteractive && plotHovered && !tooltipBoxHovered) {
        const size_t nearestIndex = NearestSortedIndex(
            ctx.marketData.plot_sX, ImPlot::GetPlotMousePos().x, (size_t)count);
        ctx.render.hoverCrosshairX = ctx.marketData.plot_sX[nearestIndex];
        ctx.render.hoverCrosshairValid = true;
    }
    if (!crosshairActive)
        ctx.render.tooltipRectVisible = false;

    const float targetHover = crosshairActive ? 1.0f : 0.0f;
    if (!hoverInteractive) {
        ctx.render.hoverAlpha = 0.0f;
        ctx.render.tooltipWipeAnim = 0.0f;
        ctx.render.hoverCrosshairValid = false;
        ctx.render.tooltipRectVisible = false;
    } else if (state.UiAnimationsEnabled()) {
        ctx.render.hoverAlpha +=
            (targetHover - ctx.render.hoverAlpha) * (1.0f - std::exp(-28.0f * dt));
        ctx.render.tooltipWipeAnim +=
            (targetHover - ctx.render.tooltipWipeAnim) * (1.0f - std::exp(-32.0f * dt));
    } else {
        ctx.render.hoverAlpha = targetHover;
        ctx.render.tooltipWipeAnim = targetHover;
    }

    if (ctx.render.hoverAlpha <= 0.01f || !ctx.render.hoverCrosshairValid)
        return crosshairActive;

    const size_t nearestIndex = NearestSortedIndex(
        ctx.marketData.plot_sX, ctx.render.hoverCrosshairX, (size_t)count);
    const double dataX = ctx.marketData.plot_sX[nearestIndex];
    const double dataY = ctx.marketData.plot_sC[nearestIndex];
    const float crosshairLineX = ImPlot::PlotToPixels(dataX, 1.0).x;
    const ImU32 lineColor = ImGui::ColorConvertFloat4ToU32(
        ImVec4(themeCol.x, themeCol.y, themeCol.z, 0.6f * ctx.render.hoverAlpha));
    ImPlot::GetPlotDrawList()->AddLine(
        ImVec2(crosshairLineX, ImPlot::GetPlotPos().y),
        ImVec2(crosshairLineX, ImPlot::GetPlotPos().y + ImPlot::GetPlotSize().y),
        lineColor,
        1.0f);

    const ImVec2 point = ImPlot::PlotToPixels(dataX, dataY);
    ImDrawList* hoverDraw = ImPlot::GetPlotDrawList();
    hoverDraw->AddCircleFilled(
        point, 7.2f, ImGui::ColorConvertFloat4ToU32(ThemeVec(state.config.theme.bg)));
    hoverDraw->AddCircleFilled(
        point,
        5.1f,
        ImGui::ColorConvertFloat4ToU32(
            CrosshairDotColor(IsLightGuiTheme(state.config.themeModeIndex),
                              themeCol,
                              ctx.render.hoverAlpha)));

    if (ctx.render.tooltipWipeAnim <= 0.05f)
        return crosshairActive;

    const std::time_t timestamp = (std::time_t)dataX;
    std::tm tooltipTime =
        squarestar::platform::SafeTimeTm(timestamp, state.config.graphTimeZone == 1);
    char timeBuffer[64];
    const int formatIndex = std::clamp(ctx.navigation.displayedTimeRangeIndex, 0, 2);
    switch (formatIndex) {
    case 0:
        std::strftime(timeBuffer, sizeof(timeBuffer), "%I:%M %p", &tooltipTime);
        break;
    case 1:
        std::strftime(timeBuffer, sizeof(timeBuffer), "%b %d, %H:%M", &tooltipTime);
        break;
    default:
        std::strftime(timeBuffer, sizeof(timeBuffer), "%b %d, %Y", &tooltipTime);
        break;
    }
    std::string timeText = timeBuffer;
    if (!timeText.empty() && timeText[0] == '0')
        timeText.erase(0, 1);
    if (ctx.navigation.displayedTimeRangeIndex == 0) {
        timeText.erase(std::remove(timeText.begin(), timeText.end(), ' '),
                       timeText.end());
    }

    const std::string overlayText = FormatDouble(dataY) + " USD  |  " + timeText;
    const ImVec2 plotPos = ImPlot::GetPlotPos();
    constexpr float boxPadX = 10.0f;
    constexpr float boxPadY = 10.0f;
    ImGui::PushFont(state.render.fontData);
    const ImVec2 textSize = ImGui::CalcTextSize(overlayText.c_str());
    ImVec2 boxMin{};
    if (state.config.crosshairMode == 1 || state.config.crosshairMode == 2) {
        const double plotMidX = panMinX + (panMaxX - panMinX) / 2.0;
        boxMin.x = dataX > plotMidX
                       ? point.x - textSize.x - boxPadX * 2 - 15.0f
                       : point.x + 15.0f;
    }
    if (state.config.crosshairMode == 1) {
        const float totalBoxHeight = textSize.y + boxPadY * 2;
        boxMin.y = point.y - totalBoxHeight - 15.0f;
        if (boxMin.y < plotPos.y)
            boxMin.y = point.y + 15.0f;
    } else if (state.config.crosshairMode == 2) {
        boxMin.y = plotPos.y + boxPadY;
    } else {
        boxMin = ImVec2(plotPos.x + boxPadX, plotPos.y + 2.0f);
    }

    const ImVec2 boxSize(textSize.x + boxPadX * 2,
                         textSize.y + boxPadY * 2);
    const ImVec2 plotMax(plotPos.x + ImPlot::GetPlotSize().x,
                         plotPos.y + ImPlot::GetPlotSize().y);
    boxMin.x = std::clamp(
        boxMin.x,
        plotPos.x + 4.0f,
        std::max(plotPos.x + 4.0f, plotMax.x - boxSize.x - 4.0f));
    boxMin.y = std::clamp(
        boxMin.y,
        plotPos.y + 4.0f,
        std::max(plotPos.y + 4.0f, plotMax.y - boxSize.y - 4.0f));
    const ImVec2 boxMax(boxMin.x + boxSize.x, boxMin.y + boxSize.y);
    ctx.render.tooltipRectMin = boxMin;
    ctx.render.tooltipRectMax = boxMax;
    ctx.render.tooltipRectVisible = true;

    ImDrawList* drawList = ImPlot::GetPlotDrawList();
    const ImU32 bg =
        ImGui::ColorConvertFloat4ToU32(ThemeVec(state.config.theme.floatingBg, 1.0f));
    const ImU32 border = ImGui::ColorConvertFloat4ToU32(
        ThemeVec(state.config.theme.floatingBorder, 1.0f));
    drawList->AddRectFilled(boxMin, boxMax, bg);
    drawList->AddRect(boxMin, boxMax, border);
    const ImU32 textColor = ImGui::ColorConvertFloat4ToU32(
        ImVec4(themeCol.x, themeCol.y, themeCol.z, ctx.render.hoverAlpha));
    drawList->AddText(ImVec2(boxMin.x + boxPadX, boxMin.y + boxPadY),
                      textColor,
                      overlayText.c_str());
    ImGui::PopFont();
    return crosshairActive;
}

static void RenderStockPlot(AppState& state,
                            StockContext& ctx,
                            int& count,
                            float dt,
                            bool cleanGuiCapture,
                            ImVec2 stockCaptureMin,
                            double previousClose,
                            const ImVec4& themeCol,
                            float chartReveal,
                            const ImVec2& plotSize,
                            const ImVec2& chartCaptureMin,
                            const ImVec2& chartCaptureMax,
                            ImGuiViewport* chartViewport,
                            bool hoverInteractive,
                            bool contextMenuInteractive) {
    bool chartObjectFocused = false;
    double manualXAxisMin = 0.0, manualXAxisMax = 1.0;
    ImVec2 manualXAxisPlotPos{};
    ImVec2 manualXAxisPlotSize{};
    auto& xAxisCache = ctx.render.stockTimeAxisCache;
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    if (ImPlot::BeginPlot("##Chrt", plotSize, kFinancialPlotFlags)) {
        SetupLockedFinancialPlotAxes();
        RenderStockChartContextMenu(state,
                                    ctx,
                                    contextMenuInteractive,
                                    chartViewport,
                                    stockCaptureMin,
                                    chartCaptureMin,
                                    chartCaptureMax);
        RefreshStockChartData(state, ctx, count);
        const bool plotDataValid =
            count > 0 && ctx.marketData.plot_sX.size() >= (size_t)count &&
            ctx.marketData.plot_sC.size() >= (size_t)count &&
            (state.config.plotLineType != 0 ||
             (ctx.marketData.plot_sO.size() >= (size_t)count && ctx.marketData.plot_sH.size() >= (size_t)count &&
              ctx.marketData.plot_sL.size() >= (size_t)count));
        const bool prevLocalTime = ImPlot::GetStyle().UseLocalTime;
        if (plotDataValid) {
            const StockPlotGeometry geometry = ConfigureStockPlot(
                state, ctx, cleanGuiCapture, previousClose, chartViewport);
            manualXAxisMin = geometry.manualXAxisMin;
            manualXAxisMax = geometry.manualXAxisMax;

            ImPlot::GetStyle().UseLocalTime = (state.config.graphTimeZone == 0);
            RenderStockPriceSeries(
                state, ctx, geometry.lowerLimitY, themeCol, chartReveal);

            const bool plotHoveredNow = hoverInteractive && ImPlot::IsPlotHovered();
            manualXAxisPlotPos = ImPlot::GetPlotPos();
            manualXAxisPlotSize = ImPlot::GetPlotSize();
            UpdateStockXAxisHover(
                state, ctx, cleanGuiCapture, plotHoveredNow, dt);
            RenderPreviousCloseReference(state,
                                         ctx,
                                         count,
                                         previousClose,
                                         geometry.panMinX,
                                         geometry.panMaxX,
                                         plotHoveredNow,
                                         dt);
            chartObjectFocused = RenderStockCrosshair(state,
                                                      ctx,
                                                      count,
                                                      geometry.panMinX,
                                                      geometry.panMaxX,
                                                      plotHoveredNow,
                                                      hoverInteractive,
                                                      dt,
                                                      themeCol);
        }
        ImPlot::EndPlot();
        ImPlot::GetStyle().UseLocalTime = prevLocalTime;
    }
    ImGui::PopStyleVar();
    const int effectiveXAxisMode = cleanGuiCapture ? 2 : state.config.chartXAxisMode;
    if (effectiveXAxisMode != 1)
        DrawFadingXAxisLabels(state,
                              xAxisCache.ticks,
                              xAxisCache.labels,
                              manualXAxisMin,
                              manualXAxisMax,
                              manualXAxisPlotPos,
                              manualXAxisPlotSize,
                              chartCaptureMin.x,
                              chartCaptureMax.x,
                              cleanGuiCapture || effectiveXAxisMode == 0
                                  ? 1.0f
                                  : ctx.render.xAxisHoverAlpha);
    DrawObjectFocusOutline(state, chartCaptureMin, chartCaptureMax, chartObjectFocused, 2);
}

void RenderStockChart(AppState& state,
                             StockContext& ctx,
                             float dt,
                             bool shortcutClock,
                             bool cleanGuiCapture,
                             ImVec2 stockCaptureMin,
                             double pC,
                             const ImVec4& themeCol) {
    ImGui::BeginChild("ChartRegion",
                      ImVec2(0, 0),
                      false,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    if (!state.navigation.pureMonitorMode)
        ImGui::Spacing();
    int count = (int)ctx.RawData().closes.size();
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ThemeVec(state.config.theme.plotBg));
    ImGui::BeginChild("GraphArea",
                      ImVec2(0, 0),
                      false,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleColor();
    if (count > 0) {
        float chartReveal = 1.0f;
        if (state.UiAnimationsEnabled()) {
            if (ctx.render.dataJustLoaded) {
                ctx.render.chartRevealProgress = 0.0f;
                ctx.render.dataJustLoaded = false;
            }
            ctx.render.chartRevealProgress =
                std::min(1.0f, ctx.render.chartRevealProgress + UiFrameDelta() / 0.36f);
            if (ctx.render.chartRevealProgress < 0.999f)
                RequestGuiRedraw();
            chartReveal = EaseOutCubic(ctx.render.chartRevealProgress);
        } else {
            ctx.render.chartRevealProgress = 1.0f;
        }
        // Monitor mode still needs enough room for the left price labels and a
        // visibly separate time row. The extra label Y padding moves the time
        // axis down so its first label no longer collides with the Y axis.
        const bool denseMonitorChart =
            state.navigation.pureMonitorMode && (ImGui::GetWindowHeight() < 310.0f ||
                                      ImGui::GetWindowWidth() < 360.0f);
        ImPlot::PushStyleVar(
            ImPlotStyleVar_PlotPadding,
            denseMonitorChart ? ImVec2(18.0f, 18.0f)
                              : (state.navigation.pureMonitorMode ? ImVec2(28.0f, 32.0f)
                                                       : ImVec2(30.0f, 36.0f)));
        ImPlot::PushStyleVar(
            ImPlotStyleVar_LabelPadding,
            denseMonitorChart ? ImVec2(5.0f, 6.0f)
                              : (state.navigation.pureMonitorMode ? ImVec2(8.0f, 11.0f)
                                                       : ImVec2(7.0f, 14.0f)));
        ImPlot::PushStyleVar(ImPlotStyleVar_PlotBorderSize, 0.f);
        ImPlot::PushStyleColor(ImPlotCol_FrameBg, {0, 0, 0, 0});
        ImPlot::PushStyleColor(ImPlotCol_PlotBg, {0, 0, 0, 0});
        ImPlot::PushStyleColor(ImPlotCol_AxisGrid, ThemeVec(state.config.theme.grid));
        ImVec2 chartTotalSize = ImGui::GetContentRegionAvail();
        chartTotalSize.x = std::max(1.0f, chartTotalSize.x);
        // Keep export geometry identical to the live frame even though the clock
        // strip itself is omitted from the clean HD image.
        const float clockStripHeight = !state.navigation.pureMonitorMode ? 48.0f : 0.0f;
        const float monitorBottomBreathingRoom = denseMonitorChart ? 24.0f : 18.0f;
        if (state.navigation.pureMonitorMode) {
            // Never force an oversized graph into a short monitor tile. The old
            // 110px minimum made the plot extend past the tile and clipped its
            // price/time labels in dense 3-row layouts.
            chartTotalSize.y = std::max(1.0f, chartTotalSize.y - monitorBottomBreathingRoom);
        } else {
            chartTotalSize.y = std::max(110.0f, chartTotalSize.y - clockStripHeight);
        }
        const ImVec2 plotSize = chartTotalSize;
        const ImVec2 chartCaptureMin = ImGui::GetCursorScreenPos();
        const ImVec2 chartCaptureMax(chartCaptureMin.x + chartTotalSize.x,
                                     chartCaptureMin.y + chartTotalSize.y);
        ImGuiViewport* chartViewport = ImGui::GetWindowViewport();
        // Monitor tiles still need hover feedback even though they have no chart menu.
        const bool hoverInteractive = !cleanGuiCapture;
        const bool contextMenuInteractive = hoverInteractive && !state.navigation.pureMonitorMode;
        RenderStockPlot(state,
                        ctx,
                        count,
                        dt,
                        cleanGuiCapture,
                        stockCaptureMin,
                        pC,
                        themeCol,
                        chartReveal,
                        plotSize,
                        chartCaptureMin,
                        chartCaptureMax,
                        chartViewport,
                        hoverInteractive,
                        contextMenuInteractive);
        ImPlot::PopStyleColor(3);
        ImPlot::PopStyleVar(3);
        RenderStockChartFooter(state,
                               ctx,
                               shortcutClock,
                               cleanGuiCapture,
                               pC,
                               chartCaptureMin,
                               chartCaptureMax,
                               clockStripHeight);
    } else if (ctx.navigation.displayedTimeRangeIndex == 0 && state.config.graphTimeZone == 1 &&
               IsMarketOpeningWindow()) {
        ImGui::TextDisabled("Waiting for the first 1D market-time chart data...");
    } else
        ImGui::TextDisabled("No chart data available for this range.");
    ImGui::EndChild();
    ImGui::PopStyleVar(1);
    ImGui::EndChild();
}


} // namespace squarestar::shell
