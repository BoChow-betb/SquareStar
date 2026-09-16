#pragma once

#include <functional>
#include <optional>
#include <string>

namespace squarestar::presentation {

struct ChartExportResult {
    bool saved = false;
    std::string path;
};

bool QueueChartExportJob(std::string path, std::function<bool()> exportWork);
bool ChartExportJobBusy();
std::optional<ChartExportResult> PollChartExportJob();
void WaitForChartExportJob();

}
