#pragma once

#include "services/config_persistence.hpp"

#include <optional>

namespace squarestar::application {
struct PersistedStateConstView;
}

namespace squarestar::config {

// UI/controller code marks configuration dirty instead of issuing writes
// directly. The main loop flushes the latest snapshot after a short debounce,
// coalescing bursts such as text input, toggles, and tab/layout changes.
struct ConfigSaveFailureNotice {
    unsigned consecutiveFailures = 0;
};

void RequestConfigSave();
[[nodiscard]] double SecondsUntilConfigSaveDue() noexcept;
[[nodiscard]] std::optional<ConfigSaveFailureNotice>
ConsumeConfigSaveFailureNotice() noexcept;
[[nodiscard]] bool FlushPendingConfigSave(
    squarestar::application::PersistedStateConstView state,
    bool force = false);

// Immediate snapshots are reserved for operations with transactional semantics
// such as secret replacement callbacks.
[[nodiscard]] bool PersistConfigSnapshot(
    squarestar::application::PersistedStateConstView state,
    ConfigWriteCompletion completion = {});
[[nodiscard]] bool PersistConfigSynchronously(
    squarestar::application::PersistedStateConstView state,
    std::optional<std::uint64_t> requiredApiKeyRevision = std::nullopt);
void CancelPendingConfigSave() noexcept;

} // namespace squarestar::config
