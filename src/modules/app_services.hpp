#pragma once

#include <string>

struct GLFWwindow;

namespace squarestar::application {
struct AppState;
struct StockContext;
}

namespace squarestar::shell {

void EnterLiteGuiWorkspace(GLFWwindow* window,
                           squarestar::application::AppState& state);
bool InitializeGuiRuntime(GLFWwindow*& window,
                          squarestar::application::AppState& state,
                          bool buildFonts,
                          std::string& failureReason);
void PumpInterfaceTransitionsOnMainThread(GLFWwindow*& window,
                                          squarestar::application::AppState& state);
void PumpPriceAlertMonitorRequests(squarestar::application::AppState& state);
void PumpStockAutoRefresh(squarestar::application::AppState& state,
                          double elapsedSeconds,
                          bool windowSuspended);
void RestoreSavedGuiStockTabs(squarestar::application::AppState& state,
                              bool keepSavedTabs = false);
double SecondsUntilNextStockAutoRefresh(squarestar::application::AppState& state,
                                        bool windowSuspended);
bool ShouldWatchHiddenStockContext(const squarestar::application::AppState& state,
                                   const squarestar::application::StockContext& ctx);
void ShutdownGuiRuntime(GLFWwindow*& window,
                        squarestar::application::AppState& state);
void WaitForStockStateRequests(squarestar::application::AppState& state);

} // namespace squarestar::shell
