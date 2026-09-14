#include "application/app_state.hpp"
#include "application/lite_tab_policy.hpp"
#include "application/stock_tab_policy.hpp"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

namespace {

int failures = 0;

void Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        ++failures;
    }
}

} // namespace

int main() {
    using namespace squarestar::application;

    AppState state;
    Check(kDefaultLiteStockTabUpBinding.key == ImGuiKey_UpArrow &&
              !kDefaultLiteStockTabUpBinding.ctrl &&
              !kDefaultLiteStockTabUpBinding.shift &&
              !kDefaultLiteStockTabUpBinding.alt &&
              kDefaultLiteStockTabDownBinding.key == ImGuiKey_DownArrow &&
              !kDefaultLiteStockTabDownBinding.ctrl &&
              !kDefaultLiteStockTabDownBinding.shift &&
              !kDefaultLiteStockTabDownBinding.alt,
          "LiteGUI stock arrows have direct up/down keyboard defaults");
    for (std::size_t i = 0; i < kMaxActiveStockTabs; ++i) {
        auto context = std::make_unique<StockContext>("T" + std::to_string(i));
        context->navigation.open = true;
        state.marketData.activeContexts.push_back(std::move(context));
    }
    state.navigation.lastActiveTab = "T0";

    const auto first = ResolveActiveStockTabIndex(state);
    Check(first && *first == 0 &&
              !FindAdjacentOpenStockTabIndex(state, *first, -1) &&
              FindAdjacentOpenStockTabIndex(state, *first, 1) == 1,
          "the first LiteGUI tab exposes only the down/next direction");
    const auto firstMonitorSelection = ResolveLiteMonitorStockTabs(state);
    Check(firstMonitorSelection.count == kMaxLiteMonitorStockTabs &&
              firstMonitorSelection.indices[0] == 0 &&
              firstMonitorSelection.indices[3] == 3,
          "LiteGUI monitor mode fills at most four stable workspace tiles");

    state.navigation.lastActiveTab = "T8";
    const auto overflowMonitorSelection = ResolveLiteMonitorStockTabs(state);
    Check(overflowMonitorSelection.count == kMaxLiteMonitorStockTabs &&
              overflowMonitorSelection.indices[0] == 0 &&
              overflowMonitorSelection.indices[1] == 1 &&
              overflowMonitorSelection.indices[2] == 2 &&
              overflowMonitorSelection.indices[3] == 8,
          "LiteGUI monitor mode keeps an active tab beyond the four-tile cap visible");
    state.navigation.lastActiveTab = "T0";

    for (std::size_t i = 1; i < kMaxActiveStockTabs; ++i)
        Check(SelectAdjacentStockTab(state, 1),
              "each available LiteGUI next tab is selectable");
    Check(state.navigation.lastActiveTab == "T15" && !SelectAdjacentStockTab(state, 1),
          "LiteGUI stops at tab 16 instead of wrapping past the last tab");

    state.marketData.activeContexts[7]->navigation.open = false;
    state.navigation.lastActiveTab = "T8";
    Check(SelectAdjacentStockTab(state, -1) && state.navigation.lastActiveTab == "T6",
          "bounded LiteGUI navigation skips a closed tab");

    state.navigation.lastActiveTab = "MISSING";
    const auto fallback = ResolveActiveStockTabIndex(state);
    Check(fallback && *fallback == 0,
          "a missing active ticker falls back to the first open LiteGUI tab");

    AppState failedStockState;
    auto priorStock = std::make_unique<StockContext>("AAPL");
    priorStock->navigation.open = true;
    failedStockState.marketData.activeContexts.push_back(std::move(priorStock));
    auto failedStock = std::make_unique<StockContext>("BAD");
    failedStock->navigation.failureReturnTicker = "AAPL";
    failedStock->navigation.failureReturnSidebarTab = SidebarTab::Stock;
    StockContext* failedStockPtr = failedStock.get();
    failedStockState.marketData.activeContexts.push_back(std::move(failedStock));
    failedStockState.navigation.activeSidebarTab = SidebarTab::Stock;
    failedStockState.navigation.lastActiveTab = "BAD";
    RestoreNavigationAfterFailedStockOpen(failedStockState, *failedStockPtr);
    Check(!failedStockPtr->navigation.open &&
              failedStockState.navigation.activeSidebarTab == SidebarTab::Stock &&
              failedStockState.navigation.lastActiveTab == "AAPL",
          "a failed first stock load returns to the exact previously active stock");

    AppState failedSidebarState;
    auto failedFromOverview = std::make_unique<StockContext>("BAD");
    failedFromOverview->navigation.failureReturnTicker = "MSFT";
    failedFromOverview->navigation.failureReturnSidebarTab = SidebarTab::Overview;
    StockContext* failedFromOverviewPtr = failedFromOverview.get();
    failedSidebarState.marketData.activeContexts.push_back(std::move(failedFromOverview));
    failedSidebarState.navigation.activeSidebarTab = SidebarTab::Stock;
    failedSidebarState.navigation.lastActiveTab = "BAD";
    RestoreNavigationAfterFailedStockOpen(failedSidebarState, *failedFromOverviewPtr);
    Check(failedSidebarState.navigation.activeSidebarTab == SidebarTab::Overview &&
              failedSidebarState.navigation.lastActiveTab == "MSFT",
          "a failed stock opened from a FullGUI page returns to that exact page");

    AppState emptyLiteFailureState;
    emptyLiteFailureState.navigation.liteGuiActive = true;
    auto loneFailedStock = std::make_unique<StockContext>("BAD");
    StockContext* loneFailedStockPtr = loneFailedStock.get();
    emptyLiteFailureState.marketData.activeContexts.push_back(std::move(loneFailedStock));
    emptyLiteFailureState.navigation.lastActiveTab = "BAD";
    RestoreNavigationAfterFailedStockOpen(emptyLiteFailureState, *loneFailedStockPtr);
    Check(emptyLiteFailureState.navigation.activeSidebarTab == SidebarTab::Stock &&
              emptyLiteFailureState.navigation.lastActiveTab.empty() &&
              emptyLiteFailureState.navigation.liteSearch.focusRequested,
          "a failed first LiteGUI stock returns immediately to the search surface");

    AppState movedWhileLoadingState;
    auto currentStock = std::make_unique<StockContext>("AAPL");
    movedWhileLoadingState.marketData.activeContexts.push_back(std::move(currentStock));
    auto staleFailedStock = std::make_unique<StockContext>("BAD");
    staleFailedStock->navigation.failureReturnTicker = "MSFT";
    StockContext* staleFailedStockPtr = staleFailedStock.get();
    movedWhileLoadingState.marketData.activeContexts.push_back(std::move(staleFailedStock));
    movedWhileLoadingState.navigation.lastActiveTab = "AAPL";
    RestoreNavigationAfterFailedStockOpen(movedWhileLoadingState, *staleFailedStockPtr);
    Check(!staleFailedStockPtr->navigation.open &&
              movedWhileLoadingState.navigation.lastActiveTab == "AAPL",
          "a late failed request closes quietly without stealing newer navigation");

    AppState comparisonState;
    auto comparisonPrimary = std::make_unique<StockContext>("NVDA");
    comparisonPrimary->navigation.open = true;
    comparisonPrimary->navigation.selectedTimeRangeIndex = 0;
    comparisonPrimary->navigation.displayedTimeRangeIndex = 0;
    comparisonPrimary->navigation.comparisonSymbols = {"NVDA", "EA"};
    squarestar::market::StockData activePrimaryData;
    activePrimaryData.success = true;
    comparisonPrimary->PublishRawData(std::move(activePrimaryData));
    StockContext* comparisonPrimaryPtr = comparisonPrimary.get();
    comparisonState.marketData.activeContexts.push_back(std::move(comparisonPrimary));

    auto stoppedComparison = std::make_unique<StockContext>("EA");
    stoppedComparison->navigation.open = true;
    stoppedComparison->navigation.selectedTimeRangeIndex = 6;
    stoppedComparison->navigation.displayedTimeRangeIndex = 6;
    squarestar::market::StockData stoppedData;
    stoppedData.success = true;
    stoppedData.tradingStatus.chartAvailability =
        squarestar::market::ChartAvailability::TradingStoppedWithHistory;
    stoppedData.tradingStatus.corporateAction =
        squarestar::market::CorporateActionStatus::Acquired;
    stoppedComparison->PublishRawData(std::move(stoppedData));
    StockContext* stoppedComparisonPtr = stoppedComparison.get();
    comparisonState.marketData.activeContexts.push_back(std::move(stoppedComparison));

    Check(ResolveComparisonSyncRangeIndex(comparisonState, *comparisonPrimaryPtr) == 6 &&
              ResolveComparisonSyncRangeIndex(comparisonState, *comparisonPrimaryPtr, 0) == 6,
          "comparison uses a stopped stock's known historical range as a floor");

    int primarySyncedRange = -1;
    int stoppedSyncedRange = -1;
    PrepareComparisonSelection(
        comparisonState,
        *comparisonPrimaryPtr,
        false,
        [&](StockContext& context, int rangeIndex) {
            if (&context == comparisonPrimaryPtr)
                primarySyncedRange = rangeIndex;
            if (&context == stoppedComparisonPtr)
                stoppedSyncedRange = rangeIndex;
        });
    Check(primarySyncedRange == 6 && stoppedSyncedRange == 6,
          "comparison synchronizes active and stopped stocks to one viable historical range");

    squarestar::market::StockData noIntradayData;
    noIntradayData.success = true;
    noIntradayData.tradingStatus.chartAvailability =
        squarestar::market::ChartAvailability::NoTradingToday;
    stoppedComparisonPtr->navigation.displayedTimeRangeIndex = 1;
    stoppedComparisonPtr->navigation.selectedTimeRangeIndex = 1;
    stoppedComparisonPtr->PublishRawData(std::move(noIntradayData));
    Check(ResolveComparisonSyncRangeIndex(comparisonState, *comparisonPrimaryPtr, 0) == 1,
          "comparison also respects a stock whose intraday range is unavailable");

    squarestar::market::StockData resumedData;
    resumedData.success = true;
    stoppedComparisonPtr->navigation.displayedTimeRangeIndex = 0;
    stoppedComparisonPtr->navigation.selectedTimeRangeIndex = 0;
    stoppedComparisonPtr->PublishRawData(std::move(resumedData));
    Check(ResolveComparisonSyncRangeIndex(comparisonState, *comparisonPrimaryPtr, 0) == 0,
          "active comparison stocks do not force an unnecessarily wide historical range");

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All LiteGUI tab policy tests passed\n";
    return EXIT_SUCCESS;
}
