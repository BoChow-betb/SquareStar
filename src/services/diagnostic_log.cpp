#include "services/diagnostic_log.hpp"

#include "platform/application_paths.hpp"
#include "platform/windows_path.hpp"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>

namespace squarestar::diagnostics {
namespace {

constexpr std::uintmax_t kMaximumDiagnosticLogBytes = 256 * 1024;
constexpr std::size_t kMaximumFieldBytes = 96;

std::mutex& DiagnosticMutex() {
    static std::mutex mutex;
    return mutex;
}

std::string SanitizeField(std::string_view input) {
    if (const std::size_t query = input.find('?'); query != std::string_view::npos)
        input = input.substr(0, query);
    if (input.find("://") != std::string_view::npos)
        return "redacted-url";

    std::string output;
    output.reserve(std::min(input.size(), kMaximumFieldBytes));
    for (const char raw : input) {
        const unsigned char value = static_cast<unsigned char>(raw);
        if (output.size() >= kMaximumFieldBytes)
            break;
        if (std::isalnum(value) || value == ' ' || value == '-' ||
            value == '_' || value == '.' || value == '/') {
            output.push_back(static_cast<char>(value));
        } else {
            output.push_back('_');
        }
    }
    return output.empty() ? "unknown" : output;
}

std::string UtcTimestamp() {
    const std::time_t now = std::time(nullptr);
    std::tm utc{};
#ifdef _WIN32
    if (gmtime_s(&utc, &now) != 0)
        return "time-unavailable";
#else
    if (!gmtime_r(&now, &utc))
        return "time-unavailable";
#endif
    char buffer[32]{};
    if (std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &utc) == 0)
        return "time-unavailable";
    return buffer;
}

void RotateIfNeeded(const std::filesystem::path& path) {
    std::error_code error;
    const std::uintmax_t size = std::filesystem::file_size(path, error);
    if (error || size < kMaximumDiagnosticLogBytes)
        return;

    std::filesystem::path first = path;
    std::filesystem::path second = path;
    first += ".1";
    second += ".2";
    std::filesystem::remove(second, error);
    error.clear();
    if (std::filesystem::exists(first, error) && !error) {
        std::filesystem::rename(first, second, error);
        error.clear();
    }
    std::filesystem::rename(path, first, error);
}

}

void WriteDiagnosticEvent(const DiagnosticEvent& event) noexcept {
    try {
        const std::string pathText = squarestar::platform::GetDiagnosticLogPath();
        if (pathText.empty())
            return;
        const std::filesystem::path path =
            squarestar::platform::Utf8FilesystemPath(pathText);

        std::lock_guard<std::mutex> lock(DiagnosticMutex());
        const std::filesystem::path parent = path.parent_path();
        if (!parent.empty()) {
            std::error_code error;
            std::filesystem::create_directories(parent, error);
            if (error)
                return;
        }

        RotateIfNeeded(path);
        std::ofstream output(path, std::ios::binary | std::ios::app);
        if (!output)
            return;
        output << UtcTimestamp()
               << " provider=" << SanitizeField(event.provider)
               << " category=" << SanitizeField(event.category)
               << " operation=" << SanitizeField(event.operation);
        if (event.httpStatus != 0)
            output << " http=" << event.httpStatus;
        output << '\n';
        output.flush();
    } catch (...) {


    }
}

void ReportBackgroundFailureEvent(const char* boundary) noexcept {
    WriteDiagnosticEvent(
        {"internal", "background", boundary ? boundary : "background task", 0});
}

}
