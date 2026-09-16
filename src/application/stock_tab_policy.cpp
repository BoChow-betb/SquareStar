#include "stock_tab_policy.hpp"

#include "domain/trading_status.hpp"

#include <algorithm>
#include <array>
#include <string_view>

namespace squarestar::application {

std::size_t CountOpenStockTabs(const AppState& state) noexcept {
    return static_cast<std::size_t>(std::count_if(
        state.marketData.activeContexts.begin(), state.marketData.activeContexts.end(), [](const auto& candidate) {
            return candidate && candidate->navigation.open;
        }));
}

bool HasActualStockTabs(const AppState& state) noexcept {
    return CountOpenStockTabs(state) > 0 || !state.navigation.guiWorkspace.stockTabs.empty();
}

StockContext* FindStockModeNoticeTarget(AppState& state) noexcept {
    if (!state.navigation.lastActiveTab.empty()) {
        const auto active = std::find_if(
            state.marketData.activeContexts.begin(), state.marketData.activeContexts.end(), [&](const auto& candidate) {
                return candidate && candidate->navigation.open && candidate->navigation.ticker == state.navigation.lastActiveTab;
            });
        if (active != state.marketData.activeContexts.end())
            return active->get();
    }
    const auto first = std::find_if(
        state.marketData.activeContexts.begin(), state.marketData.activeContexts.end(), [](const auto& candidate) {
            return candidate && candidate->navigation.open;
        });
    return first == state.marketData.activeContexts.end() ? nullptr : first->get();
}

void RestoreNavigationAfterFailedStockOpen(AppState& state,
                                           StockContext& failedContext) noexcept {
    failedContext.navigation.open = false;


if (state.navigation.lastActiveTab != failedContext.navigation.ticker)
        return;

    if (IsSidebarPage(failedContext.navigation.failureReturnSidebarTab)) {
        state.navigation.activeSidebarTab =
            failedContext.navigation.failureReturnSidebarTab;
        state.navigation.lastActiveTab =
            failedContext.navigation.failureReturnTicker;
        return;
    }

    if (!failedContext.navigation.failureReturnTicker.empty()) {
        const auto previous = std::find_if(
            state.marketData.activeContexts.begin(),
            state.marketData.activeContexts.end(),
            [&](const auto& candidate) {
                return candidate && candidate.get() != &failedContext &&
                       candidate->navigation.open &&
                       candidate->navigation.ticker ==
                           failedContext.navigation.failureReturnTicker;
            });
        if (previous != state.marketData.activeContexts.end()) {
            state.navigation.activeSidebarTab = SidebarTab::Stock;
            state.navigation.lastActiveTab =
                failedContext.navigation.failureReturnTicker;
            return;
        }
    }

    const auto first = std::find_if(
        state.marketData.activeContexts.begin(),
        state.marketData.activeContexts.end(),
        [&](const auto& candidate) {
            return candidate && candidate.get() != &failedContext &&
                   candidate->navigation.open;
        });
    if (first != state.marketData.activeContexts.end()) {
        state.navigation.activeSidebarTab = SidebarTab::Stock;
        state.navigation.lastActiveTab = (*first)->navigation.ticker;
        return;
    }

    state.navigation.lastActiveTab.clear();
    if (state.navigation.liteGuiActive) {
        state.navigation.activeSidebarTab = SidebarTab::Stock;
        state.navigation.liteSearch.focusRequested = true;
    } else {
        state.navigation.activeSidebarTab = SidebarTab::Home;
    }
}

bool CanEnterStockComparison(const AppState& state) noexcept {
    return CountOpenStockTabs(state) >= 2;
}

bool IsComparisonSymbolSelected(const StockContext& primary,
                                const char* symbol) noexcept {
    return symbol &&
           std::find(primary.navigation.comparisonSymbols.begin(),
                     primary.navigation.comparisonSymbols.end(),
                     symbol) != primary.navigation.comparisonSymbols.end();
}

int ResolveComparisonSyncRangeIndex(const AppState& state,
                                    const StockContext& primary,
                                    int preferredRangeIndex) noexcept {
    int targetRange = preferredRangeIndex >= 0
                          ? preferredRangeIndex
                          : primary.navigation.selectedTimeRangeIndex;
    const auto requiresHistoricalFloor = [](const StockContext& context) {
        const auto& status = context.RawData().tradingStatus;
        return status.chartAvailability ==
                   squarestar::market::ChartAvailability::NoTradingToday ||
               status.chartAvailability ==
                   squarestar::market::ChartAvailability::TradingStoppedWithHistory ||
               squarestar::market::HasInactiveTradingStatus(status);
    };

    if (requiresHistoricalFloor(primary))
        targetRange = std::max(targetRange, primary.navigation.displayedTimeRangeIndex);

    for (const auto& candidate : state.marketData.activeContexts) {
        if (!candidate || !candidate->navigation.open || !candidate->RawData().success ||
            !IsComparisonSymbolSelected(primary, candidate->navigation.ticker) ||
            !requiresHistoricalFloor(*candidate)) {
            continue;
        }


targetRange = std::max(targetRange,
                               candidate->navigation.displayedTimeRangeIndex);
    }
    return targetRange;
}

void PrepareComparisonSelection(AppState& state,
                                StockContext& primary,
                                bool requestPicker,
                                const ComparisonRangeSync& syncRange) {
    std::array<std::string_view, kMaxActiveStockTabs> available{};
    std::size_t availableCount = 0;
    for (const auto& candidate : state.marketData.activeContexts) {
        if (!candidate || !candidate->navigation.open || !candidate->RawData().success)
            continue;
        if (availableCount < available.size())
            available[availableCount++] = candidate->navigation.ticker;
    }
    const auto availableEnd = available.begin() + static_cast<std::ptrdiff_t>(availableCount);

    std::erase_if(primary.navigation.comparisonSymbols, [&](const std::string& symbol) {
        return std::find(available.begin(), availableEnd, std::string_view(symbol)) ==
               availableEnd;
    });

    const std::string primarySymbol = primary.navigation.ticker;
    if (primary.navigation.comparisonSymbols.empty() ||
        primary.navigation.comparisonSymbols.front() != primarySymbol ||
        std::count(primary.navigation.comparisonSymbols.begin(), primary.navigation.comparisonSymbols.end(),
                   primarySymbol) != 1) {
        std::erase(primary.navigation.comparisonSymbols, primarySymbol);


primary.navigation.comparisonSymbols.push_back(primarySymbol);
        std::rotate(primary.navigation.comparisonSymbols.begin(),
                    primary.navigation.comparisonSymbols.end() - 1,
                    primary.navigation.comparisonSymbols.end());
    }

    if (primary.navigation.comparisonSymbols.size() < 2) {
        const auto other = std::find_if(available.begin(), availableEnd,
                                        [&](std::string_view symbol) {
                                            return symbol != primarySymbol;
                                        });
        if (other != availableEnd)
            primary.navigation.comparisonSymbols.emplace_back(*other);
    }


if (requestPicker && CanEnterStockComparison(state))
        primary.navigation.comparisonPickerRequested = true;

    if (!syncRange)
        return;

    const int targetRange = ResolveComparisonSyncRangeIndex(state, primary);
    if (primary.navigation.selectedTimeRangeIndex != targetRange)
        syncRange(primary, targetRange);
    for (auto& candidate : state.marketData.activeContexts) {
        if (!candidate || candidate.get() == &primary ||
            !IsComparisonSymbolSelected(primary, candidate->navigation.ticker)) {
            continue;
        }
        syncRange(*candidate, targetRange);
    }
}

}
