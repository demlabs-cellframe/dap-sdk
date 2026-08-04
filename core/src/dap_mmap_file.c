/*
 * Reusable memory-mapped file utility for persistent storage.
 *
 * Copyright (c) 2025
 * This file is part of DAP (Distributed Applications Platform).
 * Licensed under GNU General Public License v3.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#ifdef _WIN32
# include <windows.h>
# include <io.h>
# include <stdint.h>
#else
# include <unistd.h>
# include <sys/mman.h>
#endif

#include "dap_common.h"
#include "dap_mmap_file.h"

#define LOG_TAG "dap_mmap_file"

/* Minimum mmap size: 1 page (usually 4KB). Grows by doubling. */
#define MMAP_MIN_SIZE 4096

#ifdef _WIN32
# define DAP_MMAP_FAILED NULL
#else
# define DAP_MMAP_FAILED MAP_FAILED
#endif

DAP_STATIC_INLINE int s_ftruncate(int fd, uint64_t size)
{
#ifdef _WIN32
    return _chsize_s(fd, (__int64)size) == 0 ? 0 : -1;
#else
    return ftruncate(fd, (off_t)size);
#endif
}

DAP_STATIC_INLINE uint64_t s_page_roundup(uint64_t x) {
#ifdef _WIN32
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    long ps = (long)si.dwPageSize;
#else
    long ps = sysconf(_SC_PAGESIZE);
#endif
    if (ps <= 0) ps = 4096;
    return (x + ps - 1) & ~((uint64_t)ps - 1);
}

#ifdef _WIN32
static void s_unmap(dap_mmap_file_t *mf)
{
    if (mf->map) {
        UnmapViewOfFile(mf->map);
        mf->map = NULL;
    }
    if (mf->mapping) {
        CloseHandle((HANDLE)mf->mapping);
        mf->mapping = NULL;
    }
}

static char *s_map_view(dap_mmap_file_t *mf, uint64_t map_size)
{
    HANDLE hfile = (HANDLE)_get_osfhandle(mf->fd);
    if (hfile == INVALID_HANDLE_VALUE) {
        log_it(L_ERROR, "Cannot get OS handle for mmap file \"%s\"", mf->path);
        return NULL;
    }

    DWORD protect = mf->read_only ? PAGE_READONLY : PAGE_READWRITE;
    DWORD access = mf->read_only ? FILE_MAP_READ : (FILE_MAP_READ | FILE_MAP_WRITE);
    DWORD size_hi = (DWORD)(map_size >> 32);
    DWORD size_lo = (DWORD)(map_size & 0xffffffffu);

    HANDLE mapping = CreateFileMappingA(hfile, NULL, protect, size_hi, size_lo, NULL);
    if (!mapping) {
        log_it(L_ERROR, "Cannot create file mapping for \"%s\" (size %" DAP_UINT64_FORMAT_U "): err=%lu",
               mf->path, map_size, (unsigned long)GetLastError());
        return NULL;
    }

    char *map = (char *)MapViewOfFile(mapping, access, 0, 0, (SIZE_T)map_size);
    if (!map) {
        log_it(L_ERROR, "Cannot map view of \"%s\" (size %" DAP_UINT64_FORMAT_U "): err=%lu",
               mf->path, map_size, (unsigned long)GetLastError());
        CloseHandle(mapping);
        return NULL;
    }

    mf->mapping = mapping;
    return map;
}

static int s_sync_map(dap_mmap_file_t *mf)
{
    if (!mf->map)
        return -1;
    if (!FlushViewOfFile(mf->map, (SIZE_T)mf->map_size)) {
        log_it(L_ERROR, "FlushViewOfFile(\"%s\") failed: err=%lu",
               mf->path, (unsigned long)GetLastError());
        return -1;
    }
    return 0;
}
#endif

