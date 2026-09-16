#pragma once

#include <atomic>
#include <cstdint>

namespace squarestar::application {


class ScreenerRequestToken final {
  public:
    explicit ScreenerRequestToken(std::uint64_t generation) noexcept
        : generation_(generation) {}

    [[nodiscard]] std::uint64_t Generation() const noexcept {
        return generation_;
    }

    void Cancel() noexcept {
        cancelled_.store(true, std::memory_order_release);
    }

    [[nodiscard]] bool Cancelled() const noexcept {
        return cancelled_.load(std::memory_order_acquire);
    }

  private:
    const std::uint64_t generation_;
    std::atomic_bool cancelled_{false};
};

}
