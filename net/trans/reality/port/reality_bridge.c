/**
 * @file reality_bridge.c
 * @brief Thread + socketpair bridge around a blocking reality_session.
 *        Pure POSIX; no SDK dependencies (see reality_bridge.h for contract).
 */

#include "reality_bridge.h"
#include "reality_port.h"

#include <pthread.h>
#include <poll.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/socket.h>

#define PUMP_CHUNK   16384
#define POLL_SLICE_MS 200

/* Thread-side fds must never block: a blocking send() into a full pipe parked
 * the pump thread forever, and reality_bridge_stop() then hung in
 * pthread_join() (observed as a disconnect that never completed). */
static int set_nonblock(int a_fd)
{
    int l_fl = fcntl(a_fd, F_GETFL, 0);
    if (l_fl < 0) return -1;
    return fcntl(a_fd, F_SETFL, l_fl | O_NONBLOCK);
}

struct reality_bridge {
    reality_bridge_cfg_t cfg;
    pthread_t            thread;
    int                  data_sdk, data_thr;   /* payload pipe (-1 in ext_fd mode) */
    int                  ctrl_sdk, ctrl_thr;   /* event pipe: 'R','E'+code,'C' */
    int                  ext_fd;               /* external fd mode: pump peer (-1 = socketpair mode) */
    volatile int         state;                /* 0 connecting, 1 ready, 2 closed, <0 err */
    volatile int         stop;
    int                  thread_started;
    int                  thread_joined;
    reality_io_ops_t    *volatile io;          /* set by thread; for stop() shutdown */
};

static void ctrl_send(reality_bridge_t *b, const void *p, size_t n)
{
    ssize_t r = send(b->ctrl_thr, p, n, REALITY_SEND_FLAGS);
    (void)r; /* best effort: consumer may be gone */
}

static void *bridge_thread(void *arg)
{
    reality_bridge_t *b = (reality_bridge_t *)arg;
    reality_io_ops_t *io = reality_io_socket_connect(b->cfg.host, b->cfg.port,
                                                     b->cfg.timeout_sec,
                                                     b->cfg.iface[0] ? b->cfg.iface : NULL);
    if (!io) {
        b->state = REALITY_ERR_IO;
        uint8_t ev[2] = { 'E', (uint8_t)(-REALITY_ERR_IO) };
        ctrl_send(b, ev, 2);
        return NULL;
    }
    b->io = io;  /* publish for stop() shutdown */

    reality_session_t *s = reality_session_connect(&b->cfg.rcfg, io,
                                                   reality_crypto_openssl(),
                                                   reality_time_std(), NULL,
                                                   b->cfg.dest_host, b->cfg.dest_port);
    if (!s) {
        b->state = REALITY_ERR_CLOSED;
        uint8_t ev[2] = { 'E', (uint8_t)REALITY_ERR_CLOSED };
        ctrl_send(b, ev, 2);
        reality_io_socket_free(io);
        return NULL;
    }

    b->state = 1;
    ctrl_send(b, "R", 1);

    int tcp_fd = reality_io_socket_fd(io);
    int pipe_fd = b->ext_fd >= 0 ? b->ext_fd : b->data_thr;
    uint8_t buf[PUMP_CHUNK];
    bool progress = true;   /* previous pass moved data (guards against busy-spin) */

    while (!b->stop) {
        struct pollfd pfd[2];
        pfd[0].fd = pipe_fd; pfd[0].events = POLLIN; pfd[0].revents = 0;
        pfd[1].fd = tcp_fd;  pfd[1].events = POLLIN; pfd[1].revents = 0;

        /* The record layer may already hold complete records buffered from a
         * previous recv() (one read can return several TLS records while
         * reality_session_read() delivers only one). poll() would never fire
         * for those bytes -- the socket is already drained -- so the payload
         * would be stranded until an unrelated socket event. Drain first.
         *
         * The timeout is 0 in that case (not "skip polling"): both directions
         * must be serviced every pass, otherwise a heavy download starves the
         * upload direction and the peer sees a dead tunnel. */
        bool pending = reality_session_has_pending(s);
        int pr = poll(pfd, 2, (pending && progress) ? 0 : POLL_SLICE_MS);
        if (pr < 0) {
            if (errno == EINTR) continue;
            break;
        }
        progress = false;

        /* SDK/consumer -> tunnel */
        if (pfd[0].revents & (POLLIN | POLLHUP | POLLERR)) {
            ssize_t n = recv(pipe_fd, buf, sizeof(buf), 0);
            if (n > 0) {
                progress = true;
                if (reality_session_write(s, buf, (size_t)n) != 0) {
                    b->state = REALITY_ERR_IO;
                    uint8_t ev[2] = { 'E', (uint8_t)REALITY_ERR_IO };
                    ctrl_send(b, ev, 2);
                    break;
                }
            } else if (n == 0) {
                /* consumer closed the pipe: session done */
                break;
            } else if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
                break;
            }
        }

        /* tunnel -> SDK/consumer */
        if (pending || (pfd[1].revents & (POLLIN | POLLHUP | POLLERR))) {
            size_t got = 0;
            int rc = reality_session_read(s, buf, sizeof(buf), &got);
            if (rc == 0 && got > 0) {
                progress = true;
                size_t off = 0;
                while (off < got && !b->stop) {
                    ssize_t w = send(pipe_fd, buf + off, got - off, REALITY_SEND_FLAGS);
                    if (w > 0) {
                        off += (size_t)w;
                        continue;
                    }
                    if (w < 0 && errno == EINTR)
                        continue;
                    if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                        /* Consumer is slower than the tunnel: wait for the pipe
                         * to drain in slices, re-checking the stop flag, instead
                         * of blocking inside send() indefinitely. */
                        struct pollfd wp = { .fd = pipe_fd, .events = POLLOUT, .revents = 0 };
                        int wpr = poll(&wp, 1, POLL_SLICE_MS);
                        if (wpr < 0 && errno != EINTR) {
                            b->stop = 1;
                            break;
                        }
                        continue;
                    }
                    off = got; /* peer gone */
                    b->stop = 1;
                    break;
                }
            } else if (rc == REALITY_ERR_AGAIN) {
                /* read timed out with nothing buffered: keep the session alive */
                continue;
            } else if (rc != 0) {
                b->state = (rc == REALITY_ERR_CLOSED) ? 2 : rc;
                uint8_t ev[2] = { (uint8_t)(rc == REALITY_ERR_CLOSED ? 'C' : 'E'),
                                  (uint8_t)(rc == REALITY_ERR_CLOSED ? 0 : -rc) };
                ctrl_send(b, ev, rc == REALITY_ERR_CLOSED ? 1 : 2);
                break;
            }
        }
    }

    if (b->state == 1) b->state = 2;  /* normal pump exit */
    reality_session_close(s);          /* close_notify + closes tcp fd via io */
    reality_io_socket_free(io);
    b->io = NULL;
    if (b->ext_fd >= 0) { close(b->ext_fd); b->ext_fd = -1; }
    return NULL;
}

