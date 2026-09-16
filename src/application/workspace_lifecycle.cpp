#include "application/workspace_lifecycle.hpp"

#include "application/app_state.hpp"
#include "application/lite_context_lifecycle.hpp"
#include "application/stock_context.hpp"
#include "domain/market_symbol.hpp"

#include <algorithm>
#include <memory>
#include <string>

namespace squarestar::application {
void CaptureOpenGuiStockTabs(AppState& state) {
    state.navigation.guiWorkspace.stockTabs.clear();
    state.navigation.guiWorkspace.stockTabs.reserve(state.marketData.activeContexts.size());
    for (const auto& context : state.marketData.activeContexts) {
        if (!context || !context->navigation.open)
            continue;
        state.navigation.guiWorkspace.stockTabs.push_back(
            {context->navigation.ticker,
             context->navigation.selectedTimeRangeIndex,
             context->navigation.upperTabIndex,
             context->navigation.monitorExcluded});
    }
}

void RestoreSavedGuiStockTabs(AppState& state,
                              int maximumRangeIndex,
                              const RestoreStockTabCallback& restoreData,
                              bool keepSavedTabs) {
    maximumRangeIndex = std::max(0, maximumRangeIndex);
    for (const SavedGuiStockTab& saved : state.navigation.guiWorkspace.stockTabs) {
        if (!market::MarketSymbol::Parse(saved.ticker))
            continue;
        auto restored = std::make_unique<StockContext>(saved.ticker);
        restored->navigation.selectedTimeRangeIndex =
            std::clamp(saved.selectedTimeRangeIndex, 0, maximumRangeIndex);
        restored->navigation.displayedTimeRangeIndex = restored->navigation.selectedTimeRangeIndex;
        restored->requests.pendingFetchRangeIndex = restored->navigation.selectedTimeRangeIndex;
        restored->navigation.upperTabIndex = std::clamp(saved.upperTabIndex, 0, 3);
        restored->navigation.monitorExcluded = saved.monitorExcluded;
        restored->navigation.justOpened = true;
        if (restoreData)
            restoreData(*restored);
        state.marketData.activeContexts.push_back(std::move(restored));
    }
    if (!keepSavedTabs)
        state.navigation.guiWorkspace.stockTabs.clear();
    if (!state.navigation.lastActiveTab.empty()) {
        const bool activeTabRestored =
            std::any_of(state.marketData.activeContexts.begin(),
                        state.marketData.activeContexts.end(),
                        [&](const auto& context) {
                            return context && context->navigation.ticker == state.navigation.lastActiveTab;
                        });
        if (!activeTabRestored)
            state.navigation.lastActiveTab = state.marketData.activeContexts.empty()
                                      ? std::string{}
                                      : std::string(state.marketData.activeContexts.front()->navigation.ticker);
    }
}


}
