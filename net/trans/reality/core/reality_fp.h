/**
 * @file reality_fp.h
 * @brief ClientHello template types (internal).
 */

#ifndef REALITY_FP_H
#define REALITY_FP_H

#include <stdint.h>
#include <stddef.h>

typedef struct reality_fp_template {
    const char    *name;
    const uint8_t *clienthello;
    size_t         clienthello_size;
    size_t random_off;
    size_t session_id_len_off;
    size_t session_id_off;
    size_t sni_name_off;
    size_t sni_name_len;
    size_t ks_hybrid_off;
    size_t ks_hybrid_len;
    size_t ks_x25519_off;
    size_t ks_x25519_len;
} reality_fp_template_t;

#endif /* REALITY_FP_H */
