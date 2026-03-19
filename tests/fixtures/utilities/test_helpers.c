/*
 * Authors:
 * Dmitry Gerasimov <ceo@cellframe.net>
 * DeM Labs Inc.   https://demlabs.net
 * DAP SDK  https://gitlab.demlabs.net/dap/dap-sdk
 * Copyright  (c) 2025
 * All rights reserved.

 This file is part of DAP SDK the open source project

    DAP SDK is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    DAP SDK is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with any DAP SDK based project.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "test_helpers.h"
#include "dap_config.h"
#include "dap_common.h"
#include "dap_file_utils.h"
#include "dap_strfuncs.h"
#include "dap_sdk.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define LOG_TAG "dap_test_helpers"

static char *s_test_root = NULL;

static size_t s_allocated_memory = 0;
static size_t s_allocation_count = 0;

/**
 * @brief Test memory allocation with tracking
 */
void* dap_test_mem_alloc(size_t a_size) {
    void* l_ptr = DAP_NEW_SIZE(uint8_t, a_size);
    if (l_ptr) {
        s_allocated_memory += a_size;
        s_allocation_count++;
        log_it(L_DEBUG, "Test allocated %zu bytes (total: %zu)", a_size, s_allocated_memory);
    }
    return l_ptr;
}

/**
 * @brief Free test memory with tracking
 */
void dap_test_mem_free(void* a_ptr) {
    if (a_ptr) {
        DAP_DELETE(a_ptr);
        s_allocation_count--;
        log_it(L_DEBUG, "Test freed memory (allocations remaining: %zu)", s_allocation_count);
    }
}

/**
 * @brief Generate random bytes for testing
 */
void dap_test_random_bytes(uint8_t* a_buffer, size_t a_size) {
    if (!a_buffer || a_size == 0) {
        return;
    }
    
    // Initialize random seed if not done
    static bool s_rand_initialized = false;
    if (!s_rand_initialized) {
        srand((unsigned int)time(NULL));
        s_rand_initialized = true;
    }
    
    for (size_t i = 0; i < a_size; i++) {
        a_buffer[i] = (uint8_t)(rand() % 256);
    }
}

/**
 * @brief Generate random string for testing
 */
char* dap_test_random_string(size_t a_length) {
    if (a_length == 0) {
        return NULL;
    }
    
    char* l_str = DAP_NEW_SIZE(char, a_length + 1);
    if (!l_str) {
        return NULL;
    }
    
    // Character set for random strings
    const char* l_charset = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    size_t l_charset_len = strlen(l_charset);
    
    for (size_t i = 0; i < a_length; i++) {
        l_str[i] = l_charset[rand() % l_charset_len];
    }
    l_str[a_length] = '\0';
    
    return l_str;
}

static int s_create_test_env(void)
{
    char l_template[] = "/tmp/dap_test_XXXXXX";
    char *l_root = mkdtemp(l_template);
    if (!l_root) {
        log_it(L_ERROR, "mkdtemp failed: %s", strerror(errno));
        return -1;
    }
    s_test_root = dap_strdup(l_root);

    char *l_config_dir = dap_strdup_printf("%s/config", s_test_root);
    char *l_ca_dir     = dap_strdup_printf("%s/ca", s_test_root);
    char *l_gdb_dir    = dap_strdup_printf("%s/var/lib/global_db", s_test_root);

    dap_mkdir_with_parents(l_config_dir);
    dap_mkdir_with_parents(l_ca_dir);
    dap_mkdir_with_parents(l_gdb_dir);

    char *l_cfg_path = dap_strdup_printf("%s/dap-test.cfg", l_config_dir);
    FILE *f = fopen(l_cfg_path, "w");
    if (!f) {
        log_it(L_ERROR, "Can't create test config: %s", l_cfg_path);
        DAP_DELETE(l_config_dir);
        DAP_DELETE(l_ca_dir);
        DAP_DELETE(l_gdb_dir);
        DAP_DELETE(l_cfg_path);
        return -1;
    }
    fprintf(f,
        "[resources]\n"
        "ca_folders=[%s]\n\n"
        "[global_db]\n"
        "path=%s\n",
        l_ca_dir, l_gdb_dir);
    fclose(f);

    DAP_DELETE(l_config_dir);
    DAP_DELETE(l_ca_dir);
    DAP_DELETE(l_gdb_dir);
    DAP_DELETE(l_cfg_path);
    return 0;
}

/**
 * @brief Setup full DAP SDK environment for testing
 */
int dap_test_sdk_init(void) {
    log_it(L_INFO, "Initializing DAP SDK test environment");

    s_allocated_memory = 0;
    s_allocation_count = 0;

    if (s_create_test_env() != 0)
        return -1;

    char *l_config_dir = dap_strdup_printf("%s/config", s_test_root);

    dap_sdk_config_t l_cfg = {
        .modules     = DAP_SDK_MODULE_ALL,
        .app_name    = "DAP SDK Tests",
        .sys_dir     = s_test_root,
        .config_dir  = l_config_dir,
        .config_name = "dap-test",
    };

    int ret = dap_sdk_init(&l_cfg);
    DAP_DELETE(l_config_dir);

    if (ret != 0) {
        log_it(L_ERROR, "Failed to initialize DAP SDK: %d", ret);
        return ret;
    }

    log_it(L_INFO, "DAP SDK test environment initialized successfully");
    return 0;
}

/**
 * @brief Cleanup DAP SDK test environment
 */
void dap_test_sdk_cleanup(void) {
    log_it(L_INFO, "Cleaning up DAP SDK test environment");

    dap_sdk_deinit();

    if (s_test_root) {
        dap_rm_rf(s_test_root);
        DAP_DEL_Z(s_test_root);
    }

    if (s_allocation_count > 0) {
        log_it(L_WARNING, "Memory leak detected: %zu allocations not freed", s_allocation_count);
    } else {
        log_it(L_INFO, "No memory leaks detected");
    }

    log_it(L_INFO, "DAP SDK test environment cleanup completed");
}

