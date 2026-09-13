#pragma once

#include <chrono>

namespace squarestar::application {

struct StockAlertState {
    bool priceAlertTriggered = false;
    int priceAlertSoundPlaysRemaining = 0;
    std::chrono::steady_clock::time_point nextPriceAlertSoundAt{};
    double priceAlertEditorValue = 0.0;
};

} // namespace squarestar::application
