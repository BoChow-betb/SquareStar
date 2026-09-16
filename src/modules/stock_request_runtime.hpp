#pragma once

namespace squarestar::application {
struct AppState;
}

namespace squarestar::shell {

void PumpCompletedStockRequests(squarestar::application::AppState& state,
                                bool windowSuspended);

}
