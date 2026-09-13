#include "domain/world_clock_zones.hpp"

namespace squarestar::market {

const WorldClockZone WORLD_ZONES[WORLD_ZONE_COUNT] = {
    {"UTC", L"UTC", 0},
    {"New York (ET)", L"Eastern Standard Time", -300},
    {"London (UK)", L"GMT Standard Time", 0},
    {"Frankfurt (Central Europe)", L"W. Europe Standard Time", 60},
    {"Hong Kong (HKT)", L"China Standard Time", 480},
    {"Beijing (China)", L"China Standard Time", 480},
    {"Tokyo (JST)", L"Tokyo Standard Time", 540},
    {"Sydney (AET)", L"AUS Eastern Standard Time", 600},
};

} // namespace squarestar::market
