#pragma once

namespace squarestar::application {
struct AppState;
}

namespace squarestar::shell {

void RenderCreditsModal(squarestar::application::AppState& state,
                        bool openCreditsModal);
void RenderClearDataModal(squarestar::application::AppState& state,
                          bool openSavedDataModal);

} // namespace squarestar::shell
