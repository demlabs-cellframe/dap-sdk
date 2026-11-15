/**
 * @file test_unit.c
 * @brief Offline unit tests for the carrier layer (no SDK, no network).
 *        Covers: portable pipe, trust contract validation, and the bridge
 *        pump with a mock pass-through session over a pipe "network".
 */

#include "carrier/carrier.h"
#include "carrier/carrier_trust.h"
#include "carrier_port.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>

#define LOG(...) do { printf("[carrier-unit] " __VA_ARGS__); printf("\n"); } while (0)

/* ------------------------------------------------------------------ */
static void test_pipe_roundtrip(void)
{
    LOG("test_pipe_roundtrip");
    int fds[2];
    assert(carrier_pipe_pair(fds) == 0);
    assert(fds[0] >= 0 && fds[1] >= 0);
    const char msg[] = "carrier-pipe";
    assert(send((carrier_fd_t)fds[0], msg, sizeof(msg), CARRIER_SEND_FLAGS) == (ssize_t)sizeof(msg));
    char buf[64] = {0};
    assert(recv((carrier_fd_t)fds[1], buf, sizeof(buf), 0) == (ssize_t)sizeof(msg));
    assert(strcmp(buf, msg) == 0);
    carrier_pipe_close(fds[0]);
    carrier_pipe_close(fds[1]);
}

/* ------------------------------------------------------------------ */
static void test_trust_contract(void)
{
    LOG("test_trust_contract");
    carrier_trust_t t;
    memset(&t, 0, sizeof(t));
    strcpy(t.host, "node.example");
    t.port = 443;

    /* no mode -> not prepared (never verify-none) */
    assert(!carrier_trust_prepared(&t, 1000));

    /* pin mode requires a well-formed pin */
    t.mode = CARRIER_TRUST_SPKI_PIN;
    assert(!carrier_trust_prepared(&t, 1000));
    strcpy(t.spki_sha256, "zzz"); /* not hex64 */
    assert(!carrier_trust_prepared(&t, 1000));
    strcpy(t.spki_sha256, "aa02397b30763a6a2140dbd2916e502f3e1acb176c1ef654645bbf2332523a3a");
    assert(carrier_trust_prepared(&t, 1000));
    /* full-DER pin alone also valid (Xray interop) */
    memset(t.spki_sha256, 0, sizeof(t.spki_sha256));
    strcpy(t.cert_sha256, "7f05d41a9dc87e01fdc031ba2d042414e0e387c1081eeebcdad7158c9ed37c24");
    assert(carrier_trust_prepared(&t, 1000));

    /* system_ca requires server name */
    memset(&t, 0, sizeof(t));
    strcpy(t.host, "mask.example"); t.port = 443;
    t.mode = CARRIER_TRUST_SYSTEM_CA;
    assert(!carrier_trust_prepared(&t, 1000));
    strcpy(t.tls_server_name, "www.samsung.com");
    assert(carrier_trust_prepared(&t, 1000));

    /* private_ca requires CA reference + name */
    t.mode = CARRIER_TRUST_PRIVATE_CA;
    assert(!carrier_trust_prepared(&t, 1000));
    strcpy(t.ca_reference, "kelvpn-ca");
    assert(carrier_trust_prepared(&t, 1000));

    /* validity window */
    t.valid_from = 2000; t.valid_until = 3000;
    assert(!carrier_trust_prepared(&t, 1000));
    assert(carrier_trust_prepared(&t, 2500));
    assert(!carrier_trust_prepared(&t, 4000));

    assert(carrier_trust_is_hex64("AA02397b30763a6a2140dbd2916e502f3e1acb176c1ef654645bbf2332523a3a"));
    assert(!carrier_trust_is_hex64(NULL));
    assert(!carrier_trust_is_hex64(""));
}

/* ------------------------------------------------------------------ */
/* Mock pass-through session over a pipe "network": the test holds one
 * pipe end (the "remote"), the bridge pumps the other end via the session. */
