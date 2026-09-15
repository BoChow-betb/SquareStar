#include "presentation/render_memory.hpp"

#include <vector>

#include "application/app_state.hpp"

#include "imgui_internal.h"
#ifdef _WIN32
#include "implot.h"
#include "implot_internal.h"
#endif

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
    state.fontLaunch = nullptr;
}

void ReleaseContextRenderMemory(squarestar::application::StockContext& context) {
    ReleaseVectorStorage(context.marketData.plotMarketTimeX);
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

void CompactGuiTransientMemory() {
    // SquareStar is event-driven: after a few idle seconds the previous frame's
    // CPU draw buffers are no longer needed. Dear ImGui normally compacts only
    // windows that became inactive, which means the main active window can keep
    // the largest chart/table draw allocation indefinitely. Compact all window
    // transient buffers while idle; Begin() automatically wakes them on demand.
    if (GImGui) {
        for (ImGuiWindow* window : GImGui->Windows) {
            if (window && !window->MemoryCompacted)
                ImGui::GcCompactTransientWindowBuffers(window);
        }
        ImGui::GcCompactTransientMiscBuffers();
    }

#ifdef _WIN32
    // ImPlot keeps several general-purpose scratch vectors at their historical
    // peak capacity. They contain no persistent plot state and are rebuilt on
    // demand, so release them during the same idle maintenance pass.
    if (ImPlotContext* plot = ImPlot::GetCurrentContext()) {
        plot->TempDouble1.clear();
        plot->TempDouble2.clear();
        plot->TempInt1.clear();
        plot->CTicker.Ticks.clear();
        plot->CTicker.TextBuffer.Buf.clear();
        plot->Annotations.Annotations.clear();
        plot->Annotations.TextBuffer.Buf.clear();
        plot->Annotations.Size = 0;
        plot->Tags.Tags.clear();
        plot->Tags.TextBuffer.Buf.clear();
        plot->Tags.Size = 0;
    }
#endif
}

} // namespace squarestar::presentation
