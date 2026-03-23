/*
 * DAP Cross-Platform Memory-Mapped File I/O
 *
 * Platform backends:
 *   Linux/Android -- mmap() / mremap() / msync() / madvise()
 *   macOS/BSD     -- mmap() / munmap()+mmap() / msync() / madvise()
 *   Windows       -- CreateFileMapping / MapViewOfFile / FlushViewOfFile
 *   WASM          -- heap buffer (malloc/realloc) + WASMFS/OPFS file I/O
 *
 * Authors:
 * DAP SDK Team
 * Copyright (c) 2026
 */

#include "dap_mmap.h"
#include "dap_common.h"
#include "dap_strfuncs.h"

#include <string.h>
#include <errno.h>

#if defined(DAP_OS_WASM)
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#elif defined(DAP_OS_WINDOWS)
#include <windows.h>
#include <io.h>
#else
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#endif

#define LOG_TAG "dap_mmap"

#define DAP_MMAP_PAGE_SIZE 4096

#ifdef DAP_OS_WASM
#define PAGE_ALIGN_UP(sz) (((sz) + (DAP_MMAP_PAGE_SIZE - 1)) & ~((size_t)(DAP_MMAP_PAGE_SIZE - 1)))
#elif defined(DAP_OS_WINDOWS)
#define PAGE_ALIGN_UP(sz) (((sz) + 4095ULL) & ~4095ULL)
#else
static inline size_t s_page_align_up(size_t sz) {
    long ps = sysconf(_SC_PAGESIZE);
    if (ps <= 0) ps = 4096;
    return (sz + (size_t)ps - 1) & ~((size_t)ps - 1);
}
#define PAGE_ALIGN_UP(sz) s_page_align_up(sz)
#endif

#define MIN_MAP_SIZE PAGE_ALIGN_UP(1)

// ============================================================================
// Internal structure
// ============================================================================

struct dap_mmap {
    void   *base;           // Mapped memory pointer (heap-allocated on WASM)
    size_t  map_size;       // Current mapped region size (page-aligned)
    int     flags;          // DAP_MMAP_* flags from open

#if defined(DAP_OS_WASM)
    char   *file_path;      // File path for persistence (WASMFS/OPFS)
    size_t  file_size;      // Actual data size on disk (may be < map_size)
#elif defined(DAP_OS_WINDOWS)
    HANDLE  hFile;          // File handle
    HANDLE  hMapping;       // File mapping handle
#else
    int     fd;             // POSIX file descriptor
    bool    fd_owned;       // true = we opened the fd, should close it
#endif
};

// ============================================================================
// WASM implementation: heap buffer backed by WASMFS/OPFS file
// ============================================================================

#if defined(DAP_OS_WASM)

dap_mmap_t *dap_mmap_open(const char *a_path, int a_flags, size_t a_initial_size)
{
    if (!a_path) {
        log_it(L_ERROR, "NULL path");
        return NULL;
    }

    struct stat l_st;
    size_t l_file_size = 0;
    bool l_file_exists = (stat(a_path, &l_st) == 0);

    if (l_file_exists)
        l_file_size = (size_t)l_st.st_size;
    else if (!(a_flags & DAP_MMAP_CREATE)) {
        log_it(L_ERROR, "File '%s' does not exist and DAP_MMAP_CREATE not set", a_path);
        return NULL;
    }

    size_t l_map_size = a_initial_size > 0 ? a_initial_size : l_file_size;
    if (l_map_size == 0)
        l_map_size = PAGE_ALIGN_UP(1);
    l_map_size = PAGE_ALIGN_UP(l_map_size);

    void *l_base = DAP_NEW_SIZE(void, l_map_size);
    if (!l_base) {
        log_it(L_ERROR, "Failed to allocate %zu bytes for mmap emulation", l_map_size);
        return NULL;
    }
    memset(l_base, 0, l_map_size);

    if (l_file_exists && l_file_size > 0) {
        FILE *l_fp = fopen(a_path, "rb");
        if (l_fp) {
            size_t l_read_size = l_file_size < l_map_size ? l_file_size : l_map_size;
            size_t l_read = fread(l_base, 1, l_read_size, l_fp);
            fclose(l_fp);
            if (l_read != l_read_size)
                log_it(L_WARNING, "Partial read from '%s': %zu of %zu bytes", a_path, l_read, l_read_size);
        } else {
            log_it(L_WARNING, "Can't open '%s' for reading: %s", a_path, strerror(errno));
        }
    }

    dap_mmap_t *l_mmap = DAP_NEW_Z(dap_mmap_t);
    if (!l_mmap) {
        DAP_DELETE(l_base);
        return NULL;
    }

    l_mmap->base = l_base;
    l_mmap->map_size = l_map_size;
    l_mmap->flags = a_flags;
    l_mmap->file_path = dap_strdup(a_path);
    l_mmap->file_size = l_file_size;

    return l_mmap;
}

