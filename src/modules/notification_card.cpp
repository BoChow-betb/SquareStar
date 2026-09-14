#include "modules/notification_view.hpp"
#include "modules/notification_view_internal.hpp"

#include "modules/core.hpp"
#include "modules/platform.hpp"
#include "modules/views.hpp"

#include "application/app_state.hpp"
#include "application/contextual_keybind_policy.hpp"
#include "application/key_bindings.hpp"
#include "application/main_loop_signal.hpp"
#include "application/monitor_stock_policy.hpp"
#include "application/notification_text.hpp"
#include "application/runtime_state.hpp"
#include "application/ui_animation.hpp"
#include "domain/market_calendar.hpp"
#include "platform/windows_path.hpp"
#include "presentation/notification_layout.hpp"
#include "presentation/notification_stack.hpp"
#include "services/http_client.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace squarestar::shell {

using squarestar::application::ApplicationRuntime;
using squarestar::application::AppUiMode;
using squarestar::application::RequestGuiRedraw;
using squarestar::application::RequestGuiWakeAt;
using squarestar::application::UiRounding;
using squarestar::application::AppState;
using squarestar::application::CountMonitorStockTiles;
using squarestar::application::ShouldHoldMonitorModeHint;
using squarestar::application::ResolveContextualKeybindSurface;
using squarestar::application::HasContextualKeybindHint;
using squarestar::application::ContextualKeybindSurface;
using squarestar::application::FormatKeyBind;
using squarestar::application::StockContext;
using squarestar::application::TerminalAction;
using squarestar::application::IsLightGuiTheme;
using squarestar::presentation::NotificationBlockStack;
using squarestar::presentation::NotificationCardWidth;
using squarestar::presentation::LinkedNotificationCardHeight;
using squarestar::presentation::ShouldStackNotificationRowValues;

// ============================================================================
struct GroupedStockMoveLineLayout {
    std::string bodyText;
    std::string valueText;
    int valueDirection = 0;
    bool stackValue = false;
    ImVec2 bodySize{0.0f, 0.0f};
    ImVec2 valueSize{0.0f, 0.0f};
    float height = 0.0f;
};

static constexpr float kMarketMoveValueGap = 10.0f;
static constexpr float kMarketMoveLineGap = 3.0f;


static std::vector<GroupedStockMoveLineLayout> BuildGroupedStockMoveLineLayouts(
    ImFont* bodyFont,
    float bodyFontSize,
    float cardWidth,
    const std::vector<squarestar::application::StockMoveNotification>& rows) {
    std::vector<GroupedStockMoveLineLayout> layouts;
    if (!bodyFont || rows.empty())
        return layouts;

    const std::size_t visibleRows =
        std::min(rows.size(), squarestar::application::kNotificationMaxStackRows);
    layouts.reserve(visibleRows + (rows.size() > visibleRows ? 1u : 0u));
    for (std::size_t index = 0; index < visibleRows; ++index) {
        const auto& row = rows[index];
        GroupedStockMoveLineLayout line;
        line.bodyText = row.ticker + (row.priceAlert ? " alert: " : ": ") +
                        squarestar::application::FormatStockMovePrices(
                            row.before, row.after, row.currency);
        line.valueText = squarestar::application::FormatStockMoveDelta(row.before, row.after);
        if (!line.valueText.empty()) {
            line.valueDirection = row.after > row.before ? 1 : -1;
        } else if (row.priceAlert && std::isfinite(row.alertThreshold) &&
                   row.alertThreshold > 0.0) {
            char threshold[64]{};
            std::snprintf(threshold, sizeof(threshold), "level %.2f", row.alertThreshold);
            line.valueText = threshold;
            line.valueDirection = 0;
        }

        line.bodySize = bodyFont->CalcTextSizeA(bodyFontSize,
                                                std::numeric_limits<float>::max(),
                                                0.0f,
                                                line.bodyText.c_str());
        if (!line.valueText.empty()) {
            line.valueSize = bodyFont->CalcTextSizeA(bodyFontSize,
                                                     std::numeric_limits<float>::max(),
                                                     0.0f,
                                                     line.valueText.c_str());
            line.stackValue = ShouldStackNotificationRowValues(
                line.bodySize.x, line.valueSize.x, cardWidth, kMarketMoveValueGap);
            line.height = line.stackValue
                              ? line.bodySize.y + kMarketMoveLineGap + line.valueSize.y
                              : std::max(line.bodySize.y, line.valueSize.y);
        } else {
            line.height = line.bodySize.y;
        }
        layouts.push_back(std::move(line));
    }

    if (rows.size() > visibleRows) {
        GroupedStockMoveLineLayout line;
        line.bodyText = "+" + std::to_string(rows.size() - visibleRows) + " more";
        line.bodySize = bodyFont->CalcTextSizeA(bodyFontSize,
                                                std::numeric_limits<float>::max(),
                                                0.0f,
                                                line.bodyText.c_str());
        line.height = line.bodySize.y;
        layouts.push_back(std::move(line));
    }
    return layouts;
}

