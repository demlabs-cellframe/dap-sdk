#ifndef __SPHINCSPLUS_PARAMS__
#define __SPHINCSPLUS_PARAMS__

typedef struct {
  uint32_t sSPX_D;
  uint32_t sSPX_N;
  uint32_t sSPX_FORS_MSG_BYTES;
  uint32_t sSPX_FORS_BYTES;
  uint32_t sSPX_WOTS_BYTES;
  uint32_t sSPX_WOTS_LEN;
  uint32_t sSPX_TREE_HEIGHT;
  uint32_t sSPX_BYTES;
} sphincsplus_param_t;

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


