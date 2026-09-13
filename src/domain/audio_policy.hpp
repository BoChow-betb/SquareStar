#pragma once

#include <chrono>
#include <string_view>

namespace squarestar::audio {

struct AppSoundPolicy {
    std::string_view pattern;
    int priority = 0;
    std::chrono::milliseconds guard{80};
};

AppSoundPolicy AppSoundPolicyFor(std::string_view filename) noexcept;

} // namespace squarestar::audio
