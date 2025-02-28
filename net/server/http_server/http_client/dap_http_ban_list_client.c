#include "dap_http_ban_list_client.h"
#include "dap_hash.h"
#include "json_types.h"
#include "dap_json_rpc_errors.h"

typedef struct ban_record {
    dap_hash_fast_t decree_hash;
    dap_time_t ts_created;
    UT_hash_handle hh;
    char addr[];
} ban_record_t;

pthread_rwlock_t s_ban_list_lock = PTHREAD_RWLOCK_INITIALIZER;
ban_record_t *s_ban_list;

bool dap_http_ban_list_client_check(const char *a_addr, dap_hash_fast_t *a_decree_hash, dap_time_t *a_ts) {
    ban_record_t *l_rec = NULL;
    pthread_rwlock_rdlock(&s_ban_list_lock);
    HASH_FIND_STR(s_ban_list, a_addr, l_rec);
    pthread_rwlock_unlock(&s_ban_list_lock);
    if (l_rec) {
        if (a_decree_hash) *a_decree_hash = l_rec->decree_hash;
        if (a_ts) *a_ts = l_rec->ts_created;
        return true;
    }
    return false;
}

int dap_http_ban_list_client_add(const char *a_addr, dap_hash_fast_t a_decree_hash, dap_time_t a_ts) {
    if ( dap_http_ban_list_client_check(a_addr, NULL, NULL) )
        return -1;
    ban_record_t *l_rec = DAP_NEW_Z_SIZE( ban_record_t, sizeof(ban_record_t) + strlen(a_addr) + 1);
    *l_rec = (ban_record_t) {
        .decree_hash = a_decree_hash,
        .ts_created = a_ts,
    };
    strcpy(l_rec->addr, a_addr);
    pthread_rwlock_wrlock(&s_ban_list_lock);
    HASH_ADD_STR(s_ban_list, addr, l_rec);
    pthread_rwlock_unlock(&s_ban_list_lock);
    return 0;
}

int dap_http_ban_list_client_remove(const char *a_addr) {
    ban_record_t *l_rec = NULL;
    int l_ret = 0;
    pthread_rwlock_wrlock(&s_ban_list_lock);
    HASH_FIND_STR(s_ban_list, a_addr, l_rec);
    if (l_rec) {
        HASH_DEL(s_ban_list, l_rec);
        DAP_DELETE(l_rec);
    } else
        l_ret = -1;
    pthread_rwlock_unlock(&s_ban_list_lock);
    return l_ret;
}

static void s_dap_http_ban_list_client_dump_single(ban_record_t *a_rec, json_object *a_jobj_out) {
    const char *l_decree_hash_str = dap_hash_fast_to_str_static(&a_rec->decree_hash);
    char l_ts[DAP_TIME_STR_SIZE] = { '\0' };
    dap_time_to_str_rfc822(l_ts, sizeof(l_ts), a_rec->ts_created);
    json_object_object_add(a_jobj_out, "decree_hash", json_object_new_string(l_decree_hash_str));
    json_object_object_add(a_jobj_out, "address", json_object_new_string(a_rec->addr));
    json_object_object_add(a_jobj_out, "created_at", json_object_new_string(l_ts));
}

json_object *dap_http_ban_list_client_dump(const char *a_addr) {
    int num = 1;
    ban_record_t *l_rec = NULL, *l_tmp = NULL;
    json_object *l_jobj_out = json_object_new_object();
    json_object *l_jobj_array = NULL;
    if (!l_jobj_out) return dap_json_rpc_allocation_put(l_jobj_out);
    pthread_rwlock_rdlock(&s_ban_list_lock);
    if (a_addr) {
        HASH_FIND_STR(s_ban_list, a_addr, l_rec);
        if (l_rec)
            s_dap_http_ban_list_client_dump_single(l_rec, l_jobj_out);
        else
            json_object_object_add(l_jobj_out, a_addr, json_object_new_string("Address is not banlisted"));
    } else {
        l_jobj_array = json_object_new_array();
        if (!l_jobj_array) return dap_json_rpc_allocation_put(l_jobj_out);
        json_object_object_add(l_jobj_out, "banlist", l_jobj_array);        
        HASH_ITER(hh, s_ban_list, l_rec, l_tmp) {
            json_object *l_jobj_addr = json_object_new_object();
            if (!l_jobj_addr) return dap_json_rpc_allocation_put(l_jobj_out);
            json_object_object_add(l_jobj_addr, "num", json_object_new_int(num++));
            s_dap_http_ban_list_client_dump_single(l_rec, l_jobj_addr);
            json_object_array_add(l_jobj_array, l_jobj_addr);
        }
    }
    pthread_rwlock_unlock(&s_ban_list_lock);
    return l_jobj_out;
}

int dap_http_ban_list_client_init() {
    return 0;
}

void dap_http_ban_list_client_deinit() {
    ban_record_t *l_rec = NULL, *l_tmp = NULL;
    pthread_rwlock_wrlock(&s_ban_list_lock);
    HASH_ITER(hh, s_ban_list, l_rec, l_tmp) {
        HASH_DEL(s_ban_list, l_rec);
        DAP_DELETE(l_rec);
    }
    pthread_rwlock_unlock(&s_ban_list_lock);
    pthread_rwlock_destroy(&s_ban_list_lock);
}
