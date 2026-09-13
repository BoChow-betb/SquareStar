#include "services/config_persistence.hpp"

#include "platform/application_paths.hpp"
#include "platform/windows_headers.hpp"
#include "platform/windows_path.hpp"
#include "services/diagnostic_log.hpp"

#include <algorithm>
#include <condition_variable>
#include <ctime>
#include <deque>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#ifndef _WIN32
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace squarestar::config {
namespace {

void ReportConfigWriteFailure(const char* operation) noexcept {
    squarestar::diagnostics::WriteDiagnosticEvent(
        {"local", "persistence", operation, 0});
}

struct ConfigWriteJob {
    std::string path;
    std::string payload;
    ConfigWriteCompletion completion;
};

void Complete(ConfigWriteCompletion completion, bool succeeded) noexcept {
    if (!completion)
        return;
    try {
        completion(succeeded);
    } catch (...) {
        // Completion observers must never terminate the persistence worker.
    }
}

bool WriteTemporaryConfigDurably(const std::string& temporaryPath,
                                 std::string_view payload) {
#ifdef _WIN32
    const std::wstring temporaryWide =
        squarestar::platform::Utf8PathToWide(temporaryPath);
    if (temporaryWide.empty())
        return false;
    HANDLE file = CreateFileW(temporaryWide.c_str(),
                              GENERIC_WRITE,
                              0,
                              nullptr,
                              CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH,
                              nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;

    bool succeeded = true;
    std::size_t offset = 0;
    while (offset < payload.size()) {
        const DWORD request = static_cast<DWORD>(std::min<std::size_t>(
            payload.size() - offset, static_cast<std::size_t>(MAXDWORD)));
        DWORD written = 0;
        if (!WriteFile(file, payload.data() + offset, request, &written, nullptr) ||
            written == 0) {
            succeeded = false;
            break;
        }
        offset += static_cast<std::size_t>(written);
    }
    if (succeeded)
        succeeded = FlushFileBuffers(file) != FALSE;
    if (!CloseHandle(file))
        succeeded = false;
    return succeeded;
#else
    const int descriptor = ::open(temporaryPath.c_str(),
                                  O_WRONLY | O_CREAT | O_TRUNC,
                                  0600);
    if (descriptor < 0)
        return false;

    bool succeeded = true;
    std::size_t offset = 0;
    while (offset < payload.size()) {
        const ssize_t written = ::write(descriptor,
                                        payload.data() + offset,
                                        payload.size() - offset);
        if (written < 0) {
            if (errno == EINTR)
                continue;
            succeeded = false;
            break;
        }
        if (written == 0) {
            succeeded = false;
            break;
        }
        offset += static_cast<std::size_t>(written);
    }
    if (succeeded && ::fsync(descriptor) != 0)
        succeeded = false;
    if (::close(descriptor) != 0)
        succeeded = false;
    return succeeded;
#endif
}

bool ReplaceConfigFile(const std::string& temporaryPath, const std::string& path) {
#ifdef _WIN32
    const std::wstring temporaryWide =
        squarestar::platform::Utf8PathToWide(temporaryPath);
    const std::wstring pathWide = squarestar::platform::Utf8PathToWide(path);
    return !temporaryWide.empty() && !pathWide.empty() &&
           MoveFileExW(temporaryWide.c_str(),
                       pathWide.c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
#else
    std::error_code error;
    std::filesystem::rename(temporaryPath, path, error);
    if (error)
        return false;
#if defined(__linux__)
    // fsync(temp) makes the bytes durable. Flush the containing directory too
    // so the atomic name replacement survives sudden power loss.
    std::filesystem::path parent = std::filesystem::path(path).parent_path();
    if (parent.empty())
        parent = ".";
    const int directory = ::open(parent.c_str(), O_RDONLY | O_DIRECTORY);
    if (directory < 0)
        return false;
    const bool durable = ::fsync(directory) == 0;
    const bool closed = ::close(directory) == 0;
    return durable && closed;
#else
    return true;
#endif
#endif
}

bool WriteConfigPayload(const ConfigWriteJob& job) {
    if (job.path.empty())
        return false;

    const std::filesystem::path destination =
        squarestar::platform::Utf8FilesystemPath(job.path);
    const std::filesystem::path parent = destination.parent_path();
    if (!parent.empty()) {
        std::error_code directoryError;
        std::filesystem::create_directories(parent, directoryError);
        if (directoryError) {
            ReportConfigWriteFailure("config-create-directory");
            return false;
        }
    }

    // There is one writer per user instance, so one stable temp file is enough.
    // Replace a temp file left by a crash on the next save.
    const std::string temporaryPath = job.path + ".tmp";
    if (!WriteTemporaryConfigDurably(temporaryPath, job.payload)) {
        ReportConfigWriteFailure("config-write-temporary");
        std::error_code error;
        std::filesystem::remove(
            squarestar::platform::Utf8FilesystemPath(temporaryPath), error);
        return false;
    }
    if (!ReplaceConfigFile(temporaryPath, job.path)) {
        ReportConfigWriteFailure("config-atomic-replace");
        std::error_code error;
        std::filesystem::remove(
            squarestar::platform::Utf8FilesystemPath(temporaryPath), error);
        return false;
    }
    return true;
}

class ConfigWriter final {
  public:
    ConfigWriter() : worker_([this] { Run(); }) {}
    ~ConfigWriter() {
        (void)Flush();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopping_ = true;
        }
        cv_.notify_all();
    }

    bool Submit(ConfigWriteJob job) {
        if (job.path.empty())
            return false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopping_)
                return false;
            if (jobs_.empty() && !writing_)
                batchSucceeded_ = true;
            jobs_.push_back(std::move(job));
        }
        cv_.notify_one();
        return true;
    }

    bool Flush() {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] { return jobs_.empty() && !writing_; });
        const bool succeeded = batchSucceeded_;
        batchSucceeded_ = true;
        return succeeded;
    }

  private:
    void Run() {
        for (;;) {
            ConfigWriteJob job;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [this] { return stopping_ || !jobs_.empty(); });
                if (stopping_ && jobs_.empty())
                    return;
                job = std::move(jobs_.front());
                jobs_.pop_front();
                writing_ = true;
            }

            const bool succeeded = WriteConfigPayload(job);
            Complete(std::move(job.completion), succeeded);

            {
                std::lock_guard<std::mutex> lock(mutex_);
                batchSucceeded_ = batchSucceeded_ && succeeded;
                writing_ = false;
            }
            cv_.notify_all();
        }
    }

    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<ConfigWriteJob> jobs_;
    bool writing_ = false;
    bool stopping_ = false;
    bool batchSucceeded_ = true;
    std::jthread worker_;
};

