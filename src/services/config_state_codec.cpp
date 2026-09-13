#include "services/config_state_codec.hpp"

#include "application/alert_service.hpp"
#include "application/app_config.hpp"
#include "application/app_limits.hpp"
#include "application/app_navigation.hpp"
#include "application/frame_rate.hpp"
#include "application/key_bindings.hpp"
#include "application/persisted_state.hpp"
#include "application/theme_profiles.hpp"
#include "domain/chart_ranges.hpp"
#include "domain/json_text.hpp"
#include "domain/market_symbol.hpp"
#include "domain/text.hpp"
#include "domain/world_clock_zones.hpp"
#include "services/json_access.hpp"
#include "services/secret_protection.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace squarestar::config {

using squarestar::alerts::AlertService;
using squarestar::application::AppConfig;
using squarestar::application::AppNavigation;
using squarestar::application::KeyBind;
using squarestar::application::NormalizeGuiFrameRateMode;
using squarestar::application::SavedGuiStockTab;
using squarestar::application::SetThemePreset;
using squarestar::application::SidebarTabFromPersistedValue;
using squarestar::application::SidebarTabIndex;
using squarestar::application::TerminalAction;
using squarestar::json::JsonBool;
using squarestar::json::JsonInt;
using squarestar::json::JsonNumber;
using squarestar::json::JsonString;
using squarestar::json::ParseJsonInSitu;
using squarestar::market::MarketSymbol;
using squarestar::secrets::ScopedSecureClear;
using squarestar::text::EscapeJsonStringValue;
using squarestar::text::ParseIntOr;
using YyjsonDoc = squarestar::json::Document;

