#pragma once

#include <string_view>

namespace squarestar::diagnostics {

struct DiagnosticEvent {
    std::string_view provider;
    std::string_view category;
    std::string_view operation;
    long httpStatus = 0;
};

// Small rotating local log. Callers pass labels, not URLs or payloads, so
// credentials and response bodies cannot land here.
void WriteDiagnosticEvent(const DiagnosticEvent& event) noexcept;
void ReportBackgroundFailureEvent(const char* boundary) noexcept;

} // namespace squarestar::diagnostics
