#include "dap_enc_http_ban_list_client.h"

//IP V4
static dap_enc_http_ban_list_client_record_t *s_ipv4_ban_list = NULL;
static pthread_rwlock_t s_ipv4_ban_list_rwlock;

bool dap_enc_http_ban_list_client_check_ipv4(struct in_addr a_ip) {
    pthread_rwlock_rdlock(&s_ipv4_ban_list_rwlock);
    dap_enc_http_ban_list_client_record_t *l_record = NULL;
    HASH_FIND(hh, s_ipv4_ban_list, &a_ip, sizeof (struct in_addr), l_record);
    pthread_rwlock_unlock(&s_ipv4_ban_list_rwlock);
    return l_record ? true : false;
}

void dap_enc_http_ban_list_client_add_ipv4(struct in_addr a_ip, dap_hash_fast_t a_decree_hash, dap_time_t a_ts_created) {
    dap_enc_http_ban_list_client_record_t *l_record = DAP_NEW(dap_enc_http_ban_list_client_record_t);
    l_record->ip_v4 = a_ip;
    l_record->decree_hash = a_decree_hash;
    l_record->ts_created = a_ts_created;
    pthread_rwlock_wrlock(&s_ipv4_ban_list_rwlock);
    HASH_ADD(hh, s_ipv4_ban_list, ip_v4, sizeof(struct in_addr), l_record);
    pthread_rwlock_unlock(&s_ipv4_ban_list_rwlock);
}

void dap_enc_http_ban_list_client_remove_ipv4(struct in_addr a_ip){
    pthread_rwlock_wrlock(&s_ipv4_ban_list_rwlock);
    dap_enc_http_ban_list_client_record_t *l_record = NULL, *tmp = NULL;
    HASH_FIND(hh, s_ipv4_ban_list, &a_ip, sizeof (struct in_addr), l_record);
    if (l_record) {
        HASH_DEL(s_ipv4_ban_list, l_record);
        DAP_DELETE(l_record);
    }
    pthread_rwlock_unlock(&s_ipv4_ban_list_rwlock);
}

void dap_enc_http_ban_list_client_ipv4_print(dap_string_t *a_str_out){
    a_str_out = dap_string_append(a_str_out, "\t IP v4.\n\n");
    pthread_rwlock_rdlock(&s_ipv4_ban_list_rwlock);
    if (!s_ipv4_ban_list) {
        a_str_out = dap_string_append(a_str_out, "\t\t Not found.\n");
        return;
    }
    int number = 1;
    dap_enc_http_ban_list_client_record_t *l_record = NULL, *l_tmp = NULL;
    HASH_ITER(hh, s_ipv4_ban_list, l_record, l_tmp) {
        char *l_decree_hash_str = dap_chain_hash_fast_to_str_new(&l_record->decree_hash);
        char l_tm[85];
        char l_tm_ip[INET_ADDRSTRLEN];
        dap_time_to_str_rfc822(l_tm, 85, l_record->ts_created);
        dap_string_append_printf(a_str_out, "\t\t%d) %s\n"
                                            "\t\t\tIP: %s\n"
                                            "\t\t\tCreated: %s\n\n", number, l_decree_hash_str, inet_ntop(AF_INET, &l_record->ip_v4,
                                                                                                      l_tm_ip, INET_ADDRSTRLEN), l_tm);
    }
    pthread_rwlock_unlock(&s_ipv4_ban_list_rwlock);
}

//IP V6

static dap_enc_http_ban_list_client_record_t *s_ipv6_ban_list = NULL;
static pthread_rwlock_t s_ipv6_ban_list_rwlock;

bool dap_enc_http_ban_list_client_check_ipv6(struct in6_addr a_ip_v6) {
    pthread_rwlock_rdlock(&s_ipv6_ban_list_rwlock);
    dap_enc_http_ban_list_client_record_t *l_record = NULL;
    HASH_FIND(hh, s_ipv6_ban_list, &a_ip_v6, sizeof(struct in6_addr), l_record);
    pthread_rwlock_unlock(&s_ipv6_ban_list_rwlock);
    return l_record ? true : false;
}

void dap_enc_http_ban_list_client_add_ipv6(struct in6_addr a_ip_v6, dap_hash_fast_t a_decree_hash, dap_time_t a_ts_created) {
    dap_enc_http_ban_list_client_record_t *l_record = DAP_NEW(dap_enc_http_ban_list_client_record_t);
    l_record->ip_v6 = a_ip_v6;
    l_record->decree_hash = a_decree_hash;
    l_record->ts_created = a_ts_created;
    pthread_rwlock_wrlock(&s_ipv6_ban_list_rwlock);
    HASH_ADD(hh, s_ipv6_ban_list, ip_v6, sizeof(struct in6_addr), l_record);
    pthread_rwlock_unlock(&s_ipv6_ban_list_rwlock);
}
void dap_enc_http_ban_list_client_remove_ipv6(struct in6_addr a_ip_v6) {
    pthread_rwlock_wrlock(&s_ipv6_ban_list_rwlock);
    dap_enc_http_ban_list_client_record_t *l_record = NULL;
    HASH_FIND(hh, s_ipv6_ban_list, &a_ip_v6, sizeof(struct in6_addr), l_record);
    if (l_record) {
        HASH_DEL(s_ipv6_ban_list, l_record);
        DAP_DELETE(l_record);
    }
    pthread_rwlock_unlock(&s_ipv6_ban_list_rwlock);
}

void dap_enc_http_ban_list_client_ipv6_print(dap_string_t *a_str_out) {
    a_str_out = dap_string_append(a_str_out, "\t IP v6.\n");
    pthread_rwlock_rdlock(&s_ipv6_ban_list_rwlock);
    if (!s_ipv6_ban_list) {
        a_str_out = dap_string_append(a_str_out, "\t\t Not found.\n\n");
        return;
    }
    int number = 1;
    dap_enc_http_ban_list_client_record_t *l_record = NULL, *tmp = NULL;
    HASH_ITER(hh, s_ipv6_ban_list, l_record, tmp) {
        char *l_decree_hash_str = dap_chain_hash_fast_to_str_new(&l_record->decree_hash);
        char l_tm[85];
        char l_tm_ip[INET6_ADDRSTRLEN];
        dap_time_to_str_rfc822(l_tm, 85, l_record->ts_created);
        dap_string_append_printf(a_str_out, "\t\t%d) %s\n"
                                            "\t\t\tIP: %s\n"
                                            "\t\t\tCreated: %s\n\n", number, l_decree_hash_str, inet_ntop(AF_INET6, &l_record->ip_v6,
                                                                                                      l_tm_ip, INET6_ADDRSTRLEN), l_tm);
    }
    pthread_rwlock_unlock(&s_ipv6_ban_list_rwlock);
}

int dap_enc_http_ban_list_client_init() {
    s_ipv4_ban_list = NULL;
    s_ipv6_ban_list = NULL;
    pthread_rwlock_init(&s_ipv4_ban_list_rwlock, NULL);
    pthread_rwlock_init(&s_ipv6_ban_list_rwlock, NULL);
    return 0;
}
void dap_enc_http_ban_list_client_deinit() {
    pthread_rwlock_destroy(&s_ipv4_ban_list_rwlock);
    pthread_rwlock_destroy(&s_ipv6_ban_list_rwlock);
}
