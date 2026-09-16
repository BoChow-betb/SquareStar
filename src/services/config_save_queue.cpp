#include "services/config_save_queue.hpp"

#include "application/main_loop_signal.hpp"
#include "application/persisted_state.hpp"
#include "services/diagnostic_log.hpp"

#include <algorithm>
#include <chrono>
#include <mutex>
#include <utility>

namespace squarestar::config {
namespace {

constexpr auto kConfigSaveDebounce = std::chrono::milliseconds(250);
constexpr auto kConfigSaveRetryMaximum = std::chrono::seconds(30);

struct SaveQueueState {
    std::mutex mutex;
    bool pending = false;
    bool failureNoticePending = false;
    unsigned consecutiveFailures = 0;
    std::chrono::steady_clock::time_point dueAt{};
};

SaveQueueState& SaveQueue() {
    static SaveQueueState state;
    return state;
}

void ScheduleAt(std::chrono::steady_clock::time_point dueAt) {
    SaveQueueState& queue = SaveQueue();
    {
        std::lock_guard<std::mutex> lock(queue.mutex);
        queue.pending = true;
        queue.dueAt = dueAt;
    }
    squarestar::application::WakeMainLoop();
}

void ScheduleNoLaterThan(std::chrono::steady_clock::time_point dueAt) {
    SaveQueueState& queue = SaveQueue();
    {
        std::lock_guard<std::mutex> lock(queue.mutex);
        if (!queue.pending || queue.dueAt > dueAt) {
            queue.pending = true;
            queue.dueAt = dueAt;
        }
    }
    squarestar::application::WakeMainLoop();
}

std::chrono::seconds RetryDelayForFailureCount(unsigned failureCount) noexcept {
    const unsigned exponent =
        std::min(failureCount > 0 ? failureCount - 1 : 0, 5u);
    const auto delay = std::chrono::seconds(1u << exponent);
    return std::min(delay, kConfigSaveRetryMaximum);
}

void CompleteConfigWrite(bool succeeded) noexcept {
    if (succeeded) {
        SaveQueueState& queue = SaveQueue();
        std::lock_guard<std::mutex> lock(queue.mutex);
        queue.consecutiveFailures = 0;
        queue.failureNoticePending = false;
        return;
    }

    unsigned failureCount = 1;
    {
        SaveQueueState& queue = SaveQueue();
        std::lock_guard<std::mutex> lock(queue.mutex);
        if (queue.consecutiveFailures < 64)
            ++queue.consecutiveFailures;
        failureCount = queue.consecutiveFailures;

        if (failureCount == 1)
            queue.failureNoticePending = true;
    }

    squarestar::diagnostics::WriteDiagnosticEvent(
        {"local", "persistence", "config-save-completion", 0});
    ScheduleNoLaterThan(
        std::chrono::steady_clock::now() + RetryDelayForFailureCount(failureCount));
}

ConfigWriteCompletion WrapConfigCompletion(
    ConfigWriteCompletion completion = {}) {
    return [completion = std::move(completion)](bool succeeded) mutable {
        CompleteConfigWrite(succeeded);
        if (completion)
            completion(succeeded);
    };
}

}

void RequestConfigSave() {
    ScheduleAt(std::chrono::steady_clock::now() + kConfigSaveDebounce);
}

double SecondsUntilConfigSaveDue() noexcept {
    SaveQueueState& queue = SaveQueue();
    std::lock_guard<std::mutex> lock(queue.mutex);
    if (!queue.pending)
        return 30.0;
    const double remaining = std::chrono::duration<double>(
                                 queue.dueAt - std::chrono::steady_clock::now())
                                 .count();
    return std::clamp(remaining, 0.0, 30.0);
}

std::optional<ConfigSaveFailureNotice>
ConsumeConfigSaveFailureNotice() noexcept {
    SaveQueueState& queue = SaveQueue();
    std::lock_guard<std::mutex> lock(queue.mutex);
    if (!queue.failureNoticePending)
        return std::nullopt;
    queue.failureNoticePending = false;
    return ConfigSaveFailureNotice{queue.consecutiveFailures};
}

bool FlushPendingConfigSave(squarestar::application::PersistedStateConstView state,
                            bool force) {
    SaveQueueState& queue = SaveQueue();
    {
        std::lock_guard<std::mutex> lock(queue.mutex);
        if (!queue.pending)
            return true;
        if (!force && std::chrono::steady_clock::now() < queue.dueAt)
            return true;
        queue.pending = false;
        queue.dueAt = {};
    }


    return SaveConfig(state, WrapConfigCompletion());
}

bool PersistConfigSnapshot(squarestar::application::PersistedStateConstView state,
                           ConfigWriteCompletion completion) {


    CancelPendingConfigSave();
    return SaveConfig(
        state, WrapConfigCompletion(std::move(completion)));
}

bool PersistConfigSynchronously(
    squarestar::application::PersistedStateConstView state,
    std::optional<std::uint64_t> requiredApiKeyRevision) {
    CancelPendingConfigSave();
    const bool queued = SaveConfig(state, {}, requiredApiKeyRevision);
    return FlushConfigWrites() && queued;
}

void CancelPendingConfigSave() noexcept {
    SaveQueueState& queue = SaveQueue();
    std::lock_guard<std::mutex> lock(queue.mutex);
    queue.pending = false;
    queue.dueAt = {};
}

}
