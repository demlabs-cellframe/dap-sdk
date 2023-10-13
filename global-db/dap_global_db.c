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
#include "uthash.h"
#include "dap_common.h"
#include "dap_config.h"
#include "dap_strfuncs.h"
#include "dap_file_utils.h"
#include "dap_time.h"
#include "dap_uuid.h"
#include "dap_context.h"
#include "dap_worker.h"
#include "dap_stream_worker.h"
#include "dap_cert.h"
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
    void *  callback_arg;
    union{
        struct{ // Raw get request
            uint64_t values_raw_last_id;
            uint64_t values_page_size;
        };
        struct{ //Raw set request
            dap_store_obj_t * values_raw;
            uint64_t values_raw_total;
        };
        struct{ //deserialized requests
            // Different variant of message params
            union{
                // values for multile set
                struct{
                    dap_global_db_obj_t * values;
                    size_t values_count;
                };

                // Values for get multiple request
                struct{
                    uint64_t values_last_id; // For multiple records request here stores next request id
                    uint64_t values_total; // Total values
                };

                // Value for singe request
                struct{
                    void *  value;
                    size_t  value_length;
                    bool    value_is_pinned;
                };

            };
            char * group;  // Group
            char * key; // Key
        };
    };
    dap_nanotime_t timestamp;
    dap_global_db_instance_t *dbi;
};

static pthread_cond_t s_check_db_cond = PTHREAD_COND_INITIALIZER; // Check version condition
static pthread_mutex_t s_check_db_mutex = PTHREAD_MUTEX_INITIALIZER; // Check version condition mutex
#define INVALID_RETCODE +100500
static int s_check_db_ret = INVALID_RETCODE; // Check version return value

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

// Opcode to string
static const char *s_msg_opcode_to_str(enum queue_io_msg_opcode a_opcode);

// Queue i/o processing callback
static bool s_queue_io_callback(dap_proc_thread_t *a_proc_thread, void *a_arg);

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
static dap_global_db_obj_t* s_objs_from_store_objs(const dap_store_obj_t *a_store_objs, size_t a_values_count);

/**
 * @brief dap_global_db_init
 * @param a_path
 * @param a_driver
 * @return
 */
int dap_global_db_init(const char * a_storage_path, const char * a_driver_name)
{
    int l_rc = 0;
    static bool s_is_check_version = false;

    if (a_storage_path == NULL) {
        log_it(L_CRITICAL, "Can't initialize GlobalDB without storage path");
        return -1;
    }

    if ( a_driver_name == NULL) {
        log_it(L_CRITICAL, "Can't initialize GlobalDB without driver name");
        return -2;
    }

    // Debug config
    g_dap_global_db_debug_more = dap_config_get_item_bool_default(g_config, "global_db", "debug_more", false);

    // Create and run its own context
    if (s_dbi == NULL) {
        s_dbi = DAP_NEW_Z(dap_global_db_instance_t);
        if (!s_dbi) {
        log_it(L_CRITICAL, "Memory allocation error");
            l_rc = -5;
            goto lb_return;
        }

        s_dbi->storage_path = dap_strdup(a_storage_path);
        s_dbi->driver_name = dap_strdup(a_driver_name);
        dap_cert_t *l_signing_cert = dap_cert_find_by_name("node-addr");
        if (l_signing_cert)
            s_dbi->signing_key = l_signing_cert->enc_key;
        else
            log_it(L_ERROR, "Can't find node addr cerificate, all new records will be usigned");

        uint16_t l_size_ban_list = 0, l_size_white_list = 0;
        char **l_ban_list = dap_config_get_array_str(g_config, "global_db", "ban_list_sync_groups", &l_size_ban_list);
        for (int i = 0; i < l_size_ban_list; i++)
            s_dbi->blacklist = dap_list_append(s_dbi->blacklist, dap_strdup(l_ban_list[i]));
        char **l_white_list = dap_config_get_array_str(g_config, "global_db", "white_list_sync_groups", &l_size_white_list);
        for (int i = 0; i < l_size_ban_list; i++) {
            s_dbi->whitelist = dap_list_append(s_dbi->whitelist, dap_strdup(l_white_list[i]));
            s_dbi->whitelist = dap_list_append(s_dbi->whitelist, dap_strdup_printf("%s" DAP_GLOBAL_DB_DEL_SUFFIX, l_white_list[i]));
        }
        // One year for deleted objects in undefinite objects lifetime
        s_dbi->store_time_limit = dap_config_get_item_uint32_default(g_config, "global_db", "store_time_limit", 365 * 24);
    }

    // Driver initalization
    if( (l_rc = dap_db_driver_init(s_dbi->driver_name,
                                   s_dbi->storage_path, true))  )
        return  log_it(L_CRITICAL, "Hadn't initialized DB driver \"%s\" on path \"%s\", code: %d",
                       s_dbi->driver_name, s_dbi->storage_path, l_rc), l_rc;



    // Check version and update if need it
    if(!s_is_check_version){

        s_is_check_version = true;

        if ( (l_rc = s_check_db_version()) )
            return  log_it(L_ERROR, "GlobalDB version changed, please export or remove old version!"), l_rc;
    }

lb_return:
    if (l_rc == 0 )
        log_it(L_NOTICE, "GlobalDB initialized");
    else
        log_it(L_CRITICAL, "GlobalDB wasn't initialized, code %d", l_rc);

    return l_rc;
}

/**
 * @brief kill context thread and clean context
 */
void dap_global_db_instance_deinit()
{
    dap_return_if_fail(s_dbi)
    dap_list_free_full(s_dbi->blacklist, NULL);
    dap_list_free_full(s_dbi->whitelist, NULL);
    DAP_DEL_Z(s_dbi->driver_name);
    DAP_DEL_Z(s_dbi->storage_path);
    DAP_DEL_Z(s_dbi);
}

inline dap_global_db_instance_t *dap_global_db_instance_get_default()
{
    return s_dbi;
}

/**
 * @brief dap_global_db_deinit, after fix ticket 9030 need add dap_global_db_instance_deinit()
 */
void dap_global_db_deinit() {
    dap_global_db_instance_deinit();
    dap_db_driver_deinit();
    dap_global_db_cluster_deinit();
}

