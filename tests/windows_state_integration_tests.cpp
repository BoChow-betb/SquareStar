#include "platform/single_instance_guard.hpp"
#include "platform/windows_path.hpp"
#include "services/config_persistence.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#ifdef _WIN32
#include "platform/windows_headers.hpp"
#endif

namespace {

#ifdef _WIN32

std::string HexEncode(std::string_view input) {
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string output;
    output.reserve(input.size() * 2);
    for (const char raw : input) {
        const unsigned char byte = static_cast<unsigned char>(raw);
        output.push_back(kDigits[(byte >> 4) & 0x0f]);
        output.push_back(kDigits[byte & 0x0f]);
    }
    return output;
}

int HexNibble(char value) {
    if (value >= '0' && value <= '9')
        return value - '0';
    if (value >= 'a' && value <= 'f')
        return value - 'a' + 10;
    if (value >= 'A' && value <= 'F')
        return value - 'A' + 10;
    return -1;
}

std::string HexDecode(std::string_view input) {
    if ((input.size() % 2) != 0)
        return {};
    std::string output;
    output.reserve(input.size() / 2);
    for (std::size_t i = 0; i < input.size(); i += 2) {
        const int high = HexNibble(input[i]);
        const int low = HexNibble(input[i + 1]);
        if (high < 0 || low < 0)
            return {};
        output.push_back(static_cast<char>((high << 4) | low));
    }
    return output;
}

std::wstring AsciiWide(std::string_view input) {
    return std::wstring(input.begin(), input.end());
}

bool WriteMarker(const std::string& path) {
    std::ofstream marker(squarestar::platform::Utf8FilesystemPath(path),
                         std::ios::binary | std::ios::trunc);
    marker << "ready\n";
    return marker.good();
}

bool WaitForFile(const std::string& path, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    const std::filesystem::path file =
        squarestar::platform::Utf8FilesystemPath(path);
    do {
        std::error_code error;
        if (std::filesystem::exists(file, error) && !error)
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    } while (std::chrono::steady_clock::now() < deadline);
    return false;
}

struct ChildProcess {
    PROCESS_INFORMATION process{};

    ChildProcess() = default;
    ChildProcess(const ChildProcess&) = delete;
    ChildProcess& operator=(const ChildProcess&) = delete;
    ChildProcess(ChildProcess&& other) noexcept : process(other.process) {
        other.process = {};
    }
    ChildProcess& operator=(ChildProcess&& other) noexcept {
        if (this == &other)
            return *this;
        Close();
        process = other.process;
        other.process = {};
        return *this;
    }
    ~ChildProcess() { Close(); }

    void Close() noexcept {
        if (process.hThread) {
            CloseHandle(process.hThread);
            process.hThread = nullptr;
        }
        if (process.hProcess) {
            CloseHandle(process.hProcess);
            process.hProcess = nullptr;
        }
    }

