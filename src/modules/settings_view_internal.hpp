#pragma once

namespace squarestar::application {
struct AppState;
}

namespace squarestar::shell {

// Internal settings-surface primitives shared by the page implementations.
void BeginSettingsCard(squarestar::application::AppState& state,
                       const char* id,
                       const char* heading,
                       float fixedHeight = 0.0f,
                       bool inlineHeaderControl = false,
                       float fixedWidth = 0.0f);
void EndSettingsCard();

void RenderGeneralSettingsPage(squarestar::application::AppState& state,
                               float bodyContentWidth,
                               bool& openSavedDataModal);
void RenderNotificationSettingsPage(squarestar::application::AppState& state,
                                    float bodyContentWidth);

} // namespace squarestar::shell
