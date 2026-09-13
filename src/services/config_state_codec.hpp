#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace squarestar::application {
struct PersistedStateConstView;
struct PersistedStateView;
}

namespace squarestar::config {

struct ConfigDecodeResult {
    bool parsed = false;
    bool hasProtectedApiKey = false;
    std::string protectedApiKey;
    bool hasProtectedPrivateState = false;
    std::string protectedPrivateState;
};

// Serializes privacy-sensitive state before it is encrypted by Windows DPAPI.
std::string EncodePrivateConfigState(
    squarestar::application::PersistedStateConstView state,
    std::int64_t nowEpoch,
    std::string_view imguiLayout);

// Decodes a DPAPI-unwrapped private state payload. The input buffer is securely
// cleared before returning.
bool DecodePrivateConfigState(
    std::string json,
    squarestar::application::PersistedStateView state,
    std::int64_t nowEpoch,
    std::string& imguiLayout);

std::string EncodeConfigState(
    squarestar::application::PersistedStateConstView state,
    std::string_view protectedApiKey,
    std::string_view protectedPrivateState);

ConfigDecodeResult DecodeConfigState(
    std::string json,
    squarestar::application::PersistedStateView state);

} // namespace squarestar::config