static int s_change_commit_notify(dap_global_db_instance_t *a_dbi, dap_store_obj_t *a_store_obj)
{
    dap_return_val_if_fail(a_store_obj && a_store_obj->group && a_store_obj->key, -1);
    assert(a_store_obj->type == DAP_GLOBAL_DB_OPTYPE_ADD);
    // Delete antinonim in opposite group, if any
    const char l_del_suffix[] = DAP_GLOBAL_DB_DEL_SUFFIX;
    char l_group[DAP_GLOBAL_DB_GROUP_NAME_SIZE_MAX], *l_basic_group = NULL;
    size_t l_group_len = strlen(a_store_obj->group);
    size_t l_unsuffixed_len = l_group_len - sizeof(l_del_suffix) + 1;
    if (l_group_len >= sizeof(l_del_suffix) &&
            !strcmp(l_del_suffix, a_store_obj->group + l_unsuffixed_len)) {
        strncpy(l_group, a_store_obj->group, l_unsuffixed_len);
        l_group[l_unsuffixed_len] = '\0';
        l_basic_group = l_group;
        a_store_obj->type = DAP_GLOBAL_DB_OPTYPE_DEL;
    } else {
        snprintf(l_group, sizeof(l_group) - 1, "%s" DAP_GLOBAL_DB_DEL_SUFFIX, a_store_obj->group);
        l_basic_group = a_store_obj->group;
    }
    dap_store_obj_t store_data = {
        .key        = (char*)a_store_obj->key,
        .group      = l_group
    };
    int l_ret = 0;
    if (dap_global_db_driver_is(store_data.group, store_data.key))
        l_ret = dap_global_db_driver_delete(&store_data, 1);
    else if (a_store_obj->type == DAP_GLOBAL_DB_OPTYPE_DEL) {       // Do not notify for delete if deleted record not exists
        a_store_obj->type = DAP_GLOBAL_DB_OPTYPE_ADD;
        return l_ret;
    }

    dap_global_db_cluster_t *l_cluster = dap_global_db_cluster_by_group(a_dbi, l_basic_group);
    if (l_cluster) {
        // Notify sync cluster first
        dap_global_db_cluster_broadcast(l_cluster, a_store_obj);
        if (l_cluster->callback_notify) {
            // Notify others in user space format
            char *l_old_group_ptr = a_store_obj->group;
            a_store_obj->group = l_basic_group;
            dap_global_db_cluster_notify(l_cluster, a_store_obj);
            a_store_obj->group = l_old_group_ptr;
            a_store_obj->type = DAP_GLOBAL_DB_OPTYPE_ADD;
        }
    }
    return l_ret;
}

/* *** Get functions group *** */

byte_t *dap_global_db_get_sync(const char *a_group,
                                 const char *a_key, size_t *a_data_size,
                                 bool *a_is_pinned, dap_nanotime_t *a_ts)
{

    debug_if(g_dap_global_db_debug_more, L_DEBUG, "get call executes for group \"%s\" and key \"%s\"", a_group, a_key);
    dap_store_obj_t *l_store_obj = dap_global_db_get_raw_sync(a_group, a_key);
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
int dap_global_db_get(const char * a_group, const char *a_key, dap_global_db_callback_result_t a_callback, void * a_arg )
{
    if(s_dbi == NULL){
        log_it(L_ERROR, "GlobalDB context is not initialized, can't call dap_global_db_get");
        return DAP_GLOBAL_DB_RC_ERROR;
    }
    struct queue_io_msg * l_msg = DAP_NEW_Z(struct queue_io_msg);
    if (!l_msg) {
        log_it(L_CRITICAL, "Memory allocation error");
        return -1;
    }
    l_msg->dbi = s_dbi;
    l_msg->opcode = MSG_OPCODE_GET;
    l_msg->group = dap_strdup(a_group);
    l_msg->key = dap_strdup(a_key);
    l_msg->callback_result = a_callback;
    l_msg->callback_arg = a_arg;

    int l_ret = dap_proc_thread_callback_add(NULL, s_queue_io_callback, l_msg);
    if (l_ret != 0){
        log_it(L_ERROR, "Can't exec get request, code %d", l_ret);
        s_queue_io_msg_delete(l_msg);
    }else
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
    }else if(a_msg->callback_result)
        a_msg->callback_result(a_msg->dbi, DAP_GLOBAL_DB_RC_NO_RESULTS, a_msg->group, a_msg->key,
                               NULL, 0, 0,0, a_msg->callback_arg);
}

/* *** Get raw functions group *** */

