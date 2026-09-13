#include "chart_export.hpp"

#include "domain/json_text.hpp"
#include "domain/stock_data.hpp"
#include "domain/market_calendar.hpp"
#include "domain/text.hpp"
#include "platform/windows_path.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <limits>
#include <string_view>

namespace squarestar::presentation {
namespace {

std::tm SafeUtcTm(std::time_t raw) noexcept {
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &raw);
#else
    gmtime_r(&raw, &tm);
#endif
    return tm;
}

} // namespace

ChartCanvasModel BuildChartCanvasModel(
    const squarestar::market::StockData& data,
    ChartVisualType type,
    bool useMarketTime,
    bool padRange) {
    ChartCanvasModel canvas;
    const std::size_t count = std::min(data.timestamps.size(), data.closes.size());
    const bool needsOhlc = type == ChartVisualType::Candlestick;
    canvas.x.reserve(count);
    canvas.close.reserve(count);
    if (needsOhlc) {
        canvas.open.reserve(count);
        canvas.high.reserve(count);
        canvas.low.reserve(count);
    }
    canvas.lo = std::numeric_limits<double>::infinity();
    canvas.hi = -std::numeric_limits<double>::infinity();
    for (std::size_t index = 0; index < count; ++index) {
        if (!std::isfinite(data.timestamps[index]) || data.timestamps[index] <= 0.0)
            continue;
        const double close = data.closes[index];
        if (!std::isfinite(close) || close <= 0.0)
            continue;
        const double open =
            index < data.opens.size() && data.opens[index] > 0.0
                ? data.opens[index]
                : close;
        const double high =
            index < data.highs.size() && data.highs[index] > 0.0
                ? data.highs[index]
                : std::max(open, close);
        const double low =
            index < data.lows.size() && data.lows[index] > 0.0
                ? data.lows[index]
                : std::min(open, close);
        const std::time_t pointTime = static_cast<std::time_t>(data.timestamps[index]);
        canvas.x.push_back(
            data.timestamps[index] +
            (useMarketTime ? squarestar::market::NewYorkUtcOffsetSeconds(pointTime) : 0));
        canvas.close.push_back(close);
        if (needsOhlc) {
            canvas.open.push_back(open);
            canvas.high.push_back(high);
            canvas.low.push_back(low);
        }
        canvas.lo = std::min(canvas.lo, needsOhlc ? low : close);
        canvas.hi = std::max(canvas.hi, needsOhlc ? high : close);
    }
    if (!std::isfinite(canvas.lo) || !std::isfinite(canvas.hi)) {
        canvas.lo = 0.0;
        canvas.hi = 1.0;
    }
    const double exportPrevious = data.chartPreviousClose > 0.0
                                      ? data.chartPreviousClose
                                      : data.previousClose;
    if (padRange && std::isfinite(exportPrevious) && exportPrevious > 0.0) {
        canvas.lo = std::min(canvas.lo, exportPrevious);
        canvas.hi = std::max(canvas.hi, exportPrevious);
    }
    if (std::abs(canvas.hi - canvas.lo) < 1e-9) {
        canvas.lo -= 1.0;
        canvas.hi += 1.0;
    }
    if (padRange) {
        const double span = canvas.hi - canvas.lo;
        const double center = (canvas.hi + canvas.lo) * 0.5;
        const double instability = span / std::max({std::abs(center), span, 1e-9});
        const double adaptive = std::clamp(instability / 0.08, 0.0, 1.0);
        const double lowerPadding =
            std::max(span * (0.08 + adaptive * 0.03), std::abs(center) * 0.0025);
        const double upperPadding =
            std::max(span * (0.14 + adaptive * 0.08), std::abs(center) * 0.0040);
        canvas.lo = std::max(0.0, canvas.lo - lowerPadding);
        canvas.hi += upperPadding;
    }
    return canvas;
}

ChartExportFormat DetectChartExportFormat(const std::string& path) {
    const std::size_t dot = path.find_last_of('.');
    if (dot == std::string::npos)
        return ChartExportFormat::Unknown;
    std::string ext = path.substr(dot);
    squarestar::text::LowercaseInPlace(ext);
    static constexpr std::array<std::pair<std::string_view, ChartExportFormat>, 7> formats{{
        {".png", ChartExportFormat::Png},
        {".jpg", ChartExportFormat::Jpg},
        {".jpeg", ChartExportFormat::Jpg},
        {".pdf", ChartExportFormat::Pdf},
        {".csv", ChartExportFormat::Csv},
        {".txt", ChartExportFormat::Txt},
        {".json", ChartExportFormat::Json},
    }};
    const auto found = std::find_if(formats.begin(), formats.end(),
                                    [&](const auto& entry) { return entry.first == ext; });
    return found == formats.end() ? ChartExportFormat::Unknown : found->second;
}

bool ChartExportFormatAllowed(ChartExportMethod method, ChartExportFormat format) noexcept {
    switch (method) {
    case ChartExportMethod::GuiCapture:
        return format == ChartExportFormat::Png || format == ChartExportFormat::Jpg ||
               format == ChartExportFormat::Pdf;
    case ChartExportMethod::SourceData:
        return format == ChartExportFormat::Csv || format == ChartExportFormat::Txt ||
               format == ChartExportFormat::Json;
    }
    return false;
}

