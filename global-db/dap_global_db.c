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
#include "dap_proc_thread.h"
#include "dap_global_db.h"
#include "dap_global_db_remote.h"
#include "dap_global_db_driver.h"

#define LOG_TAG "dap_global_db"

int g_dap_global_db_debug_more = false;                                         /* Enable extensible debug output */

// Queue I/O message op code
enum queue_io_msg_opcode{
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
    MSG_OPCODE_FLUSH,
    MSG_OPCODE_CONTEXT_EXEC
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

};

static pthread_cond_t s_check_db_cond = PTHREAD_COND_INITIALIZER; // Check version condition
static pthread_mutex_t s_check_db_mutex = PTHREAD_MUTEX_INITIALIZER; // Check version condition mutex
#define INVALID_RETCODE +100500
static int s_check_db_ret = INVALID_RETCODE; // Check version return value

static dap_global_db_context_t *s_context_global_db = NULL; // GlobalDB own context custom extension

// Version check& update functiosn
static int s_check_db_version();
static void s_check_db_version_callback_get (dap_global_db_context_t * a_global_db_context, int a_errno, const char * a_group, const char * a_key,
                                             const void * a_value, const size_t a_value_len,
                                             dap_nanotime_t value_ts,bool a_is_pinned, void * a_arg);
static void s_check_db_version_callback_set (dap_global_db_context_t * a_global_db_context, int a_errno, const char * a_group, const char * a_key,
                                             const void * a_value, const size_t a_value_len,
                                             dap_nanotime_t value_ts,bool a_is_pinned, void * a_arg);

// GlobalDB context start/stop callbacks
static void s_context_callback_started( dap_context_t * a_context, void *a_arg);
static void s_context_callback_stopped( dap_context_t * a_context, void *a_arg);


// Opcode to string
static const char *s_msg_opcode_to_str(enum queue_io_msg_opcode a_opcode);

// Queue i/o processing callback
static void s_queue_io_callback( dap_events_socket_t * a_es, void * a_arg);

// Queue i/o message processing functions
static bool s_msg_opcode_get(struct queue_io_msg * a_msg);
static bool s_msg_opcode_get_raw(struct queue_io_msg * a_msg);
static bool s_msg_opcode_get_del_ts(struct queue_io_msg * a_msg);
static bool s_msg_opcode_get_last(struct queue_io_msg * a_msg);
static bool s_msg_opcode_get_last_raw(struct queue_io_msg * a_msg);
static bool s_msg_opcode_get_all(struct queue_io_msg * a_msg);
static bool s_msg_opcode_get_all_raw(struct queue_io_msg * a_msg);
static bool s_msg_opcode_set(struct queue_io_msg * a_msg);
static bool s_msg_opcode_set_raw(struct queue_io_msg * a_msg);
static bool s_msg_opcode_set_multiple_zc(struct queue_io_msg * a_msg);
static bool s_msg_opcode_pin(struct queue_io_msg * a_msg);
static bool s_msg_opcode_delete(struct queue_io_msg * a_msg);
static bool s_msg_opcode_flush(struct queue_io_msg * a_msg);
static bool s_msg_opcode_context_exec(struct queue_io_msg * a_msg);

// Free memor for queue i/o message
static void s_queue_io_msg_delete( struct queue_io_msg * a_msg);

// Delete history add and del
static int s_record_del_history_add(char *a_group, char *a_key, uint64_t a_timestamp);
static int s_record_del_history_del(const char *a_group, const char *a_key);

// Call notificators
static void s_change_notify(dap_global_db_context_t *a_context, dap_store_obj_t *a_store_obj);

typedef uint64_t dap_global_db_callback_arg_uid_t;
/**
 * @brief A structure for storing callback data for synchronous calls.
 */
typedef struct sync_obj_data_callback {
    struct {
        pthread_cond_t cond;
        bool called;
        struct timespec timer_timeout;
    } hdr;
    union {
        struct {
            byte_t *data;
            size_t data_size;
            dap_nanotime_t ts;
            bool is_pinned;
            char *key;
        } get;
        struct {
            dap_store_obj_t *obj;
        } get_raw;
        struct {
            dap_global_db_obj_t *objs;
            size_t objs_count;
        } get_objs;
        struct {
            dap_store_obj_t *objs;
            size_t objs_count;
        } get_store_objs;
        struct {
            int result;
        } op_result;
        struct {
            dap_nanotime_t timestamp;
        } del_ts;
    };
    dap_global_db_callback_arg_uid_t uid;
    UT_hash_handle hh;
} sync_obj_data_callback_t;

// Saves GDB callig context
static struct sync_obj_data_callback *s_global_db_find_callback_data(dap_global_db_context_t *a_global_db_context, dap_global_db_callback_arg_uid_t a_uid)
{
    struct sync_obj_data_callback *l_found = NULL;
    HASH_FIND(hh, a_global_db_context->data_callbacks, &a_uid, sizeof(dap_global_db_callback_arg_uid_t), l_found);
    return l_found;
}

static struct sync_obj_data_callback *s_global_db_obj_data_callback_new()
{
    struct sync_obj_data_callback *l_callback = DAP_NEW_Z(struct sync_obj_data_callback);
    if (!l_callback) {
        log_it(L_CRITICAL, "Memory allocation error");
        return NULL;
    }
    pthread_condattr_t attr;
    pthread_condattr_init(&attr);
#ifndef DAP_OS_DARWIN
    pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
#endif
    pthread_cond_init(&l_callback->hdr.cond, &attr);
    clock_gettime(CLOCK_MONOTONIC, &l_callback->hdr.timer_timeout);
    l_callback->hdr.timer_timeout.tv_sec += DAP_GLOBAL_DB_SYNC_WAIT_TIMEOUT;
    l_callback->uid = dap_uuid_generate_uint64();
    pthread_mutex_lock(&s_context_global_db->data_callbacks_mutex);
    HASH_ADD(hh, s_context_global_db->data_callbacks, uid, sizeof(dap_global_db_callback_arg_uid_t), l_callback);
    return l_callback;
}

static void s_global_db_obj_data_callback_destroy(struct sync_obj_data_callback *a_obj)
{
    HASH_DEL(s_context_global_db->data_callbacks, a_obj);
    pthread_cond_destroy(&a_obj->hdr.cond);
    DAP_DELETE(a_obj);
    pthread_mutex_unlock(&s_context_global_db->data_callbacks_mutex);
}

static void s_global_db_obj_data_callback_wait(struct sync_obj_data_callback* a_obj, const char *a_module) {
    if (!a_obj) {
        log_it(L_ERROR, "Object is NULL, can't call s_global_db_obj_data_callback_wait");
        return;
    }
    while (!a_obj->hdr.called)
        if (pthread_cond_timedwait(&a_obj->hdr.cond, &s_context_global_db->data_callbacks_mutex, &a_obj->hdr.timer_timeout) == ETIMEDOUT) {
            log_it(L_ERROR, "Global DB %s operation timeout", a_module);
            break;
        }
}

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

    dap_global_db_sync_init();

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
    if (s_context_global_db == NULL) {
        s_context_global_db = DAP_NEW_Z(struct dap_global_db_context);
        if (!s_context_global_db) {
        log_it(L_CRITICAL, "Memory allocation error");
            l_rc = -5;
            goto lb_return;
        }
        s_context_global_db->context = dap_context_new(DAP_CONTEXT_TYPE_GLOBAL_DB);
        s_context_global_db->context->_inheritor = s_context_global_db;
        if (dap_context_run(s_context_global_db->context, -1, DAP_CONTEXT_POLICY_DEFAULT,
                            DAP_CONTEXT_PRIORITY_HIGH, DAP_CONTEXT_FLAG_WAIT_FOR_STARTED,
                            s_context_callback_started, s_context_callback_stopped, NULL) != 0) {
            l_rc = -2;
            goto lb_return;
        }
        dap_global_db_instance_t *l_dbi = DAP_NEW_Z(dap_global_db_instance_t);
        if (!l_dbi) {
        log_it(L_CRITICAL, "Memory allocation error");
            DAP_DEL_Z(s_context_global_db);
            l_rc = -5;
            goto lb_return;
        }

        l_dbi->storage_path = dap_strdup(a_storage_path);
        l_dbi->driver_name = dap_strdup(a_driver_name);

        uint16_t l_size_ban_list = 0, l_size_white_list = 0;
        char **l_ban_list = dap_config_get_array_str(g_config, "global_db", "ban_list_sync_groups", &l_size_ban_list);
        for (int i = 0; i < l_size_ban_list; i++)
            l_dbi->blacklist = dap_list_append(l_dbi->blacklist, dap_strdup(l_ban_list[i]));
        char **l_white_list = dap_config_get_array_str(g_config, "global_db", "white_list_sync_groups", &l_size_white_list);
        for (int i = 0; i < l_size_ban_list; i++) {
            l_dbi->whitelist = dap_list_append(l_dbi->whitelist, dap_strdup(l_white_list[i]));
            l_dbi->whitelist = dap_list_append(l_dbi->whitelist, dap_strdup_printf("%s.del", l_white_list[i]));
        }

        l_dbi->store_time_limit = dap_config_get_item_uint32_default(g_config, "global_db", "store_time_limit", 72);

        s_context_global_db->instance = l_dbi;
    }

    // Driver initalization
    if( (l_rc = dap_db_driver_init(s_context_global_db->instance->driver_name,
                                   s_context_global_db->instance->storage_path, true))  )
        return  log_it(L_CRITICAL, "Hadn't initialized DB driver \"%s\" on path \"%s\", code: %d",
                       s_context_global_db->instance->driver_name, s_context_global_db->instance->storage_path, l_rc), l_rc;



    // Check version and update if need it
    if(!s_is_check_version){

        s_is_check_version = true;

        if ( (l_rc = s_check_db_version()) )
            return  log_it(L_ERROR, "GlobalDB version changed, please export or remove old version!"), l_rc;
    }

    pthread_mutex_init(&s_context_global_db->data_callbacks_mutex, NULL);
    s_context_global_db->data_callbacks = NULL;

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
void dap_global_db_context_deinit() {
    if (s_context_global_db) {
        dap_context_stop_n_kill(s_context_global_db->context);
        dap_list_free_full(s_context_global_db->instance->blacklist, NULL);
        dap_list_free_full(s_context_global_db->instance->whitelist, NULL);
        DAP_DEL_Z(s_context_global_db->instance->driver_name);
        DAP_DEL_Z(s_context_global_db->instance->storage_path);
        DAP_DEL_Z(s_context_global_db->instance);
        DAP_DEL_Z(s_context_global_db);
    }
}

/**
 * @brief dap_global_db_deinit, after fix ticket 9030 need add dap_global_db_context_deinit() 
 */
