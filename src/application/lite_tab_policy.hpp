#pragma once

#include "app_state.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>

namespace squarestar::application {

inline constexpr KeyBind kDefaultLiteStockTabUpBinding{
    ImGuiKey_UpArrow, false, false, false};
inline constexpr KeyBind kDefaultLiteStockTabDownBinding{
    ImGuiKey_DownArrow, false, false, false};

struct LiteMonitorTabSelection {
    std::array<std::size_t, kMaxLiteMonitorStockTabs> indices{};
    std::size_t count = 0;
};

inline std::optional<std::size_t> ResolveActiveStockTabIndex(
    const AppState& state) noexcept {
    if (!state.navigation.lastActiveTab.empty()) {
        for (std::size_t i = 0; i < state.marketData.activeContexts.size(); ++i) {
            const auto& candidate = state.marketData.activeContexts[i];
            if (candidate && candidate->navigation.open && candidate->navigation.ticker == state.navigation.lastActiveTab)
                return i;
        }
    }
    for (std::size_t i = 0; i < state.marketData.activeContexts.size(); ++i) {
        const auto& candidate = state.marketData.activeContexts[i];
        if (candidate && candidate->navigation.open)
            return i;
    }
    return std::nullopt;
}

inline std::optional<std::size_t> FindAdjacentOpenStockTabIndex(
    const AppState& state,
    std::size_t fromIndex,
    int direction) noexcept {
    if (direction == 0 || fromIndex >= state.marketData.activeContexts.size())
        return std::nullopt;

    if (direction < 0) {
        for (std::size_t i = fromIndex; i-- > 0;) {
            const auto& candidate = state.marketData.activeContexts[i];
            if (candidate && candidate->navigation.open)
                return i;
        }
        return std::nullopt;
    }

    for (std::size_t i = fromIndex + 1; i < state.marketData.activeContexts.size(); ++i) {
        const auto& candidate = state.marketData.activeContexts[i];
        if (candidate && candidate->navigation.open)
            return i;
    }
    return std::nullopt;
}

inline bool SelectAdjacentStockTab(AppState& state, int direction) noexcept {
    const auto activeIndex = ResolveActiveStockTabIndex(state);
    if (!activeIndex)
        return false;
    const auto adjacentIndex =
        FindAdjacentOpenStockTabIndex(state, *activeIndex, direction);
    if (!adjacentIndex)
        return false;
    state.navigation.lastActiveTab = state.marketData.activeContexts[*adjacentIndex]->navigation.ticker;
    return true;
}

// Lite monitor mode keeps the first four open tabs in stable workspace order.
// If the active tab sits beyond that cap, it replaces the final tile so the
// user can still step through every open stock without a large picker.
inline LiteMonitorTabSelection ResolveLiteMonitorStockTabs(
    const AppState& state) noexcept {
    LiteMonitorTabSelection selection;
    const auto activeIndex = ResolveActiveStockTabIndex(state);
    for (std::size_t i = 0;
         i < state.marketData.activeContexts.size() &&
         selection.count < selection.indices.size();
         ++i) {
        const auto& candidate = state.marketData.activeContexts[i];
        if (!candidate || !candidate->navigation.open) {
            continue;
        }
        selection.indices[selection.count++] = i;
    }
    if (activeIndex && selection.count == selection.indices.size() &&
        std::find(selection.indices.begin(), selection.indices.end(), *activeIndex) ==
            selection.indices.end()) {
        selection.indices.back() = *activeIndex;
    }
    return selection;
}

} // namespace squarestar::application