namespace {

void LoadSymbolArray(yyjson_val* object,
                     const char* key,
                     std::size_t limit,
                     std::vector<std::string>& destination) {
    yyjson_val* array = yyjson_obj_get(object, key);
    if (!array || !yyjson_is_arr(array))
        return;
    destination.clear();
    size_t index, count;
    yyjson_val* item;
    yyjson_arr_foreach(array, index, count, item) {
        if (destination.size() >= limit)
            break;
        if (!yyjson_is_str(item))
            continue;
        const auto symbol = MarketSymbol::Parse(
            std::string_view(yyjson_get_str(item), yyjson_get_len(item)));
        if (!symbol)
            continue;
        std::string canonical = symbol->Canonical();
        if (std::find(destination.begin(), destination.end(), canonical) == destination.end())
            destination.push_back(std::move(canonical));
    }
}

bool DecodePrivateFields(yyjson_val* obj,
                         squarestar::application::PersistedStateView persisted,
                         std::int64_t nowEpoch) {
    AppConfig& state = persisted.config;
    AppNavigation& navigation = persisted.navigation;
    AlertService& alerts = persisted.alerts;
    bool foundAny = false;

    std::string exportDirectory;
    if (JsonString(obj, "exportDirectory", exportDirectory)) {
        foundAny = true;
        if (!exportDirectory.empty()) {
            std::snprintf(state.exportDirectory,
                          sizeof(state.exportDirectory),
                          "%s",
                          exportDirectory.c_str());
        }
    }

    if (yyjson_val* alertObject = yyjson_obj_get(obj, "priceAlerts");
        alertObject && yyjson_is_obj(alertObject)) {
        foundAny = true;
        alerts.ClearThresholds();
        yyjson_obj_iter alertIter = yyjson_obj_iter_with(alertObject);
        yyjson_val* key;
        while ((key = yyjson_obj_iter_next(&alertIter))) {
            if (alerts.Thresholds().size() >= squarestar::application::kMaxPriceAlerts)
                break;
            const char* ticker = yyjson_get_str(key);
            yyjson_val* value = yyjson_obj_iter_get_val(key);
            if (!ticker || !*ticker || !value || !yyjson_is_num(value))
                continue;
            const double alertPrice = yyjson_get_num(value);
            const auto symbol = MarketSymbol::Parse(ticker);
            if (symbol && std::isfinite(alertPrice) && alertPrice > 0.0 &&
                alertPrice <= 1.0e9) {
                (void)alerts.SetThreshold(symbol->Canonical(), alertPrice);
            }
        }
    }

    if (yyjson_val* muted = yyjson_obj_get(obj, "priceAlertMutedUntil");
        muted && yyjson_is_obj(muted)) {
        foundAny = true;
        alerts.ClearMutesAndSilences();
        yyjson_obj_iter mutedIter = yyjson_obj_iter_with(muted);
        yyjson_val* key;
        while ((key = yyjson_obj_iter_next(&mutedIter))) {
            const char* ticker = yyjson_get_str(key);
            yyjson_val* value = yyjson_obj_iter_get_val(key);
            if (!ticker || !*ticker || !value || !yyjson_is_int(value))
                continue;
            const int64_t expiry = yyjson_get_sint(value);
            const auto symbol = MarketSymbol::Parse(ticker);
            if (symbol && expiry > nowEpoch && alerts.HasThreshold(symbol->Canonical()))
                alerts.SilenceUntil(symbol->Canonical(), expiry);
        }
    }

    if (yyjson_obj_get(obj, "watchlist")) {
        foundAny = true;
        LoadSymbolArray(obj,
                        "watchlist",
                        squarestar::application::kMaxWatchlistItems,
                        state.watchlist);
    }
    if (yyjson_obj_get(obj, "searchHistory")) {
        foundAny = true;
        LoadSymbolArray(obj, "searchHistory", 8, state.searchHistory);
    }

    std::string savedGuiLastActiveTab;
    if (JsonString(obj, "savedGuiLastActiveTab", savedGuiLastActiveTab)) {
        foundAny = true;
        if (const auto symbol = MarketSymbol::Parse(savedGuiLastActiveTab))
            navigation.lastActiveTab = symbol->Canonical();
        else
            navigation.lastActiveTab.clear();
    }

    if (yyjson_val* savedTabs = yyjson_obj_get(obj, "savedGuiTabs");
        savedTabs && yyjson_is_arr(savedTabs)) {
        foundAny = true;
        navigation.guiWorkspace.stockTabs.clear();
        size_t idx, max;
        yyjson_val* item;
        yyjson_arr_foreach(savedTabs, idx, max, item) {
            if (!yyjson_is_obj(item) ||
                navigation.guiWorkspace.stockTabs.size() >=
                    squarestar::application::kMaxActiveStockTabs) {
                continue;
            }
            std::string ticker;
            if (!JsonString(item, "ticker", ticker))
                continue;
            const auto symbol = MarketSymbol::Parse(ticker);
            if (!symbol)
                continue;
            int64_t range = 0;
            int64_t upperTab = 0;
            bool monitorExcluded = false;
            JsonInt(item, "range", range);
            JsonInt(item, "upperTab", upperTab);
            JsonBool(item, "monitorExcluded", monitorExcluded);
            const std::string canonical = symbol->Canonical();
            const bool duplicate = std::any_of(
                navigation.guiWorkspace.stockTabs.begin(),
                navigation.guiWorkspace.stockTabs.end(),
                [&](const SavedGuiStockTab& existing) {
                    return existing.ticker == canonical;
                });
            if (!duplicate) {
                navigation.guiWorkspace.stockTabs.push_back(
                    {canonical,
                     std::clamp((int)range,
                                0,
                                squarestar::market::TIME_RANGE_COUNT - 1),
                     std::clamp((int)upperTab, 0, 3),
                     monitorExcluded});
            }
        }
    }

    if (!state.saveSearchHistory) {
        state.searchHistory.clear();
        state.searchHistoryNames.clear();
    }
    return foundAny;
}

} // namespace

