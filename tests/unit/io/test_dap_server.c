/**
 * @file test_dap_server.c
 * @brief Unit tests for DAP server module
 * @details Tests server creation, configuration, and lifecycle with mocking
 * @date 2025-11-21
 * @copyright (c) 2025 Cellframe Network
 */

#include <sys/socket.h>
#include <stdio.h>
#include <unistd.h>
#include "dap_common.h"
#include "dap_test.h"
#include "dap_server.h"
#include "dap_events_socket.h"
#include "dap_events.h"
#include "dap_worker.h"
#include "dap_config.h"
#include "dap_mock.h"

#define LOG_TAG "test_dap_server"

/* confcall W59-R7.1: DAP_MOCK(func) on a function with no matching
 * DAP_MOCK_WRAPPER_CUSTOM body in this file is never actually
 * linker-wrapped by this project's mock generator (module/test/mocks/lib/
 * awk/scan_mock_declarations.awk only recognises DAP_MOCK_DECLARE/_CUSTOM),
 * so every "mocked" call below always ran the real implementation.  The
 * original tests relied on the mocked return values without noticing they
 * were dead — dap_server_listen_addr_add() actually opened a real socket
 * and (with no live reactor) dap_worker_add_events_socket_auto() silently
 * failed, and dap_server_enabled() always read the real (NULL-by-default)
 * g_config, so "Server enabled after init" aborted the whole binary. This
 * file now drives the real dap_config/dap_events/dap_worker stack instead
 * of pretending it is mocked.
 */
DAP_MOCK(dap_events_socket_init);
DAP_MOCK(dap_events_socket_deinit);
DAP_MOCK(dap_events_socket_wrap_listener);
DAP_MOCK(dap_worker_add_events_socket_auto);
DAP_MOCK(dap_events_worker_get_auto);

/**
 * @brief Test: Initialize and deinitialize server system
 */
static void s_test_server_init_deinit(void)
{
    log_it(L_INFO, "Testing server init/deinit");

    int l_ret = dap_server_init();
    dap_assert(l_ret == 0, "Server initialization");

    dap_server_deinit();
    dap_pass_msg("Server deinitialization");
}

/**
 * @brief Test: Create new server
 */
static void s_test_server_new(void)
{
    log_it(L_INFO, "Testing server creation");

    int l_ret = dap_server_init();
    dap_assert(l_ret == 0, "Server initialization");

    // Prepare callbacks
    dap_events_socket_callbacks_t l_server_callbacks = {0};
    dap_events_socket_callbacks_t l_client_callbacks = {0};

    // Test creating server
    dap_server_t *l_server = dap_server_new("test_section",
                                             &l_server_callbacks,
                                             &l_client_callbacks);
    dap_assert(l_server != NULL, "Create server");

    if (l_server) {
        dap_server_delete(l_server);
        dap_pass_msg("Server deleted");
    }

    dap_server_deinit();
}

/**
 * @brief Test: Server enabled status
 *
 * confcall W59-R7.1: dap_server_init() reads "server:enabled" straight out
 * of the real global g_config (dap_config_get_item_bool_default(g_config,
 * ...)); with no config bound it always resolves the default (false).  Set
 * up a genuine dap_config with [server]/enabled=true so the "enabled after
 * init" branch is exercised for real instead of asserting on a value the
 * previous "mock" never actually produced.
 */
static const char *s_server_cfg_name = "test_dap_server_enabled";

static void s_test_server_enabled(void)
{
    log_it(L_INFO, "Testing server enabled status");

    // Before init - should return false
    bool l_enabled_before = dap_server_enabled();
    dap_assert(!l_enabled_before, "Server not enabled before init");

    char l_cfg_path[256];
    snprintf(l_cfg_path, sizeof(l_cfg_path), "%s.cfg", s_server_cfg_name);
    FILE *l_cfg_file = fopen(l_cfg_path, "w+");
    dap_assert(l_cfg_file != NULL, "Create test config file");
    if (l_cfg_file) {
        static const char l_cfg_data[] = "[server]\nenabled=true\n";
        fwrite(l_cfg_data, sizeof(char), sizeof(l_cfg_data) - 1, l_cfg_file);
        fclose(l_cfg_file);
    }

    dap_assert(dap_config_init(".") == 0, "Config subsystem initialization");
    dap_config_t *l_cfg = dap_config_open(s_server_cfg_name);
    dap_assert(l_cfg != NULL, "Test config opened");
    dap_config_set_global(l_cfg);

    int l_ret = dap_server_init();
    dap_assert(l_ret == 0, "Server initialization");

    // After init with [server]/enabled=true - should return true
    bool l_enabled_after = dap_server_enabled();
    dap_assert(l_enabled_after, "Server enabled after init");

    dap_server_deinit();

    dap_config_close(l_cfg);   // also clears g_config back to NULL
    dap_config_deinit();
    dap_assert(remove(l_cfg_path) == 0, "Remove test config file");
}

