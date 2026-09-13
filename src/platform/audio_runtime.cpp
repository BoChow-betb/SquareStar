#include "platform/audio_runtime.hpp"
#include "platform/embedded_resource.hpp"
#include "platform/windows_path.hpp"

#include "domain/audio_policy.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <mmsystem.h>
#endif

namespace squarestar::platform {
namespace {

struct AppSoundClip {
    // Embedded RCDATA is process-lifetime, file-backed memory. Keep only a view
    // for the normal path instead of duplicating every cue into the CRT heap.
    const unsigned char* bytes = nullptr;
    std::size_t byteCount = 0;
    std::vector<unsigned char> ownedBytes;
    double durationSeconds = 0.85;
#ifdef _WIN32
    WAVEFORMATEX format{};
    std::size_t dataOffset = 0;
    DWORD dataSize = 0;
    bool waveOutReady = false;
#endif
};

std::mutex g_AppSoundCacheMutex;
std::unordered_map<std::string, std::shared_ptr<const AppSoundClip>> g_AppSoundCache;

#ifdef _WIN32
std::mutex g_AudioPlaybackMutex;
int g_ActiveSoundPriority = 0;
std::chrono::steady_clock::time_point g_ActiveSoundPriorityUntil{};
std::shared_ptr<const AppSoundClip> g_ActiveSoundClip;

struct WaveOutDevice {
    WAVEFORMATEX format{};
    HWAVEOUT handle = nullptr;
};

std::vector<WaveOutDevice> g_WaveOutDevices;
HWAVEOUT g_ActiveWaveOutDevice = nullptr;
WAVEHDR g_ActiveWaveHeader{};
bool g_ActiveWaveHeaderPrepared = false;

std::uint16_t ReadLe16(const unsigned char* bytes) noexcept {
    return static_cast<std::uint16_t>(bytes[0]) |
           (static_cast<std::uint16_t>(bytes[1]) << 8u);
}

std::uint32_t ReadLe32(const unsigned char* bytes) noexcept {
    return static_cast<std::uint32_t>(bytes[0]) |
           (static_cast<std::uint32_t>(bytes[1]) << 8u) |
           (static_cast<std::uint32_t>(bytes[2]) << 16u) |
           (static_cast<std::uint32_t>(bytes[3]) << 24u);
}

bool ParsePcmWave(AppSoundClip& clip,
                  const unsigned char* bytes,
                  std::size_t size) noexcept {
    if (!bytes || size < 44 || size > 8 * 1024 * 1024)
        return false;
    if (std::memcmp(bytes, "RIFF", 4) != 0 ||
        std::memcmp(bytes + 8, "WAVE", 4) != 0) {
        return false;
    }

    bool foundFormat = false;
    bool foundData = false;
    WAVEFORMATEX format{};
    std::size_t dataOffset = 0;
    std::size_t dataSize = 0;

    for (std::size_t offset = 12; offset + 8 <= size;) {
        const unsigned char* chunk = bytes + offset;
        const std::uint32_t declaredSize = ReadLe32(chunk + 4);
        const std::size_t payloadOffset = offset + 8;
        if (declaredSize > size - payloadOffset)
            return false;

        if (std::memcmp(chunk, "fmt ", 4) == 0) {
            if (declaredSize < 16)
                return false;
            const unsigned char* fmt = bytes + payloadOffset;
            format.wFormatTag = ReadLe16(fmt);
            format.nChannels = ReadLe16(fmt + 2);
            format.nSamplesPerSec = ReadLe32(fmt + 4);
            format.nAvgBytesPerSec = ReadLe32(fmt + 8);
            format.nBlockAlign = ReadLe16(fmt + 12);
            format.wBitsPerSample = ReadLe16(fmt + 14);
            format.cbSize = 0;
            foundFormat = true;
        } else if (std::memcmp(chunk, "data", 4) == 0) {
            dataOffset = payloadOffset;
            dataSize = declaredSize;
            foundData = true;
        }

        const std::size_t paddedSize =
            static_cast<std::size_t>(declaredSize) + (declaredSize & 1u);
        if (paddedSize > size - payloadOffset)
            break;
        offset = payloadOffset + paddedSize;
    }

    if (!foundFormat || !foundData || format.wFormatTag != WAVE_FORMAT_PCM ||
        format.nChannels < 1 || format.nChannels > 2 ||
        format.nSamplesPerSec < 8000 || format.nSamplesPerSec > 192000 ||
        (format.wBitsPerSample != 8 && format.wBitsPerSample != 16) ||
        format.nBlockAlign == 0 || format.nAvgBytesPerSec == 0 ||
        dataSize == 0 || dataSize > size - dataOffset ||
        dataSize % format.nBlockAlign != 0 ||
        dataSize > static_cast<std::size_t>(0xFFFFFFFFu)) {
        return false;
    }

    clip.bytes = bytes;
    clip.byteCount = size;
    clip.format = format;
    clip.dataOffset = dataOffset;
    clip.dataSize = static_cast<DWORD>(dataSize);
    clip.durationSeconds = std::clamp(
        static_cast<double>(dataSize) /
            static_cast<double>(format.nAvgBytesPerSec),
        0.10,
        5.0);
    clip.waveOutReady = true;
    return true;
}

bool SameWaveFormat(const WAVEFORMATEX& left,
                    const WAVEFORMATEX& right) noexcept {
    return left.wFormatTag == right.wFormatTag &&
           left.nChannels == right.nChannels &&
           left.nSamplesPerSec == right.nSamplesPerSec &&
           left.nAvgBytesPerSec == right.nAvgBytesPerSec &&
           left.nBlockAlign == right.nBlockAlign &&
           left.wBitsPerSample == right.wBitsPerSample;
}

HWAVEOUT FindOrOpenWaveOutDeviceLocked(const WAVEFORMATEX& format) {
    const auto found = std::find_if(
        g_WaveOutDevices.begin(),
        g_WaveOutDevices.end(),
        [&](const WaveOutDevice& device) {
            return SameWaveFormat(device.format, format);
        });
    if (found != g_WaveOutDevices.end())
        return found->handle;

    HWAVEOUT handle = nullptr;
    WAVEFORMATEX requested = format;
    if (waveOutOpen(&handle,
                    WAVE_MAPPER,
                    &requested,
                    0,
                    0,
                    CALLBACK_NULL) != MMSYSERR_NOERROR) {
        return nullptr;
    }
    g_WaveOutDevices.push_back(WaveOutDevice{requested, handle});
    return handle;
}

void StopActiveWaveOutLocked() {
    if (!g_ActiveWaveOutDevice)
        return;
    waveOutReset(g_ActiveWaveOutDevice);
    if (g_ActiveWaveHeaderPrepared) {
        waveOutUnprepareHeader(g_ActiveWaveOutDevice,
                               &g_ActiveWaveHeader,
                               sizeof(g_ActiveWaveHeader));
    }
    g_ActiveWaveHeader = {};
    g_ActiveWaveHeaderPrepared = false;
    g_ActiveWaveOutDevice = nullptr;
}

bool StartWaveOutPlaybackLocked(const std::shared_ptr<const AppSoundClip>& clip) {
    if (!clip || !clip->waveOutReady || !clip->bytes || clip->dataSize == 0 ||
        clip->dataOffset > clip->byteCount ||
        clip->dataSize > clip->byteCount - clip->dataOffset) {
        return false;
    }

    StopActiveWaveOutLocked();
    HWAVEOUT device = FindOrOpenWaveOutDeviceLocked(clip->format);
    if (!device)
        return false;

    g_ActiveWaveHeader.lpData = reinterpret_cast<LPSTR>(
        const_cast<unsigned char*>(clip->bytes + clip->dataOffset));
    g_ActiveWaveHeader.dwBufferLength = clip->dataSize;
    if (waveOutPrepareHeader(device,
                             &g_ActiveWaveHeader,
                             sizeof(g_ActiveWaveHeader)) != MMSYSERR_NOERROR) {
        g_ActiveWaveHeader = {};
        return false;
    }
    g_ActiveWaveHeaderPrepared = true;
    g_ActiveWaveOutDevice = device;
    if (waveOutWrite(device,
                     &g_ActiveWaveHeader,
                     sizeof(g_ActiveWaveHeader)) == MMSYSERR_NOERROR) {
        return true;
    }
    StopActiveWaveOutLocked();
    return false;
}

std::string ExecutableDirectory() {
    const std::wstring executable = ExecutablePathWide();
    if (!executable.empty()) {
        return WidePathToUtf8(
            std::filesystem::path(executable).parent_path().native());
    }
    return ".";
}
#endif

std::string ResolveAppSoundPath(const char* filename) {
    if (!filename || !*filename)
        return {};
#ifdef _WIN32
    const std::filesystem::path executablePath =
        Utf8FilesystemPath(ExecutableDirectory()) / Utf8FilesystemPath(filename);
    std::error_code error;
    if (std::filesystem::is_regular_file(executablePath, error))
        return WidePathToUtf8(executablePath.native());
    const std::filesystem::path absolutePath =
        std::filesystem::absolute(Utf8FilesystemPath(filename), error);
    if (!error)
        return WidePathToUtf8(absolutePath.native());
#endif
    return filename;
}

std::shared_ptr<const AppSoundClip> CachedAppSound(const char* filename) {
    if (!filename || !*filename)
        return {};

    const EmbeddedResourceView resource = FindEmbeddedResource(filename);
    const std::string path = resource ? std::string{} : ResolveAppSoundPath(filename);
    const std::string cacheKey =
        resource ? std::string("embedded:") + filename : path;
    if (cacheKey.empty())
        return {};

    {
        std::lock_guard<std::mutex> lock(g_AppSoundCacheMutex);
        if (const auto found = g_AppSoundCache.find(cacheKey);
            found != g_AppSoundCache.end()) {
            return found->second;
        }
    }

#ifdef _WIN32
    auto clip = std::make_shared<AppSoundClip>();
    if (resource) {
        if (!ParsePcmWave(*clip, resource.data, resource.size)) {
            std::lock_guard<std::mutex> lock(g_AppSoundCacheMutex);
            g_AppSoundCache.emplace(cacheKey, nullptr);
            return {};
        }
    } else {
        std::ifstream file(Utf8FilesystemPath(path),
                           std::ios::binary | std::ios::ate);
        const std::streamoff fileSize =
            file ? static_cast<std::streamoff>(file.tellg()) : std::streamoff{-1};
        if (fileSize <= 0 || fileSize > 8 * 1024 * 1024) {
            std::lock_guard<std::mutex> lock(g_AppSoundCacheMutex);
            g_AppSoundCache.emplace(cacheKey, nullptr);
            return {};
        }
        clip->ownedBytes.resize(static_cast<std::size_t>(fileSize));
        file.seekg(0, std::ios::beg);
        if (!file.read(reinterpret_cast<char*>(clip->ownedBytes.data()), static_cast<std::streamsize>(fileSize)) ||
            !ParsePcmWave(*clip,
                          clip->ownedBytes.data(),
                          clip->ownedBytes.size())) {
            std::lock_guard<std::mutex> lock(g_AppSoundCacheMutex);
            g_AppSoundCache.emplace(cacheKey, nullptr);
            return {};
        }
        // ParsePcmWave stores the vector's stable backing pointer. The vector is
        // never mutated after this point while the clip is cached/in playback.
    }

    std::lock_guard<std::mutex> lock(g_AppSoundCacheMutex);
    if (const auto found = g_AppSoundCache.find(cacheKey);
        found != g_AppSoundCache.end()) {
        return found->second;
    }
    g_AppSoundCache.emplace(cacheKey, clip);
    return clip;
#else
    std::lock_guard<std::mutex> lock(g_AppSoundCacheMutex);
    g_AppSoundCache.emplace(cacheKey, nullptr);
    return {};
#endif
}

} // namespace

double AppSoundDurationSeconds(const char* filename) {
    const std::shared_ptr<const AppSoundClip> clip = CachedAppSound(filename);
    return clip ? clip->durationSeconds : 0.85;
}

void StopAllAppAudio() {
#ifdef _WIN32
    std::lock_guard<std::mutex> lock(g_AudioPlaybackMutex);
    StopActiveWaveOutLocked();
    g_ActiveSoundClip.reset();
    g_ActiveSoundPriority = 0;
    g_ActiveSoundPriorityUntil = {};
#endif
}

void TrimIdleAppAudioMemory() {
#ifdef _WIN32
    std::lock_guard<std::mutex> lock(g_AudioPlaybackMutex);
    // Once the short PCM cue plus a small safety margin has elapsed, close the
    // waveOut device instead of retaining the Windows audio/driver allocation
    // for the lifetime of SquareStar. The next cue lazily reopens it.
    if (g_ActiveSoundPriorityUntil != std::chrono::steady_clock::time_point{} &&
        std::chrono::steady_clock::now() <
            g_ActiveSoundPriorityUntil + std::chrono::milliseconds(250)) {
        return;
    }
    StopActiveWaveOutLocked();
    g_ActiveSoundClip.reset();
    g_ActiveSoundPriority = 0;
    g_ActiveSoundPriorityUntil = {};
    for (WaveOutDevice& device : g_WaveOutDevices) {
        if (device.handle)
            waveOutClose(device.handle);
    }
    std::vector<WaveOutDevice>().swap(g_WaveOutDevices);
#endif
}

void ShutdownAppAudio() {
    StopAllAppAudio();
#ifdef _WIN32
    {
        std::lock_guard<std::mutex> lock(g_AudioPlaybackMutex);
        for (WaveOutDevice& device : g_WaveOutDevices) {
            if (device.handle)
                waveOutClose(device.handle);
        }
        std::vector<WaveOutDevice>().swap(g_WaveOutDevices);
    }
#endif
    std::lock_guard<std::mutex> lock(g_AppSoundCacheMutex);
    decltype(g_AppSoundCache){}.swap(g_AppSoundCache);
}

bool PlayAppSoundRuntime(const char* filename) {
    if (!filename || !*filename)
        return false;
#ifdef _WIN32
    const std::shared_ptr<const AppSoundClip> clip = CachedAppSound(filename);
    if (!clip)
        return false;

    const squarestar::audio::AppSoundPolicy policy =
        squarestar::audio::AppSoundPolicyFor(filename);
    const int priority = policy.priority;
    const auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(g_AudioPlaybackMutex);
    if (now < g_ActiveSoundPriorityUntil && priority < g_ActiveSoundPriority)
        return false;
    if (!StartWaveOutPlaybackLocked(clip))
        return false;

    g_ActiveSoundClip = clip;
    g_ActiveSoundPriority = priority;
    const auto fileDuration = std::chrono::milliseconds(
        static_cast<int>(std::ceil(clip->durationSeconds * 1000.0)));
    g_ActiveSoundPriorityUntil = now + std::max(policy.guard, fileDuration);
    return true;
#else
    std::printf("\a");
    std::fflush(stdout);
    return true;
#endif
}

} // namespace squarestar::platform
