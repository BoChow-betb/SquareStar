#include "modules/integrated_search_bar.hpp"

#include "application/app_limits.hpp"
#include "application/app_state.hpp"
#include "application/main_loop_signal.hpp"
#include "application/runtime_state.hpp"
#include "application/search_state.hpp"
#include "application/theme_profiles.hpp"
#include "application/ui_animation.hpp"
#include "domain/number_format.hpp"
#include "domain/symbol_search.hpp"
#include "domain/text.hpp"
#include "modules/core.hpp"
#include "modules/market_data.hpp"
#include "modules/notification_center.hpp"
#include "modules/ui_focus.hpp"
#include "services/network_runtime.hpp"
#include "services/symbol_search_service.hpp"

#include <algorithm>
#include <array>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <future>
#include <string>
#include <string_view>

namespace squarestar::shell {
namespace {

using squarestar::application::AppState;
using squarestar::application::ApplicationRuntime;
using squarestar::application::RequestGuiRedraw;
using squarestar::application::SearchState;
using squarestar::application::UiModeRequest;
using squarestar::application::UiRounding;
using squarestar::application::UserFeedbackType;
using squarestar::search::LookupSymbols;
using squarestar::text::UppercaseInPlace;

constexpr float kPrimaryBarHeight = 40.0f;
constexpr float kHeartSpacing = 10.0f;
constexpr float kAuxiliaryButtonSpacing = 8.0f;
constexpr float kWatchlistRowHeight = 32.0f;
constexpr float kWatchlistButtonHeight = 28.0f;

struct SearchGeometry {
    float boxWidth = 0.0f;
    ImVec2 searchMin{};
    ImVec2 searchMax{};
};

struct SearchInputResult {
    bool enterPressed = false;
    bool hovered = false;
    bool activated = false;
    ImVec2 focusMin{};
    ImVec2 focusMax{};
};

bool ExecuteUiModeCommand(const AppState& state, std::string target) {
    UppercaseInPlace(target);
    UiModeRequest request = UiModeRequest::None;
    if (!state.navigation.liteGuiActive && target == "LITEGUI")
        request = UiModeRequest::LiteGui;
    if (request == UiModeRequest::None)
        return false;
    ApplicationRuntime().RequestUiMode(request);
    RequestGuiRedraw();
    return true;
}

SearchInputResult RenderSearchInput(AppState& state,
                                    SearchState& searchState,
                                    const char* idSuffix,
                                    const SearchGeometry& geometry) {
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, UiRounding(state, 8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12.0f, 7.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ThemeVec(state.config.theme.searchBg));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,
                          ThemeVec(state.config.theme.searchHover));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive,
                          ThemeVec(state.config.theme.searchActive));
    ImGui::PushItemWidth(geometry.boxWidth);
    std::array<char, 128> inputId{};
    std::snprintf(inputId.data(), inputId.size(), "##SearchInput_%s", idSuffix);
    if (searchState.focusRequested) {
        ImGui::SetKeyboardFocusHere();
        searchState.focusRequested = false;
    }
    ImGui::PushFont(state.render.fontData ? state.render.fontData : ImGui::GetFont());
    SearchInputResult input;
    input.enterPressed = ImGui::InputTextWithHint(
        inputId.data(),
        "Search ticker...",
        searchState.inputBuffer,
        sizeof(searchState.inputBuffer),
        ImGuiInputTextFlags_CharsUppercase | ImGuiInputTextFlags_EnterReturnsTrue);
    searchState.isFocused = ImGui::IsItemActive();
    input.hovered = ImGui::IsItemHovered(
        ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    input.activated = ImGui::IsItemActivated() ||
                      ImGui::IsItemClicked(ImGuiMouseButton_Left);
    input.focusMin = ImGui::GetItemRectMin();
    input.focusMax = ImGui::GetItemRectMax();
    DrawObjectFocusOutline(
        state, input.focusMin, input.focusMax, searchState.isFocused);
    ImGui::PopFont();
    ImGui::PopItemWidth();
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar(2);
    return input;
}

void RenderWatchlistRows(
    AppState& state,
    const std::function<void(const std::string&)>& onOpen) {
    if (state.config.watchlist.empty()) {
        state.navigation.watchlistFilterBuffer[0] = '\0';
        return;
    }
    const bool filterEnabled = state.config.watchlist.size() > 6;
    if (filterEnabled) {
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, UiRounding(state, 6.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 5.0f));
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##watchlist_filter",
                                 "Search watchlist...",
                                 state.navigation.watchlistFilterBuffer,
                                 IM_ARRAYSIZE(state.navigation.watchlistFilterBuffer),
                                 ImGuiInputTextFlags_CharsUppercase);
        if (state.navigation.liteGuiActive && ImGui::IsItemActivated())
            PlayUISound("click.wav", state);
        ImGui::PopStyleVar(2);
        ImGui::Dummy(ImVec2(0.0f, 4.0f));
    } else {
        state.navigation.watchlistFilterBuffer[0] = '\0';
    }

    const std::string_view filter = state.navigation.watchlistFilterBuffer;
    std::array<std::size_t, squarestar::application::kMaxWatchlistItems> matches{};
    std::size_t matchCount = 0;
    for (std::size_t index = 0;
         index < state.config.watchlist.size() && matchCount < matches.size();
         ++index) {
        if (filter.empty() || state.config.watchlist[index].find(filter) != std::string::npos)
            matches[matchCount++] = index;
    }
    if (matchCount == 0) {
        ImGui::TextDisabled("No matching symbols.");
        return;
    }

    const float listHeight =
        4.0f + static_cast<float>(std::min<std::size_t>(matchCount, 6)) *
                   kWatchlistRowHeight;
    const ImGuiWindowFlags listFlags =
        matchCount > 6 ? ImGuiWindowFlags_AlwaysVerticalScrollbar
                       : ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoScrollWithMouse;
    ImGui::BeginChild("##watchlist_rows", ImVec2(0, listHeight), false, listFlags);
    if (matchCount > 6 && ImGui::IsWindowHovered() &&
        ImGui::GetScrollMaxY() > 0.0f &&
        std::abs(ImGui::GetIO().MouseWheel) > 0.0f) {
        PlayUISound("transition.wav", state);
    }
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(7.0f, 2.0f));
    if (ImGui::BeginTable("##watchlist_table",
                          2,
                          ImGuiTableFlags_SizingStretchProp |
                              ImGuiTableFlags_PadOuterX |
                              ImGuiTableFlags_NoPadInnerX)) {
        ImGui::TableSetupColumn("Symbol", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Remove", ImGuiTableColumnFlags_WidthFixed, 44.0f);
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(matchCount), kWatchlistRowHeight);
        bool changed = false;
        while (!changed && clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                const std::size_t index = matches[static_cast<std::size_t>(row)];
                const std::string ticker = state.config.watchlist[index];
                ImGui::PushID(static_cast<int>(index));
                ImGui::TableNextRow(0, kWatchlistRowHeight);
                ImGui::TableSetColumnIndex(0);
                ImGui::PushFont(state.render.fontNormal ? state.render.fontNormal
                                                        : ImGui::GetFont());
                const ImVec2 symbolMin = ImGui::GetCursorScreenPos();
                const ImVec2 symbolSize(
                    std::max(1.0f, ImGui::GetContentRegionAvail().x),
                    kWatchlistButtonHeight);
                const bool open = ImGui::InvisibleButton("##OpenTicker", symbolSize);
                const bool symbolHovered = ImGui::IsItemHovered();
                const bool symbolHeld = ImGui::IsItemActive();
                DrawLastItemFocusOutline(state, symbolHovered || symbolHeld, 4);
                if (symbolHovered)
                    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                ImDrawList* symbolDraw = ImGui::GetWindowDrawList();
                if (symbolHovered || symbolHeld) {
                    symbolDraw->AddRectFilled(
                        symbolMin,
                        ImVec2(symbolMin.x + symbolSize.x,
                               symbolMin.y + symbolSize.y),
                        ImGui::ColorConvertFloat4ToU32(ThemeVec(
                            symbolHeld ? state.config.theme.searchActive
                                       : state.config.theme.searchHover)),
                        UiRounding(state, 5.0f));
                }
                const ImVec2 textSize = ImGui::CalcTextSize(ticker.c_str());
                symbolDraw->AddText(
                    ImVec2(symbolMin.x + 6.0f,
                           symbolMin.y +
                               std::max(0.0f, (symbolSize.y - textSize.y) * 0.5f)),
                    ImGui::ColorConvertFloat4ToU32(
                        ThemeVec(state.config.theme.text)),
                    ticker.c_str());
                ImGui::PopFont();

                ImGui::TableSetColumnIndex(1);
                ImVec2 removeMin = ImGui::GetCursorScreenPos();
                const ImVec2 removeSize(28.0f, 28.0f);
                removeMin.x += std::max(
                    0.0f,
                    (std::max(28.0f, ImGui::GetContentRegionAvail().x) -
                     removeSize.x) *
                        0.5f);
                removeMin.y += 1.0f;
                ImGui::SetCursorScreenPos(removeMin);
                const bool remove =
                    ImGui::InvisibleButton("##RemoveTicker", removeSize);
                const bool removeHovered = ImGui::IsItemHovered();
                const bool removeHeld = ImGui::IsItemActive();
                DrawLastItemFocusOutline(state, removeHovered || removeHeld, 5);
                if (removeHovered || removeHeld) {
                    ImGui::GetWindowDrawList()->AddRectFilled(
                        removeMin,
                        ImVec2(removeMin.x + removeSize.x,
                               removeMin.y + removeSize.y),
                        ImGui::ColorConvertFloat4ToU32(ThemeVec(
                            state.config.theme.negative,
                            removeHeld ? 0.20f : 0.13f)),
                        UiRounding(state, 6.0f));
                }
                const ImU32 removeColor = ImGui::ColorConvertFloat4ToU32(
                    ThemeVec(state.config.theme.negative));
                const ImVec2 center(removeMin.x + removeSize.x * 0.5f,
                                    removeMin.y + removeSize.y * 0.5f);
                ImGui::GetWindowDrawList()->AddLine(
                    ImVec2(center.x - 4.0f, center.y - 4.0f),
                    ImVec2(center.x + 4.0f, center.y + 4.0f),
                    removeColor,
                    1.8f);
                ImGui::GetWindowDrawList()->AddLine(
                    ImVec2(center.x + 4.0f, center.y - 4.0f),
                    ImVec2(center.x - 4.0f, center.y + 4.0f),
                    removeColor,
                    1.8f);
                ImGui::PopID();

                if (open) {
                    PlayUISound("click.wav", state);
                    onOpen(ticker);
                    if (state.navigation.liteGuiActive)
                        ImGui::CloseCurrentPopup();
                    else
                        CloseAnimatedFloatingMenu(true);
                    state.navigation.watchlistOpen = false;
                }
                if (remove) {
                    RemoveTickerFromWatchlist(state, ticker);
                    if (state.config.watchlist.empty()) {
                        if (state.navigation.liteGuiActive)
                            ImGui::CloseCurrentPopup();
                        else
                            CloseAnimatedFloatingMenu(true);
                        state.navigation.watchlistOpen = false;
                    }
                    changed = true;
                    break;
                }
            }
        }
        if (changed)
            clipper.End();
        ImGui::EndTable();
    }
    ImGui::PopStyleVar();
    ImGui::EndChild();
}

