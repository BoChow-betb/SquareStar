#pragma once

#include <cstddef>
#include <vector>

#include "application/stock_context.hpp"

namespace squarestar::presentation {

struct ChartRenderLodTargets {
    std::size_t line = 0;
    std::size_t shade = 0;
};

void BuildLttbSeries(const std::vector<double>& sourceX,
                     const std::vector<double>& sourceY,
                     std::size_t threshold,
                     std::vector<double>& outputX,
                     std::vector<double>& outputY);
void BuildLttbSeries(const std::vector<double>& sourceX,
                     const std::vector<double>& sourceY,
                     std::size_t threshold,
                     std::vector<float>& outputX,
                     std::vector<float>& outputY);
ChartRenderLodTargets CalculateChartRenderLodTargets(int lineType,
                                                      float logicalWidth,
                                                      float rawFramebufferScale);
void ResetChartRenderLod(squarestar::application::StockContext& context);
void BuildChartRenderLod(squarestar::application::StockContext& context,
                         int lineType,
                         float pixelWidth,
                         float framebufferScale);

} // namespace squarestar::presentation
