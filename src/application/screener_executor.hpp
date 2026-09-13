#pragma once

#include <functional>

namespace squarestar::application {

bool SubmitLatestScreenerJob(std::function<void()> job);
void ShutdownLatestScreenerJobExecutor();
bool ScreenerJobBusy() noexcept;

} // namespace squarestar::application
