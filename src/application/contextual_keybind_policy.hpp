#pragma once

#include "application/navigation_state.hpp"

namespace squarestar::application {

enum class ContextualKeybindSurface {
    Uninitialized,
    None,
    Home,
    Overview,
    Stock,
    Settings,
    Monitor,
};

struct ContextualKeybindSurfaceInputs {
    bool liteGuiActive = false;
    bool hasActiveStocks = false;
    bool pureMonitorMode = false;
    SidebarTab activeSidebarTab = SidebarTab::Home;
};

ContextualKeybindSurface ResolveContextualKeybindSurface(
    ContextualKeybindSurfaceInputs inputs) noexcept;
bool HasContextualKeybindHint(ContextualKeybindSurface surface) noexcept;

}
