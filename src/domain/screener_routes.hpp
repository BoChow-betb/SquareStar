#pragma once

#include <array>
#include <string_view>

namespace squarestar::market {

struct ScreenerRoute {
    std::string_view guiId;
    std::string_view yahooId;
    std::string_view label;
};

inline constexpr std::size_t kScreenerRouteCount = 7;
extern const std::array<ScreenerRoute, kScreenerRouteCount> kScreenerRoutes;

const ScreenerRoute* FindScreenerRouteByGuiId(std::string_view id) noexcept;

} // namespace squarestar::market
