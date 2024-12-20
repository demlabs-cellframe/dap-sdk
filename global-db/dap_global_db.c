/*
 * Authors:
 * Dmitriy A. Gearasimov <gerasimov.dmitriy@demlabs.net>
 * Demlabs Ltd.   https://demlabs.net
 * Copyright  (c) 2022
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
#include <string.h>
#include "dap_common.h"
#include "dap_config.h"
#include "dap_strfuncs.h"
#include "dap_file_utils.h"
#include "dap_time.h"
#include "dap_context.h"
#include "dap_worker.h"
#include "dap_cert.h"
#include "dap_enc_ks.h"
#include "dap_proc_thread.h"
#include "dap_global_db.h"
#include "dap_global_db_driver.h"
#include "dap_global_db_cluster.h"
#include "dap_global_db_pkt.h"

#define LOG_TAG "dap_global_db"

int g_dap_global_db_debug_more = false;                                         /* Enable extensible debug output */

// Queue I/O message op code
enum queue_io_msg_opcode {
    MSG_OPCODE_UNDEFINED = 0,
    MSG_OPCODE_GET,
    MSG_OPCODE_GET_RAW,
    MSG_OPCODE_GET_DEL_TS,
    MSG_OPCODE_GET_LAST,
    MSG_OPCODE_GET_LAST_RAW,
    MSG_OPCODE_GET_ALL,
    MSG_OPCODE_GET_ALL_RAW,
    MSG_OPCODE_SET,
    MSG_OPCODE_SET_RAW,
    MSG_OPCODE_SET_MULTIPLE,
    MSG_OPCODE_PIN,
    MSG_OPCODE_DELETE,
    MSG_OPCODE_FLUSH
};

// Queue i/o message
struct queue_io_msg{
    enum queue_io_msg_opcode opcode; // Opcode

    // For each message opcode we have only one callback
    union{
        dap_global_db_callback_t             callback;
        dap_global_db_callback_result_t      callback_result;
        dap_global_db_callback_result_raw_t  callback_result_raw;
        dap_global_db_callback_results_t     callback_results;
        dap_global_db_callback_results_raw_t callback_results_raw;
    };
    // Custom argument passed to the callback
    void *callback_arg;
    union {
        struct { // Get all request
            dap_global_db_driver_hash_t last_hash;
            uint64_t values_page_size;
            uint64_t total_records;
            uint64_t processed_records;
        };
        struct { // Raw set request
            dap_store_obj_t *values_raw;
            uint64_t values_raw_total;
        };
        struct { // Set multiply zero-copy
            dap_global_db_obj_t *values;
            uint64_t values_count;
        };
        struct { // Value for singe request
            void *value;
            size_t value_length;
            bool value_is_pinned;
            char *group;  // Group
            char *key; // Key
        };
    };
    dap_global_db_instance_t *dbi;
};

static pthread_cond_t s_check_db_cond = PTHREAD_COND_INITIALIZER; // Check version condition
static pthread_mutex_t s_check_db_mutex = PTHREAD_MUTEX_INITIALIZER; // Check version condition mutex
#define INVALID_RETCODE +100500
static int s_check_db_ret = INVALID_RETCODE; // Check version return value
static dap_timerfd_t* s_check_pinned_db_objs_timer;
static dap_timerfd_t* s_check_gdb_clean_timer;
static dap_nanotime_t s_minimal_ttl = 1800000000000;  //def hald an hour

static dap_global_db_instance_t *s_dbi = NULL; // GlobalDB instance is only static now

// Version check& update functiosn
static int s_check_db_version();
static void s_check_db_version_callback_get (dap_global_db_instance_t *a_dbi, int a_errno, const char * a_group, const char * a_key,
                                             const void * a_value, const size_t a_value_len,
                                             dap_nanotime_t value_ts,bool a_is_pinned, void * a_arg);
static void s_check_db_version_callback_set (dap_global_db_instance_t *a_dbi, int a_errno, const char * a_group, const char * a_key,
                                             const void * a_value, const size_t a_value_len,
                                             dap_nanotime_t value_ts,bool a_is_pinned, void * a_arg);
// GlobalDB context start/stop callbacks
static void s_context_callback_started( dap_context_t * a_context, void *a_arg);
static int s_context_callback_stopped( dap_context_t * a_context, void *a_arg);

static int s_gdb_clean_init();
static void s_gdb_clean_deinit();
static void s_check_pinned_db_objs_deinit();

static int s_pinned_objs_group_init();
static int s_add_pinned_obj_in_pinned_group(dap_store_obj_t * a_objs);
static void s_del_pinned_obj_from_pinned_group_by_source_group(dap_store_obj_t * a_objs);
DAP_STATIC_INLINE char *dap_get_local_pinned_groups_mask(const char *a_group);
DAP_STATIC_INLINE char *dap_get_group_from_pinned_groups_mask(const char *a_group);

// Opcode to string
static const char *s_msg_opcode_to_str(enum queue_io_msg_opcode a_opcode);

// Queue i/o processing callback
static bool s_queue_io_callback(void *a_arg);

// Queue i/o message processing functions
static void s_msg_opcode_get(struct queue_io_msg * a_msg);
static void s_msg_opcode_get_raw(struct queue_io_msg * a_msg);
static void s_msg_opcode_get_del_ts(struct queue_io_msg * a_msg);
static void s_msg_opcode_get_last(struct queue_io_msg * a_msg);
static void s_msg_opcode_get_last_raw(struct queue_io_msg * a_msg);
static bool s_msg_opcode_get_all(struct queue_io_msg * a_msg);
static bool s_msg_opcode_get_all_raw(struct queue_io_msg * a_msg);
static void s_msg_opcode_set(struct queue_io_msg * a_msg);
static void s_msg_opcode_set_raw(struct queue_io_msg * a_msg);
static void s_msg_opcode_set_multiple_zc(struct queue_io_msg * a_msg);
static void s_msg_opcode_pin(struct queue_io_msg * a_msg);
static void s_msg_opcode_delete(struct queue_io_msg * a_msg);
static void s_msg_opcode_flush(struct queue_io_msg * a_msg);

// Free memor for queue i/o message
static void s_queue_io_msg_delete( struct queue_io_msg * a_msg);

// convert dap_store_obj_t to dap_global_db_obj_t
static dap_global_db_obj_t* s_objs_from_store_objs(dap_store_obj_t *a_store_objs, size_t a_values_count);

/**
 * @brief dap_global_db_init
 * @param a_path
 * @param a_driver
 * @return
 */
int dap_global_db_init()
{
    int l_rc = 0;

    // Debug config
    g_dap_global_db_debug_more = dap_config_get_item_bool_default(g_config, "global_db", "debug_more", false);

    // Create and run its own context
    if (s_dbi == NULL) {
        s_dbi = DAP_NEW_Z(dap_global_db_instance_t);
        if (!s_dbi) {
            log_it(L_CRITICAL, "%s", c_error_memory_alloc);
            l_rc = -5;
            goto lb_return;
        }

        char *l_gdb_path_cfg = dap_config_get_item_str_path_default(g_config, "global_db", "path", NULL);
        s_dbi->storage_path = l_gdb_path_cfg ? l_gdb_path_cfg : dap_strdup_printf("%s/var/lib/global_db", g_sys_dir_path);
        const char *l_driver_name = dap_config_get_item_str(g_config, "global_db", "driver");
        s_dbi->driver_name = dap_strdup(l_driver_name ? l_driver_name : "mdbx");
        
        dap_cert_t *l_signing_cert = dap_cert_find_by_name(DAP_STREAM_NODE_ADDR_CERT_NAME);
        if (l_signing_cert)
            s_dbi->signing_key = l_signing_cert->enc_key;
        else
            log_it(L_ERROR, "Can't find node addr cerificate, all new records will be usigned");

        uint16_t l_size_ban_list = 0, l_size_white_list = 0;
        const char **l_ban_list = dap_config_get_array_str(g_config, "global_db", "ban_list_sync_groups", &l_size_ban_list);
        for (int i = 0; i < l_size_ban_list; i++)
            s_dbi->blacklist = dap_list_append(s_dbi->blacklist, dap_strdup(l_ban_list[i]));
        const char **l_white_list = dap_config_get_array_str(g_config, "global_db", "white_list_sync_groups", &l_size_white_list);
        for (int i = 0; i < l_size_white_list; i++)
            s_dbi->whitelist = dap_list_append(s_dbi->whitelist, dap_strdup(l_white_list[i]));
        // One week for objects lifetime by default
        s_dbi->store_time_limit = dap_config_get_item_uint64(g_config, "global_db", "ttl");
        // Time between sync attempts, in seconds
        s_dbi->sync_idle_time = dap_config_get_item_uint32_default(g_config, "global_db", "sync_idle_time", 30);
    }

    // Driver initalization
    if ( (l_rc = dap_global_db_driver_init(s_dbi->driver_name, s_dbi->storage_path)) )
        return log_it(L_CRITICAL, "Hadn't initialized DB driver \"%s\" on path \"%s\", code: %d",
                       s_dbi->driver_name, s_dbi->storage_path, l_rc), l_rc;

    // Clusters initialization
    if ( (l_rc = dap_global_db_cluster_init()) )
        return log_it(L_CRITICAL, "Can't initialize GlobalDB clusters"), l_rc;
    // Check version and update if need it
    if ( (l_rc = s_check_db_version()) )
        return log_it(L_ERROR, "GlobalDB version changed, please export or remove old version!"), l_rc;
    

lb_return:
    if (l_rc == 0 )
        log_it(L_NOTICE, "GlobalDB initialized");
    else
        log_it(L_CRITICAL, "GlobalDB wasn't initialized, code %d", l_rc);

    return l_rc;
}

int dap_global_db_clean_init() {
    int l_rc = 0;
    if ( (l_rc = s_pinned_objs_group_init()))
        return log_it(L_ERROR, "GlobalDB pinned objs init failed"), l_rc;

    if ( (l_rc = s_gdb_clean_init()))
        return log_it(L_ERROR, "GlobalDB clean init failed"), l_rc;

    return l_rc;
}

int dap_global_db_clean_deinit() {
    s_check_pinned_db_objs_deinit();
    s_gdb_clean_deinit();
}

/**
 * @brief kill context thread and clean context
 */
void dap_global_db_instance_deinit()
{
    dap_return_if_fail(s_dbi)
    dap_list_free_full(s_dbi->blacklist, NULL);
    dap_list_free_full(s_dbi->whitelist, NULL);
    DAP_DEL_MULTY(s_dbi->driver_name, s_dbi->storage_path, s_dbi);
}

inline dap_global_db_instance_t *dap_global_db_instance_get_default()
{
    return s_dbi;
}

/**
 * @brief dap_global_db_deinit, after fix ticket 9030 need add dap_global_db_instance_deinit()
 */
void dap_global_db_deinit() {
    dap_global_db_clean_deinit();
    dap_global_db_instance_deinit();
    dap_global_db_driver_deinit();
    dap_global_db_cluster_deinit();
}

bool dap_global_db_group_match_mask(const char *a_group, const char *a_mask)
{
    dap_return_val_if_fail(a_group && a_mask && *a_group && *a_mask, false);
    const char *l_group_tail = a_group + strlen(a_group);           // Pointer to trailng zero
    const char *l_mask_tail = a_mask + strlen(a_mask);
    const char *l_group_it = a_group, *l_mask_it = a_mask;
    const char *l_wildcard = strchr(a_mask, '*');
    while (l_mask_it < (l_wildcard ? l_wildcard : l_mask_tail) &&
                l_group_it < l_group_tail)
        if (*l_group_it++ != *l_mask_it++)
            return false;
    if (l_mask_it == l_wildcard && ++l_mask_it < l_mask_tail)
        return strstr(l_group_it, l_mask_it);
    return true;
}

