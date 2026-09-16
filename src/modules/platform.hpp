#pragma once

#include <array>
#include <functional>
#include <string>
#include <vector>

#include "domain/stock_data.hpp"
#include "platform/windows_headers.hpp"
#include "presentation/chart_export.hpp"
#include "presentation/chart_types.hpp"
#include "imgui.h"

struct GLFWwindow;

namespace squarestar::application {
struct AppState;
}

namespace squarestar::shell {

struct ChartExportData {
    std::vector<double> timestamps, opens, highs, lows, closes, volumes;
    std::string companyName;
};
struct MonitorExportData {
    std::string ticker;
    std::string range;
    ChartExportData chart;
};
bool ChooseChartExportPathForMethod(
    HWND owner,
    const std::string& ticker,
    const std::string& range,
    squarestar::presentation::ChartVisualType type,
    const std::string& currency,
    const std::string& directorySetting,
    squarestar::presentation::ChartExportMethod method,
    std::string& path);
void DrawChartExportMenuItems(
    const std::function<void(squarestar::presentation::ChartExportMethod)>& exportChart,
    const char* dataLabel = "Source data (CSV/TXT/JSON)");
bool ExportGuiDrawDataImage(const std::string& filename,
                            ImGuiViewport* viewport,
                            ImVec2 screenMin,
                            ImVec2 screenMax,
                            int* outputWidth = nullptr,
                            int* outputHeight = nullptr);
ImVec2 GuiExportFramebufferScale(ImVec2 nativeScale);
bool IsCleanGuiCaptureFrame();
ChartExportData MakeChartExportSnapshot(
    const squarestar::market::StockData& source,
    const std::string& fallbackCompany);
MonitorExportData MakeMonitorExportSnapshot(
    const squarestar::market::StockData& source,
    std::string ticker,
    std::string range,
    const std::string& fallbackCompany);
void PollChartFileExport(squarestar::application::AppState& state);
void PumpGuiCapture(GLFWwindow* window,
                    squarestar::application::AppState& state,
                    void (*renderCaptureSurface)(GLFWwindow*,
                                                 squarestar::application::AppState&));
bool QueueChartFileExport(ChartExportData data,
                          std::string ticker,
                          std::string range,
                          std::string path,
                          squarestar::presentation::ChartVisualType type);
bool QueueMonitorFileExport(std::vector<MonitorExportData> data,
                            std::string path);
bool QueueGuiPresentationExport(std::string path,
                                ImGuiViewport* viewport,
                                ImVec2 screenMin,
                                ImVec2 screenMax,
                                float capturePadding = 16.0f);
std::string SuggestedChartExportFilename(
    const std::string& ticker,
    const std::string& range,
    squarestar::presentation::ChartVisualType type,
    const std::string& currency,
    squarestar::presentation::ChartExportMethod method,
    const char* extension);

}
