/*
 * Authors:
 * Cellframe SDK Team
 * DeM Labs Inc.   https://demlabs.net
 * DeM Labs Open source community https://gitlab.demlabs.net/cellframe
 * Copyright  (c) 2017-2025
 * All rights reserved.

 This file is part of DAP (Distributed Applications Platform) the open source project

    DAP (Distributed Applications Platform) is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    DAP is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with any DAP based project.  If not, see <http://www.gnu.org/licenses/>.
*/

/**
 * @file test_dap_config.c
 * @brief Unit tests for dap_config module
 */

#include <dap_test.h>
#include <dap_config.h>
#include <stdio.h>
#include <string.h>

static const char *s_testconfig_name = "test_dap_config.cfg";
static const char *s_config_data = "[db_options]\n"
                                   "db_type=mongoDb\n"
                                   "[server_options]\n"
                                   "timeout=1,0\n"
                                   "vpn_enable=true\n"
                                   "proxy_enable=false\n"
                                   "TTL_session_key=600\n"
                                   "str_arr=[vasya, petya, grisha, petushok@microsoft.com]\n"
                                   "int_arr=[1, 3, 5]\n";

#define STR_ARR_LEN 4
static const char *s_str_arr_test_cases[] = {
    "vasya",
    "petya",
    "grisha",
    "petushok@microsoft.com"
};
#define INT_ARR_LEN 3
static const int32_t s_int_arr_test_cases[] = {1, 3, 5};

static FILE *s_config_file;
static dap_config_t *s_config;

static void create_test_config_file(void)
{
    s_config_file = fopen(s_testconfig_name, "w+");
    dap_assert(s_config_file != NULL, "Create config file");

    fwrite(s_config_data, sizeof(char), strlen(s_config_data), s_config_file);
    fclose(s_config_file);
}

static void init_test_case(void)
{
    create_test_config_file();

    // init dir path for configs files
    dap_config_init(".");

    s_config = dap_config_open("test_dap_config");
    dap_assert(s_config != NULL, "Config opened");
}

static void cleanup_test_case(void)
{
    dap_assert(remove("test_dap_config.cfg") == 0, "Remove config file");
    dap_config_close(s_config);
    dap_config_deinit();
}

static void test_config_open_fail(void)
{
    dap_assert(dap_config_open("RandomNeverExistName") == NULL,
           "Try open not exists config file");
}

static void test_get_int(void)
{
    int32_t l_result_ttl = dap_config_get_item_int32(s_config,
                                                     "server_options",
                                                     "TTL_session_key");
    dap_assert(l_result_ttl == 600, "Get int from config");
}

static void test_get_int_default(void)
{
    int32_t l_result_ttl_default = dap_config_get_item_int32_default(s_config,
                                                                     "server_options",
                                                                     "TTL_session_key",
                                                                     650);
    dap_assert(l_result_ttl_default == 600, "The correct valid int value is obtained from the default function");

    int32_t l_result_ttl_default1 = dap_config_get_item_int32_default(s_config,
                                                                      "server_options",
                                                                      "TTL_session_key2",
                                                                      650);
    dap_assert(l_result_ttl_default1 == 650, "The correct default value of int from the default function is obtained");
}

static void test_get_double(void)
{
    double l_timeout = dap_config_get_item_double(s_config,
                                                  "server_options",
                                                  "timeout");
    dap_assert(l_timeout == 1.0, "Get double from config");
}

static void test_get_double_default(void)
{
    double l_timeout_default = dap_config_get_item_double_default(s_config,
                                                                  "server_options",
                                                                  "timeout",
                                                                  1.0);
    dap_assert(l_timeout_default == 1.0, "The correct valid double value is obtained from the default function");

    double l_timeout_default2 = dap_config_get_item_double_default(s_config,
                                                                   "server_options",
                                                                   "ghsdgfyhj",
                                                                   1.5);
    dap_assert(l_timeout_default2 == 1.5, "The correct default value of double from the default function is obtained");
}

static void test_get_bool(void)
{
    bool l_bool = dap_config_get_item_bool(s_config, "server_options", "vpn_enable");
    dap_assert(l_bool == true, "Get bool from config");
    l_bool = dap_config_get_item_bool(s_config, "server_options", "proxy_enable");
    dap_assert(l_bool == false, "Get bool from config");
}

static void test_get_bool_default(void)
{
    bool l_bool = dap_config_get_item_bool_default(s_config, "server_options", "proxy_enable", true);
    dap_assert(l_bool == false, "received true true bool value from a function default");
    l_bool = dap_config_get_item_bool_default(s_config, "server_options", "proxy_enable2", false);
    dap_assert(l_bool == false, "the correct default value of bool is obtained from the default function");
}

static void test_array_str(void)
{
    uint16_t l_array_size;
    const char **l_result_arr = dap_config_get_array_str(s_config, "server_options", "str_arr", &l_array_size);

    dap_assert(l_result_arr != NULL, "Get array str from config");
    dap_assert(l_array_size == STR_ARR_LEN, "Check array length");

    for (uint32_t i = 0; i < l_array_size; i++) {
        dap_assert(strcmp(l_result_arr[i], s_str_arr_test_cases[i]) == 0, "test_array_str value");
    }
}

static void test_array_int(void)
{
    uint16_t l_array_size;
    const char **l_result_arr = dap_config_get_array_str(s_config, "server_options", "int_arr", &l_array_size);

    dap_assert(l_result_arr != NULL, "Get array int");
    dap_assert(l_array_size == INT_ARR_LEN, "Check array int length");

    dap_test_msg("Testing array int values.");
    for (uint32_t i = 0; i < l_array_size; i++) {
        dap_assert_PIF(atoi(l_result_arr[i]) == s_int_arr_test_cases[i], "Check array int");
    }
}

static void test_get_item_str(void)
{
    const char *l_db_type = dap_config_get_item_str(s_config, "db_options", "db_type");
    const char *l_expected = "mongoDb";
    dap_assert(memcmp(l_db_type, l_expected, 7) == 0, "The function returns const char*");
}

static void test_get_item_str_default(void)
{
    const char *l_expected1 = "mongoDb";
    const char *l_expected2 = "EDb";

    const char *l_db_type = dap_config_get_item_str_default(s_config, "db_options", "db_type", "EDb");
    dap_assert(memcmp(l_db_type, l_expected1, 7) == 0, "The function returns the true value of const char *");

    const char *l_db_type2 = dap_config_get_item_str_default(s_config, "db_options", "db_type2", "EDb");
    dap_assert(memcmp(l_db_type2, l_expected2, 3) == 0, "The function returns the default const char *");
}

int main(void)
{
    dap_log_level_set(L_CRITICAL);
    dap_print_module_name("dap_config");

    init_test_case();

    test_config_open_fail();
    test_get_int();
    test_get_int_default();
    test_get_bool();
    test_get_bool_default();
    test_array_str();
    test_array_int();
    test_get_item_str();
    test_get_item_str_default();
    test_get_double();
    test_get_double_default();

    cleanup_test_case();

    return 0;
}