static int s_store_obj_apply(dap_global_db_instance_t *a_dbi, dap_store_obj_t *a_obj)
{
    dap_global_db_cluster_t *l_cluster = dap_global_db_cluster_by_group(a_dbi, a_obj->group);
    if (!l_cluster) {
        log_it(L_WARNING, "An entry in the group %s was rejected because the group name doesn't match any cluster", a_obj->group);
        return -11;
    }
    dap_global_db_driver_hash_t a_obj_drv_hash = dap_global_db_driver_hash_get(a_obj);
    if (dap_global_db_driver_is_hash(a_obj->group, a_obj_drv_hash)) {
        debug_if(g_dap_global_db_debug_more, L_NOTICE, "Rejected duplicate object with group %s and key %s",
                                            a_obj->group, a_obj->key);
        return -12;
    }
    // Check time
    dap_nanotime_t l_ttl = dap_nanotime_from_sec(l_cluster->ttl),
                   l_now = dap_nanotime_now();
    if ( a_obj->timestamp > l_now ) {
        if (g_dap_global_db_debug_more) {
            char l_ts_str[DAP_TIME_STR_SIZE];
            dap_time_to_str_rfc822(l_ts_str, sizeof(l_ts_str), dap_nanotime_to_sec(a_obj->timestamp));
            log_it(L_NOTICE, "Rejected record \"%s : %s\" from future ts %s",
                             a_obj->group, a_obj->key, l_ts_str);
        }
        return -13;
    }
    if ( l_ttl && a_obj->timestamp + l_ttl < l_now ) {
        if (g_dap_global_db_debug_more) {
            char l_ts_str[DAP_TIME_STR_SIZE];
            dap_time_to_str_rfc822(l_ts_str, sizeof(l_ts_str), dap_nanotime_to_sec(a_obj->timestamp));
            log_it(L_NOTICE, "Rejected too old record \"%s : %s\" ts %s",
                             a_obj->group, a_obj->key, l_ts_str);
        }
        return -13;
    }

    dap_global_db_role_t l_signer_role = DAP_GDB_MEMBER_ROLE_INVALID;
    if (a_obj->sign) {
        dap_stream_node_addr_t l_signer_addr = dap_stream_node_addr_from_sign(a_obj->sign);
        l_signer_role = dap_cluster_member_find_role(l_cluster->role_cluster, &l_signer_addr);
    }
    if (l_signer_role == DAP_GDB_MEMBER_ROLE_INVALID)
        l_signer_role = l_cluster->default_role;
    if (l_signer_role < DAP_GDB_MEMBER_ROLE_USER) {
        debug_if(g_dap_global_db_debug_more, L_WARNING, "Global DB record with group %s and key %s is rejected "
                                                        "with signer role %s with no write access to cluster",
                                                            a_obj->group, a_obj->key,
                                                            dap_global_db_cluster_role_str(l_signer_role));
        return -14;
    }

    dap_global_db_role_t l_required_role = DAP_GDB_MEMBER_ROLE_USER;
    dap_global_db_optype_t l_obj_type = dap_store_obj_get_type(a_obj);
    dap_store_obj_t *l_read_obj = NULL;
    bool l_existed_obj_pinned = false;
    int l_ret = 0;
    if (dap_global_db_driver_is(a_obj->group, a_obj->key)) {
        l_read_obj = dap_global_db_driver_read(a_obj->group, a_obj->key, NULL, true);
        if (l_read_obj) { // Need to rewrite existed value
            l_required_role = DAP_GDB_MEMBER_ROLE_ROOT;
            if (l_read_obj->flags & DAP_GLOBAL_DB_RECORD_PINNED)
                l_existed_obj_pinned = true;
        } else {
            log_it(L_ERROR, "Existed object with group %s and key %s is broken and will be erased",
                                                        a_obj->group, a_obj->key);
            dap_store_obj_t l_to_delete = (dap_store_obj_t) { .group = a_obj->group, .key = a_obj->key };
            dap_global_db_driver_delete(&l_to_delete, 1);
        }
    }
    if (l_read_obj && l_cluster->owner_root_access &&
            a_obj->sign && (!l_read_obj->sign ||
            dap_sign_compare_pkeys(a_obj->sign, l_read_obj->sign)))
        l_signer_role = DAP_GDB_MEMBER_ROLE_ROOT;
    if (l_signer_role < l_required_role) {
        debug_if(g_dap_global_db_debug_more, L_WARNING, "Global DB record with group %s and key %s is rejected "
                                                        "with signer role %s and required role %s",
                                                            a_obj->group, a_obj->key,
                                                            dap_global_db_cluster_role_str(l_signer_role),
                                                            dap_global_db_cluster_role_str(l_required_role));
        l_ret = -16;
        goto free_n_exit;
    }
    switch (dap_store_obj_driver_hash_compare(l_read_obj, a_obj)) {
    case 1:         // Received object is older
        if (a_obj->key && (a_obj->flags & DAP_GLOBAL_DB_RECORD_NEW)) {
            dap_nanotime_t l_time_diff = l_read_obj->timestamp - a_obj->timestamp;
            a_obj->timestamp = l_read_obj->timestamp + 1;
            DAP_DEL_Z(a_obj->sign);
            a_obj->crc = 0;
            a_obj->sign = dap_store_obj_sign(a_obj, a_dbi->signing_key, &a_obj->crc);
            debug_if(g_dap_global_db_debug_more, L_WARNING, "DB record with group %s and key %s need time correction for %"DAP_UINT64_FORMAT_U" seconds to be properly applied",
                                                            a_obj->group, a_obj->key, dap_nanotime_to_sec(l_time_diff));
            if (!a_obj->sign) {
                log_it(L_ERROR, "Can't sign object with group %s and key %s", a_obj->group, a_obj->key);
                return -20;
            }
        } else {
            debug_if(g_dap_global_db_debug_more, L_DEBUG, "DB record with group %s and key %s is not applied. It's older than existed record with same key",
                                                            a_obj->group, a_obj->key);
            l_ret = -18;
        }
        break;
    case 0:         // Objects the same, omg! Use the basic object
        debug_if(g_dap_global_db_debug_more, L_WARNING, "Duplicate record with group %s and key %s not dropped by hash filter",
                                                                    a_obj->group, a_obj->key);
        l_ret = -17;
        break;
    case -1:        // Existed obj is older
        debug_if(g_dap_global_db_debug_more, L_INFO, "Applied new global DB record with type '%c' and group %s and key %s",
                                                                                        l_obj_type, a_obj->group, a_obj->key);
        break;
    default:
        log_it(L_ERROR, "Unexpected comparision result");
        l_ret = -19;
        break;
    }
    if (!l_ret) {
        // Only the condition to apply new object
        l_ret = dap_global_db_driver_apply(a_obj, 1);

        // if global_db obj is pinned
        if (a_obj->flags & DAP_GLOBAL_DB_RECORD_PINNED) {
            s_add_pinned_obj_in_pinned_group(a_obj);
        // if upin obj
        } else if (l_existed_obj_pinned && !(a_obj->flags & DAP_GLOBAL_DB_RECORD_PINNED)) {
            s_del_pinned_obj_from_pinned_group_by_source_group(a_obj);
        }

        if (l_obj_type != DAP_GLOBAL_DB_OPTYPE_DEL || l_read_obj) {
            // Do not notify for delete if deleted record not exists
            if (a_obj->flags & DAP_GLOBAL_DB_RECORD_NEW)
                // Notify sync cluster first
                dap_global_db_cluster_broadcast(l_cluster, a_obj);
            if (l_cluster->notifiers)
                // Notify others
                dap_global_db_cluster_notify(l_cluster, a_obj);
        }
    }
free_n_exit:
    if (l_read_obj)
        dap_store_obj_free_one(l_read_obj);
    return l_ret;
}

/* *** Get functions group *** */

byte_t *dap_global_db_get_sync(const char *a_group,
                                 const char *a_key, size_t *a_data_size,
                                 bool *a_is_pinned, dap_nanotime_t *a_ts)
{
    dap_return_val_if_fail(s_dbi && a_group && a_key, NULL);
    debug_if(g_dap_global_db_debug_more, L_DEBUG, "get call executes for group \"%s\" and key \"%s\"", a_group, a_key);
    size_t l_count_records = 0;
    dap_store_obj_t *l_store_obj = dap_global_db_driver_read(a_group, a_key, &l_count_records, false);
    if (l_count_records > 1)
        log_it(L_ERROR, "Get more than one global DB object by one key is unexpected");
    if (!l_store_obj)
        return NULL;
    if (a_data_size)
        *a_data_size = l_store_obj->value_len;
    if (a_is_pinned)
        *a_is_pinned = l_store_obj->flags & DAP_GLOBAL_DB_RECORD_PINNED;
    if (a_ts)
        *a_ts = l_store_obj->timestamp;
    byte_t *l_res = l_store_obj->value;
    l_store_obj->value = NULL;
    dap_store_obj_free_one(l_store_obj);
    return l_res;
}

/**
 * @brief dap_global_db_get
 * @details Get record value from GlobalDB group by key
 * @param a_group
 * @param a_key
 * @param a_callback
 * @param a_arg
 * @return
 */
int dap_global_db_get(const char * a_group, const char *a_key, dap_global_db_callback_result_t a_callback, void *a_arg)
{
    dap_return_val_if_fail(s_dbi && a_group && a_key, DAP_GLOBAL_DB_RC_ERROR);
    struct queue_io_msg *l_msg = DAP_NEW_Z_RET_VAL_IF_FAIL(struct queue_io_msg, DAP_GLOBAL_DB_RC_CRITICAL);
    l_msg->dbi = s_dbi;
    l_msg->opcode = MSG_OPCODE_GET;
    l_msg->group = dap_strdup(a_group);
    if (!l_msg->group) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        DAP_DELETE(l_msg);
        return DAP_GLOBAL_DB_RC_CRITICAL;
    }
    l_msg->key = dap_strdup(a_key);
    if (!l_msg->key) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        DAP_DEL_MULTY(l_msg->group, l_msg);
        return DAP_GLOBAL_DB_RC_CRITICAL;
    }
    l_msg->callback_result = a_callback;
    l_msg->callback_arg = a_arg;

    int l_ret = dap_proc_thread_callback_add(NULL, s_queue_io_callback, l_msg);
    if (l_ret != 0) {
        log_it(L_ERROR, "Can't exec get request, code %d", l_ret);
        s_queue_io_msg_delete(l_msg);
        l_ret = DAP_GLOBAL_DB_RC_ERROR;
    } else
        debug_if(g_dap_global_db_debug_more, L_DEBUG, "Have sent get request for %s:%s", a_group, a_key);
    return l_ret;
}

/**
 * @brief s_msg_opcode_get
 * @param a_msg
 * @return
 */
static void s_msg_opcode_get(struct queue_io_msg * a_msg)
{
    size_t l_value_len = 0;
    bool l_pinned = false;
    dap_nanotime_t l_ts = 0;
    byte_t *l_value = dap_global_db_get_sync(a_msg->group, a_msg->key, &l_value_len, &l_pinned, &l_ts);
    if (l_value && l_value_len) {
        if(a_msg->callback_result)
            a_msg->callback_result(a_msg->dbi, DAP_GLOBAL_DB_RC_SUCCESS, a_msg->group, a_msg->key,
                               l_value, l_value_len, l_ts,
                               l_pinned, a_msg->callback_arg);
        DAP_DELETE(l_value);
    } else if(a_msg->callback_result)
        a_msg->callback_result(a_msg->dbi, DAP_GLOBAL_DB_RC_NO_RESULTS, a_msg->group, a_msg->key,
                               NULL, 0, 0,0, a_msg->callback_arg);
}

