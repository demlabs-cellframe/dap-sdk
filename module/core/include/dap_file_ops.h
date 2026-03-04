/*
 * DAP File Operations - POSIX compatibility layer
 *
 * Provides pread/pwrite/fsync on platforms that lack them (Windows/MinGW).
 * Include AFTER dap_common.h (needs uint64_t, DAP_OS_WINDOWS, ssize_t).
 */

#pragma once

#ifdef DAP_OS_WINDOWS
#include <io.h>
#include <windows.h>

// pread: atomic positioned read without changing file offset
static inline ssize_t dap_pread(int fd, void *buf, size_t count, off_t offset)
{
    HANDLE h = (HANDLE)_get_osfhandle(fd);
    OVERLAPPED ov = {0};
    ov.Offset     = (DWORD)(offset & 0xFFFFFFFF);
    ov.OffsetHigh = (DWORD)((uint64_t)offset >> 32);
    DWORD bytes_read = 0;
    if (!ReadFile(h, buf, (DWORD)count, &bytes_read, &ov))
        return -1;
    return (ssize_t)bytes_read;
}

// pwrite: atomic positioned write without changing file offset
static inline ssize_t dap_pwrite(int fd, const void *buf, size_t count, off_t offset)
{
    HANDLE h = (HANDLE)_get_osfhandle(fd);
    OVERLAPPED ov = {0};
    ov.Offset     = (DWORD)(offset & 0xFFFFFFFF);
    ov.OffsetHigh = (DWORD)((uint64_t)offset >> 32);
    DWORD bytes_written = 0;
    if (!WriteFile(h, buf, (DWORD)count, &bytes_written, &ov))
        return -1;
    return (ssize_t)bytes_written;
}

// fsync: flush file data to disk
static inline int dap_fsync(int fd)
{
    HANDLE h = (HANDLE)_get_osfhandle(fd);
    return FlushFileBuffers(h) ? 0 : -1;
}

#define pread   dap_pread
#define pwrite  dap_pwrite
#define fsync   dap_fsync

#else
#include <unistd.h>
#endif
