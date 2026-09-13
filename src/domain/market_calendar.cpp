#include "market_calendar.hpp"

#include <array>
#include <cstdio>
#include <cstdint>

namespace squarestar::market {
namespace {

bool IsLeapYear(int year) noexcept {
    return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

int DaysInMonth(int year, int month) noexcept {
    static constexpr std::array<int, 12> days = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12)
        return 0;
    return days[static_cast<size_t>(month - 1)] +
           (month == 2 && IsLeapYear(year) ? 1 : 0);
}

CalendarDate DateFromUtc(std::time_t value) noexcept {
    std::tm calendar{};
#ifdef _WIN32
    gmtime_s(&calendar, &value);
#else
    gmtime_r(&value, &calendar);
#endif
    return {calendar.tm_year + 1900, calendar.tm_mon + 1, calendar.tm_mday};
}

struct OneOffMarketClosure {
    CalendarDate date;
    const char* reason;
};

// Exceptional closures are facts, not recurring holiday rules. Keep them as a
// short table: when another one occurs, add one row.
constexpr std::array<OneOffMarketClosure, 9> kOneOffMarketClosures = {{
    {{2001, 9, 11}, "September 11 market closure"},
    {{2001, 9, 12}, "September 11 market closure"},
    {{2001, 9, 13}, "September 11 market closure"},
    {{2001, 9, 14}, "September 11 market closure"},
    {{2004, 6, 11}, "National Day of Mourning (Ronald Reagan)"},
    {{2007, 1, 2}, "National Day of Mourning (Gerald Ford)"},
    {{2012, 10, 29}, "Hurricane Sandy"},
    {{2012, 10, 30}, "Hurricane Sandy"},
    {{2018, 12, 5}, "National Day of Mourning (George H. W. Bush)"},
}};

std::time_t EasternDstStartUtc(int year) noexcept {
    std::tm transition{};
    transition.tm_year = year - 1900;
    if (year == 1974) {
        transition.tm_mon = 0;
        transition.tm_mday = 6;
    } else if (year == 1975) {
        transition.tm_mon = 1;
        transition.tm_mday = 23;
    } else if (year >= 2007) {
        transition.tm_mon = 2;
        transition.tm_mday = NthWeekdayOfMonth(year, 3, 2, 0);
    } else if (year >= 1987) {
        transition.tm_mon = 3;
        transition.tm_mday = NthWeekdayOfMonth(year, 4, 1, 0);
    } else {
        transition.tm_mon = 3;
        transition.tm_mday = LastWeekdayOfMonth(year, 4, 0);
    }
    transition.tm_hour = 7; // 02:00 EST
    return UtcTmToTimeT(transition);
}

std::time_t EasternDstEndUtc(int year) noexcept {
    std::tm transition{};
    transition.tm_year = year - 1900;
    if (year >= 2007) {
        transition.tm_mon = 10;
        transition.tm_mday = NthWeekdayOfMonth(year, 11, 1, 0);
    } else {
        transition.tm_mon = 9;
        transition.tm_mday = LastWeekdayOfMonth(year, 10, 0);
    }
    transition.tm_hour = 6; // 02:00 EDT
    return UtcTmToTimeT(transition);
}

} // namespace

int GregorianWeekday(int year, int month, int day) noexcept {
    static constexpr std::array<int, 12> offsets = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (month < 1 || month > 12 || day < 1 || day > DaysInMonth(year, month))
        return -1;
    if (month < 3)
        --year;
    const std::size_t offsetIndex = static_cast<std::size_t>(month - 1);
    return (year + year / 4 - year / 100 + year / 400 + offsets[offsetIndex] + day) % 7;
}

int NthWeekdayOfMonth(int year, int month, int nth, int weekday) noexcept {
    if (nth < 1 || weekday < 0 || weekday > 6)
        return 0;
    const int firstWeekday = GregorianWeekday(year, month, 1);
    if (firstWeekday < 0)
        return 0;
    const int result = 1 + (weekday - firstWeekday + 7) % 7 + (nth - 1) * 7;
    return result <= DaysInMonth(year, month) ? result : 0;
}

int LastWeekdayOfMonth(int year, int month, int weekday) noexcept {
    const int lastDay = DaysInMonth(year, month);
    const int lastWeekday = GregorianWeekday(year, month, lastDay);
    if (lastDay == 0 || lastWeekday < 0 || weekday < 0 || weekday > 6)
        return 0;
    return lastDay - (lastWeekday - weekday + 7) % 7;
}

CalendarDate GoodFridayDate(int year) noexcept {
    const int a = year % 19;
    const int b = year / 100;
    const int c = year % 100;
    const int d = b / 4;
    const int e = b % 4;
    const int f = (b + 8) / 25;
    const int g = (b - f + 1) / 3;
    const int h = (19 * a + b - d - g + 15) % 30;
    const int i = c / 4;
    const int k = c % 4;
    const int l = (32 + 2 * e + 2 * i - h - k) % 7;
    const int m = (a + 11 * h + 22 * l) / 451;
    std::tm easter{};
    easter.tm_year = year - 1900;
    easter.tm_mon = (h + l - 7 * m + 114) / 31 - 1;
    easter.tm_mday = ((h + l - 7 * m + 114) % 31) + 1;
    const std::time_t goodFriday = UtcTmToTimeT(easter) - 2 * 24 * 60 * 60;
    return DateFromUtc(goodFriday);
}

std::string GetMarketClosedReason(int year, int month, int day) {
    const int weekday = GregorianWeekday(year, month, day);
    if (weekday < 0)
        return "Invalid date";
    if (weekday == 0 || weekday == 6)
        return "Weekend";
    const CalendarDate date{year, month, day};
    for (const auto& closure : kOneOffMarketClosures) {
        if (closure.date == date)
            return closure.reason;
    }
    if ((month == 1 && day == 1) || (month == 1 && day == 2 && weekday == 1))
        return "New Year's Day";
    if (year >= 2022 &&
        ((month == 6 && day == 19) || (month == 6 && day == 20 && weekday == 1) ||
         (month == 6 && day == 18 && weekday == 5)))
        return "Juneteenth National Independence Day";
    if ((month == 7 && day == 4) || (month == 7 && day == 5 && weekday == 1) ||
        (month == 7 && day == 3 && weekday == 5))
        return "Independence Day";
    if ((month == 12 && day == 25) || (month == 12 && day == 26 && weekday == 1) ||
        (month == 12 && day == 24 && weekday == 5))
        return "Christmas Day";
    if (month == 1 && weekday == 1 && day == NthWeekdayOfMonth(year, 1, 3, 1))
        return "Martin Luther King Jr. Day";
    if (month == 2 && weekday == 1 && day == NthWeekdayOfMonth(year, 2, 3, 1))
        return "Washington's Birthday";
    if (month == 5 && weekday == 1 && day == LastWeekdayOfMonth(year, 5, 1))
        return "Memorial Day";
    if (month == 9 && weekday == 1 && day == NthWeekdayOfMonth(year, 9, 1, 1))
        return "Labor Day";
    if (month == 11 && weekday == 4 && day == NthWeekdayOfMonth(year, 11, 4, 4))
        return "Thanksgiving Day";
    if (CalendarDate{year, month, day} == GoodFridayDate(year))
        return "Good Friday";
    return {};
}

bool IsMarketTradingDay(int year, int month, int day) {
    return GetMarketClosedReason(year, month, day).empty();
}

int MarketCloseMinutesForDate(int year, int month, int day) {
    if (!IsMarketTradingDay(year, month, day))
        return 0;

    // Current NYSE recurring early-close pattern for the core equities session:
    // July 3 when it is itself a trading day, the Friday after Thanksgiving,
    // and Christmas Eve when it is itself a trading day. A holiday/weekend
    // check above takes precedence (for example July 3, 2026 is fully closed).
    const int weekday = GregorianWeekday(year, month, day);
    const int thanksgiving = NthWeekdayOfMonth(year, 11, 4, 4);
    const bool dayBeforeIndependenceDay = month == 7 && day == 3;
    const bool dayAfterThanksgiving =
        month == 11 && weekday == 5 && day == thanksgiving + 1;
    const bool christmasEve = month == 12 && day == 24;
    if (dayBeforeIndependenceDay || dayAfterThanksgiving || christmasEve)
        return 13 * 60;
    return 16 * 60;
}

bool IsUsEasternDstLocal(int year, int month, int day, int hour) noexcept {
    if (year < 1967)
        return false;
    int startMonth = 4;
    int startDay = LastWeekdayOfMonth(year, 4, 0);
    if (year == 1974) {
        startMonth = 1;
        startDay = 6;
    } else if (year == 1975) {
        startMonth = 2;
        startDay = 23;
    } else if (year >= 2007) {
        startMonth = 3;
        startDay = NthWeekdayOfMonth(year, 3, 2, 0);
    } else if (year >= 1987) {
        startDay = NthWeekdayOfMonth(year, 4, 1, 0);
    }
    const int endMonth = year >= 2007 ? 11 : 10;
    const int endDay = year >= 2007 ? NthWeekdayOfMonth(year, 11, 1, 0)
                                    : LastWeekdayOfMonth(year, 10, 0);
    if (month < startMonth || month > endMonth)
        return false;
    if (month > startMonth && month < endMonth)
        return true;
    if (month == startMonth)
        return day > startDay || (day == startDay && hour >= 2);
    return day < endDay || (day == endDay && hour < 2);
}

int NewYorkUtcOffsetSeconds(std::time_t utcTime) noexcept {
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &utcTime);
#else
    gmtime_r(&utcTime, &utc);
#endif
    const int year = utc.tm_year + 1900;
    if (year < 1967)
        return -5 * 60 * 60;
    return utcTime >= EasternDstStartUtc(year) && utcTime < EasternDstEndUtc(year)
               ? -4 * 60 * 60
               : -5 * 60 * 60;
}

