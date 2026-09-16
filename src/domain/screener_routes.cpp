#include "screener_routes.hpp"

#include <algorithm>

namespace squarestar::market {

const std::array<ScreenerRoute, kScreenerRouteCount> kScreenerRoutes{{
    {"most_actives", "most_actives", "Most Active"},
    {"trending_now", "", "Trending Now"},
    {"day_gainers", "day_gainers", "Day Gainers"},
    {"day_losers", "day_losers", "Day Losers"},


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

}