void *dap_mmap_get_ptr(dap_mmap_t *a_mmap)
{
    return a_mmap ? a_mmap->base : NULL;
}

size_t dap_mmap_get_size(dap_mmap_t *a_mmap)
{
    return a_mmap ? a_mmap->map_size : 0;
}

int dap_mmap_get_fd(dap_mmap_t *a_mmap)
{
    (void)a_mmap;
    return -1;
}

int dap_mmap_resize(dap_mmap_t *a_mmap, size_t a_new_size)
{
    if (!a_mmap || a_new_size == 0) return -1;

    a_new_size = PAGE_ALIGN_UP(a_new_size);
    if (a_new_size == a_mmap->map_size)
        return 0;

    void *l_new = DAP_REALLOC(a_mmap->base, a_new_size);
    if (!l_new) {
        log_it(L_ERROR, "realloc(%zu -> %zu) failed", a_mmap->map_size, a_new_size);
        return -1;
    }

    if (a_new_size > a_mmap->map_size)
        memset((uint8_t *)l_new + a_mmap->map_size, 0, a_new_size - a_mmap->map_size);

    a_mmap->base = l_new;
    a_mmap->map_size = a_new_size;
    return 0;
}

static int s_mmap_write_file(dap_mmap_t *a_mmap, size_t a_offset, size_t a_length)
{
    if (!(a_mmap->flags & DAP_MMAP_WRITE) || !a_mmap->file_path)
        return 0;

    if (a_offset == 0 && a_length >= a_mmap->map_size) {
        FILE *l_fp = fopen(a_mmap->file_path, "wb");
        if (!l_fp) {
            log_it(L_ERROR, "Can't open '%s' for writing: %s", a_mmap->file_path, strerror(errno));
            return -1;
        }
        size_t l_written = fwrite(a_mmap->base, 1, a_mmap->map_size, l_fp);
        fclose(l_fp);
        if (l_written != a_mmap->map_size) {
            log_it(L_ERROR, "Partial write to '%s': %zu of %zu", a_mmap->file_path, l_written, a_mmap->map_size);
            return -1;
        }
    } else {
        FILE *l_fp = fopen(a_mmap->file_path, "r+b");
        if (!l_fp) {
            l_fp = fopen(a_mmap->file_path, "wb");
            if (!l_fp) {
                log_it(L_ERROR, "Can't open '%s' for writing: %s", a_mmap->file_path, strerror(errno));
                return -1;
            }
            fwrite(a_mmap->base, 1, a_mmap->map_size, l_fp);
            fclose(l_fp);
            return 0;
        }
        if (fseeko(l_fp, (off_t)a_offset, SEEK_SET) != 0) {
            log_it(L_ERROR, "fseeko to %zu failed: %s", a_offset, strerror(errno));
            fclose(l_fp);
            return -1;
        }
        size_t l_written = fwrite((uint8_t *)a_mmap->base + a_offset, 1, a_length, l_fp);
        fclose(l_fp);
        if (l_written != a_length) {
            log_it(L_ERROR, "Partial range write to '%s': %zu of %zu", a_mmap->file_path, l_written, a_length);
            return -1;
        }
    }
    a_mmap->file_size = a_mmap->map_size;
    return 0;
}