std::time_t UtcTmToTimeT(std::tm value) noexcept {
#ifdef _WIN32
    return _mkgmtime(&value);
#else
    return timegm(&value);
#endif
}

std::tm CurrentNewYorkTime(std::time_t utcTime) noexcept {
    const std::time_t localWall = utcTime + NewYorkUtcOffsetSeconds(utcTime);
    std::tm result{};
#ifdef _WIN32
    gmtime_s(&result, &localWall);
#else
    gmtime_r(&localWall, &result);
#endif
    return result;
}

bool IsMarketOpenAt(std::time_t utcTime) {
    const std::tm local = CurrentNewYorkTime(utcTime);
    const int minutes = local.tm_hour * 60 + local.tm_min;
    const int closeMinutes =
        MarketCloseMinutesForDate(local.tm_year + 1900, local.tm_mon + 1, local.tm_mday);
    return closeMinutes > 0 && minutes >= 9 * 60 + 30 && minutes < closeMinutes;
}

bool IsMarketOpeningWindowAt(std::time_t utcTime) {
    if (!IsMarketOpenAt(utcTime))
        return false;
    const std::tm local = CurrentNewYorkTime(utcTime);
    return local.tm_hour * 60 + local.tm_min < 10 * 60 + 30;
}

std::time_t NextMarketOpenAt(std::time_t utcTime) {
    std::tm candidate = CurrentNewYorkTime(utcTime);
    const int minutes = candidate.tm_hour * 60 + candidate.tm_min;
    if (!IsMarketTradingDay(candidate.tm_year + 1900,
                            candidate.tm_mon + 1,
                            candidate.tm_mday) ||
        minutes >= 9 * 60 + 30) {
        do {
            candidate.tm_hour = 12;
            candidate.tm_min = 0;
            candidate.tm_sec = 0;
            const std::time_t nextDay = UtcTmToTimeT(candidate) + 24 * 60 * 60;
#ifdef _WIN32
            gmtime_s(&candidate, &nextDay);
#else
            gmtime_r(&nextDay, &candidate);
#endif
        } while (!IsMarketTradingDay(candidate.tm_year + 1900,
                                     candidate.tm_mon + 1,
                                     candidate.tm_mday));
    }
    candidate.tm_hour = 9;
    candidate.tm_min = 30;
    candidate.tm_sec = 0;
    const int offset = IsUsEasternDstLocal(candidate.tm_year + 1900,
                                           candidate.tm_mon + 1,
                                           candidate.tm_mday,
                                           candidate.tm_hour)
                           ? -4 * 60 * 60
                           : -5 * 60 * 60;
    return UtcTmToTimeT(candidate) - offset;
}

