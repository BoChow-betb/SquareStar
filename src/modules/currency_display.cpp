#include "modules/currency_display.hpp"

#include "application/app_state.hpp"
#include "application/main_loop_signal.hpp"
#include "application/user_feedback.hpp"
#include "domain/currency_conversion.hpp"
#include "modules/core.hpp"
#include "services/config_save_queue.hpp"
#include "services/http_client.hpp"
#include "services/network_runtime.hpp"
#include "services/provider_payload_parser.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <exception>
#include <future>
#include <limits>
#include <string>
#include <utility>

namespace squarestar::shell {
namespace {

using squarestar::application::AppState;
using squarestar::application::RequestGuiRedraw;
using squarestar::application::UserFeedbackType;
using squarestar::http::HttpErrorUserMessage;
using squarestar::market::CurrencyRateResult;
using squarestar::market::DisplayCurrencyOption;
using squarestar::market::FindDisplayCurrency;
using squarestar::market::IsSupportedDisplayCurrency;
using squarestar::market::kDisplayCurrencies;
using squarestar::providers::ParseYahooFxRatePayload;
using squarestar::providers::ParseYahooQuoteBatchPayload;

constexpr auto kCurrencyRateRefreshInterval = std::chrono::seconds(60);

struct CurrencyPickerUiState {
    bool openRequested = false;
    ImVec2 anchorMin{};
    ImVec2 anchorMax{};
    ImGuiID viewportId = 0;
};

CurrencyPickerUiState g_CurrencyPicker;

std::future<CurrencyRateResult> ReadyCurrencyFailure(std::string currency,
                                                     std::string message) {
    std::promise<CurrencyRateResult> promise;
    auto future = promise.get_future();
    CurrencyRateResult result;
    result.currency = std::move(currency);
    result.errorMessage = std::move(message);
    promise.set_value(std::move(result));
    return future;
}

std::future<CurrencyRateResult> QueueYahooCurrencyRate(std::string currency) {
    const DisplayCurrencyOption* option = FindDisplayCurrency(currency);
    const std::string symbol = option ? std::string(option->yahooUsdSymbol) : std::string{};
    auto pool = GetBackgroundWorkerPool();
    if (!pool)
        return ReadyCurrencyFailure(std::move(currency),
                                    "Market-data worker unavailable");

    try {
        return pool->Submit(
            [currency, symbol]() mutable {
                CurrencyRateResult result;
                result.currency = currency;
                const auto finish = [](CurrencyRateResult value) {
                    RequestGuiRedraw();
                    return value;
                };
                if (symbol.empty()) {
                    result.errorMessage = "Unsupported display currency";
                    return finish(std::move(result));
                }

                squarestar::http::HttpError lastError =
                    squarestar::http::HttpError::None;
                long lastStatus = 0;

                // Prefer Yahoo's quote endpoint because its regularMarketPrice
                // is the same quote field used by the stock header. This is the
                // closest basis to the number users compare against on Yahoo.
                try {
                    std::string quoteUrl =
                        "https://query1.finance.yahoo.com/v7/finance/quote?symbols=" +
                        symbol;
                    squarestar::http::HttpResponse quoteResponse =
                        QueueYahooAuthenticatedGet(std::move(quoteUrl)).get();
                    lastError = quoteResponse.error;
                    lastStatus = quoteResponse.statusCode;
                    result.rateLimited =
                        result.rateLimited || quoteResponse.statusCode == 429;
                    if (quoteResponse.IsSuccess()) {
                        auto quotes =
                            ParseYahooQuoteBatchPayload(std::move(quoteResponse.body));
                        for (const auto& quote : quotes) {
                            if (quote.symbol != symbol ||
                                !std::isfinite(quote.currentPrice) ||
                                quote.currentPrice <= 0.0) {
                                continue;
                            }
                            result.usdToCurrency = quote.currentPrice;
                            result.timestamp = quote.timestamp;
                            result.success = true;
                            result.errorMessage.clear();
                            return finish(std::move(result));
                        }
                    }
                } catch (const std::exception& error) {
                    result.errorMessage = error.what();
                } catch (...) {
                    result.errorMessage = "Yahoo currency quote request failed";
                }

                // Public chart metadata is a Yahoo-only fallback when the
                // cookie/crumb quote session is unavailable.
                for (const char* host : {"query1.finance.yahoo.com",
                                         "query2.finance.yahoo.com"}) {
                    const std::string url =
                        std::string("https://") + host + "/v8/finance/chart/" +
                        symbol +
                        "?interval=1m&range=1d&includePrePost=false&events=none";
                    squarestar::http::HttpResponse response;
                    try {
                        response = QueueRealtimeHttpGet(url).get();
                    } catch (const std::exception& error) {
                        result.errorMessage = error.what();
                        continue;
                    } catch (...) {
                        result.errorMessage = "Yahoo currency request failed";
                        continue;
                    }
                    lastError = response.error;
                    lastStatus = response.statusCode;
                    result.rateLimited =
                        result.rateLimited || response.statusCode == 429;
                    if (!response.IsSuccess())
                        continue;

                    const auto quote =
                        ParseYahooFxRatePayload(std::move(response.body));
                    if (!quote || !std::isfinite(quote->rate) ||
                        quote->rate <= 0.0) {
                        continue;
                    }

                    result.usdToCurrency = quote->rate;
                    result.timestamp = quote->timestamp;
                    result.success = true;
                    result.errorMessage.clear();
                    return finish(std::move(result));
                }

                if (result.rateLimited) {
                    result.errorMessage = "Yahoo rate limit reached";
                } else if (lastStatus >= 500) {
                    result.errorMessage =
                        "Yahoo currency data is temporarily unavailable";
                } else if (lastError != squarestar::http::HttpError::None) {
                    result.errorMessage = HttpErrorUserMessage(lastError);
                } else if (result.errorMessage.empty()) {
                    result.errorMessage =
                        "Yahoo did not return a usable FX quote";
                }
                return finish(std::move(result));
            },
            ExecutorPriority::High);
    } catch (const std::exception& error) {
        return ReadyCurrencyFailure(std::move(currency), error.what());
    } catch (...) {
        return ReadyCurrencyFailure(std::move(currency),
                                    "Market-data worker rejected the request");
    }
}

void BeginCurrencyRequest(AppState& state,
                          std::string_view currency,
                          bool switchingCurrency) {
    const DisplayCurrencyOption* option = FindDisplayCurrency(currency);
    if (!option || option->code == "USD")
        return;

    auto& runtime = state.marketData.currencyDisplay;
    runtime.pendingRequest = QueueYahooCurrencyRate(std::string(option->code));
    runtime.pendingCurrency = std::string(option->code);
    runtime.requestPending = true;
    runtime.switchingCurrency = switchingCurrency;
    runtime.lastAttemptAt = std::chrono::steady_clock::now();
    RequestGuiRedraw();
}

void SelectDisplayCurrency(AppState& state, std::string_view currency) {
    if (!IsSupportedDisplayCurrency(currency))
        return;

    auto& runtime = state.marketData.currencyDisplay;
    runtime.initialized = true;

    if (currency == "USD") {
        // Abandon any in-flight selection. The background transfer may finish,
        // but dropping this future makes its result unable to mutate UI state.
        runtime.pendingRequest = {};
        runtime.requestPending = false;
        runtime.switchingCurrency = false;
        runtime.pendingCurrency.clear();
        runtime.activeCurrency = "USD";
        runtime.usdToActive = 1.0;
        runtime.rateTimestamp = 0;
        runtime.lastAttemptAt = std::chrono::steady_clock::now();
        if (state.config.displayCurrency != "USD") {
            state.config.displayCurrency = "USD";
            squarestar::config::RequestConfigSave();
        }
        RequestGuiRedraw();
        return;
    }

    if (!runtime.requestPending && runtime.activeCurrency == currency) {
        if (state.config.displayCurrency != currency) {
            state.config.displayCurrency = std::string(currency);
            squarestar::config::RequestConfigSave();
        }
        return;
    }
    if (runtime.requestPending && runtime.switchingCurrency &&
        runtime.pendingCurrency == currency) {
        return;
    }

    // A newly selected unit must never display a stale USD number under the new
    // label. Mark this as a switch so rendering waits for the matching FX quote.
    runtime.pendingRequest = {};
    runtime.requestPending = false;
    BeginCurrencyRequest(state, currency, true);
}

void CompleteCurrencyRequest(AppState& state) {
    auto& runtime = state.marketData.currencyDisplay;
    if (!runtime.requestPending || !runtime.pendingRequest.valid() ||
        runtime.pendingRequest.wait_for(std::chrono::seconds(0)) !=
            std::future_status::ready) {
        return;
    }

    const bool wasSwitch = runtime.switchingCurrency;
    const std::string requestedCurrency = runtime.pendingCurrency;
    CurrencyRateResult result;
    try {
        result = runtime.pendingRequest.get();
    } catch (const std::exception& error) {
        result.currency = requestedCurrency;
        result.errorMessage = error.what();
    } catch (...) {
        result.currency = requestedCurrency;
        result.errorMessage = "Currency request failed";
    }

    runtime.requestPending = false;
    runtime.switchingCurrency = false;
    runtime.pendingCurrency.clear();
    runtime.lastAttemptAt = std::chrono::steady_clock::now();

    if (result.success && result.currency == requestedCurrency &&
        std::isfinite(result.usdToCurrency) && result.usdToCurrency > 0.0) {
        runtime.activeCurrency = result.currency;
        runtime.usdToActive = result.usdToCurrency;
        runtime.rateTimestamp = result.timestamp;
        if (state.config.displayCurrency != result.currency) {
            state.config.displayCurrency = result.currency;
            squarestar::config::RequestConfigSave();
        }
        RequestGuiRedraw();
        return;
    }

    if (wasSwitch) {
        PublishUserFeedback(
            state,
            UserFeedbackType::Warning,
            "Currency rate unavailable",
            result.errorMessage.empty()
                ? std::string("Yahoo did not return the selected currency rate.")
                : result.errorMessage);
        RequestGuiRedraw();
    }
}

} // namespace

void PumpDisplayCurrency(AppState& state) {
    auto& runtime = state.marketData.currencyDisplay;
    if (!runtime.initialized) {
        runtime.initialized = true;
        if (!IsSupportedDisplayCurrency(state.config.displayCurrency)) {
            state.config.displayCurrency = "USD";
            squarestar::config::RequestConfigSave();
        } else if (state.config.displayCurrency != "USD") {
            BeginCurrencyRequest(state, state.config.displayCurrency, true);
        }
    }

    CompleteCurrencyRequest(state);
    if (runtime.requestPending || runtime.activeCurrency == "USD")
        return;

    const auto now = std::chrono::steady_clock::now();
    if (runtime.lastAttemptAt == std::chrono::steady_clock::time_point{} ||
        now - runtime.lastAttemptAt >= kCurrencyRateRefreshInterval) {
        // Refresh in place: keep showing the last valid rate until the new
        // Yahoo snapshot arrives, rather than blanking the user's prices.
        BeginCurrencyRequest(state, runtime.activeCurrency, false);
    }
}

std::string_view DisplayCurrencyCode(const AppState& state) noexcept {
    const auto& runtime = state.marketData.currencyDisplay;
    if (runtime.requestPending && runtime.switchingCurrency &&
        !runtime.pendingCurrency.empty()) {
        return runtime.pendingCurrency;
    }
    return runtime.activeCurrency.empty() ? std::string_view("USD")
                                          : std::string_view(runtime.activeCurrency);
}

bool TryConvertUsdForDisplay(const AppState& state,
                             double usdValue,
                             double& convertedValue) noexcept {
    const auto& runtime = state.marketData.currencyDisplay;
    if (!std::isfinite(usdValue) ||
        (runtime.requestPending && runtime.switchingCurrency)) {
        return false;
    }
    const double rate = runtime.activeCurrency == "USD" ? 1.0 : runtime.usdToActive;
    if (!std::isfinite(rate) || rate <= 0.0)
        return false;
    convertedValue = usdValue * rate;
    return std::isfinite(convertedValue);
}

bool TryConvertDisplayToUsd(const AppState& state,
                            double displayValue,
                            double& usdValue) noexcept {
    const auto& runtime = state.marketData.currencyDisplay;
    if (!std::isfinite(displayValue) ||
        (runtime.requestPending && runtime.switchingCurrency)) {
        return false;
    }
    const double rate = runtime.activeCurrency == "USD" ? 1.0 : runtime.usdToActive;
    if (!std::isfinite(rate) || rate <= 0.0)
        return false;
    usdValue = displayValue / rate;
    return std::isfinite(usdValue);
}

void OpenCurrencyPicker(AppState& state,
                        ImVec2 anchorMin,
                        ImVec2 anchorMax,
                        ImGuiViewport* viewport) {
    PlayUISound("click.wav", state);
    // Opening the unit picker is an idempotent request, not a toggle. Clear any
    // stale/fading animated-menu state first so a fresh LiteGUI click can never
    // be interpreted as "close the picker" while still playing click.wav.
    CloseAllAnimatedFloatingMenus();
    g_CurrencyPicker.openRequested = true;
    g_CurrencyPicker.anchorMin = anchorMin;
    g_CurrencyPicker.anchorMax = anchorMax;
    g_CurrencyPicker.viewportId = viewport ? viewport->ID : 0;
    RequestGuiRedraw();
}

void RenderCurrencyUnitSelector(AppState& state,
                                const ImVec4& textColor) {
    const std::string_view code = DisplayCurrencyCode(state);
    const std::string codeText(code);
    ImGui::PushStyleColor(ImGuiCol_Text, textColor);
    ImGui::TextUnformatted(codeText.c_str());
    ImGui::PopStyleColor();

    const bool hovered = ImGui::IsItemHovered();
    const bool clicked = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left, false);
    const ImVec2 anchorMin = ImGui::GetItemRectMin();
    const ImVec2 anchorMax = ImGui::GetItemRectMax();
    if (hovered) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        ImGui::GetWindowDrawList()->AddLine(
            ImVec2(anchorMin.x, anchorMax.y + 1.0f),
            ImVec2(anchorMax.x, anchorMax.y + 1.0f),
            ImGui::ColorConvertFloat4ToU32(textColor),
            1.0f);
    }
    if (clicked)
        OpenCurrencyPicker(state,
                           anchorMin,
                           anchorMax,
                           ImGui::GetWindowViewport());
}

