// Windows (MinGW-w64 / UCRT) shims for the few POSIX calls the port layer uses.
// Force-included into every game and platform translation unit on Windows (CMakeLists.txt).
// Deliberately free of <windows.h>: its macros would collide with game names.
#pragma once
#if defined(_WIN32)

#include <direct.h>
#include <fcntl.h>
#include <io.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <pthread_time.h>
#include <unistd.h>

#ifdef __cplusplus
extern "C" {
#endif

int port_win_mkdir(const char* path, int mode);
long long port_win_pread(int fd, void* buf, size_t count, long long offset);
int port_win_rename(const char* from, const char* to);
int port_win_setenv(const char* name, const char* value, int overwrite);
int port_win_fsync(int fd);

#ifdef __cplusplus
}
#endif

#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif

// mkdir(path, mode): MSVCRT's mkdir takes no mode.
#define mkdir(path, mode) port_win_mkdir((path), (mode))
#define pread port_win_pread
#define fsync port_win_fsync
#define setenv port_win_setenv
#define lstat stat

// rename(a, b) replaces an existing target as POSIX does. Calls with any other argument
// count (a struct's `f.rename(ctx, a, b)` member) are left alone: inside its own
// expansion the name `rename` is not expanded again.
#define PORT_WIN_RENAME_PICK(_1, _2, _3, _4, name, ...) name
#define rename(...) PORT_WIN_RENAME_PICK(__VA_ARGS__, rename, rename, port_win_rename, rename, rename)(__VA_ARGS__)

#endif  // _WIN32
