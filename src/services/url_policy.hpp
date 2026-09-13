#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace squarestar::http {

// Allow only SquareStar's HTTPS API hosts. Reject ports, userinfo, lookalike
// suffixes, and other schemes.
[[nodiscard]] bool IsAllowedApiUrl(std::string_view url) noexcept;
[[nodiscard]] bool IsYahooFinanceApiUrl(std::string_view url) noexcept;

// External news/company links may use any ordinary HTTPS DNS hostname, but
// must not contain credentials, custom ports, literal addresses, or controls.
[[nodiscard]] bool IsSafeExternalHttpsUrl(std::string_view url) noexcept;

} // namespace squarestar::http
