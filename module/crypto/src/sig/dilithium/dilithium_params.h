#ifndef __DILITHIUM_PARAMS__
#define __DILITHIUM_PARAMS__

#include <assert.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include "dap_crypto_common.h"

#define SEEDBYTES       32U
#define CRHBYTES_LEGACY 48U
#define CRHBYTES_FIPS   64U
#define CRHBYTES        48U

#define NN              256U

#define Q               8380417U
#define QBITS           23U
#define ROOT_OF_UNITY   1753U

#define D_LEGACY        14U
#define GAMMA1_LEGACY   ((Q - 1U)/16U)
#define GAMMA2_LEGACY   (GAMMA1_LEGACY/2U)
#define ALPHA_LEGACY    (2U*GAMMA2_LEGACY)

/* Keep old defines for backward compat with legacy Dilithium code */
#define D               14U
#define GAMMA1          ((Q - 1U)/16U)
#define GAMMA2          (GAMMA1/2U)
#define ALPHA           (2U*GAMMA2)

///========================================================================
typedef enum {
    /* Legacy CRYSTALS-Dilithium round 3 */
    MODE_0, MODE_1, MODE_2, MODE_3,
    /* FIPS 204 ML-DSA */
    MLDSA_44, MLDSA_65, MLDSA_87
} __attribute__((aligned(4))) dilithium_kind_t;

typedef struct {
  dilithium_kind_t kind;
  uint32_t PARAM_K;
  uint32_t PARAM_L;
  uint32_t PARAM_ETA;
  uint32_t PARAM_SETABITS;
  uint32_t PARAM_BETA;
  uint32_t PARAM_OMEGA;

  uint32_t PARAM_POL_SIZE_PACKED;
  uint32_t PARAM_POLT1_SIZE_PACKED;
  uint32_t PARAM_POLT0_SIZE_PACKED;
  uint32_t PARAM_POLETA_SIZE_PACKED;
  uint32_t PARAM_POLZ_SIZE_PACKED;
  uint32_t PARAM_POLW1_SIZE_PACKED;
  uint32_t PARAM_POLVECK_SIZE_PACKED;
  uint32_t PARAM_POLVECL_SIZE_PACKED;

  uint32_t CRYPTO_PUBLICKEYBYTES;
  uint32_t CRYPTO_SECRETKEYBYTES;
  uint32_t CRYPTO_BYTES;

  /* FIPS 204 extended parameters (0 for legacy modes) */
  uint32_t PARAM_D;
  uint32_t PARAM_TAU;
  uint32_t PARAM_GAMMA1;
  uint32_t PARAM_GAMMA2;
  uint32_t PARAM_LAMBDA_BYTES;  /* ctilde size = lambda/4 */
  uint32_t PARAM_CRHBYTES;     /* 48 legacy, 64 FIPS 204 */
  bool     is_fips204;
} dilithium_param_t;

///==========================================================================================
typedef struct {
  dilithium_kind_t kind;                 /* the kind of dilithium       */
  unsigned char *data;
} dilithium_private_key_t;

typedef struct {
  dilithium_kind_t kind;                 /* the kind of dilithium       */
  unsigned char *data;
} dilithium_public_key_t;

typedef struct {
  dilithium_kind_t kind;                      /* the kind of dilithium       */
  unsigned char *sig_data;
  uint64_t sig_len;
} dilithium_signature_t;


/* Per-level parameter accessors — works for both legacy and FIPS 204 modes */
static inline uint32_t dil_gamma1(const dilithium_param_t *p)  { return p->PARAM_GAMMA1; }
static inline uint32_t dil_gamma2(const dilithium_param_t *p)  { return p->PARAM_GAMMA2; }
static inline uint32_t dil_alpha(const dilithium_param_t *p)   { return 2U * p->PARAM_GAMMA2; }
static inline uint32_t dil_d(const dilithium_param_t *p)       { return p->PARAM_D; }
static inline uint32_t dil_tau(const dilithium_param_t *p)     { return p->PARAM_TAU; }
static inline uint32_t dil_crhbytes(const dilithium_param_t *p){ return p->PARAM_CRHBYTES; }
static inline uint32_t dil_ctildebytes(const dilithium_param_t *p) {
    return p->PARAM_LAMBDA_BYTES;
}
/* Log2 of gamma1 — needed for packing bit width */
static inline unsigned dil_gamma1_bits(const dilithium_param_t *p) {
    return p->PARAM_GAMMA1 == (1U << 17) ? 17 : 19;
}

///==========================================================================================
bool dilithium_params_init(dilithium_param_t *, dilithium_kind_t );

int dilithium_crypto_sign_keypair(dilithium_public_key_t *public_key, dilithium_private_key_t *private_key,
        dilithium_kind_t kind, const void * seed, size_t seed_size);

int dilithium_crypto_sign(dilithium_signature_t *, const unsigned char *, unsigned long long, const dilithium_private_key_t *);

int dilithium_crypto_sign_open( unsigned char *, unsigned long long, dilithium_signature_t *, const dilithium_public_key_t *);

int dilithium_crypto_sign_open_batch(
    unsigned char **a_msgs,
    unsigned long long *a_msg_lens,
    dilithium_signature_t **a_sigs,
    const dilithium_public_key_t **a_pub_keys,
    unsigned int a_count,
    int *a_results);

void dilithium_private_key_delete(void *private_key);
void dilithium_public_key_delete(void *public_key);
void dilithium_private_and_public_keys_delete(void *private_key, void *public_key);

void dilithium_signature_delete(void *sig);

///==========================================================================================

#endif


