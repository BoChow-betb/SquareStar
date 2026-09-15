#pragma once

#include <string_view>

#include "imgui.h"

namespace squarestar::application {
struct AppState;
}

namespace squarestar::shell {

// Completes user-selected FX requests and refreshes the active Yahoo FX quote
// at a bounded cadence. Call from the main loop outside an ImGui frame.
void PumpDisplayCurrency(squarestar::application::AppState& state);

[[nodiscard]] std::string_view DisplayCurrencyCode(
    const squarestar::application::AppState& state) noexcept;

// Converts a raw USD stock price into the active display currency. Returns
// false while a newly selected currency is still waiting for its first quote.
[[nodiscard]] bool TryConvertUsdForDisplay(
    const squarestar::application::AppState& state,
    double usdValue,
    double& convertedValue) noexcept;

// Converts a value entered in the active display currency back to the raw USD
// quote basis used by alerts and market-data state. Returns false while a
// newly selected currency is still waiting for its first quote.
[[nodiscard]] bool TryConvertDisplayToUsd(
    const squarestar::application::AppState& state,
    double displayValue,
    double& usdValue) noexcept;

// Opens the shared picker at an already hit-tested currency-unit rectangle.
// This is also used by LiteGUI, where the quote is draw-list text rather than
// a normal ImGui item.
void OpenCurrencyPicker(squarestar::application::AppState& state,
                        ImVec2 anchorMin,
                        ImVec2 anchorMax,
                        ImGuiViewport* viewport);

// Draws only the currency code. Hover adds an underline and the hand cursor;
// click plays click.wav and opens the shared small currency list. No tooltip is
// intentionally attached to this control.
void RenderCurrencyUnitSelector(squarestar::application::AppState& state,
                                const ImVec4& textColor);

// Render once after the main surface so opening the floating list never
// disturbs the inline layout that contains the clickable currency code.
void RenderCurrencyPickerPopup(squarestar::application::AppState& state);

} // namespace squarestar::shell