void RenderWatchlistMenu(
    AppState& state,
    const char* idSuffix,
    const std::function<void(const std::string&)>& onExecute) {
    ImGui::SameLine(0, kHeartSpacing);
    const ImVec2 heartMin = ImGui::GetCursorScreenPos();
    const ImVec2 heartMax(heartMin.x + kPrimaryBarHeight,
                          heartMin.y + kPrimaryBarHeight);
    std::array<char, 128> buttonId{};
    std::array<char, 128> menuId{};
    std::snprintf(buttonId.data(), buttonId.size(), "##WLBtn_%s", idSuffix);
    std::snprintf(menuId.data(), menuId.size(), "WLMenu_%s", idSuffix);
    const bool nativeLiteMenu = state.navigation.liteGuiActive;
    const bool liteMenuWasOpen = nativeLiteMenu && ImGui::IsPopupOpen(menuId.data());
    ImGui::InvisibleButton(buttonId.data(),
                           ImVec2(kPrimaryBarHeight, kPrimaryBarHeight));
    const bool hovered = ImGui::IsItemHovered();
    if (hovered) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
    const bool pressed = ImGui::IsItemClicked();
    const bool hasEntries = !state.config.watchlist.empty();
    bool toggle = false;
    bool closeNative = false;
    if (pressed && hasEntries) {
        if (state.navigation.notificationCenterOpen && !nativeLiteMenu)
            CloseAllAnimatedFloatingMenus();
        state.navigation.notificationCenterOpen = false;
        PlayUISound(nativeLiteMenu ? "click.wav" : "transition.wav", state);
        toggle = true;
        if (nativeLiteMenu) {
            closeNative = liteMenuWasOpen;
            state.navigation.watchlistOpen = !liteMenuWasOpen;
        } else {
            state.navigation.watchlistOpen = !state.navigation.watchlistOpen;
        }
    } else if (pressed) {
        state.navigation.watchlistOpen = false;
        PublishUserFeedback(
            state,
            UserFeedbackType::Warning,
            "Watchlist is empty",
            "Add a stock to your watchlist before opening this menu.");
    } else if (!hasEntries) {
        state.navigation.watchlistOpen = false;
    }

    const ImU32 heartColor = ImGui::ColorConvertFloat4ToU32(
        hovered ? ThemeVec(state.config.theme.dangerHover)
                : ThemeVec(state.config.theme.danger));
    DrawIcon(ImGui::GetWindowDrawList(),
             ImVec2(heartMin.x + kPrimaryBarHeight * 0.5f,
                    heartMin.y + kPrimaryBarHeight * 0.5f),
             kPrimaryBarHeight * 0.5f,
             12,
             heartColor);
    DrawObjectFocusOutline(
        state, heartMin, heartMax, hovered || state.navigation.watchlistOpen, 3);

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float width =
        std::clamp(viewport->WorkSize.x * 0.18f, 210.0f, 240.0f);
    const float maxHeight =
        std::max(140.0f, std::min(260.0f, viewport->WorkSize.y - 40.0f));
    std::size_t matchingRows = state.config.watchlist.size();
    if (matchingRows > 6 && state.navigation.watchlistFilterBuffer[0]) {
        const std::string filter = state.navigation.watchlistFilterBuffer;
        matchingRows = static_cast<std::size_t>(std::count_if(
            state.config.watchlist.begin(),
            state.config.watchlist.end(),
            [&](const std::string& ticker) {
                return ticker.find(filter) != std::string::npos;
            }));
    }
    constexpr float inset = 12.0f;
    const float visibleRows = static_cast<float>(
        std::min<std::size_t>(std::max<std::size_t>(matchingRows, 1), 6));
    const float filterHeight = state.config.watchlist.size() > 6 ? 54.0f : 0.0f;
    const float height = std::min(
        filterHeight + 4.0f + visibleRows * kWatchlistRowHeight + inset * 2.0f,
        maxHeight);
    float targetX = heartMax.x - width;
    float targetY = heartMax.y + 4.0f;
    targetX = std::clamp(targetX,
                         viewport->WorkPos.x + 10.0f,
                         viewport->WorkPos.x + viewport->WorkSize.x - width - 10.0f);
    if (targetY + height > viewport->WorkPos.y + viewport->WorkSize.y)
        targetY = heartMin.y - height - 4.0f;
    const ImVec2 menuPosition(targetX, targetY);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, UiRounding(state, 9.0f));
    const ImVec4 background = ThemeVec(state.config.theme.floatingBg);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, background);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, background);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, background);
    const auto renderContents = [&] {
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, UiRounding(state, 10.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
        ImGui::BeginChild("WLChild", ImVec2(width, height), false,
                          ImGuiWindowFlags_NoScrollbar);
        ImGui::SetCursorPos(ImVec2(inset, inset));
        ImGui::BeginChild("WLContent",
                          ImVec2(width - inset * 2.0f, height - inset * 2.0f),
                          false,
                          ImGuiWindowFlags_NoScrollbar);
        RenderWatchlistRows(state, onExecute);
        ImGui::EndChild();
        ImGui::EndChild();
        ImGui::PopStyleVar(2);
        const ImVec2 windowMin = ImGui::GetWindowPos();
        const ImVec2 windowMax(windowMin.x + ImGui::GetWindowWidth(),
                               windowMin.y + ImGui::GetWindowHeight());
        DrawObjectFocusOutline(state, heartMin, heartMax, true, 3);
        DrawObjectFocusOutline(state, windowMin, windowMax, true, 3);
    };
    if (nativeLiteMenu) {
        if (toggle && state.navigation.watchlistOpen)
            ImGui::OpenPopup(menuId.data());
        ImGui::SetNextWindowPos(menuPosition, ImGuiCond_Appearing);
        ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_Always);
        if (ImGui::BeginPopup(menuId.data(),
                              ImGuiWindowFlags_NoMove |
                                  ImGuiWindowFlags_NoResize |
                                  ImGuiWindowFlags_NoSavedSettings)) {
            if (closeNative || !hasEntries)
                ImGui::CloseCurrentPopup();
            else
                renderContents();
            ImGui::EndPopup();
        }
        state.navigation.watchlistOpen = ImGui::IsPopupOpen(menuId.data());
    } else {
        const bool open = hasEntries && BeginAnimatedFloatingMenu(
                                              state,
                                              menuId.data(),
                                              toggle,
                                              menuPosition,
                                              ImVec2(0, 0),
                                              state.UiAnimationsEnabled());
        if (open) {
            renderContents();
            EndAnimatedFloatingMenu();
        } else if (state.navigation.watchlistOpen) {
            state.navigation.watchlistOpen = false;
        }
    }
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar(2);
}

