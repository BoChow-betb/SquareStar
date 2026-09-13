#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace squarestar::secrets {

struct FinnhubApiKeySnapshot {
    std::string value;
    std::uint64_t revision = 0;
};

enum class ApiKeyCommitResult {
    Committed,
    Superseded,
    Conflict,
    PersistenceFailed,
    PersistenceFailedSuperseded,
};

std::string GetFinnhubApiKey();
FinnhubApiKeySnapshot GetFinnhubApiKeySnapshot();
bool HasFinnhubApiKey();
std::uint64_t SetFinnhubApiKey(const std::string& value);
ApiKeyCommitResult CommitFinnhubApiKeyChange(
    std::uint64_t expectedRevision,
    const std::string& replacement,
    const std::function<bool(std::uint64_t)>& persist);
std::uint64_t ApiKeyRevision() noexcept;
bool IsApiKeyRevisionCurrent(std::uint64_t revision) noexcept;

} // namespace squarestar::secrets
