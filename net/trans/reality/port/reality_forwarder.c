/**
 * @file reality_forwarder.c
 * @brief Local TCP forwarder into REALITY tunnels (see reality_forwarder.h).
 *        One bridge (thread + VLESS session) per accepted connection.
 */

#include "reality_forwarder.h"

#include <pthread.h>
#include <poll.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

typedef struct fwd_conn {
    struct fwd_conn      *next;
    struct reality_forwarder *fwd;
    reality_bridge_t     *bridge;   /* NULL once the conn thread finished */
    pthread_t             thread;
} fwd_conn_t;

struct reality_forwarder {
    reality_fwd_cfg_t cfg;
    int               listen_fd;
    uint16_t          port;
    pthread_t         accept_thread;
    volatile int      stop;
    pthread_mutex_t   mtx;
    fwd_conn_t       *active;
};

static void fwd_reap(reality_forwarder_t *f)
{
    for (;;) {
        pthread_mutex_lock(&f->mtx);
        fwd_conn_t **pp = &f->active;
        fwd_conn_t *victim = NULL;
        while (*pp) {
            if ((*pp)->bridge == NULL) { victim = *pp; *pp = victim->next; break; }
            pp = &(*pp)->next;
        }
        pthread_mutex_unlock(&f->mtx);
        if (!victim) return;
        pthread_join(victim->thread, NULL);
        free(victim);
    }
}

static void *conn_thread(void *arg)
{
    fwd_conn_t *c = (fwd_conn_t *)arg;
    reality_bridge_wait(c->bridge);          /* pump ended (EOF/error/stop) */
    int ctrl = reality_bridge_ctrl_fd(c->bridge);
    if (ctrl >= 0) close(ctrl);              /* events unused; pump already exited */
    reality_bridge_stop(c->bridge);          /* frees the bridge */
    pthread_mutex_lock(&c->fwd->mtx);
    c->bridge = NULL;                        /* mark reaped-by-owner */
    pthread_mutex_unlock(&c->fwd->mtx);
    return NULL;
}

static void *accept_thread(void *arg)
{
    reality_forwarder_t *f = (reality_forwarder_t *)arg;
    while (!f->stop) {
        struct pollfd p;
        p.fd = f->listen_fd; p.events = POLLIN; p.revents = 0;
        int pr = poll(&p, 1, 500);
        if (pr < 0) { if (errno == EINTR) continue; break; }
        if (pr > 0 && (p.revents & POLLIN)) {
            int fd = accept(f->listen_fd, NULL, NULL);
            if (fd < 0) {
                if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) continue;
                break;
            }
            reality_bridge_t *b = reality_bridge_start_fd(&f->cfg.bridge, fd);
            if (!b) { close(fd); continue; }

            fwd_conn_t *c = (fwd_conn_t *)calloc(1, sizeof(*c));
            if (!c) { reality_bridge_stop(b); continue; }
            c->bridge = b;
            c->fwd = f;
            pthread_mutex_lock(&f->mtx);
            c->next = f->active;
            f->active = c;
            pthread_mutex_unlock(&f->mtx);

            if (pthread_create(&c->thread, NULL, conn_thread, c) != 0) {
                reality_bridge_signal_stop(b);
                pthread_mutex_lock(&f->mtx);
                fwd_conn_t **pp = &f->active;
                while (*pp && *pp != c) pp = &(*pp)->next;
                if (*pp) *pp = c->next;
                pthread_mutex_unlock(&f->mtx);
                reality_bridge_stop(b);
                free(c);
            }
        }
        fwd_reap(f);
    }
    fwd_reap(f);
    return NULL;
}

reality_forwarder_t *reality_forwarder_start(const reality_fwd_cfg_t *a_cfg)
{
    if (!a_cfg || !a_cfg->bridge.host[0] || !a_cfg->bridge.dest_host[0]) return NULL;

    reality_forwarder_t *f = (reality_forwarder_t *)calloc(1, sizeof(*f));
    if (!f) return NULL;
    f->cfg = *a_cfg;
    f->listen_fd = -1;
    pthread_mutex_init(&f->mtx, NULL);

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { free(f); return NULL; }
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    sa.sin_port = htons(a_cfg->listen_port);
    if (bind(fd, (struct sockaddr *)&sa, sizeof(sa)) != 0 || listen(fd, 16) != 0) {
        close(fd);
        pthread_mutex_destroy(&f->mtx);
        free(f);
        return NULL;
    }
    socklen_t sl = sizeof(sa);
    if (getsockname(fd, (struct sockaddr *)&sa, &sl) == 0)
        f->port = ntohs(sa.sin_port);
    f->listen_fd = fd;

    if (pthread_create(&f->accept_thread, NULL, accept_thread, f) != 0) {
        close(fd);
        pthread_mutex_destroy(&f->mtx);
        free(f);
        return NULL;
    }
    return f;
}

uint16_t reality_forwarder_port(reality_forwarder_t *f)
{
    return f ? f->port : 0;
}

int reality_forwarder_conns(reality_forwarder_t *f)
{
    if (!f) return 0;
    int n = 0;
    pthread_mutex_lock(&f->mtx);
    for (fwd_conn_t *c = f->active; c; c = c->next)
        if (c->bridge) n++;
    pthread_mutex_unlock(&f->mtx);
    return n;
}

void reality_forwarder_stop(reality_forwarder_t *f)
{
    if (!f) return;
    f->stop = 1;
    if (f->listen_fd >= 0) {
        shutdown(f->listen_fd, SHUT_RDWR);
        close(f->listen_fd);
        f->listen_fd = -1;
    }
    pthread_join(f->accept_thread, NULL);   /* does a final reap pass */

    pthread_mutex_lock(&f->mtx);
    fwd_conn_t *list = f->active;
    f->active = NULL;
    pthread_mutex_unlock(&f->mtx);

    for (fwd_conn_t *c = list; c; c = c->next)
        if (c->bridge) reality_bridge_signal_stop(c->bridge);
    for (fwd_conn_t *c = list; c; ) {
        fwd_conn_t *n = c->next;
        pthread_join(c->thread, NULL);      /* conn_thread frees its bridge */
        free(c);
        c = n;
    }
    pthread_mutex_destroy(&f->mtx);
    free(f);
}
