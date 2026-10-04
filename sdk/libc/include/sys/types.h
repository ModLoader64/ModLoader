// WASM-side POSIX types
#ifndef MODLOADER_LIBC_SYS_TYPES_H
#define MODLOADER_LIBC_SYS_TYPES_H

#include <stddef.h>
#include <stdint.h>

typedef long ssize_t;
typedef long long off_t;
typedef int pid_t;
typedef unsigned int uid_t;
typedef unsigned int gid_t;
typedef unsigned int mode_t;
typedef long long time_t;
typedef long long clock_t;
typedef long long useconds_t;

#endif