std::string FormatDataTimestampUtc(double timestamp, bool excelFriendly) {
    if (!std::isfinite(timestamp) || timestamp <= 0.0)
        return {};
    const std::time_t raw = static_cast<std::time_t>(std::llround(timestamp));
    const std::tm tm = SafeUtcTm(raw);
    char buffer[40]{};
    std::strftime(buffer, sizeof(buffer),
                  excelFriendly ? "%Y-%m-%d %H:%M:%S" : "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buffer;
}

std::string EscapeCsvField(const std::string& value) {
    if (value.find_first_of(",\"\r\n") == std::string::npos)
        return value;
    std::string escaped;
    escaped.reserve(value.size() + 4);
    escaped.push_back('"');
    for (char c : value) {
        if (c == '"')
            escaped += "\"\"";
        else
            escaped.push_back(c);
    }
    escaped.push_back('"');
    return escaped;
}

namespace {

bool SpreadsheetFormulaPrefix(char value) noexcept {
    return value == '=' || value == '+' || value == '-' || value == '@';
}

bool SpreadsheetWhitespace(char value) noexcept {
    return value == ' ' || value == '\t' || value == '\r' || value == '\n' ||
           value == '\v' || value == '\f';
}

std::string NormalizeSpreadsheetText(std::string_view value) {
    std::string normalized;
    normalized.reserve(value.size() + 1);
    for (const char raw : value) {
        const unsigned char byte = static_cast<unsigned char>(raw);
        if (byte < 0x20 || byte == 0x7f)
            normalized.push_back(' ');
        else
            normalized.push_back(raw);
    }

    const auto firstContent = std::find_if_not(
        normalized.begin(), normalized.end(), SpreadsheetWhitespace);
    if (firstContent != normalized.end() && SpreadsheetFormulaPrefix(*firstContent))
        normalized.insert(normalized.begin(), '\'');
    return normalized;
}

} // namespace

std::string EscapeSpreadsheetCsvField(std::string_view value) {
    return EscapeCsvField(NormalizeSpreadsheetText(value));
}

std::string NormalizeSpreadsheetTextField(std::string_view value) {
    return NormalizeSpreadsheetText(value);
}

bool ExportComparisonData(
    const std::vector<squarestar::application::ComparisonSeries>& series,
    const std::string& range,
    const std::string& filename) {
    const ChartExportFormat format = DetectChartExportFormat(filename);
    if (!ChartExportFormatAllowed(ChartExportMethod::SourceData, format))
        return false;
    std::ofstream out(squarestar::platform::Utf8FilesystemPath(filename), std::ios::binary);
    if (!out)
        return false;
    out << std::setprecision(15);
    if (format == ChartExportFormat::Csv)
        out << "symbol,range,timestamp_utc,percent_change_from_range_start\n";
    else if (format == ChartExportFormat::Txt)
        out << "range\t" << NormalizeSpreadsheetTextField(range)
            << "\ntime_basis\tUTC\n\nsymbol\ttimestamp_utc\tpercent_change_from_range_start\n";
    else
        out << "{\n  \"range\": \"" << squarestar::text::EscapeJsonStringValue(range)
            << "\",\n  \"timeBasis\": \"UTC\",\n  \"series\": [\n";
    bool firstJsonSeries = true;
    for (const auto& item : series) {
        const std::size_t count = std::min({item.rawX.size(), item.x.size(), item.percent.size()});
        if (format == ChartExportFormat::Json) {
            if (!firstJsonSeries)
                out << ",\n";
            firstJsonSeries = false;
            out << "    {\"symbol\": \"" << squarestar::text::EscapeJsonStringValue(item.symbol)
                << "\", \"points\": [\n";
        }
        bool firstJsonPoint = true;
        for (std::size_t i = 0; i < count; ++i) {
            if (!std::isfinite(item.percent[i]) || !std::isfinite(item.rawX[i]))
                continue;
            const std::string timestamp =
                FormatDataTimestampUtc(item.rawX[i], format != ChartExportFormat::Json);
            if (format == ChartExportFormat::Csv) {
                out << EscapeSpreadsheetCsvField(item.symbol) << ','
                    << EscapeSpreadsheetCsvField(range) << ','
                    << EscapeSpreadsheetCsvField(timestamp) << ',' << item.percent[i] << '\n';
            } else if (format == ChartExportFormat::Txt) {
                out << NormalizeSpreadsheetTextField(item.symbol) << '\t'
                    << NormalizeSpreadsheetTextField(timestamp) << '\t'
                    << item.percent[i] << '\n';
            } else {
                if (!firstJsonPoint)
                    out << ",\n";
                firstJsonPoint = false;
                out << "      {\"timestamp\": \""
                    << squarestar::text::EscapeJsonStringValue(timestamp)
                    << "\", \"percentChange\": " << item.percent[i] << '}';
            }
        }
        if (format == ChartExportFormat::Json)
            out << "\n    ]}";
    }
    if (format == ChartExportFormat::Json)
        out << "\n  ]\n}\n";
    return out.good();
}

} // namespace squarestar::presentation