/* *** Get raw functions group *** */

dap_store_obj_t *dap_global_db_get_raw_sync(const char *a_group, const char *a_key)
{
    dap_return_val_if_fail(s_dbi && a_group && a_key, NULL);
    size_t l_count_records = 0;
    dap_store_obj_t *l_res = dap_global_db_driver_read(a_group, a_key, &l_count_records, true);
    if (l_count_records > 1)
        log_it(L_WARNING, "Get more than one global DB object by one key is unexpected");
    return l_res;
}

/**
 * @brief dap_global_db_get_raw
 * @param a_group
 * @param a_key
 * @param a_callback
 * @param a_arg
 * @return
 */
int dap_global_db_get_raw(const char *a_group, const char *a_key, dap_global_db_callback_result_raw_t a_callback, void *a_arg)
{
    dap_return_val_if_fail(s_dbi && a_group && a_key, DAP_GLOBAL_DB_RC_ERROR);
    struct queue_io_msg *l_msg = DAP_NEW_Z_RET_VAL_IF_FAIL(struct queue_io_msg, DAP_GLOBAL_DB_RC_CRITICAL);
    l_msg->dbi = s_dbi;
    l_msg->opcode = MSG_OPCODE_GET_RAW;
    l_msg->group = dap_strdup(a_group);
    if (!l_msg->group) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        DAP_DELETE(l_msg);
        return DAP_GLOBAL_DB_RC_CRITICAL;
    }
    l_msg->key = dap_strdup(a_key);
    if (!l_msg->key) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        DAP_DEL_MULTY(l_msg->group, l_msg);
        return DAP_GLOBAL_DB_RC_CRITICAL;
    }
    l_msg->callback_result_raw = a_callback;
    l_msg->callback_arg = a_arg;

    int l_ret = dap_proc_thread_callback_add(NULL, s_queue_io_callback, l_msg);
    if (l_ret != 0) {
        log_it(L_ERROR, "Can't exec get request, code %d", l_ret);
        s_queue_io_msg_delete(l_msg);
        l_ret = DAP_GLOBAL_DB_RC_ERROR;
    } else
        debug_if(g_dap_global_db_debug_more, L_DEBUG, "Have sent get_raw request for %s:%s", a_group, a_key);
    return l_ret;
}

/**
 * @brief s_msg_opcode_get_raw
 * @param a_msg
 * @return
 */
static void s_msg_opcode_get_raw(struct queue_io_msg * a_msg)
{
    dap_store_obj_t *l_store_obj = dap_global_db_get_raw_sync(a_msg->group, a_msg->key);

    if(a_msg->callback_result_raw)
        a_msg->callback_result_raw(a_msg->dbi, l_store_obj ? DAP_GLOBAL_DB_RC_SUCCESS:
                                                                      DAP_GLOBAL_DB_RC_NO_RESULTS,
                                                        l_store_obj, a_msg->callback_arg );
    dap_store_obj_free_one(l_store_obj);
}

/* *** Get_del_ts functions group *** */

dap_nanotime_t dap_global_db_get_del_ts_sync(const char *a_group, const char *a_key)
{
    dap_return_val_if_fail(s_dbi && a_group && a_key, 0);
    dap_store_obj_t *l_store_obj_del = dap_global_db_get_raw_sync(a_group, a_key);
    dap_nanotime_t l_timestamp = 0;
    if (l_store_obj_del) {
        if (l_store_obj_del->flags & DAP_GLOBAL_DB_RECORD_DEL)
            l_timestamp = l_store_obj_del->timestamp;
        dap_store_obj_free_one(l_store_obj_del);
    }
    return l_timestamp;
}

/**
 * @brief dap_global_db_get_del_ts
 * @param a_group
 * @param a_key
 * @param a_callback
 * @param a_arg
 * @return
 */
int dap_global_db_get_del_ts(const char *a_group, const char *a_key, dap_global_db_callback_result_t a_callback, void *a_arg)
{
    dap_return_val_if_fail(s_dbi && a_group && a_key, DAP_GLOBAL_DB_RC_ERROR);
    struct queue_io_msg *l_msg = DAP_NEW_Z_RET_VAL_IF_FAIL(struct queue_io_msg, DAP_GLOBAL_DB_RC_CRITICAL);
    l_msg->dbi = s_dbi;
    l_msg->opcode = MSG_OPCODE_GET_DEL_TS;
    l_msg->group = dap_strdup(a_group);
    if (!l_msg->group) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        DAP_DELETE(l_msg);
        return DAP_GLOBAL_DB_RC_CRITICAL;
    }
    l_msg->key = dap_strdup(a_key);
    if (!l_msg->key) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        DAP_DEL_MULTY(l_msg->group, l_msg);
        return DAP_GLOBAL_DB_RC_CRITICAL;
    }
    l_msg->callback_result = a_callback;
    l_msg->callback_arg = a_arg;

    int l_ret = dap_proc_thread_callback_add(NULL, s_queue_io_callback, l_msg);
    if (l_ret != 0) {
        log_it(L_ERROR, "Can't exec get_del_ts request, code %d", l_ret);
        s_queue_io_msg_delete(l_msg);
        l_ret = DAP_GLOBAL_DB_RC_ERROR;
    } else
        debug_if(g_dap_global_db_debug_more, L_DEBUG, "Have sent get_del_ts request for \"%s\" group \"%s\" key", a_group, a_key);
    return l_ret;
}

/**
 * @brief s_msg_opcode_get_del_ts
 * @param a_msg
 * @return
 */
static void s_msg_opcode_get_del_ts(struct queue_io_msg * a_msg)
{
    dap_nanotime_t l_timestamp = dap_global_db_get_del_ts_sync(a_msg->group, a_msg->key);
    if(l_timestamp) {
        if(a_msg->callback_result)
            a_msg->callback_result(a_msg->dbi, DAP_GLOBAL_DB_RC_SUCCESS, a_msg->group, a_msg->key,
                               NULL, 0, l_timestamp,
                               false, a_msg->callback_arg );
    } else if(a_msg->callback_result)
        a_msg->callback_result(a_msg->dbi, DAP_GLOBAL_DB_RC_NO_RESULTS, a_msg->group, a_msg->key,
                               NULL, 0, 0,0, a_msg->callback_arg );
}

/* *** Get_last functions group *** */

byte_t *dap_global_db_get_last_sync(const char *a_group, char **a_key, size_t *a_data_size,
                                      bool *a_is_pinned, dap_nanotime_t *a_ts)
{
    dap_return_val_if_fail(s_dbi && a_group, NULL);
    dap_store_obj_t *l_store_obj = dap_global_db_driver_read_last(a_group, false);
    if (!l_store_obj)
        return NULL;

    if (a_key) {
        *a_key = dap_strdup(l_store_obj->key);
        if (!*a_key) {
            log_it(L_CRITICAL, "%s", c_error_memory_alloc);
            dap_store_obj_free_one(l_store_obj);
            return NULL;
        }
    }
    if (a_data_size)
        *a_data_size = l_store_obj->value_len;
    if (a_is_pinned)
        *a_is_pinned = l_store_obj->flags & DAP_GLOBAL_DB_RECORD_PINNED;
    if (a_ts)
        *a_ts = l_store_obj->timestamp;
    byte_t *l_res = l_store_obj->value;
    l_store_obj->value = NULL;
    dap_store_obj_free_one(l_store_obj);
    return l_res;
}

/**
 * @brief dap_global_db_get_last
 * @details Get the last value in GlobalDB group
 * @param a_group
 * @param a_callback
 * @param a_arg
 * @return
 */
int dap_global_db_get_last(const char * a_group, dap_global_db_callback_result_t a_callback, void *a_arg)
{
    dap_return_val_if_fail(s_dbi && a_group, DAP_GLOBAL_DB_RC_ERROR);
    struct queue_io_msg *l_msg = DAP_NEW_Z_RET_VAL_IF_FAIL(struct queue_io_msg, DAP_GLOBAL_DB_RC_CRITICAL);
    l_msg->dbi = s_dbi;
    l_msg->opcode = MSG_OPCODE_GET_LAST;
    l_msg->group = dap_strdup(a_group);
    if (!l_msg->group) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        DAP_DELETE(l_msg);
        return DAP_GLOBAL_DB_RC_CRITICAL;
    }
    l_msg->callback_arg = a_arg;
    l_msg->callback_result = a_callback;

    int l_ret = dap_proc_thread_callback_add(NULL, s_queue_io_callback, l_msg);
    if (l_ret != 0) {
        log_it(L_ERROR, "Can't exec get_last request, code %d", l_ret);
        s_queue_io_msg_delete(l_msg);
        l_ret = DAP_GLOBAL_DB_RC_ERROR;
    } else
        debug_if(g_dap_global_db_debug_more, L_DEBUG, "Have sent get_last request for \"%s\" group", a_group);
    return l_ret;
}

/**
 * @brief s_msg_opcode_get_last
 * @param a_msg
 * @return
 */
static void s_msg_opcode_get_last(struct queue_io_msg * a_msg)
{
    size_t l_value_len = 0;
    bool l_pinned = false;
    dap_nanotime_t l_ts = 0;
    char *l_key = NULL;
    byte_t *l_value = dap_global_db_get_last_sync(a_msg->group, &l_key, &l_value_len, &l_pinned, &l_ts);
    if (l_value && l_value_len) {
        if(a_msg->callback_result)
            a_msg->callback_result(a_msg->dbi, DAP_GLOBAL_DB_RC_SUCCESS, a_msg->group, l_key,
                               l_value, l_value_len, l_ts,
                               l_pinned, a_msg->callback_arg);
        DAP_DELETE(l_value);
    } else if(a_msg->callback_result)
        a_msg->callback_result(a_msg->dbi, DAP_GLOBAL_DB_RC_NO_RESULTS, a_msg->group, l_key,
                               NULL, 0, 0,0, a_msg->callback_arg );
}

/* *** Get_last_raw functions group *** */

dap_store_obj_t *dap_global_db_get_last_raw_sync(const char *a_group)
{
    dap_return_val_if_fail(s_dbi && a_group, NULL);
    dap_store_obj_t *l_ret = dap_global_db_driver_read_last(a_group, true);
    return l_ret;
}

/**
 * @brief dap_global_db_get_last_raw
 * @param a_group
 * @param a_callback
 * @param a_arg
 * @return
 */
int dap_global_db_get_last_raw(const char * a_group, dap_global_db_callback_result_raw_t a_callback, void *a_arg)
{
    dap_return_val_if_fail(s_dbi && a_group, DAP_GLOBAL_DB_RC_ERROR);
    struct queue_io_msg *l_msg = DAP_NEW_Z_RET_VAL_IF_FAIL(struct queue_io_msg, DAP_GLOBAL_DB_RC_CRITICAL);
    l_msg->dbi = s_dbi;
    l_msg->opcode = MSG_OPCODE_GET_LAST_RAW;
    l_msg->group = dap_strdup(a_group);
    if (!l_msg->group) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        DAP_DELETE(l_msg);
        return DAP_GLOBAL_DB_RC_CRITICAL;
    }
    l_msg->callback_arg = a_arg;
    l_msg->callback_result_raw = a_callback;

    int l_ret = dap_proc_thread_callback_add(NULL, s_queue_io_callback, l_msg);
    if (l_ret != 0) {
        log_it(L_ERROR, "Can't exec get_last request, code %d", l_ret);
        s_queue_io_msg_delete(l_msg);
        l_ret = DAP_GLOBAL_DB_RC_ERROR;
    } else
        debug_if(g_dap_global_db_debug_more, L_DEBUG, "Have sent get_last request for \"%s\" group", a_group);
    return l_ret;
}

