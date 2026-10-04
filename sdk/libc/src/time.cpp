#include "internal.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

namespace {

constexpr long long gSecondsPerDay = 86400;

thread_local tm sTime;
thread_local char sText[32];

const char* const gDays[7] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
const char* const gMonths[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
const char* const gDayNames[7] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };
const char* const gMonthNames[12] = {
    "January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"
};

// Days since 1970-01-01
long long Days_From_Civil(long long year, unsigned month, unsigned day) {
    unsigned year_of_era;
    unsigned day_of_year;
    unsigned day_of_era;

    year -= month <= 2 ? 1 : 0;
    long long era = (year >= 0 ? year : year - 399) / 400;
    year_of_era = static_cast<unsigned>(year - era * 400);
    day_of_year = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    day_of_era = year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
    return era * 146097 + static_cast<long long>(day_of_era) - 719468;
}

void Civil_From_Days(long long days, long long* out_year, unsigned* out_month, unsigned* out_day) {
    unsigned day_of_era;
    unsigned year_of_era;
    unsigned day_of_year;
    unsigned month_index;

    days += 719468;
    long long era = (days >= 0 ? days : days - 146096) / 146097;
    day_of_era = static_cast<unsigned>(days - era * 146097);
    year_of_era = (day_of_era - day_of_era / 1460 + day_of_era / 36524 - day_of_era / 146096) / 365;
    long long year = static_cast<long long>(year_of_era) + era * 400;
    day_of_year = day_of_era - (365 * year_of_era + year_of_era / 4 - year_of_era / 100);
    month_index = (5 * day_of_year + 2) / 153;
    *out_day = day_of_year - (153 * month_index + 2) / 5 + 1;
    *out_month = month_index < 10 ? month_index + 3 : month_index - 9;
    *out_year = year + (*out_month <= 2 ? 1 : 0);
}

} // namespace

