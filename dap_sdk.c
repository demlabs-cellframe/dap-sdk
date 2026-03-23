/*
 * DAP SDK — centralized initialization / deinitialization
 *
 * Authors:
 *   Dmitry Gerasimov <ceo@cellframe.net>
 *   DeM Labs Inc.   https://demlabs.net
 * Copyright (c) 2025-2026
 * License: GPLv3
 */

#include "dap_sdk.h"
#include "dap_common.h"
#include "dap_config.h"
#include "dap_strfuncs.h"
#include "dap_file_utils.h"
#include "dap_crc64.h"
#include "dap_net_common.h"
#include "dap_events.h"
#include "dap_enc.h"
#include "dap_cert.h"
#include "dap_stream.h"
#include "dap_stream_ctl.h"
#include "dap_client.h"
#include "dap_server.h"
#include "dap_cluster.h"
#include "dap_cluster_node.h"
#include "dap_global_db.h"
#include "dap_enc_ks.h"

#include "dap_link_manager.h"
#ifndef DAP_OS_WASM
#include "dap_http_server.h"
#include "dap_http_folder.h"
#include "dap_http_simple.h"
#include "dap_enc_http.h"
#include "dap_notify_srv.h"
#include "dap_cli_server.h"
#include "dap_dns_server.h"
#include "dap_plugin.h"
#endif

#if defined(DAP_OS_WASM)
#include <sys/stat.h>
#endif

#define LOG_TAG "dap_sdk"

static bool     s_initialized = false;
static uint32_t s_modules     = 0;

/* ========================================================================= */
/*  Forward declarations                                                     */
/* ========================================================================= */

static int  s_init_core        (const dap_sdk_config_t *);
static int  s_init_crypto      (const dap_sdk_config_t *);
static int  s_init_io          (const dap_sdk_config_t *);
static int  s_init_net_server  (const dap_sdk_config_t *);
static int  s_init_net_http    (const dap_sdk_config_t *);
static int  s_init_net_enc     (const dap_sdk_config_t *);
static int  s_init_net_stream  (const dap_sdk_config_t *);
static int  s_init_net_cluster (const dap_sdk_config_t *);
static int  s_init_net_client  (const dap_sdk_config_t *);
static int  s_init_net_notify  (const dap_sdk_config_t *);
static int  s_init_global_db   (const dap_sdk_config_t *);
static int  s_init_net_link_mgr(const dap_sdk_config_t *);
static int  s_init_net_dns     (const dap_sdk_config_t *);
static int  s_init_cli_server  (const dap_sdk_config_t *);
static int  s_init_plugin      (const dap_sdk_config_t *);
static int  s_init_test        (const dap_sdk_config_t *);
static int  s_ensure_node_addr_cert(void);

static void s_deinit_core(void);
static void s_deinit_crypto(void);
static void s_deinit_io(void);
static void s_deinit_net_server(void);
static void s_deinit_net_http(void);
static void s_deinit_net_enc(void);
static void s_deinit_net_stream(void);
static void s_deinit_net_client(void);
static void s_deinit_net_notify(void);
static void s_deinit_global_db(void);
static void s_deinit_net_link_mgr(void);
static void s_deinit_net_dns(void);
static void s_deinit_cli_server(void);
static void s_deinit_plugin(void);

/* ========================================================================= */
/*  WASM filesystem bootstrap                                                */
/* ========================================================================= */

#ifdef DAP_OS_WASM
static void s_init_memfs(void)
{
    if (!g_sys_dir_path) {
        g_sys_dir_path = dap_strdup("/dap");
        mkdir(g_sys_dir_path, 0777);
    }
    log_it(L_NOTICE, "WASM using MEMFS at %s (non-persistent)", g_sys_dir_path);
}
#endif

/* ========================================================================= */
/*  Helper: run init step with logging                                       */
/* ========================================================================= */

#define DAP_SDK_INIT_MODULE(flag, func, label) \
    if (l_modules & (flag)) { \
        if ((l_rc = (func)(a_config)) != 0) \
            return log_it(L_CRITICAL, "Failed to init " label), l_rc; \
    }

/* ========================================================================= */
/*  Public API                                                               */
/* ========================================================================= */

