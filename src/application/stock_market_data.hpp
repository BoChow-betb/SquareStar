#pragma once

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "application/stock_data_merge.hpp"
#include "domain/stock_data.hpp"

namespace squarestar::application {

class StockMarketDataState {
  public:
    StockMarketDataState()
        : rawMarketData_(std::make_shared<squarestar::market::StockData>()) {}

    [[nodiscard]] const squarestar::market::StockData& RawData() const noexcept {
        return *rawMarketData_;
    }

    [[nodiscard]] std::shared_ptr<const squarestar::market::StockData>
    RawDataSnapshot() const noexcept {
        return rawMarketData_;
    }

    [[nodiscard]] squarestar::market::StockData CopyRawData() const {
        return *rawMarketData_;
    }

    void PublishRawData(squarestar::market::StockData next) {
        PublishRawDataAtRevision(std::move(next), dataRevision);
    }

    void PublishRawDataAtRevision(squarestar::market::StockData next,
                                  std::uint64_t revision) {
        rawMarketData_ =
            std::make_shared<squarestar::market::StockData>(std::move(next));
        dataRevision = revision;
    }

    bool ApplyQuotePatchAtRevision(const squarestar::market::StockData& patch,
                                   int timeRangeIndex,
                                   std::uint64_t revision) {
        if (!patch.success)
            return false;


        if (rawMarketData_.use_count() != 1)
            rawMarketData_ =
                std::make_shared<squarestar::market::StockData>(*rawMarketData_);
        if (!ApplyStockQuotePatch(*rawMarketData_, patch, timeRangeIndex))
            return false;
        dataRevision = revision;
        return true;
    }

    void ClearRawData() {
        PublishRawData({});
        dataRevision = 0;
    }

    std::vector<double> plot_sX, plot_sC, plot_sO, plot_sH, plot_sL;
    double plot_miY = 1e9, plot_maY = -1e9;
    int lastTimeZone = -1;
    bool needsPlotDataUpdate = false;
    uint64_t dataRevision = 0;

  private:
    std::shared_ptr<squarestar::market::StockData> rawMarketData_;
};

}