bool QueryIsUiModeCommand(const AppState& state, std::string_view query) {
    if (query.size() > 8)
        return false;
    std::string upper(query);
    UppercaseInPlace(upper);
    return !state.navigation.liteGuiActive && upper == "LITEGUI";
}

void PumpSearchResults(AppState& state, SearchState& searchState) {
    const std::string_view query = searchState.inputBuffer;
    if (query != searchState.lastQuery) {
        searchState.lastQuery.assign(query);
        searchState.pendingQuery = searchState.lastQuery;
        searchState.debounceTimer = 0.07f;
        searchState.selectedIndex = -1;
        searchState.results.clear();
    }
    if (searchState.debounceTimer > 0.0f)
        searchState.debounceTimer -= ImGui::GetIO().DeltaTime;
    if (QueryIsUiModeCommand(state, query)) {
        searchState.results.clear();
        searchState.pendingQuery.clear();
    } else if (searchState.debounceTimer <= 0.0f &&
               !searchState.pendingQuery.empty() && !searchState.isSearching) {
        searchState.inFlightQuery = searchState.pendingQuery;
        searchState.isSearching = true;
        searchState.searchFuture = GetSearchWorkerPool()->Submit(
            [query = searchState.inFlightQuery] {
                auto results = LookupSymbols(query);
                RequestGuiRedraw();
                return results;
            },
            ExecutorPriority::High);
    }
    if (!searchState.isSearching || !searchState.searchFuture.valid() ||
        searchState.searchFuture.wait_for(std::chrono::seconds(0)) !=
            std::future_status::ready) {
        return;
    }
    auto completed = searchState.searchFuture.get();
    searchState.isSearching = false;
    if (searchState.inFlightQuery == searchState.lastQuery) {
        squarestar::search::PersonalizeSymbolSearchResults(
            searchState.inFlightQuery,
            completed,
            state.config.searchHistory,
            state.config.searchHistoryNames,
            state.config.watchlist);
        searchState.results = std::move(completed);
        searchState.pendingQuery.clear();
        RequestGuiRedraw();
    } else if (!searchState.pendingQuery.empty()) {
        searchState.debounceTimer = 0.01f;
    }
}