int dap_sdk_init(const dap_sdk_config_t *a_config)
{
    if (!a_config) return -1;
    if (s_initialized) {
        log_it(L_WARNING, "DAP SDK already initialized");
        return 0;
    }

    int l_rc = 0;
    uint32_t l_modules = a_config->modules | DAP_SDK_MODULE_CORE;

    if ((l_rc = s_init_core(a_config)) != 0)
        return l_rc;

    DAP_SDK_INIT_MODULE(DAP_SDK_MODULE_CRYPTO,       s_init_crypto,       "crypto");
    DAP_SDK_INIT_MODULE(DAP_SDK_MODULE_IO,           s_init_io,           "IO");
    DAP_SDK_INIT_MODULE(DAP_SDK_MODULE_NET_SERVER,   s_init_net_server,   "net server");
    DAP_SDK_INIT_MODULE(DAP_SDK_MODULE_NET_HTTP,     s_init_net_http,     "net HTTP");
    DAP_SDK_INIT_MODULE(DAP_SDK_MODULE_NET_ENC,      s_init_net_enc,      "net enc");
    DAP_SDK_INIT_MODULE(DAP_SDK_MODULE_NET_STREAM,   s_init_net_stream,   "net stream");
    DAP_SDK_INIT_MODULE(DAP_SDK_MODULE_NET_CLUSTER,  s_init_net_cluster,  "net cluster");
    DAP_SDK_INIT_MODULE(DAP_SDK_MODULE_NET_CLIENT,   s_init_net_client,   "net client");
    DAP_SDK_INIT_MODULE(DAP_SDK_MODULE_NET_NOTIFY,   s_init_net_notify,   "net notify");
    DAP_SDK_INIT_MODULE(DAP_SDK_MODULE_GLOBAL_DB,    s_init_global_db,    "global DB");
    DAP_SDK_INIT_MODULE(DAP_SDK_MODULE_NET_LINK_MGR, s_init_net_link_mgr, "link manager");
    DAP_SDK_INIT_MODULE(DAP_SDK_MODULE_NET_DNS,      s_init_net_dns,      "DNS");
    DAP_SDK_INIT_MODULE(DAP_SDK_MODULE_CLI_SERVER,   s_init_cli_server,   "CLI server");
    DAP_SDK_INIT_MODULE(DAP_SDK_MODULE_PLUGIN,       s_init_plugin,       "plugin");
    DAP_SDK_INIT_MODULE(DAP_SDK_MODULE_TEST,         s_init_test,         "test framework");

    s_initialized = true;
    s_modules = l_modules;
    log_it(L_NOTICE, "DAP SDK initialized (modules 0x%08X)", s_modules);
    return 0;
}

int dap_sdk_init_simple(uint32_t a_modules)
{
    dap_sdk_config_t l_cfg = { .modules = a_modules, .app_name = "DAP SDK" };
    return dap_sdk_init(&l_cfg);
}

int dap_sdk_init_with_app_name(const char *a_app_name, uint32_t a_modules)
{
    if (!a_app_name) return -1;
    dap_sdk_config_t l_cfg = { .modules = a_modules, .app_name = a_app_name };
    return dap_sdk_init(&l_cfg);
}

void dap_sdk_deinit(void)
{
    if (!s_initialized) return;
    log_it(L_INFO, "Deinitializing DAP SDK (modules 0x%08X)", s_modules);

    if (s_modules & DAP_SDK_MODULE_PLUGIN)       s_deinit_plugin();
    if (s_modules & DAP_SDK_MODULE_CLI_SERVER)    s_deinit_cli_server();
    if (s_modules & DAP_SDK_MODULE_NET_DNS)       s_deinit_net_dns();
    if (s_modules & DAP_SDK_MODULE_NET_LINK_MGR)  s_deinit_net_link_mgr();
    if (s_modules & DAP_SDK_MODULE_GLOBAL_DB)     s_deinit_global_db();
    if (s_modules & DAP_SDK_MODULE_NET_NOTIFY)    s_deinit_net_notify();
    if (s_modules & DAP_SDK_MODULE_NET_CLIENT)    s_deinit_net_client();
    if (s_modules & DAP_SDK_MODULE_NET_STREAM)    s_deinit_net_stream();
    if (s_modules & DAP_SDK_MODULE_NET_ENC)       s_deinit_net_enc();
    if (s_modules & DAP_SDK_MODULE_NET_HTTP)      s_deinit_net_http();
    if (s_modules & DAP_SDK_MODULE_NET_SERVER)    s_deinit_net_server();
    if (s_modules & DAP_SDK_MODULE_IO)            s_deinit_io();
    if (s_modules & DAP_SDK_MODULE_CRYPTO)        s_deinit_crypto();
    s_deinit_core();

    s_initialized = false;
    s_modules = 0;
}

bool     dap_sdk_is_initialized(void) { return s_initialized; }
uint32_t dap_sdk_get_modules(void)    { return s_modules; }

/* ========================================================================= */
/*  Module init implementations                                              */
/* ========================================================================= */

