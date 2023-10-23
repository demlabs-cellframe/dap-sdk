#include "sphincsplus_params.h"

sphincsplus_param_t s_params[] = {
  [SPHINCS_HARAKA_128F] = {
    .spx_n = 16,
    .spx_full_height = 66,
    .spx_d = 22,
    .spx_fors_height = 6,
    .spx_fors_trees = 33,
    .spx_wots_w = 16,
    .apx_addr_bytes = 32,
    .spx_sha512 = 0
  },
  [SPHINCS_HARAKA_128S] = {
    .spx_n = 16,
    .spx_full_height = 63,
    .spx_d = 7,
    .spx_fors_height = 12,
    .spx_fors_trees = 14,
    .spx_wots_w = 16,
    .apx_addr_bytes = 32,
    .spx_sha512 = 0
    },
  [SPHINCS_HARAKA_192F] = {
    .spx_n = 24,
    .spx_full_height = 66,
    .spx_d = 22,
    .spx_fors_height = 8,
    .spx_fors_trees = 33,
    .spx_wots_w = 16,
    .apx_addr_bytes = 32,
    .spx_sha512 = 0
    },
  [SPHINCS_HARAKA_192S] = {
    .spx_n = 24,
    .spx_full_height = 63,
    .spx_d = 7,
    .spx_fors_height = 14,
    .spx_fors_trees = 17,
    .spx_wots_w = 16,
    .apx_addr_bytes = 32,
    .spx_sha512 = 0
    },
  [SPHINCS_HARAKA_256F] = {
    .spx_n = 32,
    .spx_full_height = 68,
    .spx_d = 17,
    .spx_fors_height = 9,
    .spx_fors_trees = 35,
    .spx_wots_w = 16,
    .apx_addr_bytes = 32,
    .spx_sha512 = 0
    },
  [SPHINCS_HARAKA_256S] = {
    .spx_n = 32,
    .spx_full_height = 64,
    .spx_d = 8,
    .spx_fors_height = 14,
    .spx_fors_trees = 22,
    .spx_wots_w = 16,
    .apx_addr_bytes = 32,
    .spx_sha512 = 0
    },
  [SPHINCS_SHA2_128F] = {
    .spx_n = 16,
    .spx_full_height = 66,
    .spx_d = 22,
    .spx_fors_height = 6,
    .spx_fors_trees = 33,
    .spx_wots_w = 16,
    .apx_addr_bytes = 32,
    .spx_sha512 = 0
    },
  [SPHINCS_SHA2_128S] = {
    .spx_n = 16,
    .spx_full_height = 63,
    .spx_d = 7,
    .spx_fors_height = 12,
    .spx_fors_trees = 14,
    .spx_wots_w = 16,
    .apx_addr_bytes = 32,
    .spx_sha512 = 0
    },
  [SPHINCS_SHA2_192F] = {
    .spx_n = 24,
    .spx_full_height = 66,
    .spx_d = 22,
    .spx_fors_height = 8,
    .spx_fors_trees = 33,
    .spx_wots_w = 16,
    .apx_addr_bytes = 32
    },
  [SPHINCS_SHA2_192S] = {
    .spx_n = 24,
    .spx_full_height = 63,
    .spx_d = 7,
    .spx_fors_height = 14,
    .spx_fors_trees = 17,
    .spx_wots_w = 16,
    .apx_addr_bytes = 32,
    .spx_sha512 = 1
    },
  [SPHINCS_SHA2_256F] = {
    .spx_n = 32,
    .spx_full_height = 68,
    .spx_d = 17,
    .spx_fors_height = 9,
    .spx_fors_trees = 35,
    .spx_wots_w = 16,
    .apx_addr_bytes = 32,
    .spx_sha512 = 1
    },
  [SPHINCS_SHA2_256S] = {
    .spx_n = 32,
    .spx_full_height = 64,
    .spx_d = 8,
    .spx_fors_height = 14,
    .spx_fors_trees = 22,
    .spx_wots_w = 16,
    .apx_addr_bytes = 32,
    .spx_sha512 = 1
    },
  [SPHINCS_SHAKE_128F] = {
    .spx_n = 16,
    .spx_full_height = 66,
    .spx_d = 22,
    .spx_fors_height = 6,
    .spx_fors_trees = 33,
    .spx_wots_w = 16,
    .apx_addr_bytes = 32,
    .spx_sha512 = 0
    },
  [SPHINCS_SHAKE_128S] = {
    .spx_n = 16,
    .spx_full_height = 63,
    .spx_d = 7,
    .spx_fors_height = 12,
    .spx_fors_trees = 14,
    .spx_wots_w = 16,
    .apx_addr_bytes = 32,
    .spx_sha512 = 0
    },
  [SPHINCS_SHAKE_192F] = {
    .spx_n = 24,
    .spx_full_height = 66,
    .spx_d = 22,
    .spx_fors_height = 8,
    .spx_fors_trees = 33,
    .spx_wots_w = 16,
    .apx_addr_bytes = 32,
    .spx_sha512 = 0
    },
  [SPHINCS_SHAKE_192S] = {
    .spx_n = 24,
    .spx_full_height = 63,
    .spx_d = 7,
    .spx_fors_height = 14,
    .spx_fors_trees = 17,
    .spx_wots_w = 16,
    .apx_addr_bytes = 32,
    .spx_sha512 = 0
    },
  [SPHINCS_SHAKE_256F] = {
    .spx_n = 32,
    .spx_full_height = 68,
    .spx_d = 17,
    .spx_fors_height = 9,
    .spx_fors_trees = 35,
    .spx_wots_w = 16,
    .apx_addr_bytes = 32,
    .spx_sha512 = 0
    },
  [SPHINCS_SHAKE_256S] = {
    .spx_n = 32,
    .spx_full_height = 68,
    .spx_d = 17,
    .spx_fors_height = 9,
    .spx_fors_trees = 35,
    .spx_wots_w = 16,
    .apx_addr_bytes = 32,
    .spx_sha512 = 0
    },
};