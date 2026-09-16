#pragma once

#include <string_view>

namespace squarestar::diagnostics {

struct DiagnosticEvent {
    std::string_view provider;
    std::string_view category;
    std::string_view operation;
    long httpStatus = 0;
};


void WriteDiagnosticEvent(const DiagnosticEvent& event) noexcept;
void ReportBackgroundFailureEvent(const char* boundary) noexcept;

}
