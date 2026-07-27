/*
 * TLS Mimicry Server Transport
 *
 * TLS handshake → direct handler calls (enc_init, stream_ctl, stream).
 * No HTTP server needed — parses URL+body from raw HTTP POST.
 *
 * Architecture:
 *   _inheritor → tls_conn_ctx_t (TLS state, mimicry engine)
 *   After handshake: parse HTTP POST → route by URL → call handler → TLS wrap response
 *
 * Handler adaptation for 6.0:
 *   enc_init and stream_ctl in 6.0 take dap_http_simple_t* (registered via HTTP server).
 *   The TLS server has no HTTP server, so we build minimal synthetic dap_http_simple_t +
 *   dap_http_client_t structs from the parsed raw HTTP POST data and call the handlers
 *   directly: enc_http_proc() for enc_init, and inlined stream_ctl logic with
 *   enc_http_request_decode()/enc_http_reply_encode() for stream_ctl.
 */

#include <string.h>
#include <errno.h>

#include "dap_common.h"
#include "dap_strfuncs.h"
#include "dap_events_socket.h"
#include "dap_worker.h"
#include "dap_server.h"
#include "dap_net_trans.h"
#include "dap_net_trans_server.h"
#include "dap_tls_mimicry.h"

#include "dap_stream.h"
#include "dap_stream_worker.h"
#include "dap_stream_session.h"
#include "dap_stream_pkt.h"
#include "dap_stream_ctl.h"

#include "dap_enc_http.h"
#include "dap_enc_ks.h"
#include "dap_enc_key.h"
#include "dap_enc.h"
#include "dap_enc_base64.h"
#include "dap_http_simple.h"
#include "dap_http_client.h"
#include "dap_http_header.h"
#include "dap_http_status_code.h"

#define LOG_TAG "dap_net_trans_tls_server"

#define TLS_CT_CHANGE_CIPHER_SPEC  0x14
#define TLS_CT_HANDSHAKE           0x16
#define TLS_CT_APPLICATION_DATA    0x17

/* Any TLS record content type in [0x14..0x18] means buf_out already
 * contains a valid TLS record (handshake from process_client_hello, or
 * wrapped response from s_tls_read, or leftover from a partial send). */
static inline bool s_is_tls_record_content(uint8_t a_byte)
{
    return a_byte >= TLS_CT_CHANGE_CIPHER_SPEC && a_byte <= 0x18;
}

/* ------------------------------------------------------------------ */
/*  TLS context — stored in esocket->_inheritor                        */
/* ------------------------------------------------------------------ */

typedef struct tls_conn_ctx {
    dap_tls_mimicry_t *mimicry;
    bool handshake_done;
    bool client_finished_consumed;
    size_t prev_buf_in_size;  /* guard: buf_in_size at function exit */
    bool stream_mode;         /* after stream_ctl: route DAP packets to stream layer */
    dap_stream_t *stream;     /* created after stream_ctl; NULL until then */
    bool buf_out_wrapped;     /* true if buf_out[0..N] is TLS-wrapped data */
} tls_conn_ctx_t;

static bool s_debug_more = false;

static inline tls_conn_ctx_t *s_tls_ctx(dap_events_socket_t *a_es)
{
    return a_es ? (tls_conn_ctx_t *)a_es->_inheritor : NULL;
}

/* ------------------------------------------------------------------ */
/*  HTTP POST parser — extracts URL path, query, body, and headers    */
/* ------------------------------------------------------------------ */

/* Parsed HTTP request — caller owns all heap-allocated fields. */
typedef struct {
    char *url_path;
    char *query_string;
    char *body;
    size_t body_len;
    dap_http_header_t *headers;  /* linked list, may be NULL */
} tls_parsed_request_t;

static void s_parsed_request_free(tls_parsed_request_t *a_req)
{
    DAP_DELETE(a_req->url_path);
    DAP_DELETE(a_req->query_string);
    DAP_DELETE(a_req->body);
    /* Free header linked list */
    dap_http_header_t *h = a_req->headers;
    while (h) {
        dap_http_header_t *next = h->next;
        DAP_DELETE(h);
        h = next;
    }
}

