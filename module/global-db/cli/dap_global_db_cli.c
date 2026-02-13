/*
 * Authors:
 * Dmitriy A. Gerasimov <gerasimov.dmitriy@demlabs.net>
 * Alexander Lysikov <alexander.lysikov@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net
 * Cellframe       https://cellframe.net
 * Copyright  (c) 2019-2025
 * All rights reserved.
 *
 * This file is part of DAP (Distributed Applications Platform) the open source project
 *
 * DAP is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * DAP is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with any DAP based project.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "dap_common.h"
#include "dap_strfuncs.h"
#include "dap_string.h"
#include "dap_list.h"
#include "dap_hash.h"
#include "dap_time.h"
#include "dap_enc_base64.h"

#include "dap_json.h"
#include "dap_json_rpc_errors.h"
#include "dap_cli_server.h"

#include "dap_global_db.h"
#include "dap_global_db_driver.h"

#include "dap_global_db_cli.h"

#define LOG_TAG "global_db_cli"


static int s_print_for_global_db(dap_json_t *a_json_input, dap_json_t *a_json_output, char **a_cmd_param, int a_cmd_cnt);

/**
 * @brief Main global_db CLI command handler
 */
int com_global_db(int a_argc, char **a_argv, dap_json_t *a_json_arr_reply, int a_version)
{
    enum {
        CMD_NONE, CMD_FLUSH, CMD_RECORD, CMD_WRITE, CMD_READ,
        CMD_DELETE, CMD_DROP, CMD_GET_KEYS, CMD_GROUP_LIST, CMD_CLEAR
    };
    
    int arg_index = 1;
    int cmd_name = CMD_NONE;
    
    // Parse subcommand
    if (dap_cli_server_cmd_find_option_val(a_argv, arg_index, dap_min(a_argc, arg_index + 1), "flush", NULL))
        cmd_name = CMD_FLUSH;
    else if (dap_cli_server_cmd_find_option_val(a_argv, arg_index, dap_min(a_argc, arg_index + 1), "record", NULL))
        cmd_name = CMD_RECORD;
    else if (dap_cli_server_cmd_find_option_val(a_argv, arg_index, dap_min(a_argc, arg_index + 1), "write", NULL))
        cmd_name = CMD_WRITE;
    else if (dap_cli_server_cmd_find_option_val(a_argv, arg_index, dap_min(a_argc, arg_index + 1), "read", NULL))
        cmd_name = CMD_READ;
    else if (dap_cli_server_cmd_find_option_val(a_argv, arg_index, dap_min(a_argc, arg_index + 1), "delete", NULL))
        cmd_name = CMD_DELETE;
    else if (dap_cli_server_cmd_find_option_val(a_argv, arg_index, dap_min(a_argc, arg_index + 1), "drop_table", NULL))
        cmd_name = CMD_DROP;
    else if (dap_cli_server_cmd_find_option_val(a_argv, arg_index, dap_min(a_argc, arg_index + 1), "get_keys", NULL))
        cmd_name = CMD_GET_KEYS;
    else if (dap_cli_server_cmd_find_option_val(a_argv, arg_index, dap_min(a_argc, arg_index + 1), "group_list", NULL))
        cmd_name = CMD_GROUP_LIST;
    else if (dap_cli_server_cmd_find_option_val(a_argv, arg_index, dap_min(a_argc, arg_index + 1), "clear", NULL))
        cmd_name = CMD_CLEAR;

    switch (cmd_name) {
    case CMD_FLUSH:
    {
        dap_json_t *json_obj_flush = NULL;
        int res_flush = dap_global_db_flush_sync();
        switch (res_flush) {
        case 0:
            json_obj_flush = dap_json_object_new();
            dap_json_object_add_string(json_obj_flush, a_version == 1 ? "command status" : "command_status",
                                        "Commit database and filesystem caches to disk completed.");
            dap_json_array_add(a_json_arr_reply, json_obj_flush);
            break;
        case -1:
            dap_json_rpc_error_add(a_json_arr_reply, DAP_GLOBAL_DB_CLI_CANT_OPEN_DIR,
                                   "Couldn't open db directory. Can't init cdb. Reboot the node.");
            break;
        case -2:
            dap_json_rpc_error_add(a_json_arr_reply, DAP_GLOBAL_DB_CLI_CANT_INIT_DB,
                                   "Couldn't init database. Reboot the node.");
            break;
        case -3:
            dap_json_rpc_error_add(a_json_arr_reply, DAP_GLOBAL_DB_CLI_CANT_INIT_SQL,
                                   "Can't init sqlite. Reboot the node.");
            break;
        default:
            dap_json_rpc_error_add(a_json_arr_reply, DAP_GLOBAL_DB_CLI_CANT_COMMIT_TO_DISK,
                                   "Can't commit database caches to disk. Reboot the node.");
            break;
        }
        return DAP_GLOBAL_DB_CLI_OK;
    }
    
    case CMD_RECORD:
    {
        enum { SUBCMD_GET, SUBCMD_PIN, SUBCMD_UNPIN };
        
        if (!arg_index || a_argc < 3) {
            dap_json_rpc_error_add(a_json_arr_reply, DAP_GLOBAL_DB_CLI_PARAM_ERR, "Parameters are not valid");
            return -DAP_GLOBAL_DB_CLI_PARAM_ERR;
        }
        
        int arg_index_n = ++arg_index;
        int l_subcmd;
        
        if ((arg_index_n = dap_cli_server_cmd_find_option_val(a_argv, arg_index, dap_min(a_argc, arg_index + 1), "get", NULL)) != 0) {
            l_subcmd = SUBCMD_GET;
        } else if ((arg_index_n = dap_cli_server_cmd_find_option_val(a_argv, arg_index, dap_min(a_argc, arg_index + 1), "pin", NULL)) != 0) {
            l_subcmd = SUBCMD_PIN;
        } else if ((arg_index_n = dap_cli_server_cmd_find_option_val(a_argv, arg_index, dap_min(a_argc, arg_index + 1), "unpin", NULL)) != 0) {
            l_subcmd = SUBCMD_UNPIN;
        } else {
            dap_json_rpc_error_add(a_json_arr_reply, DAP_GLOBAL_DB_CLI_PARAM_ERR,
                                   "Subcommand '%s' not recognized, available: 'get', 'pin', 'unpin'",
                                   a_argc > 2 ? a_argv[2] : "");
            return -DAP_GLOBAL_DB_CLI_PARAM_ERR;
        }
        
        const char *l_key = NULL;
        const char *l_group = NULL;
        dap_cli_server_cmd_find_option_val(a_argv, arg_index, a_argc, "-key", &l_key);
        dap_cli_server_cmd_find_option_val(a_argv, arg_index, a_argc, "-group", &l_group);
        
        size_t l_value_len = 0;
        bool l_is_pinned = false;
        dap_nanotime_t l_ts = 0;
        uint8_t *l_value = dap_global_db_get_sync(l_group, l_key, &l_value_len, &l_is_pinned, &l_ts);
        
        if (!l_value || !l_value_len) {
            dap_json_rpc_error_add(a_json_arr_reply, DAP_GLOBAL_DB_CLI_RECORD_NOT_FOUND, "Record not found");
            return -DAP_GLOBAL_DB_CLI_RECORD_NOT_FOUND;
        }
        
        dap_json_t *json_obj_rec = dap_json_object_new();
        int l_ret = 0;
        
        switch (l_subcmd) {
            case SUBCMD_GET:
            {
                char *l_value_str = DAP_NEW_Z_SIZE(char, l_value_len * 2 + 2);
                if (!l_value_str) {
                    log_it(L_CRITICAL, "%s", c_error_memory_alloc);
                    DAP_DELETE(l_value);
                    dap_json_object_free(json_obj_rec);
                    return -DAP_GLOBAL_DB_CLI_MEMORY_ERR;
                }
                dap_bin2hex(l_value_str, l_value, l_value_len);
                
                dap_json_object_add_string(json_obj_rec, a_version == 1 ? "command status" : "command_status", "Record found");
                dap_json_object_add_uint64(json_obj_rec, a_version == 1 ? "length(byte)" : "length_byte", l_value_len);
                dap_json_object_add_string(json_obj_rec, "hash", dap_get_data_hash_str(l_value, l_value_len).s);
                if (a_version == 1) {
                    dap_json_object_add_string(json_obj_rec, "pinned", l_is_pinned ? "Yes" : "No");
                } else {
                    dap_json_object_add_bool(json_obj_rec, "pinned", l_is_pinned);
                }
                dap_json_object_add_string(json_obj_rec, "value", l_value_str);
                DAP_DELETE(l_value_str);
                break;
            }
            case SUBCMD_PIN:
            {
                if (l_is_pinned) {
                    dap_json_object_add_string(json_obj_rec, a_version == 1 ? "pinned status" : "pinned_status",
                                               "Record already pinned");
                } else if (dap_global_db_pin_sync(l_group, l_key) == 0) {
                    dap_json_object_add_string(json_obj_rec, a_version == 1 ? "pinned status" : "pinned_status",
                                               "Record successfully pinned");
                } else {
                    dap_json_rpc_error_add(a_json_arr_reply, DAP_GLOBAL_DB_CLI_RECORD_NOT_PINNED, "Can't pin the record");
                    l_ret = -DAP_GLOBAL_DB_CLI_RECORD_NOT_PINNED;
                }
                break;
            }
            case SUBCMD_UNPIN:
            {
                if (!l_is_pinned) {
                    dap_json_object_add_string(json_obj_rec, a_version == 1 ? "unpinned status" : "unpinned_status",
                                               "Record already unpinned");
                } else if (dap_global_db_unpin_sync(l_group, l_key) == 0) {
                    dap_json_object_add_string(json_obj_rec, a_version == 1 ? "unpinned status" : "unpinned_status",
                                               "Record successfully unpinned");
                } else {
                    dap_json_rpc_error_add(a_json_arr_reply, DAP_GLOBAL_DB_CLI_RECORD_NOT_UNPINNED, "Can't unpin the record");
                    l_ret = -DAP_GLOBAL_DB_CLI_RECORD_NOT_UNPINNED;
                }
                break;
            }
        }
        dap_json_array_add(a_json_arr_reply, json_obj_rec);
        DAP_DELETE(l_value);
        return l_ret;
    }
    
    case CMD_WRITE:
    {
        const char *l_group_str = NULL;
        const char *l_key_str = NULL;
        const char *l_value_str = NULL;

        dap_cli_server_cmd_find_option_val(a_argv, arg_index, a_argc, "-group", &l_group_str);
        dap_cli_server_cmd_find_option_val(a_argv, arg_index, a_argc, "-key", &l_key_str);
        dap_cli_server_cmd_find_option_val(a_argv, arg_index, a_argc, "-value", &l_value_str);

        if (!l_group_str) {
            dap_json_rpc_error_add(a_json_arr_reply, DAP_GLOBAL_DB_CLI_PARAM_ERR,
                                   "%s requires parameter 'group' to be valid", a_argv[0]);
            return -DAP_GLOBAL_DB_CLI_PARAM_ERR;
        }
        if (!l_key_str) {
            dap_json_rpc_error_add(a_json_arr_reply, DAP_GLOBAL_DB_CLI_PARAM_ERR,
                                   "%s requires parameter 'key' to be valid", a_argv[0]);
            return -DAP_GLOBAL_DB_CLI_PARAM_ERR;
        }
        if (!l_value_str) {
            dap_json_rpc_error_add(a_json_arr_reply, DAP_GLOBAL_DB_CLI_PARAM_ERR,
                                   "%s requires parameter 'value' to be valid", a_argv[0]);
            return -DAP_GLOBAL_DB_CLI_PARAM_ERR;
        }

        if (!dap_global_db_set_sync(l_group_str, l_key_str, l_value_str, strlen(l_value_str) + 1, false)) {
            dap_json_t *json_obj_write = dap_json_object_new();
            dap_json_object_add_string(json_obj_write, a_version == 1 ? "write status" : "write_status",
                                       "Data has been successfully written to the database");
            dap_json_array_add(a_json_arr_reply, json_obj_write);
            return DAP_GLOBAL_DB_CLI_OK;
        } else {
            dap_json_rpc_error_add(a_json_arr_reply, DAP_GLOBAL_DB_CLI_WRITING_FAILED, "Data writing failed");
            return -DAP_GLOBAL_DB_CLI_WRITING_FAILED;
        }
    }
    
    case CMD_READ:
    {
        const char *l_group_str = NULL;
        const char *l_key_str = NULL;

        dap_cli_server_cmd_find_option_val(a_argv, arg_index, a_argc, "-group", &l_group_str);
        dap_cli_server_cmd_find_option_val(a_argv, arg_index, a_argc, "-key", &l_key_str);

        if (!l_group_str) {
            dap_json_rpc_error_add(a_json_arr_reply, DAP_GLOBAL_DB_CLI_PARAM_ERR,
                                   "%s requires parameter 'group' to be valid", a_argv[0]);
            return -DAP_GLOBAL_DB_CLI_PARAM_ERR;
        }
        if (!l_key_str) {
            dap_json_rpc_error_add(a_json_arr_reply, DAP_GLOBAL_DB_CLI_PARAM_ERR,
                                   "%s requires parameter 'key' to be valid", a_argv[0]);
            return -DAP_GLOBAL_DB_CLI_PARAM_ERR;
        }

        size_t l_out_len = 0;
        dap_nanotime_t l_ts = 0;
        uint8_t *l_value_out = dap_global_db_get_sync(l_group_str, l_key_str, &l_out_len, NULL, &l_ts);
        
        dap_json_t *json_obj_read = dap_json_object_new();
        if (l_ts) {
            char l_ts_str[80] = { '\0' };
            dap_nanotime_to_str_rfc822(l_ts_str, sizeof(l_ts_str), l_ts);
            char *l_value_hexdump = dap_dump_hex(l_value_out, l_out_len);
            if (l_value_hexdump) {
                char *l_value_hexdump_new = dap_strdup_printf("\n%s", l_value_hexdump);
                dap_json_object_add_string(json_obj_read, "group", l_group_str);
                dap_json_object_add_string(json_obj_read, "key", l_key_str);
                dap_json_object_add_string(json_obj_read, "time", l_ts_str);
                dap_json_object_add_uint64(json_obj_read, a_version == 1 ? "value len" : "value_len", l_out_len);
                dap_json_object_add_string(json_obj_read, a_version == 1 ? "value hex" : "value_hex", l_value_hexdump_new);
                DAP_DELETE(l_value_hexdump_new);
            } else {
                dap_json_rpc_error_add(a_json_arr_reply, DAP_GLOBAL_DB_CLI_TIME_NO_VALUE,
                                       "\"%s : %s\"\nTime: %s\nNo value", l_group_str, l_key_str, l_ts_str);
            }
        } else if (dap_global_db_group_match_mask(l_group_str, "*.mempool") && !l_value_out) {
            dap_store_obj_t *l_read_obj = dap_global_db_get_raw_sync(l_group_str, l_key_str);
            if (!l_read_obj || !l_read_obj->value || !l_read_obj->value_len) {
                dap_json_rpc_error_add(a_json_arr_reply, DAP_GLOBAL_DB_CLI_TIME_NO_VALUE,
                                       "\"%s : %s\"\nNo value", l_group_str, l_key_str);
            } else {
                dap_json_object_add_string(json_obj_read, "group", l_group_str);
                dap_json_object_add_string(json_obj_read, "key", l_key_str);
                dap_json_object_add_string(json_obj_read, "error", (char *)l_read_obj->value);
            }
            dap_store_obj_free_one(l_read_obj);
        } else {
            dap_json_rpc_error_add(a_json_arr_reply, DAP_GLOBAL_DB_CLI_RECORD_NOT_FOUND,
                                   "Record \"%s : %s\" not found", l_group_str, l_key_str);
        }
        DAP_DELETE(l_value_out);
        dap_json_array_add(a_json_arr_reply, json_obj_read);
        return DAP_GLOBAL_DB_CLI_OK;
    }
    
    case CMD_DELETE:
    {
        const char *l_group_str = NULL;
        const char *l_key_str = NULL;

        dap_cli_server_cmd_find_option_val(a_argv, arg_index, a_argc, "-group", &l_group_str);
        dap_cli_server_cmd_find_option_val(a_argv, arg_index, a_argc, "-key", &l_key_str);

        if (!l_group_str || !l_key_str) {
            dap_json_rpc_error_add(a_json_arr_reply, DAP_GLOBAL_DB_CLI_PARAM_ERR,
                                   "%s requires parameters 'group' and 'key' to be valid", a_argv[0]);
            return -DAP_GLOBAL_DB_CLI_PARAM_ERR;
        }

        if (!dap_global_db_driver_is(l_group_str, l_key_str)) {
            dap_json_rpc_error_add(a_json_arr_reply, DAP_GLOBAL_DB_CLI_NO_DATA_IN_GROUP,
                                   "Key %s not found in group %s", l_key_str, l_group_str);
            return -DAP_GLOBAL_DB_CLI_NO_DATA_IN_GROUP;
        }

        bool l_del_success = false;
        if (dap_global_db_group_match_mask(l_group_str, "local.*")) {
            dap_store_obj_t *l_read_obj = dap_global_db_get_raw_sync(l_group_str, l_key_str);
            l_del_success = !dap_global_db_driver_delete(l_read_obj, 1);
            dap_store_obj_free_one(l_read_obj);
        } else {
            l_del_success = !dap_global_db_del_sync(l_group_str, l_key_str);
        }

        if (l_del_success) {
            dap_json_t *json_obj_del = dap_json_object_new();
            dap_json_object_add_string(json_obj_del, a_version == 1 ? "Record key" : "record_key", l_key_str);
            dap_json_object_add_string(json_obj_del, a_version == 1 ? "Group name" : "group_name", l_group_str);
            dap_json_object_add_string(json_obj_del, "status", "deleted");
            dap_json_array_add(a_json_arr_reply, json_obj_del);
            return DAP_GLOBAL_DB_CLI_OK;
        } else {
            dap_json_rpc_error_add(a_json_arr_reply, DAP_GLOBAL_DB_CLI_DELETE_FAILED,
                                   "Record with key %s in group %s deleting failed", l_key_str, l_group_str);
            return -DAP_GLOBAL_DB_CLI_DELETE_FAILED;
        }
    }
    
    case CMD_DROP:
    {
        const char *l_group_str = NULL;
        dap_cli_server_cmd_find_option_val(a_argv, arg_index, a_argc, "-group", &l_group_str);

        if (!l_group_str) {
            dap_json_rpc_error_add(a_json_arr_reply, DAP_GLOBAL_DB_CLI_PARAM_ERR,
                                   "%s requires parameter 'group' to be valid", a_argv[0]);
            return -DAP_GLOBAL_DB_CLI_PARAM_ERR;
        }

        if (!dap_global_db_erase_table_sync(l_group_str)) {
            dap_json_t *json_obj_drop = dap_json_object_new();
            dap_json_object_add_string(json_obj_drop, a_version == 1 ? "Dropped table" : "table_dropped", l_group_str);
            dap_json_array_add(a_json_arr_reply, json_obj_drop);
            return DAP_GLOBAL_DB_CLI_OK;
        } else {
            dap_json_rpc_error_add(a_json_arr_reply, DAP_GLOBAL_DB_CLI_DROP_FAILED, "Failed to drop table %s", l_group_str);
            return -DAP_GLOBAL_DB_CLI_DROP_FAILED;
        }
    }
    
    case CMD_GET_KEYS:
    {
        const char *l_group_str = NULL;
        dap_cli_server_cmd_find_option_val(a_argv, arg_index, a_argc, "-group", &l_group_str);

        if (!l_group_str) {
            dap_json_rpc_error_add(a_json_arr_reply, DAP_GLOBAL_DB_CLI_PARAM_ERR,
                                   "%s requires parameter 'group' to be valid", a_argv[0]);
            return -DAP_GLOBAL_DB_CLI_PARAM_ERR;
        }

        size_t l_objs_count = 0;
        dap_store_obj_t *l_objs = dap_global_db_get_all_raw_sync(l_group_str, &l_objs_count);

        if (!l_objs || !l_objs_count) {
            dap_json_rpc_error_add(a_json_arr_reply, DAP_GLOBAL_DB_CLI_NO_DATA_IN_GROUP, "No data in group %s", l_group_str);
            return -DAP_GLOBAL_DB_CLI_NO_DATA_IN_GROUP;
        }

        dap_json_t *json_arr_keys = dap_json_array_new();
        for (size_t i = 0; i < l_objs_count; i++) {
            char l_ts[64] = { '\0' };
            dap_nanotime_to_str_rfc822(l_ts, sizeof(l_ts), l_objs[i].timestamp);
            dap_json_t *json_obj_keys = dap_json_object_new();
            dap_json_object_add_string(json_obj_keys, "key", l_objs[i].key);
            dap_json_object_add_string(json_obj_keys, "time", l_ts);
            dap_json_object_add_string(json_obj_keys, "type",
                                       dap_store_obj_get_type(l_objs + i) == DAP_GLOBAL_DB_OPTYPE_ADD ? "record" : "hole");
            dap_json_array_add(json_arr_keys, json_obj_keys);
        }
        dap_store_obj_free(l_objs, l_objs_count);

        dap_json_t *json_keys_list = dap_json_object_new();
        dap_json_object_add_string(json_keys_list, a_version == 1 ? "group name" : "group_name", l_group_str);
        dap_json_object_add_object(json_keys_list, a_version == 1 ? "keys list" : "keys_list", json_arr_keys);
        dap_json_array_add(a_json_arr_reply, json_keys_list);
        return DAP_GLOBAL_DB_CLI_OK;
    }
    
    case CMD_GROUP_LIST:
    {
        const char *l_mask = NULL;
        dap_cli_server_cmd_find_option_val(a_argv, arg_index, a_argc, "-mask", &l_mask);
        
        dap_json_t *json_group_list = dap_json_object_new();
        dap_list_t *l_group_list = dap_global_db_driver_get_groups_by_mask(l_mask ? l_mask : "*");
        size_t l_count = 0;
        dap_json_t *json_arr_group = dap_json_array_new();
        
        bool l_count_all = dap_cli_server_cmd_check_option(a_argv, arg_index, a_argc, "-all") != -1;
        
        for (dap_list_t *l_list = l_group_list; l_list; l_list = dap_list_next(l_list), ++l_count) {
            dap_json_t *json_obj_list = dap_json_object_new();
            dap_json_object_add_uint64(json_obj_list, (char *)l_list->data,
                                       dap_global_db_driver_count((char *)l_list->data, c_dap_global_db_driver_hash_blank, l_count_all));
            dap_json_array_add(json_arr_group, json_obj_list);
        }
        dap_json_object_add_object(json_group_list, a_version == 1 ? "group list" : "group_list", json_arr_group);
        dap_json_object_add_uint64(json_group_list, a_version == 1 ? "total count" : "total_count", l_count);
        dap_json_array_add(a_json_arr_reply, json_group_list);
        dap_list_free_full(l_group_list, NULL);
        return DAP_GLOBAL_DB_CLI_OK;
    }
    
    case CMD_CLEAR:
    {
        const char *l_group_str = NULL;
        const char *l_mask = NULL;
        bool l_pinned = dap_cli_server_cmd_find_option_val(a_argv, arg_index, a_argc, "-pinned", NULL);
        bool l_all = dap_cli_server_cmd_find_option_val(a_argv, arg_index, a_argc, "-all", NULL);
        size_t l_arg_count = (size_t)l_all;
        l_arg_count += !!dap_cli_server_cmd_find_option_val(a_argv, arg_index, a_argc, "-group", &l_group_str);
        l_arg_count += !!dap_cli_server_cmd_find_option_val(a_argv, arg_index, a_argc, "-mask", &l_mask);
        
        if ((!l_group_str && !l_mask && !l_all) || l_arg_count != 1) {
            dap_json_rpc_error_add(a_json_arr_reply, DAP_GLOBAL_DB_CLI_PARAM_ERR,
                                   "%s requires parameter 'group' or 'all' or 'mask' to be valid", a_argv[0]);
            return -DAP_GLOBAL_DB_CLI_PARAM_ERR;
        }
        
        dap_json_t *l_json_arr_clear = dap_json_array_new();
        size_t l_total_count = 0;
        
        if (l_group_str) {
            dap_json_t *l_json_obj_clear = dap_json_object_new();
            dap_json_object_add_uint64(l_json_obj_clear, l_group_str, dap_global_db_group_clear(l_group_str, l_pinned));
            dap_json_array_add(l_json_arr_clear, l_json_obj_clear);
            l_total_count = 1;
        }
        if (l_all || l_mask) {
            dap_list_t *l_group_list = dap_global_db_driver_get_groups_by_mask(l_mask ? l_mask : "*");
            for (dap_list_t *l_list = l_group_list; l_list; l_list = dap_list_next(l_list)) {
                size_t l_count = dap_global_db_group_clear((const char *)(l_list->data), l_pinned);
                if (l_count) {
                    dap_json_t *l_json_obj_clear = dap_json_object_new();
                    dap_json_object_add_uint64(l_json_obj_clear, (const char *)(l_list->data), l_count);
                    dap_json_array_add(l_json_arr_clear, l_json_obj_clear);
                    ++l_total_count;
                }
            }
            dap_list_free_full(l_group_list, NULL);
        }
        
        dap_json_t *l_json_obj_clear = dap_json_object_new();
        dap_json_object_add_object(l_json_obj_clear, "group_list_clear", l_json_arr_clear);
        dap_json_object_add_uint64(l_json_obj_clear, "total_count", l_total_count);
        dap_json_array_add(a_json_arr_reply, l_json_obj_clear);
        return DAP_GLOBAL_DB_CLI_OK;
    }
    
    default:
        dap_json_rpc_error_add(a_json_arr_reply, DAP_GLOBAL_DB_CLI_PARAM_ERR, "Parameters are not valid");
        return -DAP_GLOBAL_DB_CLI_PARAM_ERR;
    }
}