struct NotificationCardLayout {
    float width = 0.0f;
    float contentWidth = 0.0f;
    float drawContentWidth = 0.0f;
    float height = 0.0f;
    float actionY = 0.0f;
    float dismissHeight = 0.0f;
    float smooth = 1.0f;
    bool hasUrlAction = false;
    bool hasPathAction = false;
    bool hasTickerAction = false;
    bool hasAction = false;
    bool hasGroupedRows = false;
    bool hasAccent = false;
    bool stackAccent = false;
    bool light = false;
    ImFont* titleFont = nullptr;
    ImFont* bodyFont = nullptr;
    float titleFontSize = 0.0f;
    float bodyFontSize = 0.0f;
    ImVec2 titleSize{0.0f, 0.0f};
    ImVec2 bodySize{0.0f, 0.0f};
    ImVec2 actionLabelSize{0.0f, 0.0f};
    ImVec2 actionSize{0.0f, 0.0f};
    std::vector<GroupedStockMoveLineLayout> groupedRows;
};

struct NotificationCardInputResult {
    bool dismissed = false;
    bool actionHovered = false;
};

static constexpr float kNotificationHorizontalPadding = 18.0f;
static constexpr float kNotificationVerticalPadding = 14.0f;
static constexpr float kNotificationExternalIconGap = 5.0f;
static constexpr float kNotificationExternalIconSize = 8.0f;

static float GroupedNotificationBodyHeight(
    const std::vector<GroupedStockMoveLineLayout>& rows) {
    float height = 0.0f;
    for (std::size_t index = 0; index < rows.size(); ++index) {
        if (index > 0)
            height += kMarketMoveLineGap;
        height += rows[index].height;
    }
    return height;
}

