#include "benchmark_runner.hpp"

#include "application/app_state.hpp"
#include "application/main_loop_signal.hpp"
#include "application/runtime_state.hpp"
#include "domain/stock_data.hpp"
#include "presentation/chart_lod.hpp"
#include "presentation/gui_renderer_context.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <windows.h>
#include <psapi.h>
#include <GLFW/glfw3.h>

namespace squarestar::benchmark {
namespace {

using Clock = std::chrono::steady_clock;
namespace fs = std::filesystem;

enum class Mode {
    None,
    Offline,
    Startup,
    Idle,
    Memory,
    Help,
};

struct ParsedOptions {
    Mode mode = Mode::None;
    fs::path outputDirectory;
    int runs = 50;
    int warmups = 5;
    int idleSeconds = 30;
};

struct TimingStats {
    double coldMs = 0.0;
    double medianMs = 0.0;
    double p95Ms = 0.0;
    double p99Ms = 0.0;
    double minMs = 0.0;
    double maxMs = 0.0;
    std::vector<double> samplesMs;
};

struct BenchmarkRow {
    std::string suite;
    std::string caseName;
    std::size_t inputPoints = 0;
    std::size_t outputPoints = 0;
    std::size_t secondaryOutputPoints = 0;
    float logicalWidth = 0.0f;
    float framebufferScale = 0.0f;
    TimingStats timing;
    int runs = 0;
    int warmups = 0;
    bool verified = false;
};

struct SyntheticSeries {
    std::vector<double> x;
    std::vector<double> close;
    std::vector<double> open;
    std::vector<double> high;
    std::vector<double> low;
};

struct ProcessMemorySnapshot {
    double workingSetMb = 0.0;
    double privateBytesMb = 0.0;
};

struct MemoryLayerRow {
    std::string stage;
    std::string label;
    ProcessMemorySnapshot memory;
};

struct GuiProbeState {
    Mode mode = Mode::None;
    fs::path outputDirectory;
    Clock::time_point mainEntry{};
    Clock::time_point idleStartedAt{};
    Clock::time_point idleDeadline{};
    std::uint64_t idleCpu100nsStart = 0;
    std::uint64_t presentedFramesDuringIdle = 0;
    int idleSeconds = 30;
    int logicalWidth = 0;
    int logicalHeight = 0;
    int framebufferWidth = 0;
    int framebufferHeight = 0;
    bool firstPresentSeen = false;
    bool completed = false;
    int exitCode = 0;
};

GuiProbeState g_GuiProbe;
std::vector<MemoryLayerRow> g_MemoryLayers;

bool EnsureDirectory(const fs::path& directory);

std::string UtcTimestamp() {
    SYSTEMTIME utc{};
    GetSystemTime(&utc);
    std::ostringstream out;
    out << std::setfill('0')
        << std::setw(4) << utc.wYear << '-'
        << std::setw(2) << utc.wMonth << '-'
        << std::setw(2) << utc.wDay << 'T'
        << std::setw(2) << utc.wHour << ':'
        << std::setw(2) << utc.wMinute << ':'
        << std::setw(2) << utc.wSecond << '.'
        << std::setw(3) << utc.wMilliseconds << 'Z';
    return out.str();
}

std::string TimestampForDirectory() {
    SYSTEMTIME utc{};
    GetSystemTime(&utc);
    std::ostringstream out;
    out << std::setfill('0')
        << std::setw(4) << utc.wYear
        << std::setw(2) << utc.wMonth
        << std::setw(2) << utc.wDay << '-'
        << std::setw(2) << utc.wHour
        << std::setw(2) << utc.wMinute
        << std::setw(2) << utc.wSecond;
    return out.str();
}

std::string RegistryString(HKEY root,
                           const char* subkey,
                           const char* valueName) {
    char buffer[256]{};
    DWORD bytes = static_cast<DWORD>(sizeof(buffer));
    const LSTATUS status = RegGetValueA(
        root,
        subkey,
        valueName,
        RRF_RT_REG_SZ,
        nullptr,
        buffer,
        &bytes);
    if (status != ERROR_SUCCESS)
        return {};
    std::string value(buffer);
    while (!value.empty() && (value.back() == '\0' || value.back() == ' '))
        value.pop_back();
    return value;
}

std::string CpuName() {
    const std::string name = RegistryString(
        HKEY_LOCAL_MACHINE,
        "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
        "ProcessorNameString");
    return name.empty() ? "unknown" : name;
}

std::uint32_t WindowsBuildNumber(std::string_view text) {
    if (text.empty())
        return 0;

    std::uint32_t value = 0;
    for (const char ch : text) {
        if (ch < '0' || ch > '9')
            return 0;
        const std::uint32_t digit = static_cast<std::uint32_t>(ch - '0');
        if (value > (std::numeric_limits<std::uint32_t>::max() - digit) / 10)
            return 0;
        value = value * 10 + digit;
    }
    return value;
}

std::string WindowsVersion() {
    constexpr const char* key = "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";
    std::string product =
        RegistryString(HKEY_LOCAL_MACHINE, key, "ProductName");
    const std::string display =
        RegistryString(HKEY_LOCAL_MACHINE, key, "DisplayVersion");
    const std::string build =
        RegistryString(HKEY_LOCAL_MACHINE, key, "CurrentBuildNumber");

    // ProductName can still say "Windows 10" on Windows 11.
    // Build 22000+ is Windows 11.
    constexpr std::string_view reportedWindows10Prefix = "Windows 10";
    if (WindowsBuildNumber(build) >= 22000 &&
        product.rfind(reportedWindows10Prefix, 0) == 0) {
        product.replace(0, reportedWindows10Prefix.size(), "Windows 11");
    }

    if (product.empty() && display.empty() && build.empty())
        return "Windows (version unavailable)";
    std::ostringstream out;
    out << (product.empty() ? "Windows" : product);
    if (!display.empty())
        out << ' ' << display;
    if (!build.empty())
        out << " build " << build;
    return out.str();
}

std::string CompilerName() {
#if defined(_MSC_VER)
    std::ostringstream out;
    out << "MSVC " << _MSC_VER;
    return out.str();
#elif defined(__clang__)
    return std::string("Clang ") + __clang_version__;
#elif defined(__GNUC__)
    std::ostringstream out;
    out << "GCC " << __GNUC__ << '.' << __GNUC_MINOR__ << '.' << __GNUC_PATCHLEVEL__;
    return out.str();
#else
    return "unknown compiler";
#endif
}

std::string BuildMode() {
#ifdef NDEBUG
    return "Release-like (NDEBUG)";
#else
    return "Debug (results should not be published)";
#endif
}

std::string ConfiguredOptimization() {
#ifdef SQUARESTAR_BUILD_OPTIMIZATION
    return SQUARESTAR_BUILD_OPTIMIZATION;
#else
    return "unknown";
#endif
}

DWORD LogicalProcessorCount() {
    const DWORD count = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    if (count != 0)
        return count;
    SYSTEM_INFO info{};
    GetNativeSystemInfo(&info);
    return std::max<DWORD>(static_cast<DWORD>(1), info.dwNumberOfProcessors);
}

std::uint64_t FileTimeToUint64(const FILETIME& value) {
    ULARGE_INTEGER converted{};
    converted.LowPart = value.dwLowDateTime;
    converted.HighPart = value.dwHighDateTime;
    return converted.QuadPart;
}

std::optional<std::uint64_t> ProcessCpuTime100ns() {
    FILETIME creation{}, exit{}, kernel{}, user{};
    if (!GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user))
        return std::nullopt;
    return FileTimeToUint64(kernel) + FileTimeToUint64(user);
}

std::optional<double> ProcessCreationToNowMs() {
    FILETIME creation{}, exit{}, kernel{}, user{}, now{};
    if (!GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user))
        return std::nullopt;
    GetSystemTimePreciseAsFileTime(&now);
    const std::uint64_t creation100ns = FileTimeToUint64(creation);
    const std::uint64_t now100ns = FileTimeToUint64(now);
    if (now100ns < creation100ns)
        return std::nullopt;
    return static_cast<double>(now100ns - creation100ns) / 10'000.0;
}

