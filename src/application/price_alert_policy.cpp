#include "application/price_alert_policy.hpp"

#include "application/notification_text.hpp"

#include <algorithm>
#include <cmath>


namespace squarestar::application {

std::optional<double> ConfiguredPriceAlert(const squarestar::alerts::AlertService& alerts,
                                           const StockContext& context) noexcept {
    return alerts.ActiveThreshold(context.navigation.ticker);
}

bool IsStockPriceAlertActive(const squarestar::alerts::AlertService& alerts,
                             const StockContext& context) noexcept {
    const auto alert = ConfiguredPriceAlert(alerts, context);
    return alert.has_value() && context.RawData().success &&
           std::isfinite(context.RawData().currentPrice) &&
           context.RawData().currentPrice > 0.0 && context.RawData().currentPrice <= *alert;
}

bool IsPriceAlertMutedUntil(const squarestar::alerts::AlertService& alerts,
                            std::string_view ticker,
                            std::int64_t now) noexcept {
    return alerts.IsMutedUntil(ticker, now);
}

bool ApplyPriceAlertSilence(AppState& state,
                            std::string_view ticker,
                            std::optional<std::int64_t> muteUntil) {
    const std::string tickerKey(ticker);
    if (muteUntil)
        state.alerts.SilenceUntil(tickerKey, *muteUntil);
    else
        state.alerts.Silence(tickerKey);

    bool stoppedSequence = false;
    const auto silence = [&](auto& contexts) {
        for (auto& candidate : contexts) {
            if (!candidate || candidate->navigation.ticker != ticker)
                continue;
            stoppedSequence = stoppedSequence || candidate->alerts.priceAlertTriggered ||
                              candidate->alerts.priceAlertSoundPlaysRemaining > 0;
            candidate->alerts.priceAlertTriggered = IsStockPriceAlertActive(state.alerts, *candidate);
            candidate->alerts.priceAlertSoundPlaysRemaining = 0;
        }
    };
    silence(state.marketData.activeContexts);
    silence(state.alerts.Monitors());
    silence(state.marketData.retiredLiteContexts);

    state.alerts.RemoveToastsForTicker(ticker);
    return stoppedSequence;
}

bool ExpirePriceAlertMutes(AppState& state, std::int64_t now) {
    const std::vector<std::string> expired = state.alerts.ExpireMutes(now);
    for (const std::string& ticker : expired) {
        const auto rearm = [&](auto& contexts) {
            for (auto& candidate : contexts) {
                if (candidate && candidate->navigation.ticker == ticker) {
                    candidate->alerts.priceAlertTriggered = false;
                    candidate->alerts.priceAlertSoundPlaysRemaining = 0;
                }
            }
        };
        rearm(state.marketData.activeContexts);
        rearm(state.alerts.Monitors());
        rearm(state.marketData.retiredLiteContexts);
    }
    return !expired.empty();
}

bool UpsertPriceAlertToast(AppState& state,
                           const StockContext& context,
                           double threshold) {
    return state.alerts.UpsertToast(context.navigation.ticker,
                                    context.RawData().currentPrice,
                                    threshold,
                                    kNotificationMaxVisibleBlocks);
}

bool RemoveConfiguredPriceAlert(AppState& state, std::string_view ticker) {
    if (!state.alerts.RemoveThreshold(ticker))
        return false;

    state.alerts.RemoveToastsForTicker(ticker);
    state.render.notifications.notificationCenter.RemovePriceAlertForTicker(ticker);
    const auto resetMatchingContext = [&](auto& contexts) {
        for (auto& candidate : contexts) {
            if (!candidate || candidate->navigation.ticker != ticker)
                continue;
            candidate->alerts.priceAlertTriggered = false;
            candidate->alerts.priceAlertSoundPlaysRemaining = 0;
        }
    };
    resetMatchingContext(state.marketData.activeContexts);
    resetMatchingContext(state.marketData.retiredLiteContexts);
    state.alerts.RemoveMonitorsIf([&](const auto& monitor) {
        return monitor && monitor->navigation.ticker == ticker;
    });
    return true;
}

} // namespace squarestar::application
