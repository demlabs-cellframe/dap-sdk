#include "sphincsplus_params.h"
#include "sphincsplus_global.h"
#include "dap_common.h"

#define LOG_TAG "dap_enc_sig_sphincsplus_params"

_Thread_local sphincsplus_params_t g_sphincsplus_params_current = {0};

#define s_haraka_offsets {\
    .spx_offset_layer = 3,\
    .spx_offset_tree = 8,\
    .spx_offset_type = 19,\
    .spx_offset_kp_addr2 = 22,\
    .spx_offset_kp_addr1 = 23,\
    .spx_offset_chain_addr = 27,\
    .spx_offset_hash_addr = 31,\
    .spx_offset_tree_hgt = 27,\
    .spx_offset_tree_index = 28\
}
#define s_sha2_offsets {\
    .spx_offset_layer = 0,\
    .spx_offset_tree = 1,\
    .spx_offset_type = 9,\
    .spx_offset_kp_addr2 = 12,\
    .spx_offset_kp_addr1 = 13,\
    .spx_offset_chain_addr = 17,\
    .spx_offset_hash_addr = 21,\
    .spx_offset_tree_hgt = 17,\
    .spx_offset_tree_index = 18\
}
#define s_shake_offsets {\
    .spx_offset_layer = 3,\
    .spx_offset_tree = 8,\
    .spx_offset_type = 19,\
    .spx_offset_kp_addr2 = 22,\
    .spx_offset_kp_addr1 = 23,\
    .spx_offset_chain_addr = 27,\
    .spx_offset_hash_addr = 31,\
    .spx_offset_tree_hgt = 27,\
    .spx_offset_tree_index = 28\
}