dap_store_obj_t *dap_global_db_get_raw_sync(const char *a_group, const char *a_key)
{
    size_t l_count_records = 0;
    dap_store_obj_t *l_res = dap_global_db_driver_read(a_group, a_key, &l_count_records);
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
    if(s_dbi == NULL){
        log_it(L_ERROR, "GlobalDB context is not initialized, can't call dap_global_db_get");
        return DAP_GLOBAL_DB_RC_ERROR;
    }
    struct queue_io_msg * l_msg = DAP_NEW_Z(struct queue_io_msg);
    if (!l_msg) {
        log_it(L_CRITICAL, "Memory allocation error");
        return -1;
    }
    l_msg->dbi = s_dbi;
    l_msg->opcode = MSG_OPCODE_GET_RAW;
    l_msg->group = dap_strdup(a_group);
    l_msg->key = dap_strdup(a_key);
    l_msg->callback_result_raw = a_callback;
    l_msg->callback_arg = a_arg;

    int l_ret = dap_proc_thread_callback_add(NULL, s_queue_io_callback, l_msg);
    if (l_ret != 0){
        log_it(L_ERROR, "Can't exec get request, code %d", l_ret);
        s_queue_io_msg_delete(l_msg);
    }else
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
    dap_store_obj_t *l_store_obj_del = NULL;
    dap_nanotime_t l_timestamp = 0;
    char l_group[DAP_GLOBAL_DB_GROUP_NAME_SIZE_MAX];

    if (a_key && a_group) {
        snprintf(l_group, sizeof(l_group) - 1,  "%s" DAP_GLOBAL_DB_DEL_SUFFIX, a_group);
        if (dap_global_db_driver_is(l_group, a_key)) {
            l_store_obj_del = dap_global_db_get_raw_sync(l_group, a_key);
            if (l_store_obj_del) {
                l_timestamp = l_store_obj_del->timestamp;
                dap_store_obj_free_one(l_store_obj_del);
            }
        }
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
int dap_global_db_get_del_ts(const char *a_group, const char *a_key,dap_global_db_callback_result_t a_callback, void *a_arg)
{
    if(s_dbi == NULL){
        log_it(L_ERROR, "GlobalDB context is not initialized, can't call dap_global_db_get");
        return DAP_GLOBAL_DB_RC_ERROR;
    }
    struct queue_io_msg * l_msg = DAP_NEW_Z(struct queue_io_msg);
    if (!l_msg) {
        log_it(L_CRITICAL, "Memory allocation error");
        return -1;
    }
    l_msg->dbi = s_dbi;
    l_msg->opcode = MSG_OPCODE_GET_DEL_TS;
    l_msg->group = dap_strdup(a_group);
    l_msg->key = dap_strdup(a_key);
    l_msg->callback_result = a_callback;
    l_msg->callback_arg = a_arg;

    int l_ret = dap_proc_thread_callback_add(NULL, s_queue_io_callback, l_msg);
    if (l_ret != 0){
        log_it(L_ERROR, "Can't exec get_del_ts request, code %d", l_ret);
        s_queue_io_msg_delete(l_msg);
    }else
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
    if(l_timestamp){
        if(a_msg->callback_result)
            a_msg->callback_result(a_msg->dbi, DAP_GLOBAL_DB_RC_SUCCESS, a_msg->group, a_msg->key,
                               NULL, 0, l_timestamp,
                               false, a_msg->callback_arg );
    }else if(a_msg->callback_result)
        a_msg->callback_result(a_msg->dbi, DAP_GLOBAL_DB_RC_NO_RESULTS, a_msg->group, a_msg->key,
                               NULL, 0, 0,0, a_msg->callback_arg );
}

/* *** Get_last functions group *** */

byte_t *dap_global_db_get_last_sync(const char *a_group, char **a_key, size_t *a_data_size,
                                      bool *a_is_pinned, dap_nanotime_t *a_ts)
{
    dap_store_obj_t *l_store_obj = dap_global_db_get_last_raw_sync(a_group);
    if (!l_store_obj) {
        log_it(L_ERROR, "l_store_ibj is not initialized, can't call dap_global_db_get_last_unsafe");
        return NULL;
    }
    if (a_key)
        *a_key = dap_strdup(l_store_obj->key);
    if (a_data_size)
        *a_data_size = l_store_obj->value_len;
    if (a_is_pinned)
        *a_is_pinned = l_store_obj->flags & DAP_GLOBAL_DB_RECORD_PINNED;
    if (a_ts)
        *a_ts = l_store_obj->timestamp;
    byte_t *l_res = DAP_DUP_SIZE(l_store_obj->value, l_store_obj->value_len);
    if (!l_res) {
        log_it(L_CRITICAL, "Memory allocation error");
        dap_store_obj_free_one(l_store_obj);
        return NULL;
    }
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
int dap_global_db_get_last(const char * a_group, dap_global_db_callback_result_t a_callback, void * a_arg )
{
    if(s_dbi == NULL){
        log_it(L_ERROR, "GlobalDB context is not initialized, can't call dap_global_db_get_last");
        return DAP_GLOBAL_DB_RC_ERROR;
    }
    struct queue_io_msg * l_msg = DAP_NEW_Z(struct queue_io_msg);
    if (!l_msg) {
        log_it(L_CRITICAL, "Memory allocation error");
        return -1;
    }
    l_msg->dbi = s_dbi;
    l_msg->opcode = MSG_OPCODE_GET_LAST;
    l_msg->group = dap_strdup(a_group);
    l_msg->callback_arg = a_arg;
    l_msg->callback_result = a_callback;

    int l_ret = dap_proc_thread_callback_add(NULL, s_queue_io_callback, l_msg);
    if (l_ret != 0){
        log_it(L_ERROR, "Can't exec get_last request, code %d", l_ret);
        s_queue_io_msg_delete(l_msg);
    }else
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
    }else if(a_msg->callback_result)
        a_msg->callback_result(a_msg->dbi, DAP_GLOBAL_DB_RC_NO_RESULTS, a_msg->group, l_key,
                               NULL, 0, 0,0, a_msg->callback_arg );
}

/* *** Get_last_raw functions group *** */

dap_store_obj_t *dap_global_db_get_last_raw_sync(const char *a_group)
{
    dap_store_obj_t *l_ret = dap_global_db_driver_read_last(a_group);
    return l_ret;
}

/**
 * @brief dap_global_db_get_last_raw
 * @param a_group
 * @param a_callback
 * @param a_arg
 * @return
 */
int dap_global_db_get_last_raw(const char * a_group, dap_global_db_callback_result_raw_t a_callback, void * a_arg )
{
    if(s_dbi == NULL){
        log_it(L_ERROR, "GlobalDB context is not initialized, can't call dap_global_db_get_last");
        return DAP_GLOBAL_DB_RC_ERROR;
    }
    struct queue_io_msg * l_msg = DAP_NEW_Z(struct queue_io_msg);
    if (!l_msg) {
        log_it(L_CRITICAL, "Memory allocation error");
        return -1;
    }
    l_msg->dbi = s_dbi;
    l_msg->opcode = MSG_OPCODE_GET_LAST_RAW;
    l_msg->group = dap_strdup(a_group);
    l_msg->callback_arg = a_arg;
    l_msg->callback_result_raw = a_callback;

    int l_ret = dap_proc_thread_callback_add(NULL, s_queue_io_callback, l_msg);
    if (l_ret != 0){
        log_it(L_ERROR, "Can't exec get_last request, code %d", l_ret);
        s_queue_io_msg_delete(l_msg);
    }else
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

static int s_db_compare_by_ts(const void *a_obj1, const void *a_obj2) {
    dap_global_db_obj_t *l_obj1 = (dap_global_db_obj_t *)a_obj1,
            *l_obj2 = (dap_global_db_obj_t *)a_obj2;
    return l_obj2->timestamp < l_obj1->timestamp
            ? 1
            : l_obj2->timestamp > l_obj1->timestamp
              ? -1
              : 0; // should never occur...
}

dap_global_db_obj_t *dap_global_db_get_all_sync(const char *a_group, size_t *a_objs_count)
{
    size_t l_values_count = 0;
    dap_store_obj_t *l_store_objs = dap_global_db_driver_read(a_group, 0, &l_values_count);
    debug_if(g_dap_global_db_debug_more, L_DEBUG, "Get all request from group %s recieved %zu values",
                                                                            a_group, l_values_count);
    dap_global_db_obj_t *l_objs = s_objs_from_store_objs(l_store_objs, l_values_count);
    if (l_values_count > 1)
        qsort(l_objs, l_values_count, sizeof(dap_global_db_obj_t), s_db_compare_by_ts);
    if (a_objs_count)
        *a_objs_count = i;
    return l_objs;
mem_clear:
        log_it(L_CRITICAL, "Memory allocation error");
    for(size_t j = 0; j < l_values_count && l_objs; j++) {
        DAP_DEL_Z(l_objs[j].key);
        DAP_DEL_Z(l_objs[j].value);
    }
    DAP_DEL_Z(l_objs);
    dap_store_obj_free(l_store_objs, l_values_count);
    return NULL;
}

/**
 * @brief dap_global_db_get_all Get all records from the group
 * @param a_group
 * @param a_results_page_size
 * @param a_callback
 * @param a_arg
 * @return
 */
int dap_global_db_get_all(const char * a_group, size_t a_results_page_size, dap_global_db_callback_results_t a_callback, void * a_arg)
{
    // TODO make usable a_results_page_size
    if(s_dbi == NULL){
        log_it(L_ERROR, "GlobalDB context is not initialized, can't call dap_global_db_get_all");
        return DAP_GLOBAL_DB_RC_ERROR;
    }

    int l_ret = 0;

    struct queue_io_msg * l_msg = DAP_NEW_Z(struct queue_io_msg);
    if (!l_msg) {
        log_it(L_CRITICAL, "Memory allocation error");
        return l_ret;
    }
    l_msg->dbi = s_dbi;
    l_msg->opcode = MSG_OPCODE_GET_ALL;
    l_msg->group = dap_strdup(a_group);
    l_msg->callback_arg = a_arg;
    l_msg->callback_results = a_callback;
    l_msg->values_page_size = a_results_page_size;
    l_msg->timestamp = 0;

    l_ret = dap_proc_thread_callback_add(NULL, s_queue_io_callback, l_msg);

    if (l_ret != 0){
        log_it(L_ERROR, "Can't exec get_all request, code %d", l_ret);
        s_queue_io_msg_delete(l_msg);
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

    dap_global_db_iter_t *l_iter = dap_global_db_driver_iter_create(a_msg->group);
    if (!l_iter) {
        log_it(L_ERROR, "Iterator creation error");
        return false;
    }

    bool l_ret = false;
    size_t l_values_count = a_msg->values_page_size;
    dap_global_db_obj_t *l_objs= NULL;
    dap_store_obj_t *l_store_objs = NULL;

    size_t l_total_records = dap_global_db_driver_count(a_msg->group, 0);
    if (a_msg->values_page_size >= l_total_records || !a_msg->values_page_size) {
        l_objs = dap_global_db_get_all_sync(a_msg->group, &l_values_count);
        if (a_msg->callback_results)
            l_ret = a_msg->callback_results(a_msg->dbi,
                                l_objs ? DAP_GLOBAL_DB_RC_SUCCESS : DAP_GLOBAL_DB_RC_NO_RESULTS,
                                a_msg->group, l_values_count, l_values_count,
                                l_objs, a_msg->callback_arg);
        dap_global_db_objs_delete(l_objs, l_values_count);
    } else {
        for (size_t i = 0; (i < l_total_records) && l_ret; i += l_values_count) {
            l_values_count = i + a_msg->values_page_size < l_total_records ? a_msg->values_page_size : l_total_records - i;
            l_store_objs = dap_global_db_driver_cond_read(l_iter, &l_values_count, 0);

            l_objs = s_objs_from_store_objs(l_store_objs, l_values_count);
           
                // Call callback if present
            if (a_msg->callback_results)
                l_ret = a_msg->callback_results(a_msg->dbi,
                                l_objs ? DAP_GLOBAL_DB_RC_SUCCESS : DAP_GLOBAL_DB_RC_NO_RESULTS,
                                a_msg->group, l_total_records, l_values_count,
                                l_objs, a_msg->callback_arg);
            dap_global_db_objs_delete(l_objs, l_values_count);
        }
    }
    return l_ret; // All values are sent
}

/* *** Get_all_raw functions group *** */

dap_store_obj_t *dap_global_db_get_all_raw_sync(const char* a_group, size_t *a_objs_count)
{
    dap_return_val_if_fail(a_group, NULL);
    
    size_t l_values_count = 0;
    dap_store_obj_t *l_store_objs = dap_global_db_driver_read(a_group, 0, &l_values_count);
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
int dap_global_db_get_all_raw(const char * a_group, size_t a_results_page_size, dap_nanotime_t a_timestamp,
                              dap_global_db_callback_results_raw_t a_callback, void * a_arg)
{
    // TODO make usable a_results_page_size
    if (!a_group) {
        log_it(L_ERROR, "Empty db iterator");
        return DAP_GLOBAL_DB_RC_ERROR;
    }
    if(s_dbi == NULL){
        log_it(L_ERROR, "GlobalDB context is not initialized, can't call dap_global_db_get_all");
        return DAP_GLOBAL_DB_RC_ERROR;
    }
    struct queue_io_msg * l_msg = DAP_NEW_Z(struct queue_io_msg);
    if (!l_msg) {
        log_it(L_CRITICAL, "Memory allocation error");
        return -1;
    }
    l_msg->dbi = s_dbi;
    l_msg->opcode = MSG_OPCODE_GET_ALL_RAW ;
    l_msg->group = dap_strdup(a_group);
    l_msg->values_raw_last_id = 0;
    l_msg->values_page_size = a_results_page_size;
    l_msg->callback_arg = a_arg;
    l_msg->callback_results_raw = a_callback;
    l_msg->timestamp = a_timestamp;

    int l_ret = dap_proc_thread_callback_add(NULL, s_queue_io_callback, l_msg);
    if (l_ret != 0){
        log_it(L_ERROR, "Can't exec get_all_raw request, code %d", l_ret);
        s_queue_io_msg_delete(l_msg);
    }else
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

    dap_global_db_iter_t *l_iter = dap_global_db_driver_iter_create(a_msg->group);
    if (!l_iter) {
        log_it(L_ERROR, "Iterator creation error");
        return false;
    }

    bool l_ret = false;
    size_t l_values_count = a_msg->values_page_size;
    dap_store_obj_t *l_store_objs = NULL;
    dap_nanotime_t l_timestamp = a_msg->timestamp;

    size_t l_total_records = dap_global_db_driver_count(a_msg->group, l_timestamp);
    if (a_msg->values_page_size >= l_total_records || !a_msg->values_page_size) {
        l_store_objs = dap_global_db_get_all_raw_sync(a_msg->group, &l_values_count);
        if (a_msg->callback_results)
            l_ret = a_msg->callback_results_raw(s_dbi,
                                l_store_objs ? DAP_GLOBAL_DB_RC_SUCCESS : DAP_GLOBAL_DB_RC_NO_RESULTS,
                                a_msg->group, l_total_records, l_values_count,
                                l_store_objs, a_msg->callback_arg);
        dap_store_obj_free(l_store_objs, l_values_count);
    } else {
        for (size_t i = 0; (i < l_total_records) && l_ret; i += a_msg->values_page_size) {
            l_values_count = i + a_msg->values_page_size < l_total_records ? a_msg->values_page_size : l_total_records - i;
            l_store_objs = dap_global_db_driver_cond_read(l_iter, &l_values_count, l_timestamp);
            // Call callback if present
            if (a_msg->callback_results)
                l_ret = a_msg->callback_results_raw(s_dbi,
                                l_store_objs ? DAP_GLOBAL_DB_RC_SUCCESS : DAP_GLOBAL_DB_RC_NO_RESULTS,
                                a_msg->group, l_total_records, l_values_count,
                                l_store_objs, a_msg->callback_arg);
            dap_store_obj_free(l_store_objs, l_values_count);
        }
    }
    return l_ret; // All values are sent
}

static int s_set_sync_with_ts(dap_global_db_instance_t *a_dbi, const char *a_group, const char *a_key, const void *a_value,
                             const size_t a_value_length, bool a_pin_value, dap_nanotime_t a_timestamp)
{
    if (!a_group || !a_key) {
        log_it(L_WARNING, "Trying to set GDB object with NULL group or key param");
        return -1;
    }

    dap_store_obj_t l_store_data = { 0 };
    l_store_data.type = DAP_GLOBAL_DB_OPTYPE_ADD;
    l_store_data.key = (char *)a_key ;
    l_store_data.flags = a_pin_value ? DAP_GLOBAL_DB_RECORD_PINNED : 0 ;
    l_store_data.value_len =  a_value_length;
    l_store_data.value = (uint8_t *)a_value;
    l_store_data.group = (char *)a_group;
    l_store_data.timestamp = a_timestamp;
    l_store_data.sign = dap_store_obj_sign(&l_store_data, a_dbi->signing_key, &l_store_data.crc);
    if (!l_store_data.sign) {
        log_it(L_ERROR, "Can't sign new global DB object group %s key %s", a_group, a_key);
        return -2;
    }

    int l_res = dap_global_db_driver_apply(&l_store_data, 1);
    if (l_res == 0)
        s_change_commit_notify(a_dbi, &l_store_data);
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
int dap_global_db_set_sync(const char * a_group, const char *a_key, const void * a_value, const size_t a_value_length, bool a_pin_value)
{
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
int dap_global_db_set(const char * a_group, const char *a_key, const void * a_value, const size_t a_value_length, bool a_pin_value, dap_global_db_callback_result_t a_callback, void * a_arg )
{
    if(s_dbi == NULL){
        log_it(L_ERROR, "GlobalDB context is not initialized, can't call dap_global_db_set");
        return DAP_GLOBAL_DB_RC_ERROR;
    }
    if (!a_group || !a_key) {
        log_it(L_WARNING, "Trying to set GDB object with NULL group or key param");
        return -1;
    }
    struct queue_io_msg * l_msg = DAP_NEW_Z(struct queue_io_msg);
    if (!l_msg) {
        log_it(L_CRITICAL, "Memory allocation error");
        return -1;
    }
    l_msg->dbi = s_dbi;
    l_msg->opcode = MSG_OPCODE_SET;
    l_msg->group = dap_strdup(a_group);
    l_msg->key = dap_strdup(a_key);
    l_msg->value = DAP_DUP_SIZE(a_value, a_value_length);
    if (!l_msg->value && a_value) {
        log_it(L_CRITICAL, "Memory allocation error");
        DAP_DEL_Z(l_msg->group);
        DAP_DEL_Z(l_msg->key);
        DAP_DEL_Z(l_msg);
        return -1;
    }
    l_msg->value_length = a_value_length;
    l_msg->value_is_pinned = a_pin_value;
    l_msg->callback_arg = a_arg;
    l_msg->callback_result = a_callback;

    int l_ret = dap_proc_thread_callback_add(NULL, s_queue_io_callback, l_msg);
    if (l_ret != 0){
        log_it(L_ERROR, "Can't exec set request, code %d", l_ret);
        s_queue_io_msg_delete(l_msg);
    }else
        debug_if(g_dap_global_db_debug_more, L_DEBUG, "Have sent sent request for \"%s\" group \"%s\" key" , a_group, a_key);

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
    if (l_res == 0) {
        if(a_msg->callback_result)
            a_msg->callback_result(a_msg->dbi, DAP_GLOBAL_DB_RC_SUCCESS, a_msg->group, a_msg->key,
                                   a_msg->value, a_msg->value_length, l_ts_now,
                                   a_msg->value_is_pinned, a_msg->callback_arg);
    } else {
        log_it(L_ERROR, "Save error for %s:%s code %d", a_msg->group,a_msg->key, l_res);
        if(a_msg->callback_result)
            a_msg->callback_result(a_msg->dbi, DAP_GLOBAL_DB_RC_ERROR , a_msg->group, a_msg->key,
                                   a_msg->value, a_msg->value_length, l_ts_now,
                                   a_msg->value_is_pinned, a_msg->callback_arg);
    }
}

/* *** Set_raw functions group *** */

int s_db_set_raw_sync(dap_global_db_instance_t *a_dbi, dap_store_obj_t *a_store_objs, size_t a_store_objs_count)
{
    int l_ret = -1;
    for (size_t i = 0; i < a_store_objs_count; i++) {
        dap_store_obj_t *l_obj = a_store_objs + i;
        char *l_group_saved = NULL;
        if (l_obj->type != DAP_GLOBAL_DB_OPTYPE_ADD) {
            // Oh, it's compability code for old sync protocol
            l_group_saved = l_obj->group;
            l_obj->group = dap_strdup_printf("%s" DAP_GLOBAL_DB_DEL_SUFFIX, l_obj->group);
            l_obj->type = DAP_GLOBAL_DB_OPTYPE_ADD;
        }
        l_ret = dap_global_db_driver_add(l_obj, 1);
        if (l_ret)
            s_change_commit_notify(a_dbi, l_obj);
        else
            log_it(L_ERROR, "Can't save raw gdb data, code %d ", l_ret);
        if (l_group_saved) {
            l_obj->type = DAP_GLOBAL_DB_OPTYPE_DEL;
            DAP_DELETE(l_obj->group);
            l_obj->group = l_group_saved;
        }
    }
    return l_ret;
}

int dap_global_db_set_raw_sync(dap_store_obj_t *a_store_objs, size_t a_store_objs_count)
{
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
int dap_global_db_set_raw(dap_store_obj_t *a_store_objs, size_t a_store_objs_count, dap_global_db_callback_results_raw_t a_callback, void * a_arg )
{
    if(s_dbi == NULL){
        log_it(L_ERROR, "GlobalDB context is not initialized, can't call dap_global_db_set");
        return DAP_GLOBAL_DB_RC_ERROR;
    }
    struct queue_io_msg * l_msg = DAP_NEW_Z(struct queue_io_msg);
    if (!l_msg) {
        log_it(L_CRITICAL, "Memory allocation error");
        return -1;
    }
    l_msg->dbi = s_dbi;
    l_msg->opcode = MSG_OPCODE_SET_RAW;
    l_msg->callback_arg = a_arg;
    l_msg->callback_results_raw = a_callback;

    l_msg->values_raw = dap_store_obj_copy(a_store_objs, a_store_objs_count) ;
    l_msg->values_raw_total = a_store_objs_count;

    int l_ret = dap_proc_thread_callback_add(NULL, s_queue_io_callback, l_msg);
    if (l_ret != 0){
        log_it(L_ERROR, "Can't exec set_raw request, code %d", l_ret);
        s_queue_io_msg_delete(l_msg);
    }else
        debug_if(g_dap_global_db_debug_more, L_DEBUG, "Have sent set_raw request for %zu objects" , a_store_objs_count);
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
int dap_global_db_set_multiple_zc(const char * a_group, dap_global_db_obj_t * a_values, size_t a_values_count, dap_global_db_callback_results_t a_callback, void * a_arg )
{
    if(s_dbi == NULL){
        log_it(L_ERROR, "GlobalDB context is not initialized, can't call dap_global_db_set");
        return DAP_GLOBAL_DB_RC_ERROR;
    }
    struct queue_io_msg * l_msg = DAP_NEW_Z(struct queue_io_msg);
    if (!l_msg) {
        log_it(L_CRITICAL, "Memory allocation error");
        return -1;
    }
    l_msg->dbi = s_dbi;
    l_msg->opcode = MSG_OPCODE_SET_MULTIPLE;
    l_msg->group = dap_strdup(a_group);
    l_msg->values = a_values;
    l_msg->values_count = a_values_count;
    l_msg->callback_arg = a_arg;
    l_msg->callback_results = a_callback;

    int l_ret = dap_proc_thread_callback_add(NULL, s_queue_io_callback, l_msg);
    if (l_ret != 0){
        log_it(L_ERROR, "Can't exec set_multiple request, code %d", l_ret);
        s_queue_io_msg_delete(l_msg);
    }else
        debug_if(g_dap_global_db_debug_more, L_DEBUG, "Have sent set_multiple request for \"%s\" group with %zu values" , a_group, a_values_count);
    return l_ret;
}

/**
 * @brief s_msg_opcode_set_multiple
 * @param a_msg
 * @return
 */
static void s_msg_opcode_set_multiple_zc(struct queue_io_msg * a_msg)
{
    int l_ret = -1;
    size_t i=0;
    if(a_msg->values_count>0){
        dap_store_obj_t l_store_obj = {};
        l_ret = 0;
        for(;  i < a_msg->values_count && l_ret == 0  ; i++ ) {
            l_store_obj.type = DAP_GLOBAL_DB_OPTYPE_ADD;
            l_store_obj.flags = a_msg->values[i].is_pinned;
            l_store_obj.key =  a_msg->values[i].key;
            l_store_obj.group = a_msg->group;
            l_store_obj.value = a_msg->values[i].value;
            l_store_obj.value_len = a_msg->values[i].value_len;
            l_store_obj.timestamp = a_msg->values[i].timestamp;
            l_ret = dap_global_db_driver_add(&l_store_obj,1);
            if (!l_ret)
                s_change_commit_notify(s_dbi, &l_store_obj);
        }
    }
    if(a_msg->callback_results){
        a_msg->callback_results(a_msg->dbi,
                                l_ret == 0 ? DAP_GLOBAL_DB_RC_SUCCESS : DAP_GLOBAL_DB_RC_ERROR,
                                a_msg->group, i, a_msg->values_count,
                                a_msg->values, a_msg->callback_arg);
    }
    dap_global_db_objs_delete( a_msg->values, a_msg->values_count);
}

/* *** Pin/unpin functions group *** */

int s_db_object_pin_sync(dap_global_db_instance_t *a_dbi, const char *a_group, const char *a_key, bool a_pin)
{
    int l_res = DAP_GLOBAL_DB_RC_NO_RESULTS;
    dap_store_obj_t *l_store_obj = dap_global_db_get_raw_sync(a_group, a_key);
    if (l_store_obj) {
        if (a_pin)
            l_store_obj->flags |= DAP_GLOBAL_DB_RECORD_PINNED;
        else
            l_store_obj->flags ^= DAP_GLOBAL_DB_RECORD_PINNED;
        l_store_obj->type = DAP_GLOBAL_DB_OPTYPE_ADD;
        l_res = dap_global_db_set_raw_sync(l_store_obj, 1);
        if (!l_res)
            s_change_commit_notify(a_dbi, l_store_obj);
        else {
            log_it(L_ERROR,"Can't save pinned gdb data, code %d ", l_res);
            l_res = DAP_GLOBAL_DB_RC_ERROR;
        }
    }
    dap_store_obj_free_one(l_store_obj);
    return l_res;
}

int dap_global_db_pin_sync(const char *a_group, const char *a_key)
{
    return s_db_object_pin_sync(s_dbi, a_group, a_key, true);
}

int dap_global_db_unpin_sync(const char *a_group, const char *a_key)
{
    return s_db_object_pin_sync(s_dbi, a_group, a_key, false);
}

int s_db_object_pin(const char *a_group, const char *a_key, dap_global_db_callback_result_t a_callback, void *a_arg, bool a_pin)
{
    if(s_dbi == NULL){
        log_it(L_ERROR, "GlobalDB context is not initialized, can't call dap_global_db_pin");
        return DAP_GLOBAL_DB_RC_ERROR;
    }
    struct queue_io_msg * l_msg = DAP_NEW_Z(struct queue_io_msg);
    if (!l_msg) {
        log_it(L_CRITICAL, "Memory allocation error");
        return -1;
    }
    l_msg->dbi = s_dbi;
    l_msg->opcode = MSG_OPCODE_PIN;
    l_msg->group = dap_strdup(a_group);
    l_msg->key = dap_strdup(a_key);
    l_msg->callback_arg = a_arg;
    l_msg->callback_result = a_callback;
    l_msg->value_is_pinned = a_pin;

    int l_ret = dap_proc_thread_callback_add(NULL, s_queue_io_callback, l_msg);
    if (l_ret != 0){
        log_it(L_ERROR, "Can't exec %s request, code %d", a_pin ? "pin" : "unpin", l_ret);
        s_queue_io_msg_delete(l_msg);
    }else
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
        .key        = a_key,
        .group      = dap_strdup_printf("%s" DAP_GLOBAL_DB_DEL_SUFFIX, a_group),
        .type       = a_key ? DAP_GLOBAL_DB_OPTYPE_ADD : DAP_GLOBAL_DB_OPTYPE_DEL,
        .timestamp  = dap_nanotime_now()
    };
    if (a_key)
        l_store_obj.sign = dap_store_obj_sign(&l_store_obj, a_dbi->signing_key, &l_store_obj.crc);

    int l_res = dap_global_db_driver_apply(&l_store_obj, 1);
    if (a_key) {
        if (l_res)
            s_change_commit_notify(a_dbi, &l_store_obj);
    } else {
        // Drop main table too
        l_store_obj.group[dap_strlen(a_group)] = '\0';
        l_res = dap_global_db_driver_apply(&l_store_obj, 1);
    }
    DAP_DELETE(l_store_obj.group);
    return l_res;
}

inline int dap_global_db_del_sync(const char *a_group, const char *a_key)
{
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
int dap_global_db_del(const char * a_group, const char *a_key, dap_global_db_callback_result_t a_callback, void * a_arg )
{
    if (s_dbi == NULL) {
        log_it(L_ERROR, "GlobalDB context is not initialized, can't call dap_global_db_del");
        return DAP_GLOBAL_DB_RC_ERROR;
    }
    struct queue_io_msg * l_msg = DAP_NEW_Z(struct queue_io_msg);
    if (!l_msg) {
        log_it(L_CRITICAL, "Memory allocation error");
        return -1;
    }
    l_msg->dbi = s_dbi;
    l_msg->opcode = MSG_OPCODE_DELETE;
    l_msg->group = dap_strdup(a_group);
    l_msg->key = dap_strdup(a_key);
    l_msg->callback_arg = a_arg;
    l_msg->callback_result = a_callback;

    int l_ret = dap_proc_thread_callback_add(NULL, s_queue_io_callback, l_msg);
    if (l_ret != 0){
        log_it(L_ERROR, "Can't exec del request, code %d", l_ret);
        s_queue_io_msg_delete(l_msg);
    }else
        debug_if(g_dap_global_db_debug_more, L_DEBUG, "Have sent del request for \"%s\" group \"%s\" key" , a_group, a_key);

    return l_ret;
}

/**
 * @brief s_msg_opcode_delete
 * @param a_msg
 * @return
 */
static void s_msg_opcode_delete(struct queue_io_msg * a_msg)
{
    int l_res = dap_global_db_del_sync(a_msg->group, a_msg->key);

    if(a_msg->callback_result){
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
    return dap_db_driver_flush();
}

/**
 * @brief dap_global_db_flush
 * @param a_callback
 * @param a_arg
 * @return
 */
int dap_global_db_flush(dap_global_db_callback_result_t a_callback, void * a_arg)
{
    if(s_dbi == NULL){
        log_it(L_ERROR, "GlobalDB context is not initialized, can't call dap_global_db_delete");
        return DAP_GLOBAL_DB_RC_ERROR;
    }
    struct queue_io_msg * l_msg = DAP_NEW_Z(struct queue_io_msg);
    if (!l_msg) {
        log_it(L_CRITICAL, "Memory allocation error");
        return -1;
    }
    l_msg->dbi = s_dbi;
    l_msg->opcode = MSG_OPCODE_FLUSH;
    l_msg->callback_arg = a_arg;
    l_msg->callback_result = a_callback;

    int l_ret = dap_proc_thread_callback_add(NULL, s_queue_io_callback, l_msg);
    if (l_ret != 0){
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
 * @param a_objs_dest a pointer to the first destination object of the array
 * @param objs a pointer to the first source object of the array
 * @param a_count a number of objects in the array
 * @return (none)
 */
dap_global_db_obj_t *dap_global_db_objs_copy(dap_global_db_obj_t *a_objs_dest, const dap_global_db_obj_t *a_objs_src, size_t a_count)
{   /* Sanity checks */
    dap_return_val_if_pass(!a_objs_dest || !a_objs_src || !a_count, NULL);

    /* Run over array's elements */
    const dap_global_db_obj_t *l_obj = a_objs_src;
    for (dap_global_db_obj_t *l_cur = a_objs_dest; a_count--; l_cur++, l_obj++) {
        *l_cur = *l_obj;
        l_cur->key = dap_strdup(l_obj->key);
        if (l_obj->value) {
            if (l_obj->value_len)
                l_cur->value = DAP_DUP_SIZE(l_obj->value, l_obj->value_len);
            else
                log_it(L_WARNING, "Inconsistent global DB object copy requested");
        }
    }
    return a_objs_dest;
}

/**
 * @brief Deallocates memory of an objs array.
 * @param objs a pointer to the first object of the array
 * @param a_count a number of objects in the array
 * @return (none)
 */
void dap_global_db_objs_delete(dap_global_db_obj_t *a_objs, size_t a_count)
{
dap_global_db_obj_t *l_obj;

    if ( !a_objs || !a_count )                                              /* Sanity checks */
        return;

    for(l_obj = a_objs; a_count--; l_obj++) {                               /* Run over array's elements */
        DAP_DEL_Z(l_obj->key);
        DAP_DEL_Z(l_obj->value);
    }

    DAP_DELETE(a_objs);                                                     /* Finaly kill the the array */
}

/**
 * @brief s_msg_opcode_to_str
 * @param a_opcode
 * @return
 */
static const char *s_msg_opcode_to_str(enum queue_io_msg_opcode a_opcode)
{
    switch(a_opcode){
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
static bool s_queue_io_callback(dap_proc_thread_t UNUSED_ARG *a_thread, void * a_arg)
{
    struct queue_io_msg * l_msg = (struct queue_io_msg *) a_arg;
    assert(l_msg);

    debug_if(g_dap_global_db_debug_more, L_NOTICE, "Received GlobalDB I/O message with opcode %s", s_msg_opcode_to_str(l_msg->opcode) );

    switch(l_msg->opcode){
        case MSG_OPCODE_GET:            s_msg_opcode_get(l_msg); break;
        case MSG_OPCODE_GET_RAW:        s_msg_opcode_get_raw(l_msg); break;
        case MSG_OPCODE_GET_LAST:       s_msg_opcode_get_last(l_msg); break;
        case MSG_OPCODE_GET_LAST_RAW:   s_msg_opcode_get_last_raw(l_msg); break;
        case MSG_OPCODE_GET_DEL_TS:     s_msg_opcode_get_del_ts(l_msg); break;
        case MSG_OPCODE_GET_ALL:        if (s_msg_opcode_get_all(l_msg))
                                            return true;
                                        break;
        case MSG_OPCODE_GET_ALL_RAW:    if (s_msg_opcode_get_all(l_msg))
                                            return true;
                                        break;
        case MSG_OPCODE_SET:            s_msg_opcode_set(l_msg); break;
        case MSG_OPCODE_SET_MULTIPLE:   s_msg_opcode_set_multiple_zc(l_msg); break;
        case MSG_OPCODE_SET_RAW:        s_msg_opcode_set_raw(l_msg); break;
        case MSG_OPCODE_PIN:            s_msg_opcode_pin(l_msg); break;
        case MSG_OPCODE_DELETE:         s_msg_opcode_delete(l_msg); break;
        case MSG_OPCODE_FLUSH:          s_msg_opcode_flush(l_msg); break;
        default:{
            log_it(L_WARNING, "Message with undefined opcode %d received in queue_io",
                   l_msg->opcode);
        }
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
    DAP_DELETE(a_msg);
}

/**
 * @brief s_check_db_version
 * @return
 */
static int s_check_db_version()
{
    int l_ret;
    pthread_mutex_lock(&s_check_db_mutex);
    l_ret = dap_global_db_get(DAP_GLOBAL_DB_LOCAL_GENERAL, "gdb_version",s_check_db_version_callback_get, NULL);
    if (l_ret == 0){
        while (s_check_db_ret == INVALID_RETCODE)
            pthread_cond_wait(&s_check_db_cond, &s_check_db_mutex);
        l_ret = s_check_db_ret;
    }else{
        log_it(L_CRITICAL, "Can't process get gdb_version request, code %d", l_ret);
    }
    pthread_mutex_unlock(&s_check_db_mutex);
    return l_ret;
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


    if(a_errno != 0){ // No DB at all
        log_it(L_NOTICE, "No GlobalDB version at all, creating the new GlobalDB from scratch");
        a_dbi->version = DAP_GLOBAL_DB_VERSION;
        if ( (res = dap_global_db_set(DAP_GLOBAL_DB_LOCAL_GENERAL, "gdb_version",
                                      &a_dbi->version,
                                      sizeof(uint16_t), false,
                                      s_check_db_version_callback_set, NULL) ) != 0){
            log_it(L_NOTICE, "Can't set GlobalDB version, code %d", res);
            goto lb_exit;
        }
        return; // In this case the condition broadcast should happens in s_check_db_version_callback_set()

    }

    if (a_value_len == sizeof(uint16_t))
        a_dbi->version = *(uint16_t *)a_value;

    if( a_dbi->version < DAP_GLOBAL_DB_VERSION) {
        log_it(L_NOTICE, "GlobalDB version %u, but %u required. The current database will be recreated",
               a_dbi->version, DAP_GLOBAL_DB_VERSION);
        dap_global_db_deinit();
        // Database path
        const char *l_storage_path = dap_config_get_item_str(g_config, "resources", "dap_global_db_path");
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
                // Delete database file or directory
                dap_rm_rf(l_storage_path);
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
        res = dap_global_db_init(NULL, NULL);
        // Save current db version
        if(!res) {
            a_dbi->version = DAP_GLOBAL_DB_VERSION;
            if ( (res = dap_global_db_set(DAP_GLOBAL_DB_LOCAL_GENERAL, "gdb_version",
                                          &a_dbi->version,
                                          sizeof(uint16_t), false,
                                          s_check_db_version_callback_set, NULL) ) != 0){
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
    if(a_errno != 0){
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
        if (!dap_global_db_isalnum_group_key(a_store_objs + i)) {
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
        DAP_DELETE(a_store_objs[i].sign);
    }
    DAP_DELETE(a_store_objs);
    return l_objs;
}
