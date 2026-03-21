/**
 * @file dap_mlkem_params.h
 * @brief ML-KEM (FIPS 203) compile-time parameters.
 *
 * MLKEM_K must be defined externally (2, 3, or 4) before including this file.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <stdint.h>

#ifndef MLKEM_K
#define MLKEM_K 3
#endif

/* Namespace macro to avoid symbol conflicts between K=2/3/4 libraries */
#if   MLKEM_K == 2
#define MLKEM_NAMESPACE(s) dap_mlkem512##s
#elif MLKEM_K == 3
#define MLKEM_NAMESPACE(s) dap_mlkem768##s
#elif MLKEM_K == 4
#define MLKEM_NAMESPACE(s) dap_mlkem1024##s
#else
#error "MLKEM_K must be 2, 3, or 4"
#endif

#define MLKEM_N          256
#define MLKEM_Q          3329
#define MLKEM_SYMBYTES   32
#define MLKEM_SSBYTES    32
#define MLKEM_POLYBYTES  384

#define MLKEM_MONT       2285      /* 2^16 mod Q */
#define MLKEM_QINV       62209     /* Q^{-1} mod 2^16 */

#define MLKEM_POLYVECBYTES (MLKEM_K * MLKEM_POLYBYTES)

#if MLKEM_K == 2
#define MLKEM_ETA1                    3
#define MLKEM_POLYCOMPRESSEDBYTES     128
#define MLKEM_POLYVECCOMPRESSEDBYTES  (MLKEM_K * 320)
#elif MLKEM_K == 3
#define MLKEM_ETA1                    2
#define MLKEM_POLYCOMPRESSEDBYTES     128
#define MLKEM_POLYVECCOMPRESSEDBYTES  (MLKEM_K * 320)
#elif MLKEM_K == 4
#define MLKEM_ETA1                    2
#define MLKEM_POLYCOMPRESSEDBYTES     160
#define MLKEM_POLYVECCOMPRESSEDBYTES  (MLKEM_K * 352)
#endif

#define MLKEM_ETA2 2

#define MLKEM_INDCPA_MSGBYTES       MLKEM_SYMBYTES
#define MLKEM_INDCPA_PUBLICKEYBYTES (MLKEM_POLYVECBYTES + MLKEM_SYMBYTES)
#define MLKEM_INDCPA_SECRETKEYBYTES MLKEM_POLYVECBYTES
#define MLKEM_INDCPA_BYTES          (MLKEM_POLYVECCOMPRESSEDBYTES + MLKEM_POLYCOMPRESSEDBYTES)

#define MLKEM_PUBLICKEYBYTES   MLKEM_INDCPA_PUBLICKEYBYTES
#define MLKEM_SECRETKEYBYTES   (MLKEM_INDCPA_SECRETKEYBYTES + MLKEM_INDCPA_PUBLICKEYBYTES + 2 * MLKEM_SYMBYTES)
#define MLKEM_CIPHERTEXTBYTES  MLKEM_INDCPA_BYTES

/* Multi-versioning: emit both AVX2 and scalar fallback, dispatch via IFUNC */
#if defined(__GNUC__) && defined(__x86_64__) && !defined(MLKEM_NO_CLONES)
#define MLKEM_HOTFN __attribute__((target_clones("avx2", "default")))
#else
#define MLKEM_HOTFN
#endif

typedef struct {
    int16_t coeffs[MLKEM_N];
} dap_mlkem_poly;

typedef struct {
    dap_mlkem_poly vec[MLKEM_K];
} dap_mlkem_polyvec;
