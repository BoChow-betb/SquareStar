#include "application/contextual_keybind_policy.hpp"

namespace squarestar::application {

ContextualKeybindSurface ResolveContextualKeybindSurface(
    ContextualKeybindSurfaceInputs inputs) noexcept {
    if (inputs.liteGuiActive)
        return ContextualKeybindSurface::None;
    if (inputs.pureMonitorMode) {
        return ContextualKeybindSurface::Monitor;
    }
    if (inputs.activeSidebarTab == SidebarTab::Home) {
        return ContextualKeybindSurface::Home;
    }
    if (inputs.activeSidebarTab == SidebarTab::Overview) {
        return ContextualKeybindSurface::Overview;
    }
    if (inputs.activeSidebarTab == SidebarTab::Settings) {
        return ContextualKeybindSurface::Settings;
    }
    if (inputs.activeSidebarTab == SidebarTab::Stock && inputs.hasActiveStocks) {
        return ContextualKeybindSurface::Stock;
    }
    return ContextualKeybindSurface::None;
}

bool HasContextualKeybindHint(ContextualKeybindSurface surface) noexcept {
    return surface == ContextualKeybindSurface::Home ||
           surface == ContextualKeybindSurface::Overview ||
           surface == ContextualKeybindSurface::Stock;
}

} // namespace squarestar::application