int dap_mmap_sync(dap_mmap_t *a_mmap, int a_flags)
{
    (void)a_flags;
    if (!a_mmap || !a_mmap->base) return -1;
    return s_mmap_write_file(a_mmap, 0, a_mmap->map_size);
}

int dap_mmap_sync_range(dap_mmap_t *a_mmap, size_t a_offset, size_t a_length, int a_flags)
{
    (void)a_flags;
    if (!a_mmap || !a_mmap->base) return -1;
    if (a_offset + a_length > a_mmap->map_size) return -1;
    return s_mmap_write_file(a_mmap, a_offset, a_length);
}

int dap_mmap_advise(dap_mmap_t *a_mmap, int a_advice)
{
    (void)a_mmap; (void)a_advice;
    return 0;
}

int dap_mmap_advise_range(dap_mmap_t *a_mmap, size_t a_offset, size_t a_length, int a_advice)
{
    (void)a_mmap; (void)a_offset; (void)a_length; (void)a_advice;
    return 0;
}

void dap_mmap_close(dap_mmap_t *a_mmap)
{
    if (!a_mmap) return;
    DAP_DEL_Z(a_mmap->base);
    DAP_DEL_Z(a_mmap->file_path);
    DAP_DELETE(a_mmap);
}

// ============================================================================
// POSIX implementation
// ============================================================================

#elif !defined(DAP_OS_WINDOWS)

dap_mmap_t *dap_mmap_open(const char *a_path, int a_flags, size_t a_initial_size)
{
    if (!a_path) {
        log_it(L_ERROR, "NULL path");
        return NULL;
    }

    // Build open flags
    int l_oflags = 0;
    if ((a_flags & DAP_MMAP_RDWR) == DAP_MMAP_RDWR)
        l_oflags = O_RDWR;
    else if (a_flags & DAP_MMAP_WRITE)
        l_oflags = O_RDWR;
    else
        l_oflags = O_RDONLY;

    if (a_flags & DAP_MMAP_CREATE)
        l_oflags |= O_CREAT;

    int l_fd = open(a_path, l_oflags, 0644);
    if (l_fd < 0) {
        log_it(L_ERROR, "Failed to open '%s': %s", a_path, strerror(errno));
        return NULL;
    }

    // Determine file size
    struct stat l_st;
    if (fstat(l_fd, &l_st) < 0) {
        log_it(L_ERROR, "fstat failed: %s", strerror(errno));
        close(l_fd);
        return NULL;
    }

    size_t l_file_size = (size_t)l_st.st_size;
    size_t l_map_size = a_initial_size > 0 ? a_initial_size : l_file_size;
    if (l_map_size == 0)
        l_map_size = PAGE_ALIGN_UP(1);
    l_map_size = PAGE_ALIGN_UP(l_map_size);

    // Extend file if needed
    if (l_file_size < l_map_size && (a_flags & DAP_MMAP_WRITE)) {
        if (ftruncate(l_fd, (off_t)l_map_size) < 0) {
            log_it(L_ERROR, "ftruncate to %zu failed: %s", l_map_size, strerror(errno));
            close(l_fd);
            return NULL;
        }
    } else if (l_file_size < l_map_size) {
        // Read-only, can't extend — map only what exists
        l_map_size = PAGE_ALIGN_UP(l_file_size > 0 ? l_file_size : 1);
    }

    // Build mmap prot/flags
    int l_prot = 0;
    if (a_flags & DAP_MMAP_READ)  l_prot |= PROT_READ;
    if (a_flags & DAP_MMAP_WRITE) l_prot |= PROT_WRITE;
    if (l_prot == 0) l_prot = PROT_READ;

    int l_mflags = 0;
    if (a_flags & DAP_MMAP_PRIVATE)
        l_mflags = MAP_PRIVATE;
    else if (a_flags & DAP_MMAP_SHARED)
        l_mflags = MAP_SHARED;
    else
        l_mflags = MAP_SHARED; // Default to shared for database use

    void *l_base = mmap(NULL, l_map_size, l_prot, l_mflags, l_fd, 0);
    if (l_base == MAP_FAILED) {
        log_it(L_ERROR, "mmap(%zu bytes) failed: %s", l_map_size, strerror(errno));
        close(l_fd);
        return NULL;
    }

    // Allocate handle
    dap_mmap_t *l_mmap = DAP_NEW_Z(dap_mmap_t);
    if (!l_mmap) {
        munmap(l_base, l_map_size);
        close(l_fd);
        return NULL;
    }

    l_mmap->base = l_base;
    l_mmap->map_size = l_map_size;
    l_mmap->flags = a_flags;
    l_mmap->fd = l_fd;
    l_mmap->fd_owned = true;

    return l_mmap;
}