/**
 * @brief s_msg_opcode_get_last_raw
 * @param a_msg
 * @return
 */
static void s_msg_opcode_get_last_raw(struct queue_io_msg * a_msg)
{
    dap_store_obj_t *l_store_obj = dap_global_db_get_last_raw_sync(a_msg->group);
    if(a_msg->callback_result)
        a_msg->callback_result_raw(a_msg->dbi, l_store_obj ? DAP_GLOBAL_DB_RC_SUCCESS : DAP_GLOBAL_DB_RC_NO_RESULTS, l_store_obj, a_msg->callback_arg);
    dap_store_obj_free(l_store_obj, 1);
}

/* *** Get_all functions group *** */

dap_global_db_obj_t *dap_global_db_get_all_sync(const char *a_group, size_t *a_objs_count)
{
    dap_return_val_if_fail(s_dbi && a_group, NULL);
    size_t l_values_count = 0;
    dap_store_obj_t *l_store_objs = dap_global_db_driver_read(a_group, 0, &l_values_count, false);
    debug_if(g_dap_global_db_debug_more, L_DEBUG,
             "Get all request from group %s recieved %zu values", a_group, l_values_count);
    dap_global_db_obj_t *l_objs = l_store_objs ? s_objs_from_store_objs(l_store_objs, l_values_count) : NULL;
    if (a_objs_count)
        *a_objs_count = l_values_count;
    return l_objs;
}

/**
 * @brief dap_global_db_get_all Get all records from the group
 * @param a_group
 * @param a_results_page_size
 * @param a_callback
 * @param a_arg
 * @return
 */
int dap_global_db_get_all(const char *a_group, size_t a_results_page_size, dap_global_db_callback_results_t a_callback, void *a_arg)
{
    dap_return_val_if_fail(s_dbi && a_group, DAP_GLOBAL_DB_RC_ERROR);
    struct queue_io_msg *l_msg = DAP_NEW_Z_RET_VAL_IF_FAIL(struct queue_io_msg, DAP_GLOBAL_DB_RC_CRITICAL);
    l_msg->dbi = s_dbi;
    l_msg->opcode = MSG_OPCODE_GET_ALL;
    l_msg->group = dap_strdup(a_group);
    if (!l_msg->group) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        DAP_DELETE(l_msg);
        return DAP_GLOBAL_DB_RC_CRITICAL;
    }
    l_msg->callback_arg = a_arg;
    l_msg->callback_results = a_callback;
    l_msg->values_page_size = a_results_page_size;
    l_msg->last_hash = c_dap_global_db_driver_hash_blank;

    int l_ret = dap_proc_thread_callback_add(NULL, s_queue_io_callback, l_msg);

    if (l_ret != 0) {
        log_it(L_ERROR, "Can't exec get_all request, code %d", l_ret);
        s_queue_io_msg_delete(l_msg);
        l_ret = DAP_GLOBAL_DB_RC_ERROR;
    } else
        debug_if(g_dap_global_db_debug_more, L_DEBUG, "Have sent get_all request for \"%s\" group", a_group);

    return l_ret;
}

/**
 * @brief s_msg_opcode_get_all
 * @param a_msg
 * @return
 */
static bool s_msg_opcode_get_all(struct queue_io_msg * a_msg)
{  
    dap_return_val_if_pass(!a_msg, false);

    size_t l_values_count = a_msg->values_page_size;
    dap_global_db_obj_t *l_objs = NULL;
    dap_store_obj_t *l_store_objs = NULL;
    if (!a_msg->values_page_size) {
        l_objs = dap_global_db_get_all_sync(a_msg->group, &l_values_count);
        if (a_msg->callback_results)
            a_msg->callback_results(a_msg->dbi,
                                l_objs ? DAP_GLOBAL_DB_RC_SUCCESS : DAP_GLOBAL_DB_RC_NO_RESULTS,
                                a_msg->group, l_values_count, l_values_count,
                                l_objs, a_msg->callback_arg);
        dap_global_db_objs_delete(l_objs, l_values_count);
         // All values are sent
        return false;
    }
    if (!a_msg->total_records)
        a_msg->total_records = dap_global_db_driver_count(a_msg->group, c_dap_global_db_driver_hash_blank, false);
    if (a_msg->total_records)
        l_store_objs = dap_global_db_driver_cond_read(a_msg->group, a_msg->last_hash, &l_values_count, false);
    int l_rc = DAP_GLOBAL_DB_RC_NO_RESULTS;
    if (l_store_objs && l_values_count) {
        a_msg->last_hash = dap_global_db_driver_hash_get(l_store_objs + l_values_count - 1);
        if (dap_global_db_driver_hash_is_blank(&a_msg->last_hash)) {
            l_rc = DAP_GLOBAL_DB_RC_SUCCESS;
            l_values_count--;
        } else
            l_rc = DAP_GLOBAL_DB_RC_PROGRESS;
        a_msg->processed_records += l_values_count;
    }
    l_objs = l_store_objs ? s_objs_from_store_objs(l_store_objs, l_values_count) : NULL;
    // Call callback if present
    bool l_ret = false;
    if (a_msg->callback_results)
        l_ret = a_msg->callback_results(a_msg->dbi, l_rc,
                        a_msg->group, a_msg->total_records, l_values_count,
                        l_objs, a_msg->callback_arg);
    dap_global_db_objs_delete(l_objs, l_values_count);
    return l_rc == DAP_GLOBAL_DB_RC_PROGRESS && l_ret;
}

/* *** Get_all_raw functions group *** */

dap_store_obj_t *dap_global_db_get_all_raw_sync(const char* a_group, size_t *a_objs_count)
{
    dap_return_val_if_fail(a_group, NULL);
    
    size_t l_values_count = 0;
    dap_store_obj_t *l_store_objs = dap_global_db_driver_read(a_group, 0, &l_values_count, true);
    if (a_objs_count)
        *a_objs_count = l_values_count;
    return l_store_objs;
}

/**
 * @brief dap_global_db_get_all_raw
 * @param a_group
 * @param a_first_id
 * @param a_results_page_size
 * @param a_callback
 * @param a_arg
 * @return
 */
int dap_global_db_get_all_raw(const char *a_group, size_t a_results_page_size, dap_global_db_callback_results_raw_t a_callback, void *a_arg)
{
    dap_return_val_if_fail(s_dbi && a_group, DAP_GLOBAL_DB_RC_ERROR);
    struct queue_io_msg *l_msg = DAP_NEW_Z_RET_VAL_IF_FAIL(struct queue_io_msg, DAP_GLOBAL_DB_RC_CRITICAL);
    l_msg->dbi = s_dbi;
    l_msg->opcode = MSG_OPCODE_GET_ALL_RAW;
    l_msg->group = dap_strdup(a_group);
    if (l_msg->group) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        DAP_DELETE(l_msg);
        return DAP_GLOBAL_DB_RC_CRITICAL;
    }
    l_msg->values_page_size = a_results_page_size;
    l_msg->callback_arg = a_arg;
    l_msg->callback_results_raw = a_callback;
    l_msg->last_hash = c_dap_global_db_driver_hash_blank;

    int l_ret = dap_proc_thread_callback_add(NULL, s_queue_io_callback, l_msg);
    if (l_ret != 0) {
        log_it(L_ERROR, "Can't exec get_all_raw request, code %d", l_ret);
        s_queue_io_msg_delete(l_msg);
    } else
        debug_if(g_dap_global_db_debug_more, L_DEBUG, "Have sent get_all request for \"%s\" group", a_group);
    return l_ret;
}

/**
 * @brief Get all records in raw format inside GlobalDB context
 * @param a_msg
 * @return
 */
static bool s_msg_opcode_get_all_raw(struct queue_io_msg *a_msg)
{
    dap_return_val_if_pass(!a_msg, false);

    size_t l_values_count = a_msg->values_page_size;
    dap_store_obj_t *l_store_objs = NULL;
    if (!a_msg->values_page_size) {
        l_store_objs = dap_global_db_get_all_raw_sync(a_msg->group, &l_values_count);
        if (a_msg->callback_results)
            a_msg->callback_results_raw(s_dbi,
                                l_store_objs ? DAP_GLOBAL_DB_RC_SUCCESS : DAP_GLOBAL_DB_RC_NO_RESULTS,
                                a_msg->group, a_msg->total_records, l_values_count,
                                l_store_objs, a_msg->callback_arg);
        dap_store_obj_free(l_store_objs, l_values_count);
        // All values are sent
       return false;
    }
    if (!a_msg->total_records)
        a_msg->total_records = dap_global_db_driver_count(a_msg->group, c_dap_global_db_driver_hash_blank, true);
    if (a_msg->total_records)
        l_store_objs = dap_global_db_driver_cond_read(a_msg->group, a_msg->last_hash, &l_values_count, true);
    int l_rc = DAP_GLOBAL_DB_RC_NO_RESULTS;
    if (l_store_objs && l_values_count) {
        a_msg->last_hash = dap_global_db_driver_hash_get(l_store_objs + l_values_count - 1);
        if (dap_global_db_driver_hash_is_blank(&a_msg->last_hash)) {
            l_rc = DAP_GLOBAL_DB_RC_SUCCESS;
            l_values_count--;
        } else
            l_rc = DAP_GLOBAL_DB_RC_PROGRESS;
        a_msg->processed_records += l_values_count;
    }
    // Call callback if present
    bool l_ret = false;
    if (a_msg->callback_results)
        l_ret = a_msg->callback_results_raw(a_msg->dbi, l_rc,
                        a_msg->group, a_msg->total_records, l_values_count,
                        l_store_objs, a_msg->callback_arg);
    dap_store_obj_free(l_store_objs, l_values_count);
    return l_rc == DAP_GLOBAL_DB_RC_PROGRESS && l_ret;
}

static int s_set_sync_with_ts(dap_global_db_instance_t *a_dbi, const char *a_group, const char *a_key, const void *a_value,
                              const size_t a_value_length, bool a_pin_value, dap_nanotime_t a_timestamp)
{
    dap_store_obj_t l_store_data = {
        .timestamp  = a_timestamp,
        .flags      = DAP_GLOBAL_DB_RECORD_NEW | (a_pin_value ? DAP_GLOBAL_DB_RECORD_PINNED : 0),
        .group      = (char *)a_group,
        .key        = (char *)a_key,
        .value      = (byte_t *)a_value,
        .value_len  = a_value_length
    };
    l_store_data.sign = dap_store_obj_sign(&l_store_data, a_dbi->signing_key, &l_store_data.crc);
    if (!l_store_data.sign) {
        log_it(L_ERROR, "Can't sign new global DB object group %s key %s", a_group, a_key);
        return DAP_GLOBAL_DB_RC_ERROR;
    }
    int l_res = s_store_obj_apply(a_dbi, &l_store_data);
    DAP_DELETE(l_store_data.sign);
    return l_res;
}

/**
 * @brief dap_global_db_set_unsafe
 * @param a_dbi
 * @param a_group
 * @param a_key
 * @param a_value
 * @param a_value_length
 * @param a_pin_value
 * @return
 */
int dap_global_db_set_sync(const char *a_group, const char *a_key, const void *a_value, const size_t a_value_length, bool a_pin_value)
{
    dap_return_val_if_fail(s_dbi && a_group && a_key, DAP_GLOBAL_DB_RC_ERROR);
    return s_set_sync_with_ts(s_dbi, a_group, a_key, a_value,
                                a_value_length, a_pin_value, dap_nanotime_now());
}

/**
 * @brief Set GlobalDB record, identified with group and key
 * @param a_group Group name
 * @param a_key Key string
 * @param a_value Value data's pointer
 * @param a_value_length Value data's length
 * @param a_pin_value Pin value or not
 * @param a_callback  Callback executed after request processing
 * @param a_arg Argument passed to the callback
 * @return 0 if success, error code if not
 */
