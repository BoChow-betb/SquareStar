#pragma once

#include <algorithm>
#include <charconv>
#include <cctype>
#include <string>
#include <string_view>

namespace squarestar::text {

inline char AsciiUpper(unsigned char value) noexcept {
    return value >= 'a' && value <= 'z' ? static_cast<char>(value - ('a' - 'A'))
                                        : static_cast<char>(value);
}

inline char AsciiLower(unsigned char value) noexcept {
    return value >= 'A' && value <= 'Z' ? static_cast<char>(value + ('a' - 'A'))
                                        : static_cast<char>(value);
}

inline void UppercaseInPlace(std::string& value) {
    std::transform(value.begin(), value.end(), value.begin(), AsciiUpper);
}

inline void LowercaseInPlace(std::string& value) {
    std::transform(value.begin(), value.end(), value.begin(), AsciiLower);
}

inline int ParseIntOr(std::string_view value, int fallback) noexcept {
    if (value.empty())
        return fallback;
    // std::from_chars is allocation-free and, unlike std::stoi without an
    // index parameter, lets us reject partially parsed values such as "12px".
    if (value.front() == '+') {
        value.remove_prefix(1);
        if (value.empty())
            return fallback;
    }
    int parsed = 0;
    const char* const end = value.data() + value.size();
    const auto [parsedEnd, error] = std::from_chars(value.data(), end, parsed);
    return error == std::errc{} && parsedEnd == end ? parsed : fallback;
}

} // namespace squarestar::text
