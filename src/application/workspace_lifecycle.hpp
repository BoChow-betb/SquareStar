#pragma once

#include <functional>

namespace squarestar::application {

struct AppState;
struct StockContext;

using RestoreStockTabCallback = std::function<void(StockContext&)>;

void CaptureOpenGuiStockTabs(AppState& state);

void RestoreSavedGuiStockTabs(AppState& state,
                              int maximumRangeIndex,
                              const RestoreStockTabCallback& restoreData,
                              bool keepSavedTabs = false);


}
