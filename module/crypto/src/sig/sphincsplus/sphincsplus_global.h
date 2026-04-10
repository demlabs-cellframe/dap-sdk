#ifndef __SPHINCSPLUS_GLOBAL__
#define __SPHINCSPLUS_GLOBAL__

#include "sphincsplus_params.h"


extern _Thread_local sphincsplus_params_t g_sphincsplus_params_current;

#ifdef SPHINCSPLUS_FLEX  //defined in cmake

// SPX... - from sphincs libruary definitions, SPHINCSPLUS... - added definitions

#define SPX_SHA256_BLOCK_BYTES 64
#define SPX_SHA256_OUTPUT_BYTES 32  /* This does not necessarily equal SPX_N */
#define SPX_SHA512_BLOCK_BYTES 128
#define SPX_SHA512_OUTPUT_BYTES 64
#define SPX_SHA256_ADDR_BYTES 22

#if (SPX_SHA256_BLOCK_BYTES & (SPX_SHA256_BLOCK_BYTES - 1)) != 0
    #error "Assumes that SPX_SHAX_BLOCK_BYTES is a power of 2"
#endif
#if (SPX_SHA512_BLOCK_BYTES & (SPX_SHA512_BLOCK_BYTES - 1)) != 0
    #error "Assumes that SPX_SHAX_BLOCK_BYTES is a power of 2"
#endif

#define SPX_NAMESPACE(s)        SPX_##s
#define SPX_N                   g_sphincsplus_params_current.base_params.spx_n
#define SPX_FULL_HEIGHT         g_sphincsplus_params_current.base_params.spx_full_height
#define SPX_D                   g_sphincsplus_params_current.base_params.spx_d
#define SPX_FORS_HEIGHT         g_sphincsplus_params_current.base_params.spx_fors_height
#define SPX_FORS_TREES          g_sphincsplus_params_current.base_params.spx_fors_trees
#define SPX_WOTS_W              g_sphincsplus_params_current.base_params.spx_wots_w
#define SPX_ADDR_BYTES          g_sphincsplus_params_current.base_params.spx_addr_bytes
#define SPX_WOTS_LOGW           g_sphincsplus_params_current.spx_wots_logw
#define SPX_SHA512              g_sphincsplus_params_current.base_params.spx_sha512 
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

#define SPX_TREE_BITS           g_sphincsplus_params_current.spx_tree_bits
#define SPX_TREE_BYTES          g_sphincsplus_params_current.spx_tree_bytes
#define SPX_LEAF_BITS           g_sphincsplus_params_current.spx_leaf_bits
#define SPX_LEAF_BYTES          g_sphincsplus_params_current.spx_leaf_bytes
#define SPX_DGST_BYTES          g_sphincsplus_params_current.spx_dgst_bytes

#define SPX_OFFSET_LAYER        g_sphincsplus_params_current.base_params.offsets.spx_offset_layer
#define SPX_OFFSET_TREE         g_sphincsplus_params_current.base_params.offsets.spx_offset_tree
#define SPX_OFFSET_TYPE         g_sphincsplus_params_current.base_params.offsets.spx_offset_type
#define SPX_OFFSET_KP_ADDR2     g_sphincsplus_params_current.base_params.offsets.spx_offset_kp_addr2
#define SPX_OFFSET_KP_ADDR1     g_sphincsplus_params_current.base_params.offsets.spx_offset_kp_addr1
#define SPX_OFFSET_CHAIN_ADDR   g_sphincsplus_params_current.base_params.offsets.spx_offset_chain_addr
#define SPX_OFFSET_HASH_ADDR    g_sphincsplus_params_current.base_params.offsets.spx_offset_hash_addr
#define SPX_OFFSET_TREE_HGT     g_sphincsplus_params_current.base_params.offsets.spx_offset_tree_hgt
#define SPX_OFFSET_TREE_INDEX   g_sphincsplus_params_current.base_params.offsets.spx_offset_tree_index

#define SPX_SHAX_OUTPUT_BYTES   g_sphincsplus_params_current.spx_shax_output_bytes
#define SPX_SHAX_BLOCK_BYTES    g_sphincsplus_params_current.spx_shax_block_bytes

#define SPHINCSPLUS_CONFIG      g_sphincsplus_params_current.base_params.config
#define SPHINCSPLUS_DIFFICULTY  g_sphincsplus_params_current.base_params.difficulty
#endif

#endif  // __SPHINCSPLUS_GLOBAL__


