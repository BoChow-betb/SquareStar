#pragma once

#include <chrono>
#include <string>

namespace squarestar::application {

enum class UserFeedbackType {
    Information,
    Success,
    Warning,
    Error,
};

enum class UserFeedbackDestination {
    Automatic,
    Foreground,
    Background,
};

enum class UserFeedbackSound {
    Automatic,
    None,
    Confirmation,
    Decline,
    Error,
};

struct UserFeedback {
    UserFeedbackType type = UserFeedbackType::Information;
    std::string title;
    std::string body;
    std::chrono::seconds duration{4};
    UserFeedbackDestination destination = UserFeedbackDestination::Automatic;
    UserFeedbackSound sound = UserFeedbackSound::Automatic;
    std::string actionLabel{};
    std::string actionUrl{};
    std::string actionPath{};
};

struct UserFeedbackRoute {
    UserFeedbackDestination destination = UserFeedbackDestination::Automatic;
    const char* soundAsset = nullptr;
};

[[nodiscard]] UserFeedbackRoute ResolveUserFeedbackRoute(
    const UserFeedback& feedback) noexcept;

} // namespace squarestar::application
