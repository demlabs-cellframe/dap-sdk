/**
 * @file carrier_bridge.c
 * @brief Session-agnostic thread + pipe pump (generalized from reality_bridge).
 *        Runs a blocking protocol session on a dedicated thread and exposes it
 *        as a byte pipe + ctrl-event pipe. No SDK dependencies; portable
 *        (POSIX + Windows). Error codes are protocol-native (cfg.err_*).
 *
 * See carrier.h for the FD-ownership contract. The pump logic is identical to
 * the original REALITY bridge (non-blocking thread ends, has_pending drain,
 * sliced POLLOUT backpressure, shutdown()-based stop) so behavior is preserved.
 */

#include "carrier/carrier.h"
#include "carrier_port.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#define PUMP_CHUNK    16384
#define POLL_SLICE_MS 200

struct carrier_bridge {
    carrier_bridge_cfg_t cfg;
    pthread_t            thread;
    int                  data_sdk, data_thr;   /* payload pipe (-1 in ext_fd mode) */
    int                  ctrl_sdk, ctrl_thr;   /* event pipe: 'R','E'+code,'C' */
    int                  ext_fd;               /* external fd mode (-1 = pipe mode) */
    volatile int         state;                /* 0 connecting, 1 ready, 2 closed, <0 err */
    volatile int         stop;
    int                  thread_started;
    int                  thread_joined;
    carrier_io_ops_t    *volatile io;          /* set by thread; for stop() shutdown */
};

static void ctrl_send(carrier_bridge_t *b, const void *p, size_t n)
{
    ssize_t r = send((carrier_fd_t)b->ctrl_thr, (const char *)p, (int)n, CARRIER_SEND_FLAGS);
    (void)r; /* best effort: consumer may be gone */
}

