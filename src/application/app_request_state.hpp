#pragma once

#include <atomic>
#include <cstdint>
#include <memory>

#include "application/screener_request_token.hpp"

namespace squarestar::application {

struct AppRequestState {
    bool fontReloadRequested = false;
    std::atomic_uint64_t nextScreenerGeneration{0};
    std::atomic<std::shared_ptr<ScreenerRequestToken>> screenerRequest;
    std::atomic<std::shared_ptr<ScreenerRequestToken>> screenerTrendRequest;
    std::atomic_uint64_t overviewTrendPageKey{0};
    std::atomic<std::int64_t> screenerRefreshAfterEpochSeconds{0};
    uint64_t priceAlertMonitorTopologySignature = 0;
    bool priceAlertMonitorTopologyInitialized = false;
};

}