void *dap_mmap_get_ptr(dap_mmap_t *a_mmap)
{
    return a_mmap ? a_mmap->base : NULL;
}

size_t dap_mmap_get_size(dap_mmap_t *a_mmap)
{
    return a_mmap ? a_mmap->map_size : 0;
}

int dap_mmap_get_fd(dap_mmap_t *a_mmap)
{
    return a_mmap ? a_mmap->fd : -1;
}

int dap_mmap_resize(dap_mmap_t *a_mmap, size_t a_new_size)
{
    if (!a_mmap || a_new_size == 0) return -1;

    a_new_size = PAGE_ALIGN_UP(a_new_size);
    if (a_new_size == a_mmap->map_size)
        return 0;

    // Extend the file first
    if (a_mmap->flags & DAP_MMAP_WRITE) {
        if (ftruncate(a_mmap->fd, (off_t)a_new_size) < 0) {
            log_it(L_ERROR, "ftruncate to %zu failed: %s", a_new_size, strerror(errno));
            return -1;
        }
    }

#ifdef DAP_OS_LINUX
    // Linux mremap: O(1), no copy, just updates page tables
    void *l_new = mremap(a_mmap->base, a_mmap->map_size, a_new_size, MREMAP_MAYMOVE);
    if (l_new == MAP_FAILED) {
        log_it(L_ERROR, "mremap(%zu -> %zu) failed: %s",
               a_mmap->map_size, a_new_size, strerror(errno));
        return -1;
    }
    a_mmap->base = l_new;
    a_mmap->map_size = a_new_size;
    return 0;
#else
    // Other POSIX (macOS, BSD): munmap + mmap
    int l_prot = 0;
    if (a_mmap->flags & DAP_MMAP_READ)  l_prot |= PROT_READ;
    if (a_mmap->flags & DAP_MMAP_WRITE) l_prot |= PROT_WRITE;
    if (l_prot == 0) l_prot = PROT_READ;

    int l_mflags = (a_mmap->flags & DAP_MMAP_PRIVATE) ? MAP_PRIVATE : MAP_SHARED;

    munmap(a_mmap->base, a_mmap->map_size);

    void *l_new = mmap(NULL, a_new_size, l_prot, l_mflags, a_mmap->fd, 0);
    if (l_new == MAP_FAILED) {
        log_it(L_ERROR, "mmap(%zu bytes) after resize failed: %s", a_new_size, strerror(errno));
        a_mmap->base = NULL;
        a_mmap->map_size = 0;
        return -1;
    }
    a_mmap->base = l_new;
    a_mmap->map_size = a_new_size;
    return 0;
#endif
}

