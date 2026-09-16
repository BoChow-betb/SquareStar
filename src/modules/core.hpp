#pragma once

#include <chrono>
#include <cstddef>
#include <ctime>
#include <limits>
#include <string>
#include <string_view>

#include "application/app_limits.hpp"
#include "application/user_feedback.hpp"
#include "platform/windows_headers.hpp"
#include "imgui.h"

struct GLFWwindow;

namespace squarestar::application {
struct AppConfig;
struct AppState;
struct StockContext;
enum class TerminalAction;
}

namespace squarestar::shell {

inline constexpr float APP_TITLE_BAR_HEIGHT = 36.0f;
inline constexpr float APP_TITLE_BAR_BUTTON_WIDTH = 46.0f;
inline constexpr int APP_TITLE_BAR_CONTROL_COUNT = 3;
inline constexpr int LITE_TITLE_BAR_CONTROL_COUNT = 3;
inline constexpr int LITE_GUI_WIDTH = 900;
inline constexpr int LITE_GUI_SEARCH_HEIGHT = 108;


inline constexpr int LITE_GUI_NOTIFICATION_HEIGHT = 420;
inline constexpr std::size_t LITE_GUI_NOTIFICATION_MAX_BLOCKS = 2;
inline constexpr int LITE_GUI_STOCK_HEIGHT = 620;
inline constexpr int GUI_WINDOW_WIDTH = squarestar::application::kDefaultGuiWindowWidth;
inline constexpr int GUI_WINDOW_HEIGHT = squarestar::application::kDefaultGuiWindowHeight;
bool AnimatedButton(const squarestar::application::AppState& state,
                    const char* label,
                    const ImVec2& requestedSize,
                    bool active,
                    bool animEnabled);


bool RenderUnifiedLink(const squarestar::application::AppState& state,
                       const char* label,
                       const char* id,
                       float wrapWidth = 0.0f,
                       float restingContrast = 0.18f);
void ApplyRoundedWindowCorners(HWND hwnd);
void ApplyTheme(const squarestar::application::AppState& state);
bool BeginAnimatedFloatingMenu(
    const squarestar::application::AppState& state,
    const char* menuId,
    bool& toggleTrigger,
    ImVec2 pos,
    ImVec2 pivot = ImVec2(0.0f, 0.0f),
    bool animEnabled = true,
    ImVec2 minimumSize = ImVec2(0.0f, 0.0f),
    ImVec2 maximumSize = ImVec2(std::numeric_limits<float>::max(),
                                std::numeric_limits<float>::max()));
bool BeginClampedContextMenu(const squarestar::application::AppState& state,
                             const char* id,
                             ImGuiViewport* viewport,
                             ImVec2 estimatedSize,
                             bool enabled = true,
                             bool animEnabled = true,
                             ImVec2 activationMin = ImVec2(0.0f, 0.0f),
                             ImVec2 activationMax = ImVec2(0.0f, 0.0f),
                             ImVec2 maximumSize = ImVec2(
                                 std::numeric_limits<float>::max(),
                                 std::numeric_limits<float>::max()));
ImVec2 ClampPopupPosition(ImGuiViewport* viewport, ImVec2 position, ImVec2 size);
ImVec2 ClampTooltipPositionToOwningWindow(ImVec2 position,
                                          ImVec2 size,
                                          float margin = 6.0f);
void CloseAllAnimatedFloatingMenus();
void CloseAnimatedFloatingMenu(bool instant = true);
void CommitUiSetting(squarestar::application::AppState& state,
                     const char* sound = "click.wav");
void DrawContainedTooltip(const char* text, float preferredWrapWidth = 480.0f);
void DrawIcon(ImDrawList* drawList, ImVec2 center, float size, int type, ImU32 color);
void DrawOuterShadow(ImDrawList* drawList,
                     ImVec2 min,
                     ImVec2 max,
                     float rounding,
                     float alphaMultiplier,
                     ImU32 shadowColor = IM_COL32(0, 0, 0, 255));
void DrawRoundedCurrentWindowPanel(float rounding,
                                   ImGuiCol fillColumn = ImGuiCol_WindowBg);
void EndAnimatedFloatingMenu();
void EnforceZeroGraphicsMode(squarestar::application::AppState& state,
                             bool snapImmediately = false);
bool EnsureGlfwRuntimeInitialized();
void EvaluateStockPriceAlert(squarestar::application::AppState& state,
                             squarestar::application::StockContext& ctx);
bool ExpirePriceAlertSettlementMutes(squarestar::application::AppState& state,
                                     std::time_t& lastCheckedSecond);
void FlushTrayPriceMoves();
bool IsActionPressed(const squarestar::application::AppConfig& state,
                     squarestar::application::TerminalAction action);
bool IsAppMinimizedForNotifications();
bool IsAppWindowForeground();
bool IsGuiWindowMaximized(HWND hwnd);
bool IsStockInteractionSurfaceAudible(const squarestar::application::StockContext& ctx);
HICON LoadSquareStarIcon();
void MinimizeToTray(HWND hwnd);
bool PlayUISound(const char* filename,
                 const squarestar::application::AppState& state);
void PublishBackgroundError(squarestar::application::AppState& state,
                            std::string title,
                            std::string body);
void PublishForegroundStockMove(squarestar::application::AppState& state,
                                const char* ticker,
                                double before,
                                double after,
                                std::string_view currency);
void QueueTrayPriceMove(const squarestar::application::AppState& state,
                        const char* ticker,
                        double before,
                        double after,
                        bool priceAlert = false,
                        double alertThreshold = 0.0,
                        std::string_view currency = {});
void PublishSilentFeedback(
    squarestar::application::AppState& state,
    squarestar::application::UserFeedbackType type,
    std::string title,
    std::string body,
    squarestar::application::UserFeedbackDestination destination =
        squarestar::application::UserFeedbackDestination::Automatic);
void PublishUserFeedback(squarestar::application::AppState& state,
                         squarestar::application::UserFeedback feedback);
void PublishUserFeedback(
    squarestar::application::AppState& state,
    squarestar::application::UserFeedbackType type,
    std::string title,
    std::string body,
    std::chrono::seconds duration = std::chrono::seconds(4));
void PumpMarketOpenSound(squarestar::application::AppState& state);
void PumpPriceAlertSounds(squarestar::application::AppState& state);
void RemoveTrayIcon();
void RequestLiteGuiMode();
void RestoreFromTray(HWND hwnd);
double SecondsUntilPendingTrayPriceMoveFlush();
void SetupTrayIcon(HWND hwnd);
void TriggerTrayNotification(const char* title,
                             const char* message,
                             std::string actionPath = {});
bool ShouldPublishMarketMoveNotification(squarestar::application::AppState& state,
                                         const squarestar::application::StockContext& ctx,
                                         double before,
                                         double after);
void ShutdownGlfwRuntime();
void SilencePriceAlertForClosedTab(squarestar::application::AppState& state,
                                   squarestar::application::StockContext& ctx);
void SilencePriceAlertTicker(squarestar::application::AppState& state,
                             std::string_view ticker,
                             bool muteUntilSettlement);
void SnapAllUiAnimations(squarestar::application::AppState& state);
ImVec4 ThemeVec(const float (&color)[4], float alpha = -1.0f);
LRESULT CALLBACK TrayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
bool UiCombo(squarestar::application::AppState& state,
             const char* label,
             int* currentItem,
             const char* const items[],
             int itemCount);
float UiFrameDelta() noexcept;
bool UseBackgroundNotificationBlock();
bool UseForegroundNotificationBlocks();

}
