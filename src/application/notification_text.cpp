#include "application/notification_text.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace squarestar::application {

std::string FormatStockMovePrices(double before,
                                  double after,
                                  std::string_view currency) {
    std::ostringstream text;
    text << std::fixed << std::setprecision(2);
    if (std::isfinite(before) && before > 0.0)
        text << before << " --> " << after;
    else
        text << after;
    if (!currency.empty())
        text << ' ' << currency;
    return text.str();
}

std::string FormatStockMoveDelta(double before, double after) {
    if (!std::isfinite(before) || before <= 0.0 || !std::isfinite(after))
        return {};
    const double change = after - before;
    const double percent = change / before * 100.0;
    std::ostringstream text;
    text << std::showpos << std::fixed << std::setprecision(2) << change << " ("
         << percent << "%)";
    return text.str();
}

BackgroundNotificationText FormatBackgroundStockMoves(
    const std::vector<StockMoveNotification>& rows,
    std::size_t maximumMessageBytes) {
    BackgroundNotificationText result;
    if (rows.empty())
        return result;

    result.title = rows.size() == 1
                       ? std::string(rows.front().priceAlert ? "[Price alert] "
                                                            : "[Market move] ") +
                             rows.front().ticker
                       : "[Market moves] " + std::to_string(rows.size()) + " stocks";

    const std::size_t visibleRows =
        rows.size() == 1 ? 1 : std::min(rows.size(), kNotificationMaxStackRows);
    for (std::size_t index = 0; index < visibleRows; ++index) {
        const StockMoveNotification& row = rows[index];
        std::string line;
        if (rows.size() == 1) {
            line = FormatStockMovePrices(row.before, row.after, row.currency);
        } else {
            line = row.ticker + (row.priceAlert ? " alert: " : ": ") +
                   FormatStockMovePrices(row.before, row.after, row.currency);
        }
        const std::string delta = FormatStockMoveDelta(row.before, row.after);
        if (!delta.empty()) {
            if (rows.size() == 1) {
                result.accentText = delta;
                result.accentDirection = row.after > row.before ? 1 : -1;
            } else {
                line += "  " + delta;
            }
        } else if (row.priceAlert && std::isfinite(row.alertThreshold) &&
                   row.alertThreshold > 0.0) {
            std::ostringstream threshold;
            threshold << "  level " << std::fixed << std::setprecision(2)
                      << row.alertThreshold;
            line += threshold.str();
        }

        const std::string separator = result.message.empty() ? "" : "\n";
        if (result.message.size() + separator.size() + line.size() <= maximumMessageBytes) {
            result.message += separator + line;
            continue;
        }

        const std::size_t hidden = rows.size() - index;
        const std::string suffix = "\n+" + std::to_string(hidden) + " more";
        if (result.message.empty()) {
            result.message = line.substr(0, maximumMessageBytes);
        } else if (maximumMessageBytes > suffix.size()) {
            result.message.resize(
                std::min(result.message.size(), maximumMessageBytes - suffix.size()));
            result.message += suffix;
        } else {
            result.message.resize(maximumMessageBytes);
        }
        return result;
    }

    if (rows.size() > visibleRows) {
        const std::string suffix =
            "\n+" + std::to_string(rows.size() - visibleRows) + " more";
        if (result.message.size() + suffix.size() <= maximumMessageBytes) {
            result.message += suffix;
        } else if (maximumMessageBytes > suffix.size()) {
            result.message.resize(
                std::min(result.message.size(), maximumMessageBytes - suffix.size()));
            result.message += suffix;
        }
    }
    return result;
}

}
