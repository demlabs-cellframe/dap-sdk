#define _GNU_SOURCE

/*
 * Regression: queue write/remove/shutdown race must not use freed queue esocket memory.
 */

#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#if !defined(DAP_OS_WINDOWS)
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "dap_common.h"
#include "dap_events.h"
#include "dap_events_socket.h"
#include "dap_server.h"

#if defined(DAP_OS_WINDOWS)
int main(void)
{
    return 0;
}
#else

enum {
    TEST_ITERS = 40,
    WRITER_THREADS = 8,
    REMOVER_THREADS = 4,
};

typedef struct s_shared {
    atomic_uint_fast64_t es_uuid;
    atomic_bool have_socket;
    atomic_bool stop;
} s_shared_t;

static void s_alarm_handler(int a_sig)
{
    (void)a_sig;
    _exit(124);
}

static void s_client_new(dap_events_socket_t *a_es, void *a_arg)
{
    (void)a_arg;
    s_shared_t *l_shared = (s_shared_t *)a_es->callbacks.arg;
    atomic_store_explicit(&l_shared->es_uuid, a_es->uuid, memory_order_release);
    atomic_store_explicit(&l_shared->have_socket, true, memory_order_release);
}

static void *s_writer_thread(void *a_arg)
{
    s_shared_t *l_shared = (s_shared_t *)a_arg;
    static const char l_payload[] = "race-payload";

    while (!atomic_load_explicit(&l_shared->stop, memory_order_acquire)) {
        if (!atomic_load_explicit(&l_shared->have_socket, memory_order_acquire) || !dap_events_workers_init_status()) {
            sched_yield();
            continue;
        }

        dap_worker_t *l_worker = dap_events_worker_get(0);
        if (!l_worker) {
            sched_yield();
            continue;
        }

        dap_events_socket_uuid_t l_uuid = atomic_load_explicit(&l_shared->es_uuid, memory_order_acquire);
        (void)dap_events_socket_write_f(l_worker, l_uuid, "payload=%s", l_payload);
    }

    return NULL;
}

static void *s_remove_thread(void *a_arg)
{
    s_shared_t *l_shared = (s_shared_t *)a_arg;

    while (!atomic_load_explicit(&l_shared->stop, memory_order_acquire)) {
        if (!atomic_load_explicit(&l_shared->have_socket, memory_order_acquire) || !dap_events_workers_init_status()) {
            sched_yield();
            continue;
        }

        dap_worker_t *l_worker = dap_events_worker_get(0);
        if (!l_worker) {
            sched_yield();
            continue;
        }

        dap_events_socket_uuid_t l_uuid = atomic_load_explicit(&l_shared->es_uuid, memory_order_acquire);
        dap_events_socket_remove_and_delete(l_worker, l_uuid);
    }

    return NULL;
}

static uint16_t s_server_port_get(const dap_server_t *a_server)
{
    if (!a_server || !a_server->es_listeners || !a_server->es_listeners->data)
        return 0;

    dap_events_socket_t *l_es = (dap_events_socket_t *)a_server->es_listeners->data;
    struct sockaddr_in l_addr = { 0 };
    socklen_t l_addr_len = sizeof(l_addr);
    if (getsockname(l_es->socket, (struct sockaddr *)&l_addr, &l_addr_len) != 0)
        return 0;

    return ntohs(l_addr.sin_port);
}

static int s_connect_loopback(uint16_t a_port)
{
    int l_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (l_fd < 0)
        return -1;

    struct sockaddr_in l_addr = { 0 };
    l_addr.sin_family = AF_INET;
    l_addr.sin_port = htons(a_port);
    (void)inet_pton(AF_INET, "127.0.0.1", &l_addr.sin_addr);

    for (int i = 0; i < 100; i++) {
        if (connect(l_fd, (struct sockaddr *)&l_addr, sizeof(l_addr)) == 0)
            return l_fd;
        if (errno != ECONNREFUSED && errno != EINTR) {
            close(l_fd);
            return -1;
        }
        struct timespec l_pause = { .tv_sec = 0, .tv_nsec = 2 * 1000 * 1000 };
        (void)nanosleep(&l_pause, NULL);
    }

    close(l_fd);
    return -1;
}

