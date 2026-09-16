#pragma once

#include <string>
#include <string_view>

namespace squarestar::platform {


std::string GetExecutableDirectory();
std::string GetApplicationDataDirectory();
std::string GetDefaultExportDirectory();
std::string GetTemporaryDataDirectory();
std::string JoinPath(std::string_view directory, std::string_view file);
std::string ResolveExportDirectory(std::string_view setting);

std::string GetConfigPath();
std::string GetScreenerCachePath();
std::string GetDiagnosticLogPath();

}
