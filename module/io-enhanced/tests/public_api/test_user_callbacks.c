/*
 * Public API exercise: line-delimited protocol with void vs return-code frame callbacks.
 */

#include "dap_io.h"
#include "dap_io_ops.h"
#include "dap_io_send.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/* ============================================================================= */
/* Application protocol — line-delimited messages, explicit "ok\n" replies        */
/* ============================================================================= */

DAP_IO_SPAN_EXT_TYPE(line_conn_ext_t,
    size_t max_line;
);

typedef struct {
    unsigned lines_seen;
} line_app_t;

static unsigned s_app_count_lines(const char *a_batch, uint32_t a_bytes)
{
    unsigned l_lines = 0;
    const char *l_p = a_batch, *l_end = a_batch + a_bytes;
    while (l_p < l_end) {
        const char *l_nl = memchr(l_p, '\n', (size_t)(l_end - l_p));
        if (!l_nl)
            break;
        ++l_lines;
        l_p = l_nl + 1;
    }
    return l_lines;
}

static bool s_app_make_ok_replies(unsigned a_lines, char *a_buf, size_t a_cap, size_t *a_len)
{
    if (a_lines > a_cap / 3u)
        return false;
    for (unsigned i = 0; i < a_lines; ++i)
        memcpy(a_buf + i * 3u, "ok\n", 3u);
    *a_len = a_lines * 3u;
    return true;
}

static dap_io_parse_result_t s_line_parse_span(const char *a_data, size_t a_size, void *a_arg)
{
    line_conn_ext_t *l_ext = a_arg;
    size_t l_max = l_ext ? l_ext->max_line : 0;
    const char *l_p = a_data, *l_end = a_data + a_size;
    while (l_p < l_end) {
        const char *l_nl = memchr(l_p, '\n', (size_t)(l_end - l_p));
        if (!l_nl) {
            size_t l_partial = (size_t)(l_end - l_p);
            return (dap_io_parse_result_t){ (size_t)(l_p - a_data),
                    (!l_max || l_partial < l_max) ? 1u : 0u };
        }
        l_p = l_nl + 1;
    }
    return (dap_io_parse_result_t){ (size_t)(l_p - a_data), 0u };
}

static bool s_line_ext_init(line_conn_ext_t *a_e)
{
    if (!a_e) return false;
    memset(a_e, 0, sizeof *a_e);
    a_e->max_line = 4096;
    return DAP_IO_SPAN_EXT_INIT_SELF(a_e, s_line_parse_span, NULL);
}

static void s_app_on_lines_void(dap_conn_t *a_c, const char *a_batch, uint32_t a_bytes,
                                void *a_arg)
{
    line_app_t *l_a = a_arg;
    char        l_resp[64];
    size_t      l_resp_len = 0;
    unsigned    l_lines = s_app_count_lines(a_batch, a_bytes);
    if (!l_lines || !s_app_make_ok_replies(l_lines, l_resp, sizeof(l_resp), &l_resp_len))
        return;
    l_a->lines_seen += l_lines;
    (void)dap_io_tx_send_direct(a_c, l_resp, l_resp_len);
}

static dap_msg_rc_t s_app_on_lines_rc(dap_conn_t *a_c, const char *a_batch, uint32_t a_bytes,
                                      void *a_arg)
{
    line_app_t *l_a = a_arg;
    char        l_resp[64];
    size_t      l_resp_len = 0;
    unsigned    l_lines = s_app_count_lines(a_batch, a_bytes);
    if (!l_lines)
        return DAP_MSG_DONE;
    if (!s_app_make_ok_replies(l_lines, l_resp, sizeof(l_resp), &l_resp_len))
        return DAP_MSG_DROP;
    dap_msg_rc_t l_m = dap_io_send_rc_to_msg_rc(dap_io_tx_send_direct(a_c, l_resp, l_resp_len));
    if (l_m == DAP_MSG_DONE)
        l_a->lines_seen += l_lines;
    return l_m;
}