/**
 * @brief Test: Default server set/get
 */
static void s_test_server_default(void)
{
    log_it(L_INFO, "Testing default server");

    int l_ret = dap_server_init();
    dap_assert(l_ret == 0, "Server initialization");

    // Get default server (should be NULL initially)
    dap_server_t *l_default_before = dap_server_get_default();
    log_it(L_DEBUG, "Default server before set: %p", l_default_before);

    // Create and set default server
    dap_events_socket_callbacks_t l_server_callbacks = {0};
    dap_events_socket_callbacks_t l_client_callbacks = {0};

    dap_server_t *l_server = dap_server_new("test_section",
                                             &l_server_callbacks,
                                             &l_client_callbacks);
    dap_assert(l_server != NULL, "Create server");

    if (l_server) {
        dap_server_set_default(l_server);

        // Get default server (should be our server)
        dap_server_t *l_default_after = dap_server_get_default();
        dap_assert(l_default_after == l_server, "Default server matches");

        dap_server_delete(l_server);
        dap_server_set_default(NULL);   // don't leave a dangling pointer for later tests
    }

    dap_server_deinit();
}

/**
 * @brief Test: Add listen address to server
 *
 * confcall W59-R7.1: dap_server_listen_addr_add() opens a real socket,
 * binds/listens it and hands it to dap_worker_add_events_socket_auto(),
 * which dereferences a live reactor worker via
 * dap_events_worker_get_auto() - exactly as in production (dap_sdk.c
 * always starts the reactor before any listener is added). Port 0 lets the
 * kernel pick a free ephemeral port so the test can't collide with
 * anything else bound to a fixed port.
 */
static void s_test_server_listen_addr_add(void)
{
    log_it(L_INFO, "Testing listen address addition");

    dap_assert(dap_events_init(1, 60) == 0, "Events initialization");
    dap_assert(dap_events_start() == 0, "Events started");

    int l_ret = dap_server_init();
    dap_assert(l_ret == 0, "Server initialization");

    dap_events_socket_callbacks_t l_server_callbacks = {0};
    dap_events_socket_callbacks_t l_client_callbacks = {0};

    dap_server_t *l_server = dap_server_new("test_section",
                                             &l_server_callbacks,
                                             &l_client_callbacks);
    dap_assert(l_server != NULL, "Create server");

    if (l_server) {
        // Test adding TCP listen address on an ephemeral port
        dap_events_socket_callbacks_t l_listen_callbacks = {0};
        int l_add_ret = dap_server_listen_addr_add(l_server, "127.0.0.1", 0,
                                                     DESCRIPTOR_TYPE_SOCKET_LISTENING,
                                                     &l_listen_callbacks);
        log_it(L_DEBUG, "Listen addr add returned: %d", l_add_ret);
        dap_assert(l_add_ret == 0, "Listen address added to a live reactor");

        if (l_add_ret == 0 && l_server->es_listeners) {
            // Assignment to a worker happens asynchronously on the worker
            // thread (dap_worker_add_events_socket() posts to queue_es_new
            // when called off-thread) - poll for it instead of asserting
            // immediately.
            dap_events_socket_t *l_listener_es = (dap_events_socket_t *)l_server->es_listeners->data;
            for (int i = 0; i < 200 && !l_listener_es->worker; i++)
                usleep(10000);
            dap_assert(l_listener_es->worker != NULL, "Listener socket assigned to a worker");
        }

        dap_server_delete(l_server);
    }

    dap_server_deinit();
    dap_events_stop_all();
    dap_events_wait();
    dap_events_deinit();
}