int dap_mmap_sync(dap_mmap_t *a_mmap, int a_flags)
{
    if (!a_mmap || !a_mmap->base) return -1;
    int l_ms = (a_flags == DAP_MMAP_SYNC_SYNC) ? MS_SYNC : MS_ASYNC;
    if (msync(a_mmap->base, a_mmap->map_size, l_ms) < 0) {
        log_it(L_ERROR, "msync failed: %s", strerror(errno));
        return -1;
    }
    return 0;
}

int dap_mmap_sync_range(dap_mmap_t *a_mmap, size_t a_offset, size_t a_length, int a_flags)
{
    if (!a_mmap || !a_mmap->base) return -1;
    if (a_offset + a_length > a_mmap->map_size) return -1;

    // msync requires page-aligned address
    long l_ps = sysconf(_SC_PAGESIZE);
    if (l_ps <= 0) l_ps = 4096;
    size_t l_page_offset = a_offset & ~((size_t)l_ps - 1);
    size_t l_adj_length = a_length + (a_offset - l_page_offset);
    void *l_addr = (uint8_t *)a_mmap->base + l_page_offset;

    int l_ms = (a_flags == DAP_MMAP_SYNC_SYNC) ? MS_SYNC : MS_ASYNC;
    if (msync(l_addr, l_adj_length, l_ms) < 0) {
        log_it(L_ERROR, "msync range failed: %s", strerror(errno));
        return -1;
    }
    return 0;
}

int dap_mmap_advise(dap_mmap_t *a_mmap, int a_advice)
{
    if (!a_mmap || !a_mmap->base) return -1;
    return dap_mmap_advise_range(a_mmap, 0, a_mmap->map_size, a_advice);
}

int dap_mmap_advise_range(dap_mmap_t *a_mmap, size_t a_offset, size_t a_length, int a_advice)
{
    if (!a_mmap || !a_mmap->base) return -1;
    if (a_offset + a_length > a_mmap->map_size) return -1;

    int l_madv;
    switch (a_advice) {
    case DAP_MMAP_ADVISE_RANDOM:     l_madv = MADV_RANDOM;     break;
    case DAP_MMAP_ADVISE_SEQUENTIAL: l_madv = MADV_SEQUENTIAL;  break;
    case DAP_MMAP_ADVISE_WILLNEED:   l_madv = MADV_WILLNEED;   break;
    default:                         l_madv = MADV_NORMAL;      break;
    }

    if (madvise((uint8_t *)a_mmap->base + a_offset, a_length, l_madv) < 0) {
        // Advisory only, don't fail hard
        log_it(L_WARNING, "madvise(%d) failed: %s", l_madv, strerror(errno));
        return -1;
    }
    return 0;
}

void dap_mmap_close(dap_mmap_t *a_mmap)
{
    if (!a_mmap) return;

    if (a_mmap->base && a_mmap->base != MAP_FAILED) {
        munmap(a_mmap->base, a_mmap->map_size);
    }
    if (a_mmap->fd_owned && a_mmap->fd >= 0) {
        close(a_mmap->fd);
    }
    DAP_DELETE(a_mmap);
}

#else // DAP_OS_WINDOWS

// ============================================================================
// Windows implementation
// ============================================================================

