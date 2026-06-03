#include <assert.h>
#include <string.h>
#include "dilithium_params.h"


static const dilithium_param_t dilithium_params[] = {

  { MODE_0,
    3, 2, 7, 4, 375, 64,
    736, 288, 448, 128, 640, 128, 2208, 1472,
    896, 2096, 1387,
    D, 60, GAMMA1, GAMMA2, SEEDBYTES, CRHBYTES, false
  },
  { MODE_1,
    4, 3, 6, 4, 325, 80,
    736, 288, 448, 128, 640, 128, 2944, 2208,
    1184, 2800, 2044,
    D, 60, GAMMA1, GAMMA2, SEEDBYTES, CRHBYTES, false
  },
  { MODE_2,
    5, 4, 5, 4, 275, 96,
    736, 288, 448, 128, 640, 128, 3680, 2944,
    1472, 3504, 2701,
    D, 60, GAMMA1, GAMMA2, SEEDBYTES, CRHBYTES, false
  },
  { MODE_3,
    6, 5, 3, 3, 175, 120,
    736, 288, 448, 96, 640, 128, 4416, 3680,
    1760, 3856, 3366,
    D, 60, GAMMA1, GAMMA2, SEEDBYTES, CRHBYTES, false
  },

  /*
   * FIPS 204 ML-DSA parameter sets.
   *
   * ML-DSA-44: k=4, l=4, d=13, tau=39, eta=2, gamma1=2^17, gamma2=(q-1)/88
   * ML-DSA-65: k=6, l=5, d=13, tau=49, eta=4, gamma1=2^19, gamma2=(q-1)/32
   * ML-DSA-87: k=8, l=7, d=13, tau=60, eta=2, gamma1=2^19, gamma2=(q-1)/32
   *
   * Packing sizes derived from FIPS 204 Section 6:
   *   polz: 32*(1+bitlen(gamma1-1)) per poly
   *   polw1: depends on gamma2
   *   polt1: 32*(bitlen(q-1)-d) = 32*10 = 320 per poly
   *   polt0: 32*d per poly
   *   poleta: 32*bitlen(2*eta) per poly
   */
  { MLDSA_44,
    4,                  /* K */
    4,                  /* L */
    2,                  /* eta */
    3,                  /* setabits = bitlen(2*eta) = bitlen(4) = 3 */
    78,                 /* beta = tau * eta = 39 * 2 */
    80,                 /* omega */
    /* packing sizes (per polynomial, 256 coeffs) */
    736,                /* POL_SIZE_PACKED (full coefficient, same) */
    320,                /* POLT1_SIZE_PACKED: 32*10 = 320 (d=13, bitlen(q-1)=23, 23-13=10) */
    416,                /* POLT0_SIZE_PACKED: 32*13 = 416 */
    96,                 /* POLETA_SIZE_PACKED: 32*3 = 96 (bitlen(2*2)=bitlen(4)=3) */
    576,                /* POLZ_SIZE_PACKED: 32*(1+bitlen(2^17-1)) = 32*(1+17) = 32*18 = 576 */
    192,                /* POLW1_SIZE_PACKED: for gamma2=(q-1)/88, highbits in 6 bits -> 256*6/8 = 192 */
    4 * 320,            /* POLVECK_SIZE_PACKED: k * polt1 = 4*320 = 1280 */
    4 * 576,            /* POLVECL_SIZE_PACKED: l * polz = 4*576 = 2304 */
    /* crypto sizes */
    32 + 4*320,         /* CRYPTO_PUBLICKEYBYTES: seedbytes + k*polt1 = 32+1280 = 1312 */
    32+32+64 + (4+4)*96 + 4*416, /* CRYPTO_SECRETKEYBYTES: 128 + 8*96 + 4*416 = 128+768+1664 = 2560 */
    32 + 4*576 + 80+4,  /* CRYPTO_BYTES: lambda/4 + l*polz + omega+k = 32+2304+84 = 2420 */
    13,                 /* PARAM_D */
    39,                 /* PARAM_TAU */
    (1U << 17),         /* PARAM_GAMMA1 = 2^17 */
    (Q - 1U) / 88,     /* PARAM_GAMMA2 = 95232 */
    32,                 /* PARAM_LAMBDA_BYTES = 128/4 = 32 */
    64,                 /* PARAM_CRHBYTES */
    true
  },
  { MLDSA_65,
    6,                  /* K */
    5,                  /* L */
    4,                  /* eta */
    4,                  /* setabits = bitlen(2*4) = bitlen(8) = 4 */
    196,                /* beta = tau * eta = 49 * 4 */
    55,                 /* omega */
    736,                /* POL_SIZE_PACKED */
    320,                /* POLT1_SIZE_PACKED: 32*10 */
    416,                /* POLT0_SIZE_PACKED: 32*13 */
    128,                /* POLETA_SIZE_PACKED: 32*4 */
    640,                /* POLZ_SIZE_PACKED: 32*(1+19) = 32*20 = 640 */
    128,                /* POLW1_SIZE_PACKED: for gamma2=(q-1)/32, 4 bits -> 32*4 = 128 */
    6 * 320,            /* POLVECK_SIZE_PACKED */
    5 * 640,            /* POLVECL_SIZE_PACKED */
    32 + 6*320,         /* CRYPTO_PUBLICKEYBYTES = 32+1920 = 1952 */
    32+32+64 + (5+6)*128 + 6*416, /* CRYPTO_SECRETKEYBYTES: 128 + 11*128 + 6*416 = 128+1408+2496 = 4032 */
    48 + 5*640 + 55+6,  /* CRYPTO_BYTES: 192/4 + l*polz + omega+k = 48+3200+61 = 3309 */
    13, 49,
    (1U << 19),         /* PARAM_GAMMA1 = 2^19 */
    (Q - 1U) / 32,     /* PARAM_GAMMA2 = 261888 */
    48,                 /* PARAM_LAMBDA_BYTES = 192/4 = 48 */
    64, true
  },
  { MLDSA_87,
    8,                  /* K */
    7,                  /* L */
    2,                  /* eta */
    3,                  /* setabits */
    120,                /* beta = tau * eta = 60 * 2 */
    75,                 /* omega */
    736,                /* POL_SIZE_PACKED */
    320,                /* POLT1_SIZE_PACKED */
    416,                /* POLT0_SIZE_PACKED */
    96,                 /* POLETA_SIZE_PACKED */
    640,                /* POLZ_SIZE_PACKED: 32*(1+19) = 640 */
    128,                /* POLW1_SIZE_PACKED */
    8 * 320,            /* POLVECK_SIZE_PACKED */
    7 * 640,            /* POLVECL_SIZE_PACKED */
    32 + 8*320,         /* CRYPTO_PUBLICKEYBYTES = 32+2560 = 2592 */
    32+32+64 + (7+8)*96 + 8*416, /* CRYPTO_SECRETKEYBYTES: 128 + 15*96 + 8*416 = 128+1440+3328 = 4896 */
    64 + 7*640 + 75+8,  /* CRYPTO_BYTES: 256/4 + l*polz + omega+k = 64+4480+83 = 4627 */
    13, 60,
    (1U << 19),
    (Q - 1U) / 32,
    64,                 /* PARAM_LAMBDA_BYTES = 256/4 */
    64, true
  },
};

bool dilithium_params_init(dilithium_param_t *params, dilithium_kind_t kind){
  if(!params)
      return false;

  memset(params, 0, sizeof(dilithium_param_t));

  if (kind <= MLDSA_87) {
    *params = dilithium_params[kind];
    return true;
  }
  return false;
}