static const sphincsplus_base_params_t s_params[] = {
    [SPHINCSPLUS_HARAKA_128F] = {
        .config = SPHINCSPLUS_HARAKA_128F,
        .spx_n = 16,
        .spx_full_height = 66,
        .spx_d = 22,
        .spx_fors_height = 6,
        .spx_fors_trees = 33,
        .spx_wots_w = 16,
        .spx_addr_bytes = 32,
        .spx_sha512 = 0,
        .offsets = s_haraka_offsets,
        .difficulty = SPHINCSPLUS_SIMPLE
    },
    [SPHINCSPLUS_HARAKA_128S] = {
        .config = SPHINCSPLUS_HARAKA_128S,
        .spx_n = 16,
        .spx_full_height = 63,
        .spx_d = 7,
        .spx_fors_height = 12,
        .spx_fors_trees = 14,
        .spx_wots_w = 16,
        .spx_addr_bytes = 32,
        .spx_sha512 = 0,
        .offsets = s_haraka_offsets,
        .difficulty = SPHINCSPLUS_SIMPLE
    },
    [SPHINCSPLUS_HARAKA_192F] = {
        .config = SPHINCSPLUS_HARAKA_192F,
        .spx_n = 24,
        .spx_full_height = 66,
        .spx_d = 22,
        .spx_fors_height = 8,
        .spx_fors_trees = 33,
        .spx_wots_w = 16,
        .spx_addr_bytes = 32,
        .spx_sha512 = 0,
        .offsets = s_haraka_offsets,
        .difficulty = SPHINCSPLUS_SIMPLE
    },
    [SPHINCSPLUS_HARAKA_192S] = {
        .config = SPHINCSPLUS_HARAKA_192S,
        .spx_n = 24,
        .spx_full_height = 63,
        .spx_d = 7,
        .spx_fors_height = 14,
        .spx_fors_trees = 17,
        .spx_wots_w = 16,
        .spx_addr_bytes = 32,
        .spx_sha512 = 0,
        .offsets = s_haraka_offsets,
        .difficulty = SPHINCSPLUS_SIMPLE
    },
    [SPHINCSPLUS_HARAKA_256F] = {
        .config = SPHINCSPLUS_HARAKA_256F,
        .spx_n = 32,
        .spx_full_height = 68,
        .spx_d = 17,
        .spx_fors_height = 9,
        .spx_fors_trees = 35,
        .spx_wots_w = 16,
        .spx_addr_bytes = 32,
        .spx_sha512 = 0,
        .offsets = s_haraka_offsets,
        .difficulty = SPHINCSPLUS_SIMPLE
    },
    [SPHINCSPLUS_HARAKA_256S] = {
        .config = SPHINCSPLUS_HARAKA_256S,
        .spx_n = 32,
        .spx_full_height = 64,
        .spx_d = 8,
        .spx_fors_height = 14,
        .spx_fors_trees = 22,
        .spx_wots_w = 16,
        .spx_addr_bytes = 32,
        .spx_sha512 = 0,
        .offsets = s_haraka_offsets,
        .difficulty = SPHINCSPLUS_SIMPLE
    },
    [SPHINCSPLUS_SHA2_128F] = {
        .config = SPHINCSPLUS_SHA2_128F,
        .spx_n = 16,
        .spx_full_height = 66,
        .spx_d = 22,
        .spx_fors_height = 6,
        .spx_fors_trees = 33,
        .spx_wots_w = 16,
        .spx_addr_bytes = 32,
        .spx_sha512 = 0,
        .offsets = s_sha2_offsets,
        .difficulty = SPHINCSPLUS_SIMPLE
    },
    [SPHINCSPLUS_SHA2_128S] = {
        .config = SPHINCSPLUS_SHA2_128S,
        .spx_n = 16,
        .spx_full_height = 63,
        .spx_d = 7,
        .spx_fors_height = 12,
        .spx_fors_trees = 14,
        .spx_wots_w = 16,
        .spx_addr_bytes = 32,
        .spx_sha512 = 0,
        .offsets = s_sha2_offsets,
        .difficulty = SPHINCSPLUS_SIMPLE
    },
    [SPHINCSPLUS_SHA2_192F] = {
        .config = SPHINCSPLUS_SHA2_192F,
        .spx_n = 24,
        .spx_full_height = 66,
        .spx_d = 22,
        .spx_fors_height = 8,
        .spx_fors_trees = 33,
        .spx_wots_w = 16,
        .spx_addr_bytes = 32,
        .spx_sha512 = 1,
        .offsets = s_sha2_offsets,
        .difficulty = SPHINCSPLUS_SIMPLE
    },
    [SPHINCSPLUS_SHA2_192S] = {
        .config = SPHINCSPLUS_SHA2_192S,
        .spx_n = 24,
        .spx_full_height = 63,
        .spx_d = 7,
        .spx_fors_height = 14,
        .spx_fors_trees = 17,
        .spx_wots_w = 16,
        .spx_addr_bytes = 32,
        .spx_sha512 = 1,
        .offsets = s_sha2_offsets,
        .difficulty = SPHINCSPLUS_SIMPLE
    },
    [SPHINCSPLUS_SHA2_256F] = {
        .config = SPHINCSPLUS_SHA2_256F,
        .spx_n = 32,
        .spx_full_height = 68,
        .spx_d = 17,
        .spx_fors_height = 9,
        .spx_fors_trees = 35,
        .spx_wots_w = 16,
        .spx_addr_bytes = 32,
        .spx_sha512 = 1,
        .offsets = s_sha2_offsets,
        .difficulty = SPHINCSPLUS_SIMPLE
    },
    [SPHINCSPLUS_SHA2_256S] = {
        .config = SPHINCSPLUS_SHA2_256S,
        .spx_n = 32,
        .spx_full_height = 64,
        .spx_d = 8,
        .spx_fors_height = 14,
        .spx_fors_trees = 22,
        .spx_wots_w = 16,
        .spx_addr_bytes = 32,
        .spx_sha512 = 1,
        .offsets = s_sha2_offsets,
        .difficulty = SPHINCSPLUS_SIMPLE
    },
    [SPHINCSPLUS_SHAKE_128F] = {
        .config = SPHINCSPLUS_SHAKE_128F,
        .spx_n = 16,
        .spx_full_height = 66,
        .spx_d = 22,
        .spx_fors_height = 6,
        .spx_fors_trees = 33,
        .spx_wots_w = 16,
        .spx_addr_bytes = 32,
        .spx_sha512 = 0,
        .offsets = s_shake_offsets,
        .difficulty = SPHINCSPLUS_SIMPLE
    },
    [SPHINCSPLUS_SHAKE_128S] = {
        .config = SPHINCSPLUS_SHAKE_128S,
        .spx_n = 16,
        .spx_full_height = 63,
        .spx_d = 7,
        .spx_fors_height = 12,
        .spx_fors_trees = 14,
        .spx_wots_w = 16,
        .spx_addr_bytes = 32,
        .spx_sha512 = 0,
        .offsets = s_shake_offsets,
        .difficulty = SPHINCSPLUS_SIMPLE
    },
    [SPHINCSPLUS_SHAKE_192F] = {
        .config = SPHINCSPLUS_SHAKE_192F,
        .spx_n = 24,
        .spx_full_height = 66,
        .spx_d = 22,
        .spx_fors_height = 8,
        .spx_fors_trees = 33,
        .spx_wots_w = 16,
        .spx_addr_bytes = 32,
        .spx_sha512 = 0,
        .offsets = s_shake_offsets,
        .difficulty = SPHINCSPLUS_SIMPLE
    },
    [SPHINCSPLUS_SHAKE_192S] = {
        .config = SPHINCSPLUS_SHAKE_192S,
        .spx_n = 24,
        .spx_full_height = 63,
        .spx_d = 7,
        .spx_fors_height = 14,
        .spx_fors_trees = 17,
        .spx_wots_w = 16,
        .spx_addr_bytes = 32,
        .spx_sha512 = 0,
        .offsets = s_shake_offsets,
        .difficulty = SPHINCSPLUS_SIMPLE
    },
    [SPHINCSPLUS_SHAKE_256F] = {
        .config = SPHINCSPLUS_SHAKE_256F,
        .spx_n = 32,
        .spx_full_height = 68,
        .spx_d = 17,
        .spx_fors_height = 9,
        .spx_fors_trees = 35,
        .spx_wots_w = 16,
        .spx_addr_bytes = 32,
        .spx_sha512 = 0,
        .offsets = s_shake_offsets,
        .difficulty = SPHINCSPLUS_SIMPLE
    },
    [SPHINCSPLUS_SHAKE_256S] = {
        .config = SPHINCSPLUS_SHAKE_256S,
        .spx_n = 32,
        .spx_full_height = 68,
        .spx_d = 17,
        .spx_fors_height = 9,
        .spx_fors_trees = 35,
        .spx_wots_w = 16,
        .spx_addr_bytes = 32,
        .spx_sha512 = 0,
        .offsets = s_shake_offsets,
        .difficulty = SPHINCSPLUS_SIMPLE
    },
};

