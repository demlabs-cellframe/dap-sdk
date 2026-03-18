/**
 * @file dap_io_ops.h
 * @brief I/O operations - vectored read/write
 *
 * Copyright (c) 2017-2026 Demlabs
 * License: GNU GPL v3
 */

#pragma once

#include "dap_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Vectored read from file handle
 * @param a_hf File handle
 * @param a_bufs Array of iovec buffers
 * @param a_bufs_num Number of buffers
 * @param a_err Error code output (can be NULL)
 * @return Bytes read on success, -1 on error
 */
ssize_t dap_readv(dap_file_handle_t a_hf, iovec_t const *a_bufs, int a_bufs_num, dap_errnum_t *a_err);

/**
 * @brief Vectored write to file handle
 * @param a_hf File handle
 * @param a_filename Filename (used on Windows for file size adjustment)
 * @param a_bufs Array of iovec buffers
 * @param a_bufs_num Number of buffers
 * @param a_err Error code output (can be NULL)
 * @return Bytes written on success, -1 on error
 */
ssize_t dap_writev(dap_file_handle_t a_hf, const char *a_filename, iovec_t const *a_bufs, int a_bufs_num, dap_errnum_t *a_err);

#ifdef __cplusplus
}
#endif
