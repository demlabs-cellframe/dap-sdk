#include "dap_http_ban_list_client.h"
#include "dap_strfuncs.h"
#include "dap_hash.h"

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

static void s_dap_http_ban_list_client_dump_single(ban_record_t *a_rec, dap_string_t *a_str) {
    char *l_decree_hash_str = dap_hash_fast_to_str_static(&a_rec->decree_hash),
        l_ts[80] = { '\0' };
    dap_time_to_str_rfc822(l_ts, sizeof(l_ts), a_rec->ts_created);
    dap_string_append_printf(a_str, "%s\n\t\t\tAddress: %s\n\t\t\tCreated at %s\n\n",
        l_decree_hash_str, a_rec->addr, l_ts);
}

char *dap_http_ban_list_client_dump(const char *a_addr) {
    int num = 1;
    ban_record_t *l_rec = NULL, *l_tmp = NULL;
    dap_string_t *l_res = dap_string_new(NULL);
    pthread_rwlock_rdlock(&s_ban_list_lock);
    if (a_addr) {
        HASH_FIND_STR(s_ban_list, a_addr, l_rec);
        if (l_rec)
            s_dap_http_ban_list_client_dump_single(l_rec, l_res);
        else
            dap_string_append_printf(l_res, "Address %s is not banlisted", a_addr);
    } else {
        HASH_ITER(hh, s_ban_list, l_rec, l_tmp) {
            dap_string_append_printf(l_res, "\t\t%d. ", num++);
            s_dap_http_ban_list_client_dump_single(l_rec, l_res);
        }
    }
    pthread_rwlock_unlock(&s_ban_list_lock);
    return dap_string_free(l_res, false);
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
