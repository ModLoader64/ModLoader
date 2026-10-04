#ifndef MODLOADER_LIBC_SYS_TIME_H
#define MODLOADER_LIBC_SYS_TIME_H

#include <sys/types.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

struct timeval {
    time_t tv_sec;
    long tv_usec;
};

int gettimeofday(struct timeval* __restrict out_time, void* __restrict time_zone); // time_zone is ignored

#ifdef __cplusplus
}
#endif

#endif