int main(void)
{
    signal(SIGALRM, s_alarm_handler);
#ifdef SIGPIPE
    signal(SIGPIPE, SIG_IGN);
#endif
    alarm(90);

    for (int i = 0; i < TEST_ITERS; i++) {
        s_shared_t l_shared = { 0 };
        pthread_t l_writers[WRITER_THREADS] = { 0 };
        pthread_t l_removers[REMOVER_THREADS] = { 0 };

        if (dap_common_init("test_queue_write_f_remove_stop_race", NULL) != 0) {
            fprintf(stderr, "iter=%d dap_common_init failed\n", i);
            return 1;
        }

        if (dap_events_init(1, 1000) != 0) {
            fprintf(stderr, "iter=%d dap_events_init failed\n", i);
            dap_common_deinit();
            return 2;
        }

        if (dap_events_start() != 0) {
            fprintf(stderr, "iter=%d dap_events_start failed\n", i);
            dap_events_deinit();
            dap_common_deinit();
            return 3;
        }

        dap_events_socket_callbacks_t l_client_callbacks = {
            .new_callback = s_client_new,
            .arg = &l_shared,
        };

        dap_server_t *l_server = dap_server_new(NULL, NULL, &l_client_callbacks);
        if (!l_server) {
            fprintf(stderr, "iter=%d dap_server_new failed\n", i);
            dap_events_stop_all();
            dap_events_wait();
            dap_events_deinit();
            dap_common_deinit();
            return 4;
        }

        if (dap_server_listen_addr_add(l_server, "127.0.0.1", 0, DESCRIPTOR_TYPE_SOCKET_LISTENING, NULL) != 0) {
            fprintf(stderr, "iter=%d dap_server_listen_addr_add failed\n", i);
            dap_server_delete(l_server);
            dap_events_stop_all();
            dap_events_wait();
            dap_events_deinit();
            dap_common_deinit();
            return 5;
        }

        uint16_t l_port = s_server_port_get(l_server);
        if (!l_port) {
            fprintf(stderr, "iter=%d detect port failed\n", i);
            dap_server_delete(l_server);
            dap_events_stop_all();
            dap_events_wait();
            dap_events_deinit();
            dap_common_deinit();
            return 6;
        }

        int l_client_fd = s_connect_loopback(l_port);
        if (l_client_fd < 0) {
            fprintf(stderr, "iter=%d connect failed\n", i);
            dap_server_delete(l_server);
            dap_events_stop_all();
            dap_events_wait();
            dap_events_deinit();
            dap_common_deinit();
            return 7;
        }

        bool l_connected = false;
        for (int j = 0; j < 2000; j++) {
            if (atomic_load_explicit(&l_shared.have_socket, memory_order_acquire)) {
                l_connected = true;
                break;
            }
            struct timespec l_pause = { .tv_sec = 0, .tv_nsec = 1000 * 1000 };
            (void)nanosleep(&l_pause, NULL);
        }
        if (!l_connected) {
            fprintf(stderr, "iter=%d accepted socket not observed\n", i);
            close(l_client_fd);
            dap_server_delete(l_server);
            dap_events_stop_all();
            dap_events_wait();
            dap_events_deinit();
            dap_common_deinit();
            return 8;
        }

        for (int t = 0; t < WRITER_THREADS; t++) {
            if (pthread_create(&l_writers[t], NULL, s_writer_thread, &l_shared) != 0) {
                fprintf(stderr, "iter=%d writer thread create failed\n", i);
                close(l_client_fd);
                dap_server_delete(l_server);
                dap_events_stop_all();
                dap_events_wait();
                dap_events_deinit();
                dap_common_deinit();
                return 9;
            }
        }

        for (int t = 0; t < REMOVER_THREADS; t++) {
            if (pthread_create(&l_removers[t], NULL, s_remove_thread, &l_shared) != 0) {
                fprintf(stderr, "iter=%d remover thread create failed\n", i);
                close(l_client_fd);
                dap_server_delete(l_server);
                dap_events_stop_all();
                dap_events_wait();
                dap_events_deinit();
                dap_common_deinit();
                return 10;
            }
        }

        struct timespec l_spin = { .tv_sec = 0, .tv_nsec = 25 * 1000 * 1000 };
        (void)nanosleep(&l_spin, NULL);

        dap_server_delete(l_server);
        dap_events_stop_all();
        dap_events_wait();

        atomic_store_explicit(&l_shared.stop, true, memory_order_release);
        for (int t = 0; t < WRITER_THREADS; t++)
            (void)pthread_join(l_writers[t], NULL);
        for (int t = 0; t < REMOVER_THREADS; t++)
            (void)pthread_join(l_removers[t], NULL);

        close(l_client_fd);
        dap_events_deinit();
        dap_common_deinit();
    }

    alarm(0);
    return 0;
}

#endif
