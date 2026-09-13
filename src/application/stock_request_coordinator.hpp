#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace squarestar::application {

enum class StockRequestChannel : std::uint8_t {
    Chart,
    Quote,
    Refresh,
    Count,
};

struct StockRequestGeneration {
    std::uint64_t desired = 0;
    std::uint64_t inFlight = 0;
    std::uint64_t applied = 0;

    [[nodiscard]] bool IsSettled() const noexcept {
        return inFlight == 0 && applied == desired;
    }
};

// Owns request freshness independently for each stock-data concern. A result
// can be committed only when it still represents the latest desired generation
// for its channel; completion in one channel never advances another channel.
class StockRequestCoordinator {
  public:
    [[nodiscard]] std::uint64_t Request(StockRequestChannel channel) noexcept;
    [[nodiscard]] std::uint64_t EnsureDesired(StockRequestChannel channel) noexcept;
    [[nodiscard]] bool Begin(StockRequestChannel channel,
                             std::uint64_t generation) noexcept;
    [[nodiscard]] bool CanApply(StockRequestChannel channel,
                                std::uint64_t generation) const noexcept;
    [[nodiscard]] bool Complete(StockRequestChannel channel,
                                std::uint64_t generation,
                                bool succeeded) noexcept;
    void CancelDesired(StockRequestChannel channel) noexcept;

    [[nodiscard]] const StockRequestGeneration& State(
        StockRequestChannel channel) const noexcept;

  private:
    static constexpr std::size_t Index(StockRequestChannel channel) noexcept {
        return static_cast<std::size_t>(channel);
    }

    std::array<StockRequestGeneration,
               static_cast<std::size_t>(StockRequestChannel::Count)>
        generations_{};
};

} // namespace squarestar::application
