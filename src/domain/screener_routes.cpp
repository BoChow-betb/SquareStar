#include "screener_routes.hpp"

#include <algorithm>

namespace squarestar::market {

const std::array<ScreenerRoute, kScreenerRouteCount> kScreenerRoutes{{
    {"most_actives", "most_actives", "Most Active"},
    {"trending_now", "", "Trending Now"},
    {"day_gainers", "day_gainers", "Day Gainers"},
    {"day_losers", "day_losers", "Day Losers"},
    // 52-week gainers/losers use custom percent-change requests. The provider
    // builds them from guiId, so yahooId stays blank.
    {"52_week_gainers", "", "52Wk Gainers"},
    {"52_week_losers", "", "52Wk Losers"},
    {"watchlist", "", "Watchlist"},
}};

const ScreenerRoute* FindScreenerRouteByGuiId(std::string_view id) noexcept {
    const auto found = std::find_if(kScreenerRoutes.begin(), kScreenerRoutes.end(),
                                    [id](const ScreenerRoute& route) {
                                        return route.guiId == id;
                                    });
    return found == kScreenerRoutes.end() ? nullptr : &*found;
}

} // namespace squarestar::market