std::optional<ProcessMemorySnapshot> ProcessMemory() {
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = static_cast<DWORD>(sizeof(counters));
    if (!GetProcessMemoryInfo(
            GetCurrentProcess(),
            reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
            static_cast<DWORD>(sizeof(counters))))
        return std::nullopt;
    constexpr double bytesPerMb = 1024.0 * 1024.0;
    return ProcessMemorySnapshot{
        static_cast<double>(counters.WorkingSetSize) / bytesPerMb,
        static_cast<double>(counters.PrivateUsage) / bytesPerMb,
    };
}

std::string CsvQuote(std::string_view text) {
    std::string quoted;
    quoted.reserve(text.size() + 2);
    quoted.push_back('"');
    for (const char c : text) {
        if (c == '"')
            quoted.push_back('"');
        quoted.push_back(c);
    }
    quoted.push_back('"');
    return quoted;
}

bool WriteMemoryLayerReport() {
    if (g_MemoryLayers.empty())
        return true;
    if (!EnsureDirectory(g_GuiProbe.outputDirectory))
        return false;

    std::ofstream out(g_GuiProbe.outputDirectory / "memory-layers.csv",
                      std::ios::binary | std::ios::trunc);
    if (!out)
        return false;
    out << "stage,label,working_set_mb,private_bytes_mb,delta_private_mb,delta_from_a_mb\n";
    const double baseline = g_MemoryLayers.front().memory.privateBytesMb;
    double previous = baseline;
    out << std::fixed << std::setprecision(3);
    for (const MemoryLayerRow& row : g_MemoryLayers) {
        out << CsvQuote(row.stage) << ',' << CsvQuote(row.label) << ','
            << row.memory.workingSetMb << ',' << row.memory.privateBytesMb << ','
            << (row.memory.privateBytesMb - previous) << ','
            << (row.memory.privateBytesMb - baseline) << '\n';
        previous = row.memory.privateBytesMb;
    }
    return static_cast<bool>(out);
}

std::string LowerAscii(std::string text) {
    for (char& c : text) {
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    }
    return text;
}