void dap_global_db_deinit() {
    // dap_global_db_context_deinit()
    dap_db_driver_deinit();
    dap_global_db_sync_deinit();
}

/**
 * @brief dap_global_db_context_current()
 * @return
 */
dap_global_db_context_t * dap_global_db_context_current()
{
    dap_context_t *l_context = dap_context_current();
    if (l_context && l_context->type == DAP_CONTEXT_TYPE_GLOBAL_DB)
        return (dap_global_db_context_t *)l_context->_inheritor;
    return NULL;
}

dap_global_db_context_t *dap_global_db_context_get_default()
{
    return s_context_global_db;
}

/* *** Get functions group *** */

byte_t *dap_global_db_get_unsafe(dap_global_db_context_t *a_global_db_context, const char *a_group,
                                 const char *a_key, size_t *a_data_size,
                                 bool *a_is_pinned, dap_nanotime_t *a_ts)
{

    debug_if(g_dap_global_db_debug_more, L_DEBUG, "get call executes for group \"%s\" and key \"%s\"", a_group, a_key);
    dap_store_obj_t *l_store_obj = dap_global_db_get_raw_unsafe(a_global_db_context, a_group, a_key);
    if (!l_store_obj) {
        // log_it(L_ERROR, "%s is not found", a_key);
        return NULL;
    }
    if (a_data_size)
        *a_data_size = l_store_obj->value_len;
    if (a_is_pinned)
        *a_is_pinned = l_store_obj->flags & RECORD_PINNED;
    if (a_ts)
        *a_ts = l_store_obj->timestamp;
    byte_t *l_res = DAP_DUP_SIZE(l_store_obj->value, l_store_obj->value_len);
    if (!l_res) {
        log_it(L_CRITICAL, "Memory allocation error");
        return NULL;
    }
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
    if(s_context_global_db == NULL){
        log_it(L_ERROR, "GlobalDB context is not initialized, can't call dap_global_db_get");
        return DAP_GLOBAL_DB_RC_ERROR;
    }
    struct queue_io_msg * l_msg = DAP_NEW_Z(struct queue_io_msg);
    if (!l_msg) {
        log_it(L_CRITICAL, "Memory allocation error");
        return -1;
    }
    l_msg->opcode = MSG_OPCODE_GET;
    l_msg->group = dap_strdup(a_group);
    l_msg->key = dap_strdup(a_key);
    l_msg->callback_result = a_callback;
    l_msg->callback_arg = a_arg;

    int l_ret = dap_events_socket_queue_ptr_send(s_context_global_db->queue_io,l_msg);
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
static bool s_msg_opcode_get(struct queue_io_msg * a_msg)
{
    size_t l_value_len = 0;
    bool l_pinned = false;
    dap_nanotime_t l_ts = 0;
    byte_t *l_value = dap_global_db_get_unsafe(s_context_global_db, a_msg->group, a_msg->key, &l_value_len, &l_pinned, &l_ts);
    if (l_value && l_value_len) {
        if(a_msg->callback_result)
            a_msg->callback_result(s_context_global_db, DAP_GLOBAL_DB_RC_SUCCESS, a_msg->group, a_msg->key,
                               l_value, l_value_len, l_ts,
                               l_pinned, a_msg->callback_arg);
        DAP_DELETE(l_value);
    }else if(a_msg->callback_result)
        a_msg->callback_result(s_context_global_db, DAP_GLOBAL_DB_RC_NO_RESULTS, a_msg->group, a_msg->key,
                               NULL, 0, 0,0, a_msg->callback_arg);
    return true;
}


/**
 * @brief s_store_obj_get_callback
 * @param a_global_db_context
 * @param a_rc
 * @param a_group
 * @param a_key
 * @param a_value
 * @param a_value_size
 * @param a_value_ts
 * @param a_is_pinned
 * @param a_arg
 */
static void s_obj_get_callback(UNUSED_ARG dap_global_db_context_t *a_global_db_context,
                               UNUSED_ARG int a_rc, UNUSED_ARG const char *a_group,
                               const char *a_key, const void *a_value,
                               const size_t a_value_size, dap_nanotime_t a_value_ts,
                               bool a_is_pinned, void *a_arg)
{
    assert(a_arg);
    dap_global_db_callback_arg_uid_t *l_uid = a_arg;
    pthread_mutex_lock(&s_context_global_db->data_callbacks_mutex);
    struct sync_obj_data_callback *l_args = s_global_db_find_callback_data(a_global_db_context, *l_uid);
    DAP_DELETE(l_uid);
    if (!l_args) {
        pthread_mutex_unlock(&s_context_global_db->data_callbacks_mutex);
        return;
    }
    if (a_value && a_value_size) {
        l_args->get.data = DAP_DUP_SIZE(a_value, a_value_size);
        l_args->get.data_size = a_value_size;
        l_args->get.ts = a_value_ts;
        l_args->get.is_pinned = a_is_pinned;
        l_args->get.key = (char *) a_key;
    }
    l_args->hdr.called = true;
    pthread_cond_signal(&l_args->hdr.cond);
    pthread_mutex_unlock(&s_context_global_db->data_callbacks_mutex);
}

/**
 * @brief dap_global_db_gr_get_sync
 * @param a_group
 * @param a_key
 * @param a_data_size
 * @param a_is_pinned
 * @param a_ts
 * @return
 */
byte_t *dap_global_db_get_sync(const char *a_group, const char *a_key, size_t *a_data_size, bool *a_is_pinned, dap_nanotime_t *a_ts)
{
    return dap_global_db_get_unsafe(s_context_global_db, a_group, a_key, a_data_size, a_is_pinned, a_ts);
#if 0
    if (dap_global_db_context_current() == s_context_global_db)
        return dap_global_db_get_unsafe(s_context_global_db, a_group, a_key, a_data_size, a_is_pinned, a_ts);

    struct sync_obj_data_callback *l_args = s_global_db_obj_data_callback_new();
    if (!dap_global_db_get(a_group,a_key, s_obj_get_callback, DAP_DUP(&l_args->uid)))
        s_global_db_obj_data_callback_wait(l_args, "get");

    if (l_args->get.data) {
        if (a_data_size)
            *a_data_size = l_args->get.data_size;
        if (a_is_pinned)
            *a_is_pinned = l_args->get.is_pinned;
        if (a_ts)
            *a_ts = l_args->get.ts;
    }
    byte_t *l_ret = l_args->get.data;
    s_global_db_obj_data_callback_destroy(l_args);
    return l_ret;
#endif
}

/* *** Get raw functions group *** */

dap_store_obj_t *dap_global_db_get_raw_unsafe(UNUSED_ARG dap_global_db_context_t *a_global_db_context, const char *a_group, const char *a_key)
{
    size_t l_count_records = 0;
    dap_store_obj_t *l_res = dap_global_db_driver_read(a_group, a_key, &l_count_records);
    if (l_count_records > 1)
        log_it(L_WARNING, "Get more than one global DB object by one key is unexpected");
    if (!dap_global_db_isalnum_group_key(l_res)) {
        log_it(L_CRITICAL, "Delete broken object");
        dap_global_db_del_sync(l_res->group, l_res->key);
        dap_store_obj_free_one(l_res);
        l_res = NULL;
    }
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
    if(s_context_global_db == NULL){
        log_it(L_ERROR, "GlobalDB context is not initialized, can't call dap_global_db_get");
        return DAP_GLOBAL_DB_RC_ERROR;
    }
    struct queue_io_msg * l_msg = DAP_NEW_Z(struct queue_io_msg);
    if (!l_msg) {
        log_it(L_CRITICAL, "Memory allocation error");
        return -1;
    }
    l_msg->opcode = MSG_OPCODE_GET_RAW;
    l_msg->group = dap_strdup(a_group);
    l_msg->key = dap_strdup(a_key);
    l_msg->callback_result_raw = a_callback;
    l_msg->callback_arg = a_arg;

    int l_ret = dap_events_socket_queue_ptr_send(s_context_global_db->queue_io,l_msg);
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
static bool s_msg_opcode_get_raw(struct queue_io_msg * a_msg)
{
    dap_store_obj_t *l_store_obj = dap_global_db_get_raw_unsafe(s_context_global_db, a_msg->group, a_msg->key);

    if(a_msg->callback_result_raw)
        a_msg->callback_result_raw(s_context_global_db, l_store_obj ? DAP_GLOBAL_DB_RC_SUCCESS:
                                                                      DAP_GLOBAL_DB_RC_NO_RESULTS,
                                                        l_store_obj, a_msg->callback_arg );
    dap_store_obj_free_one(l_store_obj);
    return true;
}


static void s_obj_raw_get_callback(UNUSED_ARG dap_global_db_context_t *a_global_db_context,
                                   UNUSED_ARG int a_rc, dap_store_obj_t *a_value, void *a_arg)
{
    assert(a_arg);
    dap_global_db_callback_arg_uid_t *l_uid = a_arg;
    pthread_mutex_lock(&s_context_global_db->data_callbacks_mutex);
    struct sync_obj_data_callback *l_args = s_global_db_find_callback_data(a_global_db_context, *l_uid);
    DAP_DELETE(l_uid);
    if (!l_args) {
        pthread_mutex_unlock(&s_context_global_db->data_callbacks_mutex);
        return;
    }
    l_args->get_raw.obj = dap_store_obj_copy(a_value, 1);
    l_args->hdr.called = true;
    pthread_cond_signal(&l_args->hdr.cond);
    pthread_mutex_unlock(&s_context_global_db->data_callbacks_mutex);
}

/**
 * @brief Sync (blocking) function for retrieving raw GDB
 * @param a_group
 * @param a_fist_id
 * @param a_objs_count
 * @return Group's objects
 */
dap_store_obj_t *dap_global_db_get_raw_sync(const char *a_group, const char *a_key)
{
    return dap_global_db_get_raw_unsafe(s_context_global_db, a_group, a_key);
#if 0
    if (dap_global_db_context_current() == s_context_global_db)
        return dap_global_db_get_raw_unsafe(s_context_global_db, a_group, a_key);

    debug_if(g_dap_global_db_debug_more, L_DEBUG, "get_raw sync call executes for group \"%s\"", a_group);
    struct sync_obj_data_callback *l_args = s_global_db_obj_data_callback_new();
    if (!dap_global_db_get_raw(a_group, a_key, s_obj_raw_get_callback, DAP_DUP(&l_args->uid)))
        s_global_db_obj_data_callback_wait(l_args, "get_raw");

    dap_store_obj_t *l_ret = l_args->get_raw.obj;
    s_global_db_obj_data_callback_destroy(l_args);
    return l_ret;
#endif
}

/* *** Get_del_ts functions group *** */

dap_nanotime_t dap_global_db_get_del_ts_unsafe(dap_global_db_context_t *a_global_db_context, const char *a_group, const char *a_key)
{
    dap_store_obj_t *l_store_obj_del = NULL;
    dap_nanotime_t l_timestamp = 0;
    char l_group[DAP_GLOBAL_DB_GROUP_NAME_SIZE_MAX];

    if (a_key && a_group) {
        snprintf(l_group, sizeof(l_group) - 1,  "%s.del", a_group);
        if (dap_global_db_driver_is(l_group, a_key)) {
            l_store_obj_del = dap_global_db_get_raw_unsafe(a_global_db_context, l_group, a_key);
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
    if(s_context_global_db == NULL){
        log_it(L_ERROR, "GlobalDB context is not initialized, can't call dap_global_db_get");
        return DAP_GLOBAL_DB_RC_ERROR;
    }
    struct queue_io_msg * l_msg = DAP_NEW_Z(struct queue_io_msg);
    if (!l_msg) {
        log_it(L_CRITICAL, "Memory allocation error");
        return -1;
    }
    l_msg->opcode = MSG_OPCODE_GET_DEL_TS;
    l_msg->group = dap_strdup(a_group);
    l_msg->key = dap_strdup(a_key);
    l_msg->callback_result = a_callback;
    l_msg->callback_arg = a_arg;

    int l_ret = dap_events_socket_queue_ptr_send(s_context_global_db->queue_io,l_msg);
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
static bool s_msg_opcode_get_del_ts(struct queue_io_msg * a_msg)
{
    dap_nanotime_t l_timestamp = dap_global_db_get_del_ts_unsafe(s_context_global_db, a_msg->group, a_msg->key);
    if(l_timestamp){
        if(a_msg->callback_result)
            a_msg->callback_result(s_context_global_db, DAP_GLOBAL_DB_RC_SUCCESS, a_msg->group, a_msg->key,
                               NULL, 0, l_timestamp,
                               false, a_msg->callback_arg );
    }else if(a_msg->callback_result)
        a_msg->callback_result(s_context_global_db, DAP_GLOBAL_DB_RC_NO_RESULTS, a_msg->group, a_msg->key,
                               NULL, 0, 0,0, a_msg->callback_arg );
    return true;
}


static void s_del_ts_get_callback(UNUSED_ARG dap_global_db_context_t *a_global_db_context,
                                  UNUSED_ARG int a_rc, UNUSED_ARG const char *a_group,
                                  UNUSED_ARG const char *a_key, UNUSED_ARG const void *a_value,
                                  UNUSED_ARG const size_t a_value_size, dap_nanotime_t a_value_ts,
                                  UNUSED_ARG bool a_is_pinned, void *a_arg)
{
    assert(a_arg);
    dap_global_db_callback_arg_uid_t *l_uid = a_arg;
    pthread_mutex_lock(&s_context_global_db->data_callbacks_mutex);
    struct sync_obj_data_callback *l_args = s_global_db_find_callback_data(a_global_db_context, *l_uid);
    DAP_DELETE(l_uid);
    if (!l_args) {
        pthread_mutex_unlock(&s_context_global_db->data_callbacks_mutex);
        return;
    }
    l_args->del_ts.timestamp = a_value_ts;
    l_args->hdr.called = true;
    pthread_cond_signal(&l_args->hdr.cond);
    pthread_mutex_unlock(&s_context_global_db->data_callbacks_mutex);
}

/**
 * @brief Sync (blocking) function for retrieving timestamp of object deletion
 * @param a_group
 * @param a_key
 */
dap_nanotime_t dap_global_db_get_del_ts_sync(const char *a_group, const char *a_key)
{
    return dap_global_db_get_del_ts_unsafe(s_context_global_db, a_group, a_key);
#if 0
    if (dap_global_db_context_current() == s_context_global_db)
        return dap_global_db_get_del_ts_unsafe(s_context_global_db, a_group, a_key);

    debug_if(g_dap_global_db_debug_more, L_DEBUG, "get_del_ts sync call executed for group \"%s\"", a_group);
    struct sync_obj_data_callback *l_args = s_global_db_obj_data_callback_new();
    if (!dap_global_db_get_del_ts(a_group, a_key, s_del_ts_get_callback, DAP_DUP(&l_args->uid)))
        s_global_db_obj_data_callback_wait(l_args, "del_ts");

    dap_nanotime_t l_ret = l_args->del_ts.timestamp;
    s_global_db_obj_data_callback_destroy(l_args);
    return l_ret;
#endif
}

/* *** Get_last functions group *** */

byte_t *dap_global_db_get_last_unsafe(dap_global_db_context_t *a_global_db_context,
                                      const char *a_group, char **a_key, size_t *a_data_size,
                                      bool *a_is_pinned, dap_nanotime_t *a_ts)
{
    dap_store_obj_t *l_store_obj = dap_global_db_get_last_raw_unsafe(a_global_db_context, a_group);
    if (!l_store_obj) {
        log_it(L_ERROR, "l_store_ibj is not initialized, can't call dap_global_db_get_last_unsafe");
        return NULL;
    }
    if (a_key)
        *a_key = dap_strdup(l_store_obj->key);
    if (a_data_size)
        *a_data_size = l_store_obj->value_len;
    if (a_is_pinned)
        *a_is_pinned = l_store_obj->flags & RECORD_PINNED;
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
    if(s_context_global_db == NULL){
        log_it(L_ERROR, "GlobalDB context is not initialized, can't call dap_global_db_get_last");
        return DAP_GLOBAL_DB_RC_ERROR;
    }
    struct queue_io_msg * l_msg = DAP_NEW_Z(struct queue_io_msg);
    if (!l_msg) {
        log_it(L_CRITICAL, "Memory allocation error");
        return -1;
    }
    l_msg->opcode = MSG_OPCODE_GET_LAST;
    l_msg->group = dap_strdup(a_group);
    l_msg->callback_arg = a_arg;
    l_msg->callback_result = a_callback;

    int l_ret = dap_events_socket_queue_ptr_send(s_context_global_db->queue_io,l_msg);
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
static bool s_msg_opcode_get_last(struct queue_io_msg * a_msg)
{
    size_t l_value_len = 0;
    bool l_pinned = false;
    dap_nanotime_t l_ts = 0;
    char *l_key = NULL;
    byte_t *l_value = dap_global_db_get_last_unsafe(s_context_global_db, a_msg->group, &l_key, &l_value_len, &l_pinned, &l_ts);
    if (l_value && l_value_len) {
        if(a_msg->callback_result)
            a_msg->callback_result(s_context_global_db, DAP_GLOBAL_DB_RC_SUCCESS, a_msg->group, l_key,
                               l_value, l_value_len, l_ts,
                               l_pinned, a_msg->callback_arg);
        DAP_DELETE(l_value);
    }else if(a_msg->callback_result)
        a_msg->callback_result(s_context_global_db, DAP_GLOBAL_DB_RC_NO_RESULTS, a_msg->group, l_key,
                               NULL, 0, 0,0, a_msg->callback_arg );
    return true;
}

/**
 * @brief dap_global_db_get_last_sync
 * @param a_group
 * @param a_key
 * @param a_data_size
 * @param a_is_pinned
 * @param a_ts
 * @return
 */
byte_t *dap_global_db_get_last_sync(const char *a_group, char **a_key, size_t *a_data_size, bool *a_is_pinned, dap_nanotime_t *a_ts)
{
    return dap_global_db_get_last_unsafe(s_context_global_db, a_group, a_key, a_data_size, a_is_pinned, a_ts);
#if 0
    if (dap_global_db_context_current() == s_context_global_db)
        return dap_global_db_get_last_unsafe(s_context_global_db, a_group, a_key, a_data_size, a_is_pinned, a_ts);

    struct sync_obj_data_callback *l_args = s_global_db_obj_data_callback_new();
    if (!dap_global_db_get_last(a_group, s_obj_get_callback, DAP_DUP(&l_args)))
        s_global_db_obj_data_callback_wait(l_args, "get_last");

    if (l_args->get.data) {
        if (a_key)
            *a_key = l_args->get.key;
        else
            DAP_DEL_Z(l_args->get.key);
        if (a_data_size)
            *a_data_size = l_args->get.data_size;
        if (a_is_pinned)
            *a_is_pinned = l_args->get.is_pinned;
        if (a_ts)
            *a_ts = l_args->get.ts;
    }

    byte_t *l_ret = l_args->get.data;
    s_global_db_obj_data_callback_destroy(l_args);
    return l_ret;
#endif
}

/* *** Get_last_raw functions group *** */

dap_store_obj_t *dap_global_db_get_last_raw_unsafe(UNUSED_ARG dap_global_db_context_t *a_global_db_context, const char *a_group)
{
    dap_store_obj_t *l_ret = dap_global_db_driver_read_last(a_group);
    if (!dap_global_db_isalnum_group_key(l_ret)) {
        log_it(L_CRITICAL, "Delete broken object");
        dap_global_db_del_sync(l_ret->group, l_ret->key);
        dap_store_obj_free_one(l_ret);
        l_ret = NULL;
    }
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
    if(s_context_global_db == NULL){
        log_it(L_ERROR, "GlobalDB context is not initialized, can't call dap_global_db_get_last");
        return DAP_GLOBAL_DB_RC_ERROR;
    }
    struct queue_io_msg * l_msg = DAP_NEW_Z(struct queue_io_msg);
    if (!l_msg) {
        log_it(L_CRITICAL, "Memory allocation error");
        return -1;
    }
    l_msg->opcode = MSG_OPCODE_GET_LAST_RAW;
    l_msg->group = dap_strdup(a_group);
    l_msg->callback_arg = a_arg;
    l_msg->callback_result_raw = a_callback;

    int l_ret = dap_events_socket_queue_ptr_send(s_context_global_db->queue_io,l_msg);
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
static bool s_msg_opcode_get_last_raw(struct queue_io_msg * a_msg)
{
    dap_store_obj_t *l_store_obj = dap_global_db_get_last_raw_unsafe(s_context_global_db, a_msg->group);
    if(a_msg->callback_result)
        a_msg->callback_result_raw(s_context_global_db, l_store_obj ? DAP_GLOBAL_DB_RC_SUCCESS : DAP_GLOBAL_DB_RC_NO_RESULTS, l_store_obj, a_msg->callback_arg);
    dap_store_obj_free(l_store_obj, 1);
    return true;
}

/**
 * @brief Sync (blocking) function for retrieving raw GDB
 * @param a_group
 * @param a_fist_id
 * @param a_objs_count
 * @return Group's objects
 */
dap_store_obj_t *dap_global_db_get_last_raw_sync(const char *a_group)
{
    return dap_global_db_get_last_raw_unsafe(s_context_global_db, a_group);
#if 0
    if (dap_global_db_context_current() == s_context_global_db)
        return dap_global_db_get_last_raw_unsafe(s_context_global_db, a_group);

    debug_if(g_dap_global_db_debug_more, L_DEBUG, "get_raw sync call executes for group \"%s\"", a_group);
    struct sync_obj_data_callback *l_args = s_global_db_obj_data_callback_new();
    if (!dap_global_db_get_last_raw(a_group, s_obj_raw_get_callback, DAP_DUP(&l_args)))
        s_global_db_obj_data_callback_wait(l_args, "last_raw");

    dap_store_obj_t *l_ret = l_args->get_raw.obj;
    s_global_db_obj_data_callback_destroy(l_args);
    return l_ret;
#endif
}

/* *** Get_all functions group *** */


static int s_db_compare_by_ts(const void *a_obj1, const void *a_obj2) {
    dap_store_obj_t *l_obj1 = (dap_store_obj_t *)a_obj1,
            *l_obj2 = (dap_store_obj_t *)a_obj2;
    return l_obj2->timestamp < l_obj1->timestamp
            ? 1
            : l_obj2->timestamp > l_obj1->timestamp
              ? -1
              : 0; // should never occur...
}

dap_global_db_obj_t *dap_global_db_get_all_unsafe(UNUSED_ARG dap_global_db_context_t *a_global_db_context,
                                                  const char *a_group, size_t *a_objs_count)
{
    size_t l_values_count = 0;
    dap_store_obj_t *l_store_objs = dap_global_db_driver_read(a_group, 0, &l_values_count);
    debug_if(g_dap_global_db_debug_more, L_DEBUG, "Get all request from group %s recieved %zu values",
                                                   a_group, l_values_count);
    dap_global_db_obj_t *l_objs = NULL;
    size_t i = 0, j = 0;
    // Form objs from store_objs
    if (l_store_objs) {
        if (l_values_count > 1)
            qsort(l_store_objs, l_values_count, sizeof(dap_store_obj_t), s_db_compare_by_ts);
        l_objs = DAP_NEW_Z_SIZE(dap_global_db_obj_t, sizeof(dap_global_db_obj_t) * l_values_count);
        if (!l_objs) {
            goto mem_clear;
        }
        for (i = 0, j = 0; j < l_values_count; ++i, ++j){
            if (!dap_global_db_isalnum_group_key(&l_store_objs[j])) {
                log_it(L_CRITICAL, "Delete broken object");
                dap_global_db_del_sync(l_store_objs[j].group, l_store_objs[j].key);
                --i;
                continue;
            }
            l_objs[i].id = l_store_objs[i].id;
            l_objs[i].is_pinned = l_store_objs[i].flags & RECORD_PINNED;
            l_objs[i].key = dap_strdup(l_store_objs[i].key);
            if (!l_objs[i].key) {
                goto mem_clear;
            }
            l_objs[i].value = DAP_DUP_SIZE(l_store_objs[i].value, l_store_objs[i].value_len);
            if (!l_objs[i].value && l_store_objs[i].value)
                goto mem_clear;
            l_objs[i].value_len = l_store_objs[i].value_len;
            l_objs[i].timestamp = l_store_objs[i].timestamp;
        }
    }
    dap_store_obj_free(l_store_objs, l_values_count);
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
int dap_global_db_get_all(const char * a_group, UNUSED_ARG size_t a_results_page_size, dap_global_db_callback_results_t a_callback, void * a_arg)
{
    // TODO make usable a_results_page_size
    if(s_context_global_db == NULL){
        log_it(L_ERROR, "GlobalDB context is not initialized, can't call dap_global_db_get_all");
        return DAP_GLOBAL_DB_RC_ERROR;
    }
    struct queue_io_msg * l_msg = DAP_NEW_Z(struct queue_io_msg);
    if (!l_msg) {
        log_it(L_CRITICAL, "Memory allocation error");
        return -1;
    }
    l_msg->opcode = MSG_OPCODE_GET_ALL;
    l_msg->group = dap_strdup(a_group);
    l_msg->callback_arg = a_arg;
    l_msg->callback_results = a_callback;

    int l_ret = dap_events_socket_queue_ptr_send(s_context_global_db->queue_io,l_msg);
    if (l_ret != 0){
        log_it(L_ERROR, "Can't exec get_all request, code %d", l_ret);
        s_queue_io_msg_delete(l_msg);
    }else
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
    size_t l_values_count = 0;
    dap_global_db_obj_t *l_objs = dap_global_db_get_all_unsafe(s_context_global_db, a_msg->group, &l_values_count);

    // Call callback if present
    if(a_msg->callback_results)
        a_msg->callback_results(s_context_global_db,
                                l_objs ? DAP_GLOBAL_DB_RC_SUCCESS : DAP_GLOBAL_DB_RC_NO_RESULTS,
                                a_msg->group, a_msg->values_total, l_values_count,
                                l_objs, a_msg->callback_arg);
    // Clean memory
    dap_global_db_objs_delete(l_objs, l_values_count);
    return true; // All values are sent
}

/**
 * @brief s_objs_get_callback
 * @param a_global_db_context
 * @param a_rc
 * @param a_group
 * @param a_key
 * @param a_values_total
 * @param a_values_shift
 * @param a_values_count
 * @param a_values
 * @param a_arg
 */
static void s_objs_get_callback(UNUSED_ARG dap_global_db_context_t *a_global_db_context,
                                UNUSED_ARG int a_rc, UNUSED_ARG const char *a_group,
                                UNUSED_ARG const size_t a_values_total, const size_t a_values_count,
                                dap_global_db_obj_t *a_values, void *a_arg)
{
    assert(a_arg);
    dap_global_db_callback_arg_uid_t *l_uid = a_arg;
    pthread_mutex_lock(&s_context_global_db->data_callbacks_mutex);
    struct sync_obj_data_callback *l_args = s_global_db_find_callback_data(a_global_db_context, *l_uid);
    DAP_DELETE(l_uid);
    if (!l_args) {
        pthread_mutex_unlock(&s_context_global_db->data_callbacks_mutex);
        return;
    }
    l_args->get_objs.objs = dap_global_db_objs_copy(a_values, a_values_count);
    l_args->get_objs.objs_count = a_values_count;
    l_args->hdr.called = true;
    pthread_cond_signal(&l_args->hdr.cond);
    pthread_mutex_unlock(&s_context_global_db->data_callbacks_mutex);
}

/**
 * @brief Sync (blocking) function for retrieving of list of GDB content
 * @param a_group
 * @param a_fist_id
 * @param a_objs_count
 * @return Group's objects
 */
dap_global_db_obj_t *dap_global_db_get_all_sync(const char *a_group, size_t *a_objs_count)
{
    return dap_global_db_get_all_unsafe(s_context_global_db, a_group, a_objs_count);
#if 0
    if (dap_global_db_context_current() == s_context_global_db)
        return dap_global_db_get_all_unsafe(s_context_global_db, a_group, a_objs_count);

    debug_if(g_dap_global_db_debug_more, L_DEBUG, "get_all sync call executes for group \"%s\"", a_group);
    struct sync_obj_data_callback *l_args = s_global_db_obj_data_callback_new();
    if (!dap_global_db_get_all(a_group, 0, s_objs_get_callback, DAP_DUP(&l_args->uid)))
        s_global_db_obj_data_callback_wait(l_args, "get_all");
    if (a_objs_count)
        *a_objs_count = l_args->get_objs.objs_count;
    dap_global_db_obj_t *l_ret = l_args->get_objs.objs;
    s_global_db_obj_data_callback_destroy(l_args);
    return l_ret;
#endif
}

/* *** Get_all_raw functions group *** */

dap_store_obj_t* dap_global_db_get_all_raw_unsafe(UNUSED_ARG dap_global_db_context_t *a_global_db_context,
                                                  const char *a_group, uint64_t a_first_id, size_t *a_objs_count)
{
    dap_store_obj_t* l_ret = dap_global_db_driver_cond_read(a_group, a_first_id, a_objs_count);
    if (a_objs_count && *a_objs_count) {
        size_t l_cur_i = 0;
        for (dap_store_obj_t* l_cur = l_ret; l_cur_i < *a_objs_count; ++l_cur, ++l_cur_i) {
            if (!dap_global_db_isalnum_group_key(l_cur)) {
                log_it(L_CRITICAL, "Delete broken object");
                dap_global_db_del_sync(l_cur->group, l_cur->key);
                DAP_DEL_Z(l_cur->group);
                DAP_DEL_Z(l_cur->key);
                DAP_DEL_Z(l_cur->value);
                --*a_objs_count;
                if (l_cur_i != *a_objs_count - 1) {
                    memmove(l_cur, l_cur + 1, (*a_objs_count - l_cur_i) * sizeof(dap_store_obj_t));
                }
                l_ret = DAP_REALLOC(l_ret, *a_objs_count * sizeof(dap_store_obj_t));
                if (!l_ret)
                    break;
                --l_cur_i;
                --l_cur;
            }
        }
    }
    return l_ret;
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
int dap_global_db_get_all_raw(const char * a_group, uint64_t a_first_id, size_t a_results_page_size,
                              dap_global_db_callback_results_raw_t a_callback, void * a_arg)
{
    // TODO make usable a_results_page_size

    if(s_context_global_db == NULL){
        log_it(L_ERROR, "GlobalDB context is not initialized, can't call dap_global_db_get_all");
        return DAP_GLOBAL_DB_RC_ERROR;
    }
    struct queue_io_msg * l_msg = DAP_NEW_Z(struct queue_io_msg);
    if (!l_msg) {
        log_it(L_CRITICAL, "Memory allocation error");
        return -1;
    }
    l_msg->opcode = MSG_OPCODE_GET_ALL_RAW ;
    l_msg->group = dap_strdup(a_group);
    l_msg->values_raw_last_id = a_first_id;
    l_msg->values_page_size = a_results_page_size;
    l_msg->callback_arg = a_arg;
    l_msg->callback_results_raw = a_callback;

    int l_ret = dap_events_socket_queue_ptr_send(s_context_global_db->queue_io,l_msg);
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
static bool s_msg_opcode_get_all_raw(struct queue_io_msg * a_msg)
{
    size_t l_values_count = a_msg->values_page_size;
    size_t l_values_remains = dap_global_db_driver_count(a_msg->group, a_msg->values_raw_last_id);
    dap_store_obj_t *l_store_objs = dap_global_db_get_all_raw_unsafe(s_context_global_db, a_msg->group, a_msg->values_last_id, &l_values_count);
    if (l_store_objs && l_values_count)
        a_msg->values_raw_last_id = l_store_objs[l_values_count - 1].id + 1;
    debug_if(g_dap_global_db_debug_more, L_DEBUG, "Get all raw request from group %s recieved %zu values from total %zu",
                                                   a_msg->group, l_values_count, l_values_remains);
    // Call callback if present
    if(a_msg->callback_results_raw)
        a_msg->callback_results_raw(s_context_global_db,
                                    l_store_objs ? DAP_GLOBAL_DB_RC_SUCCESS : DAP_GLOBAL_DB_RC_NO_RESULTS,
                                    a_msg->group, l_values_remains, l_values_count,
                                    l_store_objs, a_msg->callback_arg);
    // Clean memory
    dap_store_obj_free(l_store_objs,l_values_count);

    /*// Here we also check if the reply was with zero values. To prevent endless loop we don't resend query request in such cases
    if (l_values_count && l_values_count != l_values_remains) {
        // Have to process callback again
        int l_ret = dap_events_socket_queue_ptr_send(s_context_global_db->queue_io,a_msg);
        debug_if(g_dap_global_db_debug_more, L_NOTICE, "Resending get all raw request values_remains:%zu", l_values_remains);
        if (!l_ret)
            return false; // Don't delete it because it just sent again to the queue{
        log_it(L_ERROR, "Can't resend i/o message for opcode GET_ALL_RAW values_remains:%zu error code %d", l_values_remains, l_ret);
    }*/
    return true; // All values are sent
}

/**
 * @brief The store_objs_get struct
 */

static void s_get_all_raw_sync_callback(UNUSED_ARG dap_global_db_context_t *a_global_db_context,
                                        UNUSED_ARG int a_rc, UNUSED_ARG const char *a_group,
                                        const size_t a_values_total, const size_t a_values_count,
                                        dap_store_obj_t *a_values, void *a_arg)
{
    assert(a_arg);
    dap_global_db_callback_arg_uid_t *l_uid = a_arg;
    pthread_mutex_lock(&s_context_global_db->data_callbacks_mutex);
    struct sync_obj_data_callback *l_args = s_global_db_find_callback_data(a_global_db_context, *l_uid);
    DAP_DELETE(l_uid);
    if (!l_args) {
        pthread_mutex_unlock(&s_context_global_db->data_callbacks_mutex);
        return;
    }
    // TODO make incremental copy
    l_args->get_store_objs.objs = dap_store_obj_copy(a_values, a_values_count);
    l_args->get_objs.objs_count += a_values_count;
    if (a_values_count != a_values_total) {
        log_it(L_WARNING, "Got only %zu records from %zu", a_values_count, a_values_total);
    }
    l_args->hdr.called = true;
    pthread_cond_signal(&l_args->hdr.cond);
    pthread_mutex_unlock(&s_context_global_db->data_callbacks_mutex);
}

dap_store_obj_t* dap_global_db_get_all_raw_sync(const char *a_group, uint64_t a_first_id, size_t *a_objs_count)
{
    return dap_global_db_get_all_raw_unsafe(s_context_global_db, a_group, a_first_id, a_objs_count);
#if 0
    if (dap_global_db_context_current() == s_context_global_db)
        return dap_global_db_get_all_raw_unsafe(s_context_global_db, a_group, a_first_id, a_objs_count);

    struct sync_obj_data_callback *l_args = s_global_db_obj_data_callback_new();
    debug_if(g_dap_global_db_debug_more, L_DEBUG, "get_all_raw sync call executes for group %s", a_group);

    if (!dap_global_db_get_all_raw(a_group, a_first_id, a_objs_count ? *a_objs_count : 0,
                                   s_get_all_raw_sync_callback, DAP_DUP(&l_args->uid)))
        s_global_db_obj_data_callback_wait(l_args, "get_all_raw");
    if (a_objs_count)
        *a_objs_count = l_args->get_store_objs.objs_count;
    dap_store_obj_t *l_ret = l_args->get_store_objs.objs;
    s_global_db_obj_data_callback_destroy(l_args);
    return l_ret;
#endif
}

/* *** Set functions group *** */

int s_set_unsafe_with_ts(dap_global_db_context_t *a_global_db_context, const char *a_group, const char *a_key, const void *a_value,
                             const size_t a_value_length, bool a_pin_value, dap_nanotime_t a_timestamp)
{
    if (!a_group || !a_key) {
        log_it(L_WARNING, "Trying to set GDB object with NULL group or key param");
        return -1;
    }

    dap_store_obj_t l_store_data = { 0 };

    l_store_data.key = (char *)a_key ;
    l_store_data.flags = a_pin_value ? RECORD_PINNED : 0 ;
    l_store_data.value_len =  a_value_length;
    l_store_data.value = (uint8_t *)a_value;
    l_store_data.group = (char*) a_group;
    l_store_data.timestamp = a_timestamp;
    l_store_data.group_len = dap_strlen(l_store_data.group);
    if (a_key == NULL) {
        l_store_data.key_len = 0;
    } else {
        l_store_data.key_len = dap_strlen(l_store_data.key);
    }
    l_store_data.type = DAP_DB$K_OPTYPE_ADD;

    int l_res = dap_global_db_driver_apply(&l_store_data, 1);
    if (l_res == 0) {
        s_record_del_history_del(a_group, a_key);
        s_change_notify(a_global_db_context, &l_store_data);
    }
    return l_res;
}

/**
 * @brief dap_global_db_set_unsafe
 * @param a_global_db_context
 * @param a_group
 * @param a_key
 * @param a_value
 * @param a_value_length
 * @param a_pin_value
 * @return
 */
int dap_global_db_set_unsafe(dap_global_db_context_t *a_global_db_context, const char *a_group, const char *a_key, const void *a_value,
                             const size_t a_value_length, bool a_pin_value)
{
    dap_nanotime_t l_ts_now = dap_nanotime_now();
    return s_set_unsafe_with_ts(a_global_db_context, a_group, a_key, a_value, a_value_length, a_pin_value, l_ts_now);
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
    if(s_context_global_db == NULL){
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

    int l_ret = dap_events_socket_queue_ptr_send(s_context_global_db->queue_io,l_msg);
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
static bool s_msg_opcode_set(struct queue_io_msg * a_msg)
{
    dap_nanotime_t l_ts_now = dap_nanotime_now();
    int l_res = s_set_unsafe_with_ts(s_context_global_db, a_msg->group, a_msg->key, a_msg->value,
                                     a_msg->value_length, a_msg->value_is_pinned, l_ts_now);
    if (l_res == 0) {
        if(a_msg->callback_result)
            a_msg->callback_result(s_context_global_db, DAP_GLOBAL_DB_RC_SUCCESS, a_msg->group, a_msg->key,
                                   a_msg->value, a_msg->value_length, l_ts_now,
                                   a_msg->value_is_pinned , a_msg->callback_arg );
    } else {
        log_it(L_ERROR, "Save error for %s:%s code %d", a_msg->group,a_msg->key, l_res);
        if(a_msg->callback_result)
            a_msg->callback_result(s_context_global_db, DAP_GLOBAL_DB_RC_ERROR , a_msg->group, a_msg->key,
                                   a_msg->value, a_msg->value_length, l_ts_now,
                                   a_msg->value_is_pinned , a_msg->callback_arg );
    }
    return true;
}


/**
 * @brief s_objs_set_callback
 * @param a_global_db_context
 * @param a_rc
 * @param a_group
 * @param a_key
 * @param a_value
 * @param a_value_len
 * @param a_value_ts
 * @param a_is_pinned
 * @param a_arg
 */
static void s_sync_op_result_callback(UNUSED_ARG dap_global_db_context_t *a_global_db_context, int a_rc,
                                      UNUSED_ARG const char *a_group, UNUSED_ARG const char *a_key,
                                      UNUSED_ARG const void *a_value, UNUSED_ARG const size_t a_value_len,
                                      UNUSED_ARG dap_nanotime_t a_value_ts, UNUSED_ARG bool a_is_pinned,
                                      void * a_arg)
{
    assert(a_arg);
    dap_global_db_callback_arg_uid_t *l_uid = a_arg;
    pthread_mutex_lock(&s_context_global_db->data_callbacks_mutex);
    struct sync_obj_data_callback *l_args = s_global_db_find_callback_data(a_global_db_context, *l_uid);
    DAP_DELETE(l_uid);
    if (!l_args) {
        pthread_mutex_unlock(&s_context_global_db->data_callbacks_mutex);
        return;
    }
    l_args->op_result.result = a_rc;
    l_args->hdr.called = true;
    pthread_cond_signal(&l_args->hdr.cond);
    pthread_mutex_unlock(&s_context_global_db->data_callbacks_mutex);
}

/**
 * @brief Set value into the GlobalDB and waits for result
 * @param a_group
 * @param a_key
 * @param a_value
 * @param a_value_length
 * @param a_pin_value
 * @return 0 if success others if not
 */
int dap_global_db_set_sync(const char * a_group, const char *a_key, const void * a_value, const size_t a_value_length, bool a_pin_value)
{
    return dap_global_db_set_unsafe(s_context_global_db, a_group, a_key, a_value, a_value_length, a_pin_value);
#if 0
    if (dap_global_db_context_current() == s_context_global_db)
        return dap_global_db_set_unsafe(s_context_global_db, a_group, a_key, a_value, a_value_length, a_pin_value);

    struct sync_obj_data_callback *l_args = s_global_db_obj_data_callback_new();
    debug_if(g_dap_global_db_debug_more, L_DEBUG, "set sync call executes for group \"%s\" key \"%s\"", a_group, a_key);

    if (!dap_global_db_set(a_group, a_key, a_value, a_value_length, a_pin_value,
                           s_sync_op_result_callback, DAP_DUP(&l_args->uid))) {
        s_global_db_obj_data_callback_wait(l_args, "set");
     } else
        l_args->op_result.result = DAP_GLOBAL_DB_RC_ERROR;

    int l_ret = l_args->op_result.result;
    s_global_db_obj_data_callback_destroy(l_args);
    return l_ret;
#endif
}

/* *** Set_raw functions group *** */

int dap_global_db_set_raw_unsafe(dap_global_db_context_t *a_global_db_context, dap_store_obj_t *a_store_objs, size_t a_store_objs_count)
{
    int l_ret = dap_global_db_driver_apply(a_store_objs, a_store_objs_count);
    if (l_ret >= 0) {
        for (size_t i = 0; i < a_store_objs_count; i++) {
            int l_res_del = 0;
            if (a_store_objs[i].type == DAP_DB$K_OPTYPE_ADD)
                l_res_del = s_record_del_history_del(a_store_objs[i].group, a_store_objs[i].key);
            else if (a_store_objs[i].type == DAP_DB$K_OPTYPE_DEL)
                l_res_del = s_record_del_history_add(a_store_objs[i].group, (char *)a_store_objs[i].key,
                                                     a_store_objs[i].timestamp);
            if (!l_res_del && !l_ret) {
                s_change_notify(a_global_db_context, &a_store_objs[i]);
            }
        }
    } else
        log_it(L_ERROR,"Can't save raw gdb data, code %d ", l_ret);
    return l_ret;
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
    if(s_context_global_db == NULL){
        log_it(L_ERROR, "GlobalDB context is not initialized, can't call dap_global_db_set");
        return DAP_GLOBAL_DB_RC_ERROR;
    }
    struct queue_io_msg * l_msg = DAP_NEW_Z(struct queue_io_msg);
    if (!l_msg) {
        log_it(L_CRITICAL, "Memory allocation error");
        return -1;
    }
    l_msg->opcode = MSG_OPCODE_SET_RAW;
    l_msg->callback_arg = a_arg;
    l_msg->callback_results_raw = a_callback;

    l_msg->values_raw = dap_store_obj_copy(a_store_objs, a_store_objs_count) ;
    l_msg->values_raw_total = a_store_objs_count;

    int l_ret = dap_events_socket_queue_ptr_send(s_context_global_db->queue_io,l_msg);
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
static bool s_msg_opcode_set_raw(struct queue_io_msg * a_msg)
{
    int l_ret = -1;
    if (a_msg->values_raw_total > 0)
        l_ret = dap_global_db_set_raw_unsafe(s_context_global_db, a_msg->values_raw, a_msg->values_raw_total);
    if (a_msg->callback_results_raw)
        a_msg->callback_results_raw(s_context_global_db,
                                    l_ret == 0 ? DAP_GLOBAL_DB_RC_SUCCESS : DAP_GLOBAL_DB_RC_ERROR,
                                    a_msg->group, a_msg->values_raw_total, a_msg->values_raw_total,
                                    a_msg->values_raw, a_msg->callback_arg);
    return true;
}

static void s_sync_op_raw_callback(UNUSED_ARG dap_global_db_context_t *a_global_db_context, int a_rc,
                                   UNUSED_ARG const char *a_group, UNUSED_ARG const size_t a_values_current,
                                   UNUSED_ARG const size_t a_values_count, UNUSED_ARG dap_store_obj_t *a_values,
                                   void *a_arg)
{
    assert(a_arg);
    dap_global_db_callback_arg_uid_t *l_uid = a_arg;
    pthread_mutex_lock(&s_context_global_db->data_callbacks_mutex);
    struct sync_obj_data_callback *l_args = s_global_db_find_callback_data(a_global_db_context, *l_uid);
    DAP_DELETE(l_uid);
    if (!l_args) {
        pthread_mutex_unlock(&s_context_global_db->data_callbacks_mutex);
        return;
    }
    l_args->op_result.result = a_rc;
    l_args->hdr.called = true;
    pthread_cond_signal(&l_args->hdr.cond);
    pthread_mutex_unlock(&s_context_global_db->data_callbacks_mutex);
}

int dap_global_db_set_raw_sync(dap_store_obj_t *a_store_objs, size_t a_store_objs_count)
{
    return dap_global_db_set_raw_unsafe(s_context_global_db, a_store_objs, a_store_objs_count);
#if 0
    if (dap_global_db_context_current() == s_context_global_db)
        return dap_global_db_set_raw_unsafe(s_context_global_db, a_store_objs, a_store_objs_count);

    struct sync_obj_data_callback *l_args = s_global_db_obj_data_callback_new();
    debug_if(g_dap_global_db_debug_more, L_DEBUG, "set_raw sync call executes");

    if (!dap_global_db_set_raw(a_store_objs, a_store_objs_count, s_sync_op_raw_callback, DAP_DUP(&l_args->uid))) {
        s_global_db_obj_data_callback_wait(l_args, "set_raw");
     } else
        l_args->op_result.result = DAP_GLOBAL_DB_RC_ERROR;

    int l_ret = l_args->op_result.result;
    s_global_db_obj_data_callback_destroy(l_args);
    return l_ret;
#endif
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
    if(s_context_global_db == NULL){
        log_it(L_ERROR, "GlobalDB context is not initialized, can't call dap_global_db_set");
        return DAP_GLOBAL_DB_RC_ERROR;
    }
    struct queue_io_msg * l_msg = DAP_NEW_Z(struct queue_io_msg);
    if (!l_msg) {
        log_it(L_CRITICAL, "Memory allocation error");
        return -1;
    }
    l_msg->opcode = MSG_OPCODE_SET_MULTIPLE;
    l_msg->group = dap_strdup(a_group);
    l_msg->values = a_values;
    l_msg->values_count = a_values_count;
    l_msg->callback_arg = a_arg;
    l_msg->callback_results = a_callback;

    int l_ret = dap_events_socket_queue_ptr_send(s_context_global_db->queue_io,l_msg);
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
static bool s_msg_opcode_set_multiple_zc(struct queue_io_msg * a_msg)
{
    int l_ret = -1;
    size_t i=0;
    if(a_msg->values_count>0){
        dap_store_obj_t l_store_obj = {};
        l_ret = 0;
        for(;  i < a_msg->values_count && l_ret == 0  ; i++ ) {
            l_store_obj.type = DAP_DB$K_OPTYPE_ADD;
            l_store_obj.flags = a_msg->values[i].is_pinned;
            l_store_obj.key =  a_msg->values[i].key;
            l_store_obj.key_len = strlen(a_msg->values[i].key);
            l_store_obj.group = a_msg->group;
            l_store_obj.group_len = dap_strlen(a_msg->group);
            l_store_obj.value = a_msg->values[i].value;
            l_store_obj.value_len = a_msg->values[i].value_len;
            l_store_obj.timestamp = a_msg->values[i].timestamp;
            s_record_del_history_del(a_msg->group, a_msg->values[i].key);
            l_ret = dap_global_db_driver_add(&l_store_obj,1);
            s_change_notify(s_context_global_db, &l_store_obj);
        }
    }
    if(a_msg->callback_results){
        a_msg->callback_results(s_context_global_db,
                                l_ret == 0 ? DAP_GLOBAL_DB_RC_SUCCESS : DAP_GLOBAL_DB_RC_ERROR,
                                a_msg->group, i, a_msg->values_count,
                                a_msg->values, a_msg->callback_arg);
    }
    dap_global_db_objs_delete( a_msg->values, a_msg->values_count);

    return true;
}

/* *** Pin/unpin functions group *** */

int s_db_object_pin_unsafe(dap_global_db_context_t *a_global_db_context, const char *a_group, const char *a_key, bool a_pin)
{
    int l_res = DAP_GLOBAL_DB_RC_NO_RESULTS;
    dap_store_obj_t *l_store_obj = dap_global_db_get_raw_unsafe(a_global_db_context, a_group, a_key);
    if (l_store_obj) {
        if (a_pin)
            l_store_obj->flags |= RECORD_PINNED;
        else
            l_store_obj->flags ^= RECORD_PINNED;
        l_res = dap_global_db_set_raw_unsafe(a_global_db_context, l_store_obj, 1);
        if (l_res == 0) {
            s_record_del_history_del(a_group, a_key);
            s_change_notify(a_global_db_context, l_store_obj);
        } else {
            log_it(L_ERROR,"Can't save pinned gdb data, code %d ", l_res);
            l_res = DAP_GLOBAL_DB_RC_ERROR;
        }
    }
    dap_store_obj_free_one(l_store_obj);
    return l_res;
}

int dap_global_db_pin_unsafe(dap_global_db_context_t *a_global_db_context, const char *a_group, const char *a_key)
{
    return s_db_object_pin_unsafe(a_global_db_context, a_group, a_key, true);
}

int dap_global_db_unpin_unsafe(dap_global_db_context_t *a_global_db_context, const char *a_group, const char *a_key)
{
    return s_db_object_pin_unsafe(a_global_db_context, a_group, a_key, false);
}

int s_db_object_pin(const char *a_group, const char *a_key, dap_global_db_callback_result_t a_callback, void *a_arg, bool a_pin)
{
    if(s_context_global_db == NULL){
        log_it(L_ERROR, "GlobalDB context is not initialized, can't call dap_global_db_pin");
        return DAP_GLOBAL_DB_RC_ERROR;
    }
    struct queue_io_msg * l_msg = DAP_NEW_Z(struct queue_io_msg);
    if (!l_msg) {
        log_it(L_CRITICAL, "Memory allocation error");
        return -1;
    }
    l_msg->opcode = MSG_OPCODE_PIN;
    l_msg->group = dap_strdup(a_group);
    l_msg->key = dap_strdup(a_key);
    l_msg->callback_arg = a_arg;
    l_msg->callback_result = a_callback;
    l_msg->value_is_pinned = a_pin;

    int l_ret = dap_events_socket_queue_ptr_send(s_context_global_db->queue_io,l_msg);
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
static bool s_msg_opcode_pin(struct queue_io_msg * a_msg)
{
    int l_res = s_db_object_pin_unsafe(s_context_global_db, a_msg->group, a_msg->key, a_msg->value_is_pinned);
    if (a_msg->callback_result)
        a_msg->callback_result(s_context_global_db, l_res, a_msg->group, a_msg->key,
                               NULL, 0, 0, a_msg->value_is_pinned, a_msg->callback_arg);
    return true;
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

int s_db_object_pin_sync(const char *a_group, const char *a_key, bool a_pin)
{
    return s_db_object_pin_unsafe(s_context_global_db, a_group, a_key, a_pin);
#if 0
    if (dap_global_db_context_current() == s_context_global_db)
        return s_db_object_pin_unsafe(s_context_global_db, a_group, a_key, a_pin);

    struct sync_obj_data_callback *l_args = s_global_db_obj_data_callback_new();
    debug_if(g_dap_global_db_debug_more, L_DEBUG, "%s sync call executes for group \"%s\" key \"%s\"",
                                                  a_pin ? "pin" : "unpin", a_group, a_key);
    if (!s_db_object_pin(a_group, a_key, s_sync_op_result_callback, DAP_DUP(&l_args->uid), a_pin)) {
        s_global_db_obj_data_callback_wait(l_args, "pin");
    } else
        l_args->op_result.result = DAP_GLOBAL_DB_RC_ERROR;

    int l_ret = l_args->op_result.result;
    s_global_db_obj_data_callback_destroy(l_args);
    return l_ret;
#endif
}

/**
 * @brief dap_global_db_pin_sync
 * @param a_group
 * @param a_key
 * @return
 */
int dap_global_db_pin_sync(const char *a_group, const char *a_key)
{
    return s_db_object_pin_sync(a_group, a_key, true);
}

/**
 * @brief dap_global_db_unpin_sync
 * @param a_group
 * @param a_key
 * @return
 */
int dap_global_db_unpin_sync(const char *a_group, const char *a_key)
{
    return s_db_object_pin_sync(a_group, a_key, false);
}

/* *** Del functions group *** */

/**
 * @brief dap_global_db_del_unsafe
 * @param a_group
 * @param a_key
 * @return
 */
int dap_global_db_del_unsafe(dap_global_db_context_t *a_global_db_context, const char * a_group, const char *a_key)
{
    dap_store_obj_t l_store_obj = {
        .key        = a_key,
        .group      = (char*)a_group,
        .type       = DAP_DB$K_OPTYPE_DEL,
        .timestamp  = dap_nanotime_now()
    };

    int l_res = dap_global_db_driver_apply(&l_store_obj, 1);

    if (a_key) {
        if (l_res >= 0)
            l_res = s_record_del_history_add(l_store_obj.group, (char *)l_store_obj.key, l_store_obj.timestamp);
        // do not notify group deletion or deletion error
        if (!l_res)
            s_change_notify(a_global_db_context, &l_store_obj);
    }
    return l_res;
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
    if (s_context_global_db == NULL) {
        log_it(L_ERROR, "GlobalDB context is not initialized, can't call dap_global_db_del");
        return DAP_GLOBAL_DB_RC_ERROR;
    }
    struct queue_io_msg * l_msg = DAP_NEW_Z(struct queue_io_msg);
    if (!l_msg) {
        log_it(L_CRITICAL, "Memory allocation error");
        return -1;
    }
    l_msg->opcode = MSG_OPCODE_DELETE;
    l_msg->group = dap_strdup(a_group);
    l_msg->key = dap_strdup(a_key);
    l_msg->callback_arg = a_arg;
    l_msg->callback_result = a_callback;

    int l_ret = dap_events_socket_queue_ptr_send(s_context_global_db->queue_io,l_msg);
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
static bool s_msg_opcode_delete(struct queue_io_msg * a_msg)
{
    int l_res = dap_global_db_del_unsafe(s_context_global_db, a_msg->group, a_msg->key);

    if(a_msg->callback_result){
        a_msg->callback_result(s_context_global_db,  l_res==0 ? DAP_GLOBAL_DB_RC_SUCCESS:
                                        DAP_GLOBAL_DB_RC_ERROR,
                                a_msg->group, a_msg->key,
                               NULL, 0, 0 , false, a_msg->callback_arg );
    }

    return true;
}

/**
 * @brief dap_global_db_del_sync
 * @param a_group
 * @param a_key
 * @return
 */
int dap_global_db_del_sync(const char *a_group, const char *a_key)
{
    return dap_global_db_del_unsafe(s_context_global_db, a_group, a_key);
#if 0
    if (dap_global_db_context_current() == s_context_global_db)
        return dap_global_db_del_unsafe(s_context_global_db, a_group, a_key);

    struct sync_obj_data_callback *l_args = s_global_db_obj_data_callback_new();
    if (!dap_global_db_del(a_group, a_key, s_sync_op_result_callback, DAP_DUP(&l_args->uid))) {
        s_global_db_obj_data_callback_wait(l_args, "del");
    } else
        l_args->op_result.result = DAP_GLOBAL_DB_RC_ERROR;

    int l_ret = l_args->op_result.result;
    s_global_db_obj_data_callback_destroy(l_args);
    return l_ret;
#endif
}

/* *** Flush functions group *** */

/**
 * @brief dap_global_db_flush_sync
 * @return
 */
int dap_global_db_flush_unsafe(UNUSED_ARG dap_global_db_context_t *a_global_db_context)
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
    if(s_context_global_db == NULL){
        log_it(L_ERROR, "GlobalDB context is not initialized, can't call dap_global_db_delete");
        return DAP_GLOBAL_DB_RC_ERROR;
    }
    struct queue_io_msg * l_msg = DAP_NEW_Z(struct queue_io_msg);
    if (!l_msg) {
        log_it(L_CRITICAL, "Memory allocation error");
        return -1;
    }
    l_msg->opcode = MSG_OPCODE_FLUSH;
    l_msg->callback_arg = a_arg;
    l_msg->callback_result = a_callback;

    int l_ret = dap_events_socket_queue_ptr_send(s_context_global_db->queue_io,l_msg);
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
static bool s_msg_opcode_flush(struct queue_io_msg * a_msg)
{
    int l_res = dap_global_db_flush_unsafe(s_context_global_db);
    if (a_msg->callback_result)
        a_msg->callback_result(s_context_global_db, l_res ? DAP_GLOBAL_DB_RC_ERROR : DAP_GLOBAL_DB_RC_SUCCESS,
                                NULL, NULL, NULL, 0, 0, false, a_msg->callback_arg);
    return true;
}

int dap_global_db_flush_sync()
{
    return dap_global_db_flush_unsafe(s_context_global_db);
#if 0
    if (dap_global_db_context_current() == s_context_global_db)
        return dap_global_db_flush_unsafe(s_context_global_db);

    struct sync_obj_data_callback *l_args = s_global_db_obj_data_callback_new();
    if (!dap_global_db_flush(s_sync_op_result_callback, DAP_DUP(&l_args->uid))) {
        s_global_db_obj_data_callback_wait(l_args, "flush");
    } else
        l_args->op_result.result = DAP_GLOBAL_DB_RC_ERROR;

    int l_ret = l_args->op_result.result;
    s_global_db_obj_data_callback_destroy(l_args);
    return l_ret;
#endif
}

/* *** Other functions *** */

/**
 * @brief Copies memory of an objs array.
 * @param objs a pointer to the first object of the array
 * @param a_count a number of objects in the array
 * @return (none)
 */
dap_global_db_obj_t *dap_global_db_objs_copy(dap_global_db_obj_t *a_objs, size_t a_count)
{   /* Sanity checks */
    if (!a_objs || !a_count)
        return NULL;

    dap_global_db_obj_t *l_ret = DAP_NEW_Z_SIZE(dap_global_db_obj_t, a_count * sizeof(dap_global_db_obj_t));
    /* Run over array's elements */
    for (dap_global_db_obj_t *l_obj = a_objs, *l_cur = l_ret; a_count--; l_cur++, l_obj++) {
        *l_cur = *l_obj;
        l_cur->key = dap_strdup(l_obj->key);
        if (l_obj->value && l_obj->value_len)
            l_cur->value = DAP_DUP_SIZE(l_obj->value, l_obj->value_len);
    }
    return l_ret;
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
 * @brief Execute callback in GlobalDB context
 * @param a_callback  Callback function
 * @param arg Custom argument
 * @return 0 if success, others if not
 */
int dap_global_db_context_exec (dap_global_db_callback_t a_callback, void * a_arg)
{
    if(s_context_global_db == NULL){
        log_it(L_ERROR, "GlobalDB context is not initialized, can't call dap_global_db_context_exec");
        return DAP_GLOBAL_DB_RC_ERROR;
    }
    struct queue_io_msg * l_msg = DAP_NEW_Z(struct queue_io_msg);
    if (!l_msg) {
        log_it(L_CRITICAL, "Memory allocation error");
        return -1;
    }
    l_msg->opcode = MSG_OPCODE_CONTEXT_EXEC;
    l_msg->callback_arg = a_arg;
    l_msg->callback = a_callback;

    int l_ret = dap_events_socket_queue_ptr_send(s_context_global_db->queue_io,l_msg);
    if (l_ret != 0){
        log_it(L_ERROR, "Can't exec context_exec request, code %d", l_ret);
        s_queue_io_msg_delete(l_msg);
    }
    return l_ret;

}

/**
 * @brief Execute callback in GlobalDB context
 * @param a_msg
 * @return
 */
static bool s_msg_opcode_context_exec(struct queue_io_msg * a_msg)
{
    if(a_msg->callback)
        a_msg->callback(s_context_global_db, a_msg->callback_arg );
    return true;
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
        case MSG_OPCODE_CONTEXT_EXEC:   return "CONTEXT_EXEC";
        default:                        return "UNKNOWN";
    }
}

/**
 * @brief s_queue_io_callback
 * @details Queue I/O process callback
 * @param a_es
 * @param a_arg
 */
static void s_queue_io_callback( dap_events_socket_t * a_es, void * a_arg)
{
    (void) a_es;
    struct queue_io_msg * l_msg = (struct queue_io_msg *) a_arg;
    bool l_msg_delete = false; // if msg resent again it shouldn't be deleted in the end of callback
    assert(l_msg);

    debug_if(g_dap_global_db_debug_more, L_NOTICE, "Received GlobalDB I/O message with opcode %s", s_msg_opcode_to_str(l_msg->opcode) );

    switch(l_msg->opcode){
        case MSG_OPCODE_GET:          l_msg_delete = s_msg_opcode_get(l_msg); break;
        case MSG_OPCODE_GET_RAW:      l_msg_delete = s_msg_opcode_get_raw(l_msg); break;
        case MSG_OPCODE_GET_LAST:     l_msg_delete = s_msg_opcode_get_last(l_msg); break;
        case MSG_OPCODE_GET_LAST_RAW: l_msg_delete = s_msg_opcode_get_last_raw(l_msg); break;
        case MSG_OPCODE_GET_DEL_TS:   l_msg_delete = s_msg_opcode_get_del_ts(l_msg); break;
        case MSG_OPCODE_GET_ALL:      l_msg_delete = s_msg_opcode_get_all(l_msg); break;
        case MSG_OPCODE_GET_ALL_RAW:  l_msg_delete = s_msg_opcode_get_all_raw(l_msg); break;
        case MSG_OPCODE_SET:          l_msg_delete = s_msg_opcode_set(l_msg); break;
        case MSG_OPCODE_SET_MULTIPLE: l_msg_delete = s_msg_opcode_set_multiple_zc(l_msg); break;
        case MSG_OPCODE_SET_RAW:      l_msg_delete = s_msg_opcode_set_raw(l_msg); break;
        case MSG_OPCODE_PIN:          l_msg_delete = s_msg_opcode_pin(l_msg); break;
        case MSG_OPCODE_DELETE:       l_msg_delete = s_msg_opcode_delete(l_msg); break;
        case MSG_OPCODE_FLUSH:        l_msg_delete = s_msg_opcode_flush(l_msg); break;
        case MSG_OPCODE_CONTEXT_EXEC: l_msg_delete = s_msg_opcode_context_exec(l_msg); break;
        default:{
            log_it(L_WARNING, "Message with undefined opcode %d received in queue_io",
                   l_msg->opcode);
        }
    }
    if( l_msg_delete )
        s_queue_io_msg_delete(l_msg);
}


/**
 * @brief Adds data to the history log
 *
 * @param a_store_data a pointer to an object
 * @return (none)
 */
static void s_change_notify(dap_global_db_context_t *a_context, dap_store_obj_t * a_store_obj)
{
    dap_global_db_notify_item_t *l_notify_item = dap_global_db_get_notify_group(a_context->instance, a_store_obj->group);
    if (l_notify_item && l_notify_item->callback_notify)
        l_notify_item->callback_notify(a_context, a_store_obj, l_notify_item->callback_arg);
}

/*
* @brief s_record_del_history_del Deletes info about the deleted object from the database
* @param a_key an object key string, looked like "0x8FAFBD00B..."
* @param a_group a group name string, for example "kelvin-testnet.nodes"
* @return If successful, returns true; otherwise, false.
*/
static int s_record_del_history_del(const char *a_group, const char *a_key)
{
    if (!a_key)
        return -1;
    char l_group[DAP_GLOBAL_DB_GROUP_NAME_SIZE_MAX];
    dap_snprintf(l_group, sizeof(l_group) - 1, "%s.del", a_group);
    dap_store_obj_t store_data = {
        .key        = (char*)a_key,
        .group      = l_group
    };

    return dap_global_db_driver_is(store_data.group, store_data.key)
            ? dap_global_db_driver_delete(&store_data, 1)
            : 0;
}

/**
 * @brief s_record_del_history_add Adds info about the deleted entry to the database.
 * @param a_group a group name string
 * @param a_key an object key string
 * @param a_timestamp an object time stamp
 * @return True if successful, false otherwise.
 */
static int s_record_del_history_add(char *a_group, char *a_key, uint64_t a_timestamp)
{
    if (!a_group || !a_key)
        return -1;
    char l_group[DAP_GLOBAL_DB_GROUP_NAME_SIZE_MAX];
    dap_snprintf(l_group, sizeof(l_group) - 1, "%s.del", a_group);
    dap_store_obj_t store_data = {
        .key        = a_key,
        .group      = l_group,
        .timestamp = a_timestamp
    };
    return dap_global_db_driver_add(&store_data, 1);
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
 * @brief s_context_callback_started
 * @details GlobalDB context started
 * @param a_context
 * @param a_arg
 */
static void s_context_callback_started( dap_context_t * a_context, void *a_arg)
{
    // Init its own queue
    s_context_global_db->queue_io = dap_context_create_queue(a_context, s_queue_io_callback);

    // Create arrays of inputs for connection with workers and proc threads
    s_context_global_db->queue_worker_io_input = DAP_NEW_Z_SIZE(dap_events_socket_t*, sizeof(dap_events_socket_t*) *
                                                                dap_events_thread_get_count() );
    s_context_global_db->queue_worker_ch_io_input = DAP_NEW_Z_SIZE(dap_events_socket_t*, sizeof(dap_events_socket_t*) *
                                                                dap_events_thread_get_count() );
    s_context_global_db->queue_worker_callback_input = DAP_NEW_Z_SIZE(dap_events_socket_t*, sizeof(dap_events_socket_t*) *
                                                                dap_events_thread_get_count() );

    s_context_global_db->queue_proc_thread_callback_input = DAP_NEW_Z_SIZE(dap_events_socket_t*, sizeof(dap_events_socket_t*) *
                                                                dap_events_thread_get_count() );

    // Fullful arrays with queue inputs
    for (uint32_t i = 0; i < dap_events_thread_get_count(); i++){
        dap_worker_t * l_worker = dap_events_worker_get(i);
        dap_stream_worker_t * l_stream_worker = DAP_STREAM_WORKER(l_worker);
        dap_proc_thread_t *l_proc_thread = dap_proc_thread_get(i);
        assert(l_worker);
        assert(l_proc_thread);

        // Input for I/O operations on worker
        s_context_global_db->queue_worker_io_input[i] =
                dap_events_socket_queue_ptr_create_input( l_worker->queue_es_io);
        dap_context_add( a_context, s_context_global_db->queue_worker_io_input[i]);

        // Input for ch I/O operations on worker
        s_context_global_db->queue_worker_ch_io_input[i] =
                dap_events_socket_queue_ptr_create_input( l_stream_worker->queue_ch_io );
        dap_context_add( a_context, s_context_global_db->queue_worker_ch_io_input[i]);

        // Input for callback queue on worker
        s_context_global_db->queue_worker_callback_input[i] =
                dap_events_socket_queue_ptr_create_input( l_worker->queue_callback);
        dap_context_add( a_context, s_context_global_db->queue_worker_callback_input[i]);

        // Input for callback queue on proc thread
        s_context_global_db->queue_proc_thread_callback_input[i] =
                dap_events_socket_queue_ptr_create_input( l_proc_thread->proc_queue->esocket );
        dap_context_add( a_context, s_context_global_db->queue_proc_thread_callback_input[i]);
    }
}


/**
 * @brief s_context_callback_stopped
 * @details Stop and destroy callback for GlobalDB context
 * @param a_context
 * @param a_arg
 */
static void s_context_callback_stopped( dap_context_t * a_context, void *a_arg)
{
    // Fullful arrays with queue inputs
    // Check ticket 9030
    for (uint32_t i = 0; i < dap_events_thread_get_count(); i++){
        dap_events_socket_remove_and_delete_unsafe(s_context_global_db->queue_worker_io_input[i], true);
        dap_events_socket_remove_and_delete_unsafe(s_context_global_db->queue_worker_callback_input[i], true);
        dap_events_socket_remove_and_delete_unsafe(s_context_global_db->queue_proc_thread_callback_input[i], true);
    }
    DAP_DELETE(s_context_global_db->queue_worker_io_input);
    DAP_DELETE(s_context_global_db->queue_worker_callback_input);
    DAP_DELETE(s_context_global_db->queue_proc_thread_callback_input);
    dap_events_socket_remove_and_delete_unsafe(s_context_global_db->queue_io, false);
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
static void s_check_db_version_callback_get (dap_global_db_context_t * a_global_db_context, int a_errno, const char * a_group, const char * a_key,
                                             const void * a_value, const size_t a_value_len,
                                             dap_nanotime_t value_ts, bool a_is_pinned, void * a_arg)
{
    int res = 0;


    if(a_errno != 0){ // No DB at all
        log_it(L_NOTICE, "No GlobalDB version at all, creating the new GlobalDB from scratch");
        a_global_db_context->instance->version = DAP_GLOBAL_DB_VERSION;
        if ( (res = dap_global_db_set(DAP_GLOBAL_DB_LOCAL_GENERAL, "gdb_version",
                                      &a_global_db_context->instance->version,
                                      sizeof(uint16_t), false,
                                      s_check_db_version_callback_set, NULL) ) != 0){
            log_it(L_NOTICE, "Can't set GlobalDB version, code %d", res);
            goto lb_exit;
        }
        return; // In this case the condition broadcast should happens in s_check_db_version_callback_set()

    }

    if (a_value_len == sizeof(uint16_t))
        a_global_db_context->instance->version = *(uint16_t *)a_value;

    if( a_global_db_context->instance->version < DAP_GLOBAL_DB_VERSION) {
        log_it(L_NOTICE, "GlobalDB version %u, but %u required. The current database will be recreated",
               a_global_db_context->instance->version, DAP_GLOBAL_DB_VERSION);
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
                                                         dap_path_get_basename(a_global_db_context->instance->storage_path),
                                                         a_global_db_context->instance->version, l_ts_now_str);
            char *l_output_file_path = dap_build_filename(l_storage_path, "../", l_output_file_name, NULL);
            // Create backup as TAR file
            if(dap_tar_directory(l_storage_path, l_output_file_path)) {
#endif
                // Delete database file or directory
                dap_rm_rf(l_storage_path);
            }
            else {
                log_it(L_ERROR, "Can't backup GlobalDB version %d", a_global_db_context->instance->version);
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
            a_global_db_context->instance->version = DAP_GLOBAL_DB_VERSION;
            if ( (res = dap_global_db_set(DAP_GLOBAL_DB_LOCAL_GENERAL, "gdb_version",
                                          &a_global_db_context->instance->version,
                                          sizeof(uint16_t), false,
                                          s_check_db_version_callback_set, NULL) ) != 0){
                log_it(L_NOTICE, "Can't set GlobalDB version, code %d", res);
                goto lb_exit;
            }
            return; // In this case the condition broadcast should happens in s_check_db_version_callback_set()
        }
    } else if(a_global_db_context->instance->version > DAP_GLOBAL_DB_VERSION) {
        log_it(L_ERROR, "GlobalDB version %d is newer than supported version %d",
                            a_global_db_context->instance->version, DAP_GLOBAL_DB_VERSION);
        res = -1;
    }
    else {
        log_it(L_NOTICE, "GlobalDB version %d", a_global_db_context->instance->version);
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
static void s_check_db_version_callback_set (dap_global_db_context_t * a_global_db_context,int a_errno, const char * a_group, const char * a_key,
                                             const void * a_value, const size_t a_value_len,
                                             dap_nanotime_t value_ts, bool a_is_pinned, void * a_arg)
{
    int l_res = 0;
    if(a_errno != 0){
        log_it(L_ERROR, "Can't process request for DB version, error code %d", a_errno);
        l_res = a_errno;
    } else
        log_it(L_NOTICE, "GlobalDB version updated to %d", a_global_db_context->instance->version);

    pthread_mutex_lock(&s_check_db_mutex); //  in calling thread
    s_check_db_ret = l_res;
    pthread_cond_signal(&s_check_db_cond);
    pthread_mutex_unlock(&s_check_db_mutex); //  in calling thread
}

bool dap_global_db_isalnum_group_key(dap_store_obj_t* a_obj) {
    if (!a_obj)
        return true;
    bool ret = true;
    for (char *c = (char*)a_obj->key; *c; ++c) {
        if (!dap_ascii_isprint(*c)) {
            ret = false;
            break;
        }
    }

    for (char *c = (char*)a_obj->group; *c; ++c) {
        if (!dap_ascii_isprint(*c)) {
            ret = false;
            break;
        }
    }

    if (!ret) {
        char l_ts[128] = { '\0' };
        dap_gbd_time_to_str_rfc822(l_ts, sizeof(l_ts), a_obj->timestamp);
        log_it(L_MSG, "[!] Corrupted object %s (decalred len %zu / actual len %zu) : %s (decalred len %zu / actual len %zu), ts %s",
               a_obj->group, a_obj->group_len, dap_strlen(a_obj->group),
               a_obj->key, a_obj->key_len, dap_strlen(a_obj->key), l_ts);
    }
    return ret;
}