static int s_init_core(const dap_sdk_config_t *a_config)
{
    int l_rc = 0;
    dap_log_level_set(a_config->log_level);

#ifdef DAP_OS_WASM
    s_init_memfs();
#endif

    if (a_config->sys_dir && !g_sys_dir_path)
        g_sys_dir_path = dap_strdup(a_config->sys_dir);

    const char *l_app = a_config->app_name ? a_config->app_name : "DAP SDK";
    if ((l_rc = dap_common_init(l_app, a_config->log_file)) != 0)
        return log_it(L_ERROR, "dap_common_init failed: %d", l_rc), l_rc;
    if ((l_rc = dap_crc64_init()) != 0)
        return log_it(L_ERROR, "dap_crc64_init failed: %d", l_rc), l_rc;

    if (a_config->config_dir) {
        if ((l_rc = dap_config_init(a_config->config_dir)) != 0)
            return log_it(L_ERROR, "dap_config_init(%s) failed: %d", a_config->config_dir, l_rc), l_rc;
    }

    if (a_config->config_name) {
        g_config = dap_config_open(a_config->config_name);
        if (!g_config)
            return log_it(L_CRITICAL, "Can't open config %s.cfg", a_config->config_name), -5;
    }

    if (a_config->enable_debug || (g_config && dap_config_get_item_bool_default(g_config, "general", "debug_mode", false)))
        dap_log_level_set(L_DEBUG);

    return 0;
}

static int s_init_crypto(const dap_sdk_config_t *a_config)
{
    (void)a_config;
    return dap_enc_init();
}

static int s_init_io(const dap_sdk_config_t *a_config)
{
    uint32_t l_threads = a_config->io_threads;
    if (!l_threads && g_config)
        l_threads = dap_config_get_item_int32_default(g_config, "resources", "threads_cnt", 0);
    uint32_t l_timeout = a_config->io_timeout;
    int l_rc = dap_events_init(l_threads, l_timeout);
    if (l_rc) return l_rc;
    dap_events_start();
    return dap_net_common_init();
}

static int s_init_net_server(const dap_sdk_config_t *a_config)
{
    (void)a_config;
#ifndef DAP_OS_WASM
    if (g_config && !dap_config_get_item_bool_default(g_config, "server", "enabled", false))
        return 0;
    return dap_server_init();
#else
    return 0;
#endif
}

static int s_init_net_http(const dap_sdk_config_t *a_config)
{
    (void)a_config;
#ifndef DAP_OS_WASM
    int l_rc;
    if ((l_rc = dap_http_init()) != 0) return l_rc;
    if ((l_rc = dap_http_folder_init()) != 0) return l_rc;
    if ((l_rc = dap_http_simple_module_init()) != 0) return l_rc;
#endif
    return 0;
}

static int s_init_net_enc(const dap_sdk_config_t *a_config)
{
    (void)a_config;
#ifndef DAP_OS_WASM
    return enc_http_init();
#else
    return 0;
#endif
}

static int s_init_net_stream(const dap_sdk_config_t *a_config)
{
    (void)a_config;
    int l_rc;
#ifdef DAP_OS_WASM
    if ((l_rc = s_ensure_node_addr_cert()) != 0)
        return l_rc;
#endif
    if ((l_rc = dap_stream_init(g_config)) != 0) return l_rc;
#ifndef DAP_OS_WASM
    if ((l_rc = dap_stream_ctl_init()) != 0) return l_rc;
#endif
    return 0;
}

static int s_init_net_cluster(const dap_sdk_config_t *a_config)
{
    (void)a_config;
    return dap_cluster_init();
}

static int s_init_net_client(const dap_sdk_config_t *a_config)
{
    (void)a_config;
    dap_client_init();
    return 0;
}

static int s_init_net_notify(const dap_sdk_config_t *a_config)
{
    (void)a_config;
#ifndef DAP_OS_WASM
    return dap_notify_server_init();
#else
    return 0;
#endif
}

static int s_ensure_node_addr_cert(void)
{
    dap_cert_t *l_cert = dap_cert_find_by_name(DAP_STREAM_NODE_ADDR_CERT_NAME);
    if (!l_cert) {
        const char *l_folder = dap_cert_get_folder(DAP_CERT_FOLDER_PATH_DEFAULT);
        if (!l_folder) {
            if (!g_sys_dir_path)
                return log_it(L_CRITICAL, "No cert folder and no sys_dir — "
                              "set [resources] ca_folders in config"), -1;
            char *l_default = dap_strdup_printf("%s/certs", g_sys_dir_path);
            dap_cert_add_folder(l_default);
            DAP_DELETE(l_default);
            l_folder = dap_cert_get_folder(DAP_CERT_FOLDER_PATH_DEFAULT);
        }
        dap_mkdir_with_parents(l_folder);

        char *l_path = dap_strdup_printf("%s/%s.dcert", l_folder,
                                         DAP_STREAM_NODE_ADDR_CERT_NAME);
        l_cert = dap_cert_generate(DAP_STREAM_NODE_ADDR_CERT_NAME,
                                   l_path,
                                   DAP_STREAM_NODE_ADDR_CERT_TYPE);
        DAP_DELETE(l_path);
        if (!l_cert)
            return log_it(L_CRITICAL, "Failed to generate %s certificate",
                          DAP_STREAM_NODE_ADDR_CERT_NAME), -2;

        log_it(L_NOTICE, "Generated %s certificate (%s)",
               DAP_STREAM_NODE_ADDR_CERT_NAME,
               dap_enc_get_type_name(DAP_STREAM_NODE_ADDR_CERT_TYPE));
    }
    g_node_addr = dap_cluster_node_addr_from_cert(l_cert);
    return 0;
}

