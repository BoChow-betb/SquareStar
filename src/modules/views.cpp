#include "modules/views.hpp"
#include "modules/core.hpp"
#include "modules/ui_focus.hpp"
#include "modules/market_data.hpp"
#include "modules/integrated_search_bar.hpp"
#include "modules/lite_gui.hpp"
#include "modules/gui_shell.hpp"

#include "services/config_save_queue.hpp"
#include "application/alert_service.hpp"
#include "application/main_loop_signal.hpp"
#include "application/app_limits.hpp"
#include "application/notification_text.hpp"
#include "application/navigation_state.hpp"
#include "application/screener_controller.hpp"
#include "application/search_state.hpp"
#include "domain/market_calendar.hpp"
#include "domain/screener_routes.hpp"
#include "domain/symbol_search.hpp"
#include "platform/world_clock_runtime.hpp"
#include "presentation/notification_layout.hpp"
#include "presentation/screener_sparkline.hpp"
#include "presentation/stock_display_text.hpp"
#include "services/network_runtime.hpp"
#include "services/symbol_search_service.hpp"

#include <algorithm>
#include <ctime>
#include <vector>
namespace squarestar::shell {


using squarestar::platform::Win32AppRuntime;
using squarestar::format::FormatLargeNumber;
using squarestar::format::FormatDouble;
using squarestar::market::kScreenerRoutes;
using squarestar::market::MarketSettlementTimeLabel;
using squarestar::application::RequestGuiRedraw;
using squarestar::application::RequestGuiWakeAt;
using squarestar::application::UiRounding;
using squarestar::presentation::kControlHeight;
using squarestar::presentation::GuiShellRuntime;
using squarestar::presentation::BuildSparklineUnitGeometry;
using squarestar::presentation::EllipsizeTextBinary;
using squarestar::application::AppState;
using squarestar::application::ScreenerItem;
using squarestar::application::TerminalAction;
using squarestar::application::IsLightGuiTheme;
using squarestar::application::UserFeedbackType;
using squarestar::alerts::PriceAlertToast;
using squarestar::alerts::PriceAlertToastElapsedSeconds;
using squarestar::alerts::PriceAlertToastNeedsContinuousRedraw;
using squarestar::presentation::NotificationBlockStack;
using squarestar::presentation::NotificationCardWidth;
bool OpenNotificationStock(AppState& state, const std::string& ticker) {
    const StockOpenResult result = OpenStock(state, ticker);
    if (result != StockOpenResult::SelectedExisting &&
        result != StockOpenResult::OpenedNew)
        return false;

    if (state.navigation.liteGuiActive) {
        if (state.navigation.liteMonitorMode)
            SetLiteMonitorMode(state, false);
    } else {
        ExitPureMonitorMode(state);
    }
    state.navigation.activeSidebarTab = squarestar::application::SidebarTab::Stock;
    state.navigation.lastActiveTab = ticker;
    RequestGuiRedraw();
    return true;
}

void RenderPriceAlertPopups(AppState& state, NotificationBlockStack& stack) {
    if (state.alerts.ToastCount() == 0 || state.StartupAnimationVisible())
        return;

    const auto now = std::chrono::steady_clock::now();
    if (now < state.alerts.AlertPresentationNotBefore()) {
        RequestGuiWakeAt(state.alerts.AlertPresentationNotBefore());
        return;
    }

    struct PendingAlertAction {
        std::string ticker;
        bool muteUntilSettlement = false;
    };
    std::vector<PendingAlertAction> pendingActions;
    bool requestConfigSave = false;
    bool playClick = false;

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float margin = 16.0f;
    const float width = NotificationCardWidth(viewport->WorkSize.x);
    const float alertTitleFontSize =
        state.render.fontData ? state.config.theme.fontData : ImGui::GetFontSize();
    const float titleRowHeight = std::max(26.0f, alertTitleFontSize);
    const float bodyLineHeight = ImGui::GetTextLineHeight();
    const float priceRowsHeight =
        bodyLineHeight * 2.0f + ImGui::GetStyle().ItemSpacing.y;
    const float priceRowsY = 14.0f + titleRowHeight + 10.0f;
    const float lookupY = priceRowsY + priceRowsHeight + 8.0f;
    const float checkboxY = lookupY + bodyLineHeight + 10.0f;
    const float height = checkboxY + ImGui::GetFrameHeight() + 14.0f;
    const float liteVerticalCapacity =
        squarestar::presentation::NotificationVerticalCapacity(
            viewport->WorkSize.y, APP_TITLE_BAR_HEIGHT);
    const auto pauseToastTimer = [](PriceAlertToast& toast) {
        if (toast.startedAt != std::chrono::steady_clock::time_point{}) {
            toast.startedAt +=
                std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                    std::chrono::duration<float>(UiFrameDelta()));
        }
    };

    // FullGUI keeps the requested 3-windowed / 5-fullscreen price-alert limit.
    // LiteGUI walks the whole five-item queue so hidden alerts can have their
    // timers paused while the shared two-card tower is occupied.
    const std::size_t toastCount =
        state.navigation.liteGuiActive
            ? state.alerts.ToastCount()
            : std::min(state.alerts.ToastCount(),
                       state.navigation.isFullscreen
                           ? squarestar::application::kNotificationMaxVisibleBlocks
                           : std::size_t{3});
    for (std::size_t toastIndex = 0; toastIndex < toastCount; ++toastIndex) {
        PriceAlertToast* toastPtr = state.alerts.ToastAt(toastIndex);
        if (!toastPtr)
            break;
        PriceAlertToast& toast = *toastPtr;

        if (state.navigation.liteGuiActive && !stack.HasBlockCapacity()) {
            pauseToastTimer(toast);
            continue;
        }
        if (toast.startedAt == std::chrono::steady_clock::time_point{})
            toast.startedAt = now;

        const float elapsed = PriceAlertToastElapsedSeconds(toast, now);
        if (elapsed >= squarestar::alerts::kPriceAlertTotalSeconds) {
            pendingActions.push_back({toast.ticker, false});
            continue;
        }

        const float nextStaticBoundary = state.UiAnimationsEnabled()
                                             ? squarestar::alerts::kPriceAlertEnterSeconds +
                                                   squarestar::alerts::kPriceAlertHoldSeconds
                                             : squarestar::alerts::kPriceAlertTotalSeconds;
        if (elapsed < nextStaticBoundary) {
            const auto wakeAt = toast.startedAt +
                std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                    std::chrono::duration<float>(nextStaticBoundary));
            RequestGuiWakeAt(wakeAt);
        }

