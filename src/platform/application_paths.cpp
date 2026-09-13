#include "platform/application_paths.hpp"

#include "platform/windows_path.hpp"

#include <cctype>
#include <filesystem>
#include <string>

namespace squarestar::platform {
namespace {

bool EqualsAsciiCaseInsensitive(std::string_view left, std::string_view right) {
    if (left.size() != right.size())
        return false;
    for (std::size_t index = 0; index < left.size(); ++index) {
        const auto lhs = static_cast<unsigned char>(left[index]);
        const auto rhs = static_cast<unsigned char>(right[index]);
        if (std::tolower(lhs) != std::tolower(rhs))
            return false;
    }
    return true;
}

} // namespace

std::string GetExecutableDirectory() {
#ifdef _WIN32
    const std::wstring executable = ExecutablePathWide();
    if (!executable.empty())
        return WidePathToUtf8(std::filesystem::path(executable).parent_path().native());
#endif
    return ".";
}

std::string GetApplicationDataDirectory() {
    const std::string executableDirectory = GetExecutableDirectory();
    return executableDirectory.empty() ? std::string{}
                                       : JoinPath(executableDirectory, "data");
}

std::string GetDefaultExportDirectory() {
    const std::string appData = GetApplicationDataDirectory();
    return appData.empty() ? GetExecutableDirectory() : JoinPath(appData, "exports");
}

std::string GetTemporaryDataDirectory() {
    const std::string appData = GetApplicationDataDirectory();
    return appData.empty() ? std::string{} : JoinPath(appData, "temp");
}

std::string JoinPath(std::string_view directory, std::string_view file) {
    if (directory.empty() || directory == ".")
        return std::string(file);
    const char last = directory.back();
    if (last == '\\' || last == '/')
        return std::string(directory) + std::string(file);
#ifdef _WIN32
    return std::string(directory) + "\\" + std::string(file);
#else
    return std::string(directory) + "/" + std::string(file);
#endif
}

std::string ResolveExportDirectory(std::string_view setting) {
    if (setting.empty() || EqualsAsciiCaseInsensitive(setting, "default"))
        return GetDefaultExportDirectory();
    return std::string(setting);
}

std::string GetConfigPath() {
    const std::string directory = GetApplicationDataDirectory();
    return directory.empty() ? std::string{} : JoinPath(directory, "config.json");
}

std::string GetScreenerCachePath() {
    const std::string directory = GetApplicationDataDirectory();
    return directory.empty() ? std::string{}
                             : JoinPath(JoinPath(directory, "cache"),
                                        "screener-cache.json");
}

std::string GetDiagnosticLogPath() {
    const std::string directory = GetApplicationDataDirectory();
    return directory.empty() ? std::string{}
                             : JoinPath(JoinPath(directory, "logs"),
                                        "diagnostics.log");
}

} // namespace squarestar::platform
