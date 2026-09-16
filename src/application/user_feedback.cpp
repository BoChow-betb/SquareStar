#include "application/user_feedback.hpp"

namespace squarestar::application {

UserFeedbackRoute ResolveUserFeedbackRoute(const UserFeedback& feedback) noexcept {


const UserFeedbackSound sound =
        feedback.sound != UserFeedbackSound::Automatic
            ? feedback.sound
        : feedback.type == UserFeedbackType::Warning ||
                  feedback.type == UserFeedbackType::Error
            ? UserFeedbackSound::Decline
            : UserFeedbackSound::Confirmation;

    UserFeedbackRoute route;
    route.destination = feedback.destination;
    switch (sound) {
    case UserFeedbackSound::None:
    case UserFeedbackSound::Automatic:
        route.soundAsset = nullptr;
        break;
    case UserFeedbackSound::Confirmation:
        route.soundAsset = "key.wav";
        break;
    case UserFeedbackSound::Decline:
        route.soundAsset = "decline.wav";
        break;
    case UserFeedbackSound::Error:
        route.soundAsset = "error.wav";
        break;
    }
    return route;
}

}