static NotificationCardLayout BuildNotificationCardLayout(
    AppState& state,
    const ImGuiViewport& viewport,
    const NotificationCardSpec& spec) {
    NotificationCardLayout layout;
    layout.width = NotificationCardWidth(viewport.WorkSize.x);
    layout.contentWidth = std::max(40.0f, layout.width - 36.0f);
    layout.drawContentWidth =
        std::max(40.0f, layout.width - kNotificationHorizontalPadding * 2.0f);
    layout.hasUrlAction = spec.actionUrl && *spec.actionUrl;
    layout.hasPathAction = spec.actionPath && *spec.actionPath;
    layout.hasTickerAction = spec.actionTicker && *spec.actionTicker && spec.requestedTicker;
    layout.hasAction = spec.actionLabel && *spec.actionLabel &&
                       (layout.hasUrlAction || layout.hasPathAction || layout.hasTickerAction);
    layout.titleFont = state.render.fontData ? state.render.fontData : ImGui::GetFont();
    layout.bodyFont = state.render.fontNormal ? state.render.fontNormal : ImGui::GetFont();
    layout.titleFontSize =
        state.render.fontData ? state.config.theme.fontData : ImGui::GetFontSize();
    layout.bodyFontSize =
        state.render.fontNormal ? state.config.theme.fontBody : ImGui::GetFontSize();

    const char* title = spec.title ? spec.title : "";
    const char* body = spec.body ? spec.body : "";
    layout.titleSize = layout.titleFont->CalcTextSizeA(layout.titleFontSize,
                                                       std::numeric_limits<float>::max(),
                                                       layout.contentWidth,
                                                       title);
    layout.hasGroupedRows = spec.groupedStockRows && spec.groupedStockRows->size() > 1;
    if (layout.hasGroupedRows) {
        layout.groupedRows = BuildGroupedStockMoveLineLayouts(layout.bodyFont,
                                                              layout.bodyFontSize,
                                                              layout.width,
                                                              *spec.groupedStockRows);
    }

    layout.hasAccent = !layout.hasGroupedRows && spec.accentText && *spec.accentText &&
                       spec.accentDirection != 0;
    if (!layout.hasGroupedRows) {
        layout.bodySize = layout.bodyFont->CalcTextSizeA(layout.bodyFontSize,
                                                         std::numeric_limits<float>::max(),
                                                         layout.hasAccent ? 0.0f
                                                                          : layout.contentWidth,
                                                         body);
    }
    ImVec2 accentSize{0.0f, 0.0f};
    if (layout.hasAccent) {
        accentSize = layout.bodyFont->CalcTextSizeA(layout.bodyFontSize,
                                                    std::numeric_limits<float>::max(),
                                                    0.0f,
                                                    spec.accentText);
        layout.stackAccent = layout.bodySize.x + kMarketMoveValueGap + accentSize.x >
                             layout.contentWidth;
    }

    float bodyHeight = layout.hasGroupedRows ? GroupedNotificationBodyHeight(layout.groupedRows)
                                             : layout.bodySize.y;
    if (layout.hasAccent) {
        bodyHeight = layout.stackAccent
                         ? layout.bodySize.y + kMarketMoveLineGap + accentSize.y
                         : std::max(layout.bodySize.y, accentSize.y);
    }

    if (layout.hasAction) {
        layout.actionLabelSize = layout.bodyFont->CalcTextSizeA(
            layout.bodyFontSize,
            std::numeric_limits<float>::max(),
            layout.contentWidth,
            spec.actionLabel);
        layout.actionSize =
            ImVec2(layout.actionLabelSize.x + kNotificationExternalIconGap +
                       kNotificationExternalIconSize,
                   std::max(layout.actionLabelSize.y, kNotificationExternalIconSize));
    }

    const float naturalHeight = kNotificationVerticalPadding + layout.titleSize.y + 8.0f +
                                bodyHeight +
                                (layout.hasAction ? 6.0f + layout.actionSize.y : 0.0f) +
                                kNotificationVerticalPadding;
    layout.height = layout.hasAction ? LinkedNotificationCardHeight(naturalHeight) : naturalHeight;
    layout.actionY = layout.hasAction
                         ? layout.height - kNotificationVerticalPadding - layout.actionSize.y
                         : layout.height - kNotificationVerticalPadding;
    layout.dismissHeight =
        layout.hasAction ? std::max(1.0f, layout.actionY - 4.0f) : layout.height;
    layout.smooth = spec.animation * spec.animation * (3.0f - 2.0f * spec.animation);
    layout.light = IsLightGuiTheme(state.config.themeModeIndex);
    return layout;
}

static NotificationCardInputResult HandleNotificationCardInput(
    AppState& state,
    const NotificationCardSpec& spec,
    const NotificationCardLayout& layout) {
    NotificationCardInputResult result;
    const char* dismissId = spec.dismissId ? spec.dismissId : "##DismissNotification";
    ImGui::SetCursorPos(ImVec2(0.0f, 0.0f));
    if (spec.pointerInteractionEnabled) {
        result.dismissed = ImGui::InvisibleButton(
            dismissId, ImVec2(layout.width, layout.dismissHeight));
        if (result.dismissed) {
            if (layout.hasPathAction)
                (void)squarestar::platform::RevealFileInFolder(spec.actionPath);
            PlayUISound("click.wav", state);
            RequestGuiRedraw();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    } else {
        // The click that created the notice must not dismiss it in the same frame.
        ImGui::Dummy(ImVec2(layout.width, layout.dismissHeight));
    }

    if (!layout.hasAction || !spec.pointerInteractionEnabled)
        return result;

    ImGui::SetCursorPos(ImVec2(std::max(0.0f, kNotificationHorizontalPadding - 4.0f),
                              layout.actionY - 2.0f));
    ImGui::PushID(dismissId);
    ImGui::InvisibleButton("##NotificationAction",
                           ImVec2(layout.actionSize.x + 8.0f, layout.actionSize.y + 4.0f));
    result.actionHovered = ImGui::IsItemHovered();
    if (result.actionHovered)
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        if (layout.hasPathAction)
            (void)squarestar::platform::RevealFileInFolder(spec.actionPath);
        else if (layout.hasUrlAction)
            squarestar::http::OpenExternalHttpsUrl(spec.actionUrl);
        else if (layout.hasTickerAction)
            spec.requestedTicker->assign(spec.actionTicker);
        PlayUISound("click.wav", state);
        RequestGuiRedraw();
        result.dismissed = true;
    }
    ImGui::PopID();
    return result;
}