int sphincsplus_check_params(const sphincsplus_base_params_t *a_base_params) {
    if (!a_base_params) {
        return -1;
    }
    if(memcmp(a_base_params, &s_params[a_base_params->config], sizeof(sphincsplus_base_params_t) - sizeof(sphincsplus_difficulty_t))) {
        log_it(L_ERROR, "Differents between current config and checked");
        return -2;
    }
    if (!a_base_params->spx_d || a_base_params->spx_full_height % a_base_params->spx_d) {
        log_it(L_ERROR, "SPX_D should always divide SPX_FULL_HEIGHT");
        return -3;
    }
#ifdef SPHINCSPLUS_FLEX
    if (SPX_SHA256_OUTPUT_BYTES < a_base_params->spx_n) {
        log_it(L_ERROR, "Linking against SHA-256 with N larger than 32 bytes is not supported");
        return -4;
    }
#endif
    if (a_base_params->spx_wots_w != 256 && a_base_params->spx_wots_w != 16) {
        log_it(L_ERROR, "SPX_WOTS_W assumed 16 or 256");
        return -5;
    }

    if (a_base_params->spx_n > 256) {
        log_it(L_ERROR, "Did not precompute SPX_WOTS_LEN2 for n outside {2, .., 256}");
        return -6;
    }
    return 0;
}

