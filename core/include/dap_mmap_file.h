/*
 * Reusable memory-mapped file utility for persistent storage.
 * Provides MAP_SHARED backed file I/O with auto-resizing append support.
 *
 * Copyright (c) 2025
 * This file is part of DAP (Distributed Applications Platform).
 * Licensed under GNU General Public License v3.
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <sys/types.h>

/**
 * @struct dap_mmap_file
 * @brief Opaque handle for a memory-mapped file.
 *
 * The file is mmap'd MAP_SHARED for read-write access. Writes to the
 * mapped region are automatically persisted to disk by the OS page cache.
 */
typedef struct dap_mmap_file {
    int         fd;           /**< File descriptor */
    char       *map;          /**< mmap'd base pointer */
    uint64_t    map_size;     /**< Current mmap'd region size (page-aligned) */
    uint64_t    file_size;    /**< Logical data size (<= map_size) */
    char        path[1024];   /**< File path */
    bool        read_only;    /**< If true, mmap is PROT_READ only */
#ifdef _WIN32
    void       *mapping;      /**< Win32 file-mapping HANDLE */
#endif
} dap_mmap_file_t;

/**
 * Open or create a memory-mapped file.
 * @param path      Filesystem path
 * @param create    If true, create file if it doesn't exist
 * @param read_only If true, map as PROT_READ only (no writes)
 * @return Handle or NULL on error
 */
dap_mmap_file_t *dap_mmap_file_open(const char *path, bool create, bool read_only);

/**
 * Close and unmap. Calls msync() before munmap for data safety.
 */
void dap_mmap_file_close(dap_mmap_file_t *mf);

/**
 * Get the mmap'd base pointer. Data from offset 0 to file_size is valid.
 */
void *dap_mmap_file_ptr(dap_mmap_file_t *mf);

/**
 * Get the logical data size (bytes actually written).
 */
uint64_t dap_mmap_file_size(dap_mmap_file_t *mf);

/**
 * Append data to the end of the file. Auto-resizes mmap if needed.
 * @return Offset where data was written, or -1 on error
 */
ssize_t dap_mmap_file_append(dap_mmap_file_t *mf, const void *data, size_t size);

/**
 * Resize the file and remap. New size must be >= file_size.
 * @return 0 on success, -1 on error
 */
int dap_mmap_file_resize(dap_mmap_file_t *mf, uint64_t new_size);

/**
 * Force sync of mmap'd pages to disk.
 * @return 0 on success, -1 on error
 */
int dap_mmap_file_sync(dap_mmap_file_t *mf);
