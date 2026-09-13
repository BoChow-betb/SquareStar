#pragma once

#include "platform/windows_headers.hpp"

#include <string>

namespace squarestar::platform {

// Per-user writer guard. Keep the name stable across upgrades so two versions
// cannot write the same state at once. The mutex leaves no portable data behind.
class SingleInstanceGuard {
  public:
    SingleInstanceGuard() = default;
#ifdef _WIN32
    // Allows integration tests/support tooling to exercise the exact guard
    // implementation without colliding with the production application mutex.
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
// Exposed for the Windows integration tests. The returned name starts
// with Global\\ and contains the current user SID, preventing unrelated users
// from sharing one application-instance identity.
[[nodiscard]] std::wstring CurrentUserSquareStarInstanceMutexName();
#endif

} // namespace squarestar::platform
