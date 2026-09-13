#pragma once

namespace squarestar::market {

struct WorldClockZone {
    const char* label = "";
    const wchar_t* windowsKey = L"";
    int fallbackOffsetMinutes = 0;
};

inline constexpr int WORLD_ZONE_COUNT = 8;
extern const WorldClockZone WORLD_ZONES[WORLD_ZONE_COUNT];

} // namespace squarestar::market
