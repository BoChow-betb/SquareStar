#pragma once

namespace squarestar::application {

using BackgroundFailureReporter = void (*)(const char* boundary) noexcept;

void SetBackgroundFailureReporter(BackgroundFailureReporter reporter) noexcept;
void ReportBackgroundFailure(const char* boundary) noexcept;

}