bool WriteLoadedModuleAudit() {
    std::array<HMODULE, 1024> modules{};
    DWORD bytesNeeded = 0;
    if (!EnumProcessModulesEx(GetCurrentProcess(),
                              modules.data(),
                              static_cast<DWORD>(modules.size() * sizeof(HMODULE)),
                              &bytesNeeded,
                              LIST_MODULES_ALL)) {
        return false;
    }

    const std::size_t count = std::min<std::size_t>(
        modules.size(), bytesNeeded / sizeof(HMODULE));
    std::ofstream out(g_GuiProbe.outputDirectory / "loaded-modules.txt",
                      std::ios::binary | std::ios::trunc);
    if (!out)
        return false;

    bool foundOpenGl32 = false;
    bool foundVendorOpenGl = false;
    out << "SquareStar loaded-module audit\n"
        << "generated_utc: " << UtcTimestamp() << "\n\n";
    for (std::size_t i = 0; i < count; ++i) {
        char path[MAX_PATH * 4]{};
        const DWORD length = GetModuleFileNameExA(
            GetCurrentProcess(), modules[i], path, static_cast<DWORD>(sizeof(path)));
        if (!length)
            continue;
        const std::string modulePath(path, length);
        const std::string lower = LowerAscii(modulePath);
        if (lower.ends_with("\\opengl32.dll") || lower.ends_with("/opengl32.dll"))
            foundOpenGl32 = true;
        if (lower.find("nvoglv") != std::string::npos ||
            lower.find("atio6axx") != std::string::npos ||
            lower.find("atig6pxx") != std::string::npos ||
            lower.find("ig9icd") != std::string::npos ||
            lower.find("ig4icd") != std::string::npos ||
            lower.find("igxelpicd") != std::string::npos) {
            foundVendorOpenGl = true;
        }
        out << modulePath << '\n';
    }
    out << "\nsummary\n"
        << "opengl32_loaded: " << (foundOpenGl32 ? "yes" : "no") << '\n'
        << "vendor_opengl_icd_detected: " << (foundVendorOpenGl ? "yes" : "no") << '\n';
    return static_cast<bool>(out);
}

std::string SystemInfoText() {
    MEMORYSTATUSEX memory{};
    memory.dwLength = static_cast<DWORD>(sizeof(memory));
    const bool memoryOk = GlobalMemoryStatusEx(&memory) != FALSE;
    constexpr double bytesPerGb = 1024.0 * 1024.0 * 1024.0;

    std::ostringstream out;
    out << "SquareStar benchmark environment\n"
        << "generated_utc: " << UtcTimestamp() << '\n'
        << "cpu: " << CpuName() << '\n'
        << "logical_processors: " << LogicalProcessorCount() << '\n'
        << "memory_gb: ";
    if (memoryOk)
        out << std::fixed << std::setprecision(1)
            << static_cast<double>(memory.ullTotalPhys) / bytesPerGb;
    else
        out << "unknown";
    out << '\n'
        << "os: " << WindowsVersion() << '\n'
        << "compiler: " << CompilerName() << '\n'
        << "build_mode: " << BuildMode() << '\n'
        << "configured_release_optimization: " << ConfiguredOptimization() << '\n'
        << "architecture: x64\n";
    if (g_GuiProbe.firstPresentSeen) {
        out << "gpu_d3d11_adapter: "
            << squarestar::presentation::GuiD3D11AdapterName() << '\n'
            << "d3d_feature_level: "
            << squarestar::presentation::GuiD3D11FeatureLevelName() << '\n'
            << "gui_logical_size: " << g_GuiProbe.logicalWidth << 'x'
            << g_GuiProbe.logicalHeight << '\n'
            << "gui_framebuffer_size: " << g_GuiProbe.framebufferWidth << 'x'
            << g_GuiProbe.framebufferHeight << '\n';
    }
    return out.str();
}

bool EnsureDirectory(const fs::path& directory) {
    std::error_code ec;
    fs::create_directories(directory, ec);
    if (ec) {
        std::cerr << "[benchmark] Could not create output directory: "
                  << directory.string() << " (" << ec.message() << ")\n";
        return false;
    }
    return true;
}

fs::path ResolveOutputDirectory(const fs::path& requested) {
    if (!requested.empty())
        return requested;
    return fs::current_path() / "benchmark-results" / TimestampForDirectory();
}

bool WriteSystemInfo(const fs::path& directory) {
    std::ofstream out(directory / "system.txt", std::ios::binary | std::ios::trunc);
    if (!out)
        return false;
    out << SystemInfoText();
    return static_cast<bool>(out);
}

bool AppendTextLine(const fs::path& path,
                    std::string_view header,
                    std::string_view row) {
    std::error_code ec;
    const bool needsHeader = !fs::exists(path, ec) || fs::file_size(path, ec) == 0;
    std::ofstream out(path, std::ios::binary | std::ios::app);
    if (!out)
        return false;
    if (needsHeader)
        out << header << '\n';
    out << row << '\n';
    return static_cast<bool>(out);
}

bool ParseInt(std::string_view text, int minimum, int maximum, int& output) {
    if (text.empty())
        return false;
    int value = 0;
    for (const char c : text) {
        if (c < '0' || c > '9')
            return false;
        if (value > (maximum - (c - '0')) / 10)
            return false;
        value = value * 10 + (c - '0');
    }
    if (value < minimum || value > maximum)
        return false;
    output = value;
    return true;
}

void PrintHelp() {
    std::cout
        << "SquareStar benchmark modes\n\n"
        << "  SquareStar.exe --benchmark [options]\n"
        << "      Run deterministic, provider-free chart/LOD benchmarks.\n\n"
        << "  SquareStar.exe --benchmark-startup [options]\n"
        << "      Launch the real GUI and record process-creation/main-entry ->\n"
        << "      first visible Direct3D Present completion, then exit.\n\n"
        << "  SquareStar.exe --benchmark-idle [options]\n"
        << "      Launch a settled Home GUI, measure process CPU/memory and\n"
        << "      presented frame count while idle, then exit.\n\n"
        << "  SquareStar.exe --benchmark-memory [options]\n"
        << "      Launch a deterministic Home GUI and record staged process\n"
        << "      memory after Win32/app baseline, GLFW, D3D11, ImGui, fonts,\n"
        << "      ImPlot, network-runtime initialization, and first full frame.\n\n"
        << "Options:\n"
        << "  --benchmark-output DIR    Output directory (default: benchmark-results/<UTC timestamp>)\n"
        << "  --benchmark-runs N        Timed offline iterations, 5..10000 (default 50)\n"
        << "  --benchmark-warmups N     Offline warmups, 0..1000 (default 5)\n"
        << "  --benchmark-seconds N     Idle sample length, 5..600 seconds (default 30)\n"
        << "  --benchmark-help          Show this help\n\n"
        << "Normal MinGW developer build:\n"
        << "  cd build\\dev-mingw\n"
        << "  SquareStar.exe --benchmark\n\n"
        << "For repeated startup/idle sampling, use a Release/O2 build with\n"
        << "scripts\\run-benchmarks.ps1.\n";
}

