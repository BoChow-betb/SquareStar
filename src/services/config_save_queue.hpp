#pragma once

#include "services/config_persistence.hpp"

#include <optional>

namespace squarestar::application {
struct PersistedStateConstView;
}

namespace squarestar::config {


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


[[nodiscard]] bool PersistConfigSnapshot(
    squarestar::application::PersistedStateConstView state,
    ConfigWriteCompletion completion = {});
[[nodiscard]] bool PersistConfigSynchronously(
    squarestar::application::PersistedStateConstView state,
    std::optional<std::uint64_t> requiredApiKeyRevision = std::nullopt);
void CancelPendingConfigSave() noexcept;

}
