#include "services/config_persistence.hpp"
#include "services/config_state_codec.hpp"

#include "application/app_limits.hpp"
#include "application/frame_rate.hpp"
#include "application/gui_layout_persistence.hpp"
#include "application/key_bindings.hpp"
#include "application/persisted_state.hpp"
#include "application/theme_profiles.hpp"
#include "domain/chart_ranges.hpp"
#include "domain/json_text.hpp"
#include "domain/market_symbol.hpp"
#include "domain/text.hpp"
#include "domain/world_clock_zones.hpp"
#include "platform/application_paths.hpp"
#include "platform/windows_path.hpp"
#include "services/api_key_store.hpp"
#include "services/json_access.hpp"
#include "services/secret_protection.hpp"

#include "platform/windows_headers.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifndef _WIN32
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif


namespace squarestar::config {

using squarestar::alerts::AlertService;
using squarestar::application::AppConfig;
using squarestar::application::AppNavigation;
using squarestar::application::InitializeDefaultKeybinds;
using squarestar::application::InitializeThemeProfiles;
using squarestar::application::CommitGuiLayoutSnapshot;
using squarestar::application::GuiLayoutSnapshotForConfig;
using squarestar::application::KeyBind;
using squarestar::application::NormalizeGuiFrameRateMode;
using squarestar::application::SavedGuiStockTab;
using squarestar::application::SetThemePreset;
using squarestar::application::TerminalAction;
using squarestar::json::JsonBool;
using squarestar::json::JsonInt;
using squarestar::json::JsonNumber;
using squarestar::json::JsonString;
using squarestar::json::ParseJsonInSitu;
using squarestar::market::MarketSymbol;
using squarestar::market::TIME_RANGES;
using squarestar::market::WORLD_ZONES;
using squarestar::secrets::GetFinnhubApiKeySnapshot;
using squarestar::secrets::IsApiKeyRevisionCurrent;
using squarestar::secrets::ProtectApiKey;
using squarestar::secrets::ProtectLocalState;
using squarestar::secrets::SecureClear;
using squarestar::secrets::SetFinnhubApiKey;
using squarestar::secrets::UnprotectApiKey;
using squarestar::secrets::UnprotectLocalState;
using squarestar::text::EscapeJsonStringValue;
using squarestar::text::ParseIntOr;
using squarestar::text::UppercaseInPlace;
using YyjsonDoc = squarestar::json::Document;

namespace {

bool DeletePersistentFileIfPresent(const std::string& path) {
    if (path.empty())
        return true;
#ifdef _WIN32
    const std::wstring widePath = squarestar::platform::Utf8PathToWide(path);
    if (widePath.empty())
        return false;
    const DWORD attributes = GetFileAttributesW(widePath.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES)
        return GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND;
    if ((attributes & FILE_ATTRIBUTE_READONLY) != 0 &&
        !SetFileAttributesW(widePath.c_str(), attributes & ~FILE_ATTRIBUTE_READONLY)) {
        return false;
    }
    if (DeleteFileW(widePath.c_str()))
        return true;
    return GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND;
#else
    std::error_code error;
    const std::filesystem::path filePath(path);
    const bool removed = std::filesystem::remove(filePath, error);
    if (removed)
        return true;
    if (error)
        return false;
    const bool fileStillExists = std::filesystem::exists(filePath, error);
    return !error && !fileStillExists;
#endif
}


std::string FilenameUtf8(const std::filesystem::path& path) {
#ifdef _WIN32
    return squarestar::platform::WidePathToUtf8(path.filename().native());
#else
    return path.filename().string();
#endif
}

bool IsOwnedConfigTemporaryFilename(std::string_view candidateName,
                                    std::string_view configName) {
    if (candidateName.empty() || configName.empty())
        return false;
    const std::string temporaryName = std::string(configName) + ".tmp";
    return candidateName == temporaryName ||
           (candidateName.size() > temporaryName.size() + 1 &&
            candidateName.compare(0, temporaryName.size(), temporaryName) == 0 &&
            candidateName[temporaryName.size()] == '.');
}

bool IsConfigTemporaryFile(const std::filesystem::path& candidate,
                           const std::filesystem::path& configPath) {
    const std::string candidateName = FilenameUtf8(candidate);
    const std::string configName = FilenameUtf8(configPath);
    if (candidateName.empty() || configName.empty())
        return false;
    return IsOwnedConfigTemporaryFilename(candidateName, configName);
}

struct ConfigTemporaryCleanupResult {
    std::size_t matched = 0;
    std::size_t removed = 0;
    std::size_t failed = 0;
};

ConfigTemporaryCleanupResult DeleteConfigTemporaryFiles(
    const std::string& configPath) {
    ConfigTemporaryCleanupResult result;
    if (configPath.empty())
        return result;
    const std::filesystem::path destination =
        squarestar::platform::Utf8FilesystemPath(configPath);
    std::filesystem::path directory = destination.parent_path();
    if (directory.empty())
        directory = std::filesystem::current_path();
    std::error_code error;
    if (!std::filesystem::exists(directory, error))
        return result;
    if (error) {
        result.failed = 1;
        return result;
    }
    for (std::filesystem::directory_iterator it(directory, error), end;
         !error && it != end;
         it.increment(error)) {
        if (!it->is_regular_file(error) || error)
            continue;
        if (!IsConfigTemporaryFile(it->path(), destination))
            continue;
        ++result.matched;
        std::error_code removeError;
        if (std::filesystem::remove(it->path(), removeError))
            ++result.removed;
        else if (removeError)
            ++result.failed;
    }
    if (error)
        ++result.failed;
    return result;
}

struct ProtectedApiKeySnapshot {
    std::string value;
    uint64_t revision = 0;
    bool current = false;
    bool canWrite = false;
};
struct ProtectedApiKeyCache {
    std::mutex mutex;
    uint64_t revision = 0;
    std::optional<std::string> value;
    bool current = false;
};
static ProtectedApiKeyCache& ApiKeyProtectionCache() {
    static ProtectedApiKeyCache cache;
    return cache;
}
static void RememberProtectedApiKeyForConfig(std::string value,
                                             bool current,
                                             uint64_t revision) {
    ProtectedApiKeyCache& cache = ApiKeyProtectionCache();
    std::lock_guard<std::mutex> lock(cache.mutex);
    cache.revision = revision;
    cache.value = std::move(value);
    cache.current = current;
}
static ProtectedApiKeySnapshot ProtectedApiKeyForConfig(
    std::optional<uint64_t> requiredRevision) {
    ProtectedApiKeyCache& cache = ApiKeyProtectionCache();
    constexpr int kSnapshotAttempts = 3;
    for (int attempt = 0; attempt < kSnapshotAttempts; ++attempt) {
        auto apiKey = GetFinnhubApiKeySnapshot();
        if (requiredRevision && apiKey.revision != *requiredRevision) {
            if (!apiKey.value.empty())
                SecureClear(apiKey.value);
            return {};
        }

        {
            std::lock_guard<std::mutex> lock(cache.mutex);
            if (cache.revision == apiKey.revision && cache.value) {
                if (!apiKey.value.empty())
                    SecureClear(apiKey.value);
                return {*cache.value, apiKey.revision, cache.current, true};
            }
        }

        std::optional<std::string> protectedApiKey = ProtectApiKey(apiKey.value);
        if (!apiKey.value.empty())
            SecureClear(apiKey.value);
        if (!protectedApiKey) {
            if (requiredRevision)
                return {};
            std::lock_guard<std::mutex> lock(cache.mutex);
            // Keep the previous ciphertext when DPAPI is temporarily unavailable so
            // unrelated settings still persist without destroying the saved key.
            return {cache.value.value_or(std::string{}),
                    cache.revision,
                    false,
                    cache.value.has_value()};
        }

        if (!IsApiKeyRevisionCurrent(apiKey.revision))
            continue;
        {
            std::lock_guard<std::mutex> lock(cache.mutex);
            if (!IsApiKeyRevisionCurrent(apiKey.revision))
                continue;
            cache.value = protectedApiKey;
            cache.revision = apiKey.revision;
            cache.current = true;
            return {*protectedApiKey, apiKey.revision, true, true};
        }
    }
    // A rapidly changing key cannot be represented by a coherent snapshot.
    // Fail closed instead of queueing ciphertext for a revision that no longer exists.
    return {};
}

bool DeletePortableRuntimeDataExceptExports() {
    const std::string dataDirectory =
        squarestar::platform::GetApplicationDataDirectory();
    if (dataDirectory.empty())
        return false;
    const std::filesystem::path root =
        squarestar::platform::Utf8FilesystemPath(dataDirectory);
    std::error_code error;
    if (!std::filesystem::exists(root, error))
        return !error;
    if (error || !std::filesystem::is_directory(root, error) || error)
        return false;

    for (std::filesystem::directory_iterator it(root, error), end;
         !error && it != end;
         it.increment(error)) {
        const std::string name = FilenameUtf8(it->path());
        if (name == "exports" || name == "Exports")
            continue;
        std::filesystem::remove_all(it->path(), error);
        if (error)
            return false;
    }
    if (error)
        return false;

    // If no user-created exports remain, remove the empty data directory too.
    error.clear();
    if (std::filesystem::is_empty(root, error) && !error)
        std::filesystem::remove(root, error);
    return !error;
}
} // namespace

bool HasPersistedConfig() noexcept {
    try {
        const std::string configPath = squarestar::platform::GetConfigPath();
        if (configPath.empty())
            return false;
        std::error_code error;
        const bool exists = std::filesystem::is_regular_file(
            squarestar::platform::Utf8FilesystemPath(configPath), error);
        return exists && !error;
    } catch (...) {
        return false;
    }
}

bool DeletePersistentApplicationState() {
    (void)FlushConfigWrites();
    const std::string configPath = squarestar::platform::GetConfigPath();
    const std::string screenerCachePath =
        squarestar::platform::GetScreenerCachePath();
    const std::string diagnosticLogPath =
        squarestar::platform::GetDiagnosticLogPath();

    const bool configDeleted = DeletePersistentFileIfPresent(configPath);
    const ConfigTemporaryCleanupResult configTemporary =
        DeleteConfigTemporaryFiles(configPath);
    const bool screenerCacheDeleted =
        DeletePersistentFileIfPresent(screenerCachePath);
    const bool temporaryScreenerCacheDeleted = screenerCachePath.empty() ||
        DeletePersistentFileIfPresent(screenerCachePath + ".tmp");
    const bool diagnosticLogDeleted =
        DeletePersistentFileIfPresent(diagnosticLogPath);
    const bool diagnosticLogFirstRotationDeleted = diagnosticLogPath.empty() ||
        DeletePersistentFileIfPresent(diagnosticLogPath + ".1");
    const bool diagnosticLogSecondRotationDeleted = diagnosticLogPath.empty() ||
        DeletePersistentFileIfPresent(diagnosticLogPath + ".2");
    const bool portableRuntimeDeleted =
        DeletePortableRuntimeDataExceptExports();
    CommitGuiLayoutSnapshot({});
    return configDeleted && configTemporary.failed == 0 &&
           screenerCacheDeleted && temporaryScreenerCacheDeleted &&
           diagnosticLogDeleted && diagnosticLogFirstRotationDeleted &&
           diagnosticLogSecondRotationDeleted && portableRuntimeDeleted;
}

TemporaryDataCleanupResult ClearSquareStarTemporaryData() {
    FlushConfigWrites();
    TemporaryDataCleanupResult result;
    const std::string configPath = squarestar::platform::GetConfigPath();
    const std::string screenerCachePath =
        squarestar::platform::GetScreenerCachePath();
    const std::string pendingScreenerCache = screenerCachePath.empty()
                                                   ? std::string{}
                                                   : screenerCachePath + ".tmp";
    std::error_code error;
    if (!configPath.empty()) {
        const ConfigTemporaryCleanupResult cleanup =
            DeleteConfigTemporaryFiles(configPath);
        result.filesRemoved += cleanup.removed;
        result.filesInUse += cleanup.failed;
    }
    error.clear();
    const bool pendingScreenerCacheExists =
        !pendingScreenerCache.empty() && std::filesystem::exists(
            squarestar::platform::Utf8FilesystemPath(pendingScreenerCache), error);
    if (!error && pendingScreenerCacheExists) {
        if (DeletePersistentFileIfPresent(pendingScreenerCache))
            ++result.filesRemoved;
        else
            ++result.filesInUse;
    }

    // SquareStar-owned temporary data lives exclusively inside data/temp.
    const std::string portableTemporaryPath =
        squarestar::platform::GetTemporaryDataDirectory();
    if (!portableTemporaryPath.empty()) {
        error.clear();
        const std::filesystem::path portableTemporary =
            squarestar::platform::Utf8FilesystemPath(portableTemporaryPath);
        if (std::filesystem::exists(portableTemporary, error) && !error) {
            const std::uintmax_t removed =
                std::filesystem::remove_all(portableTemporary, error);
            if (!error)
                result.filesRemoved += static_cast<std::size_t>(removed);
            else
                ++result.filesInUse;
        } else if (error) {
            ++result.filesInUse;
        }
    }

    return result;
}

bool SaveConfig(
    squarestar::application::PersistedStateConstView persisted,
    ConfigWriteCompletion completion,
    std::optional<std::uint64_t> requiredApiKeyRevision) {
    const ProtectedApiKeySnapshot protectedApiKey =
        ProtectedApiKeyForConfig(requiredApiKeyRevision);
    if (!protectedApiKey.canWrite) {
        if (completion)
            completion(false);
        return false;
    }
    const std::string configPath = squarestar::platform::GetConfigPath();
    if (configPath.empty()) {
        if (completion)
            completion(false);
        return false;
    }
    const std::int64_t nowEpoch = static_cast<std::int64_t>(std::time(nullptr));
    std::string privateState = EncodePrivateConfigState(
        persisted, nowEpoch, GuiLayoutSnapshotForConfig());
    std::optional<std::string> protectedPrivateState = ProtectLocalState(privateState);
    SecureClear(privateState);
    if (!protectedPrivateState) {
        if (completion)
            completion(false);
        return false;
    }
    const std::string payload = EncodeConfigState(
        persisted, protectedApiKey.value, *protectedPrivateState);
    if (requiredApiKeyRevision &&
        !IsApiKeyRevisionCurrent(*requiredApiKeyRevision)) {
        if (completion)
            completion(false);
        return false;
    }
    ConfigWriteCompletion failureCompletion = completion;
    const bool queued = SubmitConfigWrite(
        configPath, payload, std::move(completion));
    if (!queued && failureCompletion)
        failureCompletion(false);
    return queued && protectedApiKey.current;
}
ConfigLoadStatus LoadConfig(
    squarestar::application::PersistedStateView persisted) {
    AppConfig stagedConfig;
    AppNavigation stagedNavigation;
    AlertService stagedAlerts;
    InitializeDefaultKeybinds(stagedConfig);
    InitializeThemeProfiles(stagedConfig);
    squarestar::application::PersistedStateView staged{
        stagedConfig, stagedNavigation, stagedAlerts};

    const auto commitStagedState = [&] {
        persisted.config = std::move(stagedConfig);
        persisted.navigation = std::move(stagedNavigation);
        persisted.alerts = std::move(stagedAlerts);
    };

    const std::string configPath = squarestar::platform::GetConfigPath();
    if (configPath.empty()) {
        commitStagedState();
        return ConfigLoadStatus::NoState;
    }

    std::ifstream file(squarestar::platform::Utf8FilesystemPath(configPath),
                       std::ios::binary);
    if (!file.is_open()) {
        commitStagedState();
        return ConfigLoadStatus::NoState;
    }

    file.seekg(0, std::ios::end);
    const std::streamoff configSize = file.tellg();
    if (configSize < 0 || configSize > 1024 * 1024)
        return ConfigLoadStatus::Rejected;
    file.seekg(0, std::ios::beg);
    std::string jsonStr(static_cast<std::size_t>(configSize), '\0');
    if (!jsonStr.empty()) {
        file.read(jsonStr.data(), static_cast<std::streamsize>(jsonStr.size()));
        if (!file)
            return ConfigLoadStatus::Rejected;
    }

    ConfigDecodeResult decoded = DecodeConfigState(std::move(jsonStr), staged);
    if (!decoded.parsed)
        return ConfigLoadStatus::Rejected;

    auto privateState = UnprotectLocalState(decoded.protectedPrivateState);
    std::string imguiLayout;
    if (!privateState ||
        !DecodePrivateConfigState(
            std::move(*privateState),
            staged,
            static_cast<std::int64_t>(std::time(nullptr)),
            imguiLayout)) {
        return ConfigLoadStatus::Rejected;
    }

    auto unprotectedApiKey = UnprotectApiKey(decoded.protectedApiKey);
    if (!unprotectedApiKey)
        return ConfigLoadStatus::Rejected;

    std::string apiKey = std::move(*unprotectedApiKey);
    const uint64_t loadedApiKeyRevision = SetFinnhubApiKey(apiKey);
    if (!apiKey.empty())
        SecureClear(apiKey);

    commitStagedState();
    CommitGuiLayoutSnapshot(std::move(imguiLayout));
    RememberProtectedApiKeyForConfig(
        std::move(decoded.protectedApiKey),
        true,
        loadedApiKeyRevision);
    return ConfigLoadStatus::Loaded;
}

} // namespace squarestar::config
