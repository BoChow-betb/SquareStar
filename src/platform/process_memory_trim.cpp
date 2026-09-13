#include "platform/process_memory_trim.hpp"

#ifdef _WIN32
#include "platform/windows_headers.hpp"
#endif

namespace squarestar::platform {

bool TrimProcessPrivateMemory() noexcept {
#ifdef _WIN32
    // HeapOptimizeResources (information class 3) asks Windows to decommit
    // unused LFH backing pages. Resolve dynamically so this remains compatible
    // with MinGW SDKs that do not expose the newer enum symbol in headers.
    struct HeapOptimizeResourcesInformation {
        DWORD Version;
        DWORD Flags;
    };
    using HeapSetInformationFn = BOOL(WINAPI*)(HANDLE, int, PVOID, SIZE_T);

    const HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
    if (!kernel)
        return false;
    const FARPROC raw = GetProcAddress(kernel, "HeapSetInformation");
    if (!raw)
        return false;

    using GenericFunctionPointer = void (*)();
    const auto generic = reinterpret_cast<GenericFunctionPointer>(raw);
    const auto heapSetInformation =
        reinterpret_cast<HeapSetInformationFn>(generic);

    HeapOptimizeResourcesInformation info{1, 0};
    constexpr int kHeapOptimizeResources = 3;
    return heapSetInformation(nullptr,
                              kHeapOptimizeResources,
                              &info,
                              sizeof(info)) != FALSE;
#else
    return false;
#endif
}

} // namespace squarestar::platform