typedef struct {
    int net_fd;          /* bridge-side network pipe end */
    carrier_io_ops_t *io;
} mock_net_t;

static int mock_read(void *u, uint8_t *b, size_t l, size_t *o)
{
    mock_net_t *n = u;
    ssize_t r = recv((carrier_fd_t)n->net_fd, (char *)b, (int)l, 0);
    if (r > 0) { *o = (size_t)r; return 0; }
    if (r == 0) { *o = 0; return CARRIER_ERR_CLOSED; }
    int e = carrier_errno();
    if (e == CARRIER_EAGAIN || e == CARRIER_EWOULDBLOCK || e == CARRIER_EINTR) { *o = 0; return CARRIER_ERR_AGAIN; }
    return CARRIER_ERR_IO;
}
static int mock_write(void *u, const uint8_t *b, size_t l)
{
    mock_net_t *n = u;
    size_t off = 0;
    while (off < l) {
        ssize_t w = send((carrier_fd_t)n->net_fd, (const char *)b + off, (int)(l - off), CARRIER_SEND_FLAGS);
        if (w <= 0) return CARRIER_ERR_IO;
        off += (size_t)w;
    }
    return 0;
}
static void mock_close(void *u) { mock_net_t *n = u; if (n->net_fd >= 0) { carrier_pipe_close(n->net_fd); n->net_fd = -1; } }
static int mock_fd(void *u) { return ((mock_net_t *)u)->net_fd; }
static void mock_shutdown(void *u) { int fd = ((mock_net_t *)u)->net_fd; if (fd >= 0) shutdown((carrier_fd_t)fd, SHUT_RDWR); }

static carrier_io_ops_t *mock_io_connect(void *user, const char *h, uint16_t p, int to, const char *iface)
{
    (void)h; (void)p; (void)to; (void)iface;
    mock_net_t *n = user;
    carrier_io_ops_t *ops = calloc(1, sizeof(*ops));
    if (!ops) return NULL;
    ops->user = n; ops->read = mock_read; ops->write = mock_write;
    ops->close = mock_close; ops->fd = mock_fd; ops->shutdown = mock_shutdown;
    n->io = ops;
    return ops;
}

static void *mock_sess_connect(void *u, carrier_io_ops_t *io) { (void)io; return u; }
static int mock_sess_read(void *u, void *s, uint8_t *b, size_t l, size_t *o) { (void)s; return mock_read(u, b, l, o); }
static int mock_sess_write(void *u, void *s, const uint8_t *b, size_t l) { (void)s; return mock_write(u, b, l); }
static bool mock_sess_pending(void *u, void *s) { (void)u; (void)s; return false; }
static void mock_sess_close(void *u, void *s) { (void)u; (void)s; }

static int wait_ctrl_byte(int fd, char *out, int timeout_ms)
{
    struct pollfd p = { .fd = fd, .events = POLLIN, .revents = 0 };
    int pr = carrier_poll(&p, 1, timeout_ms);
    if (pr <= 0) return -1;
    ssize_t r = recv((carrier_fd_t)fd, out, 1, 0);
    return r == 1 ? 0 : -1;
}

static carrier_bridge_t *start_mock_bridge(mock_net_t *net, int net_pair[2])
{
    assert(carrier_pipe_pair(net_pair) == 0);
    carrier_set_nonblock(net_pair[1]);
    net->net_fd = net_pair[1]; net->io = NULL;
    carrier_bridge_cfg_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    strcpy(cfg.host, "mock"); cfg.port = 1;
    cfg.io_connect = mock_io_connect; cfg.io_user = net;
    cfg.session.user = net;
    cfg.session.connect = mock_sess_connect;
    cfg.session.read = mock_sess_read;
    cfg.session.write = mock_sess_write;
    cfg.session.has_pending = mock_sess_pending;
    cfg.session.close = mock_sess_close;
    cfg.err_io = CARRIER_ERR_IO; cfg.err_closed = CARRIER_ERR_CLOSED; cfg.err_again = CARRIER_ERR_AGAIN;
    return carrier_bridge_start(&cfg);
}