extern "C" {

time_t time(time_t* out_time) {
    time_t now = static_cast<time_t>(ModLoader_Host_Time_Now(Libc::gClockRealtime) / 1000000);

    if (out_time != nullptr) {
        *out_time = now;
    }
    return now;
}

clock_t clock(void) {
    static uint64_t sStart = ModLoader_Host_Time_Now(Libc::gClockMonotonic);
    return static_cast<clock_t>(ModLoader_Host_Time_Now(Libc::gClockMonotonic) - sStart);
}

int gettimeofday(timeval* __restrict out_time, void* __restrict) {
    uint64_t microseconds = ModLoader_Host_Time_Now(Libc::gClockRealtime);

    out_time->tv_sec = static_cast<time_t>(microseconds / 1000000);
    out_time->tv_usec = static_cast<long>(microseconds % 1000000);
    return 0;
}

double difftime(time_t end, time_t start) {
    return static_cast<double>(end - start);
}

int clock_gettime(clockid_t clock_id, timespec* out_time) {
    uint64_t microseconds;

    if (clock_id == CLOCK_REALTIME) {
        microseconds = ModLoader_Host_Time_Now(Libc::gClockRealtime);
    }
    else if (clock_id == CLOCK_MONOTONIC || clock_id == CLOCK_PROCESS_CPUTIME_ID || clock_id == CLOCK_THREAD_CPUTIME_ID) {
        microseconds = ModLoader_Host_Time_Now(Libc::gClockMonotonic);
    }
    else {
        errno = EINVAL;
        return -1;
    }
    out_time->tv_sec = static_cast<time_t>(microseconds / 1000000);
    out_time->tv_nsec = static_cast<long>(microseconds % 1000000) * 1000;
    return 0;
}

int clock_getres(clockid_t, timespec* out_resolution) {
    if (out_resolution != nullptr) {
        out_resolution->tv_sec = 0;
        out_resolution->tv_nsec = 1000;
    }
    return 0;
}

int timespec_get(timespec* out_time, int base) {
    if (base != TIME_UTC || clock_gettime(CLOCK_REALTIME, out_time) != 0) {
        return 0;
    }
    return base;
}

int nanosleep(const timespec* duration, timespec* out_remaining) {
    long long milliseconds = duration->tv_sec * 1000 + duration->tv_nsec / 1000000;
    ModLoader_Host_Thread_Sleep(static_cast<uint32_t>(milliseconds > 0 ? milliseconds : 0));
    if (out_remaining != nullptr) {
        out_remaining->tv_sec = 0;
        out_remaining->tv_nsec = 0;
    }
    return 0;
}

tm* gmtime_r(const time_t* __restrict time_value, tm* __restrict out_time) {
    unsigned month;
    unsigned day;

    long long seconds = *time_value;
    long long days = seconds / gSecondsPerDay;
    long long remainder = seconds % gSecondsPerDay;
    if (remainder < 0) {
        remainder += gSecondsPerDay;
        days--;
    }
    long long year;
    Civil_From_Days(days, &year, &month, &day);
    out_time->tm_sec = static_cast<int>(remainder % 60);
    out_time->tm_min = static_cast<int>(remainder / 60 % 60);
    out_time->tm_hour = static_cast<int>(remainder / 3600);
    out_time->tm_mday = static_cast<int>(day);
    out_time->tm_mon = static_cast<int>(month) - 1;
    out_time->tm_year = static_cast<int>(year - 1900);
    long long weekday = (days + 4) % 7; // 1970-01-01 was a Thursday
    out_time->tm_wday = static_cast<int>(weekday < 0 ? weekday + 7 : weekday);
    out_time->tm_yday = static_cast<int>(days - Days_From_Civil(year, 1, 1));
    out_time->tm_isdst = 0;
    return out_time;
}

tm* gmtime(const time_t* time_value) {
    return gmtime_r(time_value, &sTime);
}

tm* localtime_r(const time_t* __restrict time_value, tm* __restrict out_time) {
    return gmtime_r(time_value, out_time);
}

tm* localtime(const time_t* time_value) {
    return gmtime_r(time_value, &sTime);
}

time_t timegm(tm* time_value) {
    int month;
    time_t result;

    // Normalize months first
    long long year = time_value->tm_year + 1900LL + time_value->tm_mon / 12;
    month = time_value->tm_mon % 12;
    if (month < 0) {
        month += 12;
        year--;
    }
    long long days = Days_From_Civil(year, static_cast<unsigned>(month + 1), 1) + time_value->tm_mday - 1;
    result = days * gSecondsPerDay + time_value->tm_hour * 3600LL + time_value->tm_min * 60LL + time_value->tm_sec;
    gmtime_r(&result, time_value);
    return result;
}

time_t mktime(tm* time_value) {
    return timegm(time_value);
}

size_t strftime(char* __restrict buffer, size_t size, const char* __restrict format, const tm* __restrict time_value) {
    size_t length = 0;
    auto append = [&](const char* text) {
        size_t text_length = strlen(text);

        if (length + text_length < size) {
            memcpy(buffer + length, text, text_length);
        }
        length += text_length;
    };
    char field[64];

    for (const char* cursor = format; *cursor != '\0'; cursor++) {
        if (*cursor != '%' || cursor[1] == '\0') {
            char single[2] = { *cursor, '\0' };

            append(single);
            continue;
        }
        cursor++;
        long long year = time_value->tm_year + 1900LL;
        switch (*cursor) {
        case 'a':
            append(gDays[time_value->tm_wday % 7]);
            break;
        case 'A':
            append(gDayNames[time_value->tm_wday % 7]);
            break;
        case 'b':
        case 'h':
            append(gMonths[time_value->tm_mon % 12]);
            break;
        case 'B':
            append(gMonthNames[time_value->tm_mon % 12]);
            break;
        case 'c':
            snprintf(
                field,
                sizeof(field),
                "%s %s %2d %02d:%02d:%02d %lld",
                gDays[time_value->tm_wday % 7],
                gMonths[time_value->tm_mon % 12],
                time_value->tm_mday,
                time_value->tm_hour,
                time_value->tm_min,
                time_value->tm_sec,
                year
            );
            append(field);
            break;
        case 'd':
            snprintf(field, sizeof(field), "%02d", time_value->tm_mday);
            append(field);
            break;
        case 'e':
            snprintf(field, sizeof(field), "%2d", time_value->tm_mday);
            append(field);
            break;
        case 'F':
            snprintf(field, sizeof(field), "%lld-%02d-%02d", year, time_value->tm_mon + 1, time_value->tm_mday);
            append(field);
            break;
        case 'H':
            snprintf(field, sizeof(field), "%02d", time_value->tm_hour);
            append(field);
            break;
        case 'I':
            snprintf(field, sizeof(field), "%02d", time_value->tm_hour % 12 == 0 ? 12 : time_value->tm_hour % 12);
            append(field);
            break;
        case 'j':
            snprintf(field, sizeof(field), "%03d", time_value->tm_yday + 1);
            append(field);
            break;
        case 'm':
            snprintf(field, sizeof(field), "%02d", time_value->tm_mon + 1);
            append(field);
            break;
        case 'M':
            snprintf(field, sizeof(field), "%02d", time_value->tm_min);
            append(field);
            break;
        case 'n':
            append("\n");
            break;
        case 'p':
            append(time_value->tm_hour < 12 ? "AM" : "PM");
            break;
        case 'S':
            snprintf(field, sizeof(field), "%02d", time_value->tm_sec);
            append(field);
            break;
        case 't':
            append("\t");
            break;
        case 'T':
            snprintf(field, sizeof(field), "%02d:%02d:%02d", time_value->tm_hour, time_value->tm_min, time_value->tm_sec);
            append(field);
            break;
        case 'u':
            snprintf(field, sizeof(field), "%d", time_value->tm_wday == 0 ? 7 : time_value->tm_wday);
            append(field);
            break;
        case 'w':
            snprintf(field, sizeof(field), "%d", time_value->tm_wday);
            append(field);
            break;
        case 'y':
            snprintf(field, sizeof(field), "%02lld", year % 100);
            append(field);
            break;
        case 'Y':
            snprintf(field, sizeof(field), "%lld", year);
            append(field);
            break;
        case 'z':
            append("+0000");
            break;
        case 'Z':
            append("UTC");
            break;
        case '%':
            append("%");
            break;
        default: {
            char unknown[3] = { '%', *cursor, '\0' };

            append(unknown);
            break;
        }
        }
    }
    
    if (length >= size) {
        if (size != 0) {
            buffer[0] = '\0';
        }
        return 0;
    }
    buffer[length] = '\0';
    return length;
}

char* asctime(const tm* time_value) {
    snprintf(
        sText,
        sizeof(sText),
        "%s %s %2d %02d:%02d:%02d %d\n",
        gDays[time_value->tm_wday % 7],
        gMonths[time_value->tm_mon % 12],
        time_value->tm_mday,
        time_value->tm_hour,
        time_value->tm_min,
        time_value->tm_sec,
        time_value->tm_year + 1900
    );
    return sText;
}

char* ctime(const time_t* time_value) {
    return asctime(gmtime(time_value));
}

} // extern "C"