    [[nodiscard]] bool Valid() const noexcept { return process.hProcess != nullptr; }
};

ChildProcess LaunchChild(const std::vector<std::string>& arguments) {
    ChildProcess child;
    const std::wstring executable = squarestar::platform::ExecutablePathWide();
    if (executable.empty())
        return child;

    std::wstring commandLine = L"\"" + executable + L"\"";
    for (const std::string& argument : arguments) {
        commandLine.push_back(L' ');
        commandLine += AsciiWide(argument);
    }
    std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
    mutableCommand.push_back(L'\0');

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    if (!CreateProcessW(executable.c_str(),
                        mutableCommand.data(),
                        nullptr,
                        nullptr,
                        FALSE,
                        CREATE_NO_WINDOW,
                        nullptr,
                        nullptr,
                        &startup,
                        &child.process)) {
        child.process = {};
    }
    return child;
}

bool WaitForExit(ChildProcess& child, DWORD timeoutMilliseconds, DWORD& exitCode) {
    if (!child.Valid())
        return false;
    if (WaitForSingleObject(child.process.hProcess, timeoutMilliseconds) != WAIT_OBJECT_0)
        return false;
    return GetExitCodeProcess(child.process.hProcess, &exitCode) != FALSE;
}

int RunInstanceHoldChild(int argc, char** argv) {
    if (argc != 5)
        return 30;
    const std::string ready = HexDecode(argv[2]);
    const std::string stop = HexDecode(argv[3]);
    const std::string instanceName = HexDecode(argv[4]);
    const std::wstring wideInstanceName =
        squarestar::platform::Utf8PathToWide(instanceName);
    if (wideInstanceName.empty())
        return 34;
    squarestar::platform::SingleInstanceGuard guard(wideInstanceName);
    if (!guard.Acquire())
        return 31;
    if (!WriteMarker(ready))
        return 32;
    if (!WaitForFile(stop, std::chrono::seconds(10)))
        return 33;
    return 0;
}

int RunParent() {
    namespace fs = std::filesystem;
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path root = fs::temp_directory_path() /
                          (L"squarestar-win-state-integration-" +
                           std::to_wstring(suffix));
    std::error_code error;
    fs::create_directories(root, error);
    if (error) {
        std::cerr << "could not create Windows state integration directory\n";
        return EXIT_FAILURE;
    }

    const std::wstring instanceName =
        squarestar::platform::CurrentUserSquareStarInstanceMutexName();
    if (instanceName.rfind(L"Global\\SquareStar-StateWriter-", 0) != 0 ||
        instanceName.find(L"1.0.1") != std::wstring::npos) {
        std::cerr << "single-instance identity is not global and version-independent\n";
        fs::remove_all(root, error);
        return EXIT_FAILURE;
    }

    // Validation may run while SquareStar owns the production mutex. Use a unique
    // sibling name so the test does not collide with the running app.
    const std::wstring isolatedInstanceName =
        instanceName + L"-integration-" + std::to_wstring(GetCurrentProcessId()) +
        L"-" + std::to_wstring(suffix);
    const std::string isolatedInstanceNameUtf8 =
        squarestar::platform::WidePathToUtf8(isolatedInstanceName);
    if (isolatedInstanceNameUtf8.empty()) {
        std::cerr << "could not encode isolated instance identity\n";
        fs::remove_all(root, error);
        return EXIT_FAILURE;
    }

    const fs::path instanceReady = root / L"instance-ready";
    const fs::path instanceStop = root / L"instance-stop";
    const std::string instanceReadyUtf8 =
        squarestar::platform::WidePathToUtf8(instanceReady.native());
    const std::string instanceStopUtf8 =
        squarestar::platform::WidePathToUtf8(instanceStop.native());
    ChildProcess holder = LaunchChild({"--child-instance-hold",
                                       HexEncode(instanceReadyUtf8),
                                       HexEncode(instanceStopUtf8),
                                       HexEncode(isolatedInstanceNameUtf8)});
    if (!holder.Valid() ||
        !WaitForFile(instanceReadyUtf8, std::chrono::seconds(5))) {
        std::cerr << "could not start isolated instance-guard holder\n";
        fs::remove_all(root, error);
        return EXIT_FAILURE;
    }

    squarestar::platform::SingleInstanceGuard competingGuard(isolatedInstanceName);
    if (competingGuard.Acquire()) {
        std::cerr << "a second process acquired the isolated instance identity\n";
        fs::remove_all(root, error);
        return EXIT_FAILURE;
    }
    if (!WriteMarker(instanceStopUtf8)) {
        std::cerr << "could not release the instance-guard child\n";
        fs::remove_all(root, error);
        return EXIT_FAILURE;
    }
    DWORD holderExit = 0;
    if (!WaitForExit(holder, 10000, holderExit) || holderExit != 0) {
        std::cerr << "instance-guard holder did not exit cleanly\n";
        fs::remove_all(root, error);
        return EXIT_FAILURE;
    }

    squarestar::platform::SingleInstanceGuard afterRelease(isolatedInstanceName);
    if (!afterRelease.Acquire()) {
        std::cerr << "instance identity stayed locked after the owning process exited\n";
        fs::remove_all(root, error);
        return EXIT_FAILURE;
    }

    fs::remove_all(root, error);
    return EXIT_SUCCESS;
}

#endif

} // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    if (argc > 1 && std::string_view(argv[1]) == "--child-instance-hold")
        return RunInstanceHoldChild(argc, argv);
    return RunParent();
#else
    (void)argc;
    (void)argv;
    return EXIT_SUCCESS;
#endif
}