bool UpdateDropdownState(AppState& state,
                         SearchState& searchState,
                         const SearchInputResult& input) {
    const bool wasOpen = searchState.isDropdownOpen;
    const bool hasInput = searchState.inputBuffer[0] != '\0';
    const ImVec2 pointer = ImGui::GetMousePos();
    const bool overPrevious =
        searchState.dropdownBoundsValid &&
        pointer.x >= searchState.dropdownMin.x &&
        pointer.x <= searchState.dropdownMax.x &&
        pointer.y >= searchState.dropdownMin.y &&
        pointer.y <= searchState.dropdownMax.y;
    const bool clickStarted = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    const bool retainOpen = searchState.isDropdownOpen &&
                            (!clickStarted || input.hovered || overPrevious);
    const bool show =
        (input.activated || searchState.isFocused ||
         searchState.isHoveringDropdown || overPrevious || retainOpen) &&
        (hasInput || !state.config.searchHistory.empty() ||
         state.navigation.liteGuiActive);
    searchState.isDropdownOpen = show;


if (wasOpen != show)
        RequestGuiRedraw();
    if (state.UiAnimationsEnabled()) {
        if (show && searchState.dropdownShowsRecommendations != hasInput) {
            searchState.dropdownShowsRecommendations = hasInput;
            searchState.dropdownAnim = 0.0f;
        }
        const float target = show ? 1.0f : 0.0f;
        const float response = 1.0f - std::exp(-20.0f * UiFrameDelta());
        searchState.dropdownAnim +=
            (target - searchState.dropdownAnim) * std::clamp(response, 0.0f, 1.0f);
        searchState.dropdownAnim =
            std::clamp(searchState.dropdownAnim, 0.0f, 1.0f);
        if (std::abs(searchState.dropdownAnim - target) > 0.001f)
            RequestGuiRedraw();
    } else {
        searchState.dropdownAnim = show ? 1.0f : 0.0f;
    }
    if (show)
        searchState.dropdownShowsRecommendations = hasInput;
    const int itemCount = hasInput
                              ? static_cast<int>(searchState.results.size())
                              : static_cast<int>(state.config.searchHistory.size());
    if (show && itemCount > 0) {
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))
            searchState.selectedIndex = (searchState.selectedIndex + 1) % itemCount;
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
            --searchState.selectedIndex;
            if (searchState.selectedIndex < 0)
                searchState.selectedIndex = itemCount - 1;
        }
    }
    return show || searchState.dropdownAnim > 0.001f;
}

