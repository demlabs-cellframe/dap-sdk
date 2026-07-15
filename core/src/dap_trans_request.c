/**
 * @file dap_trans_request.c
 * @brief Transport-independent request/reply implementation
 */

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "dap_common.h"
#include "dap_trans_request.h"

#define LOG_TAG "dap_trans_request"

int trans_reply(dap_trans_request_t *a_req, const void *a_data, size_t a_size)
{
    if (!a_req || !a_data || a_size == 0)
        return -1;

    /* Ensure capacity */
    if (a_req->reply_size + a_size > a_req->reply_capacity) {
        size_t l_new_cap = a_req->reply_capacity ? a_req->reply_capacity * 2 : 4096;
        while (l_new_cap < a_req->reply_size + a_size)
            l_new_cap *= 2;
        if (l_new_cap > DAP_TRANS_REPLY_MAX)
            l_new_cap = DAP_TRANS_REPLY_MAX;
        uint8_t *l_new = DAP_NEW_SIZE(uint8_t, l_new_cap);
        if (!l_new) return -1;
        if (a_req->reply) {
            memcpy(l_new, a_req->reply, a_req->reply_size);
            DAP_DELETE(a_req->reply);
        }
        a_req->reply = l_new;
        a_req->reply_capacity = l_new_cap;
    }

    size_t l_copy = a_size;
    if (a_req->reply_size + l_copy > a_req->reply_capacity)
        l_copy = a_req->reply_capacity - a_req->reply_size;

    memcpy(a_req->reply + a_req->reply_size, a_data, l_copy);
    a_req->reply_size += l_copy;
    return 0;
}

int trans_reply_f(dap_trans_request_t *a_req, const char *a_fmt, ...)
{
    if (!a_req || !a_fmt) return -1;

    char l_buf[4096];
    va_list ap;
    va_start(ap, a_fmt);
    int l_len = vsnprintf(l_buf, sizeof(l_buf), a_fmt, ap);
    va_end(ap);

    if (l_len <= 0) return -1;
    if (trans_reply(a_req, l_buf, (size_t)l_len) != 0)
        return -1;
    return l_len;
}

int trans_reply_flush(dap_trans_request_t *a_req)
{
    if (!a_req || !a_req->reply_cb)
        return -1;
    return a_req->reply_cb(a_req, a_req->reply, a_req->reply_size);
}