int dap_global_db_set(const char * a_group, const char *a_key, const void * a_value, const size_t a_value_length, bool a_pin_value, dap_global_db_callback_result_t a_callback, void *a_arg)
{
    dap_return_val_if_fail(s_dbi && a_group && a_key, DAP_GLOBAL_DB_RC_ERROR);
    struct queue_io_msg *l_msg = DAP_NEW_Z_RET_VAL_IF_FAIL(struct queue_io_msg, DAP_GLOBAL_DB_RC_CRITICAL);
    l_msg->dbi = s_dbi;
    l_msg->opcode = MSG_OPCODE_SET;
    l_msg->group = dap_strdup(a_group);
    if (!l_msg->group) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        DAP_DELETE(l_msg);
        return DAP_GLOBAL_DB_RC_CRITICAL;
    }
    l_msg->key = dap_strdup(a_key);
    if (!l_msg->key) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        DAP_DEL_MULTY(l_msg->group, l_msg);
        return DAP_GLOBAL_DB_RC_CRITICAL;
    }
    if (a_value && a_value_length) {
        l_msg->value = DAP_DUP_SIZE_RET_VAL_IF_FAIL((char*)a_value, a_value_length, DAP_GLOBAL_DB_RC_CRITICAL, l_msg->key, l_msg->group, l_msg);
        l_msg->value_length = a_value_length;
    }
    l_msg->value_is_pinned = a_pin_value;
    l_msg->callback_arg = a_arg;
    l_msg->callback_result = a_callback;

    int l_ret = dap_proc_thread_callback_add(NULL, s_queue_io_callback, l_msg);
    if (l_ret != 0) {
        log_it(L_ERROR, "Can't exec set request, code %d", l_ret);
        s_queue_io_msg_delete(l_msg);
        l_ret = DAP_GLOBAL_DB_RC_ERROR;
    } else
        debug_if(g_dap_global_db_debug_more, L_DEBUG, "Have sent sent request for \"%s\" group \"%s\" key", a_group, a_key);

    return l_ret;
}

/**
 * @brief s_msg_opcode_set
 * @param a_msg
 * @return
 */
static void s_msg_opcode_set(struct queue_io_msg * a_msg)
{
    dap_nanotime_t l_ts_now = dap_nanotime_now();
    int l_res = s_set_sync_with_ts(a_msg->dbi, a_msg->group, a_msg->key, a_msg->value,
                                     a_msg->value_length, a_msg->value_is_pinned, l_ts_now);

    if (l_res != DAP_GLOBAL_DB_RC_SUCCESS)
        log_it(L_ERROR, "Save error for %s:%s code %d", a_msg->group, a_msg->key, l_res);
    if (a_msg->callback_result)
        a_msg->callback_result(a_msg->dbi, l_res, a_msg->group, a_msg->key,
                               a_msg->value, a_msg->value_length, l_ts_now,
                               a_msg->value_is_pinned, a_msg->callback_arg);
}

/* *** Set_raw functions group *** */

int s_db_set_raw_sync(dap_global_db_instance_t *a_dbi, dap_store_obj_t *a_store_objs, size_t a_store_objs_count)
{
    int l_ret = DAP_GLOBAL_DB_RC_ERROR;
    if (a_store_objs_count > 1)
        dap_global_db_driver_txn_start();
    for (size_t i = 0; i < a_store_objs_count; i++) {
        l_ret = s_store_obj_apply(a_dbi, a_store_objs + i);
        if (l_ret)
            debug_if(g_dap_global_db_debug_more, L_ERROR, "Can't save raw gdb data to %s/%s, code %d", (a_store_objs + i)->group, (a_store_objs + i)->key, l_ret);
    }
    if (a_store_objs_count > 1)
        dap_global_db_driver_txn_end(!l_ret);
    return l_ret;
}

int dap_global_db_set_raw_sync(dap_store_obj_t *a_store_objs, size_t a_store_objs_count)
{
    dap_return_val_if_fail(s_dbi && a_store_objs && a_store_objs_count, DAP_GLOBAL_DB_RC_ERROR);
    return s_db_set_raw_sync(s_dbi, a_store_objs, a_store_objs_count);
}

/**
 * @brief dap_global_db_set_raw
 * @param a_store_objs
 * @param a_store_objs_count
 * @param a_callback
 * @param a_arg
 * @return
 */
int dap_global_db_set_raw(dap_store_obj_t *a_store_objs, size_t a_store_objs_count, dap_global_db_callback_results_raw_t a_callback, void *a_arg)
{
    dap_return_val_if_fail(s_dbi && a_store_objs && a_store_objs_count, DAP_GLOBAL_DB_RC_ERROR);
    struct queue_io_msg *l_msg = DAP_NEW_Z_RET_VAL_IF_FAIL(struct queue_io_msg, DAP_GLOBAL_DB_RC_CRITICAL);
    l_msg->dbi = s_dbi;
    l_msg->opcode = MSG_OPCODE_SET_RAW;
    l_msg->callback_arg = a_arg;
    l_msg->callback_results_raw = a_callback;

    l_msg->values_raw = dap_store_obj_copy(a_store_objs, a_store_objs_count);
    if (!l_msg->values_raw) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        DAP_DELETE(l_msg);
        return DAP_GLOBAL_DB_RC_CRITICAL;
    }
    l_msg->values_raw_total = a_store_objs_count;

    int l_ret = dap_proc_thread_callback_add(NULL, s_queue_io_callback, l_msg);
    if (l_ret != 0) {
        log_it(L_ERROR, "Can't exec set_raw request, code %d", l_ret);
        s_queue_io_msg_delete(l_msg);
        l_ret = DAP_GLOBAL_DB_RC_ERROR;
    } else
        debug_if(g_dap_global_db_debug_more, L_DEBUG, "Have sent set_raw request for %zu objects", a_store_objs_count);
    return l_ret;

}

/**
 * @brief s_msg_opcode_set_raw
 * @param a_msg
 * @return
 */
static void s_msg_opcode_set_raw(struct queue_io_msg * a_msg)
{
    int l_ret = -1;
    if (a_msg->values_raw_total > 0)
        l_ret = s_db_set_raw_sync(a_msg->dbi, a_msg->values_raw, a_msg->values_raw_total);
    if (a_msg->callback_results_raw)
        a_msg->callback_results_raw(a_msg->dbi,
                                    l_ret == 0 ? DAP_GLOBAL_DB_RC_SUCCESS : DAP_GLOBAL_DB_RC_ERROR,
                                    a_msg->group, a_msg->values_raw_total, a_msg->values_raw_total,
                                    a_msg->values_raw, a_msg->callback_arg);
}

/* *** Set_multiple_zc functions group *** */

/**
 * @brief dap_global_db_set_multiple_zc Set multiple values, without duplication (zero copy, values are freed after set callback execution )
 * @param a_group
 * @param a_values
 * @param a_values_count
 * @param a_callback
 * @param a_arg
 * @return
 */
int dap_global_db_set_multiple_zc(const char *a_group, dap_global_db_obj_t *a_values, size_t a_values_count, dap_global_db_callback_results_t a_callback, void *a_arg)
{
    dap_return_val_if_fail(s_dbi && a_group && a_values && a_values_count, DAP_GLOBAL_DB_RC_ERROR);
    struct queue_io_msg *l_msg = DAP_NEW_Z_RET_VAL_IF_FAIL(struct queue_io_msg, DAP_GLOBAL_DB_RC_CRITICAL);
    l_msg->dbi = s_dbi;
    l_msg->opcode = MSG_OPCODE_SET_MULTIPLE;
    l_msg->group = dap_strdup(a_group);
    if (!l_msg->group) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        DAP_DELETE(l_msg);
        return DAP_GLOBAL_DB_RC_CRITICAL;
    }
    l_msg->values = a_values;
    l_msg->value_is_pinned = false;
    l_msg->values_count = a_values_count;
    l_msg->callback_arg = a_arg;
    l_msg->callback_results = a_callback;

    int l_ret = dap_proc_thread_callback_add(NULL, s_queue_io_callback, l_msg);
    if (l_ret != 0) {
        log_it(L_ERROR, "Can't exec set_multiple request, code %d", l_ret);
        s_queue_io_msg_delete(l_msg);
        l_ret = DAP_GLOBAL_DB_RC_ERROR;
    } else
        debug_if(g_dap_global_db_debug_more, L_DEBUG, "Have sent set_multiple request for \"%s\" group with %zu values", a_group, a_values_count);
    return l_ret;
}

/**
 * @brief s_msg_opcode_set_multiple
 * @param a_msg
 * @return
 */
static void s_msg_opcode_set_multiple_zc(struct queue_io_msg * a_msg)
{
    int l_ret = 0;
    size_t i=0;
#ifdef DAP_TPS_TEST
    FILE *l_mempool_file = fopen("/opt/cellframe-node/share/ca/mempool_start.txt", "a");
    if (l_mempool_file)
        fclose(l_mempool_file);
    log_it(L_TPS, "Start write %zu records to mempool", a_msg->values_count);
#endif
    if(a_msg->values_count>0) {
        dap_store_obj_t l_store_obj = {};
        for(;  i < a_msg->values_count && l_ret == 0  ; i++ ) {
            l_ret = s_set_sync_with_ts(a_msg->dbi, a_msg->group, a_msg->values[i].key, a_msg->values[i].value, a_msg->values[i].value_len, a_msg->value_is_pinned, a_msg->values[i].timestamp);
        }
    }
    if(a_msg->callback_results) {
        a_msg->callback_results(a_msg->dbi,
                                l_ret == 0 ? DAP_GLOBAL_DB_RC_SUCCESS : DAP_GLOBAL_DB_RC_ERROR,
                                a_msg->group, i, a_msg->values_count,
                                a_msg->values, a_msg->callback_arg);
    }
    dap_global_db_objs_delete( a_msg->values, a_msg->values_count);
#ifdef DAP_TPS_TEST
    l_mempool_file = fopen("/opt/cellframe-node/share/ca/mempool_finish.txt", "a");
    if (l_mempool_file)
        fclose(l_mempool_file);
    log_it(L_TPS, "Finish write %zu records to mempool ", a_msg->values_count);
#endif
}

/* *** Pin/unpin functions group *** */

int s_db_object_pin_sync(dap_global_db_instance_t *a_dbi, const char *a_group, const char *a_key, bool a_pin)
{
    int l_res = DAP_GLOBAL_DB_RC_NO_RESULTS;
    dap_store_obj_t *l_store_obj = dap_global_db_get_raw_sync(a_group, a_key);
    if (l_store_obj) {
        l_res = dap_global_db_set_sync(l_store_obj->group, l_store_obj->key, l_store_obj->value, l_store_obj->value_len, a_pin);
        if (l_res) {
            log_it(L_ERROR,"Can't save pinned gdb data, code %d ", l_res);
            l_res = DAP_GLOBAL_DB_RC_ERROR;
        }
    }
    dap_store_obj_free_one(l_store_obj);
    return l_res;
}

int dap_global_db_pin_sync(const char *a_group, const char *a_key)
{
    dap_return_val_if_fail(s_dbi && a_group && a_key, DAP_GLOBAL_DB_RC_ERROR);
    return s_db_object_pin_sync(s_dbi, a_group, a_key, true);
}

int dap_global_db_unpin_sync(const char *a_group, const char *a_key)
{
    dap_return_val_if_fail(s_dbi && a_group && a_key, DAP_GLOBAL_DB_RC_ERROR);
    return s_db_object_pin_sync(s_dbi, a_group, a_key, false);
}