/* ============================================================================= */
/* Harness                                                                      */
/* ============================================================================= */

#define S_LINE_OLB_CAP ((size_t)(256u * 1024u))

static void *s_worker_thread(void *a_arg)
{
    dap_worker_loop(a_arg);
    return NULL;
}

static void *s_proc_thread(void *a_arg)
{
    dap_proc_loop_run(a_arg);
    return NULL;
}

static unsigned s_count_ok_token(unsigned *a_state, const char *a_buf, size_t a_n)
{
    unsigned l_c = 0;
    for (size_t i = 0; i < a_n; ++i) {
        char l_ch = a_buf[i];
        if (*a_state == 0) {
            *a_state = (l_ch == 'o') ? 1u : 0u;
        } else if (*a_state == 1) {
            *a_state = (l_ch == 'k') ? 2u : ((l_ch == 'o') ? 1u : 0u);
        } else if (l_ch == '\n') {
            ++l_c;
            *a_state = 0;
        } else {
            *a_state = (l_ch == 'o') ? 1u : 0u;
        }
    }
    return l_c;
}

typedef struct {
    int                 fd;
    _Atomic(unsigned)   ok_tokens;
    unsigned            expected_tokens;
    unsigned            ok_state;
} harness_sink_t;

static void *s_harness_sink(void *a_arg)
{
    harness_sink_t *l_s = a_arg;
    char              l_buf[512];
    struct pollfd     l_pfd = { .fd = l_s->fd, .events = POLLIN };
    unsigned          l_idle = 0;
    for (;;) {
        ssize_t l_r = read(l_s->fd, l_buf, sizeof(l_buf));
        if (l_r > 0) {
            unsigned l_add = s_count_ok_token(&l_s->ok_state, l_buf, (size_t)l_r);
            unsigned l_now = atomic_fetch_add_explicit(&l_s->ok_tokens,
                l_add, memory_order_relaxed) + l_add;
            if (l_now >= l_s->expected_tokens)
                break;
            l_idle = 0;
            continue;
        }
        if (l_r == 0)
            break;
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            if (poll(&l_pfd, 1, 100) == 0 && ++l_idle > 50)
                break;
            continue;
        }
        if (errno != EINTR)
            break;
    }
    return NULL;
}

