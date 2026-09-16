#pragma once

#include "platform/windows_headers.hpp"

#include <string>

namespace squarestar::platform {


class SingleInstanceGuard {
  public:
    SingleInstanceGuard() = default;
#ifdef _WIN32


    explicit SingleInstanceGuard(std::wstring instanceNameOverride);
#endif
    SingleInstanceGuard(const SingleInstanceGuard&) = delete;
    SingleInstanceGuard& operator=(const SingleInstanceGuard&) = delete;
    ~SingleInstanceGuard();

    [[nodiscard]] bool Acquire();
    void Release() noexcept;

  private:
#ifdef _WIN32
    HANDLE handle_ = nullptr;
    std::wstring instanceNameOverride_;
#else
    bool acquired_ = false;
#endif
};

#ifdef _WIN32


[[nodiscard]] std::wstring CurrentUserSquareStarInstanceMutexName();
#endif

}
