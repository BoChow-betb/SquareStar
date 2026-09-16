#pragma once

#include <future>

namespace squarestar::application {

enum class ApiKeyValidation { Valid, Empty, Invalid, Forbidden, NetworkError };


struct SettingsRuntimeState {
    char apiKeyBuffer[192] = "";
    bool apiKeyInputInvalid = false;
    std::future<ApiKeyValidation> validationFuture;
};

}