std::string EncodePrivateConfigState(
    squarestar::application::PersistedStateConstView persisted,
    std::int64_t nowEpoch,
    std::string_view imguiLayout) {
    const AppConfig& state = persisted.config;
    const AppNavigation& navigation = persisted.navigation;
    const AlertService& alerts = persisted.alerts;
    std::ostringstream file;
    file << std::boolalpha;
    const auto stringField = [&](std::string_view key,
                                 std::string_view value,
                                 bool trailingComma = true) {
        file << "  \"" << key << "\": \"" << EscapeJsonStringValue(value) << "\"";
        if (trailingComma)
            file << ",";
        file << "\n";
    };

    file << "{\n";
    file << "  \"version\": 1,\n";
    stringField("exportDirectory", state.exportDirectory);
    stringField("imguiLayout", imguiLayout);

    const auto& thresholds = alerts.Thresholds();
    const size_t validAlertCount = (size_t)std::count_if(
        thresholds.begin(), thresholds.end(), [](const auto& entry) {
            return std::isfinite(entry.second) && entry.second > 0.0;
        });
    file << "  \"priceAlerts\": {";
    if (validAlertCount > 0)
        file << "\n";
    size_t alertIndex = 0;
    for (const auto& [ticker, alertPrice] : thresholds) {
        if (!std::isfinite(alertPrice) || alertPrice <= 0.0)
            continue;
        file << "    \"" << EscapeJsonStringValue(ticker) << "\": " << alertPrice;
        if (++alertIndex < validAlertCount)
            file << ",";
        file << "\n";
    }
    if (validAlertCount > 0)
        file << "  ";
    file << "},\n";

    const auto& mutes = alerts.Mutes();
    const size_t validMuteCount = (size_t)std::count_if(
        mutes.begin(), mutes.end(), [&](const auto& entry) {
            return entry.second > nowEpoch && alerts.HasThreshold(entry.first);
        });
    file << "  \"priceAlertMutedUntil\": {";
    if (validMuteCount > 0)
        file << "\n";
    size_t muteIndex = 0;
    for (const auto& [ticker, expiry] : mutes) {
        if (expiry <= nowEpoch || !alerts.HasThreshold(ticker))
            continue;
        file << "    \"" << EscapeJsonStringValue(ticker) << "\": " << expiry;
        if (++muteIndex < validMuteCount)
            file << ",";
        file << "\n";
    }
    if (validMuteCount > 0)
        file << "  ";
    file << "},\n";

    file << "  \"watchlist\": [";
    for (size_t i = 0; i < state.watchlist.size(); ++i) {
        file << "\"" << EscapeJsonStringValue(state.watchlist[i]) << "\"";
        if (i + 1 < state.watchlist.size())
            file << ", ";
    }
    file << "],\n";

    file << "  \"searchHistory\": [";
    if (state.saveSearchHistory) {
        for (size_t i = 0; i < state.searchHistory.size(); ++i) {
            file << "\"" << EscapeJsonStringValue(state.searchHistory[i]) << "\"";
            if (i + 1 < state.searchHistory.size())
                file << ", ";
        }
    }
    file << "],\n";

    const std::string& savedGuiLastActiveTab =
        navigation.liteGuiActive ? navigation.guiWorkspace.lastActiveTab
                                 : navigation.lastActiveTab;
    stringField("savedGuiLastActiveTab", savedGuiLastActiveTab);
    file << "  \"savedGuiTabs\": [";
    if (!navigation.guiWorkspace.stockTabs.empty())
        file << "\n";
    for (size_t i = 0; i < navigation.guiWorkspace.stockTabs.size(); ++i) {
        const SavedGuiStockTab& tab = navigation.guiWorkspace.stockTabs[i];
        file << "    {\"ticker\": \"" << EscapeJsonStringValue(tab.ticker)
             << "\", \"range\": " << tab.selectedTimeRangeIndex
             << ", \"upperTab\": " << tab.upperTabIndex
             << ", \"monitorExcluded\": " << tab.monitorExcluded << "}";
        if (i + 1 < navigation.guiWorkspace.stockTabs.size())
            file << ",";
        file << "\n";
    }
    if (!navigation.guiWorkspace.stockTabs.empty())
        file << "  ";
    file << "]\n";
    file << "}\n";
    return file.str();
}

bool DecodePrivateConfigState(
    std::string json,
    squarestar::application::PersistedStateView persisted,
    std::int64_t nowEpoch,
    std::string& imguiLayout) {
    ScopedSecureClear clearJson(json);
    YyjsonDoc document = ParseJsonInSitu(json);
    yyjson_val* obj = document ? yyjson_doc_get_root(document.get()) : nullptr;
    if (!obj || !yyjson_is_obj(obj))
        return false;
    int64_t version = 0;
    if (!JsonInt(obj, "version", version) || version != 1)
        return false;
    if (!JsonString(obj, "imguiLayout", imguiLayout))
        return false;
    return DecodePrivateFields(obj, persisted, nowEpoch);
}