static void *bridge_thread(void *arg)
{
    carrier_bridge_t *b = (carrier_bridge_t *)arg;
    const carrier_bridge_cfg_t *cfg = &b->cfg;

    carrier_io_ops_t *io = cfg->io_connect
        ? cfg->io_connect(cfg->io_user, cfg->host, cfg->port, cfg->timeout_sec,
                          cfg->iface[0] ? cfg->iface : NULL)
        : carrier_io_socket_connect(cfg->host, cfg->port, cfg->timeout_sec,
                                    cfg->iface[0] ? cfg->iface : NULL);
    if (!io) {
        b->state = cfg->err_io;
        uint8_t ev[2] = { 'E', (uint8_t)(-cfg->err_io) };
        ctrl_send(b, ev, 2);
        return NULL;
    }
    b->io = io;  /* publish for stop() shutdown */

    void *s = cfg->session.connect ? cfg->session.connect(cfg->session.user, io) : NULL;
    if (!s) {
        b->state = cfg->err_closed;
        uint8_t ev[2] = { 'E', (uint8_t)cfg->err_closed };
        ctrl_send(b, ev, 2);
        io->close(io->user);
        free(io);
        return NULL;
    }

    b->state = 1;
    ctrl_send(b, "R", 1);

    int tcp_fd = io->fd ? io->fd(io->user) : -1;
    int pipe_fd = b->ext_fd >= 0 ? b->ext_fd : b->data_thr;
    uint8_t buf[PUMP_CHUNK];
    bool progress = true;   /* previous pass moved data (guards against busy-spin) */

    while (!b->stop) {
        struct pollfd pfd[2];
        pfd[0].fd = pipe_fd; pfd[0].events = POLLIN; pfd[0].revents = 0;
        pfd[1].fd = tcp_fd;  pfd[1].events = POLLIN; pfd[1].revents = 0;

        /* The session may already hold complete records buffered from a
         * previous recv() (one read can return several TLS records while the
         * session read delivers only one). poll() would never fire for those
         * bytes -- the socket is already drained -- so drain first. Timeout 0
         * in that case: both directions must be serviced every pass, otherwise
         * a heavy download starves the upload direction. */
        bool pending = cfg->session.has_pending
            ? cfg->session.has_pending(cfg->session.user, s) : false;
        int pr = carrier_poll(pfd, 2, (pending && progress) ? 0 : POLL_SLICE_MS);
        if (pr < 0) {
            if (carrier_errno() == CARRIER_EINTR) continue;
            break;
        }
        progress = false;

        /* SDK/consumer -> tunnel */
        if (pfd[0].revents & (POLLIN | POLLHUP | POLLERR)) {
            ssize_t n = recv((carrier_fd_t)pipe_fd, (char *)buf, sizeof(buf), 0);
            if (n > 0) {
                progress = true;
                if (cfg->session.write(cfg->session.user, s, buf, (size_t)n) != 0) {
                    b->state = cfg->err_io;
                    uint8_t ev[2] = { 'E', (uint8_t)(-cfg->err_io) };
                    ctrl_send(b, ev, 2);
                    break;
                }
            } else if (n == 0) {
                /* consumer closed the pipe: session done */
                break;
            } else {
                int e = carrier_errno();
                if (e != CARRIER_EINTR && e != CARRIER_EAGAIN && e != CARRIER_EWOULDBLOCK)
                    break;
            }
        }

        /* tunnel -> SDK/consumer */
        if (pending || (pfd[1].revents & (POLLIN | POLLHUP | POLLERR))) {
            size_t got = 0;
            int rc = cfg->session.read(cfg->session.user, s, buf, sizeof(buf), &got);
            if (rc == 0 && got > 0) {
                progress = true;
                size_t off = 0;
                while (off < got && !b->stop) {
                    ssize_t w = send((carrier_fd_t)pipe_fd, (const char *)buf + off,
                                     (int)(got - off), CARRIER_SEND_FLAGS);
                    if (w > 0) { off += (size_t)w; continue; }
                    int e = carrier_errno();
                    if (w < 0 && e == CARRIER_EINTR)
                        continue;
                    if (w < 0 && (e == CARRIER_EAGAIN || e == CARRIER_EWOULDBLOCK)) {
                        /* Consumer slower than the tunnel: wait for the pipe to
                         * drain in slices, re-checking stop, instead of blocking
                         * inside send() indefinitely. */
                        struct pollfd wp = { .fd = pipe_fd, .events = POLLOUT, .revents = 0 };
                        int wpr = carrier_poll(&wp, 1, POLL_SLICE_MS);
                        if (wpr < 0 && carrier_errno() != CARRIER_EINTR) {
                            b->stop = 1;
                            break;
                        }
                        continue;
                    }
                    off = got; /* peer gone */
                    b->stop = 1;
                    break;
                }
            } else if (rc == cfg->err_again) {
                /* read timed out with nothing buffered: keep the session alive */
                continue;
            } else if (rc != 0) {
                b->state = (rc == cfg->err_closed) ? 2 : rc;
                uint8_t ev[2] = { (uint8_t)(rc == cfg->err_closed ? 'C' : 'E'),
                                  (uint8_t)(rc == cfg->err_closed ? 0 : -rc) };
                ctrl_send(b, ev, rc == cfg->err_closed ? 1 : 2);
                break;
            }
        }
    }

    if (b->state == 1) b->state = 2;  /* normal pump exit */
    if (cfg->session.close) cfg->session.close(cfg->session.user, s);
    io->close(io->user);              /* closes the tcp fd */
    free(io);
    b->io = NULL;
    if (b->ext_fd >= 0) { carrier_close_fd(b->ext_fd); b->ext_fd = -1; }
    return NULL;
}

