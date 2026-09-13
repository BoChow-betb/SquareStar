#pragma once

#include <future>

namespace squarestar::application {

enum class ApiKeyValidation { Valid, Empty, Invalid, Forbidden, NetworkError };

// Ephemeral Settings-page state belongs to the AppState lifetime rather than
// process-lifetime function statics. This keeps profile/state replacement from
// inheriting an old input buffer or an unrelated validation future.
struct SettingsRuntimeState {
    char apiKeyBuffer[192] = "";
    bool apiKeyInputInvalid = false;
    std::future<ApiKeyValidation> validationFuture;
};

} // namespace squarestar::application
