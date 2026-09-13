#pragma once

#include <atomic>
#include <cstdint>
#include <list>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "application/navigation_state.hpp"
#include "application/screener_item.hpp"
#include "application/stock_context.hpp"

namespace squarestar::application {

using ScreenerItems = std::vector<ScreenerItem>;

enum class ScreenerLoadState : std::uint8_t {
    Idle,
    Loading,
    Refreshing,
    Ready,
    Failed,
};

// A screener surface is published as one immutable value. Keeping the route,
// generation, phase, and rows together prevents the renderer from combining a
// newly selected route with rows or flags left behind by the previous request.
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

    // StockContext objects themselves stay heap-stable behind unique_ptr. The
    // active set is capped at sixteen tabs and is traversed far more often than
    // it is inserted/erased, so contiguous pointer storage is a better fit than
    // one allocation per std::list node.
    std::vector<std::unique_ptr<StockContext>> activeContexts;
    // Retired contexts may outlive the visible surface while in-flight work
    // settles, so keep the node-stable retirement queue independent of the hot
    // active-tab traversal container.
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

} // namespace squarestar::application