static int s_init_global_db(const dap_sdk_config_t *a_config)
{
    if (a_config->auto_node_cert) {
        int l_rc = s_ensure_node_addr_cert();
        if (l_rc)
            return l_rc;
    }
    return dap_global_db_init();
}

static int s_default_fill_net_info(dap_link_t *a_link)
{
    (void)a_link;
    return 0;
}

static int s_init_net_link_mgr(const dap_sdk_config_t *a_config)
{
    const dap_link_manager_callbacks_t *l_cb = a_config->link_manager_callbacks;
    dap_link_manager_callbacks_t l_defaults = { .fill_net_info = s_default_fill_net_info };
    return dap_link_manager_init(l_cb ? l_cb : &l_defaults);
}

static int s_init_net_dns(const dap_sdk_config_t *a_config)
{
    (void)a_config;
#ifndef DAP_OS_WASM
    if (g_config && dap_config_get_item_bool_default(g_config, "bootstrap_balancer", "dns_server", false))
        dap_dns_server_start("bootstrap_balancer");
#endif
    return 0;
}

static int s_init_cli_server(const dap_sdk_config_t *a_config)
{
#ifndef DAP_OS_WASM
    return dap_cli_server_init(a_config->enable_debug, "cli-server");
#else
    (void)a_config;
    return 0;
#endif
}

static int s_init_plugin(const dap_sdk_config_t *a_config)
{
    (void)a_config;
#ifndef DAP_OS_WASM
    if (!g_config || !dap_config_get_item_bool_default(g_config, "plugins", "enabled", false))
        return 0;
    char *l_default = g_sys_dir_path
        ? dap_strdup_printf("%s/var/lib/plugins", g_sys_dir_path)
        : dap_strdup("/var/lib/plugins");
    const char *l_path = dap_config_get_item_str_default(g_config, "plugins", "path", l_default);
    int l_rc = dap_plugin_init(l_path);
    DAP_DELETE(l_default);
    return l_rc;
#else
    return 0;
#endif
}

static int s_init_test(const dap_sdk_config_t *a_config)
{
    (void)a_config;
    return 0;
}

/* ========================================================================= */
/*  Module deinit implementations                                            */
/* ========================================================================= */

static void s_deinit_plugin(void)
{
#ifndef DAP_OS_WASM
    if (g_config && dap_config_get_item_bool_default(g_config, "plugins", "enabled", false)) {
        dap_plugin_stop_all();
        dap_plugin_deinit();
    }
#endif
}

static void s_deinit_cli_server(void)
{
#ifndef DAP_OS_WASM
    dap_cli_server_deinit();
#endif
}

static void s_deinit_net_dns(void)
{
#ifndef DAP_OS_WASM
    dap_dns_server_stop();
#endif
}

static void s_deinit_net_link_mgr(void)
{
    dap_link_manager_deinit();
}

static void s_deinit_global_db(void)
{
    dap_global_db_deinit();
}

static void s_deinit_net_notify(void)
{
#ifndef DAP_OS_WASM
    dap_notify_server_deinit();
#endif
}

static void s_deinit_net_client(void)
{
    dap_client_deinit();
}

static void s_deinit_net_stream(void)
{
    dap_stream_deinit();
#ifndef DAP_OS_WASM
    dap_stream_ctl_deinit();
#endif
}

static void s_deinit_net_enc(void)
{
#ifndef DAP_OS_WASM
    enc_http_deinit();
    dap_enc_ks_deinit();
#endif
}

static void s_deinit_net_http(void)
{
#ifndef DAP_OS_WASM
    dap_http_folder_deinit();
    dap_http_deinit();
#endif
}

static void s_deinit_net_server(void)
{
#ifndef DAP_OS_WASM
    dap_server_deinit();
#endif
}

static void s_deinit_io(void)
{
    dap_events_deinit();
}

static void s_deinit_crypto(void)
{
    dap_enc_deinit();
}

static void s_deinit_core(void)
{
    dap_config_deinit();
    dap_common_deinit();
}
