/**
 * @file dap_tls_fingerprint.c
 * @brief TLS fingerprint profile registry (TL.2 / Milestone B)
 *
 * Profile registry manages wire-captured ClientHello templates.
 * Each profile has a fixed template with a known SNI patch point.
 */

#include <string.h>
#include "dap_common.h"
#include "dap_tls_fingerprint.h"

#define LOG_TAG "dap_tls_fingerprint"

/* External profile constructors */
extern const dap_tls_fp_profile_t *dap_tls_fp_chrome_120(void);
extern const dap_tls_fp_profile_t *dap_tls_fp_firefox_121(void);
extern const dap_tls_fp_profile_t *dap_tls_fp_edge_120(void);

static const dap_tls_fp_profile_t *s_profiles[] = {
    NULL, /* filled at init */
    NULL,
    NULL,
};
static size_t s_profile_count = 0;

__attribute__((constructor))
static void s_registry_init(void)
{
    s_profiles[0] = dap_tls_fp_chrome_120();
    s_profiles[1] = dap_tls_fp_firefox_121();
    s_profiles[2] = dap_tls_fp_edge_120();
    s_profile_count = 3;
    log_it(L_NOTICE, "TLS fingerprint registry: %zu profiles loaded", s_profile_count);
}

const dap_tls_fp_profile_t *dap_tls_fp_get(const char *a_name)
{
    if (!a_name)
        return NULL;
    for (size_t i = 0; i < s_profile_count; i++) {
        if (s_profiles[i] && strcmp(s_profiles[i]->name, a_name) == 0)
            return s_profiles[i];
    }
    return NULL;
}

const dap_tls_fp_profile_t *dap_tls_fp_get_by_index(size_t a_index)
{
    if (a_index >= s_profile_count)
        return NULL;
    return s_profiles[a_index];
}

size_t dap_tls_fp_count(void)
{
    return s_profile_count;
}

size_t dap_tls_fp_list(const char **a_out, size_t a_max)
{
    size_t l_n = s_profile_count < a_max ? s_profile_count : a_max;
    for (size_t i = 0; i < l_n; i++)
        a_out[i] = s_profiles[i]->name;
    return l_n;
}

int dap_tls_fp_build_clienthello(const dap_tls_fp_profile_t *a_profile,
                                 const char *a_sni,
                                 void **a_out, size_t *a_out_size)
{
    if (!a_profile || !a_out || !a_out_size)
        return -1;

    size_t l_sni_len = a_sni ? strlen(a_sni) : 0;
    if (l_sni_len > DAP_TLS_FP_SNI_MAX_LEN)
        return -2;

    size_t l_base_size = a_profile->clienthello_size;
    /* Calculate total output size: base template + SNI hostname (if not already in template) */
    size_t l_total = l_base_size + l_sni_len;

    uint8_t *l_buf = DAP_NEW_SIZE(uint8_t, l_total);
    if (!l_buf)
        return -3;

    memcpy(l_buf, a_profile->clienthello, l_base_size);

    /* Patch SNI hostname length (2 bytes BE) at sni_length_offset */
    if (a_profile->sni_length_offset + 2 <= l_base_size) {
        l_buf[a_profile->sni_length_offset]     = (uint8_t)(l_sni_len >> 8);
        l_buf[a_profile->sni_length_offset + 1] = (uint8_t)(l_sni_len & 0xFF);
    }

    /* Patch SNI hostname at sni_offset */
    if (a_profile->sni_offset + l_sni_len <= l_total) {
        if (l_sni_len > 0)
            memcpy(l_buf + a_profile->sni_offset, a_sni, l_sni_len);
    }

    *a_out = l_buf;
    *a_out_size = l_total;
    return 0;
}
