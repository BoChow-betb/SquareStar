#pragma once

#include <cstdint>
#include <ctime>
#include <utility>
#include <vector>
#include <string>

#include "application/stock_render_cache.hpp"

namespace squarestar::presentation {

void RefreshLabelPointers(const std::vector<std::string>& labels,
                          std::vector<const char*>& labelPtrs);

struct TimeAxisFormatContext {
    int rangeIndex = 0;
    bool marketTime = true;
    double axisOrigin = 0.0;
    double axisSecondsPerUnit = 1.0;
};

std::pair<double, double> OneDayMarketSessionBounds(std::time_t rawAnchor,
                                                    bool marketTime);

void UpdateStableChartAxisLock(application::StockRenderCache& cache,
                               int rangeIndex,
                               double firstTs,
                               double lastTs,
                               bool marketTime,
                               std::time_t rawAnchor,
                               bool regularEquitySession);

int FormatTimeAxisValue(double value, char* buffer, int size, void* userData);

void BuildConfiguredTimeAxisTicks(const std::vector<double>& values,
                                  double minX,
                                  double maxX,
                                  int rangeIndex,
                                  bool marketTime,
                                  int xAxisMode,
                                  std::vector<double>& ticks,
                                  std::vector<std::string>& labels,
                                  std::vector<const char*>& labelPtrs,
                                  double axisOrigin = 0.0,
                                  double axisSecondsPerUnit = 1.0);

void EnsureConfiguredTimeAxisTicks(application::TimeAxisTickCache& cache,
                                   std::uint64_t dataRevision,
                                   const std::vector<double>& values,
                                   double minX,
                                   double maxX,
                                   int rangeIndex,
                                   bool marketTime,
                                   int xAxisMode,
                                   double axisOrigin = 0.0,
                                   double axisSecondsPerUnit = 1.0);

}