static void test_bridge_pump(void)
{
    LOG("test_bridge_pump");
    int net_pair[2];
    mock_net_t net;
    carrier_bridge_t *b = start_mock_bridge(&net, net_pair);
    assert(b);
    int data_fd = carrier_bridge_data_fd(b);
    int ctrl_fd = carrier_bridge_ctrl_fd(b);
    assert(data_fd >= 0 && ctrl_fd >= 0);

    /* ready event */
    char ev = 0;
    assert(wait_ctrl_byte(ctrl_fd, &ev, 3000) == 0 && ev == 'R');
    assert(carrier_bridge_state(b) == 1);

    /* consumer -> tunnel: write to data_fd, read from the "remote" end */
    const char up[] = "hello-up";
    assert(send((carrier_fd_t)data_fd, up, sizeof(up), CARRIER_SEND_FLAGS) == (ssize_t)sizeof(up));
    char buf[64] = {0};
    struct pollfd rp = { .fd = net_pair[0], .events = POLLIN, .revents = 0 };
    assert(carrier_poll(&rp, 1, 3000) > 0);
    assert(recv((carrier_fd_t)net_pair[0], buf, sizeof(buf), 0) == (ssize_t)sizeof(up));
    assert(strcmp(buf, up) == 0);

    /* tunnel -> consumer: write from the "remote", read on data_fd */
    const char down[] = "hello-down";
    assert(send((carrier_fd_t)net_pair[0], down, sizeof(down), CARRIER_SEND_FLAGS) == (ssize_t)sizeof(down));
    memset(buf, 0, sizeof(buf));
    struct pollfd dp = { .fd = data_fd, .events = POLLIN, .revents = 0 };
    assert(carrier_poll(&dp, 1, 3000) > 0);
    assert(recv((carrier_fd_t)data_fd, buf, sizeof(buf), 0) == (ssize_t)sizeof(down));
    assert(strcmp(buf, down) == 0);

    /* clean stop: no hang. NOTE: stop() frees the bridge (caller-owned SDK fds
     * survive), so state must be queried BEFORE stop, not after. */
    assert(carrier_bridge_state(b) == 1);
    carrier_bridge_stop(b);
    carrier_pipe_close(net_pair[0]);
    /* SDK-side fds are caller-owned */
    carrier_pipe_close(data_fd);
    carrier_pipe_close(ctrl_fd);
}

static void test_bridge_peer_close(void)
{
    LOG("test_bridge_peer_close");
    int net_pair[2];
    mock_net_t net;
    carrier_bridge_t *b = start_mock_bridge(&net, net_pair);
    assert(b);
    int ctrl_fd = carrier_bridge_ctrl_fd(b);
    int data_fd = carrier_bridge_data_fd(b);
    char ev = 0;
    assert(wait_ctrl_byte(ctrl_fd, &ev, 3000) == 0 && ev == 'R');

    /* remote closes the network pipe -> session read EOF -> 'C' event */
    carrier_pipe_close(net_pair[0]);
    assert(wait_ctrl_byte(ctrl_fd, &ev, 3000) == 0 && ev == 'C');
    carrier_bridge_wait(b);
    assert(carrier_bridge_state(b) == 2);

    carrier_bridge_stop(b);
    carrier_pipe_close(data_fd);
    carrier_pipe_close(ctrl_fd);
}

int main(void)
{
#ifdef _WIN32
    WSADATA wsa; WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
    test_pipe_roundtrip();
    test_trust_contract();
    test_bridge_pump();
    test_bridge_peer_close();
    LOG("ALL CARRIER UNIT TESTS PASSED");
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}