/**
 * @brief Initialize global_db CLI module
 */
int dap_global_db_cli_init(void)
{
    log_it(L_INFO, "Initializing global_db CLI module");
    
    dap_cli_server_cmd_add("global_db", com_global_db, s_print_for_global_db, "Work with global database", 0,
        "global_db flush\n"
        "\tFlushes the current state of the database to disk.\n\n"
        "global_db write -group <group_name> -key <key_name> -value <value>\n"
        "\tWrites a key value to a specified group in the database.\n\n"
        "global_db read -group <group_name> -key <key_name>\n"
        "\tReads a value by key from a specified group.\n\n"
        "global_db delete -group <group_name> -key <key_name>\n"
        "\tRemoves a value by key from a specified group.\n\n"
        "global_db record {get | pin | unpin} -group <group_name> -key <key_name>\n"
        "\tGet record info, pin or unpin a record.\n\n"
        "global_db group_list [-mask <mask>] [-all]\n"
        "\tGets a list of groups in the database.\n"
        "\t-mask <mask>: list groups by mask\n"
        "\t-all: count actual and hole record types\n\n"
        "global_db drop_table -group <group_name>\n"
        "\tPerforms deletion of the entire group in the database.\n\n"
        "global_db get_keys -group <group_name>\n"
        "\tGets all record keys from a specified group.\n\n"
        "global_db clear -group <group_name> | -mask <mask> | -all [-pinned]\n"
        "\tRemove all hole type records from a specified group or all groups.\n"
        "\t-mask <mask>: clear groups by mask\n"
        "\t-all: clear all groups\n"
        "\t-pinned: remove pinned records too\n");
    
    log_it(L_NOTICE, "global_db CLI module initialized");
    return 0;
}