int sphincsplus_set_params(const sphincsplus_base_params_t *a_base_params)
{
    if (sphincsplus_check_params(a_base_params))
        return -1;
#ifdef SPHINCSPLUS_FLEX
    if (a_base_params->config == SPHINCSPLUS_CONFIG) {
        SPHINCSPLUS_DIFFICULTY = a_base_params->difficulty;
        return 0;
    }
    sphincsplus_params_t l_res = {0};
    l_res.base_params = *a_base_params;

    if (a_base_params->spx_wots_w == 256) {
        l_res.spx_wots_logw = 8;
        if (a_base_params->spx_n <= 1) {
            l_res.spx_wots_len2 = 1;
        } else {
            l_res.spx_wots_len2 = 2;
        }
    } else {
        l_res.spx_wots_logw = 4;
        if (a_base_params->spx_n <= 8) {
            l_res.spx_wots_len2 = 2;
        } else if (a_base_params->spx_n <= 136) {
            l_res.spx_wots_len2 = 3;
        } else {
            l_res.spx_wots_len2 = 4;
        }
    }

    l_res.spx_wots_len1 = (8 * a_base_params->spx_n) / l_res.spx_wots_logw;
    l_res.spx_wots_len = l_res.spx_wots_len1 + l_res.spx_wots_len2;
    l_res.spx_wots_bytes = l_res.spx_wots_len * a_base_params->spx_n;
    l_res.spx_wots_pk_bytes = l_res.spx_wots_bytes;
    
    l_res.spx_tree_height = a_base_params->spx_full_height / a_base_params->spx_d;

    l_res.spx_fors_msg_bytes = (a_base_params->spx_fors_height * a_base_params->spx_fors_trees + 7) / 8;
    l_res.spx_fors_bytes = (a_base_params->spx_fors_height + 1) * a_base_params->spx_fors_trees * a_base_params->spx_n;
    l_res.spx_fors_pk_bytes = a_base_params->spx_n;

    l_res.spx_bytes = a_base_params->spx_n + l_res.spx_fors_bytes + a_base_params->spx_d * l_res.spx_wots_bytes + 
                                        a_base_params->spx_full_height * a_base_params->spx_n;
    l_res.spx_pk_bytes = 2 * a_base_params->spx_n;
    l_res.spx_sk_bytes = 2 * a_base_params->spx_n + l_res.spx_pk_bytes;

    l_res.spx_tree_bits = l_res.spx_tree_height * (a_base_params->spx_d - 1);
    if (l_res.spx_tree_bits > 64) {
        log_it(L_ERROR, "For given height and depth, 64 bits cannot represent all subtrees");
        return -6;
    }
    l_res.spx_tree_bytes = (l_res.spx_tree_bits + 7) / 8;
    l_res.spx_leaf_bits = l_res.spx_tree_height;
    l_res.spx_leaf_bytes = (l_res.spx_leaf_bits + 7) / 8;
    l_res.spx_dgst_bytes = l_res.spx_fors_msg_bytes + l_res.spx_tree_bytes + l_res.spx_leaf_bytes; 

    if (a_base_params->spx_n >= 24) {
        l_res.spx_shax_output_bytes = SPX_SHA512_OUTPUT_BYTES;
        l_res.spx_shax_block_bytes = SPX_SHA512_BLOCK_BYTES;
    } else {
        l_res.spx_shax_output_bytes = SPX_SHA256_OUTPUT_BYTES;
        l_res.spx_shax_block_bytes = SPX_SHA256_BLOCK_BYTES;
    }

    if (a_base_params->spx_n > l_res.spx_shax_block_bytes) {
        log_it(L_ERROR, "Currently only supports SPX_N of at most SPX_SHAX_BLOCK_BYTES");
        return -8;
    }
    g_sphincsplus_params_current = l_res;
#endif
    return 0;
}

int sphincsplus_set_config(sphincsplus_config_t a_config) {
    if (a_config <= SPHINCSPLUS_CONFIG_MIN_ARG || a_config >= SPHINCSPLUS_CONFIG_MAX_ARG) {
        log_it(L_ERROR, "Wrong sphincplus sig config");
        return -1;
    }
    return sphincsplus_set_params(&s_params[a_config]);
}

int sphincsplus_get_params(sphincsplus_config_t a_config, sphincsplus_base_params_t *a_params) {
    if(!a_params) {
        return -1;
    }
    if(a_config <= SPHINCSPLUS_CONFIG_MIN_ARG || a_config >= SPHINCSPLUS_CONFIG_MAX_ARG) {
        log_it(L_ERROR, "Wrong sphincplus sig config");
        return -2;
    }
    *a_params = s_params[a_config];
    return 0;
}