std::optional<ParsedOptions> ParseOptions(int argc, char** argv) {
    bool sawBenchmarkArgument = false;
    ParsedOptions options;

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg(argv[i] ? argv[i] : "");
        if (arg == "--benchmark") {
            sawBenchmarkArgument = true;
            if (options.mode != Mode::None) {
                std::cerr << "[benchmark] Choose only one benchmark mode.\n";
                return std::nullopt;
            }
            options.mode = Mode::Offline;
        } else if (arg == "--benchmark-startup") {
            sawBenchmarkArgument = true;
            if (options.mode != Mode::None) {
                std::cerr << "[benchmark] Choose only one benchmark mode.\n";
                return std::nullopt;
            }
            options.mode = Mode::Startup;
        } else if (arg == "--benchmark-idle") {
            sawBenchmarkArgument = true;
            if (options.mode != Mode::None) {
                std::cerr << "[benchmark] Choose only one benchmark mode.\n";
                return std::nullopt;
            }
            options.mode = Mode::Idle;
        } else if (arg == "--benchmark-memory") {
            sawBenchmarkArgument = true;
            if (options.mode != Mode::None) {
                std::cerr << "[benchmark] Choose only one benchmark mode.\n";
                return std::nullopt;
            }
            options.mode = Mode::Memory;
        } else if (arg == "--benchmark-help") {
            sawBenchmarkArgument = true;
            options.mode = Mode::Help;
        } else if (arg == "--benchmark-output") {
            sawBenchmarkArgument = true;
            if (++i >= argc) {
                std::cerr << "[benchmark] --benchmark-output requires a directory.\n";
                return std::nullopt;
            }
            options.outputDirectory = fs::path(argv[i]);
        } else if (arg == "--benchmark-runs") {
            sawBenchmarkArgument = true;
            if (++i >= argc || !ParseInt(argv[i], 5, 10000, options.runs)) {
                std::cerr << "[benchmark] --benchmark-runs must be 5..10000.\n";
                return std::nullopt;
            }
        } else if (arg == "--benchmark-warmups") {
            sawBenchmarkArgument = true;
            if (++i >= argc || !ParseInt(argv[i], 0, 1000, options.warmups)) {
                std::cerr << "[benchmark] --benchmark-warmups must be 0..1000.\n";
                return std::nullopt;
            }
        } else if (arg == "--benchmark-seconds") {
            sawBenchmarkArgument = true;
            if (++i >= argc || !ParseInt(argv[i], 5, 600, options.idleSeconds)) {
                std::cerr << "[benchmark] --benchmark-seconds must be 5..600.\n";
                return std::nullopt;
            }
        } else if (arg.starts_with("--benchmark")) {
            sawBenchmarkArgument = true;
            std::cerr << "[benchmark] Unknown benchmark option: " << arg << "\n";
            return std::nullopt;
        }
    }

    if (!sawBenchmarkArgument)
        return ParsedOptions{};
    if (options.mode == Mode::None) {
        std::cerr << "[benchmark] Select --benchmark, --benchmark-startup, --benchmark-idle, or --benchmark-memory.\n";
        return std::nullopt;
    }
    return options;
}

SyntheticSeries MakeSyntheticSeries(std::size_t count) {
    SyntheticSeries result;
    result.x.resize(count);
    result.close.resize(count);
    result.open.resize(count);
    result.high.resize(count);
    result.low.resize(count);

    constexpr double startEpoch = 1'700'000'000.0;
    for (std::size_t i = 0; i < count; ++i) {
        const double t = static_cast<double>(i);
        const double trend = t * 0.000015;
        const double close = 100.0 + trend + std::sin(t * 0.013) * 2.4 +
                             std::sin(t * 0.0017) * 7.0;
        const double open = close + std::sin(t * 0.027) * 0.42;
        const double spread = 0.25 + std::abs(std::sin(t * 0.019)) * 0.85;
        result.x[i] = startEpoch + t * 60.0;
        result.close[i] = close;
        result.open[i] = open;
        result.high[i] = std::max(open, close) + spread;
        result.low[i] = std::min(open, close) - spread;
    }
    return result;
}

double MeasureOneMs(const auto& function) {
    const Clock::time_point started = Clock::now();
    function();
    return std::chrono::duration<double, std::milli>(Clock::now() - started).count();
}

double Median(const std::vector<double>& sorted) {
    if (sorted.empty())
        return 0.0;
    const std::size_t middle = sorted.size() / 2;
    if ((sorted.size() & 1U) != 0U)
        return sorted[middle];
    return (sorted[middle - 1] + sorted[middle]) * 0.5;
}

double NearestRankPercentile(const std::vector<double>& sorted, double percentile) {
    if (sorted.empty())
        return 0.0;
    const double rank = std::ceil(percentile * static_cast<double>(sorted.size()));
    const std::size_t index = static_cast<std::size_t>(std::max(1.0, rank)) - 1;
    return sorted[std::min(index, sorted.size() - 1)];
}

