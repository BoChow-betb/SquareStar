#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "imgui.h"

namespace squarestar::application {

struct ComparisonSeries {
    std::string symbol;
    std::vector<double> x;
    std::vector<double> rawX;
    std::vector<double> percent;
    std::vector<float> plotX;
    std::vector<float> plotPercent;
    double minPercent = std::numeric_limits<double>::infinity();
    double maxPercent = -std::numeric_limits<double>::infinity();
    ImVec4 color{};
};

struct PriceAxisTickCache {
    bool valid = false;
    uint64_t dataRevision = 0;
    size_t sourceCount = 0;
    bool candlestick = false;
    double visibleMinX = 0.0;
    double visibleMaxX = 0.0;
    double requiredPrice = 0.0;
    double paddingFraction = 0.0;
    double axisMin = 0.0;
    double axisMax = 1.0;
    std::vector<double> ticks;
    std::vector<std::string> labels;
    std::vector<const char*> labelPtrs;
};

struct TimeAxisTickCache {
    bool valid = false;
    uint64_t dataRevision = 0;
    size_t sourceCount = 0;
    double sourceFirst = 0.0;
    double sourceLast = 0.0;
    double minX = 0.0;
    double maxX = 0.0;
    double axisOrigin = 0.0;
    double axisSecondsPerUnit = 1.0;
    int rangeIndex = -1;
    bool marketTime = false;
    int mode = -1;
    std::vector<double> ticks;
    std::vector<std::string> labels;
    std::vector<const char*> labelPtrs;
};

struct ComparisonHoverSample {
    size_t seriesIndex = 0;
    std::array<char, 24> valueText{};
    ImVec2 point{};
};

struct StockRenderCache {
    std::vector<double> render_sX, render_sC;
    std::vector<double> renderCandle_sX;
    std::vector<float> renderCandle_sC, renderCandle_sO, renderCandle_sH,
        renderCandle_sL;
    std::vector<double> renderShade_sX, renderShade_sC;
    size_t renderLodSourceCount = 0;
    size_t renderLodTarget = 0;
    size_t renderShadeLodTarget = 0;
    int renderLodLineType = -1;
    PriceAxisTickCache priceAxisCache;
    TimeAxisTickCache stockTimeAxisCache;
    TimeAxisTickCache comparisonTimeAxisCache;
    bool soundPanPlayed = false;
    std::string transientNoticeTitle;
    std::string transientNoticeMessage;
    std::chrono::steady_clock::time_point transientNoticeUntil{};
    float transientNoticeAnim = 0.0f;
    float animProgress = 0.0f;
    float fadeAlpha = 0.0f;
    float priceFlashAnim = 0.0f;
    double lastTrackedPrice = 0.0;
    ImVec4 flashColor = ImVec4(0, 0, 0, 0);
    int lockedAxisRangeIndex = -1;
    double lockedAxisMinX = 0.0;
    double lockedAxisMaxX = 0.0;
    int lastPlotLineType = -1;
    float tabFadeAnim = 1.0f;
    float loadingBlockAnim = 0.0f;
    float openTransitionProgress = 0.0f;
    bool dataJustLoaded = false;
    bool suppressInitialLoadPresentation = false;
    float chartRevealProgress = 0.0f;
    double displayPrice = 0.0;
    float hoverAlpha = 0.0f;
    float tooltipWipeAnim = 0.0f;
    float prevCloseLabelAlpha = 1.0f;
    float xAxisHoverAlpha = 0.0f;
    float comparisonXAxisHoverAlpha = 0.0f;
    double hoverCrosshairX = 0.0;
    bool hoverCrosshairValid = false;
    bool tooltipRectVisible = false;
    ImVec2 tooltipRectMin{};
    ImVec2 tooltipRectMax{};
    double comparisonCrosshairX = 0.0;
    bool comparisonCrosshairValid = false;
    bool comparisonTooltipRectVisible = false;
    ImVec2 comparisonTooltipRectMin{};
    ImVec2 comparisonTooltipRectMax{};
    size_t comparisonCacheSignature = 0;
    double comparisonAxisOrigin = 0.0;
    std::vector<ComparisonSeries> comparisonCache;
    double comparisonDataMinX = std::numeric_limits<double>::infinity();
    double comparisonDataMaxX = -std::numeric_limits<double>::infinity();
    double comparisonDataMinY = std::numeric_limits<double>::infinity();
    double comparisonDataMaxY = -std::numeric_limits<double>::infinity();
    bool comparisonYAxisValid = false;
    size_t comparisonYAxisSignature = 0;
    std::vector<double> comparisonYAxisTicks;
    std::vector<std::string> comparisonYAxisLabels;
    std::vector<const char*> comparisonYAxisLabelPtrs;
    std::vector<ComparisonHoverSample> comparisonHoverSamples;
};

}