std::string EncodeConfigState(
    squarestar::application::PersistedStateConstView persisted,
    std::string_view protectedApiKey,
    std::string_view protectedPrivateState) {
    const AppConfig& state = persisted.config;
    const AppNavigation& navigation = persisted.navigation;
    std::ostringstream file;
    file << std::boolalpha;
    const auto field = [&](std::string_view key, const auto& value) {
        file << "  \"" << key << "\": " << value << ",\n";
    };
    const auto stringField = [&](std::string_view key, std::string_view value) {
        file << "  \"" << key << "\": \"" << EscapeJsonStringValue(value) << "\",\n";
    };
    file << "{\n";
    field("version", 1);
    stringField("apiKeyProtected", protectedApiKey);
    stringField("privateStateProtected", protectedPrivateState);
    field("sound", state.soundEnabled);
    field("anim", state.animEnabled);
    field("zeroGraphics", state.zeroGraphics);
    field("themeMode", state.themeModeIndex);
    field("lastOpenMode", state.lastOpenMode);
    field("liteGuiHintShown", state.liteGuiHintShown);
    field("monitorModeHintDisabled", state.monitorModeHintDisabled);
    field("keybindReminders", state.keybindReminders);
    field("saveLastUsedUiData", state.saveLastUsedUiData);
    field("saveSearchHistory", state.saveSearchHistory);
    field("askBeforeInterfaceSwitch", state.askBeforeInterfaceSwitch);
    field("marketMoveNotifications", state.marketMoveNotifications);
    field("marketMoveThresholdPct", state.marketMoveThresholdPct);
    field("marketMoveCooldownMinutes", state.marketMoveCooldownMinutes);
    field("marketMove52WeekEvents", state.marketMove52WeekEvents);
    field("marketMoveStateChanges", state.marketMoveStateChanges);
    field("marketMoveBatching", state.marketMoveBatching);
    field("fpsMode", state.fpsMode);
    field("graphTimeZone", state.graphTimeZone);
    field("chartXAxisMode", state.chartXAxisMode);
    field("plotLineType", state.plotLineType);
    field("crosshairMode", state.crosshairMode);
    const std::vector<int>& persistedWorldClocks = state.activeWorldClocks;
    file << "  \"activeWorldClocks\": [";
    for (size_t i = 0; i < persistedWorldClocks.size(); ++i) {
        file << persistedWorldClocks[i];
        if (i + 1 < persistedWorldClocks.size())
            file << ", ";
    }
    file << "],\n";
    field("objectFocus", state.objectFocus);
    field("stockTabBarHidden", state.stockTabBarHidden);

    const int savedGuiSidebarTab = SidebarTabIndex(
        navigation.liteGuiActive ? navigation.guiWorkspace.activeSidebarTab
                                 : navigation.activeSidebarTab);
    field("savedGuiSidebarTab", savedGuiSidebarTab);

    file << "  \"keybinds\": {\n";
    auto it = state.KeyBinds.begin();
    while (it != state.KeyBinds.end()) {
        file << "    \"" << static_cast<int>(it->first) << "\": {\n";
        file << "      \"key\": " << static_cast<int>(it->second.key) << ",\n";
        file << "      \"ctrl\": " << it->second.ctrl << ",\n";
        file << "      \"shift\": " << it->second.shift << ",\n";
        file << "      \"alt\": " << it->second.alt << "\n";
        auto next = it;
        ++next;
        file << "    }" << (next != state.KeyBinds.end() ? "," : "") << "\n";
        it = next;
    }
    file << "  }\n";
    file << "}\n";
    return file.str();
}

