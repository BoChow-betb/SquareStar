#pragma once

#include <cstdint>
#include <ctime>
#include <future>

#include "application/stock_request_tracker.hpp"
#include "domain/stock_data.hpp"

namespace squarestar::application {

enum class FetchStartResult : uint8_t {
    Started,
    Busy,
    SkippedFresh,
};

struct StockRequestState {
    std::future<squarestar::market::StockFetchResult> pendingRequest;
    std::future<squarestar::market::StockFetchResult> pendingDetailsRequest;
    StockRequestTracker tracker;
    StockRequestChannel pendingChannel = StockRequestChannel::Refresh;
    std::uint64_t pendingGeneration = 0;
    uint32_t requestedDetailMask = 0;
    uint32_t detailRetryMask = 0;
    int detailRetryAttempts = 0;
    std::time_t nextDetailRetryTime = 0;
    squarestar::market::FetchKind pendingFetchKind = squarestar::market::FetchKind::Full;
    bool isLoading = false;
    bool isBackgroundFetching = false;
    float autoRefreshTimer = 0.0f;
    float chartRefreshTimer = 0.0f;
    std::time_t lastFetchTime = 0;
    int pendingFetchRangeIndex = 0;
};

}
