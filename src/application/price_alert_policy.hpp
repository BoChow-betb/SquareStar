#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "application/app_state.hpp"

namespace squarestar::application {

std::optional<double> ConfiguredPriceAlert(const squarestar::alerts::AlertService& alerts,
                                           const StockContext& context) noexcept;
bool IsStockPriceAlertActive(const squarestar::alerts::AlertService& alerts,
                             const StockContext& context) noexcept;
bool IsPriceAlertMutedUntil(const squarestar::alerts::AlertService& alerts,
                            std::string_view ticker,
                            std::int64_t now) noexcept;
bool ApplyPriceAlertSilence(AppState& state,
                            std::string_view ticker,
                            std::optional<std::int64_t> muteUntil);
bool ExpirePriceAlertMutes(AppState& state, std::int64_t now);
bool UpsertPriceAlertToast(AppState& state,
                           const StockContext& context,
                           double threshold);
bool RemoveConfiguredPriceAlert(AppState& state, std::string_view ticker);

} // namespace squarestar::application
