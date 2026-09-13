#pragma once

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <string>
#include <system_error>

namespace squarestar::format {

inline std::string FormatCompactNumber(double number, int smallPrecision = 2) {
    if (!std::isfinite(number))
        return "N/A";
    struct Scale {
        double threshold = 0.0;
        const char* suffix = "";
    };
    static constexpr std::array scales = {Scale{1e12, "T"},
                                          Scale{1e9, "B"},
                                          Scale{1e6, "M"},
                                          Scale{1e3, "K"}};
    const char* suffix = "";
    double scaled = number;
    for (const auto& scale : scales) {
        if (std::abs(number) >= scale.threshold) {
            scaled /= scale.threshold;
            suffix = scale.suffix;
            break;
        }
    }
    const int precision =
        suffix[0] ? (std::abs(scaled) >= 100.0 ? 0 : (std::abs(scaled) >= 10.0 ? 1 : 2))
                  : std::clamp(smallPrecision, 0, 15);
    // A stack buffer avoids constructing a locale-aware stream in this hot
    // formatting path. The largest finite double needs at most 309 integer
    // digits, plus sign, decimal places, and suffix.
    std::array<char, 384> buffer{};
    const auto [end, error] = std::to_chars(buffer.data(),
                                            buffer.data() + buffer.size(),
                                            scaled,
                                            std::chars_format::fixed,
                                            precision);
    if (error != std::errc{})
        return "N/A";
    std::string text(buffer.data(), end);
    if (text.find('.') != std::string::npos) {
        while (!text.empty() && text.back() == '0')
            text.pop_back();
        if (!text.empty() && text.back() == '.')
            text.pop_back();
    }
    return text + suffix;
}

inline std::string FormatDouble(double number) {
    if (number <= 0.0)
        return "-";
    std::array<char, 384> buffer{};
    std::snprintf(buffer.data(), buffer.size(), "%.2f", number);
    return std::string(buffer.data());
}

inline std::string FormatLargeNumber(double number) {
    if (number <= 0.0 || !std::isfinite(number))
        return "N/A";
    return FormatCompactNumber(number, 2);
}

inline bool PriceChangedAtDisplayPrecision(double previousPrice,
                                           double currentPrice) noexcept {
    if (!std::isfinite(previousPrice) || !std::isfinite(currentPrice) ||
        previousPrice <= 0.0 || currentPrice <= 0.0)
        return false;
    const double previousCents = std::nearbyint(previousPrice * 100.0);
    const double currentCents = std::nearbyint(currentPrice * 100.0);
    if (!std::isfinite(previousCents) || !std::isfinite(currentCents))
        return false;
    return std::abs(previousCents - currentCents) >= 1.0;
}

} // namespace squarestar::format