std::string ResolveEnterTarget(const AppState& state,
                               const SearchState& searchState) {
    const std::string_view query = searchState.inputBuffer;
    if (!query.empty()) {
        if (searchState.selectedIndex >= 0 &&
            searchState.selectedIndex < static_cast<int>(searchState.results.size())) {
            return searchState.results[searchState.selectedIndex].first;
        }
        if (!searchState.results.empty() && !searchState.isSearching &&
            searchState.pendingQuery.empty() &&
            searchState.inFlightQuery == query) {
            return searchState.results.front().first;
        }
        return std::string(query);
    }
    if (searchState.selectedIndex >= 0 &&
        searchState.selectedIndex <
            static_cast<int>(state.config.searchHistory.size())) {
        return state.config.searchHistory[searchState.selectedIndex];
    }
    return {};
}

std::string RenderSearchDropdownContents(AppState& state,
                                         SearchState& searchState,
                                         const SearchInputResult& input) {
    std::string selection;
    searchState.isHoveringDropdown = ImGui::IsWindowHovered(
        ImGuiHoveredFlags_AllowWhenBlockedByActiveItem |
        ImGuiHoveredFlags_ChildWindows);
    searchState.dropdownMin = ImGui::GetWindowPos();
    searchState.dropdownMax =
        ImVec2(searchState.dropdownMin.x + ImGui::GetWindowWidth(),
               searchState.dropdownMin.y + ImGui::GetWindowHeight());
    searchState.dropdownBoundsValid = true;

    const bool recommendations = searchState.dropdownShowsRecommendations;
    ImGui::TextDisabled(recommendations ? "Recommended" : "History");
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::Dummy(ImVec2(0, 2));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 2));
    ImGui::PopStyleVar();
    if (recommendations && searchState.isSearching) {
        ImGui::TextDisabled("Searching...");
    } else if (recommendations && searchState.results.empty()) {
        ImGui::TextDisabled("No results found.");
    } else if (!recommendations && state.config.searchHistory.empty()) {
        ImGui::TextDisabled("No recent searches yet.");
    } else {
        const int count = recommendations
                              ? static_cast<int>(searchState.results.size())
                              : static_cast<int>(state.config.searchHistory.size());
        for (int index = 0; index < count; ++index) {
            std::string ticker;
            std::string company;
            if (recommendations) {
                ticker = searchState.results[index].first;
                company = searchState.results[index].second;
            } else {
                ticker = state.config.searchHistory[index];
                const auto name = state.config.searchHistoryNames.find(ticker);
                if (name != state.config.searchHistoryNames.end())
                    company = name->second;
                if (company.empty() || company == "Fetching...") {
                    for (const auto& context : state.marketData.activeContexts) {
                        if (context && context->navigation.ticker == ticker &&
                            context->RawData().success &&
                            context->RawData().companyName != "Fetching...") {
                            company = context->RawData().companyName;
                            state.config.searchHistoryNames[ticker] = company;
                            break;
                        }
                    }
                }
            }
            if (ticker.empty())
                continue;
            std::string label = ticker;
            if (!company.empty() && company != "Fetching...")
                label += "  -  " + company;
            const bool selected = index == searchState.selectedIndex;
            if (ImGui::Selectable(label.c_str(), selected)) {
                selection = ticker;
                break;
            }
            if (selected)
                ImGui::SetScrollHereY(0.5f);
            if (ImGui::IsItemHovered())
                searchState.selectedIndex = index;
        }
    }

    const ImVec2 dropdownMin = ImGui::GetWindowPos();
    const ImVec2 dropdownMax(dropdownMin.x + ImGui::GetWindowWidth(),
                             dropdownMin.y + ImGui::GetWindowHeight());
    DrawObjectFocusOutline(state, input.focusMin, input.focusMax, true, 2);
    DrawObjectFocusOutline(state, dropdownMin, dropdownMax, true, 2);
    return selection;
}

