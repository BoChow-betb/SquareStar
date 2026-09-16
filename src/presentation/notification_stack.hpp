#pragma once

#include <algorithm>
#include <cstddef>
#include <limits>
#include <optional>

namespace squarestar::presentation {


class NotificationBlockStack {
public:
    explicit NotificationBlockStack(
        float margin = 16.0f,
        float spacing = 12.0f,
        std::size_t maximumBlocks = std::numeric_limits<std::size_t>::max()) noexcept
        : nextBottom_(margin), gap_(spacing), maximumBlocks_(maximumBlocks) {}


[[nodiscard]] float ReserveSpace(float height) noexcept {
        const float bottom = nextBottom_;
        nextBottom_ += std::max(0.0f, height) + gap_;
        return bottom;
    }

    [[nodiscard]] float Reserve(float height) noexcept {
        ++reservedBlocks_;
        return ReserveSpace(height);
    }

    [[nodiscard]] float Spacing() const noexcept { return gap_; }
    [[nodiscard]] std::size_t ReservedBlocks() const noexcept { return reservedBlocks_; }
    [[nodiscard]] bool HasBlockCapacity() const noexcept {
        return reservedBlocks_ < maximumBlocks_;
    }

    [[nodiscard]] float RemainingHeight(float maximumExtent) const noexcept {
        return std::max(0.0f, std::max(0.0f, maximumExtent) - nextBottom_);
    }

    [[nodiscard]] bool CanReserve(float height, float maximumExtent) const noexcept {
        return HasBlockCapacity() &&
               std::max(0.0f, height) <= RemainingHeight(maximumExtent);
    }

    [[nodiscard]] std::optional<float> TryReserve(float height,
                                                  float maximumExtent) noexcept {
        if (!CanReserve(height, maximumExtent))
            return std::nullopt;
        return Reserve(height);
    }

private:
    float nextBottom_;
    float gap_;
    std::size_t maximumBlocks_;
    std::size_t reservedBlocks_ = 0;
};

}