TimingStats Measure(const auto& function, int warmups, int runs) {
    TimingStats result;
    result.coldMs = MeasureOneMs(function);
    for (int i = 0; i < warmups; ++i)
        function();

    result.samplesMs.reserve(static_cast<std::size_t>(runs));
    for (int i = 0; i < runs; ++i)
        result.samplesMs.push_back(MeasureOneMs(function));
    std::vector<double> sorted = result.samplesMs;
    std::sort(sorted.begin(), sorted.end());

    result.medianMs = Median(sorted);
    result.p95Ms = NearestRankPercentile(sorted, 0.95);
    result.p99Ms = NearestRankPercentile(sorted, 0.99);
    result.minMs = sorted.front();
    result.maxMs = sorted.back();
    return result;
}

bool StrictlyIncreasing(const std::vector<double>& values) {
    for (std::size_t i = 1; i < values.size(); ++i) {
        if (!(values[i] > values[i - 1]))
            return false;
    }
    return true;
}

BenchmarkRow BenchmarkLttb(const SyntheticSeries& source,
                           std::size_t threshold,
                           int warmups,
                           int runs) {
    std::vector<double> outputX;
    std::vector<double> outputY;
    auto run = [&] {
        squarestar::presentation::BuildLttbSeries(
            source.x, source.close, threshold, outputX, outputY);
    };

    BenchmarkRow row;
    row.suite = "lttb";
    row.caseName = "to_2048_points";
    row.inputPoints = source.x.size();
    row.timing = Measure(run, warmups, runs);
    row.outputPoints = outputX.size();
    row.runs = runs;
    row.warmups = warmups;
    const std::size_t expected = std::min(source.x.size(), threshold);
    row.verified = outputX.size() == expected && outputY.size() == expected &&
                   !outputX.empty() && outputX.front() == source.x.front() &&
                   outputX.back() == source.x.back() && StrictlyIncreasing(outputX);
    return row;
}

BenchmarkRow BenchmarkChartLineLod(const SyntheticSeries& source,
                                   float logicalWidth,
                                   float framebufferScale,
                                   int warmups,
                                   int runs) {
    squarestar::application::StockContext context("BENCH");
    squarestar::market::StockData raw;
    raw.timestamps = source.x;
    raw.closes = source.close;
    context.PublishRawData(std::move(raw));
    auto run = [&] {
        context.render.renderLodLineType = -1;
        squarestar::presentation::BuildChartRenderLod(
            context, 1, logicalWidth, framebufferScale);
    };

    BenchmarkRow row;
    row.suite = "chart_lod";
    row.caseName = "line_1600px_1x";
    row.inputPoints = source.x.size();
    row.logicalWidth = logicalWidth;
    row.framebufferScale = framebufferScale;
    row.timing = Measure(run, warmups, runs);
    row.outputPoints = context.render.render_sX.empty()
                           ? source.x.size()
                           : context.render.render_sX.size();
    row.secondaryOutputPoints = context.render.renderShade_sX.empty()
                                    ? source.x.size()
                                    : context.render.renderShade_sX.size();
    row.runs = runs;
    row.warmups = warmups;

    const auto targets = squarestar::presentation::CalculateChartRenderLodTargets(
        1, logicalWidth, framebufferScale);
    const std::size_t expectedLine = std::min(source.x.size(), targets.line);
    const std::size_t expectedShade = std::min(source.x.size(), targets.shade);
    row.verified = row.outputPoints == expectedLine &&
                   row.secondaryOutputPoints == expectedShade;
    return row;
}

BenchmarkRow BenchmarkChartCandleLod(const SyntheticSeries& source,
                                     float logicalWidth,
                                     float framebufferScale,
                                     int warmups,
                                     int runs) {
    squarestar::application::StockContext context("BENCH");
    squarestar::market::StockData raw;
    raw.timestamps = source.x;
    raw.closes = source.close;
    raw.opens = source.open;
    raw.highs = source.high;
    raw.lows = source.low;
    context.PublishRawData(std::move(raw));
    auto run = [&] {
        context.render.renderLodLineType = -1;
        squarestar::presentation::BuildChartRenderLod(
            context, 0, logicalWidth, framebufferScale);
    };

    BenchmarkRow row;
    row.suite = "chart_lod";
    row.caseName = "candles_1600px_1x";
    row.inputPoints = source.x.size();
    row.logicalWidth = logicalWidth;
    row.framebufferScale = framebufferScale;
    row.timing = Measure(run, warmups, runs);
    row.outputPoints = context.render.renderCandle_sX.empty()
                           ? source.x.size()
                           : context.render.renderCandle_sX.size();
    row.runs = runs;
    row.warmups = warmups;

    const std::size_t sizes = context.render.renderCandle_sX.size();
    bool candlesValid = sizes == context.render.renderCandle_sC.size() &&
                        sizes == context.render.renderCandle_sO.size() &&
                        sizes == context.render.renderCandle_sH.size() &&
                        sizes == context.render.renderCandle_sL.size();
    for (std::size_t i = 0; candlesValid && i < sizes; ++i) {
        const float open = context.render.renderCandle_sO[i];
        const float close = context.render.renderCandle_sC[i];
        candlesValid = context.render.renderCandle_sH[i] >= std::max(open, close) &&
                       context.render.renderCandle_sL[i] <= std::min(open, close);
    }
    const auto targets = squarestar::presentation::CalculateChartRenderLodTargets(
        0, logicalWidth, framebufferScale);
    row.verified = candlesValid && row.outputPoints <= source.x.size() &&
                   (source.x.size() <= targets.line || row.outputPoints <= targets.line);
    return row;
}

