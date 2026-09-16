#pragma once

#include <string_view>

#include "imgui.h"

namespace squarestar::application {
struct AppState;
}

namespace squarestar::shell {


void PumpDisplayCurrency(squarestar::application::AppState& state);

[[nodiscard]] std::string_view DisplayCurrencyCode(
    const squarestar::application::AppState& state) noexcept;


[[nodiscard]] bool TryConvertUsdForDisplay(
    const squarestar::application::AppState& state,
    double usdValue,
    double& convertedValue) noexcept;


[[nodiscard]] bool TryConvertDisplayToUsd(
    const squarestar::application::AppState& state,
    double displayValue,
    double& usdValue) noexcept;


void OpenCurrencyPicker(squarestar::application::AppState& state,
                        ImVec2 anchorMin,
                        ImVec2 anchorMax,
                        ImGuiViewport* viewport);


void RenderCurrencyUnitSelector(squarestar::application::AppState& state,
                                const ImVec4& textColor);


void RenderCurrencyPickerPopup(squarestar::application::AppState& state);

}
