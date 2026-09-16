#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace squarestar::http {


[[nodiscard]] bool IsAllowedApiUrl(std::string_view url) noexcept;
[[nodiscard]] bool IsYahooFinanceApiUrl(std::string_view url) noexcept;


[[nodiscard]] bool IsSafeExternalHttpsUrl(std::string_view url) noexcept;

}