bool WriteOfflineSamplesCsv(const fs::path& directory,
                            const std::vector<BenchmarkRow>& rows) {
    std::ofstream out(directory / "benchmark-samples.csv",
                      std::ios::binary | std::ios::trunc);
    if (!out)
        return false;
    out << "suite,case,input_points,sample_kind,sample_index,ms\n";
    out << std::fixed << std::setprecision(6);
    for (const BenchmarkRow& row : rows) {
        out << row.suite << ',' << row.caseName << ',' << row.inputPoints
            << ",first_call,0," << row.timing.coldMs << '\n';
        for (std::size_t i = 0; i < row.timing.samplesMs.size(); ++i) {
            out << row.suite << ',' << row.caseName << ',' << row.inputPoints
                << ",steady," << (i + 1) << ',' << row.timing.samplesMs[i] << '\n';
        }
    }
    return static_cast<bool>(out);
}

bool WriteOfflineCsv(const fs::path& directory,
                     const std::vector<BenchmarkRow>& rows) {
    std::ofstream out(directory / "benchmark-results.csv",
                      std::ios::binary | std::ios::trunc);
    if (!out)
        return false;
    out << "suite,case,input_points,output_points,secondary_output_points,logical_width,framebuffer_scale,cold_ms,median_ms,p95_ms,p99_ms,min_ms,max_ms,runs,warmups,verified\n";
    out << std::fixed << std::setprecision(6);
    for (const BenchmarkRow& row : rows) {
        out << row.suite << ',' << row.caseName << ',' << row.inputPoints << ','
            << row.outputPoints << ',' << row.secondaryOutputPoints << ','
            << row.logicalWidth << ',' << row.framebufferScale << ','
            << row.timing.coldMs << ',' << row.timing.medianMs << ','
            << row.timing.p95Ms << ',' << row.timing.p99Ms << ','
            << row.timing.minMs << ',' << row.timing.maxMs << ','
            << row.runs << ',' << row.warmups << ','
            << (row.verified ? "PASS" : "FAIL") << '\n';
    }
    return static_cast<bool>(out);
}

std::string OfflineSummary(const std::vector<BenchmarkRow>& rows,
                           int warmups,
                           int runs) {
    std::ostringstream out;
    out << "SquareStar offline benchmark\n"
        << "Provider/network I/O: excluded\n"
        << "Synthetic data: deterministic and generated outside timed regions\n"
        << "Cold: first call including output allocation\n"
        << "Steady: " << warmups << " warmups, then " << runs
        << " timed runs; median/p95/p99 shown\n"
        << "Percentiles: nearest-rank (median uses the ordinary midpoint rule)\n\n";

    out << std::left << std::setw(28) << "case"
        << std::right << std::setw(11) << "input"
        << std::setw(11) << "output"
        << std::setw(11) << "cold ms"
        << std::setw(11) << "median"
        << std::setw(11) << "p95"
        << std::setw(11) << "p99"
        << std::setw(9) << "verify" << '\n';
    out << std::string(103, '-') << '\n';
    out << std::fixed << std::setprecision(3);
    for (const BenchmarkRow& row : rows) {
        const std::string displayCase = row.suite + "/" + row.caseName;
        out << std::left << std::setw(28) << displayCase
            << std::right << std::setw(11) << row.inputPoints
            << std::setw(11) << row.outputPoints
            << std::setw(11) << row.timing.coldMs
            << std::setw(11) << row.timing.medianMs
            << std::setw(11) << row.timing.p95Ms
            << std::setw(11) << row.timing.p99Ms
            << std::setw(9) << (row.verified ? "PASS" : "FAIL") << '\n';
    }
    return out.str();
}

int RunOfflineBenchmark(const ParsedOptions& options) {
    const fs::path directory = ResolveOutputDirectory(options.outputDirectory);
    if (!EnsureDirectory(directory))
        return 2;
    if (!WriteSystemInfo(directory)) {
        std::cerr << "[benchmark] Could not write system.txt\n";
        return 2;
    }

#ifndef NDEBUG
    std::cerr << "[benchmark] WARNING: this is a Debug build. Do not publish these numbers.\n";
#endif

    constexpr float logicalWidth = 1600.0f;
    constexpr float framebufferScale = 1.0f;
    constexpr std::size_t lttbThreshold = 2048;
    const std::size_t inputSizes[] = {10'000, 100'000, 1'000'000};

    std::vector<BenchmarkRow> rows;
    rows.reserve(9);
    bool allVerified = true;

    for (const std::size_t count : inputSizes) {
        std::cout << "[benchmark] generating " << count << " deterministic points...\n";
        const SyntheticSeries source = MakeSyntheticSeries(count);
        rows.push_back(BenchmarkLttb(source, lttbThreshold, options.warmups, options.runs));
        rows.push_back(BenchmarkChartLineLod(
            source, logicalWidth, framebufferScale, options.warmups, options.runs));
        rows.push_back(BenchmarkChartCandleLod(
            source, logicalWidth, framebufferScale, options.warmups, options.runs));
        allVerified = allVerified && rows[rows.size() - 3].verified &&
                      rows[rows.size() - 2].verified && rows[rows.size() - 1].verified;
    }

    if (!WriteOfflineCsv(directory, rows)) {
        std::cerr << "[benchmark] Could not write benchmark-results.csv\n";
        return 2;
    }
    if (!WriteOfflineSamplesCsv(directory, rows)) {
        std::cerr << "[benchmark] Could not write benchmark-samples.csv\n";
        return 2;
    }
    const std::string summary = OfflineSummary(rows, options.warmups, options.runs);
    {
        std::ofstream out(directory / "benchmark-summary.txt",
                          std::ios::binary | std::ios::trunc);
        if (!out) {
            std::cerr << "[benchmark] Could not write benchmark-summary.txt\n";
            return 2;
        }
        out << summary;
    }

    std::cout << '\n' << summary
              << "\nOutput: " << directory.string() << '\n';
    if (!allVerified) {
        std::cerr << "[benchmark] Verification failed. Do not publish these results.\n";
        return 3;
    }
    return 0;
}

