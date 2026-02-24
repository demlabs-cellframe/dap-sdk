/**
 * @file dap_io_ops.c
 * @brief I/O operations - vectored read/write
 *
 * Copyright (c) 2017-2026 Demlabs
 * License: GNU GPL v3
 */

#include <errno.h>
#ifdef DAP_OS_UNIX
#include <sys/uio.h>
#endif

#include "dap_common.h"
#include "dap_io_ops.h"

#define LOG_TAG "dap_io_ops"

ssize_t dap_readv(dap_file_handle_t a_hf, iovec_t const *a_bufs, int a_bufs_num, dap_errnum_t *a_err)
{
#ifdef DAP_OS_WINDOWS
    if (!a_bufs || !a_bufs_num) {
        return -1;
    }
    DWORD l_ret = 0;
    bool l_is_aligned = false;
    if (!l_is_aligned) {
        for (iovec_t const *cur_buf = a_bufs, *end = a_bufs + a_bufs_num; cur_buf < end; ++cur_buf) {
            DWORD l_read = 0;
            if (ReadFile(a_hf, (char*)cur_buf->iov_base, cur_buf->iov_len, &l_read, 0) == FALSE) {
                if (a_err)
                    *a_err = GetLastError();
                return -1;
            }
            l_ret += l_read;
        }
        return l_ret;
    }

    size_t l_total_bufs_size = 0;
    for (iovec_t const *i = a_bufs, *end = a_bufs + a_bufs_num; i < end; ++i)
        l_total_bufs_size += i->iov_len;
    l_ret += l_total_bufs_size;

    size_t l_page_size = dap_pagesize();
    int l_pages_count = (l_total_bufs_size + l_page_size - 1) / l_page_size;
    PFILE_SEGMENT_ELEMENT l_seg_arr = DAP_PAGE_ALMALLOC(sizeof(FILE_SEGMENT_ELEMENT) * (l_pages_count + 1)),
            l_cur_seg = l_seg_arr;
    for (iovec_t const *i = a_bufs, *end = a_bufs + a_bufs_num; i < end; ++i)
        for (size_t j = 0; j < i->iov_len; j += l_page_size, ++l_cur_seg)
            l_cur_seg->Buffer = PtrToPtr64((((char*)i->iov_base) + j));
    l_cur_seg->Buffer = 0;

    OVERLAPPED l_ol = {
        .hEvent = CreateEvent(0, TRUE, FALSE, 0)
    };

    l_total_bufs_size = l_pages_count * l_page_size;
    if (!ReadFileScatter(a_hf, l_seg_arr, l_total_bufs_size, 0, &l_ol)) {
        DWORD l_err = GetLastError();
        if (l_err != ERROR_IO_PENDING) {
            if (a_err)
                *a_err = GetLastError();
            CloseHandle(l_ol.hEvent);
            DAP_PAGE_ALFREE(l_seg_arr);
            return -1;
        }
        if (!GetOverlappedResult(a_hf, &l_ol, &l_ret, TRUE)) {
            if (a_err)
                *a_err = GetLastError();
            CloseHandle(l_ol.hEvent);
            DAP_PAGE_ALFREE(l_seg_arr);
            return -1;
        }
    }
    CloseHandle(l_ol.hEvent);
    DAP_PAGE_ALFREE(l_seg_arr);
    return l_ret;
#else
    dap_errnum_t l_err = 0;
    ssize_t l_res = readv(a_hf, a_bufs, a_bufs_num);
    if (l_res == -1)
        l_err = errno;
    if (a_err)
        *a_err = l_err;
    return l_res;
#endif
}