int s_db_object_pin(const char *a_group, const char *a_key, dap_global_db_callback_result_t a_callback, void *a_arg, bool a_pin)
{
    struct queue_io_msg *l_msg = DAP_NEW_Z_RET_VAL_IF_FAIL(struct queue_io_msg, DAP_GLOBAL_DB_RC_CRITICAL);
    l_msg->dbi = s_dbi;
    l_msg->opcode = MSG_OPCODE_PIN;
    l_msg->group = dap_strdup(a_group);
    if (!l_msg->group) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        DAP_DELETE(l_msg);
        return DAP_GLOBAL_DB_RC_CRITICAL;
    }
    l_msg->key = dap_strdup(a_key);
    if (!l_msg->key) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        DAP_DEL_MULTY(l_msg->group, l_msg);
        return DAP_GLOBAL_DB_RC_CRITICAL;
    }
    l_msg->callback_arg = a_arg;
    l_msg->callback_result = a_callback;
    l_msg->value_is_pinned = a_pin;
    debug_if(g_dap_global_db_debug_more, L_DEBUG, "%s \"%s\" group \"%s\" key from pinned groups",
                                                    a_pin ? "Add" : "Remove", a_group, a_key);

    int l_ret = dap_proc_thread_callback_add(NULL, s_queue_io_callback, l_msg);
    if (l_ret != 0) {
        log_it(L_ERROR, "Can't exec %s request, code %d", a_pin ? "pin" : "unpin", l_ret);
        s_queue_io_msg_delete(l_msg);
    } else
        debug_if(g_dap_global_db_debug_more, L_DEBUG, "Have sent %s request for \"%s\" group \"%s\" key",
                                                       a_pin ? "pin" : "unpin", a_group, a_key);
    return l_ret;
}

/**
 * @brief s_msg_opcode_pin
 * @param a_msg
 * @return
 */
static void s_msg_opcode_pin(struct queue_io_msg * a_msg)
{
    int l_res = s_db_object_pin_sync(a_msg->dbi, a_msg->group, a_msg->key, a_msg->value_is_pinned);
    if (a_msg->callback_result)
        a_msg->callback_result(s_dbi, l_res, a_msg->group, a_msg->key,
                               NULL, 0, 0, a_msg->value_is_pinned, a_msg->callback_arg);
}

/**
 * @brief dap_global_db_pin
 * @param a_group
 * @param a_key
 * @param a_callback
 * @param a_arg
 * @return
 */
int dap_global_db_pin(const char *a_group, const char *a_key, dap_global_db_callback_result_t a_callback, void *a_arg)
{
    dap_return_val_if_fail(s_dbi && a_group && a_key, DAP_GLOBAL_DB_RC_ERROR);
    return s_db_object_pin(a_group, a_key, a_callback, a_arg, true);
}
/**
 * @brief dap_global_db_unpin
 * @param a_group
 * @param a_key
 * @param a_callback
 * @param a_arg
 * @return
 */
int dap_global_db_unpin(const char *a_group, const char *a_key, dap_global_db_callback_result_t a_callback, void *a_arg)
{
    dap_return_val_if_fail(s_dbi && a_group && a_key, DAP_GLOBAL_DB_RC_ERROR);
    return s_db_object_pin(a_group, a_key, a_callback, a_arg, false);
}

/* *** Del functions group *** */

/**
 * @brief dap_global_db_del_unsafe
 * @param a_group
 * @param a_key
 * @return
 */
static int s_del_sync_with_dbi(dap_global_db_instance_t *a_dbi, const char *a_group, const char *a_key)
{
    dap_store_obj_t l_store_obj = {
        .key        = (char*)a_key,
        .group      = (char*)a_group,
        .flags      = DAP_GLOBAL_DB_RECORD_NEW | (a_key ? DAP_GLOBAL_DB_RECORD_DEL : DAP_GLOBAL_DB_RECORD_ERASE),
        .timestamp  = dap_nanotime_now()
    };
    if (a_key)
        l_store_obj.sign = dap_store_obj_sign(&l_store_obj, a_dbi->signing_key, &l_store_obj.crc);

    int l_res = -1;
    if (a_key) {
        l_res = s_store_obj_apply(a_dbi, &l_store_obj);
        DAP_DELETE(l_store_obj.sign);
    } else {
        // Drop the whole table
        l_store_obj.flags |= DAP_GLOBAL_DB_RECORD_ERASE;
        l_res = dap_global_db_driver_apply(&l_store_obj, 1);
        if (l_res)
            log_it(L_ERROR, "Can't delete group %s", l_store_obj.group);
    }    
    return l_res;
}

inline int dap_global_db_del_sync(const char *a_group, const char *a_key)
{
    dap_return_val_if_fail(s_dbi && a_group, DAP_GLOBAL_DB_RC_ERROR);
    return s_del_sync_with_dbi(s_dbi, a_group, a_key);
}

/**
 * @brief dap_global_db_delete
 * @param a_group
 * @param a_key
 * @param a_callback
 * @param a_arg
 * @return
 */
int dap_global_db_del(const char * a_group, const char *a_key, dap_global_db_callback_result_t a_callback, void *a_arg)
{
    dap_return_val_if_fail(s_dbi && a_group, DAP_GLOBAL_DB_RC_ERROR);
    struct queue_io_msg *l_msg = DAP_NEW_Z_RET_VAL_IF_FAIL(struct queue_io_msg, DAP_GLOBAL_DB_RC_CRITICAL);
    l_msg->dbi = s_dbi;
    l_msg->opcode = MSG_OPCODE_DELETE;
    l_msg->group = dap_strdup(a_group);
    l_msg->key = dap_strdup(a_key);
    l_msg->callback_arg = a_arg;
    l_msg->callback_result = a_callback;

    int l_ret = dap_proc_thread_callback_add(NULL, s_queue_io_callback, l_msg);
    if (l_ret != 0) {
        log_it(L_ERROR, "Can't exec del request, code %d", l_ret);
        s_queue_io_msg_delete(l_msg);
    } else
        debug_if(g_dap_global_db_debug_more, L_DEBUG, "Have sent del request for \"%s\" group \"%s\" key", a_group, a_key);

    return l_ret;
}

/**
 * @brief erase table, call dap_global_db_del_sync with NULL key
 * @param a_group - table name
 * @return result of dap_global_db_del_sync
 */
DAP_INLINE int dap_global_db_erase_table_sync(const char *a_group)
{
    return dap_global_db_del_sync(a_group, NULL);
}

/**
 * @brief erase table, call dap_global_db_del with NULL key
 * @param a_group - table name
 * @param a_callback - callback result
 * @param a_arg - callback args
 * @return result of dap_global_db_del
 */
DAP_INLINE int dap_global_db_erase_table(const char *a_group, dap_global_db_callback_result_t a_callback, void *a_arg)
{
    return dap_global_db_del(a_group, NULL, a_callback, a_arg);
}

/**
 * @brief s_msg_opcode_delete
 * @param a_msg
 * @return
 */
static void s_msg_opcode_delete(struct queue_io_msg * a_msg)
{
    int l_res = dap_global_db_del_sync(a_msg->group, a_msg->key);

    if(a_msg->callback_result) {
        a_msg->callback_result(a_msg->dbi, l_res==0 ? DAP_GLOBAL_DB_RC_SUCCESS:
                                        DAP_GLOBAL_DB_RC_ERROR,
                                a_msg->group, a_msg->key,
                               NULL, 0, 0 , false, a_msg->callback_arg );
    }
}

/* *** Flush functions group *** */

/**
 * @brief dap_global_db_flush_sync
 * @return
 */
int dap_global_db_flush_sync()
{
    return dap_global_db_driver_flush();
}

/**
 * @brief dap_global_db_flush
 * @param a_callback
 * @param a_arg
 * @return
 */
int dap_global_db_flush(dap_global_db_callback_result_t a_callback, void * a_arg)
{
    dap_return_val_if_fail(s_dbi, DAP_GLOBAL_DB_RC_ERROR);
    struct queue_io_msg *l_msg = DAP_NEW_Z_RET_VAL_IF_FAIL(struct queue_io_msg, DAP_GLOBAL_DB_RC_CRITICAL);
    l_msg->dbi = s_dbi;
    l_msg->opcode = MSG_OPCODE_FLUSH;
    l_msg->callback_arg = a_arg;
    l_msg->callback_result = a_callback;

    int l_ret = dap_proc_thread_callback_add(NULL, s_queue_io_callback, l_msg);
    if (l_ret != 0) {
        log_it(L_ERROR, "Can't exec flush request, code %d", l_ret);
        s_queue_io_msg_delete(l_msg);
    }
    return l_ret;
}

/**
 * @brief s_msg_opcode_flush
 * @param a_msg
 * @return
 */
static void s_msg_opcode_flush(struct queue_io_msg * a_msg)
{
    int l_res = dap_global_db_flush_sync();
    if (a_msg->callback_result)
        a_msg->callback_result(a_msg->dbi, l_res ? DAP_GLOBAL_DB_RC_ERROR : DAP_GLOBAL_DB_RC_SUCCESS,
                                NULL, NULL, NULL, 0, 0, false, a_msg->callback_arg);
}

/* *** Other functions *** */

/**
 * @brief Copies memory of an objs array.
 * @param objs a pointer to the first source object of the array
 * @param a_count a number of objects in the array
 * @return (none)
 */
dap_global_db_obj_t *dap_global_db_objs_copy(const dap_global_db_obj_t *a_objs_src, size_t a_count)
{   /* Sanity checks */
    dap_return_val_if_fail(a_objs_src && a_count, NULL);

    /* Run over array's elements */
    const dap_global_db_obj_t *l_obj = a_objs_src;
    dap_global_db_obj_t *l_objs_dest = DAP_NEW_Z_COUNT_RET_VAL_IF_FAIL(dap_global_db_obj_t, a_count, NULL);
    for (dap_global_db_obj_t *l_cur = l_objs_dest; a_count--; l_cur++, l_obj++) {
        *l_cur = *l_obj;
        if (l_obj->key) {
            l_cur->key = dap_strdup(l_obj->key);
            if (!l_cur->key) {
                log_it(L_CRITICAL, "%s", c_error_memory_alloc);
                DAP_DELETE(l_objs_dest);
                return NULL;
            }
        } else
            log_it(L_WARNING, "Inconsistent global DB object copy requested");

        if (l_obj->value) {
            if (l_obj->value_len) {
                l_cur->value = DAP_DUP_SIZE(l_obj->value, l_obj->value_len);
                if (!l_cur->value) {
                    log_it(L_CRITICAL, "%s", c_error_memory_alloc);
                    DAP_DEL_MULTY(l_cur->key, l_objs_dest);
                    return NULL;
                }
            } else
                log_it(L_WARNING, "Inconsistent global DB object copy requested");
        }
    }
    return l_objs_dest;
}

/**
 * @brief Deallocates memory of an objs array.
 * @param objs a pointer to the first object of the array
 * @param a_count a number of objects in the array
 * @return (none)
 */
void dap_global_db_objs_delete(dap_global_db_obj_t *a_objs, size_t a_count)
{
    if (a_objs && a_count) {
        for (dap_global_db_obj_t *l_obj = a_objs; a_count--; ++l_obj)
            DAP_DEL_MULTY(l_obj->key, l_obj->value);
    }
    DAP_DELETE(a_objs);
}

/**
 * @brief s_msg_opcode_to_str
 * @param a_opcode
 * @return
 */