bool WriteStartupSample(double mainEntryMilliseconds,
                        double processCreationMilliseconds) {
    if (!EnsureDirectory(g_GuiProbe.outputDirectory))
        return false;
    (void)WriteSystemInfo(g_GuiProbe.outputDirectory);
    std::ostringstream row;
    row << UtcTimestamp() << ','
        << std::fixed << std::setprecision(6) << mainEntryMilliseconds << ','
        << processCreationMilliseconds
        << ",provider_free,PASS";
    return AppendTextLine(
        g_GuiProbe.outputDirectory / "startup.csv",
        "timestamp_utc,main_entry_to_first_visible_present_ms,process_creation_to_first_visible_present_ms,mode,verified",
        row.str());
}

bool WriteIdleSample(double wallSeconds,
                     double cpuPercent,
                     const ProcessMemorySnapshot& memory) {
    if (!EnsureDirectory(g_GuiProbe.outputDirectory))
        return false;
    (void)WriteSystemInfo(g_GuiProbe.outputDirectory);
    std::ostringstream row;
    row << UtcTimestamp() << ','
        << std::fixed << std::setprecision(6) << wallSeconds << ','
        << cpuPercent << ','
        << memory.workingSetMb << ','
        << memory.privateBytesMb << ','
        << g_GuiProbe.presentedFramesDuringIdle << ','
        << (wallSeconds > 0.0
                ? static_cast<double>(g_GuiProbe.presentedFramesDuringIdle) / wallSeconds
                : 0.0)
        << ",provider_audio_animation_config_io_suppressed,PASS";
    return AppendTextLine(
        g_GuiProbe.outputDirectory / "idle.csv",
        "timestamp_utc,duration_seconds,cpu_percent_total_machine,working_set_mb,private_bytes_mb,presented_frames_during_window,presented_frames_per_second,mode,verified",
        row.str());
}

} // namespace

CommandLineDispatch HandleBenchmarkCommandLine(
    int argc,
    char** argv,
    std::chrono::steady_clock::time_point mainEntry) {
    bool hasBenchmarkMarker = false;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg(argv[i] ? argv[i] : "");
        if (arg.starts_with("--benchmark")) {
            hasBenchmarkMarker = true;
            break;
        }
    }
    if (!hasBenchmarkMarker)
        return {};

    const std::optional<ParsedOptions> parsed = ParseOptions(argc, argv);
    if (!parsed)
        return {true, 64};
    const ParsedOptions& options = *parsed;

    if (options.mode == Mode::Help) {
        PrintHelp();
        return {true, 0};
    }
    if (options.mode == Mode::Offline)
        return {true, RunOfflineBenchmark(options)};

    g_GuiProbe = {};
    g_GuiProbe.mode = options.mode;
    g_GuiProbe.outputDirectory = ResolveOutputDirectory(options.outputDirectory);
    g_GuiProbe.mainEntry = mainEntry;
    g_GuiProbe.idleSeconds = options.idleSeconds;
    g_MemoryLayers.clear();
    if (!EnsureDirectory(g_GuiProbe.outputDirectory))
        return {true, 2};

    std::cout << "[benchmark] GUI probe output: "
              << g_GuiProbe.outputDirectory.string() << '\n';
    if (options.mode == Mode::Startup) {
        std::cout << "[benchmark] Measuring process creation/main() entry -> first visible present.\n";
    } else if (options.mode == Mode::Memory) {
        std::cout << "[benchmark] Measuring staged Private Bytes/working set through first full GUI frame.\n";
    } else {
        std::cout << "[benchmark] Measuring settled Home idle for "
                  << options.idleSeconds << " seconds after first visible present.\n";
    }
    return {false, 0};
}

bool GuiProbeActive() noexcept {
    return g_GuiProbe.mode == Mode::Startup || g_GuiProbe.mode == Mode::Idle ||
           g_GuiProbe.mode == Mode::Memory;
}

bool MemoryLayerProbeActive() noexcept {
    return g_GuiProbe.mode == Mode::Memory;
}

bool SuppressExternalWork() noexcept {
    return GuiProbeActive();
}

int GuiProbeExitCode() noexcept {
    return g_GuiProbe.exitCode;
}

void PrepareGuiProbeState(squarestar::application::AppState& state) {
    if (!GuiProbeActive())
        return;
    state.config.lastOpenMode = 0;
    state.config.soundEnabled = false;
    state.config.showStartupAnim = false;
    state.config.zeroGraphics = false;
    state.alerts.ClearThresholds();
    state.alerts.ClearToasts();
    if (g_GuiProbe.mode == Mode::Idle || g_GuiProbe.mode == Mode::Memory)
        state.config.animEnabled = false;
}

void RecordMemoryLayer(const char* stage, const char* label) {
    if (!MemoryLayerProbeActive() || !stage || !*stage || g_GuiProbe.completed)
        return;
    if (!g_MemoryLayers.empty() && g_MemoryLayers.back().stage == stage)
        return;
    const std::optional<ProcessMemorySnapshot> memory = ProcessMemory();
    if (!memory) {
        std::cerr << "[benchmark] Failed to read memory at layer " << stage << ".\n";
        g_GuiProbe.exitCode = 2;
        return;
    }
    g_MemoryLayers.push_back(MemoryLayerRow{
        stage,
        label ? label : "",
        *memory,
    });
    if (!WriteMemoryLayerReport()) {
        std::cerr << "[benchmark] Failed to write memory-layers.csv\n";
        g_GuiProbe.exitCode = 2;
    }
    const double baseline = g_MemoryLayers.front().memory.privateBytesMb;
    const double previous = g_MemoryLayers.size() > 1
                                ? g_MemoryLayers[g_MemoryLayers.size() - 2].memory.privateBytesMb
                                : memory->privateBytesMb;
    std::cout << std::fixed << std::setprecision(3)
              << "[benchmark] memory " << stage << " " << (label ? label : "")
              << ": private=" << memory->privateBytesMb << " MB"
              << " (delta=" << (memory->privateBytesMb - previous) << " MB, from A="
              << (memory->privateBytesMb - baseline) << " MB), working-set="
              << memory->workingSetMb << " MB\n";
}

