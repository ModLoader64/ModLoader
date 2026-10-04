// write() to 1 and 2 goes to the log; there are no other descriptors
#ifndef MODLOADER_LIBC_UNISTD_H
#define MODLOADER_LIBC_UNISTD_H

#include <stddef.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

// What POSIX features exist: threads (pthread.h) and clock_gettime with a monotonic clock
#define _POSIX_THREADS 200809L
#define _POSIX_TIMERS 200809L
#define _POSIX_MONOTONIC_CLOCK 200809L

#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

#define _SC_PAGESIZE 30
#define _SC_PAGE_SIZE _SC_PAGESIZE
#define _SC_NPROCESSORS_CONF 83
#define _SC_NPROCESSORS_ONLN 84

ssize_t write(int descriptor, const void* data, size_t size);
ssize_t read(int descriptor, void* buffer, size_t size);
int close(int descriptor);
int isatty(int descriptor);
unsigned int sleep(unsigned int seconds);
int usleep(useconds_t microseconds); // Blocks the calling thread; truncated to whole milliseconds
pid_t getpid(void);
long sysconf(int name);
int getpagesize(void);

#ifdef __cplusplus
}
#endif

#endif
