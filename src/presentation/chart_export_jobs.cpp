#include "presentation/chart_export_jobs.hpp"

#include <chrono>
#include <future>
#include <mutex>
#include <utility>

namespace squarestar::presentation {
namespace {

std::mutex g_ChartExportJobMutex;
std::future<ChartExportResult> g_ChartExportJob;

} // namespace

bool QueueChartExportJob(std::string path, std::function<bool()> exportWork) {
    std::lock_guard<std::mutex> lock(g_ChartExportJobMutex);
    if (g_ChartExportJob.valid() || !exportWork)
        return false;
    g_ChartExportJob = std::async(
        std::launch::async,
        [path = std::move(path), exportWork = std::move(exportWork)]() mutable {
            ChartExportResult result;
            result.path = path;
            try {
                result.saved = exportWork();
            } catch (...) {
                result.saved = false;
            }
            return result;
        });
    return true;
}

bool ChartExportJobBusy() {
    std::lock_guard<std::mutex> lock(g_ChartExportJobMutex);
    return g_ChartExportJob.valid();
}

std::optional<ChartExportResult> PollChartExportJob() {
    std::future<ChartExportResult> readyJob;
    {
        std::lock_guard<std::mutex> lock(g_ChartExportJobMutex);
        if (!g_ChartExportJob.valid() ||
            g_ChartExportJob.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return std::nullopt;
        readyJob = std::move(g_ChartExportJob);
    }
    try {
        return readyJob.get();
    } catch (...) {
        return ChartExportResult{};
    }
}

void WaitForChartExportJob() {
    std::future<ChartExportResult> pendingJob;
    {
        std::lock_guard<std::mutex> lock(g_ChartExportJobMutex);
        if (!g_ChartExportJob.valid())
            return;
        pendingJob = std::move(g_ChartExportJob);
    }
    try {
        (void)pendingJob.get();
    } catch (...) {
    }
}

} // namespace squarestar::presentation
