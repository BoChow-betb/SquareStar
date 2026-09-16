#pragma once

#include <vector>

#include "application/stock_render_cache.hpp"

namespace squarestar::application {
struct AppState;
struct StockContext;
}

namespace squarestar::presentation {

const std::vector<application::ComparisonSeries>&
BuildComparisonSeries(const application::AppState& state,
                      application::StockContext& primary);

void EnsureComparisonYAxisTicks(application::StockContext& primary,
                                double& minY,
                                double& maxY);

}