ConfigDecodeResult DecodeConfigState(
    std::string json,
    squarestar::application::PersistedStateView persisted) {
    AppConfig& state = persisted.config;
    AppNavigation& navigation = persisted.navigation;
    YyjsonDoc document = ParseJsonInSitu(json);
    yyjson_val* obj = document ? yyjson_doc_get_root(document.get()) : nullptr;
    if (!obj || !yyjson_is_obj(obj))
        return {};

    int64_t version = 0;
    if (!JsonInt(obj, "version", version) || version != 1)
        return {};

    ConfigDecodeResult decoded;
    decoded.hasProtectedApiKey =
        JsonString(obj, "apiKeyProtected", decoded.protectedApiKey);
    decoded.hasProtectedPrivateState =
        JsonString(obj, "privateStateProtected", decoded.protectedPrivateState);
    if (!decoded.hasProtectedApiKey || !decoded.hasProtectedPrivateState)
        return {};
    decoded.parsed = true;
    JsonBool(obj, "sound", state.soundEnabled);
    JsonBool(obj, "anim", state.animEnabled);
    JsonBool(obj, "zeroGraphics", state.zeroGraphics);
    int64_t iVal = 0;
    if (JsonInt(obj, "themeMode", iVal))
        state.themeModeIndex = ((int)iVal == 1 ? 1 : 0);
    if (JsonInt(obj, "lastOpenMode", iVal))
        state.lastOpenMode = ((int)iVal == 2 ? 2 : 0);
    JsonBool(obj, "liteGuiHintShown", state.liteGuiHintShown);
    JsonBool(obj, "monitorModeHintDisabled", state.monitorModeHintDisabled);
    JsonBool(obj, "keybindReminders", state.keybindReminders);
    JsonBool(obj, "saveLastUsedUiData", state.saveLastUsedUiData);
    JsonBool(obj, "saveSearchHistory", state.saveSearchHistory);
    JsonBool(obj, "askBeforeInterfaceSwitch", state.askBeforeInterfaceSwitch);
    JsonBool(obj, "marketMoveNotifications", state.marketMoveNotifications);
    double dVal = 0.0;
    if (JsonNumber(obj, "marketMoveThresholdPct", dVal) && std::isfinite(dVal))
        state.marketMoveThresholdPct = std::clamp(dVal, 0.0, 10.0);
    if (JsonInt(obj, "marketMoveCooldownMinutes", iVal)) {
        state.marketMoveCooldownMinutes =
            static_cast<int>(std::clamp(iVal, int64_t{0}, int64_t{60}));
    }
    JsonBool(obj, "marketMove52WeekEvents", state.marketMove52WeekEvents);
    JsonBool(obj, "marketMoveStateChanges", state.marketMoveStateChanges);
    JsonBool(obj, "marketMoveBatching", state.marketMoveBatching);
    if (JsonInt(obj, "fpsMode", iVal))
        state.fpsMode = NormalizeGuiFrameRateMode((int)iVal);
    if (JsonInt(obj, "graphTimeZone", iVal))
        state.graphTimeZone = std::clamp((int)iVal, 0, 1);
    if (JsonInt(obj, "chartXAxisMode", iVal))
        state.chartXAxisMode = std::clamp((int)iVal, 0, 2);
    if (JsonInt(obj, "plotLineType", iVal))
        state.plotLineType = std::clamp((int)iVal, 0, 1);
    if (JsonInt(obj, "crosshairMode", iVal))
        state.crosshairMode = std::clamp((int)iVal, 0, 2);
    yyjson_val* clockArr = yyjson_obj_get(obj, "activeWorldClocks");
    if (clockArr && yyjson_is_arr(clockArr)) {
        state.activeWorldClocks.clear();
        size_t index, count;
        yyjson_val* item;
        yyjson_arr_foreach(clockArr, index, count, item) {
            if (!yyjson_is_int(item))
                continue;
            int zoneIndex = (int)yyjson_get_sint(item);
            if (zoneIndex >= 0 && zoneIndex < squarestar::market::WORLD_ZONE_COUNT &&
                std::find(state.activeWorldClocks.begin(),
                          state.activeWorldClocks.end(),
                          zoneIndex) == state.activeWorldClocks.end()) {
                state.activeWorldClocks.push_back(zoneIndex);
                if (state.activeWorldClocks.size() >= 5)
                    break;
            }
        }
    }
    JsonBool(obj, "objectFocus", state.objectFocus);
    JsonBool(obj, "stockTabBarHidden", state.stockTabBarHidden);


    if (JsonInt(obj, "savedGuiSidebarTab", iVal))
        navigation.activeSidebarTab =
            SidebarTabFromPersistedValue(std::clamp((int)iVal, 0, 2));

    yyjson_val* keybindsObj = yyjson_obj_get(obj, "keybinds");
    if (keybindsObj && yyjson_is_obj(keybindsObj)) {
        yyjson_obj_iter iter = yyjson_obj_iter_with(keybindsObj);
        yyjson_val* key;
        while ((key = yyjson_obj_iter_next(&iter))) {
            const char* keyText = yyjson_get_str(key);
            yyjson_val* bindData = yyjson_obj_iter_get_val(key);
            if (!keyText || !bindData || !yyjson_is_obj(bindData))
                continue;
            const int actionInt = ParseIntOr(keyText, -1);
            if (actionInt < 0 || actionInt >= static_cast<int>(TerminalAction::Count))
                continue;
            KeyBind& bind = state.KeyBinds[static_cast<TerminalAction>(actionInt)];
            int64_t keycode = 0;
            bool value = false;
            if (JsonInt(bindData, "key", keycode) &&
                (keycode == static_cast<int64_t>(ImGuiKey_None) ||
                 (keycode >= static_cast<int64_t>(ImGuiKey_NamedKey_BEGIN) &&
                  keycode < static_cast<int64_t>(ImGuiKey_NamedKey_END)))) {
                bind.key = static_cast<ImGuiKey>(keycode);
            }
            if (JsonBool(bindData, "ctrl", value))
                bind.ctrl = value;
            if (JsonBool(bindData, "shift", value))
                bind.shift = value;
            if (JsonBool(bindData, "alt", value))
                bind.alt = value;
        }
    }
    SetThemePreset(state, state.themeModeIndex);
    return decoded;
}

} // namespace squarestar::config
