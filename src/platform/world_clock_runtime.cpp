#include "platform/world_clock_runtime.hpp"

#include "domain/market_calendar.hpp"
#include "domain/world_clock_zones.hpp"

#include "platform/windows_headers.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>


namespace squarestar::platform {
namespace {

#ifdef _WIN32
const DYNAMIC_TIME_ZONE_INFORMATION* ResolveWorldClockZone(int zoneIndex) {
    static std::array<DYNAMIC_TIME_ZONE_INFORMATION, squarestar::market::WORLD_ZONE_COUNT> cache{};
    static std::array<bool, squarestar::market::WORLD_ZONE_COUNT> valid{};
    static std::once_flag initialize;
    std::call_once(initialize, [] {
        for (DWORD index = 0;; ++index) {
            DYNAMIC_TIME_ZONE_INFORMATION candidate{};
            const DWORD result = EnumDynamicTimeZoneInformation(index, &candidate);
            if (result == ERROR_NO_MORE_ITEMS)
                break;
            if (result != ERROR_SUCCESS)
                continue;
            for (int zone = 0; zone < squarestar::market::WORLD_ZONE_COUNT; ++zone) {
                if (std::wcscmp(candidate.TimeZoneKeyName,
                                squarestar::market::WORLD_ZONES[zone].windowsKey) == 0) {
                    cache[zone] = candidate;
                    valid[zone] = true;
                }
            }
        }
    });
    return zoneIndex >= 0 && zoneIndex < squarestar::market::WORLD_ZONE_COUNT && valid[zoneIndex]
               ? &cache[zoneIndex]
               : nullptr;
}
#endif

} // namespace

std::tm SafeTimeTm(std::time_t raw, bool utc) noexcept {
    std::tm calendar{};
#ifdef _WIN32
    if (utc)
        gmtime_s(&calendar, &raw);
    else
        localtime_s(&calendar, &raw);
#else
    if (utc)
        gmtime_r(&raw, &calendar);
    else
        localtime_r(&raw, &calendar);
#endif
    return calendar;
}

WorldClockReading ReadWorldClock(int zoneIndex, std::time_t raw) {
    if (raw <= 0)
        raw = std::time(nullptr);
    zoneIndex = std::clamp(zoneIndex, 0, squarestar::market::WORLD_ZONE_COUNT - 1);

#ifdef _WIN32
    const std::tm utc = SafeTimeTm(raw, true);
    SYSTEMTIME utcSystem{};
    utcSystem.wYear = static_cast<WORD>(utc.tm_year + 1900);
    utcSystem.wMonth = static_cast<WORD>(utc.tm_mon + 1);
    utcSystem.wDay = static_cast<WORD>(utc.tm_mday);
    utcSystem.wDayOfWeek = static_cast<WORD>(utc.tm_wday);
    utcSystem.wHour = static_cast<WORD>(utc.tm_hour);
    utcSystem.wMinute = static_cast<WORD>(utc.tm_min);
    utcSystem.wSecond = static_cast<WORD>(utc.tm_sec);
    SYSTEMTIME localSystem{};
    if (const auto* zone = ResolveWorldClockZone(zoneIndex);
        zone && SystemTimeToTzSpecificLocalTimeEx(zone, &utcSystem, &localSystem)) {
        std::tm local{};
        local.tm_year = static_cast<int>(localSystem.wYear) - 1900;
        local.tm_mon = static_cast<int>(localSystem.wMonth) - 1;
        local.tm_mday = static_cast<int>(localSystem.wDay);
        local.tm_hour = static_cast<int>(localSystem.wHour);
        local.tm_min = static_cast<int>(localSystem.wMinute);
        local.tm_sec = static_cast<int>(localSystem.wSecond);
        local.tm_wday = static_cast<int>(localSystem.wDayOfWeek);
        const std::time_t wallAsUtc = squarestar::market::UtcTmToTimeT(local);
        return {local,
                static_cast<int>(std::lround(std::difftime(wallAsUtc, raw) / 60.0))};
    }
#endif

    const int fallback = squarestar::market::WORLD_ZONES[zoneIndex].fallbackOffsetMinutes;
    return {SafeTimeTm(raw + fallback * 60, true), fallback};
}

std::string FormatUtcOffset(int offsetMinutes) {
    const char sign = offsetMinutes < 0 ? '-' : '+';
    const int absolute = std::abs(offsetMinutes);
    char buffer[24]{};
    if (absolute % 60 == 0)
        std::snprintf(buffer, sizeof(buffer), "UTC%c%d", sign, absolute / 60);
    else
        std::snprintf(buffer,
                      sizeof(buffer),
                      "UTC%c%d:%02d",
                      sign,
                      absolute / 60,
                      absolute % 60);
    return buffer;
}

std::string FormatClockTime(const std::tm& calendar, bool includeSeconds) {
    char buffer[32]{};
    std::strftime(buffer,
                  sizeof(buffer),
                  includeSeconds ? "%I:%M:%S %p" : "%I:%M %p",
                  &calendar);
    std::string value = buffer;
    if (!value.empty() && value.front() == '0')
        value.erase(value.begin());
    return value;
}

std::string WorldClockOptionLabel(int zoneIndex, std::time_t raw) {
    zoneIndex = std::clamp(zoneIndex, 0, squarestar::market::WORLD_ZONE_COUNT - 1);
    const WorldClockReading reading = ReadWorldClock(zoneIndex, raw);
    return std::string(squarestar::market::WORLD_ZONES[zoneIndex].label) + "  \xC2\xB7  " +
           FormatUtcOffset(reading.utcOffsetMinutes);
}

std::string FormatAsOfTime(std::time_t raw, bool marketTime) {
    if (raw <= 0)
        raw = std::time(nullptr);
    const std::tm shown = marketTime
                              ? SafeTimeTm(raw + squarestar::market::NewYorkUtcOffsetSeconds(raw),
                                           true)
                              : SafeTimeTm(raw, false);
    return "As of " + FormatClockTime(shown) + (marketTime ? " ET" : " Local");
}

} // namespace squarestar::platform
