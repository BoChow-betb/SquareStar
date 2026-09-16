#pragma once

#include "app_state.hpp"
#include "lite_tab_policy.hpp"

#include <cstddef>
#include <functional>

namespace squarestar::application {

using ComparisonRangeSync = std::function<void(StockContext&, int)>;

std::size_t CountOpenStockTabs(const AppState& state) noexcept;
bool HasActualStockTabs(const AppState& state) noexcept;
StockContext* FindStockModeNoticeTarget(AppState& state) noexcept;
void RestoreNavigationAfterFailedStockOpen(AppState& state,
                                           StockContext& failedContext) noexcept;
bool CanEnterStockComparison(const AppState& state) noexcept;
bool IsComparisonSymbolSelected(const StockContext& primary,
                                const char* symbol) noexcept;
int ResolveComparisonSyncRangeIndex(const AppState& state,
                                    const StockContext& primary,
                                    int preferredRangeIndex = -1) noexcept;
void PrepareComparisonSelection(AppState& state,
                                StockContext& primary,
                                bool requestPicker,
                                const ComparisonRangeSync& syncRange);

}
