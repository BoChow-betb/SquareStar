#include "services/config_persistence.hpp"
#include "services/config_save_queue.hpp"
#include "application/app_state.hpp"
#include "application/persisted_state.hpp"
#include "platform/application_paths.hpp"
#include "platform/windows_path.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#ifndef _WIN32
#include <sys/stat.h>
#endif

namespace {

std::string ReadFileBytes(const std::filesystem::path& path) {
    std::error_code error;
    const std::uintmax_t byteCount = std::filesystem::file_size(path, error);
    if (error || byteCount > static_cast<std::uintmax_t>(std::string{}.max_size()))
        return {};

    std::string contents(static_cast<std::size_t>(byteCount), '\0');
    std::ifstream input(path, std::ios::binary);
    if (!input)
        return {};
    if (!contents.empty()) {
        input.read(contents.data(), static_cast<std::streamsize>(contents.size()));
        if (!input)
            return {};
    }
    return contents;
}

} // namespace

int main() {
    namespace fs = std::filesystem;
    using squarestar::config::FlushConfigWrites;
    using squarestar::config::SubmitConfigWrite;

    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path root =
        fs::temp_directory_path() / ("squarestar-config-writer-" + std::to_string(suffix));
    std::error_code error;
    fs::create_directories(root, error);
    if (error) {
        std::cerr << "could not create config writer test directory\n";
        return EXIT_FAILURE;
    }

    const fs::path config = root / "state.json";
    if (!SubmitConfigWrite(config.string(), "{\"ok\":true}\n") ||
        !FlushConfigWrites()) {
        std::cerr << "successful config write was reported as failed\n";
        fs::remove_all(root, error);
        return EXIT_FAILURE;
    }

#ifndef _WIN32
    struct stat configStat {};
    if (::stat(config.c_str(), &configStat) != 0 ||
        (configStat.st_mode & (S_IRWXG | S_IRWXO)) != 0) {
        std::cerr << "config temporary/replace path exposed state to group or other users\n";
        fs::remove_all(root, error);
        return EXIT_FAILURE;
    }
#endif

    const fs::path ordered = root / "ordered.json";
    if (!SubmitConfigWrite(ordered.string(), "first\n") ||
        !SubmitConfigWrite(ordered.string(), "second\n") ||
        !SubmitConfigWrite(ordered.string(), "latest\n") ||
        !FlushConfigWrites() || ReadFileBytes(ordered) != "latest\n") {
        std::cerr << "serialized config writes did not preserve submission order\n";
        fs::remove_all(root, error);
        return EXIT_FAILURE;
    }

    // A stale temp file from a prior crash must never block the next save.
    const fs::path staleTemp = fs::path(ordered.string() + ".tmp");
    {
        std::ofstream stale(staleTemp, std::ios::binary | std::ios::trunc);
        stale << "stale-temp\n";
    }
    if (!SubmitConfigWrite(ordered.string(), "after-crash\n") ||
        !FlushConfigWrites() || ReadFileBytes(ordered) != "after-crash\n" ||
        fs::exists(staleTemp)) {
        std::cerr << "stale config temp file blocked crash-safe replacement\n";
        fs::remove_all(root, error);
        return EXIT_FAILURE;
    }

    const std::string saved = ReadFileBytes(config);
    if (saved != "{\"ok\":true}\n") {
        std::cerr << "config writer did not atomically persist the latest payload\n";
        fs::remove_all(root, error);
        return EXIT_FAILURE;
    }

    const fs::path nestedConfig = root / "nested" / "SquareStar" / "state.json";
    if (!SubmitConfigWrite(nestedConfig.string(), "{\"nested\":true}\n") ||
        !FlushConfigWrites() || ReadFileBytes(nestedConfig) != "{\"nested\":true}\n") {
        std::cerr << "config writer did not create the requested config directory tree\n";
        fs::remove_all(root, error);
        return EXIT_FAILURE;
    }

#ifdef _WIN32
    const fs::path unicodeConfigPath = root / L"SquareStar-配置.json";
    const std::string unicodeConfig =
        squarestar::platform::WidePathToUtf8(unicodeConfigPath.native());
#else
    const fs::path unicodeConfigPath = root / "SquareStar-配置.json";
    const std::string unicodeConfig = unicodeConfigPath.string();
#endif
    if (unicodeConfig.empty() ||
        !SubmitConfigWrite(unicodeConfig, "{\"unicode\":true}\n") ||
        !FlushConfigWrites()) {
        std::cerr << "UTF-8 config path was not persisted\n";
        fs::remove_all(root, error);
        return EXIT_FAILURE;
    }
    const std::string unicodeSaved = ReadFileBytes(unicodeConfigPath);
    if (unicodeSaved != "{\"unicode\":true}\n") {
        std::cerr << "UTF-8 config path contained the wrong payload\n";
        fs::remove_all(root, error);
        return EXIT_FAILURE;
    }

    const fs::path coalescedA = root / "coalesced-a.json";
    const fs::path coalescedB = root / "coalesced-b.ini";
    bool coalescedACompleted = false;
    bool coalescedBCompleted = false;
    if (!SubmitConfigWrite(coalescedA.string(), "A\n", [&](bool succeeded) {
            coalescedACompleted = succeeded;
        }) ||
        !SubmitConfigWrite(coalescedB.string(), "B\n", [&](bool succeeded) {
            coalescedBCompleted = succeeded;
        }) ||
        !FlushConfigWrites()) {
        std::cerr << "independent config paths did not flush successfully\n";
        fs::remove_all(root, error);
        return EXIT_FAILURE;
    }
    if (ReadFileBytes(coalescedA) != "A\n" || ReadFileBytes(coalescedB) != "B\n" ||
        !coalescedACompleted || !coalescedBCompleted) {
        std::cerr << "debouncing one config path dropped another pending path\n";
        fs::remove_all(root, error);
        return EXIT_FAILURE;
    }

    const fs::path blockedDestination = root / "directory-target";
    fs::create_directory(blockedDestination, error);
    const fs::path mixedSuccess = root / "mixed-success.json";
    bool blockedCompletionCalled = false;
    bool blockedCompletionSucceeded = true;
    if (error ||
        !SubmitConfigWrite(
            blockedDestination.string(),
            "cannot replace a directory",
            [&](bool succeeded) {
                blockedCompletionCalled = true;
                blockedCompletionSucceeded = succeeded;
            }) ||
        !SubmitConfigWrite(mixedSuccess.string(), "still written\n") ||
        FlushConfigWrites()) {
        std::cerr << "config writer did not expose a replace failure across a mixed batch\n";
        fs::remove_all(root, error);
        return EXIT_FAILURE;
    }
    if (ReadFileBytes(mixedSuccess) != "still written\n") {
        std::cerr << "a failed config path prevented an independent path from persisting\n";
        fs::remove_all(root, error);
        return EXIT_FAILURE;
    }
    if (!blockedCompletionCalled || blockedCompletionSucceeded) {
        std::cerr << "final config replace failure was not reported through completion\n";
        fs::remove_all(root, error);
        return EXIT_FAILURE;
    }
    if (!SubmitConfigWrite(config.string(), "{\"ok\":true}\n") ||
        !FlushConfigWrites()) {
        std::cerr << "returning to an already-persisted snapshot kept a stale failure\n";
        fs::remove_all(root, error);
        return EXIT_FAILURE;
    }

    // A rejected startup config must be preserved byte-for-byte under a unique
    // backup name so the app can recover with defaults without destroying data.
    const fs::path portableDataRoot =
        squarestar::platform::Utf8FilesystemPath(
            squarestar::platform::GetApplicationDataDirectory());
    fs::remove_all(portableDataRoot, error);
    error.clear();
    squarestar::application::AppState cleanState;
    if (!squarestar::config::FlushPendingConfigSave(
            squarestar::application::PersistedStateOf(cleanState), true) ||
        fs::exists(portableDataRoot)) {
        std::cerr << "clean shutdown flush created portable storage without a save request\n";
        fs::remove_all(portableDataRoot, error);
        fs::remove_all(root, error);
        return EXIT_FAILURE;
    }
    if (squarestar::config::HasPersistedConfig()) {
        std::cerr << "missing portable config was incorrectly reported as present\n";
        fs::remove_all(root, error);
        return EXIT_FAILURE;
    }
    const fs::path rejectedConfig =
        squarestar::platform::Utf8FilesystemPath(
            squarestar::platform::GetConfigPath());
    fs::create_directories(rejectedConfig.parent_path(), error);
    if (error) {
        std::cerr << "could not create rejected-state recovery directory\n";
        fs::remove_all(root, error);
        return EXIT_FAILURE;
    }
    const std::string rejectedBytes = "{\"unsupported\":true}\n";
    {
        std::ofstream rejected(rejectedConfig, std::ios::binary | std::ios::trunc);
        rejected << rejectedBytes;
    }
    if (!squarestar::config::HasPersistedConfig()) {
        std::cerr << "existing portable config was not detected\n";
        fs::remove_all(portableDataRoot, error);
        fs::remove_all(root, error);
        return EXIT_FAILURE;
    }
    const auto preserved =
        squarestar::config::PreserveRejectedConfigForRecovery();
    if (!preserved || fs::exists(rejectedConfig) ||
        ReadFileBytes(rejectedConfig.parent_path() / *preserved) != rejectedBytes) {
        std::cerr << "rejected startup config was not preserved safely\n";
        fs::remove_all(portableDataRoot, error);
        fs::remove_all(root, error);
        return EXIT_FAILURE;
    }
    if (squarestar::config::HasPersistedConfig()) {
        std::cerr << "preserved rejected config still appeared as canonical state\n";
        fs::remove_all(portableDataRoot, error);
        fs::remove_all(root, error);
        return EXIT_FAILURE;
    }

    fs::remove_all(portableDataRoot, error);
    error.clear();
    fs::remove_all(root, error);
    return EXIT_SUCCESS;
}