/**
 * @brief Deinitialize global_db CLI module
 */
void dap_global_db_cli_deinit(void)
{
    log_it(L_INFO, "Deinitializing global_db CLI module");
}

// Callback for counting object keys
static void s_count_keys_callback(const char* key, dap_json_t* value, void* user_data) {
    (void)key; (void)value;
    int *count = (int*)user_data;
    (*count)++;
}

// Callback for printing group list to dap_string_t
static void s_print_group_callback(const char* key, dap_json_t* val, void* user_data) {
    dap_string_t *l_str = (dap_string_t*)user_data;
    if (l_str)
        dap_string_append_printf(l_str, " - %s: %" DAP_INT64_FORMAT "\n", key, dap_json_get_int64(val));
}


/**
 * @brief s_print_for_global_db
 * Post-processing callback for global_db command. Formats JSON input into
 * human-readable string output.
 *
 * @param a_json_input Input JSON from command handler
 * @param a_json_output Output JSON array to write formatted result
 * @param a_cmd_param Command parameters array
 * @param a_cmd_cnt Count of command parameters
 * @return 0 on success (result written to a_json_output), non-zero to use original input
 */
static int s_print_for_global_db(dap_json_t *a_json_input, dap_json_t *a_json_output, char **a_cmd_param, int a_cmd_cnt)
{
    dap_return_val_if_pass(!a_json_input || !a_json_output, -1);
    // If no -h flag, return raw JSON (don't process)
    if (dap_cli_server_cmd_check_option(a_cmd_param, 0, a_cmd_cnt, "-h") == -1) {
        return -1; // Use original JSON
    }
    dap_string_t *l_str = dap_string_new("");

    // group_list: format as table
    if (dap_cli_server_cmd_check_option(a_cmd_param, 0, a_cmd_cnt, "group_list") != -1) {
        if (dap_json_get_type(a_json_input) == DAP_JSON_TYPE_ARRAY) {
            int len = dap_json_array_length(a_json_input);
            if (len <= 0) {
                dap_string_append(l_str, "Response array is empty\n");
                goto finalize;
            }
            dap_json_t *obj = dap_json_array_get_idx(a_json_input, 0);
            dap_json_t *arr = NULL, *total = NULL;
            if (obj && dap_json_get_type(obj) == DAP_JSON_TYPE_OBJECT) {
                dap_json_object_get_ex(obj, "group_list", &arr);
                if (!arr) dap_json_object_get_ex(obj, "group list", &arr);
                dap_json_object_get_ex(obj, "total_count", &total);
                if (!total) dap_json_object_get_ex(obj, "total count", &total);

                if (arr) {
                    int64_t groups_total = 0;
                    if (total)
                        groups_total = dap_json_get_int64(total);
                    else if (dap_json_get_type(arr) == DAP_JSON_TYPE_ARRAY)
                        groups_total = (int64_t)dap_json_array_length(arr);
                    else if (dap_json_get_type(arr) == DAP_JSON_TYPE_OBJECT) {
                        int count = 0;
                        dap_json_object_foreach(arr, s_count_keys_callback, &count);
                        groups_total = (int64_t)count;
                    }
                    dap_string_append_printf(l_str, "Groups (total: %" DAP_INT64_FORMAT "):\n", groups_total);

                    if (dap_json_get_type(arr) == DAP_JSON_TYPE_ARRAY) {
                        for (size_t i = 0; i < (size_t)dap_json_array_length(arr); i++) {
                            dap_json_t *it = dap_json_array_get_idx(arr, (int)i);
                            if (it && dap_json_get_type(it) == DAP_JSON_TYPE_OBJECT) {
                                dap_json_object_foreach(it, s_print_group_callback, l_str);
                            }
                        }
                        goto finalize;
                    } else if (dap_json_get_type(arr) == DAP_JSON_TYPE_OBJECT) {
                        dap_json_object_foreach(arr, s_print_group_callback, l_str);
                        goto finalize;
                    }
                }
            }
        }
        // Fallback - return original JSON
        dap_string_free(l_str, true);
        return -1;
    }
    // get_keys: format keys list
    if (dap_cli_server_cmd_check_option(a_cmd_param, 0, a_cmd_cnt, "get_keys") != -1) {
        if (dap_json_get_type(a_json_input) == DAP_JSON_TYPE_ARRAY) {
            dap_json_t *obj = dap_json_array_get_idx(a_json_input, 0);
            dap_json_t *group = NULL, *keys = NULL;
            if (obj && dap_json_get_type(obj) == DAP_JSON_TYPE_OBJECT) {
                dap_json_object_get_ex(obj, "group_name", &group);
                if (!group) dap_json_object_get_ex(obj, "group name", &group);
                dap_json_object_get_ex(obj, "keys_list", &keys);
                if (!keys) dap_json_object_get_ex(obj, "keys list", &keys);
                if (keys && dap_json_get_type(keys) == DAP_JSON_TYPE_ARRAY) {
                    dap_string_append_printf(l_str, "Keys in group %s:\n", 
                                             group ? dap_json_get_string(group) : "<unknown>");
                    for (size_t i = 0; i < (size_t)dap_json_array_length(keys); i++) {
                        dap_json_t *it = dap_json_array_get_idx(keys, (int)i);
                        dap_json_t *k = NULL, *ts = NULL, *type = NULL;
                        if (it && dap_json_get_type(it) == DAP_JSON_TYPE_OBJECT) {
                            dap_json_object_get_ex(it, "key", &k);
                            dap_json_object_get_ex(it, "time", &ts);
                            dap_json_object_get_ex(it, "type", &type);
                            dap_string_append_printf(l_str, " - %s (%s) [%s]\n",
                                   k ? dap_json_get_string(k) : "<no key>",
                                   ts ? dap_json_get_string(ts) : "-",
                                   type ? dap_json_get_string(type) : "-");
                        }
                    }
                    goto finalize;
                }
            }
        }
        dap_string_free(l_str, true);
        return -1;
    }
    
    // clusters: display list of global_db clusters
    if (dap_cli_server_cmd_check_option(a_cmd_param, 0, a_cmd_cnt, "clusters") != -1) {
        if (dap_json_get_type(a_json_input) != DAP_JSON_TYPE_ARRAY) {
            dap_string_free(l_str, true);
            return -1;
        }
        dap_json_t *obj = dap_json_array_get_idx(a_json_input, 0);
        if (!obj || dap_json_get_type(obj) != DAP_JSON_TYPE_OBJECT) {
            dap_string_append(l_str, "Response format error\n");
            goto finalize;
        }
        
        dap_json_t *clusters_arr = NULL, *total = NULL;
        dap_json_object_get_ex(obj, "clusters", &clusters_arr);
        dap_json_object_get_ex(obj, "total_count", &total);
        
        if (!clusters_arr || dap_json_get_type(clusters_arr) != DAP_JSON_TYPE_ARRAY) {
            dap_string_append(l_str, "No clusters found\n");
            goto finalize;
        }
        bool l_verbose = dap_cli_server_cmd_check_option(a_cmd_param, 0, a_cmd_cnt, "-verbose") != -1;
        int clusters_count = dap_json_array_length(clusters_arr);
        dap_string_append(l_str, "\n=== GlobalDB clusters ===\n");
        if (total)
        dap_string_append_printf(l_str, "Total clusters: %"DAP_INT64_FORMAT"\n\n", dap_json_get_int64(total));
        
        for (int i = 0; i < clusters_count; i++) {
            dap_json_t *cluster = dap_json_array_get_idx(clusters_arr, i);
            if (!cluster || dap_json_get_type(cluster) != DAP_JSON_TYPE_OBJECT)
                continue;
            
            dap_json_t *j_mask = NULL, *j_guuid = NULL, *j_mnem = NULL;
            dap_json_t *j_ttl = NULL, *j_role = NULL, *j_root = NULL, *j_links = NULL;
            dap_json_t *j_role_members = NULL, *j_role_members_count = NULL;
            
            dap_json_object_get_ex(cluster, "groups_mask", &j_mask);
            dap_json_object_get_ex(cluster, "links_cluster_guuid", &j_guuid);
            dap_json_object_get_ex(cluster, "mnemonim", &j_mnem);
            dap_json_object_get_ex(cluster, "ttl", &j_ttl);
            dap_json_object_get_ex(cluster, "default_role", &j_role);
            dap_json_object_get_ex(cluster, "owner_root_access", &j_root);
            dap_json_object_get_ex(cluster, "links", &j_links);
            dap_json_object_get_ex(cluster, "role_members", &j_role_members);
            dap_json_object_get_ex(cluster, "role_members_count", &j_role_members_count);
            dap_string_append_printf(l_str, "--- Cluster #%d ---\n", i + 1);
            dap_string_append_printf(l_str, "  Groups mask:       %s\n", j_mask ? dap_json_get_string(j_mask) : "N/A");
            dap_string_append_printf(l_str, "  Mnemonim:          %s\n", j_mnem ? dap_json_get_string(j_mnem) : "N/A");
            dap_string_append_printf(l_str, "  Links cluster GUUID: %s\n", j_guuid ? dap_json_get_string(j_guuid) : "N/A");
            dap_string_append_printf(l_str, "  TTL:               %"DAP_UINT64_FORMAT_U" sec\n", j_ttl ? dap_json_get_uint64(j_ttl) : 0);
            dap_string_append_printf(l_str, "  Default role:      %s\n", j_role ? dap_json_get_string(j_role) : "N/A");
            dap_string_append_printf(l_str, "  Owner root access: %s\n", j_root ? (dap_json_get_bool(j_root) ? "Yes" : "No") : "N/A");
            
            // Print role members in verbose mode
            if (l_verbose && j_role_members && dap_json_get_type(j_role_members) == DAP_JSON_TYPE_ARRAY) {
                uint64_t l_members_count = j_role_members_count ? dap_json_get_uint64(j_role_members_count) : 
                                   dap_json_array_length(j_role_members);
                dap_string_append_printf(l_str, "\n  Role members (total: %" DAP_UINT64_FORMAT_U "):\n", l_members_count);
                if (l_members_count > 0) {
                    dap_string_append_printf(l_str, "    %-22s | %-10s\n", "Node address", "Role");
                    dap_string_append(l_str, "    --------------------------------------\n");
                    
                    for (size_t j = 0; j < dap_json_array_length(j_role_members); j++) {
                        dap_json_t *member = dap_json_array_get_idx(j_role_members, j);
                        if (!member || dap_json_get_type(member) != DAP_JSON_TYPE_OBJECT)
                            continue;

                        dap_json_t *j_addr = NULL, *j_member_role = NULL;
                        dap_json_object_get_ex(member, "node_addr", &j_addr);
                        dap_json_object_get_ex(member, "role", &j_member_role);
                        dap_string_append_printf(l_str, "    %-22s | %-10s\n", 
                            j_addr ? dap_json_get_string(j_addr) : "N/A",
                            j_member_role ? dap_json_get_string(j_member_role) : "N/A");
                    }
                } else {
                    dap_string_append(l_str, "    No members\n");
                }
            }
                        
            // Print links in verbose mode
            if (l_verbose && j_links) {
                dap_string_append(l_str, "\n  Links information:\n");
                char *l_links_str = dap_json_to_string(j_links);
                if (l_links_str) {
                    dap_string_append(l_str, l_links_str);
                    dap_string_append(l_str, "\n");
                    DAP_DELETE(l_links_str);
                }
            }
            dap_string_append(l_str, "\n");
        }
        goto finalize;
    }
    
    // No specific handler matched - use original JSON
    dap_string_free(l_str, true);
    return -1;

finalize:
    // Create output JSON with formatted string
    {
        dap_json_t *l_json_result = dap_json_object_new();
        dap_json_object_add_string(l_json_result, "output", l_str->str);
        dap_json_array_add(a_json_output, l_json_result);
        dap_string_free(l_str, true);
    }
    return 0;
}
                                                                                    