static int s_feed_blocking(int a_fd, const void *a_data, size_t a_len)
{
    const uint8_t *l_p = a_data;
    size_t         l_rem = a_len;
    struct pollfd    l_po = { .fd = a_fd, .events = POLLOUT };
    while (l_rem) {
        ssize_t l_w = write(a_fd, l_p, l_rem);
        if (l_w > 0) {
            l_p += l_w;
            l_rem -= (size_t)l_w;
            continue;
        }
        if (l_w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            poll(&l_po, 1, -1);
        else if (errno != EINTR)
            return -1;
    }
    return 0;
}

static void s_ext_dtor(void *a_ext) { free(a_ext); }

static int s_run_line_scenario(bool a_use_rc)
{
    int l_sp[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, l_sp) < 0)
        return fprintf(stderr, "socketpair failed\n"), -1;
    fcntl(l_sp[0], F_SETFL, fcntl(l_sp[0], F_GETFL) | O_NONBLOCK);
    fcntl(l_sp[1], F_SETFL, fcntl(l_sp[1], F_GETFL) | O_NONBLOCK);

    dap_io_t *l_io = dap_io_create(1, 1);
    if (!l_io)
        return close(l_sp[0]), close(l_sp[1]), fprintf(stderr, "dap_io_create failed\n"), -1;

    line_conn_ext_t *l_ext = malloc(sizeof *l_ext);
    if (!l_ext) {
        dap_io_destroy(l_io);
        close(l_sp[0]);
        close(l_sp[1]);
        return fprintf(stderr, "malloc ext failed\n"), -1;
    }
    if (!s_line_ext_init(l_ext)) {
        free(l_ext);
        dap_io_destroy(l_io);
        close(l_sp[0]);
        close(l_sp[1]);
        return fprintf(stderr, "s_line_ext_init failed\n"), -1;
    }

    dap_io_conn_cfg_t l_open = DAP_IO_CONN_CFG_INIT;
    l_open.io = l_io;
    l_open.worker_id = 0;
    l_open.kind = DAP_IO_SOCK;
    l_open.fd = l_sp[0];
    l_open.olb_cap = S_LINE_OLB_CAP;
    l_open.read_cb = dap_io_rx_bridge;
    l_open.ext = l_ext;
    l_open.ext_dtor = s_ext_dtor;
    dap_conn_t *l_conn = dap_io_conn_open_cfg(&l_open);
    if (!l_conn) {
        free(l_ext);
        dap_io_destroy(l_io);
        close(l_sp[0]);
        close(l_sp[1]);
        return fprintf(stderr, "dap_io_conn_open_cfg failed\n"), -1;
    }
    (void)l_conn;

    line_app_t l_app = {0};
    if (a_use_rc) {
        if (!dap_io_proc_set_frame_rc_cb(l_io, 0, s_app_on_lines_rc, &l_app)) {
            dap_io_destroy(l_io);
            close(l_sp[0]);
            close(l_sp[1]);
            return fprintf(stderr, "dap_io_proc_set_frame_rc_cb failed\n"), -1;
        }
    } else {
        if (!dap_io_proc_set_frame_cb(l_io, 0, s_app_on_lines_void, &l_app)) {
            dap_io_destroy(l_io);
            close(l_sp[0]);
            close(l_sp[1]);
            return fprintf(stderr, "dap_io_proc_set_frame_cb failed\n"), -1;
        }
    }

    dap_proc_ctx_t *l_proc = dap_io_proc(l_io, 0);
    pthread_t       l_pt, l_wt, l_st;
    harness_sink_t  l_sink = { .fd = l_sp[1], .ok_tokens = 0, .expected_tokens = 2 };

    pthread_create(&l_pt, NULL, s_proc_thread, l_proc);
    pthread_create(&l_wt, NULL, s_worker_thread, dap_io_worker(l_io, 0));
    pthread_create(&l_st, NULL, s_harness_sink, &l_sink);

    static const char l_payload[] = "alpha\nbeta\n";
    if (s_feed_blocking(l_sp[1], l_payload, sizeof(l_payload) - 1) != 0) {
        dap_io_shutdown(l_io);
        pthread_join(l_wt, NULL);
        pthread_join(l_pt, NULL);
        dap_io_destroy(l_io);
        close(l_sp[0]);
        pthread_join(l_st, NULL);
        close(l_sp[1]);
        return fprintf(stderr, "feed failed\n"), -1;
    }
    shutdown(l_sp[1], SHUT_WR);

    pthread_join(l_st, NULL);
    dap_io_shutdown(l_io);
    pthread_join(l_wt, NULL);
    pthread_join(l_pt, NULL);
    dap_io_destroy(l_io);
    close(l_sp[0]);
    close(l_sp[1]);

    if (l_app.lines_seen != 2) {
        fprintf(stderr, "lines_seen=%u (expected 2)\n", l_app.lines_seen);
        return -1;
    }
    unsigned l_tok = atomic_load_explicit(&l_sink.ok_tokens, memory_order_relaxed);
    if (l_tok != 2) {
        fprintf(stderr, "sink ok tokens=%u (expected 2)\n", l_tok);
        return -1;
    }
    return 0;
}

int main(void)
{
    if (s_run_line_scenario(false) != 0) {
        fputs("scenario void frame_cb FAILED\n", stderr);
        return 1;
    }
    if (s_run_line_scenario(true) != 0) {
        fputs("scenario frame_rc_cb FAILED\n", stderr);
        return 1;
    }
    puts("OK");
    return 0;
}