        float visible = 1.0f;
        if (state.UiAnimationsEnabled()) {
            if (elapsed < squarestar::alerts::kPriceAlertEnterSeconds) {
                visible = squarestar::application::EaseOutCubic(
                    elapsed / squarestar::alerts::kPriceAlertEnterSeconds);
                RequestGuiRedraw();
            } else if (elapsed > squarestar::alerts::kPriceAlertEnterSeconds +
                                   squarestar::alerts::kPriceAlertHoldSeconds) {
                visible = 1.0f - squarestar::application::EaseOutCubic(
                                     (elapsed - squarestar::alerts::kPriceAlertEnterSeconds -
                                      squarestar::alerts::kPriceAlertHoldSeconds) /
                                     squarestar::alerts::kPriceAlertExitSeconds);
                RequestGuiRedraw();
            }
        }

        float bottomOffset = 0.0f;
        if (state.navigation.liteGuiActive) {
            const auto reservation = stack.TryReserve(height, liteVerticalCapacity);
            if (!reservation) {
                pauseToastTimer(toast);
                continue;
            }
            bottomOffset = *reservation;
        } else {
            bottomOffset = stack.Reserve(height);
        }
        const float hiddenOffset = width + margin + 6.0f;
        const ImVec2 position(
            viewport->WorkPos.x + viewport->WorkSize.x - width - margin +
                hiddenOffset * (1.0f - visible),
            viewport->WorkPos.y + viewport->WorkSize.y - height - bottomOffset);
        ImGui::SetNextWindowPos(position, ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_Always);
        const bool lightAlert = IsLightGuiTheme(state.config.themeModeIndex);
        const ImVec4 alertBg = lightAlert ? ImVec4(0.975f, 0.975f, 0.98f, 0.995f)
                                          : ImVec4(0.055f, 0.055f, 0.06f, 0.985f);
        const ImVec4 alertBorder = lightAlert ? ImVec4(0.55f, 0.55f, 0.58f, 1.0f)
                                              : ImVec4(0.34f, 0.34f, 0.36f, 1.0f);
        const ImVec4 alertText = lightAlert ? ImVec4(0.08f, 0.08f, 0.09f, 1.0f)
                                            : ImVec4(0.96f, 0.96f, 0.97f, 1.0f);
        const ImVec4 alertDim = lightAlert ? ImVec4(0.36f, 0.36f, 0.39f, 1.0f)
                                           : ImVec4(0.66f, 0.66f, 0.69f, 1.0f);
        const ImVec4 alertButton = lightAlert ? ImVec4(0.90f, 0.90f, 0.92f, 1.0f)
                                              : ImVec4(0.12f, 0.12f, 0.13f, 1.0f);
        const ImVec4 alertButtonHover = lightAlert ? ImVec4(0.82f, 0.82f, 0.85f, 1.0f)
                                                   : ImVec4(0.24f, 0.24f, 0.26f, 1.0f);
        const ImVec4 alertButtonActive = lightAlert ? ImVec4(0.74f, 0.74f, 0.78f, 1.0f)
                                                    : ImVec4(0.31f, 0.31f, 0.33f, 1.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18.0f, 14.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, UiRounding(state, 10.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, visible);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, alertBg);
        ImGui::PushStyleColor(ImGuiCol_Border, alertBorder);
        ImGui::PushStyleColor(ImGuiCol_Text, alertText);
        ImGui::PushStyleColor(ImGuiCol_TextDisabled, alertDim);
        ImGui::PushStyleColor(ImGuiCol_Button, alertButton);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, alertButtonHover);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, alertButtonActive);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, alertButton);
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, alertButtonHover);
        ImGui::PushStyleColor(ImGuiCol_CheckMark, alertText);
        const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
                                       ImGuiWindowFlags_NoResize |
                                       ImGuiWindowFlags_NoMove |
                                       ImGuiWindowFlags_NoSavedSettings |
                                       ImGuiWindowFlags_NoScrollbar |
                                       ImGuiWindowFlags_NoFocusOnAppearing;
        char windowId[96]{};
        std::snprintf(windowId,
                      sizeof(windowId),
                      "##PriceAlertPopup_%s",
                      toast.ticker.c_str());
        ImGui::Begin(windowId, nullptr, flags);

        bool dismiss = false;
        bool muteUntilSettlement = false;
        std::string requestedTicker;
        ImGui::PushFont(state.render.fontData ? state.render.fontData : ImGui::GetFont());
        ImGui::TextUnformatted("[Alert]");
        ImGui::SameLine();
        ImGui::TextColored(lightAlert ? ImVec4(0.76f, 0.10f, 0.12f, 1.0f)
                                      : ImVec4(0.92f, 0.22f, 0.24f, 1.0f),
                           "%s",
                           toast.ticker.c_str());
        ImGui::PopFont();

        ImGui::SetCursorPosY(priceRowsY);
        ImGui::Text("Current price  %s USD", FormatDouble(toast.price).c_str());
        ImGui::TextDisabled("Alert level  %s USD or below",
                            FormatDouble(toast.threshold).c_str());

        ImGui::SetCursorPosY(lookupY);
        const std::string lookupLabel = "Open in Terminal  " + toast.ticker;
        const float lookupWidth = std::max(40.0f, width - 36.0f);
        const bool lookupClicked = RenderUnifiedLink(
            state, lookupLabel.c_str(), "##PriceAlertOpenTicker", lookupWidth, 1.0f);
        const ImVec2 lookupMin = ImGui::GetItemRectMin();
        const ImVec2 lookupMax = ImGui::GetItemRectMax();
        // RenderUnifiedLink includes an 8 px external-link arrow plus a 5 px gap.
        // Extend the exclusion rect so clicking the icon cannot also dismiss the card.
        const bool lookupHovered =
            ImGui::IsWindowHovered() &&
            ImGui::IsMouseHoveringRect(
                lookupMin, ImVec2(lookupMax.x + 13.0f, lookupMax.y), false);
        if (lookupClicked) {
            requestedTicker = toast.ticker;
            playClick = true;
        }

        ImGui::SetCursorPosY(checkboxY);
        const std::string muteLabel =
            "Mute until market close (" +
            MarketSettlementTimeLabel(std::time(nullptr)) + ")";
        if (ImGui::Checkbox(muteLabel.c_str(), &muteUntilSettlement))
            dismiss = muteUntilSettlement;
        const bool muteHovered = ImGui::IsItemHovered();
        const bool alertHovered = ImGui::IsWindowHovered(
            ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
        if (alertHovered && !muteHovered && !lookupHovered)
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if (!dismiss && alertHovered && !muteHovered && !lookupHovered &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left, false)) {
            dismiss = true;
            playClick = true;
        }
        ImGui::End();
        ImGui::PopStyleColor(10);
        ImGui::PopStyleVar(4);

        if (!requestedTicker.empty() && OpenNotificationStock(state, requestedTicker))
            dismiss = true;

        if (dismiss) {
            pendingActions.push_back({toast.ticker, muteUntilSettlement});
            requestConfigSave = requestConfigSave || muteUntilSettlement;
        } else if (PriceAlertToastNeedsContinuousRedraw(
                       toast, state.UiAnimationsEnabled())) {
            RequestGuiRedraw();
        }
    }

    // Mutating the toast deque is deferred until every visible card has been
    // rendered, so dismissing one alert cannot invalidate the next card.
    for (const auto& action : pendingActions)
        SilencePriceAlertTicker(state, action.ticker, action.muteUntilSettlement);
    if (requestConfigSave)
        squarestar::config::RequestConfigSave();
    // Silencing may stop active audio, so play feedback only after all queued
    // dismissals have been applied.
    if (playClick)
        PlayUISound("click.wav", state);
}

