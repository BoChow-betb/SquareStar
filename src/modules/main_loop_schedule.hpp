#pragma once

#include "application/gui_page.hpp"


namespace squarestar::application {
struct AppState;
}

namespace squarestar::shell {

squarestar::application::GuiPageKind CurrentGuiPage(
    const squarestar::application::AppState& state);
double GuiPeriodicRefreshSeconds(const squarestar::application::AppState& state,
                                 squarestar::application::GuiPageKind page);
bool GuiPageShowsSecondClock(squarestar::application::GuiPageKind page) noexcept;
double GuiSettledWaitSeconds(squarestar::application::AppState& state,
                             double lastGuiRenderAt,
                             bool windowSuspended,
                             bool priceAlertAudioPending);

}
