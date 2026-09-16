#pragma once

#include <algorithm>

#include "application/app_config.hpp"
#include "application/app_market_data.hpp"
#include "application/app_navigation.hpp"
#include "application/app_request_state.hpp"
#include "application/alert_service.hpp"
#include "application/app_render_cache.hpp"
#include "application/screener_cache.hpp"
#include "application/settings_runtime_state.hpp"

namespace squarestar::application {


struct AppState {
    AppConfig config;
    AppNavigation navigation;
    AppRequestState requests;
    SettingsRuntimeState settings;
    AppMarketData marketData;
    ScreenerCache screenerCache;
    AppRenderCache render;
    squarestar::alerts::AlertService alerts;

    [[nodiscard]] bool UiAnimationsEnabled() const noexcept {
        return config.animEnabled && !ZeroGraphicsEnabled();
    }

    [[nodiscard]] bool ZeroGraphicsEnabled() const noexcept {
        return config.zeroGraphics || navigation.liteGuiActive;
    }

    [[nodiscard]] bool ObjectFocusEnabled() const noexcept {
        return config.objectFocus && !navigation.liteGuiActive;
    }

    [[nodiscard]] const std::vector<int>& VisibleWorldClocks() const noexcept {
        return navigation.liteGuiActive ? navigation.liteWorldClocks
                                        : config.activeWorldClocks;
    }

    void SetWorldClockEnabled(int zoneIndex, bool enabled) {
        auto& visible = navigation.liteGuiActive ? navigation.liteWorldClocks
                                                 : config.activeWorldClocks;
        const auto found = std::find(visible.begin(), visible.end(), zoneIndex);
        if (enabled) {
            if (found == visible.end())
                visible.push_back(zoneIndex);
        } else if (found != visible.end()) {
            visible.erase(found);
        }

        if (!navigation.liteGuiActive)
            return;

        std::vector<int> merged = visible;
        for (const int hiddenZone : navigation.liteHiddenWorldClocks) {
            if (std::find(merged.begin(), merged.end(), hiddenZone) != merged.end())
                continue;
            if (merged.size() >= 5)
                break;
            merged.push_back(hiddenZone);
        }
        config.activeWorldClocks = std::move(merged);
    }
};

}
