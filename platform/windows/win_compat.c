// Implementations of the POSIX shims declared in win_compat.h.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <direct.h>
#include <errno.h>
#include <io.h>
#include <stdlib.h>

int port_win_mkdir(const char* path, int mode) {
    (void)mode;
    return _mkdir(path);
}

// Positional read that leaves the descriptor's file pointer alone and is safe across threads.
long long port_win_pread(int fd, void* buf, size_t count, long long offset) {
    HANDLE h = (HANDLE)_get_osfhandle(fd);
    if (h == INVALID_HANDLE_VALUE) { errno = EBADF; return -1; }
    long long total = 0;
    while (count > 0) {
        OVERLAPPED ov = {0};
        ov.Offset = (DWORD)(unsigned long long)offset;
        ov.OffsetHigh = (DWORD)((unsigned long long)offset >> 32);
        const DWORD chunk = count > 0x40000000u ? 0x40000000u : (DWORD)count;
        DWORD got = 0;
        if (!ReadFile(h, buf, chunk, &got, &ov)) {
            if (GetLastError() == ERROR_HANDLE_EOF) break;
            errno = EIO;
            return total ? total : -1;
        }
        if (got == 0) break;
        total += got;
        count -= got;
        offset += got;
        buf = (char*)buf + got;
    }
    return total;
}

int port_win_rename(const char* from, const char* to) {
    if (MoveFileExA(from, to, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) return 0;
    errno = GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND ? ENOENT : EACCES;
    return -1;
}

int port_win_setenv(const char* name, const char* value, int overwrite) {
    if (!overwrite && getenv(name)) return 0;
    return _putenv_s(name, value) == 0 ? 0 : -1;
}

int port_win_fsync(int fd) { return _commit(fd); }