static reality_bridge_t *bridge_start_common(const reality_bridge_cfg_t *a_cfg, int a_ext_fd)
{
    if (!a_cfg || !a_cfg->host[0] || !a_cfg->port || !a_cfg->dest_host[0]) return NULL;

    reality_bridge_t *b = (reality_bridge_t *)calloc(1, sizeof(*b));
    if (!b) return NULL;
    b->cfg = *a_cfg;
    b->state = 0;
    b->ext_fd = a_ext_fd;
    b->data_sdk = b->data_thr = -1;
    b->ctrl_sdk = b->ctrl_thr = -1;

    int ctrl_pair[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, ctrl_pair) != 0) { free(b); return NULL; }
    b->ctrl_sdk = ctrl_pair[0]; b->ctrl_thr = ctrl_pair[1];
    set_nonblock(b->ctrl_thr);
    reality_no_sigpipe(b->ctrl_thr);
    /* The SDK side is handed to the event reactor, which assumes non-blocking
     * descriptors (dap creates its own sockets with O_NONBLOCK). Leaving it
     * blocking let a full pipe park the reactor's worker thread inside send()
     * -- observed as a fully frozen extension (no timers, no stats, no reads). */
    set_nonblock(b->ctrl_sdk);
    reality_no_sigpipe(b->ctrl_sdk);

    if (a_ext_fd < 0) {
        int data_pair[2];
        if (socketpair(AF_UNIX, SOCK_STREAM, 0, data_pair) != 0) {
            close(b->ctrl_sdk); close(b->ctrl_thr); free(b); return NULL;
        }
        b->data_sdk = data_pair[0]; b->data_thr = data_pair[1];
        set_nonblock(b->data_thr);
        reality_no_sigpipe(b->data_thr);
        set_nonblock(b->data_sdk);
        reality_no_sigpipe(b->data_sdk);
    } else {
        /* ownership of the external fd is ours for the pump's lifetime */
        set_nonblock(a_ext_fd);
        reality_no_sigpipe(a_ext_fd);
    }

    if (pthread_create(&b->thread, NULL, bridge_thread, b) != 0) {
        close(b->ctrl_sdk); close(b->ctrl_thr);
        if (b->data_sdk >= 0) { close(b->data_sdk); close(b->data_thr); }
        if (b->ext_fd >= 0) close(b->ext_fd);
        free(b);
        return NULL;
    }
    b->thread_started = 1;
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
    if (b && b->thread_started && !b->thread_joined) {
        pthread_join(b->thread, NULL);
        b->thread_joined = 1;
    }
}

int reality_bridge_data_fd(reality_bridge_t *b) { return b ? b->data_sdk : -1; }
int reality_bridge_ctrl_fd(reality_bridge_t *b) { return b ? b->ctrl_sdk : -1; }
int reality_bridge_state(reality_bridge_t *b)   { return b ? b->state : REALITY_ERR_PARAM; }

void reality_bridge_signal_stop(reality_bridge_t *b)
{
    if (!b) return;
    b->stop = 1;
    if (b->io) reality_io_socket_shutdown(b->io);
    /* shutdown(), NOT send(): a 1-byte send into a full pipe blocks, which made
     * signal_stop() (and thus reality_bridge_stop() -> pthread_join) hang. */
    if (b->ext_fd >= 0) {
        shutdown(b->ext_fd, SHUT_RDWR);
    } else if (b->data_thr >= 0) {
        shutdown(b->data_thr, SHUT_RDWR);
    }
}

void reality_bridge_stop(reality_bridge_t *b)
{
    if (!b) return;
    reality_bridge_signal_stop(b);
    if (b->thread_started && !b->thread_joined) {
        pthread_join(b->thread, NULL);
        b->thread_joined = 1;
    }
    if (b->data_thr >= 0) { close(b->data_thr); b->data_thr = -1; }
    if (b->ctrl_thr >= 0) { close(b->ctrl_thr); b->ctrl_thr = -1; }
    /* ext_fd is closed by the pump thread; SDK-side fds are caller-owned */
    free(b);
}