void RenderHomePage(AppState& state) {
    ImVec2 avail = ImGui::GetContentRegionAvail();
    time_t t_now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    const tm local_time = squarestar::platform::SafeTimeTm(t_now, false);
    char dateBuf[64];
    std::strftime(dateBuf, sizeof(dateBuf), "%A, %B %d, %Y", &local_time);
    float searchBarY = avail.y * 0.5f;
    float marketY = searchBarY - 60.0f;
    float dateY = marketY - 80.0f;
    if (dateY < 10.0f) {
        float offset = 10.0f - dateY;
        dateY += offset;
        marketY += offset;
        searchBarY += offset;
    }
    ImGui::PushFont(state.render.fontLarge);
    float dW = ImGui::CalcTextSize(dateBuf).x;
    ImGui::SetCursorPos(ImVec2((avail.x - dW) * 0.5f, dateY));
    ImGui::TextDisabled("%s", dateBuf);
    ImGui::PopFont();
    std::string mktStr = GetNextMarketOpenString();
    ImGui::PushFont(state.render.fontGiant);
    const ImVec2 marketTextSize = ImGui::CalcTextSize(mktStr.c_str());
    ImGui::SetCursorPos(ImVec2((avail.x - marketTextSize.x) * 0.5f, marketY));
    const ImVec2 marketTextPos = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddRectFilled(
        ImVec2(marketTextPos.x - 5.0f, marketTextPos.y - 3.0f),
        ImVec2(marketTextPos.x + marketTextSize.x + 5.0f,
               marketTextPos.y + marketTextSize.y + 3.0f),
        ImGui::ColorConvertFloat4ToU32(ThemeVec(state.config.theme.bg)));
    ImGui::TextUnformatted(mktStr.c_str());
    ImGui::PopFont();
    float searchW = ImMin(600.0f, avail.x * 0.8f);
    // customWidth already includes the search field, gaps, watchlist, and alert buttons.
    // Center the complete control so it shares the title's vertical axis.
    ImGui::SetCursorPos(ImVec2((avail.x - searchW) * 0.5f, searchBarY));
    RenderIntegratedSearchBar(
        state,
        state.navigation.mainSearch,
        "HOME",
        searchW,
        [&](const std::string& sym) {
            PlayUISound("click.wav", state);
            OpenStock(state, sym);
            memset(state.navigation.mainSearch.inputBuffer, 0, sizeof(state.navigation.mainSearch.inputBuffer));
            state.navigation.mainSearch.lastQuery = "";
            state.navigation.mainSearch.results.clear();
        });
}
static void DrawFiveDaySparkline(const char* id,
                          const ScreenerItem& item,
                          ImVec2 size,
                          const ImVec4& lineColor,
                          float revealProgress = 1.0f) {
    // Published screener snapshots are shared with background trend workers.
    // Keep renderer geometry thread-local instead of mutating a shared row.
    static thread_local std::vector<ImVec2> unitPoints;
    BuildSparklineUnitGeometry(item.sparkline, unitPoints);
    if (unitPoints.size() < 2 || size.x <= 2.0f || size.y <= 2.0f) {
        ImGui::Dummy(size);
        return;
    }
    ImGui::InvisibleButton(id, size);
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    static thread_local std::vector<ImVec2> allPoints;
    static thread_local std::vector<ImVec2> visiblePoints;
    static thread_local std::vector<ImVec2> fillPoints;
    allPoints.clear();
    visiblePoints.clear();
    fillPoints.clear();
    if (allPoints.capacity() < unitPoints.size())
        allPoints.reserve(unitPoints.size());
    if (visiblePoints.capacity() < unitPoints.size())
        visiblePoints.reserve(unitPoints.size());
    for (const ImVec2& point : unitPoints) {
        allPoints.emplace_back(min.x + point.x * size.x,
                               max.y - 2.0f - point.y * (size.y - 4.0f));
    }
    const float reveal = std::clamp(revealProgress, 0.0f, 1.0f);
    if (reveal <= 0.001f)
        return;
    const float scaledIndex = reveal * (float)(allPoints.size() - 1);
    const size_t completedIndex = std::min((size_t)std::floor(scaledIndex), allPoints.size() - 1);
    visiblePoints.insert(
        visiblePoints.end(), allPoints.begin(), allPoints.begin() + completedIndex + 1);
    const float fractional = scaledIndex - (float)completedIndex;
    if (completedIndex + 1 < allPoints.size() && fractional > 0.001f) {
        const ImVec2& from = allPoints[completedIndex];
        const ImVec2& to = allPoints[completedIndex + 1];
        visiblePoints.push_back(
            ImVec2(from.x + (to.x - from.x) * fractional, from.y + (to.y - from.y) * fractional));
    }
    if (visiblePoints.size() < 2)
        return;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    fillPoints.reserve(visiblePoints.size() + 2);
    fillPoints.insert(fillPoints.end(), visiblePoints.begin(), visiblePoints.end());
    const float fillBaseline = max.y - 1.0f;
    fillPoints.emplace_back(visiblePoints.back().x, fillBaseline);
    fillPoints.emplace_back(visiblePoints.front().x, fillBaseline);
    ImVec4 effectiveFillColor = lineColor;
    effectiveFillColor.w *= ImGui::GetStyle().Alpha * 0.16f;
    dl->AddConcavePolyFilled(fillPoints.data(),
                             (int)fillPoints.size(),
                             ImGui::ColorConvertFloat4ToU32(effectiveFillColor));
    ImVec4 effectiveLineColor = lineColor;
    effectiveLineColor.w *= ImGui::GetStyle().Alpha;
    dl->AddPolyline(visiblePoints.data(),
                    (int)visiblePoints.size(),
                    ImGui::ColorConvertFloat4ToU32(effectiveLineColor),
                    ImDrawFlags_None,
                    1.5f);
}
static bool DrawOverviewSymbol(AppState& state, const ScreenerItem& row, float rowHeight) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const ImVec2 sz = ImGui::CalcTextSize(row.symbol.c_str());
    const float hitHeight = std::max(
        sz.y,
        rowHeight - ImGui::GetStyle().CellPadding.y * 2.0f);
    const ImVec2 textPos(p.x, p.y + std::max(0.0f, (hitHeight - sz.y) * 0.5f));
    ImGui::InvisibleButton("##OpenSymbol", ImVec2(std::max(28.0f, sz.x), hitHeight));
    const bool hovered = ImGui::IsItemHovered();
    const ImU32 color = ImGui::GetColorU32(ImGuiCol_Text);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddText(textPos, color, row.symbol.c_str());
    if (hovered) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        dl->AddLine({textPos.x, textPos.y + sz.y + 1},
                    {textPos.x + sz.x, textPos.y + sz.y + 1},
                    color,
                    1.2f);
    }
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        PlayUISound("click.wav", state);
        OpenStock(state, row.symbol);
    }
    return hovered;
}
static void RenderOverviewTable(AppState& state,
                                const std::vector<ScreenerItem>& screenerItems,
                                size_t startIdx,
                                size_t count,
                                float rowHeight) {
    const float availableWidth = ImGui::GetContentRegionAvail().x;
    const bool compact = availableWidth < 820.0f;
    const bool wide = !compact && availableWidth >= 1480.0f;
    const int columns = compact ? 4 : (wide ? 10 : 7);
    const ImGuiTableFlags flags =
        ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings;
    ImGui::PushStyleColor(ImGuiCol_TableHeaderBg, ThemeVec(state.config.theme.panelAlt));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ThemeVec(state.config.theme.panelAlt));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ThemeVec(state.config.theme.panelAlt));
    ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt,
                          IsLightGuiTheme(state.config.themeModeIndex)
                              ? ThemeVec(state.config.theme.panelAlt, 0.72f)
                              : ThemeVec(state.config.theme.panel, 0.72f));
    if (!ImGui::BeginTable("OverviewMarketTable", columns, flags)) {
        ImGui::PopStyleColor(4);
        return;
    }
    ImGui::TableSetupColumn(compact ? "SYMBOL / COMPANY" : "SYMBOL",
                            compact ? ImGuiTableColumnFlags_WidthStretch
                                    : ImGuiTableColumnFlags_WidthFixed,
                            compact ? 1.7f : 76.0f);
    if (!compact)
        ImGui::TableSetupColumn("COMPANY",
                                ImGuiTableColumnFlags_WidthStretch,
                                wide ? 1.35f : 2.1f);
    ImGui::TableSetupColumn("5D TREND",
                            compact ? ImGuiTableColumnFlags_WidthFixed
                                    : ImGuiTableColumnFlags_WidthStretch,
                            compact ? 116.0f : (wide ? 0.65f : 1.4f));
    constexpr const char* priceHeader = "PRICE (USD)";
    ImGui::TableSetupColumn(priceHeader,
                            ImGuiTableColumnFlags_WidthFixed,
                            compact ? 108.0f : 124.0f);
    ImGui::TableSetupColumn("CHANGE", ImGuiTableColumnFlags_WidthFixed, compact ? 82.0f : 92.0f);
    if (!compact) {
        ImGui::TableSetupColumn("VOLUME", ImGuiTableColumnFlags_WidthFixed, 100.0f);
        if (wide) {
            ImGui::TableSetupColumn("AVG VOL (3M)", ImGuiTableColumnFlags_WidthFixed, 118.0f);
            ImGui::TableSetupColumn("P/E", ImGuiTableColumnFlags_WidthFixed, 72.0f);
            ImGui::TableSetupColumn("52W CHANGE", ImGuiTableColumnFlags_WidthFixed, 116.0f);
        }
        ImGui::TableSetupColumn("MARKET CAP", ImGuiTableColumnFlags_WidthFixed, 112.0f);
    }
    const char* compactHeaders[] = {
        "SYMBOL / COMPANY", "5D TREND", priceHeader, "CHANGE"};
    const char* fullHeaders[] = {
        "SYMBOL", "COMPANY", "5D TREND", priceHeader, "CHANGE", "VOLUME", "MARKET CAP"};
    const char* wideHeaders[] = {"SYMBOL",
                                 "COMPANY",
                                 "5D TREND",
                                 priceHeader,
                                 "CHANGE",
                                 "VOLUME",
                                 "AVG VOL (3M)",
                                 "P/E",
                                 "52W CHANGE",
                                 "MARKET CAP"};
    const char** headers = compact ? compactHeaders : (wide ? wideHeaders : fullHeaders);
    ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
    for (int headerColumn = 0; headerColumn < columns; ++headerColumn) {
        ImGui::TableSetColumnIndex(headerColumn);
        ImGui::TableHeader(headers[headerColumn]);
    }
    const std::size_t revealFirst =
        std::min(state.render.overviewRowsRevealFirstIndex, count);
    const std::size_t revealKnown =
        std::clamp(state.render.overviewRowsRevealKnownCount, revealFirst, count);
    const std::size_t revealBatchCount = revealKnown - revealFirst;
    if (state.UiAnimationsEnabled() && revealBatchCount > 0) {
        state.render.overviewRowsRevealElapsedSeconds += std::max(0.0f, UiFrameDelta());
        if (state.render.overviewRowsRevealElapsedSeconds <
            squarestar::presentation::ScreenerRowRevealSequenceSeconds(revealBatchCount)) {
            RequestGuiRedraw();
        }
    }
    for (size_t i = startIdx; i < startIdx + count; ++i) {
        const ScreenerItem& row = screenerItems[i];
        const std::size_t visibleRow = i - startIdx;
        float rowReveal = 1.0f;
        if (state.UiAnimationsEnabled() && visibleRow >= revealFirst &&
            visibleRow < revealKnown) {
            rowReveal = squarestar::presentation::ScreenerRowRevealProgress(
                state.render.overviewRowsRevealElapsedSeconds,
                visibleRow - revealFirst);
        }
        const float rowSlide = (1.0f - rowReveal) * 18.0f;
        ImGui::PushID((int)i);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha,
                            ImGui::GetStyle().Alpha * std::max(0.001f, rowReveal));
        ImGui::TableNextRow(ImGuiTableRowFlags_None, rowHeight);
        int column = 0;
        auto BeginColumn = [&](int columnIndex) {
            ImGui::TableSetColumnIndex(columnIndex);
            if (rowSlide > 0.01f)
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + rowSlide);
        };
        auto CenterRowText = [&]() {
            const float contentHeight = std::max(
                ImGui::GetTextLineHeight(),
                rowHeight - ImGui::GetStyle().CellPadding.y * 2.0f);
            ImGui::SetCursorPosY(
                ImGui::GetCursorPosY() +
                std::max(0.0f, (contentHeight - ImGui::GetTextLineHeight()) * 0.5f));
        };
        BeginColumn(column++);
        DrawOverviewSymbol(state, row, rowHeight);
        if (compact) {
            ImGui::SameLine(0.0f, 8.0f);
            CenterRowText();
            if (!row.name.empty()) {
                const std::string name =
                    EllipsizeTextBinary(row.name,
                                        std::max(40.0f, ImGui::GetContentRegionAvail().x - 6.0f));
                ImGui::TextUnformatted(name.c_str());
            } else {
                ImGui::TextDisabled("N/A");
            }
        } else {
            BeginColumn(column++);
            CenterRowText();
            if (!row.name.empty()) {
                const std::string name =
                    EllipsizeTextBinary(row.name,
                                        std::max(40.0f, ImGui::GetContentRegionAvail().x - 6.0f));
                ImGui::TextUnformatted(name.c_str());
            } else {
                ImGui::TextDisabled("N/A");
            }
        }
        BeginColumn(column++);
        const double performanceDirection =
            squarestar::presentation::ScreenerRowPerformanceDirection(
                row.hasChangePercent, row.changePercent, row.sparkline);
        const ImVec4 performanceColor =
            performanceDirection < 0.0   ? ThemeVec(state.config.theme.negative)
            : performanceDirection > 0.0 ? ThemeVec(state.config.theme.positive)
                                         : ThemeVec(state.config.theme.textDisabled);
        if (row.sparkline.size() >= 2) {
            DrawFiveDaySparkline(
                "##FiveDayTrend",
                row,
                ImVec2(std::max(28.0f, ImGui::GetContentRegionAvail().x - 8.0f), 24.0f),
                performanceColor,
                rowReveal);
        } else if (row.sparklineAttempted) {
            ImGui::TextDisabled("N/A");
        } else {
            // Reserve the chart cell while the 5D request is in flight without
            // flashing placeholder text that is immediately replaced.
            ImGui::Dummy(ImVec2(std::max(28.0f, ImGui::GetContentRegionAvail().x - 8.0f),
                                24.0f));
        }
        BeginColumn(column++);
        ImGui::AlignTextToFramePadding();
        if (row.hasPrice)
            ImGui::Text("%s", FormatDouble(row.price).c_str());
        else
            ImGui::TextDisabled("N/A");
        BeginColumn(column++);
        ImGui::AlignTextToFramePadding();
        if (row.hasChangePercent) {
            ImGui::PushStyleColor(ImGuiCol_Text, performanceColor);
            ImGui::Text("%+.2f%%", row.changePercent);
            ImGui::PopStyleColor();
        } else
            ImGui::TextDisabled("N/A");
        if (!compact) {
            BeginColumn(column++);
            ImGui::AlignTextToFramePadding();
            if (row.hasVolume)
                ImGui::Text("%s", FormatLargeNumber(row.volume).c_str());
            else
                ImGui::TextDisabled("N/A");
            if (wide) {
                BeginColumn(column++);
                ImGui::AlignTextToFramePadding();
                if (row.hasAvgVol3M)
                    ImGui::Text("%s", FormatLargeNumber(row.avgVol3M).c_str());
                else
                    ImGui::TextDisabled("N/A");
                BeginColumn(column++);
                ImGui::AlignTextToFramePadding();
                if (row.hasPeRatio)
                    ImGui::Text("%s", FormatDouble(row.peRatio).c_str());
                else
                    ImGui::TextDisabled("N/A");
                BeginColumn(column++);
                ImGui::AlignTextToFramePadding();
                if (row.hasFiftyTwoWkChange) {
                    const ImVec4 fiftyTwoWeekColor =
                        row.fiftyTwoWkChange < 0.0   ? ThemeVec(state.config.theme.negative)
                        : row.fiftyTwoWkChange > 0.0 ? ThemeVec(state.config.theme.positive)
                                                     : ThemeVec(state.config.theme.textDisabled);
                    ImGui::PushStyleColor(ImGuiCol_Text, fiftyTwoWeekColor);
                    ImGui::Text("%+.2f%%", row.fiftyTwoWkChange);
                    ImGui::PopStyleColor();
                } else {
                    ImGui::TextDisabled("N/A");
                }
            }
            BeginColumn(column++);
            ImGui::AlignTextToFramePadding();
            if (row.hasMarketCap)
                ImGui::Text("%s", FormatLargeNumber(row.marketCap).c_str());
            else
                ImGui::TextDisabled("N/A");
        }
        ImGui::PopStyleVar();
        ImGui::PopID();
    }
    ImGui::EndTable();
    ImGui::PopStyleColor(4);
}

