#include "services/api_key_store.hpp"
#include "services/secret_protection.hpp"

#include <atomic>
#include <mutex>

namespace squarestar::secrets {
namespace {

std::mutex g_ApiKeyMutex;
std::string g_FinnhubApiKey;
std::atomic_uint64_t g_ApiKeyRevision{1};

bool ReplaceFinnhubApiKeyIfRevision(std::uint64_t expectedRevision,
                                    const std::string& replacement,
                                    FinnhubApiKeySnapshot* previous,
                                    std::uint64_t* installedRevision) {
    std::lock_guard<std::mutex> lock(g_ApiKeyMutex);
    const std::uint64_t currentRevision =
        g_ApiKeyRevision.load(std::memory_order_acquire);
    if (currentRevision != expectedRevision)
        return false;

    if (previous)
        *previous = {g_FinnhubApiKey, currentRevision};
    SecureClear(g_FinnhubApiKey);
    g_FinnhubApiKey.clear();
    g_FinnhubApiKey.assign(replacement);
    const std::uint64_t revision =
        g_ApiKeyRevision.fetch_add(1, std::memory_order_acq_rel) + 1;
    if (installedRevision)
        *installedRevision = revision;
    return true;
}

} // namespace

std::string GetFinnhubApiKey() {
    std::lock_guard<std::mutex> lock(g_ApiKeyMutex);
    return g_FinnhubApiKey;
}

FinnhubApiKeySnapshot GetFinnhubApiKeySnapshot() {
    std::lock_guard<std::mutex> lock(g_ApiKeyMutex);
    return {g_FinnhubApiKey, g_ApiKeyRevision.load(std::memory_order_acquire)};
}

bool HasFinnhubApiKey() {
    std::lock_guard<std::mutex> lock(g_ApiKeyMutex);
    return !g_FinnhubApiKey.empty();
}

std::uint64_t SetFinnhubApiKey(const std::string& value) {
    std::lock_guard<std::mutex> lock(g_ApiKeyMutex);
    squarestar::secrets::SecureClear(g_FinnhubApiKey);
    g_FinnhubApiKey.clear();
    g_FinnhubApiKey.assign(value);
    return g_ApiKeyRevision.fetch_add(1, std::memory_order_acq_rel) + 1;
}

ApiKeyCommitResult CommitFinnhubApiKeyChange(
    std::uint64_t expectedRevision,
    const std::string& replacement,
    const std::function<bool(std::uint64_t)>& persist) {
    FinnhubApiKeySnapshot previous;
    std::uint64_t installedRevision = 0;
    if (!ReplaceFinnhubApiKeyIfRevision(
            expectedRevision, replacement, &previous, &installedRevision)) {
        return ApiKeyCommitResult::Conflict;
    }

    bool persisted = false;
    try {
        persisted = persist && persist(installedRevision);
    } catch (...) {
        // A persistence exception is a failed transaction, never a partial success.
        persisted = false;
    }
    if (persisted) {
        SecureClear(previous.value);
        return IsApiKeyRevisionCurrent(installedRevision)
                   ? ApiKeyCommitResult::Committed
                   : ApiKeyCommitResult::Superseded;
    }

    const bool restored = ReplaceFinnhubApiKeyIfRevision(
        installedRevision, previous.value, nullptr, nullptr);
    SecureClear(previous.value);
    return restored ? ApiKeyCommitResult::PersistenceFailed
                    : ApiKeyCommitResult::PersistenceFailedSuperseded;
}

std::uint64_t ApiKeyRevision() noexcept {
    return g_ApiKeyRevision.load(std::memory_order_acquire);
}

bool IsApiKeyRevisionCurrent(std::uint64_t revision) noexcept {
    return ApiKeyRevision() == revision;
}

} // namespace squarestar::secrets
