#include "presentation/chart_price_axis.hpp"

#include "application/stock_context.hpp"
#include "presentation/chart_axis.hpp"
#include "presentation/chart_y_axis.hpp"

#include <algorithm>
#include <cmath>
#include <compare>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

namespace squarestar::presentation {
namespace {

bool SameCacheDouble(double left, double right) noexcept {
    return (left <=> right) == std::partial_ordering::equivalent;
}

bool BuildVisiblePriceTicks(const application::StockContext& context,
                            bool candlestick,
                            double visibleMinX,
                            double visibleMaxX,
                            double requiredPrice,
                            std::vector<double>& ticks,
                            std::vector<std::string>& labels,
                            std::vector<const char*>& labelPtrs,
                            double& visibleMinPrice,
                            double& visibleMaxPrice,
                            double axisPaddingFraction) {
    ticks.clear();
    labels.clear();
    labelPtrs.clear();
    visibleMinPrice = std::numeric_limits<double>::max();
    visibleMaxPrice = -std::numeric_limits<double>::max();
    const auto& sourceX = context.marketData.PlotX();
    const auto& raw = context.RawData();
    const std::size_t count = std::min(sourceX.size(), raw.closes.size());
    const auto valuesBegin = sourceX.begin();
    const auto valuesEnd = valuesBegin + static_cast<std::ptrdiff_t>(count);
    const std::size_t firstVisible = static_cast<std::size_t>(
        std::distance(valuesBegin, std::lower_bound(valuesBegin, valuesEnd, visibleMinX)));
    const std::size_t lastVisible = static_cast<std::size_t>(
        std::distance(valuesBegin, std::upper_bound(valuesBegin, valuesEnd, visibleMaxX)));
    for (std::size_t i = firstVisible; i < lastVisible; ++i) {
        const double lo = candlestick && i < raw.lows.size() ? raw.lows[i] : raw.closes[i];
        const double hi = candlestick && i < raw.highs.size() ? raw.highs[i] : raw.closes[i];
        if (!std::isfinite(lo) || !std::isfinite(hi))
            continue;
        visibleMinPrice = std::min(visibleMinPrice, lo);
        visibleMaxPrice = std::max(visibleMaxPrice, hi);
    }
    if (!std::isfinite(visibleMinPrice) || !std::isfinite(visibleMaxPrice))
        return false;
    if (ShouldIncludeReferencePriceInVisibleAxis(
            visibleMinPrice, visibleMaxPrice, requiredPrice)) {
        visibleMinPrice = std::min(visibleMinPrice, requiredPrice);
        visibleMaxPrice = std::max(visibleMaxPrice, requiredPrice);
    }
    const UnifiedYAxis yAxis = CalculateUnifiedYAxis(
        visibleMinPrice, visibleMaxPrice, axisPaddingFraction, true);
    visibleMinPrice = yAxis.bottom;
    visibleMaxPrice = yAxis.top;
    for (double value : yAxis.ticks) {
        const std::string label = FormatAxisTickValue(value);
        if (!labels.empty() && labels.back() == label)
            continue;
        ticks.push_back(value);
        labels.push_back(label);
    }
    RefreshLabelPointers(labels, labelPtrs);
    return true;
}

} // namespace

bool ShouldIncludeReferencePriceInVisibleAxis(double visibleMinPrice,
                                              double visibleMaxPrice,
                                              double referencePrice) {
    if (!std::isfinite(visibleMinPrice) || !std::isfinite(visibleMaxPrice) ||
        !std::isfinite(referencePrice) || referencePrice <= 0.0)
        return false;
    if (visibleMaxPrice < visibleMinPrice)
        std::swap(visibleMinPrice, visibleMaxPrice);
    if (referencePrice >= visibleMinPrice && referencePrice <= visibleMaxPrice)
        return true;

    const double visibleSpan = std::max(visibleMaxPrice - visibleMinPrice, 0.0);
    const double scale = std::max({std::abs(visibleMinPrice),
                                   std::abs(visibleMaxPrice),
                                   1.0});
    // Previous-close/reference lines are useful when they are near the traded
    // range, but a large overnight gap should not make an otherwise stable
    // intraday chart mostly empty. The 2% floor still keeps nearby references
    // visible when the session itself is exceptionally flat.
    const double referenceGuard = std::max(visibleSpan * 1.5, scale * 0.02);
    return referencePrice >= visibleMinPrice - referenceGuard &&
           referencePrice <= visibleMaxPrice + referenceGuard;
}

double ResolveVisibleReferenceLineY(double projectedY,
                                    double plotTop,
                                    double plotBottom,
                                    double labelHeight) noexcept {
    if (!std::isfinite(projectedY) || !std::isfinite(plotTop) ||
        !std::isfinite(plotBottom))
        return projectedY;
    if (plotBottom < plotTop)
        std::swap(plotTop, plotBottom);
    if (!(plotBottom > plotTop))
        return plotTop;
    if (projectedY >= plotTop && projectedY <= plotBottom)
        return projectedY;

    constexpr double borderInset = 1.5;
    constexpr double labelToLineGap = 11.0;
    const double insideTop = std::min(plotBottom, plotTop + borderInset);
    const double insideBottom = std::max(insideTop, plotBottom - borderInset);
    if (projectedY > plotBottom)
        return insideBottom;

    const double safeLabelHeight =
        std::isfinite(labelHeight) ? std::max(labelHeight, 0.0) : 0.0;
    // The pinned label starts six pixels below the plot edge. Place the line
    // just beneath that block so both remain visible without implying that the
    // far-away close is part of the plotted price range.
    return std::clamp(
        plotTop + safeLabelHeight + labelToLineGap, insideTop, insideBottom);
}

void EnsureVisiblePriceTicks(application::StockContext& context,
                             bool candlestick,
                             double visibleMinX,
                             double visibleMaxX,
                             double requiredPrice,
                             double axisPaddingFraction) {
    application::PriceAxisTickCache& cache = context.render.priceAxisCache;
    const std::size_t sourceCount =
        std::min(context.marketData.PlotX().size(), context.RawData().closes.size());
    if (cache.valid && cache.dataRevision == context.marketData.dataRevision &&
        cache.sourceCount == sourceCount && cache.candlestick == candlestick &&
        SameCacheDouble(cache.visibleMinX, visibleMinX) &&
        SameCacheDouble(cache.visibleMaxX, visibleMaxX) &&
        SameCacheDouble(cache.requiredPrice, requiredPrice) &&
        SameCacheDouble(cache.paddingFraction, axisPaddingFraction))
        return;
    cache.valid = true;
    cache.dataRevision = context.marketData.dataRevision;
    cache.sourceCount = sourceCount;
    cache.candlestick = candlestick;
    cache.visibleMinX = visibleMinX;
    cache.visibleMaxX = visibleMaxX;
    cache.requiredPrice = requiredPrice;
    cache.paddingFraction = axisPaddingFraction;
    if (BuildVisiblePriceTicks(context,
                               candlestick,
                               visibleMinX,
                               visibleMaxX,
                               requiredPrice,
                               cache.ticks,
                               cache.labels,
                               cache.labelPtrs,
                               cache.axisMin,
                               cache.axisMax,
                               axisPaddingFraction))
        return;
    const UnifiedYAxis fallbackAxis = CalculateUnifiedYAxis(
        context.marketData.plot_miY, context.marketData.plot_maY, axisPaddingFraction, true);
    cache.axisMin = fallbackAxis.bottom;
    cache.axisMax = fallbackAxis.top;
    cache.ticks.clear();
    cache.labels.clear();
    cache.labelPtrs.clear();
    for (double value : fallbackAxis.ticks) {
        const std::string label = FormatAxisTickValue(value);
        if (!cache.labels.empty() && cache.labels.back() == label)
            continue;
        cache.ticks.push_back(value);
        cache.labels.push_back(label);
    }
    RefreshLabelPointers(cache.labels, cache.labelPtrs);
}

} // namespace squarestar::presentation