static carrier_bridge_t *bridge_start_common(const carrier_bridge_cfg_t *a_cfg, int a_ext_fd)
{
    if (!a_cfg || !a_cfg->host[0] || !a_cfg->port) return NULL;
    if (!a_cfg->session.connect || !a_cfg->session.read ||
        !a_cfg->session.write || !a_cfg->session.close) return NULL;

    carrier_bridge_t *b = (carrier_bridge_t *)calloc(1, sizeof(*b));
    if (!b) return NULL;
    b->cfg = *a_cfg;
    b->state = 0;
    b->ext_fd = a_ext_fd;
    b->data_sdk = b->data_thr = -1;
    b->ctrl_sdk = b->ctrl_thr = -1;

    int ctrl_pair[2];
    if (carrier_pipe_pair(ctrl_pair) != 0) { free(b); return NULL; }
    b->ctrl_sdk = ctrl_pair[0]; b->ctrl_thr = ctrl_pair[1];
    /* Thread-side fds must never block: a blocking send() into a full pipe
     * parks the pump forever and stop() then hangs in pthread_join(). */
    carrier_set_nonblock(b->ctrl_thr);
    carrier_no_sigpipe(b->ctrl_thr);
    /* SDK side goes to the event reactor, which assumes non-blocking fds. */
    carrier_set_nonblock(b->ctrl_sdk);
    carrier_no_sigpipe(b->ctrl_sdk);

    if (a_ext_fd < 0) {
        int data_pair[2];
        if (carrier_pipe_pair(data_pair) != 0) {
            carrier_pipe_close(b->ctrl_sdk); carrier_pipe_close(b->ctrl_thr);
            free(b); return NULL;
        }
        b->data_sdk = data_pair[0]; b->data_thr = data_pair[1];
        carrier_set_nonblock(b->data_thr);
        carrier_no_sigpipe(b->data_thr);
        carrier_set_nonblock(b->data_sdk);
        carrier_no_sigpipe(b->data_sdk);
    } else {
        /* ownership of the external fd is ours for the pump's lifetime */
        carrier_set_nonblock(a_ext_fd);
        carrier_no_sigpipe(a_ext_fd);
    }

    if (pthread_create(&b->thread, NULL, bridge_thread, b) != 0) {
        carrier_pipe_close(b->ctrl_sdk); carrier_pipe_close(b->ctrl_thr);
        if (b->data_sdk >= 0) { carrier_pipe_close(b->data_sdk); carrier_pipe_close(b->data_thr); }
        if (b->ext_fd >= 0) carrier_close_fd(b->ext_fd);
        free(b);
        return NULL;
    }
    b->thread_started = 1;
    return b;
}

carrier_bridge_t *carrier_bridge_start(const carrier_bridge_cfg_t *a_cfg)
{
    return bridge_start_common(a_cfg, -1);
}

carrier_bridge_t *carrier_bridge_start_fd(const carrier_bridge_cfg_t *a_cfg, int a_ext_fd)
{
    if (a_ext_fd < 0) return NULL;
    return bridge_start_common(a_cfg, a_ext_fd);
}

void carrier_bridge_wait(carrier_bridge_t *b)
{
    if (b && b->thread_started && !b->thread_joined) {
        pthread_join(b->thread, NULL);
        b->thread_joined = 1;
    }
}

int carrier_bridge_data_fd(carrier_bridge_t *b) { return b ? b->data_sdk : -1; }
int carrier_bridge_ctrl_fd(carrier_bridge_t *b) { return b ? b->ctrl_sdk : -1; }
int carrier_bridge_state(carrier_bridge_t *b)   { return b ? b->state : CARRIER_ERR_PARAM; }

void carrier_bridge_signal_stop(carrier_bridge_t *b)
{
    if (!b) return;
    b->stop = 1;
    if (b->io && b->io->shutdown) b->io->shutdown(b->io->user);
    /* shutdown(), NOT send(): a 1-byte send into a full pipe blocks, which made
     * signal_stop() (and thus stop() -> pthread_join) hang. */
    if (b->ext_fd >= 0) {
        shutdown((carrier_fd_t)b->ext_fd, SHUT_RDWR);
    } else if (b->data_thr >= 0) {
        shutdown((carrier_fd_t)b->data_thr, SHUT_RDWR);
    }
}

void carrier_bridge_stop(carrier_bridge_t *b)
{
    if (!b) return;
    carrier_bridge_signal_stop(b);
    if (b->thread_started && !b->thread_joined) {
        pthread_join(b->thread, NULL);
        b->thread_joined = 1;
    }
    if (b->data_thr >= 0) { carrier_pipe_close(b->data_thr); b->data_thr = -1; }
    if (b->ctrl_thr >= 0) { carrier_pipe_close(b->ctrl_thr); b->ctrl_thr = -1; }
    /* ext_fd is closed by the pump thread; SDK-side fds are caller-owned */
    free(b);
}