#pragma once

#include <array>
#include <cstddef>
#include <ctime>
#include <string>
#include <string_view>

namespace squarestar::market {

struct DisplayCurrencyOption {
    std::string_view code;
    std::string_view yahooUsdSymbol;
};


inline constexpr std::array<DisplayCurrencyOption, 10> kDisplayCurrencies{{
    {"USD", ""},
    {"HKD", "HKD=X"},
    {"CNY", "CNY=X"},
    {"EUR", "EUR=X"},
    {"GBP", "GBP=X"},
    {"JPY", "JPY=X"},
    {"CAD", "CAD=X"},
    {"AUD", "AUD=X"},
    {"SGD", "SGD=X"},
    {"CHF", "CHF=X"},
}};

inline const DisplayCurrencyOption* FindDisplayCurrency(
    std::string_view code) noexcept {
    for (const DisplayCurrencyOption& option : kDisplayCurrencies) {
        if (option.code == code)
            return &option;
    }
    return nullptr;
}

inline bool IsSupportedDisplayCurrency(std::string_view code) noexcept {
    return FindDisplayCurrency(code) != nullptr;
}

struct CurrencyRateResult {
    std::string currency;
    double usdToCurrency = 0.0;
    std::time_t timestamp = 0;
    bool success = false;
    bool rateLimited = false;
    std::string errorMessage;
};

}
