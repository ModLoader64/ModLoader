// Modules SIGABRT aborts, others are ignored
#ifndef MODLOADER_LIBC_SIGNAL_H
#define MODLOADER_LIBC_SIGNAL_H

#ifdef __cplusplus
extern "C" {
#endif

typedef int sig_atomic_t;
typedef void (*sighandler_t)(int);

#define SIG_DFL ((sighandler_t)0)
#define SIG_IGN ((sighandler_t)1)
#define SIG_ERR ((sighandler_t) - 1)
#define SIGINT 2
#define SIGILL 4
#define SIGABRT 6
#define SIGFPE 8
#define SIGSEGV 11
#define SIGTERM 15

sighandler_t signal(int signal_number, sighandler_t handler);
int raise(int signal_number);

#ifdef __cplusplus
}
#endif

#endif
