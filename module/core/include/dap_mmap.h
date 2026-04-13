/*
 * DAP Cross-Platform Memory-Mapped File I/O
 *
 * Provides a unified API for memory-mapped files across platforms:
 * - Linux/Android: mmap() / mremap() / msync() / madvise()
 * - macOS/BSD:     mmap() / munmap()+mmap() / msync() / madvise()
 * - Windows:       CreateFileMapping() / MapViewOfFile() / FlushViewOfFile()
 * - WASM:          heap buffer (malloc/realloc) + file I/O via WASMFS/OPFS
 *
 * Designed for high-performance database engines (B-tree, etc.).
 *
 * Authors:
 * DAP SDK Team
 * Copyright (c) 2026
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Types
// ============================================================================

/** @brief Opaque memory-mapped file handle */
typedef struct dap_mmap dap_mmap_t;

// ============================================================================
// Open flags (bitfield)
// ============================================================================

#define DAP_MMAP_READ       0x01    ///< Read access
#define DAP_MMAP_WRITE      0x02    ///< Write access
#define DAP_MMAP_RDWR       (DAP_MMAP_READ | DAP_MMAP_WRITE)
#define DAP_MMAP_CREATE     0x04    ///< Create file if it does not exist
#define DAP_MMAP_SHARED     0x08    ///< Changes visible to other processes (MAP_SHARED)
#define DAP_MMAP_PRIVATE    0x10    ///< Copy-on-write, private mapping (MAP_PRIVATE)

// ============================================================================
// Sync flags
// ============================================================================

#define DAP_MMAP_SYNC_ASYNC  0      ///< Asynchronous flush (MS_ASYNC)
#define DAP_MMAP_SYNC_SYNC   1      ///< Synchronous flush (MS_SYNC)

// ============================================================================
// madvise hints
// ============================================================================

#define DAP_MMAP_ADVISE_NORMAL     0    ///< No special treatment (MADV_NORMAL)
#define DAP_MMAP_ADVISE_RANDOM     1    ///< Random access pattern (MADV_RANDOM)
#define DAP_MMAP_ADVISE_SEQUENTIAL 2    ///< Sequential access pattern (MADV_SEQUENTIAL)
#define DAP_MMAP_ADVISE_WILLNEED   3    ///< Will need this data soon (MADV_WILLNEED)

// ============================================================================
// API
// ============================================================================

/**
 * @brief Open (or create) a memory-mapped file
 *
 * @param a_path         File path
 * @param a_flags        Combination of DAP_MMAP_* flags
 * @param a_initial_size Initial mapping size in bytes (file will be extended
 *                       if smaller; 0 = use existing file size)
 * @return Handle, or NULL on error
 */
dap_mmap_t *dap_mmap_open(const char *a_path, int a_flags, size_t a_initial_size);

/**
 * @brief Get base pointer of the mapped region
 * @return Pointer to the first byte, or NULL if not mapped
 */
void *dap_mmap_get_ptr(dap_mmap_t *a_mmap);

/**
 * @brief Get current mapped region size
 */
size_t dap_mmap_get_size(dap_mmap_t *a_mmap);

/**
 * @brief Get the underlying file descriptor (POSIX) or INVALID_HANDLE_VALUE (Windows)
 *
 * Useful when the caller needs to do additional file operations.
 * @return File descriptor (>=0 on POSIX), or -1 on error / Windows
 */
int dap_mmap_get_fd(dap_mmap_t *a_mmap);

/**
 * @brief Resize the mapping (and the underlying file)
 *
 * On Linux uses mremap() for O(1) resize when possible.
 * On other platforms: munmap + ftruncate + mmap.
 *
 * @param a_new_size New size in bytes (must be > 0)
 * @return 0 on success, -1 on error
 */
int dap_mmap_resize(dap_mmap_t *a_mmap, size_t a_new_size);

/**
 * @brief Flush entire mapping to disk
 *
 * @param a_flags DAP_MMAP_SYNC_ASYNC or DAP_MMAP_SYNC_SYNC
 * @return 0 on success, -1 on error
 */
int dap_mmap_sync(dap_mmap_t *a_mmap, int a_flags);

/**
 * @brief Flush a specific range to disk
 *
 * @param a_offset Byte offset from start of mapping
 * @param a_length Number of bytes to sync
 * @param a_flags  DAP_MMAP_SYNC_ASYNC or DAP_MMAP_SYNC_SYNC
 * @return 0 on success, -1 on error
 */
int dap_mmap_sync_range(dap_mmap_t *a_mmap, size_t a_offset, size_t a_length, int a_flags);

/**
 * @brief Advise kernel about expected access pattern
 *
 * @param a_advice One of DAP_MMAP_ADVISE_* constants
 * @return 0 on success, -1 on error (non-fatal; advisory only)
 */
int dap_mmap_advise(dap_mmap_t *a_mmap, int a_advice);

/**
 * @brief Advise kernel about expected access pattern for a range
 *
 * @param a_offset Byte offset from start of mapping
 * @param a_length Number of bytes
 * @param a_advice One of DAP_MMAP_ADVISE_* constants
 * @return 0 on success, -1 on error (non-fatal)
 */
int dap_mmap_advise_range(dap_mmap_t *a_mmap, size_t a_offset, size_t a_length, int a_advice);

/**
 * @brief Close the mapping and release all resources
 *
 * Does NOT sync to disk. Call dap_mmap_sync() before if needed.
 * Safe to call with NULL.
 */
void dap_mmap_close(dap_mmap_t *a_mmap);

#ifdef __cplusplus
}
#endif