std::string RenderSearchDropdown(AppState& state,
                                 SearchState& searchState,
                                 const char* idSuffix,
                                 const SearchGeometry& geometry,
                                 const SearchInputResult& input,
                                 bool fixedDropdownHeight,
                                 bool detachedDropdown,
                                 bool showDropdown) {
    ImVec2 position(geometry.searchMin.x, geometry.searchMax.y + 4.0f);
    position.y += (1.0f - searchState.dropdownAnim) * 8.0f;
    ImGuiViewport* viewport = ImGui::GetWindowViewport();
    if (!viewport)
        viewport = ImGui::GetMainViewport();

    bool useDetachedViewport = false;
#ifdef IMGUI_HAS_VIEWPORT
    useDetachedViewport =
        detachedDropdown &&
        (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0;
#endif
    const float maxHeight =
        useDetachedViewport
            ? FLT_MAX
            : fixedDropdownHeight
            ? 300.0f
            : std::max(64.0f,
                       viewport->WorkPos.y + viewport->WorkSize.y - position.y - 12.0f);
    if (!fixedDropdownHeight && !useDetachedViewport)
        position = ClampPopupPosition(
            viewport, position, ImVec2(geometry.boxWidth, maxHeight));
    ImGui::SetNextWindowPos(position, ImGuiCond_Always);
    ImGui::SetNextWindowSizeConstraints(ImVec2(geometry.boxWidth, 0.0f),
                                        ImVec2(geometry.boxWidth, maxHeight));
#ifdef IMGUI_HAS_VIEWPORT
    ImGuiWindowClass detachedClass;
    if (useDetachedViewport) {
        detachedClass.ParentViewportId = ImGui::GetMainViewport()->ID;
        detachedClass.ViewportFlagsOverrideSet =
            ImGuiViewportFlags_NoAutoMerge |
            ImGuiViewportFlags_NoDecoration |
            ImGuiViewportFlags_NoTaskBarIcon;
        ImGui::SetNextWindowClass(&detachedClass);
    }
#endif
    const ImVec4 background = ThemeVec(state.config.theme.floatingBg);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, background);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, background);
    ImGui::PushStyleColor(
        ImGuiCol_Border, ThemeVec(state.config.theme.floatingBorder));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, UiRounding(state, 8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 10.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, searchState.dropdownAnim);
    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing;
    if (useDetachedViewport)
        flags |= ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    if (!showDropdown)
        flags |= ImGuiWindowFlags_NoInputs;

    std::string selection;
    const std::string windowId = "##Dropdown_" + std::string(idSuffix);
    if (ImGui::Begin(windowId.c_str(), nullptr, flags))
        selection = RenderSearchDropdownContents(state, searchState, input);
    ImGui::End();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(3);
    return selection;
}