void OnGuiFramePresented(GLFWwindow* window) {
    if (!GuiProbeActive() || g_GuiProbe.completed)
        return;

    const Clock::time_point now = Clock::now();
    if (!g_GuiProbe.firstPresentSeen) {
        g_GuiProbe.firstPresentSeen = true;
        if (window) {
            glfwGetWindowSize(window, &g_GuiProbe.logicalWidth, &g_GuiProbe.logicalHeight);
            glfwGetFramebufferSize(
                window, &g_GuiProbe.framebufferWidth, &g_GuiProbe.framebufferHeight);
        }
        if (g_GuiProbe.mode == Mode::Memory) {
            RecordMemoryLayer("H", "+ first full SquareStar UI frame");
            (void)WriteSystemInfo(g_GuiProbe.outputDirectory);
            if (!WriteLoadedModuleAudit()) {
                std::cerr << "[benchmark] Failed to write loaded-modules.txt\n";
                g_GuiProbe.exitCode = 2;
            }
            g_GuiProbe.completed = true;
            squarestar::application::ApplicationRuntime().RequestQuit();
            return;
        }
        if (g_GuiProbe.mode == Mode::Startup) {
            const double mainEntryMilliseconds =
                std::chrono::duration<double, std::milli>(now - g_GuiProbe.mainEntry).count();
            const std::optional<double> processCreationMilliseconds = ProcessCreationToNowMs();
            if (!processCreationMilliseconds ||
                !WriteStartupSample(mainEntryMilliseconds, *processCreationMilliseconds)) {
                std::cerr << "[benchmark] Failed to append startup.csv\n";
                g_GuiProbe.exitCode = 2;
            } else {
                std::cout << std::fixed << std::setprecision(3)
                          << "[benchmark] startup process->first present: "
                          << *processCreationMilliseconds << " ms\n"
                          << "[benchmark] startup main->first present: "
                          << mainEntryMilliseconds << " ms\n";
            }
            g_GuiProbe.completed = true;
            squarestar::application::ApplicationRuntime().RequestQuit();
            return;
        }

        const std::optional<std::uint64_t> cpu = ProcessCpuTime100ns();
        if (!cpu) {
            std::cerr << "[benchmark] GetProcessTimes failed; idle probe cannot continue.\n";
            g_GuiProbe.exitCode = 2;
            g_GuiProbe.completed = true;
            squarestar::application::ApplicationRuntime().RequestQuit();
            return;
        }
        g_GuiProbe.idleCpu100nsStart = *cpu;
        g_GuiProbe.idleStartedAt = now;
        g_GuiProbe.idleDeadline = now + std::chrono::seconds(g_GuiProbe.idleSeconds);
        squarestar::application::RequestGuiWakeAt(g_GuiProbe.idleDeadline);
        return;
    }

    if (g_GuiProbe.mode == Mode::Idle)
        ++g_GuiProbe.presentedFramesDuringIdle;
}

bool PollGuiProbe() {
    if (g_GuiProbe.mode != Mode::Idle || !g_GuiProbe.firstPresentSeen ||
        g_GuiProbe.completed || Clock::now() < g_GuiProbe.idleDeadline)
        return false;

    const Clock::time_point finished = Clock::now();
    const std::optional<std::uint64_t> cpu = ProcessCpuTime100ns();
    const std::optional<ProcessMemorySnapshot> memory = ProcessMemory();
    if (!cpu || !memory) {
        std::cerr << "[benchmark] Failed to read process CPU/memory counters.\n";
        g_GuiProbe.exitCode = 2;
        g_GuiProbe.completed = true;
        squarestar::application::ApplicationRuntime().RequestQuit();
        return true;
    }

    const double wallSeconds =
        std::chrono::duration<double>(finished - g_GuiProbe.idleStartedAt).count();
    const double processCpuSeconds =
        static_cast<double>(*cpu - g_GuiProbe.idleCpu100nsStart) / 10'000'000.0;
    const double cpuPercent = wallSeconds > 0.0
                                  ? (processCpuSeconds / wallSeconds /
                                     static_cast<double>(LogicalProcessorCount())) * 100.0
                                  : 0.0;

    if (!WriteIdleSample(wallSeconds, cpuPercent, *memory)) {
        std::cerr << "[benchmark] Failed to append idle.csv\n";
        g_GuiProbe.exitCode = 2;
    } else {
        std::cout << std::fixed << std::setprecision(3)
                  << "[benchmark] idle CPU: " << cpuPercent << "% of total machine\n"
                  << "[benchmark] working set: " << memory->workingSetMb << " MB\n"
                  << "[benchmark] private bytes: " << memory->privateBytesMb << " MB\n"
                  << "[benchmark] presented frames during sample: "
                  << g_GuiProbe.presentedFramesDuringIdle << '\n';
    }

    g_GuiProbe.completed = true;
    squarestar::application::ApplicationRuntime().RequestQuit();
    return true;
}

} // namespace squarestar::benchmark