static void DrawNotificationCardBody(AppState& state,
                                     const NotificationCardSpec& spec,
                                     const NotificationCardLayout& layout,
                                     bool actionHovered) {
    const char* title = spec.title ? spec.title : "";
    const char* body = spec.body ? spec.body : "";
    const ImVec2 cardMin = ImGui::GetWindowPos();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImU32 titleColor = ImGui::ColorConvertFloat4ToU32(
        layout.light ? ImVec4(0.08f, 0.08f, 0.09f, layout.smooth)
                     : ImVec4(0.96f, 0.96f, 0.97f, layout.smooth));
    const ImVec4 neutralBodyColor =
        layout.light ? ImVec4(0.36f, 0.36f, 0.39f, layout.smooth)
                     : ImVec4(0.70f, 0.70f, 0.73f, layout.smooth);
    const ImU32 packedNeutralBodyColor = ImGui::ColorConvertFloat4ToU32(neutralBodyColor);

    draw->AddText(layout.titleFont,
                  layout.titleFontSize,
                  ImVec2(cardMin.x + kNotificationHorizontalPadding,
                         cardMin.y + kNotificationVerticalPadding),
                  titleColor,
                  title,
                  nullptr,
                  layout.contentWidth);
    const ImVec2 bodyPos(cardMin.x + kNotificationHorizontalPadding,
                         cardMin.y + kNotificationVerticalPadding + layout.titleSize.y + 8.0f);

    if (layout.hasGroupedRows) {
        float lineTop = bodyPos.y;
        for (std::size_t index = 0; index < layout.groupedRows.size(); ++index) {
            const auto& line = layout.groupedRows[index];
            const bool tickerRow = index < spec.groupedStockRows->size() &&
                                   index < squarestar::application::kNotificationMaxStackRows;
            const ImVec2 rowMin(bodyPos.x - 4.0f, lineTop - 1.0f);
            const ImVec2 rowMax(bodyPos.x + layout.drawContentWidth + 4.0f,
                                lineTop + line.height + 1.0f);
            const bool rowHovered =
                spec.pointerInteractionEnabled && tickerRow && spec.requestedTicker &&
                ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(rowMin, rowMax, false);
            if (rowHovered) {
                draw->AddRectFilled(
                    rowMin,
                    rowMax,
                    ImGui::ColorConvertFloat4ToU32(
                        ThemeVec(state.config.theme.searchHover, 0.55f * layout.smooth)),
                    UiRounding(state, 4.0f));
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left, false))
                    spec.requestedTicker->assign((*spec.groupedStockRows)[index].ticker);
            }
            draw->AddText(layout.bodyFont,
                          layout.bodyFontSize,
                          ImVec2(bodyPos.x, lineTop),
                          packedNeutralBodyColor,
                          line.bodyText.c_str());
            if (!line.valueText.empty()) {
                const ImVec4 valueColor =
                    line.valueDirection > 0
                        ? ThemeVec(state.config.theme.positive, layout.smooth)
                    : line.valueDirection < 0
                        ? ThemeVec(state.config.theme.negative, layout.smooth)
                        : neutralBodyColor;
                const ImVec2 valuePos =
                    line.stackValue
                        ? ImVec2(bodyPos.x, lineTop + line.bodySize.y + kMarketMoveLineGap)
                        : ImVec2(bodyPos.x + line.bodySize.x + kMarketMoveValueGap, lineTop);
                draw->AddText(layout.bodyFont,
                              layout.bodyFontSize,
                              valuePos,
                              ImGui::ColorConvertFloat4ToU32(valueColor),
                              line.valueText.c_str());
            }
            lineTop += line.height;
            if (index + 1 < layout.groupedRows.size())
                lineTop += kMarketMoveLineGap;
        }
    } else {
        draw->AddText(layout.bodyFont,
                      layout.bodyFontSize,
                      bodyPos,
                      packedNeutralBodyColor,
                      body,
                      nullptr,
                      layout.hasAccent ? 0.0f : layout.contentWidth);
        if (layout.hasAccent) {
            const ImVec4 accentColor =
                spec.accentDirection > 0 ? ThemeVec(state.config.theme.positive, layout.smooth)
                                         : ThemeVec(state.config.theme.negative, layout.smooth);
            const ImVec2 accentPos =
                layout.stackAccent
                    ? ImVec2(bodyPos.x, bodyPos.y + layout.bodySize.y + kMarketMoveLineGap)
                    : ImVec2(bodyPos.x + layout.bodySize.x + kMarketMoveValueGap, bodyPos.y);
            draw->AddText(layout.bodyFont,
                          layout.bodyFontSize,
                          accentPos,
                          ImGui::ColorConvertFloat4ToU32(accentColor),
                          spec.accentText);
        }
    }

    if (!layout.hasAction)
        return;

    const ImVec2 actionPos(cardMin.x + kNotificationHorizontalPadding,
                           cardMin.y + layout.actionY);
    const ImVec4 actionColor =
        actionHovered ? ThemeVec(state.config.theme.text, layout.smooth)
                      : ThemeVec(state.config.theme.textDisabled, layout.smooth);
    const ImU32 packedActionColor = ImGui::ColorConvertFloat4ToU32(actionColor);
    draw->AddText(layout.bodyFont,
                  layout.bodyFontSize,
                  actionPos,
                  packedActionColor,
                  spec.actionLabel,
                  nullptr,
                  layout.drawContentWidth,
                  nullptr);
    const ImVec2 iconTopLeft(
        actionPos.x + layout.actionLabelSize.x + kNotificationExternalIconGap,
        actionPos.y +
            std::max(0.0f, (layout.actionSize.y - kNotificationExternalIconSize) * 0.5f));
    const ImVec2 iconTopRight(iconTopLeft.x + kNotificationExternalIconSize, iconTopLeft.y);
    draw->AddLine(ImVec2(iconTopLeft.x, iconTopLeft.y + kNotificationExternalIconSize),
                  iconTopRight,
                  packedActionColor,
                  1.25f);
    draw->AddLine(iconTopRight,
                  ImVec2(iconTopRight.x - 4.0f, iconTopRight.y),
                  packedActionColor,
                  1.25f);
    draw->AddLine(iconTopRight,
                  ImVec2(iconTopRight.x, iconTopRight.y + 4.0f),
                  packedActionColor,
                  1.25f);
    if (actionHovered) {
        draw->AddLine(ImVec2(actionPos.x, actionPos.y + layout.actionSize.y),
                      ImVec2(actionPos.x + layout.actionLabelSize.x,
                             actionPos.y + layout.actionSize.y),
                      packedActionColor,
                      1.0f);
    }
}

