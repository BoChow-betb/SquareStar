#pragma once

#include <chrono>
#include <string>

namespace squarestar::application {

struct StockAlertState {
    bool priceAlertTriggered = false;
    int priceAlertSoundPlaysRemaining = 0;
    std::chrono::steady_clock::time_point nextPriceAlertSoundAt{};
    double priceAlertEditorValue = 0.0;
    double priceAlertEditorUsdPerDisplayUnit = 1.0;
    std::string priceAlertEditorCurrency = "USD";
};

} // namespace squarestar::application