static const char *s_msg_opcode_to_str(enum queue_io_msg_opcode a_opcode)
{
    switch(a_opcode) {
        case MSG_OPCODE_GET:            return "GET";
        case MSG_OPCODE_GET_RAW:        return "GET_RAW";
        case MSG_OPCODE_GET_LAST:       return "GET_LAST";
        case MSG_OPCODE_GET_DEL_TS:     return "GET_DEL_TS";
        case MSG_OPCODE_GET_LAST_RAW:   return "GET_LAST_RAW";
        case MSG_OPCODE_GET_ALL:        return "GET_ALL";
        case MSG_OPCODE_GET_ALL_RAW:    return "GET_ALL_RAW";
        case MSG_OPCODE_SET:            return "SET";
        case MSG_OPCODE_SET_MULTIPLE:   return "SET_MULTIPLE";
        case MSG_OPCODE_SET_RAW:        return "SET_RAW";
        case MSG_OPCODE_PIN:            return "PIN";
        case MSG_OPCODE_DELETE:         return "DELETE";
        case MSG_OPCODE_FLUSH:          return "FLUSH";
        default:                        return "UNKNOWN";
    }
}

/**
 * @brief s_queue_io_callback
 * @details Queue I/O process callback
 * @param a_es
 * @param a_arg
 */
static bool s_queue_io_callback(void * a_arg)
{
    struct queue_io_msg *l_msg = (struct queue_io_msg *) a_arg;
    assert(l_msg);

    debug_if(g_dap_global_db_debug_more, L_NOTICE, "Received GlobalDB I/O message with opcode %s", s_msg_opcode_to_str(l_msg->opcode) );

    switch (l_msg->opcode) {
    case MSG_OPCODE_GET:            s_msg_opcode_get(l_msg); break;
    case MSG_OPCODE_GET_RAW:        s_msg_opcode_get_raw(l_msg); break;
    case MSG_OPCODE_GET_LAST:       s_msg_opcode_get_last(l_msg); break;
    case MSG_OPCODE_GET_LAST_RAW:   s_msg_opcode_get_last_raw(l_msg); break;
    case MSG_OPCODE_GET_DEL_TS:     s_msg_opcode_get_del_ts(l_msg); break;
    case MSG_OPCODE_GET_ALL:        if (s_msg_opcode_get_all(l_msg)) return true; break;
    case MSG_OPCODE_GET_ALL_RAW:    if (s_msg_opcode_get_all_raw(l_msg)) return true; break;
    case MSG_OPCODE_SET:            s_msg_opcode_set(l_msg); break;
    case MSG_OPCODE_SET_MULTIPLE:   s_msg_opcode_set_multiple_zc(l_msg); break;
    case MSG_OPCODE_SET_RAW:        s_msg_opcode_set_raw(l_msg); break;
    case MSG_OPCODE_PIN:            s_msg_opcode_pin(l_msg); break;
    case MSG_OPCODE_DELETE:         s_msg_opcode_delete(l_msg); break;
    case MSG_OPCODE_FLUSH:          s_msg_opcode_flush(l_msg); break;
    default:
        log_it(L_WARNING, "Message with undefined opcode %d received in queue_io",
               l_msg->opcode);
    }
    s_queue_io_msg_delete(l_msg);
    return false;
}

/**
 * @brief s_queue_io_msg_delete
 * @param a_msg
 */
static void s_queue_io_msg_delete( struct queue_io_msg * a_msg)
{
    switch(a_msg->opcode) {    
    case MSG_OPCODE_SET:
        DAP_DEL_Z(a_msg->value);
    case MSG_OPCODE_GET:
    case MSG_OPCODE_GET_RAW:
    case MSG_OPCODE_GET_DEL_TS:
    case MSG_OPCODE_PIN:
    case MSG_OPCODE_DELETE:
        DAP_DEL_Z(a_msg->key);
    case MSG_OPCODE_GET_LAST:
    case MSG_OPCODE_GET_LAST_RAW:
    case MSG_OPCODE_GET_ALL:
    case MSG_OPCODE_GET_ALL_RAW:
    case MSG_OPCODE_SET_MULTIPLE:
        DAP_DEL_Z(a_msg->group);
        break;
    case MSG_OPCODE_SET_RAW:
        dap_store_obj_free(a_msg->values_raw, a_msg->values_raw_total);
    default:;
    }
    DAP_DEL_Z(a_msg);
}

/**
 * @brief s_check_db_version
 * @return
 */
static int s_check_db_version()
{
    pthread_mutex_lock(&s_check_db_mutex);
    int l_ret = dap_global_db_get(DAP_GLOBAL_DB_LOCAL_GENERAL, "gdb_version",s_check_db_version_callback_get, NULL);
    if (l_ret == 0) {
        while (s_check_db_ret == INVALID_RETCODE)
            pthread_cond_wait(&s_check_db_cond, &s_check_db_mutex);
        l_ret = s_check_db_ret;
    } else{
        log_it(L_CRITICAL, "Can't process get gdb_version request, code %d", l_ret);
    }
    pthread_mutex_unlock(&s_check_db_mutex);
    return l_ret;
}

static bool s_clean_old_obj_gdb_callback() {
    debug_if(g_dap_global_db_debug_more, L_INFO, "Start clean old objs in gdb callback");
    dap_nanotime_t l_time_now = dap_nanotime_now();
    dap_list_t *l_group_list = dap_global_db_driver_get_groups_by_mask("*");
    size_t l_count = 0;
    for (dap_list_t *l_list = l_group_list; l_list; l_list = dap_list_next(l_list), ++l_count) {
        size_t l_count_obj = dap_global_db_driver_count((char*)l_list->data, c_dap_global_db_driver_hash_blank, true);
        if (!l_count_obj) {
            debug_if(g_dap_global_db_debug_more, L_INFO, "Empty group %s, delete it", (char*)l_list->data);
            dap_global_db_del_sync((char*)l_list->data, NULL);
            continue;
        }
        size_t l_ret_count;
        dap_store_obj_t * l_ret = dap_global_db_driver_read_obj_below_timestamp((char*)l_list->data, l_time_now + 100, &l_ret_count);
        if (!l_ret || !l_ret->group) {
            DAP_DELETE(l_ret);
            continue;
        }
        dap_global_db_cluster_t *l_cluster = dap_global_db_cluster_by_group(s_dbi, l_ret->group);
        if (!l_cluster) {
            DAP_DELETE(l_ret);
            continue;
        }

        dap_nanotime_t l_ttl = dap_nanotime_from_sec(l_cluster->ttl);
        for(size_t i = 0; i < l_ret_count; i++) {
            if (!(l_ret[i].flags & DAP_GLOBAL_DB_RECORD_PINNED)) {
                if (l_ttl != 0) {
                    if (l_ret[i].timestamp + l_ttl < l_time_now) {
                        debug_if(g_dap_global_db_debug_more, L_INFO, "Delete from gdb obj %s group, %s key", l_ret[i].group, l_ret[i].key);
                        if (l_cluster->del_callback)
                            l_cluster->del_callback(l_ret+i, NULL);
                        else dap_global_db_driver_delete(l_ret + i, 1);
                    }
                } else if ( l_ret[i].flags & DAP_GLOBAL_DB_RECORD_DEL && dap_global_db_group_match_mask(l_ret->group, "local.*")) {       
                    debug_if(g_dap_global_db_debug_more, L_INFO, "Delete from empty local gdb obj %s group, %s key", l_ret[i].group, l_ret[i].key);
                    dap_global_db_driver_delete(l_ret + i, 1);
                }
            } 
        }
        DAP_DELETE(l_ret);
    }
    dap_list_free(l_group_list);
    return true;
}

static int s_gdb_clean_init() {
    debug_if(g_dap_global_db_debug_more, L_INFO, "Init global_db clean old objects");
    s_check_gdb_clean_timer = dap_timerfd_start(1800000, (dap_timerfd_callback_t)s_clean_old_obj_gdb_callback, NULL);
    if (!s_check_gdb_clean_timer)
        return -1;
    return 0;
}

static void s_gdb_clean_deinit() {
    dap_timerfd_delete_mt(s_check_gdb_clean_timer->worker, s_check_gdb_clean_timer->esocket_uuid);
}


static bool s_check_pinned_db_objs_callback() {
    debug_if(g_dap_global_db_debug_more, L_INFO, "Start check pinned objs callback");
    dap_nanotime_t l_time_now = dap_nanotime_now();
    dap_list_t *l_group_list = dap_global_db_driver_get_groups_by_mask("*.pinned");
    size_t l_count = 0;
    for (dap_list_t *l_list = l_group_list; l_list; l_list = dap_list_next(l_list), ++l_count) {
        size_t l_ret_count;
        dap_store_obj_t * l_ret = dap_global_db_driver_read_obj_below_timestamp((char*)l_list->data, l_time_now - s_minimal_ttl/2, &l_ret_count); 
        if (!l_ret || !l_ret->group)
            continue;
        char * l_group_name = dap_get_group_from_pinned_groups_mask(l_ret->group);
        if (!l_group_name) {
            DAP_DELETE(l_ret);
            continue;
        }
        dap_global_db_cluster_t *l_cluster = dap_global_db_cluster_by_group(s_dbi, l_group_name);
        if (!l_cluster) {
            DAP_DELETE(l_ret);
            continue;
        }
        dap_nanotime_t l_ttl = dap_nanotime_from_sec(l_cluster->ttl);

        for(size_t i = 0; i < l_ret_count; i++) {
            if (l_ret[i].timestamp + l_ttl <= l_time_now + s_minimal_ttl) {
                dap_store_obj_t * l_gdb_rec =  dap_global_db_get_raw_sync(l_group_name, l_ret[i].key);
                if (!l_gdb_rec) {
                    debug_if(g_dap_global_db_debug_more, L_INFO, "Can't find source gdb obj %s group, %s key delete obj from pinned group gdb ", l_gdb_rec->group, l_gdb_rec->key);
                    s_del_pinned_obj_from_pinned_group_by_source_group(l_gdb_rec);
                    continue;
                }
                debug_if(g_dap_global_db_debug_more, L_INFO, "Repin gdb obj %s group, %s key", l_gdb_rec->group, l_gdb_rec->key);
                l_ret->timestamp = l_time_now;
                dap_global_db_set_raw_sync(l_ret+i, 1);
                l_gdb_rec->timestamp = l_time_now;
                dap_global_db_set_raw_sync(l_gdb_rec, 1);
                dap_store_obj_free(l_gdb_rec, 1);
            }
        }
        DAP_DELETE(l_ret);
    }
    dap_list_free(l_group_list);
    return true;
}

DAP_STATIC_INLINE char *dap_get_local_pinned_groups_mask(const char *a_group)
{
    return dap_strdup_printf("local.%s.pinned", a_group);
}

DAP_STATIC_INLINE char *dap_get_group_from_pinned_groups_mask(const char *a_group) {
    if (!a_group)
        return NULL;
    char ** l_group_arr = dap_strsplit(a_group, ".", 3);
    if (!l_group_arr[1] || !l_group_arr[2]) {
        dap_strfreev(l_group_arr);
        return NULL;
    }
    char *result = dap_strdup_printf("%s.%s", l_group_arr[1], l_group_arr[2]);
    dap_strfreev(l_group_arr);
    return result;
}

static void s_get_all_pinned_objs_in_group(dap_store_obj_t * a_objs, size_t a_objs_count) {
    debug_if(g_dap_global_db_debug_more, L_INFO, "Get all pinned obj");
    for(size_t i = 0; i < a_objs_count; i++) {
        if (a_objs[i].flags & DAP_GLOBAL_DB_RECORD_PINNED)
            s_add_pinned_obj_in_pinned_group(a_objs + i);
    }
}