dap_mmap_t *dap_mmap_open(const char *a_path, int a_flags, size_t a_initial_size)
{
    if (!a_path) {
        log_it(L_ERROR, "NULL path");
        return NULL;
    }

    // Build access flags
    DWORD l_access = 0;
    DWORD l_share = FILE_SHARE_READ;
    DWORD l_create = OPEN_EXISTING;

    if (a_flags & DAP_MMAP_WRITE) {
        l_access = GENERIC_READ | GENERIC_WRITE;
        l_share = FILE_SHARE_READ | FILE_SHARE_WRITE;
    } else {
        l_access = GENERIC_READ;
    }

    if (a_flags & DAP_MMAP_CREATE) {
        l_create = OPEN_ALWAYS;
    }

    HANDLE l_hFile = CreateFileA(a_path, l_access, l_share, NULL, l_create,
                                  FILE_ATTRIBUTE_NORMAL, NULL);
    if (l_hFile == INVALID_HANDLE_VALUE) {
        log_it(L_ERROR, "CreateFileA('%s') failed: %lu", a_path, GetLastError());
        return NULL;
    }

    // Get file size
    LARGE_INTEGER l_li;
    if (!GetFileSizeEx(l_hFile, &l_li)) {
        log_it(L_ERROR, "GetFileSizeEx failed: %lu", GetLastError());
        CloseHandle(l_hFile);
        return NULL;
    }

    size_t l_file_size = (size_t)l_li.QuadPart;
    size_t l_map_size = a_initial_size > 0 ? a_initial_size : l_file_size;
    if (l_map_size == 0)
        l_map_size = PAGE_ALIGN_UP(1);
    l_map_size = PAGE_ALIGN_UP(l_map_size);

    // Build mapping protection
    DWORD l_protect = PAGE_READONLY;
    DWORD l_map_access = FILE_MAP_READ;
    if (a_flags & DAP_MMAP_WRITE) {
        if (a_flags & DAP_MMAP_PRIVATE) {
            l_protect = PAGE_WRITECOPY;
            l_map_access = FILE_MAP_COPY;
        } else {
            l_protect = PAGE_READWRITE;
            l_map_access = FILE_MAP_ALL_ACCESS;
        }
    }

    LARGE_INTEGER l_map_li;
    l_map_li.QuadPart = (LONGLONG)l_map_size;
    HANDLE l_hMapping = CreateFileMappingA(l_hFile, NULL, l_protect,
                                            l_map_li.HighPart, l_map_li.LowPart, NULL);
    if (!l_hMapping) {
        log_it(L_ERROR, "CreateFileMappingA failed: %lu", GetLastError());
        CloseHandle(l_hFile);
        return NULL;
    }

    void *l_base = MapViewOfFile(l_hMapping, l_map_access, 0, 0, l_map_size);
    if (!l_base) {
        log_it(L_ERROR, "MapViewOfFile(%zu) failed: %lu", l_map_size, GetLastError());
        CloseHandle(l_hMapping);
        CloseHandle(l_hFile);
        return NULL;
    }

    dap_mmap_t *l_mmap = DAP_NEW_Z(dap_mmap_t);
    if (!l_mmap) {
        UnmapViewOfFile(l_base);
        CloseHandle(l_hMapping);
        CloseHandle(l_hFile);
        return NULL;
    }

    l_mmap->base = l_base;
    l_mmap->map_size = l_map_size;
    l_mmap->flags = a_flags;
    l_mmap->hFile = l_hFile;
    l_mmap->hMapping = l_hMapping;

    return l_mmap;
}

void *dap_mmap_get_ptr(dap_mmap_t *a_mmap)
{
    return a_mmap ? a_mmap->base : NULL;
}

size_t dap_mmap_get_size(dap_mmap_t *a_mmap)
{
    return a_mmap ? a_mmap->map_size : 0;
}

int dap_mmap_get_fd(dap_mmap_t *a_mmap)
{
    (void)a_mmap;
    return -1; // Windows uses HANDLE, not fd
}