ssize_t dap_writev(dap_file_handle_t a_hf, const char* a_filename, iovec_t const *a_bufs, int a_bufs_num, dap_errnum_t *a_err)
{
#ifdef DAP_OS_WINDOWS
    if (!a_bufs || !a_bufs_num) {
        log_it(L_ERROR, "Bad input data");
        return -1;
    }
    DWORD l_ret = 0;
    bool l_is_aligned = false;
    if (!l_is_aligned) {
        for (iovec_t const *cur_buf = a_bufs, *end = a_bufs + a_bufs_num; cur_buf < end; ++cur_buf) {
            DWORD l_written = 0;
            if (!WriteFile(a_hf, (char const*)cur_buf->iov_base, cur_buf->iov_len, &l_written, NULL)) {
                if (a_err)
                    *a_err = GetLastError();
                return -1;
            }
            l_ret += l_written;
        }
        return l_ret;
    }

    size_t l_total_bufs_size = 0, l_file_size = 0;
    for (iovec_t const *i = a_bufs, *end = a_bufs + a_bufs_num; i < end; ++i)
        l_total_bufs_size += i->iov_len;
    l_ret += l_total_bufs_size;

    size_t l_page_size = dap_pagesize();
    int l_pages_count = (l_total_bufs_size + l_page_size - 1) / l_page_size;
    PFILE_SEGMENT_ELEMENT l_seg_arr = DAP_PAGE_ALMALLOC(sizeof(FILE_SEGMENT_ELEMENT) * (l_pages_count + 1)),
            l_cur_seg = l_seg_arr;
    int l_idx = 0;
    for (iovec_t const *cur_buf = a_bufs; l_idx++ < a_bufs_num; ++cur_buf)
        for (size_t j = 0; j < cur_buf->iov_len; j += l_page_size)
            l_cur_seg++->Buffer = PtrToPtr64((((char*)cur_buf->iov_base) + j));
    l_cur_seg->Buffer = 0;

    OVERLAPPED l_ol = {
        .Offset = 0xFFFFFFFF, .OffsetHigh = 0xFFFFFFFF,
        .hEvent = CreateEvent(0, TRUE, FALSE, 0)
    };

    if (l_total_bufs_size & (l_page_size - 1)) {
        l_file_size = l_total_bufs_size;
        l_total_bufs_size = l_pages_count * l_page_size;
    }

    DWORD l_err;
    l_err = GetLastError();
    if (!WriteFileGather(a_hf, l_seg_arr, l_total_bufs_size * 3, 0, &l_ol)) {
        l_err = GetLastError();
        if (l_err != ERROR_IO_PENDING) {
            if (a_err)
                *a_err = l_err;
            DAP_PAGE_ALFREE(l_seg_arr);
            CloseHandle(l_ol.hEvent);
            log_it(L_ERROR, "Write file err: %lu", l_err);
            return -1;
        }
        DWORD l_tmp;
        if (!GetOverlappedResult(a_hf, &l_ol, &l_tmp, TRUE)) {
            l_err = GetLastError();
            if (a_err)
                *a_err = l_err;
            DAP_PAGE_ALFREE(l_seg_arr);
            CloseHandle(l_ol.hEvent);
            log_it(L_ERROR, "Async writing failure, err %lu", l_err);
            return -1;
        }
        if (l_tmp < l_ret)
            l_ret = l_tmp;
    }
    CloseHandle(l_ol.hEvent);
    DAP_PAGE_ALFREE(l_seg_arr);
    if (l_file_size) {
        HANDLE l_hf = CreateFile(a_filename, GENERIC_WRITE,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE,
                                 0, OPEN_EXISTING,
                                 FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS,
                                 0);
        if (l_hf == INVALID_HANDLE_VALUE) {
            if (a_err)
                *a_err = GetLastError();
            return -1;
        }

        LARGE_INTEGER l_offs = { };
        l_offs.QuadPart = l_file_size;
        if (!SetFilePointerEx(l_hf, l_offs, &l_offs, FILE_BEGIN)) {
            CloseHandle(l_hf);
            if (a_err)
                *a_err = GetLastError();
            log_it(L_ERROR, "File pointer setting err: %lu", l_err);
            return -1;
        }
        if (!SetEndOfFile(l_hf)) {
            if (a_err)
                *a_err = GetLastError();
            CloseHandle(l_hf);
            log_it(L_ERROR, "EOF setting err: %lu", l_err);
            return -1;
        }
        CloseHandle(l_hf);
    }
    return l_ret;
#else
    UNUSED(a_filename);
    dap_errnum_t l_err = 0;
    ssize_t l_res = writev(a_hf, a_bufs, a_bufs_num);
    if (l_res == -1)
        l_err = errno;
    if (a_err)
        *a_err = l_err;
    return l_res;
#endif
}
