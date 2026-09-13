#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace squarestar::application {
struct PersistedStateConstView;
struct PersistedStateView;
} // namespace squarestar::application

namespace squarestar::config {

using ConfigWriteCompletion = std::function<void(bool)>;

// Configuration writes are serialized by one process-local writer. SquareStar
// already enforces a per-user single-instance guard, so persistence only needs
// crash-safe file replacement here; it does not implement a second multi-writer
// database protocol.
bool SubmitConfigWrite(std::string path,
                       std::string payload,
                       ConfigWriteCompletion completion = {});
bool FlushConfigWrites();

struct TemporaryDataCleanupResult {
    std::size_t filesRemoved = 0;
    std::size_t filesInUse = 0;
};

bool DeletePersistentApplicationState();
TemporaryDataCleanupResult ClearSquareStarTemporaryData();
[[nodiscard]] bool HasPersistedConfig() noexcept;

// Preserve an unreadable canonical state file without deleting it so startup can
// recover with defaults. Returns the backup filename on success.
std::optional<std::string> PreserveRejectedConfigForRecovery();

bool SaveConfig(squarestar::application::PersistedStateConstView state,
                ConfigWriteCompletion completion = {},
                std::optional<std::uint64_t> requiredApiKeyRevision = std::nullopt);

enum class ConfigLoadStatus { NoState, Loaded, Rejected };
ConfigLoadStatus LoadConfig(squarestar::application::PersistedStateView state);

} // namespace squarestar::config
