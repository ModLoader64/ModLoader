#ifndef MODLOADER_LIBC_SCHED_H
#define MODLOADER_LIBC_SCHED_H

#ifdef __cplusplus
extern "C" {
#endif

struct sched_param {
    int sched_priority;
};

int sched_yield(void);

#ifdef __cplusplus
}
#endif

#endif
