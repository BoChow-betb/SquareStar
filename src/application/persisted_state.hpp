#pragma once

#include "application/app_config.hpp"
#include "application/app_navigation.hpp"
#include "application/alert_service.hpp"

namespace squarestar::application {

struct PersistedStateConstView {
    const AppConfig& config;
    const AppNavigation& navigation;
    const squarestar::alerts::AlertService& alerts;
};

struct PersistedStateView {
    AppConfig& config;
    AppNavigation& navigation;
    squarestar::alerts::AlertService& alerts;

    operator PersistedStateConstView() const noexcept {
        return {config, navigation, alerts};
    }
};

template <typename State>
PersistedStateView PersistedStateOf(State& state) noexcept {
    return {state.config, state.navigation, state.alerts};
}

template <typename State>
PersistedStateConstView PersistedStateOf(const State& state) noexcept {
    return {state.config, state.navigation, state.alerts};
}

} // namespace squarestar::application
