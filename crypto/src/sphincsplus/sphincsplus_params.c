#include "sphincsplus_params.h"
#include "dap_common.h"

#define LOG_TAG "dap_enc_sig_sphincsplus_params"

const sphincsplus_offsets_t s_haraka_offsets = {
    .spx_offset_layer = 3,
    .spx_offset_tree = 8,
    .spx_offset_type = 19,
    .spx_offset_kp_addr2 = 22,
    .spx_offset_kp_addr1 = 23,
    .spx_offset_chain_addr = 27,
    .spx_offset_hash_addr = 31,
    .spx_offset_tree_hgt = 27,
    .spx_offset_tree_index = 28
};
const sphincsplus_offsets_t s_sha2_offsets = {
    .spx_offset_layer = 0,
    .spx_offset_tree = 1,
    .spx_offset_type = 9,
    .spx_offset_kp_addr2 = 12,
    .spx_offset_kp_addr1 = 13,
    .spx_offset_chain_addr = 17,
    .spx_offset_hash_addr = 21,
    .spx_offset_tree_hgt = 17,
    .spx_offset_tree_index = 18
};
const sphincsplus_offsets_t s_shake_offsets = {
    .spx_offset_layer = 3,
    .spx_offset_tree = 8,
    .spx_offset_type = 19,
    .spx_offset_kp_addr2 = 22,
    .spx_offset_kp_addr1 = 23,
    .spx_offset_chain_addr = 27,
    .spx_offset_hash_addr = 31,
    .spx_offset_tree_hgt = 27,
    .spx_offset_tree_index = 28
};

const sphincsplus_param_t s_params[] = {
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
        .offsets = s_haraka_offsets
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
        .offsets = s_haraka_offsets
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
        .offsets = s_haraka_offsets
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
        .offsets = s_haraka_offsets
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
        .offsets = s_haraka_offsets
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
        .offsets = s_haraka_offsets
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
        .offsets = s_sha2_offsets
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
        .offsets = s_sha2_offsets
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
        .offsets = s_sha2_offsets
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
        .offsets = s_sha2_offsets
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
        .offsets = s_sha2_offsets
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
        .offsets = s_sha2_offsets
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
        .offsets = s_shake_offsets
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
        .offsets = s_shake_offsets
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
        .offsets = s_shake_offsets
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
        .offsets = s_shake_offsets
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
        .offsets = s_shake_offsets
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
        .offsets = s_shake_offsets
    },
};

int sphincsplus_set_params(const sphincsplus_param_t *a_params) {
    
    sphincsplus_param_t l_res = *a_params;
    if (l_res.spx_full_height % l_res.spx_d) {
        log_it(L_ERROR, "SPX_D should always divide SPX_FULL_HEIGHT");
        return -1;
    }
    if (l_res.spx_wots_w == 256) {
        l_res.spx_wots_logw = 8;
        if (l_res.spx_n <= 1) {
            l_res.spx_wots_len2 = 1;
        } else if (l_res.spx_n <= 256) {
            l_res.spx_wots_len2 = 2;
        } else {
            log_it(L_ERROR, "Did not precompute SPX_WOTS_LEN2 for n outside {2, .., 256}");
            return -3;
        }
    } else if(l_res.spx_wots_w == 16) {
        l_res.spx_wots_logw = 4;

        if (l_res.spx_n <= 8) {
            l_res.spx_wots_len2 = 2;
        } else if (l_res.spx_n <= 136) {
            l_res.spx_wots_len2 = 3;
        } else if (l_res.spx_n <= 256) {
            l_res.spx_wots_len2 = 4;
        } else {
            log_it(L_ERROR, "Did not precompute SPX_WOTS_LEN2 for n outside {2, .., 256}");
            return -4;
        }
    } else {
        log_it(L_ERROR, "SPX_WOTS_W assumed 16 or 256");
        return -2;
    }

    l_res.spx_wots_len1 = (8 * l_res.spx_n) / l_res.spx_wots_logw;
    l_res.spx_wots_len = l_res.spx_wots_len1 + l_res.spx_wots_len2;
    l_res.spx_wots_bytes = l_res.spx_wots_len*l_res.spx_n;
    l_res.spx_wots_pk_bytes = l_res.spx_wots_bytes;
    
    l_res.spx_tree_height = l_res.spx_full_height / l_res.spx_d;

    l_res.spx_fors_msg_bytes = (l_res.spx_fors_height * l_res.spx_fors_trees + 7) / 8;
    l_res.spx_fors_bytes = (l_res.spx_fors_height + 1) * l_res.spx_fors_trees * l_res.spx_n;
    l_res.spx_fors_pk_bytes = l_res.spx_n;

    l_res.spx_bytes = l_res.spx_n + l_res.spx_fors_bytes + l_res.spx_d * l_res.spx_wots_bytes + l_res.spx_full_height * l_res.spx_n;
    l_res.spx_pk_bytes = 2 * l_res.spx_n;
    l_res.spx_sk_bytes = 2 * l_res.spx_n + l_res.spx_pk_bytes;

    g_sphincsplus_params_current = l_res;
    return 0;
}

int sphincsplus_set_config(sphincsplus_config_t a_config) {
    if (a_config <= SPHINCSPLUS_CONFIG_MIN_ARG || a_config >= SPHINCSPLUS_CONFIG_MAX_ARG) {
        log_it(L_ERROR, "Wrong sphincplus sig config");
        return -1;
    }
    if (a_config == g_sphincsplus_params_current.config)
        return 0;
    return sphincsplus_set_params(&s_params[a_config]);
}

sphincsplus_param_t sphincsplus_get_params(sphincsplus_config_t a_config) {
    if(a_config <= SPHINCSPLUS_CONFIG_MIN_ARG || a_config >= SPHINCSPLUS_CONFIG_MAX_ARG) {
        log_it(L_ERROR, "Wrong sphincplus sig config");
        return (sphincsplus_param_t){0};
    }
    return s_params[a_config];
}
