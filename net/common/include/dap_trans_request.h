/**
 * @file dap_trans_request.h
 * @brief Transport-independent request/reply abstraction
 *
 * Handlers (enc_init, stream_ctl, stream) use this API instead of
 * HTTP-specific structures. Works for HTTP, TLS, WebSocket — any transport.
 *
 * Usage:
 *   void my_handler(dap_trans_request_t *req) {
 *       const char *query = trans_get_query(req);
 *       const void *body = trans_get_body(req, &body_sz);
 *       trans_reply(req, response_data, response_sz);
 *       trans_set_status(req, 200);
 *   }
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DAP_TRANS_REPLY_MAX  65536

typedef struct dap_trans_request dap_trans_request_t;

/* Reply callback — transport-specific (HTTP reply, TLS wrap+send, etc.) */
typedef int (*dap_trans_reply_cb_t)(dap_trans_request_t *a_req,
                                    const void *a_data, size_t a_size);

struct dap_trans_request {
    /* Request data (set by transport layer) */
    const char *query_string;       /* URL query: "enc_type=6,pkey_exchange_type=..." */
    const char *url_path;           /* URL path: "gd4y5yh78w42aaagh" or "channels=RS,..." */
    size_t      url_path_len;
    const void *body;               /* Raw request body */
    size_t      body_len;
    const char *key_id;             /* KeyID from headers (NULL if not present) */

    /* Reply buffer (filled by handler) */
    uint8_t    *reply;
    size_t      reply_size;
    size_t      reply_capacity;
    int         status_code;        /* 200, 400, 401, 500, etc. */

    /* Transport context (opaque to handler) */
    void       *transport_ctx;      /* e.g. dap_http_simple_t*, or TLS esocket* */
    dap_trans_reply_cb_t reply_cb;  /* Called when reply is ready to send */
};

/* ---- Accessors (handler reads request data) ---- */

static inline const char *trans_get_query(dap_trans_request_t *a_req)
{
    return a_req ? a_req->query_string : NULL;
}

static inline const char *trans_get_url_path(dap_trans_request_t *a_req)
{
    return a_req ? a_req->url_path : NULL;
}

static inline size_t trans_get_url_path_len(dap_trans_request_t *a_req)
{
    return a_req ? a_req->url_path_len : 0;
}

static inline const void *trans_get_body(dap_trans_request_t *a_req, size_t *a_out_size)
{
    if (a_out_size) *a_out_size = a_req ? a_req->body_len : 0;
    return a_req ? a_req->body : NULL;
}

static inline const char *trans_get_key_id(dap_trans_request_t *a_req)
{
    return a_req ? a_req->key_id : NULL;
}

/* ---- Reply functions (handler writes response) ---- */

/**
 * Write raw data to reply buffer.
 * @return 0 on success, -1 on error
 */
int trans_reply(dap_trans_request_t *a_req, const void *a_data, size_t a_size);

/**
 * Write formatted string to reply buffer.
 * @return bytes written, or -1 on error
 */
int trans_reply_f(dap_trans_request_t *a_req, const char *a_fmt, ...);

/**
 * Set HTTP-like status code.
 */
static inline void trans_set_status(dap_trans_request_t *a_req, int a_code)
{
    if (a_req) a_req->status_code = a_code;
}

/**
 * Flush reply through transport callback.
 * @return 0 on success
 */
int trans_reply_flush(dap_trans_request_t *a_req);

#ifdef __cplusplus
}
#endif
