#include "application/contextual_keybind_policy.hpp"

#include <cstdlib>
#include <iostream>

namespace {
void Check(bool condition, const char* message) {
    if (condition)
        return;
    std::cerr << "FAILED: " << message << '\n';
    std::exit(1);
}
}

int main() {
    using namespace squarestar::application;

    Check(ResolveContextualKeybindSurface(
              {true, false, false, SidebarTab::Home}) == ContextualKeybindSurface::None,
          "LiteGUI home must not schedule a keybind reminder");
    Check(ResolveContextualKeybindSurface(
              {true, true, false, SidebarTab::Stock}) == ContextualKeybindSurface::None,
          "LiteGUI stock view must not schedule a keybind reminder");
    Check(!HasContextualKeybindHint(ContextualKeybindSurface::None),
          "the disabled LiteGUI reminder surface must not render a hint");
    Check(ResolveContextualKeybindSurface(
              {false, false, false, SidebarTab::Home}) == ContextualKeybindSurface::Home,
          "full GUI home keeps contextual keybind reminders");
    Check(ResolveContextualKeybindSurface(
              {false, true, false, SidebarTab::Stock}) == ContextualKeybindSurface::Stock,
          "full GUI stock view keeps contextual keybind reminders");

    std::cout << "Contextual keybind policy tests passed.\n";
    return 0;
}
