#include "presentation/render_memory.hpp"

#include <vector>

#include "application/app_state.hpp"

namespace squarestar::presentation {
namespace {

template <typename T>
void ReleaseVectorStorage(std::vector<T>& values) {
    std::vector<T>().swap(values);
}

} // namespace

void ClearApplicationFontPointers(squarestar::application::AppRenderCache& state) {
    state.fontNormal = nullptr;
    state.fontData = nullptr;
    state.fontLarge = nullptr;
    state.fontGiant = nullptr;
    state.fontQuote = nullptr;
}

void ReleaseContextRenderMemory(squarestar::application::StockContext& context) {
    ReleaseVectorStorage(context.marketData.plot_sX);
    ReleaseVectorStorage(context.marketData.plot_sC);
    ReleaseVectorStorage(context.marketData.plot_sO);
    ReleaseVectorStorage(context.marketData.plot_sH);
    ReleaseVectorStorage(context.marketData.plot_sL);
    ReleaseVectorStorage(context.render.render_sX);
    ReleaseVectorStorage(context.render.render_sC);
    ReleaseVectorStorage(context.render.renderCandle_sX);
    ReleaseVectorStorage(context.render.renderCandle_sC);
    ReleaseVectorStorage(context.render.renderCandle_sO);
    ReleaseVectorStorage(context.render.renderCandle_sH);
    ReleaseVectorStorage(context.render.renderCandle_sL);
    ReleaseVectorStorage(context.render.renderShade_sX);
    ReleaseVectorStorage(context.render.renderShade_sC);
    context.render.renderLodSourceCount = 0;
    context.render.renderLodTarget = 0;
    context.render.renderShadeLodTarget = 0;
    context.render.renderLodLineType = -1;
    context.render.priceAxisCache = {};
    context.render.stockTimeAxisCache = {};
    context.render.comparisonTimeAxisCache = {};
    ReleaseVectorStorage(context.render.comparisonCache);
    ReleaseVectorStorage(context.render.comparisonYAxisTicks);
    ReleaseVectorStorage(context.render.comparisonYAxisLabels);
    ReleaseVectorStorage(context.render.comparisonYAxisLabelPtrs);
    ReleaseVectorStorage(context.render.comparisonHoverSamples);
    context.render.comparisonCacheSignature = 0;
    context.render.comparisonYAxisValid = false;
    context.render.comparisonYAxisSignature = 0;
    context.marketData.needsPlotDataUpdate = true;
}

void ReleaseGuiStateRenderMemory(squarestar::application::AppState& state) {
    const auto release = [](auto& contexts) {
        for (auto& candidate : contexts) {
            if (candidate)
                ReleaseContextRenderMemory(*candidate);
        }
    };
    release(state.marketData.activeContexts);
    release(state.marketData.retiredLiteContexts);
    release(state.alerts.Monitors());
}


} // namespace squarestar::presentation
