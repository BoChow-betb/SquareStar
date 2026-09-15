#include "presentation/chart_lod.hpp"

#include <algorithm>
#include <cmath>

namespace squarestar::presentation {
namespace {

template <typename Output>
void BuildLttbSeriesAs(const std::vector<double>& sourceX,
                       const std::vector<double>& sourceY,
                       std::size_t threshold,
                       std::vector<Output>& outputX,
                       std::vector<Output>& outputY) {
    outputX.clear();
    outputY.clear();
    const std::size_t count = std::min(sourceX.size(), sourceY.size());
    if (count == 0)
        return;
    if (threshold < 3 || count <= threshold) {
        outputX.reserve(count);
        outputY.reserve(count);
        for (std::size_t i = 0; i < count; ++i) {
            outputX.push_back(static_cast<Output>(sourceX[i]));
            outputY.push_back(static_cast<Output>(sourceY[i]));
        }
        return;
    }
    outputX.reserve(threshold);
    outputY.reserve(threshold);
    outputX.push_back(static_cast<Output>(sourceX.front()));
    outputY.push_back(static_cast<Output>(sourceY.front()));
    const double bucketWidth = static_cast<double>(count - 2) /
                               static_cast<double>(threshold - 2);
    std::size_t selected = 0;
    for (std::size_t bucket = 0; bucket < threshold - 2; ++bucket) {
        const std::size_t averageBegin = std::min(
            count - 1,
            static_cast<std::size_t>(std::floor(static_cast<double>(bucket + 1) *
                                                bucketWidth)) +
                1);
        const std::size_t averageEnd = std::min(
            count,
            static_cast<std::size_t>(std::floor(static_cast<double>(bucket + 2) *
                                                bucketWidth)) +
                1);
        double averageX = sourceX[averageBegin];
        double averageY = sourceY[averageBegin];
        if (averageEnd > averageBegin) {
            averageX = 0.0;
            averageY = 0.0;
            for (std::size_t i = averageBegin; i < averageEnd; ++i) {
                averageX += sourceX[i];
                averageY += sourceY[i];
            }
            const double divisor = static_cast<double>(averageEnd - averageBegin);
            averageX /= divisor;
            averageY /= divisor;
        }
        const std::size_t rangeBegin = std::min(
            count - 1,
            static_cast<std::size_t>(std::floor(static_cast<double>(bucket) *
                                                bucketWidth)) +
                1);
        const std::size_t rangeEnd = std::min(
            count - 1,
            static_cast<std::size_t>(std::floor(static_cast<double>(bucket + 1) *
                                                bucketWidth)) +
                1);
        const double anchorX = sourceX[selected];
        const double anchorY = sourceY[selected];
        double largestArea = -1.0;
        std::size_t nextSelected = rangeBegin;
        for (std::size_t i = rangeBegin; i < rangeEnd; ++i) {
            const double area = std::abs(
                (anchorX - averageX) * (sourceY[i] - anchorY) -
                (anchorX - sourceX[i]) * (averageY - anchorY));
            if (area > largestArea) {
                largestArea = area;
                nextSelected = i;
            }
        }
        selected = nextSelected;
        outputX.push_back(static_cast<Output>(sourceX[selected]));
        outputY.push_back(static_cast<Output>(sourceY[selected]));
    }
    outputX.push_back(static_cast<Output>(sourceX[count - 1]));
    outputY.push_back(static_cast<Output>(sourceY[count - 1]));
}

} // namespace

void BuildLttbSeries(const std::vector<double>& sourceX,
                     const std::vector<double>& sourceY,
                     std::size_t threshold,
                     std::vector<double>& outputX,
                     std::vector<double>& outputY) {
    BuildLttbSeriesAs(sourceX, sourceY, threshold, outputX, outputY);
}

void BuildLttbSeries(const std::vector<double>& sourceX,
                     const std::vector<double>& sourceY,
                     std::size_t threshold,
                     std::vector<float>& outputX,
                     std::vector<float>& outputY) {
    BuildLttbSeriesAs(sourceX, sourceY, threshold, outputX, outputY);
}

ChartRenderLodTargets CalculateChartRenderLodTargets(int lineType,
                                                      float logicalWidth,
                                                      float rawFramebufferScale) {
    const float safeWidth =
        std::isfinite(logicalWidth) ? std::max(1.0f, logicalWidth) : 1.0f;
    if (lineType == 0) {
        const std::size_t candles = static_cast<std::size_t>(
            std::clamp(std::ceil(safeWidth * 0.45f), 128.0f, 2048.0f));
        return {((candles + 63) / 64) * 64, 0};
    }
    const float framebufferScale = std::isfinite(rawFramebufferScale)
                                       ? std::clamp(rawFramebufferScale, 1.0f, 2.0f)
                                       : 1.0f;
    const float physicalWidth = safeWidth * framebufferScale;
    const std::size_t line = static_cast<std::size_t>(
        std::clamp(std::ceil(physicalWidth * 0.72f), 256.0f, 3072.0f));
    const std::size_t shade = static_cast<std::size_t>(
        std::clamp(std::ceil(physicalWidth * 0.28f), 192.0f, 1536.0f));
    return {((line + 63) / 64) * 64, ((shade + 63) / 64) * 64};
}

void ResetChartRenderLod(squarestar::application::StockContext& ctx) {
    ctx.render.render_sX.clear();
    ctx.render.render_sC.clear();
    ctx.render.renderCandle_sX.clear();
    ctx.render.renderCandle_sC.clear();
    ctx.render.renderCandle_sO.clear();
    ctx.render.renderCandle_sH.clear();
    ctx.render.renderCandle_sL.clear();
    ctx.render.renderShade_sX.clear();
    ctx.render.renderShade_sC.clear();
    ctx.render.renderLodSourceCount = 0;
    ctx.render.renderLodTarget = 0;
    ctx.render.renderShadeLodTarget = 0;
    ctx.render.renderLodLineType = -1;
}

void BuildChartRenderLod(squarestar::application::StockContext& ctx,
                         int lineType,
                         float pixelWidth,
                         float framebufferScale) {
    const auto& sourceX = ctx.marketData.PlotX();
    const auto& raw = ctx.RawData();
    const auto& sourceC = raw.closes;
    const std::size_t count = std::min(sourceX.size(), sourceC.size());
    const ChartRenderLodTargets targets =
        CalculateChartRenderLodTargets(lineType, pixelWidth, framebufferScale);
    if (ctx.render.renderLodSourceCount == count && ctx.render.renderLodTarget == targets.line &&
        ctx.render.renderShadeLodTarget == targets.shade && ctx.render.renderLodLineType == lineType)
        return;
    ResetChartRenderLod(ctx);
    ctx.render.renderLodSourceCount = count;
    ctx.render.renderLodTarget = targets.line;
    ctx.render.renderShadeLodTarget = targets.shade;
    ctx.render.renderLodLineType = lineType;
    if (lineType != 0) {
        if (count > targets.line)
            BuildLttbSeries(sourceX, sourceC, targets.line, ctx.render.render_sX, ctx.render.render_sC);
        if (count > targets.shade)
            BuildLttbSeries(sourceX, sourceC, targets.shade,
                            ctx.render.renderShade_sX, ctx.render.renderShade_sC);
        return;
    }
    if (count <= targets.line)
        return;
    if (raw.opens.size() < count || raw.highs.size() < count || raw.lows.size() < count)
        return;
    const std::size_t bucketSize = std::max<std::size_t>(1, (count + targets.line - 1) / targets.line);
    const std::size_t bucketCount = (count + bucketSize - 1) / bucketSize;
    ctx.render.renderCandle_sX.reserve(bucketCount);
    ctx.render.renderCandle_sC.reserve(bucketCount);
    ctx.render.renderCandle_sO.reserve(bucketCount);
    ctx.render.renderCandle_sH.reserve(bucketCount);
    ctx.render.renderCandle_sL.reserve(bucketCount);
    for (std::size_t begin = 0; begin < count; begin += bucketSize) {
        const std::size_t end = std::min(count, begin + bucketSize);
        double high = raw.highs[begin];
        double low = raw.lows[begin];
        for (std::size_t i = begin + 1; i < end; ++i) {
            high = std::max(high, raw.highs[i]);
            low = std::min(low, raw.lows[i]);
        }
        ctx.render.renderCandle_sX.push_back(sourceX[begin + (end - begin - 1) / 2]);
        ctx.render.renderCandle_sO.push_back(static_cast<float>(raw.opens[begin]));
        ctx.render.renderCandle_sH.push_back(static_cast<float>(high));
        ctx.render.renderCandle_sL.push_back(static_cast<float>(low));
        ctx.render.renderCandle_sC.push_back(static_cast<float>(sourceC[end - 1]));
    }
}

} // namespace squarestar::presentation