void RenderCurrencyPickerPopup(AppState& state) {
    bool toggle = g_CurrencyPicker.openRequested;
    g_CurrencyPicker.openRequested = false;

    ImGuiViewport* viewport = g_CurrencyPicker.viewportId != 0
                                  ? ImGui::FindViewportByID(g_CurrencyPicker.viewportId)
                                  : ImGui::GetMainViewport();
    if (!viewport)
        viewport = ImGui::GetMainViewport();

    // Render from a stable, input-transparent host window. The selector can be
    // activated from either the screener table or a stock header, but both
    // source windows may already be closed by the time shell overlays render.
    ImGui::SetNextWindowPos(viewport->Pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(viewport->Size, ImGuiCond_Always);
#ifdef IMGUI_HAS_VIEWPORT
    ImGui::SetNextWindowViewport(viewport->ID);
#endif
    constexpr ImGuiWindowFlags hostFlags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs |
        ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoNav |
        ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::Begin("##CurrencyPickerHost", nullptr, hostFlags);

    const bool compactLite = state.navigation.liteGuiActive;
    const float rowHeight = std::max(compactLite ? 19.0f : 22.0f,
                                     ImGui::GetTextLineHeight() +
                                         (compactLite ? 2.0f : 4.0f));
    const float itemSpacingY = compactLite ? 0.0f : 1.0f;
    const float popupPadding = compactLite ? 5.0f : 7.0f;
    const ImVec2 estimatedSize(
        compactLite ? 92.0f : 104.0f,
        popupPadding * 2.0f +
            static_cast<float>(kDisplayCurrencies.size()) * rowHeight +
            static_cast<float>(kDisplayCurrencies.size() - 1) * itemSpacingY + 2.0f);
    const ImVec2 requestedPosition = compactLite
                                         ? ImVec2(g_CurrencyPicker.anchorMin.x,
                                                  g_CurrencyPicker.anchorMin.y -
                                                      estimatedSize.y - 5.0f)
                                         : ImVec2(g_CurrencyPicker.anchorMin.x,
                                                  g_CurrencyPicker.anchorMax.y + 5.0f);
    const ImVec2 popupPosition = ClampPopupPosition(
        viewport,
        requestedPosition,
        estimatedSize);
#ifdef IMGUI_HAS_VIEWPORT
    const bool detachedLitePicker =
        compactLite &&
        (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0;
    ImGuiWindowClass pickerWindowClass;
    if (detachedLitePicker) {
        // LiteGUI is a fixed compact native host. Keep the upward-opening
        // currency list in its own owned borderless viewport so it cannot be
        // clipped or hidden by the LiteGUI platform window.
        pickerWindowClass.ParentViewportId = viewport->ID;
        pickerWindowClass.ViewportFlagsOverrideSet =
            ImGuiViewportFlags_NoAutoMerge |
            ImGuiViewportFlags_NoDecoration |
            ImGuiViewportFlags_NoTaskBarIcon;
        ImGui::SetNextWindowClass(&pickerWindowClass);
    } else {
        ImGui::SetNextWindowViewport(viewport->ID);
    }
#endif
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                        ImVec2(popupPadding, popupPadding));
    if (!BeginAnimatedFloatingMenu(state,
                                   "Display currency##Picker",
                                   toggle,
                                   popupPosition,
                                   ImVec2(0.0f, 0.0f),
                                   state.UiAnimationsEnabled(),
                                   ImVec2(estimatedSize.x, 0.0f),
                                   ImVec2(estimatedSize.x,
                                          std::numeric_limits<float>::max()))) {
        ImGui::PopStyleVar();
        ImGui::End();
        return;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                        ImVec2(compactLite ? 2.0f : 4.0f, itemSpacingY));
    const std::string_view selectedCode = DisplayCurrencyCode(state);
    for (const DisplayCurrencyOption& option : kDisplayCurrencies) {
        const bool selected = option.code == selectedCode;
        const std::string optionText(option.code);
        if (ImGui::Selectable(optionText.c_str(),
                              selected,
                              ImGuiSelectableFlags_None,
                              ImVec2(0.0f, rowHeight))) {
            PlayUISound("click.wav", state);
            SelectDisplayCurrency(state, option.code);
            CloseAnimatedFloatingMenu(false);
        }
    }
    ImGui::PopStyleVar();
    EndAnimatedFloatingMenu();
    ImGui::PopStyleVar();
    ImGui::End();
}

} // namespace squarestar::shell
