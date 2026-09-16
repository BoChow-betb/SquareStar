#pragma once
#include "platform/windows_headers.hpp"

#ifdef _WIN32
#include <shellapi.h>
#endif

#include <algorithm>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>


namespace squarestar::platform {

#ifdef _WIN32
inline std::wstring Utf8PathToWide(std::string_view text) {
    if (text.empty())
        return {};
    const int count = MultiByteToWideChar(CP_UTF8,
                                          MB_ERR_INVALID_CHARS,
                                          text.data(),
                                          static_cast<int>(text.size()),
                                          nullptr,
                                          0);
    if (count <= 0)
        return {};
    std::wstring result(static_cast<std::size_t>(count), L'\0');
    if (MultiByteToWideChar(CP_UTF8,
                            MB_ERR_INVALID_CHARS,
                            text.data(),
                            static_cast<int>(text.size()),
                            result.data(),
                            count) != count) {
        return {};
    }
    return result;
}

inline std::string WidePathToUtf8(std::wstring_view text) {
    if (text.empty())
        return {};
    const int count = WideCharToMultiByte(CP_UTF8,
                                          WC_ERR_INVALID_CHARS,
                                          text.data(),
                                          static_cast<int>(text.size()),
                                          nullptr,
                                          0,
                                          nullptr,
                                          nullptr);
    if (count <= 0)
        return {};
    std::string result(static_cast<std::size_t>(count), '\0');
    if (WideCharToMultiByte(CP_UTF8,
                            WC_ERR_INVALID_CHARS,
                            text.data(),
                            static_cast<int>(text.size()),
                            result.data(),
                            count,
                            nullptr,
                            nullptr) != count) {
        return {};
    }
    return result;
}

inline std::wstring ExecutablePathWide() {
    std::vector<wchar_t> buffer(512, L'\0');
    for (;;) {
        const DWORD length =
            GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0)
            return {};
        if (length < buffer.size())
            return std::wstring(buffer.data(), length);
        if (buffer.size() >= 32768)
            return {};
        buffer.resize(std::min<std::size_t>(32768, buffer.size() * 2), L'\0');
    }
}

inline std::wstring EnvironmentPathWide(const wchar_t* name) {
    if (!name || !*name)
        return {};
    const DWORD required = GetEnvironmentVariableW(name, nullptr, 0);
    if (required == 0)
        return {};
    std::vector<wchar_t> buffer(required, L'\0');
    const DWORD length = GetEnvironmentVariableW(name, buffer.data(), required);
    if (length == 0 || length >= required)
        return {};
    return std::wstring(buffer.data(), length);
}
#endif

inline std::filesystem::path Utf8FilesystemPath(std::string_view text) {
#ifdef _WIN32
    return std::filesystem::path(Utf8PathToWide(text));
#else
    return std::filesystem::path(std::string(text));
#endif
}

inline bool RevealFileInFolder(std::string_view text) {
#ifdef _WIN32
    if (text.empty())
        return false;

    const std::wstring wide = Utf8PathToWide(text);
    if (wide.empty())
        return false;

    std::filesystem::path path(wide);
    std::error_code error;
    const std::filesystem::path absolute = std::filesystem::absolute(path, error);
    if (!error)
        path = absolute;

    error.clear();
    if (std::filesystem::is_directory(path, error)) {
        const HINSTANCE result =
            ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return reinterpret_cast<INT_PTR>(result) > 32;
    }

    const std::filesystem::path parent = path.parent_path();
    error.clear();
    if (parent.empty() || !std::filesystem::exists(parent, error))
        return false;

    const std::wstring parameters = L"/select,\"" + path.wstring() + L"\"";
    const HINSTANCE result =
        ShellExecuteW(nullptr,
                      L"open",
                      L"explorer.exe",
                      parameters.c_str(),
                      nullptr,
                      SW_SHOWNORMAL);
    return reinterpret_cast<INT_PTR>(result) > 32;
#else
    (void)text;
    return false;
#endif
}

}
