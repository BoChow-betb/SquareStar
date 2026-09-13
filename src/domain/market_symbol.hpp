#pragma once

#include "text.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace squarestar::market {

inline bool IsAsciiAlpha(unsigned char value) noexcept {
    return (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z');
}

inline bool IsAsciiDigit(unsigned char value) noexcept {
    return value >= '0' && value <= '9';
}

inline std::string YahooSymbolKey(std::string symbol) {
    text::UppercaseInPlace(symbol);
    std::replace(symbol.begin(), symbol.end(), '.', '-');
    return symbol;
}

inline std::string FinnhubSymbolKey(std::string symbol) {
    text::UppercaseInPlace(symbol);
    std::replace(symbol.begin(), symbol.end(), '-', '.');
    return symbol;
}

// Providers usually stop serving historical data under an old ticker as soon
// as a corporate rename becomes effective. Keep confirmed changes here so every
// caller uses the same current symbol.
inline std::string_view CurrentTickerFor(std::string_view canonical) noexcept {
    static constexpr std::array<std::pair<std::string_view, std::string_view>, 1>
        confirmedRenames{{{"AXIA", "AXIAY"}}};
    const auto found = std::find_if(
        confirmedRenames.begin(), confirmedRenames.end(), [&](const auto& rename) {
            return rename.first == canonical;
        });
    return found == confirmedRenames.end() ? canonical : found->second;
}

// One validated, canonical representation keeps provider-specific symbol rules
// out of the GUI, LiteGUI, cache, and request-building code.
class MarketSymbol {
  public:
    static std::optional<MarketSymbol> Parse(std::string_view raw) {
        if (raw.empty() || raw.size() > 8)
            return std::nullopt;

        std::string canonical(raw);
        text::UppercaseInPlace(canonical);

        // Yahoo Finance futures use compact continuous symbols such as GC=F,
        // ES=F, and 6E=F. Futures may start with a digit, unlike US equities,
        // so validate the futures grammar before applying equity-only rules.
        const size_t futuresSuffix = canonical.find("=F");
        if (futuresSuffix != std::string::npos) {
            if (futuresSuffix + 2 != canonical.size() || futuresSuffix == 0 ||
                futuresSuffix > 5)
                return std::nullopt;
            bool hasAlpha = false;
            for (size_t index = 0; index < futuresSuffix; ++index) {
                const unsigned char value = (unsigned char)canonical[index];
                if (!IsAsciiAlpha(value) && !IsAsciiDigit(value))
                    return std::nullopt;
                hasAlpha = hasAlpha || IsAsciiAlpha(value);
            }
            return hasAlpha ? std::optional<MarketSymbol>(MarketSymbol(std::move(canonical)))
                            : std::nullopt;
        }

        if (!IsAsciiAlpha((unsigned char)canonical.front()) || canonical.size() > 6)
            return std::nullopt;
        size_t separatorCount = 0;
        size_t separatorPosition = std::string::npos;
        for (size_t index = 0; index < canonical.size(); ++index) {
            const unsigned char value = (unsigned char)canonical[index];
            if (IsAsciiAlpha(value) || IsAsciiDigit(value))
                continue;
            if (value != '.' && value != '-')
                return std::nullopt;
            ++separatorCount;
            separatorPosition = index;
        }
        if (separatorCount > 1)
            return std::nullopt;
        if (separatorCount == 1 &&
            (separatorPosition == 0 || separatorPosition + 2 != canonical.size()))
            return std::nullopt;
        canonical.assign(CurrentTickerFor(canonical));
        return MarketSymbol(std::move(canonical));
    }

    const std::string& Canonical() const noexcept {
        return canonical_;
    }

    std::string Yahoo() const {
        return YahooSymbolKey(canonical_);
    }

    std::string Finnhub() const {
        return FinnhubSymbolKey(canonical_);
    }

    bool IsYahooFutures() const noexcept {
        return canonical_.size() > 2 && canonical_.ends_with("=F");
    }

    bool HasSupportedUsClassSuffix() const noexcept {
        if (IsYahooFutures())
            return true;
        const size_t separator = canonical_.find_first_of(".-");
        return separator == std::string::npos || canonical_.back() == 'A' ||
               canonical_.back() == 'B';
    }

  private:
    explicit MarketSymbol(std::string canonical) : canonical_(std::move(canonical)) {}

    std::string canonical_;
};

} // namespace squarestar::market
