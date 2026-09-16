#pragma once

#include <ctime>
#include <string>

namespace squarestar::market {

struct CalendarDate {
    int year = 1970;
    int month = 1;
    int day = 1;

    friend bool operator==(const CalendarDate&, const CalendarDate&) = default;
};

int GregorianWeekday(int year, int month, int day) noexcept;
int NthWeekdayOfMonth(int year, int month, int nth, int weekday) noexcept;
int LastWeekdayOfMonth(int year, int month, int weekday) noexcept;
CalendarDate GoodFridayDate(int year) noexcept;

std::string GetMarketClosedReason(int year, int month, int day);
bool IsMarketTradingDay(int year, int month, int day);
int MarketCloseMinutesForDate(int year, int month, int day);
bool IsUsEasternDstLocal(int year, int month, int day, int hour) noexcept;
int NewYorkUtcOffsetSeconds(std::time_t utcTime) noexcept;
std::time_t UtcTmToTimeT(std::tm value) noexcept;
std::tm CurrentNewYorkTime(std::time_t utcTime = std::time(nullptr)) noexcept;
bool IsMarketOpenAt(std::time_t utcTime);
bool IsMarketOpeningWindowAt(std::time_t utcTime);
std::time_t NextMarketOpenAt(std::time_t utcTime);
std::time_t NextMarketSettlementAt(std::time_t utcTime);
std::string MarketSettlementTimeLabel(std::time_t utcTime);

}