std::time_t NextMarketSettlementAt(std::time_t utcTime) {
    std::tm candidate = CurrentNewYorkTime(utcTime);
    const int minutes = candidate.tm_hour * 60 + candidate.tm_min;
    int closeMinutes =
        MarketCloseMinutesForDate(candidate.tm_year + 1900,
                                  candidate.tm_mon + 1,
                                  candidate.tm_mday);
    if (closeMinutes == 0 || minutes >= closeMinutes) {
        do {
            candidate.tm_hour = 12;
            candidate.tm_min = 0;
            candidate.tm_sec = 0;
            const std::time_t nextDay = UtcTmToTimeT(candidate) + 24 * 60 * 60;
#ifdef _WIN32
            gmtime_s(&candidate, &nextDay);
#else
            gmtime_r(&nextDay, &candidate);
#endif
            closeMinutes =
                MarketCloseMinutesForDate(candidate.tm_year + 1900,
                                          candidate.tm_mon + 1,
                                          candidate.tm_mday);
        } while (closeMinutes == 0);
    }
    candidate.tm_hour = closeMinutes / 60;
    candidate.tm_min = closeMinutes % 60;
    candidate.tm_sec = 0;
    const int offset = IsUsEasternDstLocal(candidate.tm_year + 1900,
                                           candidate.tm_mon + 1,
                                           candidate.tm_mday,
                                           candidate.tm_hour)
                           ? -4 * 60 * 60
                           : -5 * 60 * 60;
    return UtcTmToTimeT(candidate) - offset;
}

std::string MarketSettlementTimeLabel(std::time_t utcTime) {
    const std::tm settlement = CurrentNewYorkTime(NextMarketSettlementAt(utcTime));
    const int hour = settlement.tm_hour > 12 ? settlement.tm_hour - 12
                                             : settlement.tm_hour;
    char label[32]{};
    std::snprintf(label,
                  sizeof(label),
                  "%d:%02d %s ET",
                  hour,
                  settlement.tm_min,
                  settlement.tm_hour >= 12 ? "PM" : "AM");
    return label;
}

} // namespace squarestar::market
