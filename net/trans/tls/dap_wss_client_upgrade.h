/* Client HTTP Upgrade over an authenticated TLS byte stream. No socket owner
 * changes. The owner calls start after pin verification, then feeds plaintext. */
#ifndef DAP_WSS_CLIENT_UPGRADE_H
#define DAP_WSS_CLIENT_UPGRADE_H
#include "dap_http_input.h"
#include "../websocket/dap_ws_accept.h"
#include "../websocket/dap_ws_upgrade_response.h"
#include <stdio.h>

typedef struct dap_wss_client_upgrade {
    dap_http_input_t headers;
    char expected_accept[29];
    bool started, ready, failed;
} dap_wss_client_upgrade_t;

static int dap_wss_client_upgrade_start(dap_wss_client_upgrade_t *u,
    const char *host, const char *path, const char *key,
    int (*write)(void *, const void *, size_t), void *arg)
{
    if (!u || u->started || !host || !*host || !path || path[0]!='/' || !key || !write) return -1;
    for (const unsigned char *p=(const unsigned char *)host; *p; ++p)
        if (*p<=32 || *p==127) return -1;
    for (const unsigned char *p=(const unsigned char *)path; *p; ++p)
        if (*p<=32 || *p==127 || *p=='#') return -1;
    if (dap_ws_accept_key(key,u->expected_accept,sizeof(u->expected_accept))) return -1;
    char request[2048];
    int n=snprintf(request,sizeof(request),
        "GET %s HTTP/1.1\r\nHost: %s\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
        "Sec-WebSocket-Version: 13\r\nSec-WebSocket-Key: %s\r\n\r\n",path,host,key);
    if (n<0 || (size_t)n>=sizeof(request)) return -1;
    if (dap_http_input_init(&u->headers,DAP_WS_UPGRADE_HEADERS_MAX+16384U)) return -1;
    u->started=true;
    if (write(arg,request,(size_t)n)) {u->failed=true;return -1;}
    return 0;
}

/* ready callback may enqueue ENC; destruction must be deferred until return.
 * The tail callback consumes/copies all supplied bytes. */
static int dap_wss_client_upgrade_feed(dap_wss_client_upgrade_t *u,
    const void *data,size_t size, int (*ready)(void *),
    int (*tail)(void *,const void *,size_t),void *arg)
{
    if (!u || !u->started || u->failed || !ready || !tail) return -1;
    if (u->ready) return tail(arg,data,size);
    if (dap_http_input_append(&u->headers,data,size)) goto fail;
    size_t length=0;
    int rc=dap_ws_upgrade_header_size(u->headers.data,u->headers.size,&length);
    if (!rc) return 0;
    if (rc<0 || !dap_ws_upgrade_response_valid(u->headers.data,length,u->expected_accept,NULL)) goto fail;
    u->ready=true;
    if (ready(arg)) goto fail;
    if (u->headers.size>length && tail(arg,u->headers.data+length,u->headers.size-length)) goto fail;
    dap_http_input_clear(&u->headers);
    return 0;
fail:
    u->failed=true;
    dap_http_input_clear(&u->headers);
    return -1;
}
static void dap_wss_client_upgrade_clear(dap_wss_client_upgrade_t *u)
{
    if (!u) return;
    dap_http_input_clear(&u->headers);
    memset(u,0,sizeof(*u));
}
#endif
