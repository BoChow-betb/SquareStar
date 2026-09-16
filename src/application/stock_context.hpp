#pragma once

#include <cstdio>
#include <string>

#include "application/stock_alert_state.hpp"
#include "application/stock_market_data.hpp"
#include "application/stock_navigation.hpp"
#include "application/stock_request_state.hpp"
#include "application/stock_render_cache.hpp"

namespace squarestar::application {

struct StockContext {
    StockMarketDataState marketData;
    StockRequestState requests;
    StockNavigationState navigation;
    StockRenderCache render;
    StockAlertState alerts;

    explicit StockContext(const std::string& value) {
        navigation.hasSearched = true;
        std::snprintf(navigation.ticker, sizeof(navigation.ticker), "%s", value.c_str());
    }
    StockContext(const StockContext&) = delete;
    StockContext& operator=(const StockContext&) = delete;
    StockContext(StockContext&&) = delete;
    StockContext& operator=(StockContext&&) = delete;

    [[nodiscard]] const squarestar::market::StockData& RawData() const noexcept {
        return marketData.RawData();
    }
    [[nodiscard]] squarestar::market::StockData CopyRawData() const {
        return marketData.CopyRawData();
    }
    [[nodiscard]] auto RawDataSnapshot() const noexcept {
        return marketData.RawDataSnapshot();
    }
    void PublishRawData(squarestar::market::StockData next) {
        marketData.PublishRawData(std::move(next));
    }
    void PublishRawDataAtRevision(squarestar::market::StockData next,
                                  std::uint64_t revision) {
        marketData.PublishRawDataAtRevision(std::move(next), revision);
    }
    bool ApplyQuotePatchAtRevision(const squarestar::market::StockData& patch,
                                   int timeRangeIndex,
                                   std::uint64_t revision) {
        return marketData.ApplyQuotePatchAtRevision(patch, timeRangeIndex, revision);
    }
    void ClearRawData() { marketData.ClearRawData(); }
};

}
