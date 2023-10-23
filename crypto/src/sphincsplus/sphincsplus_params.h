#ifndef __SPHINCSPLUS_PARAMS__
#define __SPHINCSPLUS_PARAMS__

#include <stdint.h>

typedef struct {
  uint32_t spx_n;
  uint32_t spx_full_height;
  uint32_t spx_d;
  uint32_t spx_fors_height;
  uint32_t spx_fors_trees;
  uint32_t spx_wots_w;
  uint32_t apx_addr_bytes;
  uint8_t  spx_sha512;
} sphincsplus_param_t;

typedef enum {
  SPHINCS_HARAKA_128F,
  SPHINCS_HARAKA_128S,
  SPHINCS_HARAKA_192F,
  SPHINCS_HARAKA_192S,
  SPHINCS_HARAKA_256F,
  SPHINCS_HARAKA_256S,
  SPHINCS_SHA2_128F,
  SPHINCS_SHA2_128S,
  SPHINCS_SHA2_192F,
  SPHINCS_SHA2_192S,
  SPHINCS_SHA2_256F,
  SPHINCS_SHA2_256S,
  SPHINCS_SHAKE_128F,
  SPHINCS_SHAKE_128S,
  SPHINCS_SHAKE_192F,
  SPHINCS_SHAKE_192S,
  SPHINCS_SHAKE_256F,
  SPHINCS_SHAKE_256S,
} sphincsplus_config_t;

static sphincsplus_param_t g_sphincsplus_params_current = {0};

#ifdef SPHINCSPLUS_FLEX
#define SPX_SHA512 0 /* Use SHA-256 for all hashes */
#define SPX_N g_sphincsplus_params_current.spx_n
#define SPX_FULL_HEIGHT g_sphincsplus_params_current.spx_full_height
#define SPX_D g_sphincsplus_params_current.spx_d
#define SPX_FORS_HEIGHT g_sphincsplus_params_current.spx_fors_height
#define sSPX_FORS_TREES g_sphincsplus_params_current.spx_fors_trees
#define SPX_WOTS_W g_sphincsplus_params_current.spx_wots_w
#define SPX_ADDR_BYTES g_sphincsplus_params_current.apx_addr_bytes
#endif


///==========================================================================================
typedef struct {
  uint8_t *data;
} sphincsplus_private_key_t;

typedef struct {
  uint8_t *data;
} sphincsplus_public_key_t;

typedef struct {
  sphincsplus_param_t sig_params;
  uint64_t sig_len;
  uint8_t *sig_data;
} sphincsplus_signature_t;

#endif  // __SPHINCSPLUS_PARAMS__


