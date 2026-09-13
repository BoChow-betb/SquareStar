#pragma once

#include <ctime>
#include <string>

namespace squarestar::platform {

struct WorldClockReading {
    std::tm calendar{};
    int utcOffsetMinutes = 0;
};

std::tm SafeTimeTm(std::time_t raw, bool utc) noexcept;
WorldClockReading ReadWorldClock(int zoneIndex, std::time_t raw);
std::string FormatUtcOffset(int offsetMinutes);
std::string FormatClockTime(const std::tm& calendar, bool includeSeconds = false);
std::string WorldClockOptionLabel(int zoneIndex, std::time_t raw);
std::string FormatAsOfTime(std::time_t raw, bool marketTime);

} // namespace squarestar::platform