static int s_parse_http_post(char *a_raw, size_t a_raw_len, tls_parsed_request_t *a_out)
{
    memset(a_out, 0, sizeof(*a_out));

    /* Find end of request line: POST <url> HTTP/1.x\r\n */
    char *l_eol = memchr(a_raw, '\n', a_raw_len);
    if (!l_eol) return -1;

    /* Skip "POST " */
    char *l_p = a_raw + 5;

    /* Extract URL path (up to '?' or ' ') */
    char *l_path_start = l_p;
    while (*l_p && *l_p != '?' && *l_p != ' ' && *l_p != '\r' && *l_p != '\n')
        l_p++;
    size_t l_path_len = (size_t)(l_p - l_path_start);
    a_out->url_path = DAP_NEW_SIZE(char, l_path_len + 1);
    memcpy(a_out->url_path, l_path_start, l_path_len);
    a_out->url_path[l_path_len] = '\0';

    /* Extract query string (after '?' up to ' ') */
    if (*l_p == '?') {
        l_p++;
        char *l_qstart = l_p;
        while (*l_p && *l_p != ' ' && *l_p != '\r' && *l_p != '\n')
            l_p++;
        size_t l_qlen = (size_t)(l_p - l_qstart);
        a_out->query_string = DAP_NEW_SIZE(char, l_qlen + 1);
        memcpy(a_out->query_string, l_qstart, l_qlen);
        a_out->query_string[l_qlen] = '\0';
    }

    /* Parse headers: extract Content-Length, KeyID, and other headers.
     * Build a linked list of dap_http_header_t for synthetic struct construction. */
    size_t l_content_length = 0;
    char *l_body_start = NULL;
    dap_http_header_t *l_last_hdr = NULL;
    char *l_line = l_eol + 1;

    while (l_line < a_raw + a_raw_len) {
        if (*l_line == '\r' && *(l_line + 1) == '\n') {
            l_body_start = l_line + 2;
            break;
        }
        char *l_line_end = memchr(l_line, '\n', (size_t)(a_raw + a_raw_len - l_line));
        if (!l_line_end) break;

        /* Parse header: "Name: Value\r\n" */
        char *l_colon = memchr(l_line, ':', (size_t)(l_line_end - l_line));
        if (l_colon) {
            size_t l_name_len = (size_t)(l_colon - l_line);
            /* Skip ": " after colon */
            char *l_val_start = l_colon + 1;
            while (*l_val_start == ' ' && l_val_start < l_line_end) l_val_start++;
            /* Trim trailing \r */
            size_t l_val_len = (size_t)(l_line_end - l_val_start);
            if (l_val_len > 0 && *(l_val_start + l_val_len - 1) == '\r')
                l_val_len--;

            /* Content-Length: special handling (not added to header list) */
            if (l_name_len == 14 && strncasecmp(l_line, "Content-Length:", 14) == 0) {
                l_content_length = (size_t)atoi(l_val_start);
            } else {
                /* Allocate header node and add to linked list */
                dap_http_header_t *l_hdr = DAP_NEW_Z(dap_http_header_t);
                if (l_hdr) {
                    size_t l_copy = (l_name_len < DAP_HTTP$SZ_FIELD_NAME - 1) ? l_name_len : DAP_HTTP$SZ_FIELD_NAME - 1;
                    memcpy(l_hdr->name, l_line, l_copy);
                    l_hdr->name[l_copy] = '\0';
                    l_hdr->namesz = l_copy;

                    l_copy = (l_val_len < DAP_HTTP$SZ_FIELD_VALUE - 1) ? l_val_len : DAP_HTTP$SZ_FIELD_VALUE - 1;
                    memcpy(l_hdr->value, l_val_start, l_copy);
                    l_hdr->value[l_copy] = '\0';
                    l_hdr->valuesz = l_copy;

                    l_hdr->prev = l_last_hdr;
                    l_hdr->next = NULL;
                    if (l_last_hdr)
                        l_last_hdr->next = l_hdr;
                    else
                        a_out->headers = l_hdr;
                    l_last_hdr = l_hdr;
                }
            }
        }
        l_line = l_line_end + 1;
    }

    if (l_body_start && l_content_length > 0) {
        a_out->body = DAP_NEW_SIZE(char, l_content_length + 1);
        memcpy(a_out->body, l_body_start, l_content_length);
        a_out->body[l_content_length] = '\0';
        a_out->body_len = l_content_length;
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/*  Synthetic HTTP struct builder — for calling enc_http / stream_ctl  */
/* ------------------------------------------------------------------ */

/* Build a minimal dap_http_simple_t + dap_http_client_t on the heap.
 * Caller must free with s_synthetic_http_free().
 * The KeyID header (if present in a_req->headers) is used by
 * dap_enc_ks_find_http() to locate the enc_init session key. */
static dap_http_simple_t *s_build_synthetic_http(const tls_parsed_request_t *a_req)
{
    dap_http_client_t *l_hc = DAP_NEW_Z(dap_http_client_t);
    if (!l_hc) return NULL;

    dap_http_simple_t *l_hs = DAP_NEW_Z(dap_http_simple_t);
    if (!l_hs) { DAP_DELETE(l_hc); return NULL; }

    /* Populate dap_http_client_t */
    strcpy(l_hc->action, "POST");
    l_hc->action_len = 4;
    if (a_req->url_path) {
        strncpy(l_hc->url_path, a_req->url_path, sizeof(l_hc->url_path) - 1);
        l_hc->url_path_len = strlen(a_req->url_path);
    }
    if (a_req->query_string)
        strncpy(l_hc->in_query_string, a_req->query_string, sizeof(l_hc->in_query_string) - 1);
    l_hc->in_headers = a_req->headers;
    /* in_cookie is left empty (TLS server doesn't use cookies) */

    /* Populate dap_http_simple_t */
    l_hs->http_client = l_hc;
    if (a_req->body && a_req->body_len > 0) {
        size_t l_alloc = a_req->body_len + 1;
        l_hs->request = DAP_NEW_SIZE(void, l_alloc);
        if (!l_hs->request) { DAP_DELETE(l_hs); DAP_DELETE(l_hc); return NULL; }
        memcpy(l_hs->request, a_req->body, a_req->body_len);
        l_hs->request_size = a_req->body_len;
        l_hs->request_size_max = l_alloc;
    }
    l_hs->reply_size_max = 140000;  /* Same as enc_http_proc registration size */

    return l_hs;
}

static void s_synthetic_http_free(dap_http_simple_t *a_hs)
{
    if (!a_hs) return;
    DAP_DELETE(a_hs->request);
    /* Note: we do NOT free a_hs->reply here — the caller owns the reply buffer
     * after enc_http_proc() / enc_http_reply_encode() writes to it. */
    DAP_DELETE(a_hs->http_client);
    DAP_DELETE(a_hs);
}

/* ------------------------------------------------------------------ */
/*  URL router — dispatches to enc_init or stream_ctl handler          */
/* ------------------------------------------------------------------ */

static int s_route_request(const tls_parsed_request_t *a_req,
                           char **a_response, size_t *a_response_len)
{
    *a_response = NULL;
    *a_response_len = 0;

    int l_rc = -1;

    /* Route by URL path prefix (skip leading '/') */
    const char *l_path = a_req->url_path;
    if (l_path && *l_path == '/') l_path++;

    if (l_path && strncmp(l_path, "enc_init", 8) == 0) {
        /* --- enc_init: encryption handshake --- */
        log_it(L_NOTICE, "TLS server: routing to enc_init");

        dap_http_simple_t *l_hs = s_build_synthetic_http(a_req);
        if (!l_hs) {
            log_it(L_ERROR, "TLS server: failed to build synthetic HTTP for enc_init");
            return -1;
        }

        /* enc_http_proc() checks url_path == "gd4y5yh78w42aaagh", so we need
         * to set it to the obfuscated path.  The real URL was "/enc_init?..."
         * but enc_http_proc expects the obfuscated internal URL.  Overwrite. */
        strncpy(l_hs->http_client->url_path, "gd4y5yh78w42aaagh",
                sizeof(l_hs->http_client->url_path) - 1);
        /* Copy query string to url_path (enc_http_proc reads query from url_path
         * via in_query_string, but the URL check is on url_path). The query string
         * is already in in_query_string from s_build_synthetic_http(). */

        dap_http_status_code_t l_return_code = DAP_HTTP_STATUS_BAD_REQUEST;
        enc_http_proc(l_hs, &l_return_code);

        if (l_hs->reply && l_hs->reply_size > 0) {
            /* Transfer reply ownership to caller */
            *a_response = (char *)l_hs->reply;
            *a_response_len = l_hs->reply_size;
            l_hs->reply = NULL;  /* prevent double-free in s_synthetic_http_free */
            l_rc = 0;
            log_it(L_NOTICE, "TLS server: enc_init reply %zu bytes (status=%d)",
                   l_hs->reply_size, l_return_code);
        } else {
            log_it(L_ERROR, "TLS server: enc_init produced no reply (status=%d)", l_return_code);
            l_rc = -1;
        }

        s_synthetic_http_free(l_hs);

    } else if (l_path && strncmp(l_path, "stream_ctl", 10) == 0) {
        /* --- stream_ctl: session creation --- */
        log_it(L_NOTICE, "TLS server: routing to stream_ctl");

        dap_http_simple_t *l_hs = s_build_synthetic_http(a_req);
        if (!l_hs) {
            log_it(L_ERROR, "TLS server: failed to build synthetic HTTP for stream_ctl");
            return -1;
        }

        /* enc_http_request_decode() decrypts URL path, query, and body using the
         * enc_init session key found via KeyID header.  After decode, the decrypted
         * URL path contains session parameters (channels, enc_type, etc.). */
        enc_http_delegate_t *l_dg = enc_http_request_decode(l_hs);
        if (!l_dg) {
            log_it(L_ERROR, "TLS server: stream_ctl enc_http_request_decode failed "
                   "(no KeyID or decryption error)");
            s_synthetic_http_free(l_hs);
            return -1;
        }

        /* Inline s_stream_ctl_proc() logic — creates a new stream session and
         * formats the response.  The original s_stream_ctl_proc() is static in
         * dap_stream_ctl.c and cannot be called directly. */
        dap_stream_session_t *l_session = NULL;
        char l_channels_str[16] = {0};
        dap_enc_key_type_t l_enc_type = dap_stream_get_preferred_encryption_type();
        size_t l_enc_key_size = 32;
        int l_enc_headers = 0;
        bool l_is_legacy = true;

        /* Parse decrypted URL path for parameters: "channels=X,enc_type=Y,..." */
        if (l_dg->url_path && l_dg->url_path_size > 0) {
            char *l_tok_tmp;
            char *l_tok = strtok_r(l_dg->url_path, ",", &l_tok_tmp);
            while (l_tok) {
                char *l_eq = strchr(l_tok, '=');
                if (l_eq && l_eq != l_tok) {
                    *l_eq++ = '\0';
                    if (strcmp(l_tok, "channels") == 0) {
                        strncpy(l_channels_str, l_eq, sizeof(l_channels_str) - 1);
                    } else if (strcmp(l_tok, "enc_type") == 0) {
                        l_enc_type = (dap_enc_key_type_t)atoi(l_eq);
                        l_is_legacy = false;
                    } else if (strcmp(l_tok, "enc_key_size") == 0) {
                        l_enc_key_size = (size_t)atoi(l_eq);
                        if (l_enc_key_size > l_dg->request_size)
                            l_enc_key_size = 32;
                        l_is_legacy = false;
                    } else if (strcmp(l_tok, "enc_headers") == 0) {
                        l_enc_headers = atoi(l_eq);
                    }
                }
                l_tok = strtok_r(NULL, ",", &l_tok_tmp);
            }
        }

        /* Look up KeyID to get ACL and node_addr for the session */
        dap_http_header_t *l_hdr_key_id = dap_http_header_find(a_req->headers, "KeyID");
        if (l_hdr_key_id) {
            if (l_is_legacy) {
                /* KeyID present but URL path decryption failed or no enc_type parsed.
                 * Key handshake is broken — fail fast. */
                log_it(L_ERROR, "TLS server: stream_ctl KeyID present but enc_type not parsed — rejecting");
                enc_http_delegate_delete(l_dg);
                s_synthetic_http_free(l_hs);
                return -1;
            }
        } else {
            /* No KeyID header — legacy mode (OAES) */
            log_it(L_INFO, "TLS server: stream_ctl legacy encryption mode (OAES)");
            l_enc_type = DAP_ENC_KEY_TYPE_OAES;
        }

        /* Create new stream session */
        l_session = dap_stream_session_pure_new();
        if (!l_session) {
            log_it(L_ERROR, "TLS server: stream_ctl failed to create session");
            enc_http_delegate_delete(l_dg);
            s_synthetic_http_free(l_hs);
            return -1;
        }

        snprintf(l_session->active_channels, sizeof(l_session->active_channels),
                 "%s", l_channels_str);

        /* Generate random session key */
        char *l_key_str = DAP_NEW_Z_SIZE(char, KEX_KEY_STR_SIZE + 1);
        dap_random_string_fill(l_key_str, KEX_KEY_STR_SIZE);
        l_session->key = dap_enc_key_new_generate(l_enc_type, l_key_str, KEX_KEY_STR_SIZE,
                                                   NULL, 0, 32 /* s_socket_forward_key.size */);

        /* Set ACL and node_addr from keystore key if KeyID present */
        if (l_hdr_key_id) {
            dap_enc_ks_key_t *l_ks_key = dap_enc_ks_find(l_hdr_key_id->value);
            if (l_ks_key) {
                l_session->acl = l_ks_key->acl_list;
                l_session->node = l_ks_key->node_addr;
                log_it(L_INFO, "TLS server: stream_ctl session %u node_addr set from KeyID",
                       l_session->id);
            } else {
                log_it(L_WARNING, "TLS server: stream_ctl KeyID '%s' not found in keystore",
                       l_hdr_key_id->value);
            }
        }

        /* Format reply: "<session_id> <key_str> [protocol_version enc_type enc_headers]" */
        if (l_is_legacy)
            enc_http_reply_f(l_dg, "%u %s", l_session->id, l_key_str);
        else
            enc_http_reply_f(l_dg, "%u %s %u %d %d", l_session->id, l_key_str,
                              DAP_PROTOCOL_VERSION, l_enc_type, l_enc_headers);

        /* Encrypt the reply using the enc_init session key */
        enc_http_reply_encode(l_hs, l_dg);

        /* Extract encrypted reply */
        if (l_hs->reply && l_hs->reply_size > 0) {
            *a_response = (char *)l_hs->reply;
            *a_response_len = l_hs->reply_size;
            l_hs->reply = NULL;  /* prevent double-free */
            l_rc = 0;
            log_it(L_NOTICE, "TLS server: stream_ctl session %u created, reply %zu bytes "
                   "(enc_type=%d, enc_headers=%d, legacy=%d)",
                   l_session->id, l_hs->reply_size, l_enc_type, l_enc_headers, l_is_legacy);
        } else {
            log_it(L_ERROR, "TLS server: stream_ctl produced no reply");
            l_rc = -1;
        }

        /* Cleanup — do NOT delete the enc_init key from keystore.
         * It must remain until the stream session closes. */
        enc_http_delegate_delete(l_dg);
        DAP_DELETE(l_key_str);
        s_synthetic_http_free(l_hs);

    } else {
        log_it(L_WARNING, "TLS server: unknown URL path '%s'",
               a_req->url_path ? a_req->url_path : "(null)");
        l_rc = -1;
    }

    return l_rc;
}

/* ------------------------------------------------------------------ */
/*  Forward declarations                                               */
/* ------------------------------------------------------------------ */

static void s_tls_read(dap_events_socket_t *a_es, void *a_arg);
static bool s_tls_write(dap_events_socket_t *a_es, void *a_arg);
static void s_tls_delete(dap_events_socket_t *a_es, void *a_arg);
static void s_tls_error(dap_events_socket_t *a_es, int a_error);

/* ------------------------------------------------------------------ */
/*  Read: TLS handshake → route request → TLS wrap response            */
/* ------------------------------------------------------------------ */

static void s_tls_read(dap_events_socket_t *a_es, void *a_arg)
{
    tls_conn_ctx_t *t = s_tls_ctx(a_es);
    if (!t) return;

    /* New data arrived — clear the TLS wrapping flag only if buf_out was
     * fully flushed.  If there's still pending wrapped data in buf_out
     * (partial send), don't clear — s_tls_write will detect via TLS content type. */
    (void)a_es->buf_out_size;

    /* Guard: if buf_in hasn't changed since we last exited, skip.
     * Prevents tight loop when unwrap returns rc=1 (need more data)
     * and event loop re-enters with same buffer. */
    if (a_es->buf_in_size > 0 && a_es->buf_in_size == t->prev_buf_in_size)
        return;

    debug_if(s_debug_more, L_DEBUG, "TLS server: s_tls_read fd=%d, buf_in_size=%zu, t=%p",
           a_es ? a_es->socket : -1, a_es ? a_es->buf_in_size : 0, (void*)t);

    /* === TLS handshake phase === */
    if (!t->handshake_done) {
        if (a_es->buf_in_size == 0) return;

        if (!t->mimicry) {
            t->mimicry = dap_tls_mimicry_new(true);
            log_it(L_NOTICE, "TLS server: mimicry_new=%p", (void*)t->mimicry);
            if (!t->mimicry) {
                dap_events_socket_remove_and_delete_unsafe(a_es, true);
                return;
            }
        }

        void *l_resp = NULL; size_t l_resp_sz = 0;
        int l_rc = dap_tls_mimicry_process_client_hello(
            t->mimicry, a_es->buf_in, a_es->buf_in_size, &l_resp, &l_resp_sz);
        a_es->buf_in_size = 0;
        log_it(L_NOTICE, "TLS server: process_client_hello rc=%d, resp_sz=%zu", l_rc, l_resp_sz);

        if (l_rc < 0) {
            DAP_DELETE(l_resp);
            dap_events_socket_remove_and_delete_unsafe(a_es, true);
            return;
        }

        if (l_resp && l_resp_sz > 0) {
            dap_events_socket_write_unsafe(a_es, l_resp, l_resp_sz);
            DAP_DELETE(l_resp);
        }

        t->handshake_done = true;
        debug_if(s_debug_more, L_DEBUG, "TLS handshake done");
        return;
    }

    /* === Post-handshake: skip CCS+Finished, unwrap TLS, route request === */
    if (!t->mimicry || a_es->buf_in_size == 0) return;

    /* Skip CCS+Finished from client */
    if (!t->client_finished_consumed) {
        uint8_t *d = a_es->buf_in;
        size_t pos = 0, sz = a_es->buf_in_size;

        while (pos + 5 <= sz && d[pos] == TLS_CT_CHANGE_CIPHER_SPEC) {
            uint16_t len = ((uint16_t)d[pos+3] << 8) | d[pos+4];
            if (pos + 5 + len > sz) break;
            pos += 5 + len;
        }
        if (pos + 5 <= sz && d[pos] == TLS_CT_APPLICATION_DATA) {
            uint16_t len = ((uint16_t)d[pos+3] << 8) | d[pos+4];
            if (pos + 5 + len <= sz) pos += 5 + len;
        }
        t->client_finished_consumed = true;
        log_it(L_NOTICE, "TLS server: CCS skip pos=%zu, sz=%zu, first_byte=0x%02X", pos, sz, sz > 0 ? d[0] : 0);

        if (pos > 0) {
            if (pos < sz) {
                memmove(a_es->buf_in, a_es->buf_in + pos, sz - pos);
                a_es->buf_in_size -= pos;
            } else {
                a_es->buf_in_size = 0;
                return;
            }
        }
    }

    if (a_es->buf_in_size == 0) return;

    /* Unwrap TLS APPLICATION_DATA records */
    void *l_raw = NULL; size_t l_raw_sz = 0, l_consumed = 0;
    int l_rc = dap_tls_mimicry_unwrap(t->mimicry,
        a_es->buf_in, a_es->buf_in_size, &l_raw, &l_raw_sz, &l_consumed);
    debug_if(s_debug_more, L_DEBUG, "TLS server: unwrap rc=%d, consumed=%zu, raw_sz=%zu, buf_in_size=%zu",
           l_rc, l_consumed, l_raw_sz, a_es->buf_in_size);

    if (l_consumed > 0) {
        if (l_consumed < a_es->buf_in_size) {
            memmove(a_es->buf_in, a_es->buf_in + l_consumed,
                    a_es->buf_in_size - l_consumed);
            a_es->buf_in_size -= l_consumed;
        } else {
            a_es->buf_in_size = 0;
        }
    }

    if (l_rc != 0 || !l_raw || l_raw_sz == 0) {
        DAP_DELETE(l_raw);
        t->prev_buf_in_size = a_es->buf_in_size;  /* update guard at exit */
        return;
    }

    /* After stream_ctl, data is DAP stream packets (not HTTP POST).
     * Detect deterministically by the DAP stream packet signature
     * ({0xa0,0x95,0x96,0xa9,...} — see c_dap_stream_sig), not by absence of "POST".
     * HTTP POST starts with ASCII 'P' (0x50), which can never match the binary
     * signature's first byte 0xa0, so the two cases are unambiguous. */
    if (!t->stream_mode && l_raw_sz >= STREAM_PKT_SIG_SIZE
            && memcmp(l_raw, c_dap_stream_sig, STREAM_PKT_SIG_SIZE) == 0) {
        t->stream_mode = true;
        log_it(L_NOTICE, "TLS server: switching to stream mode (DAP stream signature detected, %zu bytes)", l_raw_sz);
    }

    if (t->stream_mode) {
        /* DAP stream packets — need stream layer to process.
         * _inheritor is tls_conn_ctx_t, not trans_ctx.  The stream
         * pointer lives in t->stream, set after stream_ctl. */
        dap_stream_t *l_stream = t->stream;
        if (l_stream) {
            dap_stream_data_proc_read_ext(l_stream, l_raw, l_raw_sz);
        } else {
            /* No stream — this is expected if stream wasn't created via HTTP path.
             * The stream_ctl handler should have created the session, but the
             * stream object itself is managed by the HTTP server layer. */
            log_it(L_WARNING, "TLS server: stream mode but no stream (dropping %zu bytes)", l_raw_sz);
        }
        DAP_DELETE(l_raw);
        t->prev_buf_in_size = a_es->buf_in_size;
        return;
    }

    /* Parse HTTP POST: extract URL, query, body, and headers */
    tls_parsed_request_t l_req;
    s_parse_http_post((char *)l_raw, l_raw_sz, &l_req);
    DAP_DELETE(l_raw);

    log_it(L_NOTICE, "TLS server: HTTP POST %s (body=%zu bytes, raw_sz=%zu)",
           l_req.url_path ? l_req.url_path : "?", l_req.body_len, l_raw_sz);

    /* Route request to handler (enc_init or stream_ctl) */
    char *l_response = NULL;
    size_t l_response_len = 0;
    s_route_request(&l_req, &l_response, &l_response_len);

    /* After stream_ctl response, create stream and switch to stream mode */
    if (l_req.url_path && strncmp(l_req.url_path, "/stream_ctl", 11) == 0 && l_response && l_response_len > 0) {
        /* Parse session_id from response: "<session_id> <key_str> ..." */
        unsigned l_session_id = 0;
        if (sscanf(l_response, "%u", &l_session_id) == 1 && l_session_id > 0) {
            dap_stream_session_t *l_session = dap_stream_session_id_mt(l_session_id);
            if (l_session) {
                dap_cluster_node_addr_t *l_node_addr = &l_session->node;
                dap_events_socket_callbacks_t l_saved_cbs = a_es->callbacks;
                dap_stream_t *l_stream = dap_stream_new_es_client(a_es, l_node_addr, false);
                if (l_stream) {
                    l_stream->trans = dap_net_trans_find(DAP_NET_TRANS_TLS_DIRECT);
                    if (a_es->worker)
                        l_stream->stream_worker = DAP_STREAM_WORKER(a_es->worker);
                    l_stream->is_client_to_uplink = false;
                    l_stream->session = l_session;
                    dap_stream_session_open(l_session);
                    /* Restore our TLS callbacks — they handle TLS wrap/unwrap.
                     * The stream's transport (dap_stream_trans_tls) is NOT used on the
                     * server side for wrap/unwrap — our s_tls_read/s_tls_write do that. */
                    a_es->callbacks = l_saved_cbs;
                    /* Restore _inheritor to our tls_conn_ctx_t. */
                    a_es->_inheritor = t;
                    /* Save stream in tls_conn_ctx for s_tls_read/s_tls_delete. */
                    t->stream = l_stream;
                    /* Mimicry handoff: the stream transport's close()
                     * (dap_stream_trans_tls.c::s_tls_close) frees trans_ctx->_inheritor.
                     * We set trans_ctx->_inheritor = NULL to prevent double-free of our
                     * mimicry (which stays in tls_conn_ctx_t and is freed by s_tls_delete).
                     * Full mimicry handoff (transferring keys) is a TODO for now — the TLS
                     * server uses its own mimicry for the connection lifetime. */
                    if (l_stream->trans_ctx)
                        l_stream->trans_ctx->_inheritor = NULL;
                    log_it(L_NOTICE, "TLS server: stream created (session=%u, stream=%p, trans=%p)",
                           l_session_id, (void*)l_stream, (void*)l_stream->trans);
                } else {
                    log_it(L_ERROR, "TLS server: failed to create stream for session %u", l_session_id);
                }
            } else {
                log_it(L_WARNING, "TLS server: session %u not found after stream_ctl", l_session_id);
            }
        }
        t->stream_mode = true;
        log_it(L_NOTICE, "TLS server: stream_ctl processed, stream_mode=%s", t->stream ? "YES" : "NO");
    }

    log_it(L_NOTICE, "TLS server: route result: response_len=%zu, response=%p", l_response_len, (void*)l_response);

    /* Wrap response in TLS records and send */
    if (l_response && l_response_len > 0 && t->mimicry) {
        void *l_wrapped = NULL; size_t l_wrapped_sz = 0;
        int l_wrap_rc = dap_tls_mimicry_wrap(t->mimicry, l_response, l_response_len,
                                  &l_wrapped, &l_wrapped_sz);
        log_it(L_NOTICE, "TLS server: wrap rc=%d, wrapped_sz=%zu, response_len=%zu", l_wrap_rc, l_wrapped_sz, l_response_len);
        if (l_wrap_rc == 0 && l_wrapped && l_wrapped_sz > 0) {
            dap_events_socket_write_unsafe(a_es, l_wrapped, l_wrapped_sz);
            DAP_DELETE(l_wrapped);
            log_it(L_NOTICE, "TLS server: response sent (%zu bytes)", l_wrapped_sz);
        }
        DAP_DELETE(l_response);
    } else {
        log_it(L_NOTICE, "TLS server: no response to send (resp=%p, resp_len=%zu, mimicry=%p)",
               (void*)l_response, l_response_len, t ? (void*)t->mimicry : NULL);
        DAP_DELETE(l_response);
    }
    t->prev_buf_in_size = a_es->buf_in_size;  /* update guard at exit */
    s_parsed_request_free(&l_req);
}

/* ------------------------------------------------------------------ */
/*  Write: TLS-wrap buf_out data before event-loop send()              */
/* ------------------------------------------------------------------ */

static bool s_tls_write(dap_events_socket_t *a_es, void *a_arg)
{
    if (!a_es)
        return false;
    tls_conn_ctx_t *t = s_tls_ctx(a_es);
    if (!t)
        return true;

    /* If buf_out is empty, nothing to do */
    if (a_es->buf_out_size == 0)
        return false;

    if (!t->mimicry)
        return true;  /* no mimicry context — send raw */

    /* Detect already-wrapped data by checking the TLS record content-type byte.
     * Both init responses (ServerHello from process_client_hello starts with
     * 0x16, enc_init/stream_ctl responses from s_tls_read start with 0x17) and
     * stream-mode data wrapped by a previous s_tls_write call are valid TLS
     * records.  Raw DAP stream packets never start with a TLS content type
     * (first byte is 0xa0 from c_dap_stream_sig), so this is reliable. */
    if (a_es->buf_out_size >= 5 &&
        s_is_tls_record_content(((const uint8_t *)a_es->buf_out)[0])) {
        /* Already wrapped — flush as-is (no re-wrap). */
        return false;
    }

    /* Stream mode: wrap raw buf_out in TLS records */
    void *l_wrapped = NULL; size_t l_wrapped_sz = 0;
    int l_rc = dap_tls_mimicry_wrap(t->mimicry,
                                    a_es->buf_out, a_es->buf_out_size,
                                    &l_wrapped, &l_wrapped_sz);
    if (l_rc != 0 || !l_wrapped || l_wrapped_sz == 0) {
        log_it(L_WARNING, "TLS server: s_tls_write wrap failed rc=%d", l_rc);
        return true;  /* send raw as fallback */
    }

    /* Replace buf_out contents with TLS-wrapped data */
    if (l_wrapped_sz > a_es->buf_out_size_max) {
        byte_t *l_new = DAP_NEW_SIZE(byte_t, l_wrapped_sz);
        if (!l_new) { DAP_DELETE(l_wrapped); return true; }
        DAP_DELETE(a_es->buf_out);
        a_es->buf_out = l_new;
        a_es->buf_out_size_max = l_wrapped_sz;
    }
    memcpy(a_es->buf_out, l_wrapped, l_wrapped_sz);
    a_es->buf_out_size = l_wrapped_sz;
    DAP_DELETE(l_wrapped);
    t->buf_out_wrapped = true;
    return false;  /* data ready to send */
}

/* ------------------------------------------------------------------ */
/*  Delete / Error                                                      */
/* ------------------------------------------------------------------ */

static void s_tls_delete(dap_events_socket_t *a_es, void *a_arg)
{
    tls_conn_ctx_t *t = s_tls_ctx(a_es);
    if (!t) return;

    log_it(L_NOTICE, "TLS server: delete fd=%d, t=%p, mimicry=%p, stream=%p",
           a_es ? a_es->socket : -1, (void*)t, (void*)t->mimicry, (void*)t->stream);

    /* Clean up stream if created (stream_ctl phase).
     * We NULL out trans_ctx->esocket to prevent dap_stream_delete_unsafe
     * from trying to remove_and_delete the esocket again (we're already in
     * the delete path). */
    if (t->stream) {
        dap_stream_t *l_stream = t->stream;
        t->stream = NULL;
        if (l_stream->trans_ctx) {
            l_stream->trans_ctx->esocket = NULL;
            l_stream->trans_ctx->esocket_uuid = 0;
            l_stream->trans_ctx->esocket_worker = NULL;
        }
        /* Set a NULL trans so close() doesn't try to access esocket */
        l_stream->trans = NULL;
        dap_stream_delete_unsafe(l_stream);
    }

    if (t->mimicry) dap_tls_mimicry_free(t->mimicry);
    a_es->_inheritor = NULL;
    DAP_DELETE(t);
}

static void s_tls_error(dap_events_socket_t *a_es, int a_error)
{
    log_it(L_NOTICE, "TLS server: error fd=%d, err=%d", a_es ? a_es->socket : -1, a_error);
}

/* ------------------------------------------------------------------ */
/*  New connection                                                      */
/* ------------------------------------------------------------------ */

static void s_tls_client_new(dap_events_socket_t *a_es, void *a_arg)
{
    log_it(L_NOTICE, "TLS server: new client fd=%d, read_cb=%p", a_es ? a_es->socket : -1,
           a_es ? (void*)a_es->callbacks.read_callback : NULL);

    tls_conn_ctx_t *t = DAP_NEW_Z(tls_conn_ctx_t);
    if (!t) return;

    t->handshake_done = false;
    t->client_finished_consumed = false;
    t->mimicry = NULL;

    a_es->_inheritor = t;
    a_es->callbacks.read_callback = s_tls_read;
    a_es->callbacks.write_callback = s_tls_write;
    a_es->callbacks.delete_callback = s_tls_delete;
    a_es->callbacks.error_callback = s_tls_error;
}

/* ------------------------------------------------------------------ */
/*  Server ops                                                          */
/* ------------------------------------------------------------------ */

typedef struct { char name[64]; dap_server_t *server; } tls_server_ctx_t;

/* Client callbacks — used for accepted TLS client connections.
 * accept_callback is NULL because dap_server's internal s_es_server_accept
 * handles the accept() system call and creates the client esocket with these
 * callbacks.  new_callback is our entry point to install TLS state. */
static dap_events_socket_callbacks_t s_tls_client_cbs = {
    .new_callback    = s_tls_client_new,
};

static void *s_tls_server_new(const char *a_name)
{
    tls_server_ctx_t *c = DAP_NEW_Z(tls_server_ctx_t);
    if (c) dap_strncpy(c->name, a_name, sizeof(c->name) - 1);
    return c;
}

static int s_tls_server_start(void *a_server, const char *a_cfg,
                              const char **a_addrs, uint16_t *a_ports, size_t a_count)
{
    tls_server_ctx_t *c = (tls_server_ctx_t *)a_server;
    if (!c || !a_ports || !a_count) return -1;

    const char *l_addr = (a_addrs && a_addrs[0]) ? a_addrs[0] : "0.0.0.0";
    uint16_t l_port = a_ports[0];

    /* Minimal server — no HTTP server, no URL routing.
     * TLS server handles everything directly. */
    dap_server_t *l_server = dap_server_new(NULL, NULL, &s_tls_client_cbs);
    if (!l_server) return -1;

    /* Listener uses dap_server's internal accept/new handlers (NULL callbacks).
     * Client connections get s_tls_client_cbs (set above via dap_server_new). */
    int l_rc = dap_server_listen_addr_add(l_server, l_addr, l_port,
                                           DESCRIPTOR_TYPE_SOCKET_LISTENING,
                                           NULL);
    if (l_rc != 0) {
        dap_server_delete(l_server);
        return -1;
    }

    c->server = l_server;
    log_it(L_NOTICE, "TLS mimicry server listening on %s:%u", l_addr, l_port);
    return 0;
}

static void s_tls_server_stop(void *a_server)
{
    tls_server_ctx_t *c = (tls_server_ctx_t *)a_server;
    if (c && c->server) { dap_server_delete(c->server); c->server = NULL; }
}

static void s_tls_server_delete(void *a_server)
{
    tls_server_ctx_t *c = (tls_server_ctx_t *)a_server;
    if (c) { if (c->server) dap_server_delete(c->server); DAP_DELETE(c); }
}

static const dap_net_trans_server_ops_t s_tls_ops = {
    .new = s_tls_server_new, .start = s_tls_server_start,
    .stop = s_tls_server_stop, .delete = s_tls_server_delete,
};

int dap_net_trans_tls_server_init(void)
{
    int r = dap_net_trans_server_register_ops(DAP_NET_TRANS_TLS_DIRECT, &s_tls_ops);
    if (r == 0) log_it(L_NOTICE, "TLS mimicry server module initialized");
    return r;
}

void dap_net_trans_tls_server_deinit(void)
{
    dap_net_trans_server_unregister_ops(DAP_NET_TRANS_TLS_DIRECT);
}
