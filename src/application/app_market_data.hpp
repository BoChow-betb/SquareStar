#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <future>
#include <list>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "application/navigation_state.hpp"
#include "application/screener_item.hpp"
#include "application/stock_context.hpp"
#include "domain/currency_conversion.hpp"

namespace squarestar::application {

using ScreenerItems = std::vector<ScreenerItem>;

enum class ScreenerLoadState : std::uint8_t {
    Idle,
    Loading,
    Refreshing,
    Ready,
    Failed,
};


struct ScreenerSnapshot {
    std::uint64_t generation = 0;
    std::string screenerId;
    ScreenerLoadState loadState = ScreenerLoadState::Idle;
    std::shared_ptr<const ScreenerItems> items;

    [[nodiscard]] bool IsLoading() const noexcept {
        return loadState == ScreenerLoadState::Loading ||
               loadState == ScreenerLoadState::Refreshing;
    }

    [[nodiscard]] bool HasRows() const noexcept {
        return items && !items->empty();
    }
};

struct CurrencyDisplayRuntime {
    std::string activeCurrency = "USD";
    double usdToActive = 1.0;
    std::time_t rateTimestamp = 0;
    bool initialized = false;
    bool requestPending = false;
    bool switchingCurrency = false;
    std::string pendingCurrency;
    std::future<squarestar::market::CurrencyRateResult> pendingRequest;
    std::chrono::steady_clock::time_point lastAttemptAt{};
};

struct AppMarketData {
    AppMarketData()
        : screenerSnapshot(std::make_shared<const ScreenerSnapshot>()) {
        activeContexts.reserve(kMaxActiveStockTabs);
    }

    void RetireActiveContexts() {
        for (auto& context : activeContexts)
            retiredLiteContexts.push_back(std::move(context));
        activeContexts.clear();
    }


CurrencyDisplayRuntime currencyDisplay;

    std::vector<std::unique_ptr<StockContext>> activeContexts;


std::list<std::unique_ptr<StockContext>> retiredLiteContexts;

    [[nodiscard]] std::shared_ptr<const ScreenerSnapshot>
    LoadScreenerSnapshot() const noexcept {
        return screenerSnapshot.load(std::memory_order_acquire);
    }

    void PublishScreenerSnapshot(ScreenerSnapshot snapshot) {
        screenerSnapshot.store(
            std::make_shared<const ScreenerSnapshot>(std::move(snapshot)),
            std::memory_order_release);
    }

    bool CompareExchangeScreenerSnapshot(
        std::shared_ptr<const ScreenerSnapshot>& expected,
        ScreenerSnapshot desired) {
        auto replacement =
            std::make_shared<const ScreenerSnapshot>(std::move(desired));
        return screenerSnapshot.compare_exchange_strong(
            expected,
            std::move(replacement),
            std::memory_order_acq_rel,
            std::memory_order_acquire);
    }

  private:
    std::atomic<std::shared_ptr<const ScreenerSnapshot>> screenerSnapshot;
};

}
