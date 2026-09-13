#include "secret_protection.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincrypt.h>
#endif

namespace squarestar::secrets {

void SecureClear(std::string& value) noexcept {
    if (value.empty())
        return;
#ifdef _WIN32
    SecureZeroMemory(value.data(), value.size());
#else
    volatile char* bytes = value.data();
    for (std::size_t index = 0; index < value.size(); ++index)
        bytes[index] = 0;
#endif
}

#ifdef _WIN32
namespace {

std::string HexEncode(const unsigned char* data, std::size_t size) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string encoded(size * 2, '\0');
    for (std::size_t i = 0; i < size; ++i) {
        encoded[i * 2] = digits[data[i] >> 4];
        encoded[i * 2 + 1] = digits[data[i] & 0x0F];
    }
    return encoded;
}

bool HexDecode(std::string_view encoded, std::vector<unsigned char>& decoded) {
    decoded.clear();
    if (encoded.empty() || encoded.size() % 2 != 0 || encoded.size() > 2 * 1024 * 1024)
        return false;
    const auto nibble = [](unsigned char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    decoded.reserve(encoded.size() / 2);
    for (std::size_t i = 0; i < encoded.size(); i += 2) {
        const int high = nibble(static_cast<unsigned char>(encoded[i]));
        const int low = nibble(static_cast<unsigned char>(encoded[i + 1]));
        if (high < 0 || low < 0) {
            decoded.clear();
            return false;
        }
        decoded.push_back(static_cast<unsigned char>((high << 4) | low));
    }
    return true;
}

void SecureClear(std::vector<unsigned char>& bytes) noexcept {
    if (!bytes.empty())
        SecureZeroMemory(bytes.data(), bytes.size());
}

std::optional<std::string> ProtectDpapi(std::string_view plaintext,
                                        const wchar_t* description) {
    if (plaintext.empty())
        return std::string{};
    if (plaintext.size() > std::numeric_limits<DWORD>::max())
        return std::nullopt;
    DATA_BLOB input{static_cast<DWORD>(plaintext.size()),
                    reinterpret_cast<BYTE*>(const_cast<char*>(plaintext.data()))};
    DATA_BLOB output{};
    if (!CryptProtectData(&input,
                          description,
                          nullptr,
                          nullptr,
                          nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN,
                          &output)) {
        return std::nullopt;
    }
    std::string protectedValue = HexEncode(output.pbData, output.cbData);
    SecureZeroMemory(output.pbData, output.cbData);
    LocalFree(output.pbData);
    return protectedValue;
}

std::optional<std::string> UnprotectDpapi(std::string_view protectedValue) {
    if (protectedValue.empty())
        return std::string{};
    std::vector<unsigned char> encrypted;
    if (!HexDecode(protectedValue, encrypted) ||
        encrypted.size() > std::numeric_limits<DWORD>::max()) {
        return std::nullopt;
    }
    DATA_BLOB input{static_cast<DWORD>(encrypted.size()), encrypted.data()};
    DATA_BLOB output{};
    if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr,
                            CRYPTPROTECT_UI_FORBIDDEN, &output)) {
        SecureClear(encrypted);
        return std::nullopt;
    }
    std::string plaintext(reinterpret_cast<const char*>(output.pbData), output.cbData);
    SecureZeroMemory(output.pbData, output.cbData);
    LocalFree(output.pbData);
    SecureClear(encrypted);
    return plaintext;
}

} // namespace
#endif

std::optional<std::string> ProtectApiKey(std::string_view key) {
#ifdef _WIN32
    return ProtectDpapi(key, L"SquareStar API key");
#else
    if (key.empty())
        return std::string{};
    (void)key;
    return std::nullopt;
#endif
}

std::optional<std::string> UnprotectApiKey(std::string_view protectedKey) {
#ifdef _WIN32
    return UnprotectDpapi(protectedKey);
#else
    if (protectedKey.empty())
        return std::string{};
    (void)protectedKey;
    return std::nullopt;
#endif
}

std::optional<std::string> ProtectLocalState(std::string_view plaintext) {
#ifdef _WIN32
    return ProtectDpapi(plaintext, L"SquareStar private local state");
#else
    if (plaintext.empty())
        return std::string{};
    (void)plaintext;
    return std::nullopt;
#endif
}

std::optional<std::string> UnprotectLocalState(std::string_view protectedState) {
#ifdef _WIN32
    return UnprotectDpapi(protectedState);
#else
    if (protectedState.empty())
        return std::string{};
    (void)protectedState;
    return std::nullopt;
#endif
}

} // namespace squarestar::secrets
