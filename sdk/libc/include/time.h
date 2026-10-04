// Calendar conversions use UTC, including localtime()/mktime()
#ifndef MODLOADER_LIBC_TIME_H
#define MODLOADER_LIBC_TIME_H

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int clockid_t;

#define CLOCKS_PER_SEC 1000000LL
#define CLOCK_REALTIME 0
#define CLOCK_MONOTONIC 1
#define CLOCK_PROCESS_CPUTIME_ID 2
#define CLOCK_THREAD_CPUTIME_ID 3
#define TIME_UTC 1

struct tm {
    int tm_sec;
    int tm_min;
    int tm_hour;
    int tm_mday;
    int tm_mon;
    int tm_year;
    int tm_wday;
    int tm_yday;
    int tm_isdst;
};

struct timespec {
    time_t tv_sec;
    long tv_nsec;
};

time_t time(time_t* out_time);
clock_t clock(void); // monotonic microseconds since the first call
double difftime(time_t end, time_t start);
time_t mktime(struct tm* time);
time_t timegm(struct tm* time);
struct tm* gmtime(const time_t* time);
struct tm* gmtime_r(const time_t* __restrict time, struct tm* __restrict out_time);
struct tm* localtime(const time_t* time);
struct tm* localtime_r(const time_t* __restrict time, struct tm* __restrict out_time);
size_t strftime(char* __restrict buffer, size_t size, const char* __restrict format, const struct tm* __restrict time);
char* asctime(const struct tm* time);
char* ctime(const time_t* time);
// clocks with microsecond resolution; PROCESS_CPUTIME_ID/THREAD_CPUTIME_ID use monotonic time too
int clock_gettime(clockid_t clock, struct timespec* out_time);
int clock_getres(clockid_t clock, struct timespec* out_resolution);
int timespec_get(struct timespec* out_time, int base);
int nanosleep(const struct timespec* duration, struct timespec* out_remaining); // Blocks this thread; duration is truncated to milliseconds

#ifdef __cplusplus
}
#endif

#endif
