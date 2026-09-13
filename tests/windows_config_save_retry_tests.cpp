#include "application/app_config.hpp"
#include "application/app_navigation.hpp"
#include "application/alert_service.hpp"
#include "application/persisted_state.hpp"
#include "platform/application_paths.hpp"
#include "platform/windows_path.hpp"
#include "services/config_persistence.hpp"
#include "services/config_save_queue.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>

int main() {
#ifdef _WIN32
    namespace fs = std::filesystem;
    using squarestar::application::AppConfig;
    using squarestar::application::AppNavigation;
    using squarestar::application::PersistedStateConstView;
    using squarestar::alerts::AlertService;

    const fs::path portableDataRoot =
        squarestar::platform::Utf8FilesystemPath(
            squarestar::platform::GetApplicationDataDirectory());
    const fs::path configPath =
        squarestar::platform::Utf8FilesystemPath(
            squarestar::platform::GetConfigPath());
    std::error_code error;
    fs::remove_all(portableDataRoot, error);
    error.clear();
    fs::create_directories(configPath.parent_path(), error);
    if (error) {
        std::cerr << "could not create portable config test root\n";
        return EXIT_FAILURE;
    }

    // Make the config filename a directory so the atomic file replacement fails.
    fs::create_directories(configPath, error);
    if (error) {
        std::cerr << "could not create blocked config destination\n";
        fs::remove_all(portableDataRoot, error);
        return EXIT_FAILURE;
    }

    AppConfig config;
    AppNavigation navigation;
    AlertService alerts;
    const PersistedStateConstView persisted{config, navigation, alerts};

    squarestar::config::CancelPendingConfigSave();
    while (squarestar::config::ConsumeConfigSaveFailureNotice()) {
    }

    if (!squarestar::config::PersistConfigSnapshot(persisted)) {
        std::cerr << "config snapshot was rejected before the async writer\n";
        fs::remove_all(portableDataRoot, error);
        return EXIT_FAILURE;
    }
    if (squarestar::config::FlushConfigWrites()) {
        std::cerr << "blocked destination unexpectedly persisted\n";
        fs::remove_all(portableDataRoot, error);
        return EXIT_FAILURE;
    }

    const auto notice =
        squarestar::config::ConsumeConfigSaveFailureNotice();
    if (!notice || notice->consecutiveFailures != 1) {
        std::cerr << "final async write failure did not reach the save queue\n";
        fs::remove_all(portableDataRoot, error);
        return EXIT_FAILURE;
    }
    const double retryDelay =
        squarestar::config::SecondsUntilConfigSaveDue();
    if (retryDelay < 0.0 || retryDelay > 1.5) {
        std::cerr << "first persistence retry was not scheduled with bounded backoff\n";
        fs::remove_all(portableDataRoot, error);
        return EXIT_FAILURE;
    }

    // Remove the blocked destination and force the pending retry.
    fs::remove_all(configPath, error);
    if (error) {
        std::cerr << "could not unblock config destination\n";
        fs::remove_all(portableDataRoot, error);
        return EXIT_FAILURE;
    }
    if (!squarestar::config::FlushPendingConfigSave(persisted, true) ||
        !squarestar::config::FlushConfigWrites() ||
        !fs::is_regular_file(configPath, error) || error) {
        std::cerr << "save queue did not recover after the destination became writable\n";
        fs::remove_all(portableDataRoot, error);
        return EXIT_FAILURE;
    }

    squarestar::config::CancelPendingConfigSave();
    fs::remove_all(portableDataRoot, error);
    return EXIT_SUCCESS;
#else
    return EXIT_SUCCESS;
#endif
}