dap_mmap_file_t *dap_mmap_file_open(const char *path, bool create, bool read_only)
{
    if (!path)
        return NULL;

    int flags = read_only ? O_RDONLY : O_RDWR;
    if (create)
        flags |= O_CREAT;
#ifdef _WIN32
    flags |= O_BINARY;
#endif

    int fd = open(path, flags, 0644);
    if (fd < 0) {
        log_it(L_ERROR, "Cannot open mmap file \"%s\": %s", path, dap_strerror(errno));
        return NULL;
    }

    struct stat st;
    if (fstat(fd, &st) < 0) {
        log_it(L_ERROR, "Cannot stat mmap file \"%s\": %s", path, dap_strerror(errno));
        close(fd);
        return NULL;
    }

    uint64_t file_size = (uint64_t)st.st_size;
    uint64_t map_size = s_page_roundup(file_size < MMAP_MIN_SIZE ? MMAP_MIN_SIZE : file_size);

    /* Ensure file is at least map_size for mmap */
    if ((uint64_t)st.st_size < map_size) {
        if (s_ftruncate(fd, map_size) < 0) {
            log_it(L_ERROR, "Cannot truncate mmap file \"%s\" to %" DAP_UINT64_FORMAT_U ": %s",
                   path, map_size, dap_strerror(errno));
            close(fd);
            return NULL;
        }
    }

    dap_mmap_file_t *mf = DAP_NEW_Z(dap_mmap_file_t);
    if (!mf) {
        close(fd);
        return NULL;
    }

    mf->fd = fd;
    mf->map_size = map_size;
    mf->file_size = file_size;
    strncpy(mf->path, path, sizeof(mf->path) - 1);
    mf->read_only = read_only;

#ifdef _WIN32
    mf->map = s_map_view(mf, map_size);
    if (!mf->map) {
        close(fd);
        DAP_DELETE(mf);
        return NULL;
    }
#else
    int prot = PROT_READ;
    if (!read_only)
        prot |= PROT_WRITE;

    char *map = mmap(NULL, map_size, prot, MAP_SHARED, fd, 0);
    if (map == MAP_FAILED) {
        log_it(L_ERROR, "Cannot mmap file \"%s\" (size %" DAP_UINT64_FORMAT_U "): %s",
               path, map_size, dap_strerror(errno));
        close(fd);
        DAP_DELETE(mf);
        return NULL;
    }
    mf->map = map;
#endif

    log_it(L_INFO, "Opened mmap file \"%s\": file_size=%" DAP_UINT64_FORMAT_U
           " map_size=%" DAP_UINT64_FORMAT_U " %s",
           path, file_size, map_size, read_only ? "RO" : "RW");

    return mf;
}

void dap_mmap_file_close(dap_mmap_file_t *mf)
{
    if (!mf)
        return;

    if (mf->map && mf->map != DAP_MMAP_FAILED) {
        if (!mf->read_only) {
#ifdef _WIN32
            s_sync_map(mf);
#else
            msync(mf->map, mf->map_size, MS_SYNC);
#endif
        }
#ifdef _WIN32
        s_unmap(mf);
#else
        munmap(mf->map, mf->map_size);
#endif
    }

    if (mf->fd >= 0)
        close(mf->fd);

    DAP_DELETE(mf);
}

void *dap_mmap_file_ptr(dap_mmap_file_t *mf)
{
    return mf ? mf->map : NULL;
}

uint64_t dap_mmap_file_size(dap_mmap_file_t *mf)
{
    return mf ? mf->file_size : 0;
}

int dap_mmap_file_resize(dap_mmap_file_t *mf, uint64_t new_size)
{
    if (!mf || new_size < mf->file_size)
        return -1;

    uint64_t new_map_size = s_page_roundup(new_size);
    if (new_map_size <= mf->map_size) {
        /* Just update logical size, no remap needed */
        mf->file_size = new_size;
        return 0;
    }

    /* Sync old mapping */
    if (!mf->read_only) {
#ifdef _WIN32
        s_sync_map(mf);
#else
        msync(mf->map, mf->map_size, MS_SYNC);
#endif
    }
#ifdef _WIN32
    s_unmap(mf);
#else
    munmap(mf->map, mf->map_size);
#endif

    /* Extend file */
    if (s_ftruncate(mf->fd, new_map_size) < 0) {
        log_it(L_ERROR, "Cannot resize mmap file \"%s\" to %" DAP_UINT64_FORMAT_U ": %s",
               mf->path, new_map_size, dap_strerror(errno));
        mf->map = DAP_MMAP_FAILED;
        return -1;
    }

#ifdef _WIN32
    char *new_map = s_map_view(mf, new_map_size);
    if (!new_map) {
        mf->map = DAP_MMAP_FAILED;
        return -1;
    }
#else
    int prot = PROT_READ;
    if (!mf->read_only)
        prot |= PROT_WRITE;

    char *new_map = mmap(NULL, new_map_size, prot, MAP_SHARED, mf->fd, 0);
    if (new_map == MAP_FAILED) {
        log_it(L_ERROR, "Cannot remap file \"%s\" (size %" DAP_UINT64_FORMAT_U "): %s",
               mf->path, new_map_size, dap_strerror(errno));
        mf->map = MAP_FAILED;
        return -1;
    }
#endif

    mf->map = new_map;
    mf->map_size = new_map_size;
    mf->file_size = new_size;

    return 0;
}

ssize_t dap_mmap_file_append(dap_mmap_file_t *mf, const void *data, size_t size)
{
    if (!mf || !data || size == 0 || mf->read_only)
        return -1;

    uint64_t offset = mf->file_size;
    uint64_t needed = offset + size;

    if (needed > mf->map_size) {
        /* Double until we have enough room */
        uint64_t new_map_size = mf->map_size;
        while (new_map_size < needed)
            new_map_size *= 2;

        if (dap_mmap_file_resize(mf, needed) < 0)
            return -1;
        /* resize updated file_size to needed, but we only want offset+size */
    } else {
        mf->file_size = needed;
    }

    memcpy(mf->map + offset, data, size);

    return (ssize_t)offset;
}

int dap_mmap_file_sync(dap_mmap_file_t *mf)
{
    if (!mf || !mf->map || mf->map == DAP_MMAP_FAILED)
        return -1;
#ifdef _WIN32
    return s_sync_map(mf);
#else
    return msync(mf->map, mf->map_size, MS_SYNC);
#endif
}