static bool DrawOverviewPagerButton(const char* id,
                                    const char* label,
                                    float width,
                                    bool enabled) {
    ImGui::PushID(id);
    if (!enabled)
        ImGui::BeginDisabled();
    const ImVec2 size(width, kControlHeight);
    const bool pressed = ImGui::InvisibleButton("##PagerButton", size) && enabled;
    const ImVec2 minimum = ImGui::GetItemRectMin();
    const ImVec2 maximum = ImGui::GetItemRectMax();
    const ImGuiCol background =
        ImGui::IsItemActive()
            ? ImGuiCol_ButtonActive
            : (ImGui::IsItemHovered() ? ImGuiCol_ButtonHovered : ImGuiCol_Button);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(minimum,
                            maximum,
                            ImGui::GetColorU32(background),
                            ImGui::GetStyle().FrameRounding);
    const ImVec2 textSize = ImGui::CalcTextSize(label);
    const ImVec2 textPosition(
        minimum.x + std::floor((size.x - textSize.x) * 0.5f),
        minimum.y + std::floor((size.y - textSize.y) * 0.5f));
    drawList->AddText(textPosition, ImGui::GetColorU32(ImGuiCol_Text), label);
    if (!enabled)
        ImGui::EndDisabled();
    ImGui::PopID();
    return pressed;
}

