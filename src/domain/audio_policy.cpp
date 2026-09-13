#include "domain/audio_policy.hpp"

#include <algorithm>
#include <array>

namespace squarestar::audio {

AppSoundPolicy AppSoundPolicyFor(std::string_view filename) noexcept {
    static constexpr std::array policies = {
        AppSoundPolicy{"error.", 121, std::chrono::milliseconds(220)},
        AppSoundPolicy{"decline.", 120, std::chrono::milliseconds(300)},
        AppSoundPolicy{"marketopenclose.", 119, std::chrono::milliseconds(1100)},
        AppSoundPolicy{"loading.", 115, std::chrono::milliseconds(500)},
        AppSoundPolicy{"launch.", 90, std::chrono::milliseconds(350)},
        AppSoundPolicy{"transition.", 84, std::chrono::milliseconds(120)},
        AppSoundPolicy{"on.", 80, std::chrono::milliseconds(120)},
        AppSoundPolicy{"off.", 80, std::chrono::milliseconds(120)},
        AppSoundPolicy{"click.", 75, std::chrono::milliseconds(80)},
        AppSoundPolicy{"gain.", 65, std::chrono::milliseconds(120)},
        AppSoundPolicy{"loss.", 65, std::chrono::milliseconds(120)},
        AppSoundPolicy{"key.", 55, std::chrono::milliseconds(80)}};
    const auto found = std::find_if(policies.begin(), policies.end(), [&](const auto& policy) {
        return filename.find(policy.pattern) != std::string_view::npos;
    });
    if (found != policies.end())
        return *found;
    return {"", filename.empty() ? 0 : 50, std::chrono::milliseconds(80)};
}

} // namespace squarestar::audio