/**
 * @brief Test: Set server callbacks
 */
static void s_test_server_callbacks_set(void)
{
    log_it(L_INFO, "Testing server callbacks setting");

    int l_ret = dap_server_init();
    dap_assert(l_ret == 0, "Server initialization");

    dap_events_socket_callbacks_t l_server_callbacks = {0};
    dap_events_socket_callbacks_t l_client_callbacks = {0};

    dap_server_t *l_server = dap_server_new("test_section",
                                             &l_server_callbacks,
                                             &l_client_callbacks);
    dap_assert(l_server != NULL, "Create server");

    if (l_server) {
        // Create new callbacks
        dap_events_socket_callbacks_t l_new_server_cb = {0};
        dap_events_socket_callbacks_t l_new_client_cb = {0};

        // Set new callbacks
        int l_set_ret = dap_server_callbacks_set(l_server, &l_new_server_cb, &l_new_client_cb);
        log_it(L_DEBUG, "Callbacks set returned: %d", l_set_ret);
        dap_assert(l_set_ret == 0, "Callbacks set successfully");

        dap_server_delete(l_server);
    }

    dap_server_deinit();
}

/**
 * @brief Test: Edge cases and NULL checks
 */
static void s_test_server_edge_cases(void)
{
    log_it(L_INFO, "Testing server edge cases");

    int l_ret = dap_server_init();
    dap_assert(l_ret == 0, "Server initialization");

    // Test creating server with NULL section - the whole config-driven
    // listener block is guarded by `if (a_cfg_section)`, so this always
    // succeeds with an empty listener list.
    dap_events_socket_callbacks_t l_callbacks = {0};
    dap_server_t *l_server_null = dap_server_new(NULL, &l_callbacks, &l_callbacks);
    dap_assert(l_server_null != NULL, "Server with NULL section still created");

    if (l_server_null) {
        dap_server_delete(l_server_null);
    }

    // Test setting NULL as default
    dap_server_set_default(NULL);
    dap_pass_msg("Set NULL default handled gracefully");

    // Test deleting NULL server
    dap_server_delete(NULL);
    dap_pass_msg("Delete NULL server handled gracefully");

    dap_server_deinit();
}

/**
 * @brief Test: Multiple servers
 */
static void s_test_multiple_servers(void)
{
    log_it(L_INFO, "Testing multiple servers");

    int l_ret = dap_server_init();
    dap_assert(l_ret == 0, "Server initialization");

    // Create multiple servers. g_config is unbound here (NULL) so each
    // cfg-section lookup is a safe no-op and no listeners get created.
    dap_events_socket_callbacks_t l_callbacks = {0};

    dap_server_t *l_server1 = dap_server_new("test1", &l_callbacks, &l_callbacks);
    dap_server_t *l_server2 = dap_server_new("test2", &l_callbacks, &l_callbacks);
    dap_server_t *l_server3 = dap_server_new("test3", &l_callbacks, &l_callbacks);

    dap_assert(l_server1 != NULL, "Create server 1");
    dap_assert(l_server2 != NULL, "Create server 2");
    dap_assert(l_server3 != NULL, "Create server 3");

    // Cleanup
    if (l_server1) dap_server_delete(l_server1);
    if (l_server2) dap_server_delete(l_server2);
    if (l_server3) dap_server_delete(l_server3);

    dap_server_deinit();
}

/**
 * @brief Main test function
 */
int main(int argc, char **argv)
{
    UNUSED(argc);
    UNUSED(argv);

    // Initialize DAP SDK
    int l_ret = dap_common_init("test_dap_server", NULL);
    if (l_ret != 0) {
        printf("Failed to initialize DAP SDK\n");
        return 1;
    }

    // Initialize mock framework
    dap_mock_init();

    log_it(L_INFO, "=== DAP Server - Unit Tests ===");

    // Run tests
    s_test_server_init_deinit();
    s_test_server_new();
    s_test_server_enabled();
    s_test_server_default();
    s_test_server_listen_addr_add();
    s_test_server_callbacks_set();
    s_test_server_edge_cases();
    s_test_multiple_servers();

    log_it(L_INFO, "=== All Server Tests PASSED! ===");

    // Cleanup
    dap_mock_deinit();
    dap_common_deinit();

    return 0;
}