std::string RenderContainedSearchDropdown(AppState& state,
                                          SearchState& searchState,
                                          const char* idSuffix,
                                          const SearchGeometry& geometry,
                                          const SearchInputResult& input,
                                          bool showDropdown) {
    ImVec2 position(geometry.searchMin.x, geometry.searchMax.y + 4.0f);
    position.y += (1.0f - searchState.dropdownAnim) * 8.0f;


const float ownerBottom =
        ImGui::GetWindowPos().y + ImGui::GetWindowHeight() -
        ImGui::GetStyle().WindowPadding.y;
    const float dropdownHeight = std::max(0.0f, ownerBottom - position.y);
    if (dropdownHeight < 48.0f) {
        searchState.isHoveringDropdown = false;
        searchState.dropdownBoundsValid = false;
        return {};
    }

    ImGui::SetCursorScreenPos(position);
    const ImVec4 background = ThemeVec(state.config.theme.floatingBg);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, background);
    ImGui::PushStyleColor(
        ImGuiCol_Border, ThemeVec(state.config.theme.floatingBorder));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, UiRounding(state, 8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 10.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, searchState.dropdownAnim);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoSavedSettings;
    if (!showDropdown)
        flags |= ImGuiWindowFlags_NoInputs;

    std::string selection;
    const std::string childId = "##ContainedDropdown_" + std::string(idSuffix);
    if (ImGui::BeginChild(childId.c_str(),
                          ImVec2(geometry.boxWidth, dropdownHeight),
                          ImGuiChildFlags_Borders |
                              ImGuiChildFlags_AlwaysUseWindowPadding,
                          flags)) {
        selection = RenderSearchDropdownContents(state, searchState, input);
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor(2);
    return selection;
}

