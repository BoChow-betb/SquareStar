#include "services/api_key_store.hpp"
#include "services/secret_protection.hpp"

#include <algorithm>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>

namespace {

int failures = 0;

void Check(bool condition, const char* message) {
    if (condition)
        return;
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

} // namespace

int main() {
    using squarestar::secrets::ApiKeyRevision;
    using squarestar::secrets::ApiKeyCommitResult;
    using squarestar::secrets::CommitFinnhubApiKeyChange;
    using squarestar::secrets::GetFinnhubApiKeySnapshot;
    using squarestar::secrets::HasFinnhubApiKey;
    using squarestar::secrets::ProtectApiKey;
    using squarestar::secrets::ProtectLocalState;
    using squarestar::secrets::SecureClear;
    using squarestar::secrets::SetFinnhubApiKey;
    using squarestar::secrets::UnprotectApiKey;
    using squarestar::secrets::UnprotectLocalState;

    SetFinnhubApiKey("test-key");
    auto savedKey = GetFinnhubApiKeySnapshot();
    Check(savedKey.value == "test-key" && savedKey.revision == ApiKeyRevision(),
          "API key snapshots must pair a value with its exact revision");
    Check(CommitFinnhubApiKeyChange(
              savedKey.revision,
              "replacement",
              [](std::uint64_t) { return false; }) ==
              ApiKeyCommitResult::PersistenceFailed &&
              GetFinnhubApiKeySnapshot().value == "test-key",
          "a failed persistence step must restore the previous process-local key");
    const auto beforeSuccessfulCommit = GetFinnhubApiKeySnapshot();
    Check(CommitFinnhubApiKeyChange(beforeSuccessfulCommit.revision,
                                    "replacement",
                                    [](std::uint64_t) { return true; }) ==
                  ApiKeyCommitResult::Committed &&
              GetFinnhubApiKeySnapshot().value == "replacement",
          "a successful persistence step must commit the replacement key");

    SetFinnhubApiKey("race-A");
    const auto raceStart = GetFinnhubApiKeySnapshot();
    std::mutex raceMutex;
    std::condition_variable raceCv;
    bool firstCommitIsPersisting = false;
    bool allowFirstCommitToFail = false;
    ApiKeyCommitResult firstResult = ApiKeyCommitResult::Committed;

    std::thread firstCommit([&] {
        firstResult = CommitFinnhubApiKeyChange(
            raceStart.revision, "race-B", [&](std::uint64_t) {
                std::unique_lock<std::mutex> lock(raceMutex);
                firstCommitIsPersisting = true;
                raceCv.notify_all();
                raceCv.wait(lock, [&] { return allowFirstCommitToFail; });
                return false;
            });
    });

    {
        std::unique_lock<std::mutex> lock(raceMutex);
        raceCv.wait(lock, [&] { return firstCommitIsPersisting; });
    }
    const auto installedB = GetFinnhubApiKeySnapshot();
    const ApiKeyCommitResult secondResult = CommitFinnhubApiKeyChange(
        installedB.revision, "race-C", [](std::uint64_t) { return true; });
    {
        std::lock_guard<std::mutex> lock(raceMutex);
        allowFirstCommitToFail = true;
    }
    raceCv.notify_all();
    firstCommit.join();

    Check(secondResult == ApiKeyCommitResult::Committed,
          "a concurrent newer key commit must succeed");
    Check(firstResult == ApiKeyCommitResult::PersistenceFailedSuperseded,
          "a failed older transaction must report that a newer key superseded it");
    Check(GetFinnhubApiKeySnapshot().value == "race-C",
          "a failed older transaction must never roll back a newer successful key");

    const auto staleSnapshot = raceStart;
    Check(CommitFinnhubApiKeyChange(staleSnapshot.revision,
                                    "stale-write",
                                    [](std::uint64_t) { return true; }) ==
                  ApiKeyCommitResult::Conflict &&
              GetFinnhubApiKeySnapshot().value == "race-C",
          "a stale expected revision must not overwrite the current key");

    SetFinnhubApiKey("");
    const auto clearedKey = GetFinnhubApiKeySnapshot();
    Check(clearedKey.value.empty() && !HasFinnhubApiKey() &&
              clearedKey.revision > savedKey.revision,
          "clearing must remove the process-local key and invalidate prior snapshots");
    Check(!squarestar::secrets::IsApiKeyRevisionCurrent(savedKey.revision) &&
              squarestar::secrets::IsApiKeyRevisionCurrent(clearedKey.revision),
          "provider fallbacks must reject captured stale key revisions");
    SecureClear(savedKey.value);

    const auto protectedEmpty = ProtectApiKey("");
    Check(protectedEmpty && protectedEmpty->empty(),
          "an empty key should round-trip as an empty value");
    const auto unprotectedEmpty = UnprotectApiKey("");
    Check(unprotectedEmpty && unprotectedEmpty->empty(),
          "an empty protected value should decode as empty");
    const auto protectedEmptyState = ProtectLocalState("");
    Check(protectedEmptyState && protectedEmptyState->empty(),
          "an empty private-state payload should round-trip as empty");
    const auto unprotectedEmptyState = UnprotectLocalState("");
    Check(unprotectedEmptyState && unprotectedEmptyState->empty(),
          "an empty protected private-state value should decode as empty");

    std::string sensitive = "test-secret-value";
    SecureClear(sensitive);
    Check(std::all_of(sensitive.begin(), sensitive.end(),
                      [](char value) { return value == '\0'; }),
          "SecureClear should overwrite the active string buffer");

#ifdef _WIN32
    const std::string key = "squarestar-dpapi-round-trip";
    const auto protectedKey = ProtectApiKey(key);
    Check(protectedKey && !protectedKey->empty() && *protectedKey != key,
          "DPAPI should return non-plaintext protected data");
    if (protectedKey) {
        auto roundTrip = UnprotectApiKey(*protectedKey);
        Check(roundTrip && *roundTrip == key,
              "DPAPI protected data should decrypt for the same Windows user");
        if (roundTrip)
            SecureClear(*roundTrip);

        std::string corrupted = *protectedKey;
        corrupted.front() = corrupted.front() == '0' ? '1' : '0';
        Check(!UnprotectApiKey(corrupted),
              "corrupted DPAPI ciphertext should fail closed");
    }

    const std::string privateState = R"json({"watchlist":["AAPL"],"searchHistory":["MSFT"]})json";
    const auto protectedState = ProtectLocalState(privateState);
    Check(protectedState && !protectedState->empty() && *protectedState != privateState,
          "DPAPI should protect private local state from plaintext inspection");
    if (protectedState) {
        auto roundTripState = UnprotectLocalState(*protectedState);
        Check(roundTripState && *roundTripState == privateState,
              "private local state should decrypt for the same Windows user");
        if (roundTripState)
            SecureClear(*roundTripState);
    }
#else
    Check(!ProtectApiKey("non-empty"),
          "non-Windows builds must not persist an unprotected key");
    Check(!UnprotectApiKey("0011"),
          "non-Windows builds must not claim to decrypt DPAPI data");
    Check(!ProtectLocalState("private-state"),
          "non-Windows builds must not persist private state without DPAPI");
    Check(!UnprotectLocalState("0011"),
          "non-Windows builds must not claim to decrypt private DPAPI state");
#endif

    if (failures != 0)
        return 1;
    std::cout << "All secret protection tests passed\n";
    return 0;
}
