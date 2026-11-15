/**
 * @file reality_bridge.c
 * @brief Thin REALITY-specific wrapper over the shared carrier bridge.
 *
 * The pump logic now lives in net/trans/carrier (carrier_bridge.c); this file
 * only adapts REALITY's session/io ports and error-code space to the carrier
 * contract. The public API (reality_bridge.h) and observable behavior
 * (ctrl events 'R'/'E'+code/'C', state values, FD ownership) are unchanged.
 *
 * No SDK dependencies (carrier + reality ports are SDK-free).
 */

#include "reality_bridge.h"
#include "reality_port.h"
#include "carrier/carrier.h"

#include <stdlib.h>
#include <string.h>

/* Per-bridge context: REALITY config + the io port created on the pump thread
 * (io_connect runs before session.connect on the same thread). */
typedef struct {
    reality_bridge_cfg_t cfg;
    reality_io_ops_t    *cur_rio;
} r_ctx_t;

struct reality_bridge {
    carrier_bridge_t *cb;
    r_ctx_t           ctx;
};

/* ---- io port adaptation: reality_io_ops -> carrier_io_ops ---- */
typedef struct { reality_io_ops_t *rio; } r_io_wrap_t;

static int r_io_read(void *u, uint8_t *b, size_t l, size_t *o)
{
    r_io_wrap_t *w = u; return w->rio->read(w->rio->user, b, l, o);
}
static int r_io_write(void *u, const uint8_t *b, size_t l)
{
    r_io_wrap_t *w = u; return w->rio->write(w->rio->user, b, l);
}
static void r_io_close(void *u)
{
    r_io_wrap_t *w = u;
    reality_io_socket_free(w->rio);
    free(w);
}
static int r_io_fd(void *u) { return reality_io_socket_fd(((r_io_wrap_t *)u)->rio); }
static void r_io_shutdown(void *u) { reality_io_socket_shutdown(((r_io_wrap_t *)u)->rio); }

static carrier_io_ops_t *r_io_connect(void *user, const char *host, uint16_t port,
                                      int timeout_sec, const char *iface)
{
    r_ctx_t *ctx = user;
    reality_io_ops_t *rio = reality_io_socket_connect(host, port, timeout_sec, iface);
    if (!rio) return NULL;
    ctx->cur_rio = rio;
    r_io_wrap_t *w = (r_io_wrap_t *)calloc(1, sizeof(*w));
    carrier_io_ops_t *ops = (carrier_io_ops_t *)calloc(1, sizeof(*ops));
    if (!w || !ops) { free(w); free(ops); reality_io_socket_free(rio); ctx->cur_rio = NULL; return NULL; }
    w->rio = rio;
    ops->user = w;
    ops->read = r_io_read; ops->write = r_io_write; ops->close = r_io_close;
    ops->fd = r_io_fd; ops->shutdown = r_io_shutdown;
    return ops;
}

/* ---- session port adaptation: reality_session_* (codes pass through) ---- */
static void *r_sess_connect(void *user, carrier_io_ops_t *io)
{
    (void)io;
    r_ctx_t *ctx = user;
    return reality_session_connect(&ctx->cfg.rcfg, ctx->cur_rio,
                                   reality_crypto_openssl(), reality_time_std(), NULL,
                                   ctx->cfg.dest_host, ctx->cfg.dest_port);
}
static int r_sess_read(void *user, void *s, uint8_t *b, size_t l, size_t *o)
{
    (void)user; return reality_session_read((reality_session_t *)s, b, l, o);
}
static int r_sess_write(void *user, void *s, const uint8_t *b, size_t l)
{
    (void)user; return reality_session_write((reality_session_t *)s, b, l);
}
static bool r_sess_pending(void *user, void *s)
{
    (void)user; return reality_session_has_pending((reality_session_t *)s);
}
static void r_sess_close(void *user, void *s)
{
    (void)user; reality_session_close((reality_session_t *)s);
}

static reality_bridge_t *bridge_start_common(const reality_bridge_cfg_t *a_cfg, int a_ext_fd)
{
    if (!a_cfg || !a_cfg->host[0] || !a_cfg->port || !a_cfg->dest_host[0]) return NULL;

    reality_bridge_t *b = (reality_bridge_t *)calloc(1, sizeof(*b));
    if (!b) return NULL;
    b->ctx.cfg = *a_cfg;

    carrier_bridge_cfg_t ccfg;
    memset(&ccfg, 0, sizeof(ccfg));
    strncpy(ccfg.host, a_cfg->host, sizeof(ccfg.host) - 1);
    ccfg.port = a_cfg->port;
    ccfg.timeout_sec = a_cfg->timeout_sec;
    strncpy(ccfg.iface, a_cfg->iface, sizeof(ccfg.iface) - 1);
    ccfg.io_connect = r_io_connect;
    ccfg.io_user = &b->ctx;
    ccfg.session.user = &b->ctx;
    ccfg.session.connect = r_sess_connect;
    ccfg.session.read = r_sess_read;
    ccfg.session.write = r_sess_write;
    ccfg.session.has_pending = r_sess_pending;
    ccfg.session.close = r_sess_close;
    /* REALITY error-code space passes through unchanged (events/state). */
    ccfg.err_io = REALITY_ERR_IO;
    ccfg.err_closed = REALITY_ERR_CLOSED;
    ccfg.err_again = REALITY_ERR_AGAIN;

    b->cb = a_ext_fd < 0 ? carrier_bridge_start(&ccfg)
                         : carrier_bridge_start_fd(&ccfg, a_ext_fd);
    if (!b->cb) { free(b); return NULL; }
    return b;
}

reality_bridge_t *reality_bridge_start(const reality_bridge_cfg_t *a_cfg)
{
    return bridge_start_common(a_cfg, -1);
}

reality_bridge_t *reality_bridge_start_fd(const reality_bridge_cfg_t *a_cfg, int a_ext_fd)
{
    if (a_ext_fd < 0) return NULL;
    return bridge_start_common(a_cfg, a_ext_fd);
}

void reality_bridge_wait(reality_bridge_t *b)
{
    if (b) carrier_bridge_wait(b->cb);
}

int reality_bridge_data_fd(reality_bridge_t *b) { return b ? carrier_bridge_data_fd(b->cb) : -1; }
int reality_bridge_ctrl_fd(reality_bridge_t *b) { return b ? carrier_bridge_ctrl_fd(b->cb) : -1; }
int reality_bridge_state(reality_bridge_t *b)   { return b ? carrier_bridge_state(b->cb) : REALITY_ERR_PARAM; }

void reality_bridge_signal_stop(reality_bridge_t *b)
{
    if (b) carrier_bridge_signal_stop(b->cb);
}

void reality_bridge_stop(reality_bridge_t *b)
{
    if (!b) return;
    carrier_bridge_stop(b->cb);
    free(b);
}