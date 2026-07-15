/**
 * @file dap_tls_fingerprint.h
 * @brief TLS fingerprint profile registry (TL.2 / Milestone B)
 *
 * Each profile is a wire-captured ClientHello template with an SNI override hook.
 * The mimicry engine selects a profile and patches the SNI hostname at runtime.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DAP_TLS_FP_SNI_MAX_LEN  255u
#define DAP_TLS_FP_MAX_PROFILES 16u

/**
 * @brief TLS fingerprint profile — a captured ClientHello template
 *
 * @field name          Human-readable name (e.g. "chrome_120")
 * @field ja3_string    Expected JA3 string for this profile
 * @field ja3_hash      MD5 hash of ja3_string (32 hex chars + NUL)
 * @field clienthello   Raw ClientHello body (after TLS record header)
 * @field clienthello_size  Size of clienthello in bytes
 * @field sni_offset    Byte offset within clienthello where SNI hostname starts
 * @field sni_length_offset  Byte offset where SNI hostname length (2 bytes BE) sits
 */
typedef struct dap_tls_fp_profile {
    const char     *name;
    const char     *ja3_string;
    const char     *ja3_hash;          /* 32 hex chars + NUL */
    const uint8_t  *clienthello;
    size_t          clienthello_size;
    size_t          sni_hostname_length_offset; /* offset of 2-byte BE hostname_length in SNI ext */
    size_t          sni_hostname_offset;        /* offset of hostname bytes in SNI ext */
    size_t          sni_data_length_offset;     /* offset of 2-byte BE data_length in SNI ext */
    size_t          extensions_length_offset;   /* offset of 2-byte BE extensions_length */
} dap_tls_fp_profile_t;

/**
 * @brief Get a profile by name
 * @param a_name  Profile name (e.g. "chrome_120")
 * @return Profile pointer or NULL if not found
 */
const dap_tls_fp_profile_t *dap_tls_fp_get(const char *a_name);

/**
 * @brief Get a profile by index
 * @param a_index  0-based index
 * @return Profile pointer or NULL if out of range
 */
const dap_tls_fp_profile_t *dap_tls_fp_get_by_index(size_t a_index);

/**
 * @brief Get total number of registered profiles
 */
size_t dap_tls_fp_count(void);

/**
 * @brief Get all profile names (for listing)
 * @param a_out     Output array of name pointers ( caller provides array )
 * @param a_max     Max entries in a_out
 * @return Number of profiles written to a_out
 */
size_t dap_tls_fp_list(const char **a_out, size_t a_max);

/**
 * @brief Patch SNI hostname in a ClientHello template
 * @param a_profile   Source profile
 * @param a_sni       New SNI hostname (NULL or empty = no SNI)
 * @param a_out       Output buffer (caller freed with DAP_DELETE)
 * @param a_out_size  Output size
 * @return 0 on success, -1 on error
 */
int dap_tls_fp_build_clienthello(const dap_tls_fp_profile_t *a_profile,
                                 const char *a_sni,
                                 void **a_out, size_t *a_out_size);

#ifdef __cplusplus
}
#endif
