#pragma once

#include "application/stock_render_cache.hpp"
#include "chart_types.hpp"

#include <string>
#include <vector>


namespace squarestar::market {
struct StockData;
}

namespace squarestar::presentation {

enum class ChartExportMethod : int { GuiCapture = 1, SourceData = 2 };
enum class ChartExportFormat { Png, Jpg, Pdf, Csv, Txt, Json, Unknown };

struct ChartCanvasModel {
    std::vector<double> x;
    std::vector<double> close;
    std::vector<double> open;
    std::vector<double> high;
    std::vector<double> low;
    double lo = 0.0;
    double hi = 1.0;
};

ChartCanvasModel BuildChartCanvasModel(
    const squarestar::market::StockData& data,
    ChartVisualType type,
    bool useMarketTime,
    bool padRange = true);

ChartExportFormat DetectChartExportFormat(const std::string& path);
bool ChartExportFormatAllowed(ChartExportMethod method, ChartExportFormat format) noexcept;
std::string FormatDataTimestampUtc(double timestamp, bool excelFriendly);
std::string EscapeCsvField(const std::string& value);
std::string EscapeSpreadsheetCsvField(std::string_view value);
std::string NormalizeSpreadsheetTextField(std::string_view value);
bool ExportComparisonData(const std::vector<squarestar::application::ComparisonSeries>& series,
                          const std::string& range,
                          const std::string& filename);

} // namespace squarestar::presentation