void ResetAfterExecute(SearchState& searchState) {
    searchState.ResetUi();
    ImGui::SetWindowFocus(nullptr);
}

}

void RenderIntegratedSearchBar(
    AppState& state,
    SearchState& searchState,
    const char* idSuffix,
    float customWidth,
    const std::function<void(const std::string&)>& onExecute,
    SearchBarOptions options) {
    const float barWidth = customWidth > 0.0f
                               ? customWidth
                               : ImGui::GetContentRegionAvail().x;
    SearchGeometry geometry;
    const float accessoryWidth =
        kPrimaryBarHeight + kHeartSpacing +
        (options.showNotificationCenter ? kPrimaryBarHeight + kAuxiliaryButtonSpacing : 0.0f);
    geometry.boxWidth = std::max(1.0f, barWidth - accessoryWidth);
    geometry.searchMin = ImGui::GetCursorScreenPos();
    geometry.searchMax = ImVec2(geometry.searchMin.x + geometry.boxWidth,
                                geometry.searchMin.y + kPrimaryBarHeight);

    const SearchInputResult input =
        RenderSearchInput(state, searchState, idSuffix, geometry);
    RenderWatchlistMenu(state, idSuffix, onExecute);
    if (options.showNotificationCenter) {
        RenderNotificationCenterMenu(state,
                                     idSuffix,
                                     kPrimaryBarHeight,
                                     kAuxiliaryButtonSpacing,
                                     options.notificationCenterVisibleCardLimit,
                                     onExecute);
    } else {
        state.navigation.notificationCenterOpen = false;
        state.render.notifications.notificationCenter.panelAnimation = 0.0f;
    }
    PumpSearchResults(state, searchState);
    const bool renderDropdown =
        UpdateDropdownState(state, searchState, input);

    if (input.enterPressed &&
        (searchState.inputBuffer[0] != '\0' || searchState.selectedIndex >= 0)) {
        const std::string target = ResolveEnterTarget(state, searchState);
        if (!target.empty()) {
            if (!ExecuteUiModeCommand(state, target))
                onExecute(target);
            ResetAfterExecute(searchState);
            return;
        }
    }

    if (renderDropdown) {
        const std::string selection = options.containDropdownInParent
                                          ? RenderContainedSearchDropdown(
                                                state,
                                                searchState,
                                                idSuffix,
                                                geometry,
                                                input,
                                                searchState.isDropdownOpen)
                                          : RenderSearchDropdown(
                                                state,
                                                searchState,
                                                idSuffix,
                                                geometry,
                                                input,
                                                options.fixedDropdownHeight,
                                                options.detachedDropdown,
                                                searchState.isDropdownOpen);
        if (!selection.empty()) {
            onExecute(selection);
            ResetAfterExecute(searchState);
            RequestGuiRedraw();
        }
    } else {
        searchState.isHoveringDropdown = false;
        searchState.dropdownBoundsValid = false;
        searchState.dropdownShowsRecommendations = false;
    }
}

}
