/**
 * @file dap_aes_armce.h
 * @brief AES-256-CBC using ARM Crypto Extensions.
 *
 * Internal header — use dap_enc_aes.h public API instead.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "dap_enc_key.h"
#include "dap_cpu_arch.h"

#if DAP_PLATFORM_ARM

size_t dap_aes_armce_cbc_encrypt_fast(struct dap_enc_key *a_key, const void *a_in,
                                      size_t a_in_size, void *a_out, size_t a_out_size);

size_t dap_aes_armce_cbc_decrypt_fast(struct dap_enc_key *a_key, const void *a_in,
                                      size_t a_in_size, void *a_out, size_t a_out_size);

#endif