static int s_add_pinned_obj_in_pinned_group(dap_store_obj_t * a_objs){
    if (!dap_global_db_group_match_mask(a_objs->group, "*pinned")) {
        char * l_pinned_mask = dap_get_local_pinned_groups_mask(a_objs->group);
        dap_store_obj_t * l_ret_check = dap_global_db_get_raw_sync(l_pinned_mask, a_objs->key);
        if (!l_ret_check) {
            if (!dap_global_db_set_sync(l_pinned_mask, a_objs->key, NULL, 0, true)) {
                debug_if(g_dap_global_db_debug_more, L_INFO, "Pinned objs was added in pinned group %s, %s key", l_pinned_mask, a_objs->key);
                a_objs->timestamp = dap_time_now();
                dap_global_db_driver_apply(a_objs, 1);
            } else
                debug_if(g_dap_global_db_debug_more, L_ERROR, "Adding error in pinned group %s", a_objs->group);
        }

        dap_global_db_cluster_t *l_cluster = dap_global_db_cluster_by_group(s_dbi, a_objs->group);
        if ((l_cluster->ttl != 0 && s_minimal_ttl > dap_nanotime_from_sec(l_cluster->ttl)) || !s_check_pinned_db_objs_timer) {
            s_check_pinned_db_objs_deinit();
            if (l_cluster->ttl != 0)
                s_minimal_ttl = dap_nanotime_from_sec(l_cluster->ttl);
            s_check_pinned_db_objs_timer = dap_timerfd_start(dap_nanotime_to_millitime(s_minimal_ttl/2), (dap_timerfd_callback_t)s_check_pinned_db_objs_callback, NULL);
            debug_if(g_dap_global_db_debug_more, L_INFO, "New pinned callback timer %llu", s_minimal_ttl);
        }
        dap_store_obj_free_one(l_ret_check);
        DAP_DELETE(l_pinned_mask);
    }
    return 0;
}

static void s_del_pinned_obj_from_pinned_group_by_source_group(dap_store_obj_t * a_objs) {
    debug_if(g_dap_global_db_debug_more, L_INFO, "Delete pinned group by source group %s, %s key", a_objs->group, a_objs->key);
    char * l_pinned_group = dap_get_local_pinned_groups_mask(a_objs->group);
    dap_store_obj_t * l_pin_del_obj = dap_global_db_get_raw_sync(l_pinned_group, a_objs->key);
    if (l_pin_del_obj)
        dap_global_db_driver_delete(l_pin_del_obj, 1);
}

static int s_pinned_objs_group_init() {
    debug_if(g_dap_global_db_debug_more, L_INFO, "Check pinned db objs init");
    dap_list_t *l_group_list = dap_global_db_driver_get_groups_by_mask("*");
    for (dap_list_t *l_list = l_group_list; l_list; l_list = dap_list_next(l_list)) {
        size_t l_ret_count;
        dap_store_obj_t  * l_ret = dap_global_db_get_all_raw_sync((char*)l_list->data, &l_ret_count);
        if (!l_ret) {
            dap_store_obj_free(l_ret, l_ret_count);
            continue;
        }
        s_get_all_pinned_objs_in_group(l_ret, l_ret_count);
        dap_store_obj_free(l_ret, l_ret_count);
    }   
    return 0;
}

static void s_check_pinned_db_objs_deinit() {
    if (s_check_pinned_db_objs_timer)
        dap_timerfd_delete_mt(s_check_pinned_db_objs_timer->worker, s_check_pinned_db_objs_timer->esocket_uuid);
}


/**
 * @brief s_check_db_version_callback_get
 * @details Notify callback on reading GlobalDB version
 * @param a_errno
 * @param a_group
 * @param a_key
 * @param a_value
 * @param a_value_len
 * @param a_arg
 */
static void s_check_db_version_callback_get (dap_global_db_instance_t *a_dbi, int a_errno, const char * a_group, const char * a_key,
                                             const void * a_value, const size_t a_value_len,
                                             dap_nanotime_t value_ts, bool a_is_pinned, void * a_arg)
{
    int res = 0;
    if (a_errno != 0) { // No DB at all
        log_it(L_NOTICE, "No GlobalDB version at all, creating the new GlobalDB from scratch");
        a_dbi->version = DAP_GLOBAL_DB_VERSION;
        if ( (res = dap_global_db_set(DAP_GLOBAL_DB_LOCAL_GENERAL, "gdb_version",
                                      &a_dbi->version,
                                      sizeof(a_dbi->version), false,
                                      s_check_db_version_callback_set, NULL) ) != 0) {
            log_it(L_NOTICE, "Can't set GlobalDB version, code %d", res);
            goto lb_exit;
        }
        return; // In this case the condition broadcast should happens in s_check_db_version_callback_set()

    }

    if (a_value_len == sizeof(a_dbi->version))
        a_dbi->version = *(uint32_t *)a_value;

    if( a_dbi->version < DAP_GLOBAL_DB_VERSION) {
        log_it(L_NOTICE, "Current GlobalDB version is %u, but %u is required. The current database will be recreated",
               a_dbi->version, DAP_GLOBAL_DB_VERSION);
        dap_global_db_driver_deinit();
        // Database path
        const char *l_storage_path = a_dbi->storage_path;
        // Delete database
        if(dap_file_test(l_storage_path) || dap_dir_test(l_storage_path)) {
            // Backup filename: backup_global_db_ver.X_DATE_TIME.zip
            char l_ts_now_str[255];
            time_t t = time(NULL);
            strftime(l_ts_now_str, 200, "%y.%m.%d-%H_%M_%S", localtime(&t));
#ifdef DAP_BUILD_WITH_ZIP
            char *l_output_file_name = dap_strdup_printf("backup_%s_ver.%d_%s.zip", dap_path_get_basename(l_storage_path), l_gdb_version, now);
            char *l_output_file_path = dap_build_filename(l_storage_path, "../", l_output_file_name, NULL);
            // Create backup as ZIP file
            if(dap_zip_directory(l_storage_path, l_output_file_path)) {
#else
            char *l_output_file_name = dap_strdup_printf("backup_%s_ver.%d_%s.tar",
                                                         dap_path_get_basename(a_dbi->storage_path),
                                                         a_dbi->version, l_ts_now_str);
            char *l_output_file_path = dap_build_filename(l_storage_path, "../", l_output_file_name, NULL);
            // Create backup as TAR file
            if(dap_tar_directory(l_storage_path, l_output_file_path)) {
#endif
                char *l_rm_path = dap_strdup_printf("%s/*", l_storage_path);
                // Delete database file or directory
                dap_rm_rf(l_rm_path);
                DAP_DELETE(l_rm_path);
            }
            else {
                log_it(L_ERROR, "Can't backup GlobalDB version %d", a_dbi->version);
                res = -2;
                goto lb_exit;
            }
            DAP_DELETE(l_output_file_name);
            DAP_DELETE(l_output_file_path);
        }
        // Reinitialize database
        res = dap_global_db_driver_init(s_dbi->driver_name, s_dbi->storage_path);
        // Save current db version
        if(!res) {
            a_dbi->version = DAP_GLOBAL_DB_VERSION;
            if ( (res = dap_global_db_set(DAP_GLOBAL_DB_LOCAL_GENERAL, "gdb_version",
                                          &a_dbi->version,
                                          sizeof(uint32_t), false,
                                          s_check_db_version_callback_set, NULL) ) != 0) {
                log_it(L_NOTICE, "Can't set GlobalDB version, code %d", res);
                goto lb_exit;
            }
            return; // In this case the condition broadcast should happens in s_check_db_version_callback_set()
        }
    } else if(a_dbi->version > DAP_GLOBAL_DB_VERSION) {
        log_it(L_ERROR, "GlobalDB version %d is newer than supported version %d",
                            a_dbi->version, DAP_GLOBAL_DB_VERSION);
        res = -1;
    }
    else {
        log_it(L_NOTICE, "GlobalDB version %d", a_dbi->version);
    }
lb_exit:
    pthread_mutex_lock(&s_check_db_mutex); //    To be sure thats we're on pthread_cond_wait() line
    s_check_db_ret = res;
    pthread_cond_signal(&s_check_db_cond);
    pthread_mutex_unlock(&s_check_db_mutex); //  in calling thread
}

/**
 * @brief s_check_db_version_callback_set
 * @details GlobalDB version update callback
 * @param a_errno
 * @param a_group
 * @param a_key
 * @param a_value
 * @param a_value_len
 * @param a_arg
 */
static void s_check_db_version_callback_set (dap_global_db_instance_t *a_dbi,int a_errno, const char * a_group, const char * a_key,
                                             const void * a_value, const size_t a_value_len,
                                             dap_nanotime_t value_ts, bool a_is_pinned, void * a_arg)
{
    int l_res = 0;
    if(a_errno != 0) {
        log_it(L_ERROR, "Can't process request for DB version, error code %d", a_errno);
        l_res = a_errno;
    } else
        log_it(L_NOTICE, "GlobalDB version updated to %d", a_dbi->version);

    pthread_mutex_lock(&s_check_db_mutex); //  in calling thread
    s_check_db_ret = l_res;
    pthread_cond_signal(&s_check_db_cond);
    pthread_mutex_unlock(&s_check_db_mutex); //  in calling thread
}

/**
 * @brief s_objs_from_store_objs
 * @details convert dap_store_obj_t to dap_global_db_obj_t
 * @param a_store_objs src dap_store_obj_t pointer
 * @param a_values_count count records inarray
 * @return pointer if not error, else NULL
 */

dap_global_db_obj_t *s_objs_from_store_objs(dap_store_obj_t *a_store_objs, size_t a_values_count)
{
    dap_return_val_if_pass(!a_store_objs, NULL);
    
    dap_global_db_obj_t *l_objs = NULL;

    l_objs = DAP_NEW_Z_SIZE(dap_global_db_obj_t, sizeof(dap_global_db_obj_t) *a_values_count);
    if (!l_objs) {
        log_it(L_CRITICAL, "Insufficient memory");
        return NULL;
    }
    for (size_t i = 0; i < a_values_count; i++) {
        if (!dap_global_db_isalnum_group_key(a_store_objs + i, true)) {
            log_it(L_ERROR, "Delete broken object");
            dap_global_db_del(a_store_objs[i].group, a_store_objs[i].key, NULL, NULL);
            continue;
        }
        l_objs[i].is_pinned = a_store_objs[i].flags & DAP_GLOBAL_DB_RECORD_PINNED;
        l_objs[i].key = a_store_objs[i].key;
        l_objs[i].value = a_store_objs[i].value;
        l_objs[i].value_len = a_store_objs[i].value_len;
        l_objs[i].timestamp = a_store_objs[i].timestamp;
        DAP_DELETE(a_store_objs[i].group);
        DAP_DEL_Z(a_store_objs[i].sign);
    }
    DAP_DELETE(a_store_objs);
    return l_objs;
}

bool dap_global_db_isalnum_group_key(const dap_store_obj_t *a_obj, bool a_not_null_key)
{
    dap_return_val_if_fail(a_obj && a_obj->group, false);

    bool ret = true;
    if (a_obj->key) {
        for (char *c = (char*)a_obj->key; *c; ++c) {
            if (!dap_ascii_isprint(*c)) {
                ret = false;
                break;
            }
        }
    } else if (a_not_null_key)
        ret = false;

    if (ret) {
        for (char *c = (char*)a_obj->group; *c; ++c) {
            if (!dap_ascii_isprint(*c)) {
                ret = false;
                break;
            }
        }
    }

    if (!ret) {
        char l_ts[128] = { '\0' };
        dap_nanotime_to_str_rfc822(l_ts, sizeof(l_ts), a_obj->timestamp);
        log_it(L_MSG, "[!] Corrupted object %s (len %zu) : %s (len %zu), ts %s",
               a_obj->group, dap_strlen(a_obj->group), a_obj->key, dap_strlen(a_obj->key), l_ts);
    }
    return ret;
}