NotificationCardRenderResult RenderNotificationCard(
    AppState& state,
    ImGuiViewport* viewport,
    NotificationBlockStack& stack,
    const NotificationCardSpec& spec) {
    if (!viewport)
        return NotificationCardRenderResult::Deferred;

    const NotificationCardLayout layout = BuildNotificationCardLayout(state, *viewport, spec);
    // LiteGUI grows its host window on the next redraw instead of clipping a card
    // into the compact search shell.
    const float verticalCapacity = squarestar::presentation::NotificationVerticalCapacity(
        viewport->WorkSize.y, APP_TITLE_BAR_HEIGHT);
    const auto bottomReservation = stack.TryReserve(layout.height, verticalCapacity);
    if (!bottomReservation) {
        if (state.navigation.liteGuiActive)
            RequestGuiRedraw();
        return NotificationCardRenderResult::Deferred;
    }

    const float restingX = viewport->WorkPos.x + viewport->WorkSize.x - layout.width - 16.0f;
    ImGui::SetNextWindowPos(
        ImVec2(restingX + (1.0f - layout.smooth) * 56.0f,
               viewport->WorkPos.y + viewport->WorkSize.y - layout.height - *bottomReservation),
        ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(layout.width, layout.height), ImGuiCond_Always);
#ifdef IMGUI_HAS_VIEWPORT
    ImGui::SetNextWindowViewport(viewport->ID);
#endif
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, UiRounding(state, 10.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, layout.smooth);
    ImGui::PushStyleColor(ImGuiCol_WindowBg,
                          layout.light ? ImVec4(0.975f, 0.975f, 0.98f, 0.995f)
                                       : ImVec4(0.055f, 0.055f, 0.06f, 0.985f));
    ImGui::PushStyleColor(ImGuiCol_Border,
                          layout.light ? ImVec4(0.55f, 0.55f, 0.58f, 1.0f)
                                       : ImVec4(0.34f, 0.34f, 0.36f, 1.0f));
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
                                   ImGuiWindowFlags_NoResize |
                                   ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoScrollbar |
                                   ImGuiWindowFlags_NoFocusOnAppearing |
                                   ImGuiWindowFlags_NoNav;
    ImGui::Begin(spec.windowId ? spec.windowId : "##Notification", nullptr, flags);
    const NotificationCardInputResult input = HandleNotificationCardInput(state, spec, layout);
    DrawNotificationCardBody(state, spec, layout, input.actionHovered);
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(4);
    return input.dismissed ? NotificationCardRenderResult::Dismissed
                           : NotificationCardRenderResult::Visible;
}


} // namespace squarestar::shell