static std::time_t LatestScreenerMarketTime(
    const std::vector<ScreenerItem>& items) noexcept {
    std::time_t latest = 0;
    for (const ScreenerItem& item : items)
        latest = std::max(latest, item.lastMarketTime);
    return latest;
}

void RenderOverviewFirstPage(AppState& state) {
    RenderOverviewFirstPage(
        state,
        !GuiShellRuntime().ForceCleanGuiCaptureFrame());
}

void RenderOverviewFirstPage(AppState& state, bool allowDataRequests) {
    state.navigation.activeScreenerIndex = std::clamp(
        state.navigation.activeScreenerIndex,
        0,
        static_cast<int>(std::size(kScreenerRoutes)) - 1);
    const bool pageBackRequested =
        IsActionPressed(state.config, TerminalAction::ScreenerPagePrev);
    const bool pageNextRequested =
        IsActionPressed(state.config, TerminalAction::ScreenerPageNext);
    auto SelectScreener = [&](int index) {
        if (index < 0 || index >= (int)std::size(kScreenerRoutes) ||
            index == state.navigation.activeScreenerIndex) {
            return;
        }
        const bool emptyWatchlist =
            kScreenerRoutes[index].guiId == "watchlist" && state.config.watchlist.empty();
        if (emptyWatchlist) {
            PublishUserFeedback(state,
                                UserFeedbackType::Warning,
                                "Watchlist is empty",
                                "Add a stock to your watchlist before opening this view.");
            return;
        }
        PlayUISound("transition.wav", state);
        state.navigation.activeScreenerIndex = index;
        state.navigation.currentScreenerPage = 0;
    };
    if (IsActionPressed(state.config, TerminalAction::ScreenerPrev))
        SelectScreener(state.navigation.activeScreenerIndex - 1);
    else if (IsActionPressed(state.config, TerminalAction::ScreenerNext))
        SelectScreener(state.navigation.activeScreenerIndex + 1);
    const std::string titleScreenerId(
        kScreenerRoutes[state.navigation.activeScreenerIndex].guiId);
    const auto titleSnapshot = state.marketData.LoadScreenerSnapshot();

    ImGui::PushFont(state.render.fontLarge);
    ImGui::TextUnformatted(kScreenerRoutes[state.navigation.activeScreenerIndex].label.data());
    ImGui::PopFont();
    if (titleSnapshot && titleSnapshot->screenerId == titleScreenerId &&
        titleSnapshot->HasRows()) {
        const std::time_t latestMarketTime =
            LatestScreenerMarketTime(*titleSnapshot->items);
        if (latestMarketTime > 0) {
            const std::string asOf =
                squarestar::platform::FormatAsOfTime(
                    latestMarketTime, state.config.graphTimeZone == 1);
            ImGui::SameLine(0.0f, 10.0f);
            ImGui::PushFont(state.render.fontNormal
                                ? state.render.fontNormal
                                : ImGui::GetFont());
            ImGui::TextDisabled("%s", asOf.c_str());
            ImGui::PopFont();
        }
    }
    ImGui::Spacing();
    const ImVec4 activeBg = ThemeVec(state.config.theme.overviewActive);
    const ImVec4 activeText = ThemeVec(state.config.theme.overviewActiveText);
    const ImVec4 idleBg = ThemeVec(state.config.theme.overviewIdle);
    const ImVec4 idleHover = ThemeVec(state.config.theme.overviewHover);
    const ImVec4 idleText = ThemeVec(state.config.theme.overviewIdleText);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, UiRounding(state, 16.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(15.0f, 7.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 8.0f));
    auto DrawScreenerSelector = [&](int i) {
        const bool isActive = i == state.navigation.activeScreenerIndex;
        ImGui::PushStyleColor(ImGuiCol_Button, isActive ? activeBg : idleBg);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, isActive ? activeBg : idleHover);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, isActive ? activeBg : idleHover);
        ImGui::PushStyleColor(ImGuiCol_Text, isActive ? activeText : idleText);
        if (!isActive)
            ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 0.92f);
        if (ImGui::Button(kScreenerRoutes[i].label.data()) && !isActive)
            SelectScreener(i);
        if (!isActive)
            ImGui::PopFont();
        ImGui::PopStyleColor(4);
    };
    const float selectorRight = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    for (int i = 0; i < (int)std::size(kScreenerRoutes); ++i) {
        DrawScreenerSelector(i);
        if (i + 1 < (int)std::size(kScreenerRoutes)) {
            const float nextWidth =
                ImGui::CalcTextSize(kScreenerRoutes[i + 1].label.data()).x +
                ImGui::GetStyle().FramePadding.x * 2.0f;
            if (ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + nextWidth <
                selectorRight) {
                ImGui::SameLine();
            }
        }
    }
    ImGui::PopStyleVar(3);
    ImGui::Dummy(ImVec2(0.0f, 14.0f));
    const std::string activeScreenerId(
        kScreenerRoutes[state.navigation.activeScreenerIndex].guiId);
    auto screenerSnapshot = state.marketData.LoadScreenerSnapshot();
    auto SnapshotMatchesSelection = [&] {
        return screenerSnapshot &&
               (screenerSnapshot->screenerId == activeScreenerId ||
                (!allowDataRequests && screenerSnapshot->screenerId.empty()));
    };
    const std::int64_t refreshAfter =
        state.requests.screenerRefreshAfterEpochSeconds.load(std::memory_order_acquire);
    const bool refreshDue =
        SnapshotMatchesSelection() && screenerSnapshot->loadState ==
                                          squarestar::application::ScreenerLoadState::Ready &&
        refreshAfter > 0 && static_cast<std::int64_t>(std::time(nullptr)) >= refreshAfter;
    if (allowDataRequests &&
        (!SnapshotMatchesSelection() ||
         screenerSnapshot->loadState == squarestar::application::ScreenerLoadState::Idle ||
         refreshDue)) {
        squarestar::application::StartScreenerFetch(
            state, activeScreenerId);
        screenerSnapshot = state.marketData.LoadScreenerSnapshot();
    }
    if (!SnapshotMatchesSelection() ||
        screenerSnapshot->loadState == squarestar::application::ScreenerLoadState::Loading) {
        ImU32 spinnerColor = ImGui::GetColorU32(ImGuiCol_Text);
        RenderLoadingSpinner(state, "Fetching Screener Data...", 10.0f, 3, spinnerColor);
        RequestGuiRedraw();
        return;
    }
    if (screenerSnapshot->loadState ==
        squarestar::application::ScreenerLoadState::Failed) {
        ImGui::TextDisabled("Unable to load market data.");
        ImGui::SameLine();
        if (ImGui::Button("Retry")) {
            PlayUISound("loading.wav", state);
            squarestar::application::StartScreenerFetch(
                state,
                activeScreenerId);
        }
        return;
    }
    if (!screenerSnapshot->HasRows()) {
        ImGui::TextDisabled("No matching stocks.");
        return;
    }
    const auto& screenerItems = *screenerSnapshot->items;
    const size_t totalItems = screenerItems.size();
    const float remainingHeight = std::max(1.0f, ImGui::GetContentRegionAvail().y);
    const float footerReserve = totalItems > 1 ? 80.0f : 0.0f;
    constexpr float tableHeaderHeight = 30.0f;
    constexpr float minimumOverviewRowHeight = 24.0f;
    const float rowsHeight =
        std::max(1.0f, remainingHeight - footerReserve - tableHeaderHeight - 8.0f);
    const std::size_t targetRows =
        IsGuiWindowMaximized(Win32AppRuntime().MainWindow())
            ? squarestar::application::kMaximizedOverviewRows
            : squarestar::application::kWindowedOverviewRows;
    const std::size_t fittedRows = static_cast<std::size_t>(
        std::max(1.0f, std::floor(rowsHeight / minimumOverviewRowHeight)));
    const size_t itemsPerPage = std::min(targetRows, fittedRows);
    const float overviewRowHeight =
        std::clamp(std::floor(rowsHeight / static_cast<float>(itemsPerPage)),
                   minimumOverviewRowHeight,
                   kControlHeight);
    const int totalPages = std::max(1, (int)((totalItems + itemsPerPage - 1) / itemsPerPage));
    state.navigation.currentScreenerPage = std::clamp(state.navigation.currentScreenerPage, 0, totalPages - 1);
    if (pageBackRequested && state.navigation.currentScreenerPage > 0) {
        --state.navigation.currentScreenerPage;
        PlayUISound("transition.wav", state);
    } else if (pageNextRequested && state.navigation.currentScreenerPage < totalPages - 1) {
        ++state.navigation.currentScreenerPage;
        PlayUISound("transition.wav", state);
    }
    const size_t startIdx = (size_t)state.navigation.currentScreenerPage * itemsPerPage;
    const size_t count = std::min(itemsPerPage, totalItems - startIdx);
    const bool overviewSurfaceChanged =
        state.render.overviewRowsRevealScreenerIndex !=
            state.navigation.activeScreenerIndex ||
        state.render.overviewRowsRevealPage !=
            state.navigation.currentScreenerPage ||
        state.render.overviewRowsRevealStartIndex != startIdx;
    if (!state.UiAnimationsEnabled()) {
        state.render.overviewRowsRevealScreenerIndex =
            state.navigation.activeScreenerIndex;
        state.render.overviewRowsRevealPage = state.navigation.currentScreenerPage;
        state.render.overviewRowsRevealStartIndex = startIdx;
        state.render.overviewRowsRevealFirstIndex = count;
        state.render.overviewRowsRevealKnownCount = count;
        state.render.overviewRowsRevealElapsedSeconds = 0.0f;
    } else if (overviewSurfaceChanged) {
        state.render.overviewRowsRevealScreenerIndex =
            state.navigation.activeScreenerIndex;
        state.render.overviewRowsRevealPage = state.navigation.currentScreenerPage;
        state.render.overviewRowsRevealStartIndex = startIdx;
        state.render.overviewRowsRevealFirstIndex = 0;
        state.render.overviewRowsRevealKnownCount = count;
        state.render.overviewRowsRevealElapsedSeconds = 0.0f;
    } else if (count > state.render.overviewRowsRevealKnownCount) {
        const std::size_t previousKnown = state.render.overviewRowsRevealKnownCount;
        const std::size_t previousFirst =
            std::min(state.render.overviewRowsRevealFirstIndex, previousKnown);
        const std::size_t previousBatchCount = previousKnown - previousFirst;
        const bool previousBatchFinished =
            previousBatchCount == 0 ||
            state.render.overviewRowsRevealElapsedSeconds >=
                squarestar::presentation::ScreenerRowRevealSequenceSeconds(
                    previousBatchCount);
        if (previousBatchFinished) {
            state.render.overviewRowsRevealFirstIndex = previousKnown;
            state.render.overviewRowsRevealElapsedSeconds = 0.0f;
        }
        state.render.overviewRowsRevealKnownCount = count;
    } else if (count < state.render.overviewRowsRevealKnownCount) {
        state.render.overviewRowsRevealKnownCount = count;
        state.render.overviewRowsRevealFirstIndex =
            std::min(state.render.overviewRowsRevealFirstIndex, count);
    }
    // Fetch only the rows that are visible now. If the window grows, the newly
    // visible rows are requested then instead of making the normal window pay
    // for fullscreen-only chart requests up front.
    const size_t trendRequestCount = count;
    const uint64_t overviewRequestId = screenerSnapshot->generation;
    const uint64_t trendPageKey =
        (overviewRequestId << 16U) ^ (static_cast<uint64_t>(startIdx) << 5U) ^
        trendRequestCount;
    if (allowDataRequests &&
        state.requests.overviewTrendPageKey.load(std::memory_order_acquire) !=
            trendPageKey) {
        if (squarestar::application::StartScreenerTrendFetch(
                state,
                activeScreenerId,
                startIdx,
                trendRequestCount)) {
            state.requests.overviewTrendPageKey.store(
                trendPageKey, std::memory_order_release);
        }
    }
    // Keep pagination centered on the table itself rather than depending on
    // whatever horizontal cursor state remains after EndTable().
    const float overviewTableLeft = ImGui::GetCursorPosX();
    const float overviewTableWidth = ImGui::GetContentRegionAvail().x;
    RenderOverviewTable(state, screenerItems, startIdx, count, overviewRowHeight);
    if (totalPages > 1) {
        ImGui::Dummy(ImVec2(0.0f, 14.0f));
        const std::string pageText = "Page " + std::to_string(state.navigation.currentScreenerPage + 1) +
                                     " / " + std::to_string(totalPages);
        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        const float indicatorWidth =
            std::max(88.0f, ImGui::CalcTextSize(pageText.c_str()).x + 24.0f);
        const float buttonWidth =
            std::clamp((overviewTableWidth - indicatorWidth - spacing * 2.0f) * 0.5f,
                       64.0f,
                       92.0f);
        const float controlsWidth = buttonWidth * 2.0f + indicatorWidth + spacing * 2.0f;
        ImGui::SetCursorPosX(overviewTableLeft +
                             std::max(0.0f, (overviewTableWidth - controlsWidth) * 0.5f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, UiRounding(state, 8.0f));
        const bool canGoBack = state.navigation.currentScreenerPage > 0;
        if (DrawOverviewPagerButton(
                "PreviousPager", "Previous", buttonWidth, canGoBack)) {
            --state.navigation.currentScreenerPage;
            PlayUISound("transition.wav", state);
        }
        ImGui::SameLine();
        const ImVec2 indicatorPos = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##PageIndicator", ImVec2(indicatorWidth, kControlHeight));
        const ImVec2 indicatorTextSize = ImGui::CalcTextSize(pageText.c_str());
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(indicatorPos.x + (indicatorWidth - indicatorTextSize.x) * 0.5f,
                   indicatorPos.y + (kControlHeight - indicatorTextSize.y) * 0.5f),
            ImGui::GetColorU32(ImGuiCol_TextDisabled),
            pageText.c_str());
        ImGui::SameLine();
        const bool canGoNext = state.navigation.currentScreenerPage < totalPages - 1;
        if (DrawOverviewPagerButton("NextPager", "Next", buttonWidth, canGoNext)) {
            ++state.navigation.currentScreenerPage;
            PlayUISound("transition.wav", state);
        }
        ImGui::PopStyleVar();
    }
}

} // namespace squarestar::shell
