#include "platform/application_paths.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>

int main() {
    namespace fs = std::filesystem;
    using squarestar::platform::GetApplicationDataDirectory;
    using squarestar::platform::GetConfigPath;
    using squarestar::platform::GetDefaultExportDirectory;
    using squarestar::platform::GetDiagnosticLogPath;
    using squarestar::platform::GetExecutableDirectory;
    using squarestar::platform::GetScreenerCachePath;
    using squarestar::platform::GetTemporaryDataDirectory;
    using squarestar::platform::ResolveExportDirectory;

    const std::string executableDirectory = GetExecutableDirectory();
    const std::string appData = GetApplicationDataDirectory();
    const std::string config = GetConfigPath();
    const std::string defaultExport = GetDefaultExportDirectory();
    const std::string diagnosticLog = GetDiagnosticLogPath();
    const std::string resolvedDefaultExport = ResolveExportDirectory("default");
    const std::string screenerCache = GetScreenerCachePath();
    const std::string temporaryData = GetTemporaryDataDirectory();
    if (executableDirectory.empty() || appData.empty() || config.empty() ||
        screenerCache.empty() || diagnosticLog.empty() || defaultExport.empty() ||
        temporaryData.empty()) {
        std::cerr << "portable storage path is empty\n";
        return EXIT_FAILURE;
    }

    const fs::path expectedData =
        (fs::path(executableDirectory) / "data").lexically_normal();
    if (fs::path(appData).lexically_normal() != expectedData) {
        std::cerr << "application data is not stored beside the executable\n";
        return EXIT_FAILURE;
    }
    if (fs::path(config).filename() != "config.json" ||
        fs::path(config).parent_path().lexically_normal() != expectedData) {
        std::cerr << "canonical runtime state is not data/config.json\n";
        return EXIT_FAILURE;
    }
    if (fs::path(screenerCache).parent_path().filename() != "cache" ||
        fs::path(screenerCache).parent_path().parent_path().lexically_normal() != expectedData ||
        fs::path(screenerCache).filename() != "screener-cache.json") {
        std::cerr << "screener cache is not isolated under data/cache\n";
        return EXIT_FAILURE;
    }
    if (fs::path(diagnosticLog).parent_path().filename() != "logs" ||
        fs::path(diagnosticLog).parent_path().parent_path().lexically_normal() != expectedData ||
        fs::path(diagnosticLog).filename() != "diagnostics.log") {
        std::cerr << "diagnostic log is not isolated under data/logs\n";
        return EXIT_FAILURE;
    }
    if (fs::path(temporaryData).parent_path().lexically_normal() != expectedData ||
        fs::path(temporaryData).filename() != "temp") {
        std::cerr << "temporary data is not isolated under data/temp\n";
        return EXIT_FAILURE;
    }
    if (resolvedDefaultExport != defaultExport ||
        fs::path(defaultExport).parent_path().lexically_normal() != expectedData ||
        fs::path(defaultExport).filename() != "exports") {
        std::cerr << "default export path is not data/exports\n";
        return EXIT_FAILURE;
    }
    if (ResolveExportDirectory("C:/Custom/SquareStarExports") !=
        "C:/Custom/SquareStarExports") {
        std::cerr << "custom export directory was not preserved\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
