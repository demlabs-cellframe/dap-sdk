#ifndef __SPHINCSPLUS_PARAMS__
#define __SPHINCSPLUS_PARAMS__

#include <stdint.h>

typedef enum {
    SPHINCSPLUS_CONFIG_MIN_ARG,
    SPHINCSPLUS_HARAKA_128F,
    SPHINCSPLUS_HARAKA_128S,
    SPHINCSPLUS_HARAKA_192F,
    SPHINCSPLUS_HARAKA_192S,
    SPHINCSPLUS_HARAKA_256F,
    SPHINCSPLUS_HARAKA_256S,
    SPHINCSPLUS_SHA2_128F,
    SPHINCSPLUS_SHA2_128S,
    SPHINCSPLUS_SHA2_192F,
    SPHINCSPLUS_SHA2_192S,
    SPHINCSPLUS_SHA2_256F,
    SPHINCSPLUS_SHA2_256S,
    SPHINCSPLUS_SHAKE_128F,
    SPHINCSPLUS_SHAKE_128S,
    SPHINCSPLUS_SHAKE_192F,
    SPHINCSPLUS_SHAKE_192S,
    SPHINCSPLUS_SHAKE_256F,
    SPHINCSPLUS_SHAKE_256S,
    SPHINCSPLUS_CONFIG_MAX_ARG,
} sphincsplus_config_t;

typedef struct {
uint32_t spx_offset_layer;
uint32_t spx_offset_tree;
uint32_t spx_offset_type;
uint32_t spx_offset_kp_addr2;
uint32_t spx_offset_kp_addr1;
uint32_t spx_offset_chain_addr;
uint32_t spx_offset_hash_addr;
uint32_t spx_offset_tree_hgt;
uint32_t spx_offset_tree_index;
} sphincsplus_offsets_t;

typedef struct {
sphincsplus_config_t config;
uint32_t spx_n;
uint32_t spx_full_height;
uint32_t spx_d;
uint32_t spx_fors_height;
uint32_t spx_fors_trees;
uint32_t spx_wots_w;
uint32_t spx_addr_bytes;
uint32_t spx_wots_logw;
uint8_t spx_sha512;
uint32_t spx_wots_len1;
uint32_t spx_wots_len2;
uint32_t spx_wots_len;
uint32_t spx_wots_bytes;
uint32_t spx_wots_pk_bytes;
uint32_t spx_tree_height;
uint32_t spx_fors_msg_bytes;
uint32_t spx_fors_bytes;
uint32_t spx_fors_pk_bytes;
uint32_t spx_bytes;
uint32_t spx_pk_bytes;
uint32_t spx_sk_bytes;
sphincsplus_offsets_t offsets;
} sphincsplus_param_t;

sphincsplus_param_t g_sphincsplus_params_current;

#define SPHINCSPLUS_FLEX

#ifdef SPHINCSPLUS_FLEX

#define SPX_NAMESPACE(s)        SPX_##s
#define SPX_N                   g_sphincsplus_params_current.spx_n
#define SPX_FULL_HEIGHT         g_sphincsplus_params_current.spx_full_height
#define SPX_D                   g_sphincsplus_params_current.spx_d
#define SPX_FORS_HEIGHT         g_sphincsplus_params_current.spx_fors_height
#define SPX_FORS_TREES          g_sphincsplus_params_current.spx_fors_trees
#define SPX_WOTS_W              g_sphincsplus_params_current.spx_wots_w
#define SPX_ADDR_BYTES          g_sphincsplus_params_current.spx_addr_bytes
#define SPX_WOTS_LOGW           g_sphincsplus_params_current.spx_wots_logw
#define SPX_SHA512              g_sphincsplus_params_current.spx_sha512 
#define SPX_WOTS_LEN1           g_sphincsplus_params_current.spx_wots_len1
#define SPX_WOTS_LEN2           g_sphincsplus_params_current.spx_wots_len2
#define SPX_WOTS_LEN            g_sphincsplus_params_current.spx_wots_len
#define SPX_WOTS_BYTES          g_sphincsplus_params_current.spx_wots_bytes
#define SPX_WOTS_PK_BYTES       g_sphincsplus_params_current.spx_wots_pk_bytes
#define SPX_TREE_HEIGHT         g_sphincsplus_params_current.spx_tree_height
#define SPX_FORS_MSG_BYTES      g_sphincsplus_params_current.spx_fors_msg_bytes
#define SPX_FORS_BYTES          g_sphincsplus_params_current.spx_fors_bytes
#define SPX_FORS_PK_BYTES       g_sphincsplus_params_current.spx_fors_pk_bytes
#define SPX_BYTES               g_sphincsplus_params_current.spx_bytes
#define SPX_PK_BYTES            g_sphincsplus_params_current.spx_pk_bytes
#define SPX_SK_BYTES            g_sphincsplus_params_current.spx_sk_bytes

#define SPX_OFFSET_LAYER        g_sphincsplus_params_current.offsets.spx_offset_layer
#define SPX_OFFSET_TREE         g_sphincsplus_params_current.offsets.spx_offset_tree
#define SPX_OFFSET_TYPE         g_sphincsplus_params_current.offsets.spx_offset_type
#define SPX_OFFSET_KP_ADDR2     g_sphincsplus_params_current.offsets.spx_offset_kp_addr2
#define SPX_OFFSET_KP_ADDR1     g_sphincsplus_params_current.offsets.spx_offset_kp_addr1
#define SPX_OFFSET_CHAIN_ADDR   g_sphincsplus_params_current.offsets.spx_offset_chain_addr
#define SPX_OFFSET_HASH_ADDR    g_sphincsplus_params_current.offsets.spx_offset_hash_addr
#define SPX_OFFSET_TREE_HGT     g_sphincsplus_params_current.offsets.spx_offset_tree_hgt
#define SPX_OFFSET_TREE_INDEX   g_sphincsplus_params_current.offsets.spx_offset_tree_index
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

int sphincsplus_set_config(sphincsplus_config_t a_config);
int sphincsplus_set_params(const sphincsplus_param_t *a_params);
sphincsplus_param_t sphincsplus_get_params(sphincsplus_config_t a_config);

#endif  // __SPHINCSPLUS_PARAMS__


