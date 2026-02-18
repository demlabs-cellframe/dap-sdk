#define _GNU_SOURCE

/*
 * Regression: accept path must not crash when dap_events_socket_wrap_no_add() fails.
 */

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

#if !defined(DAP_OS_WINDOWS)
#include <arpa/inet.h>
#include <signal.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#endif

#include "dap_common.h"
#include "dap_events.h"
#include "dap_server.h"

#if defined(DAP_OS_WINDOWS)
int main(void)
{
    return 0;
}
#else

#if !defined(ENABLE_TESTING)
#error "test_server_accept_wrap_no_add_null requires ENABLE_TESTING"
#endif

enum {
    CONNECT_ATTEMPTS = 32
};

struct connector_arg {
    uint16_t port;
};

static void *s_connector_thread(void *a_arg)
{
    struct connector_arg *l_arg = (struct connector_arg *)a_arg;
    for (int i = 0; i < CONNECT_ATTEMPTS; i++) {
        int l_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (l_fd < 0)
            continue;

        struct sockaddr_in l_addr = { 0 };
        l_addr.sin_family = AF_INET;
        l_addr.sin_port = htons(l_arg->port);
        (void)inet_pton(AF_INET, "127.0.0.1", &l_addr.sin_addr);

        (void)connect(l_fd, (struct sockaddr *)&l_addr, sizeof(l_addr));
        close(l_fd);

        struct timespec l_pause = { .tv_sec = 0, .tv_nsec = 200 * 1000 };
        (void)nanosleep(&l_pause, NULL);
    }
    return NULL;
}

static void s_alarm_handler(int a_sig)
{
    (void)a_sig;
    _exit(124);
}

static int s_join_with_timeout(pthread_t a_thread, int a_timeout_sec)
{
#if defined(__linux__) && !defined(DAP_OS_WINDOWS)
    struct timespec l_deadline = { 0 };
    if (clock_gettime(CLOCK_REALTIME, &l_deadline) != 0)
        return errno ? errno : -1;
    l_deadline.tv_sec += a_timeout_sec;
    return pthread_timedjoin_np(a_thread, NULL, &l_deadline);
#else
    (void)a_timeout_sec;
    return pthread_join(a_thread, NULL);
#endif
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

int main(void)
{
    signal(SIGALRM, s_alarm_handler);
    alarm(30);

    (void)setenv("DAP_TEST_FAIL_ACCEPT_WRAP_NO_ADD_AT", "1", 1);

    int l_common_rc = dap_common_init("test_server_accept_wrap_no_add_null", NULL);
    if (l_common_rc != 0) {
        fprintf(stderr, "dap_common_init failed: %d\n", l_common_rc);
        return 1;
    }

    int l_events_init_rc = dap_events_init(1, 1000);
    if (l_events_init_rc != 0) {
        fprintf(stderr, "dap_events_init failed: %d\n", l_events_init_rc);
        dap_common_deinit();
        return 2;
    }

    int l_events_start_rc = dap_events_start();
    if (l_events_start_rc != 0) {
        fprintf(stderr, "dap_events_start failed: %d\n", l_events_start_rc);
        dap_events_deinit();
        dap_common_deinit();
        return 3;
    }

    dap_server_t *l_server = dap_server_new(NULL, NULL, NULL);
    if (!l_server) {
        fprintf(stderr, "dap_server_new failed\n");
        dap_events_stop_all();
        dap_events_wait();
        dap_events_deinit();
        dap_common_deinit();
        return 4;
    }

    int l_add_rc = dap_server_listen_addr_add(l_server, "127.0.0.1", 0,
                                              DESCRIPTOR_TYPE_SOCKET_LISTENING, NULL);
    if (l_add_rc != 0) {
        fprintf(stderr, "dap_server_listen_addr_add failed: %d\n", l_add_rc);
        dap_server_delete(l_server);
        dap_events_stop_all();
        dap_events_wait();
        dap_events_deinit();
        dap_common_deinit();
        return 5;
    }

    uint16_t l_port = s_server_port_get(l_server);
    if (!l_port) {
        fprintf(stderr, "failed to detect listening port\n");
        dap_server_delete(l_server);
        dap_events_stop_all();
        dap_events_wait();
        dap_events_deinit();
        dap_common_deinit();
        return 6;
    }

    pthread_t l_connector = 0;
    struct connector_arg l_arg = { .port = l_port };
    if (pthread_create(&l_connector, NULL, s_connector_thread, &l_arg) != 0) {
        fprintf(stderr, "pthread_create failed\n");
        dap_server_delete(l_server);
        dap_events_stop_all();
        dap_events_wait();
        dap_events_deinit();
        dap_common_deinit();
        return 7;
    }

    int l_join_rc = s_join_with_timeout(l_connector, 5);
    if (l_join_rc != 0) {
        fprintf(stderr, "connector join failed/timeout: %d\n", l_join_rc);
        dap_server_delete(l_server);
        dap_events_stop_all();
        dap_events_wait();
        dap_events_deinit();
        dap_common_deinit();
        return 8;
    }

    dap_server_delete(l_server);
    dap_events_stop_all();
    dap_events_wait();
    dap_events_deinit();
    dap_common_deinit();

    alarm(0);
    return 0;
}

#endif