ConfigWriter& GetConfigWriter() {
    static ConfigWriter writer;
    return writer;
}

} // namespace

std::optional<std::string> PreserveRejectedConfigForRecovery() {
    const std::string configPath = squarestar::platform::GetConfigPath();
    if (configPath.empty())
        return std::nullopt;

    const std::filesystem::path source =
        squarestar::platform::Utf8FilesystemPath(configPath);
    std::error_code error;
    if (!std::filesystem::is_regular_file(source, error) || error)
        return std::nullopt;

    std::filesystem::path directory = source.parent_path();
    if (directory.empty())
        directory = std::filesystem::current_path(error);
    if (error)
        return std::nullopt;

    const std::int64_t epoch =
        static_cast<std::int64_t>(std::time(nullptr));
    for (int attempt = 0; attempt < 100; ++attempt) {
        std::string filename =
            "config.rejected-" + std::to_string(epoch);
        if (attempt != 0)
            filename += "-" + std::to_string(attempt);
        filename += ".json";

        const std::filesystem::path destination =
            directory / squarestar::platform::Utf8FilesystemPath(filename);
        error.clear();
        const bool destinationExists =
            std::filesystem::exists(destination, error);
        if (error)
            return std::nullopt;
        if (destinationExists)
            continue;

        std::filesystem::rename(source, destination, error);
        if (!error)
            return filename;

        // The application holds the per-user single-instance guard, so a
        // rename failure is not expected to be a competing SquareStar writer.
        return std::nullopt;
    }
    return std::nullopt;
}

bool SubmitConfigWrite(std::string path,
                       std::string payload,
                       ConfigWriteCompletion completion) {
    const bool queued = GetConfigWriter().Submit(
        {std::move(path), std::move(payload), std::move(completion)});
    if (!queued)
        ReportConfigWriteFailure("config-writer-queue");
    return queued;
}

bool FlushConfigWrites() {
    return GetConfigWriter().Flush();
}

} // namespace squarestar::config
