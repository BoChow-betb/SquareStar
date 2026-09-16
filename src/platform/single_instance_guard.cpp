#include "platform/single_instance_guard.hpp"

#ifdef _WIN32
#include <sddl.h>

#include <cstddef>
#include <utility>
#include <vector>
#endif

namespace squarestar::platform {

#ifdef _WIN32
namespace {

std::wstring CurrentUserSidText() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return {};

    DWORD required = 0;
    (void)GetTokenInformation(token, TokenUser, nullptr, 0, &required);
    if (required == 0) {
        CloseHandle(token);
        return {};
    }

    std::vector<std::byte> storage(required);
    if (!GetTokenInformation(token,
                             TokenUser,
                             storage.data(),
                             required,
                             &required)) {
        CloseHandle(token);
        return {};
    }
    CloseHandle(token);

    const auto* tokenUser = reinterpret_cast<const TOKEN_USER*>(storage.data());
    LPWSTR sidText = nullptr;
    if (!ConvertSidToStringSidW(tokenUser->User.Sid, &sidText) || !sidText)
        return {};
    std::wstring result(sidText);
    LocalFree(sidText);
    return result;
}

}

std::wstring CurrentUserSquareStarInstanceMutexName() {
    const std::wstring sid = CurrentUserSidText();
    if (sid.empty())
        return {};


    return L"Global\\SquareStar-StateWriter-" + sid;
}
#endif

#ifdef _WIN32
SingleInstanceGuard::SingleInstanceGuard(std::wstring instanceNameOverride)
    : instanceNameOverride_(std::move(instanceNameOverride)) {}
#endif

SingleInstanceGuard::~SingleInstanceGuard() {
    Release();
}

bool SingleInstanceGuard::Acquire() {
#ifdef _WIN32
    if (handle_)
        return true;
    const std::wstring name = instanceNameOverride_.empty()
                                  ? CurrentUserSquareStarInstanceMutexName()
                                  : instanceNameOverride_;
    if (name.empty())
        return false;

    HANDLE handle = CreateMutexW(nullptr, FALSE, name.c_str());
    if (!handle) {


if (GetLastError() == ERROR_ACCESS_DENIED) {
            HANDLE existing = OpenMutexW(SYNCHRONIZE, FALSE, name.c_str());
            if (existing)
                CloseHandle(existing);
        }
        return false;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(handle);
        return false;
    }
    handle_ = handle;
    return true;
#else
    if (acquired_)
        return true;
    acquired_ = true;
    return true;
#endif
}

void SingleInstanceGuard::Release() noexcept {
#ifdef _WIN32
    if (handle_) {
        CloseHandle(handle_);
        handle_ = nullptr;
    }
#else
    acquired_ = false;
#endif
}

}