int dap_mmap_resize(dap_mmap_t *a_mmap, size_t a_new_size)
{
    if (!a_mmap || a_new_size == 0) return -1;

    a_new_size = PAGE_ALIGN_UP(a_new_size);
    if (a_new_size == a_mmap->map_size)
        return 0;

    // Unmap current view and mapping
    if (a_mmap->base) {
        UnmapViewOfFile(a_mmap->base);
        a_mmap->base = NULL;
    }
    if (a_mmap->hMapping) {
        CloseHandle(a_mmap->hMapping);
        a_mmap->hMapping = NULL;
    }

    // Re-create mapping with new size
    DWORD l_protect = PAGE_READONLY;
    DWORD l_map_access = FILE_MAP_READ;
    if (a_mmap->flags & DAP_MMAP_WRITE) {
        if (a_mmap->flags & DAP_MMAP_PRIVATE) {
            l_protect = PAGE_WRITECOPY;
            l_map_access = FILE_MAP_COPY;
        } else {
            l_protect = PAGE_READWRITE;
            l_map_access = FILE_MAP_ALL_ACCESS;
        }
    }

    LARGE_INTEGER l_map_li;
    l_map_li.QuadPart = (LONGLONG)a_new_size;
    a_mmap->hMapping = CreateFileMappingA(a_mmap->hFile, NULL, l_protect,
                                           l_map_li.HighPart, l_map_li.LowPart, NULL);
    if (!a_mmap->hMapping) {
        log_it(L_ERROR, "CreateFileMappingA resize to %zu failed: %lu", a_new_size, GetLastError());
        a_mmap->map_size = 0;
        return -1;
    }

    a_mmap->base = MapViewOfFile(a_mmap->hMapping, l_map_access, 0, 0, a_new_size);
    if (!a_mmap->base) {
        log_it(L_ERROR, "MapViewOfFile resize to %zu failed: %lu", a_new_size, GetLastError());
        CloseHandle(a_mmap->hMapping);
        a_mmap->hMapping = NULL;
        a_mmap->map_size = 0;
        return -1;
    }

    a_mmap->map_size = a_new_size;
    return 0;
}

int dap_mmap_sync(dap_mmap_t *a_mmap, int a_flags)
{
    if (!a_mmap || !a_mmap->base) return -1;

    if (!FlushViewOfFile(a_mmap->base, a_mmap->map_size)) {
        log_it(L_ERROR, "FlushViewOfFile failed: %lu", GetLastError());
        return -1;
    }

    if (a_flags == DAP_MMAP_SYNC_SYNC && a_mmap->hFile != INVALID_HANDLE_VALUE) {
        if (!FlushFileBuffers(a_mmap->hFile)) {
            log_it(L_ERROR, "FlushFileBuffers failed: %lu", GetLastError());
            return -1;
        }
    }
    return 0;
}

int dap_mmap_sync_range(dap_mmap_t *a_mmap, size_t a_offset, size_t a_length, int a_flags)
{
    if (!a_mmap || !a_mmap->base) return -1;
    if (a_offset + a_length > a_mmap->map_size) return -1;

    if (!FlushViewOfFile((uint8_t *)a_mmap->base + a_offset, a_length)) {
        log_it(L_ERROR, "FlushViewOfFile range failed: %lu", GetLastError());
        return -1;
    }

    if (a_flags == DAP_MMAP_SYNC_SYNC && a_mmap->hFile != INVALID_HANDLE_VALUE) {
        FlushFileBuffers(a_mmap->hFile);
    }
    return 0;
}

int dap_mmap_advise(dap_mmap_t *a_mmap, int a_advice)
{
    // Windows has no madvise equivalent; this is a no-op
    (void)a_mmap;
    (void)a_advice;
    return 0;
}

int dap_mmap_advise_range(dap_mmap_t *a_mmap, size_t a_offset, size_t a_length, int a_advice)
{
    (void)a_mmap;
    (void)a_offset;
    (void)a_length;
    (void)a_advice;
    return 0;
}

void dap_mmap_close(dap_mmap_t *a_mmap)
{
    if (!a_mmap) return;

    if (a_mmap->base) {
        UnmapViewOfFile(a_mmap->base);
    }
    if (a_mmap->hMapping) {
        CloseHandle(a_mmap->hMapping);
    }
    if (a_mmap->hFile && a_mmap->hFile != INVALID_HANDLE_VALUE) {
        CloseHandle(a_mmap->hFile);
    }
    DAP_DELETE(a_mmap);
}

#endif // DAP_OS_WINDOWS
