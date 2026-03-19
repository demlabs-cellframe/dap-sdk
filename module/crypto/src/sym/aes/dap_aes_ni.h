/**
 * @file dap_aes_ni.h
 * @brief AES-256-CBC using Intel AES-NI intrinsics.
 *
 * Internal header — use dap_enc_aes.h public API instead.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "dap_enc_key.h"

#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)

size_t dap_aes_ni_cbc_encrypt_fast(struct dap_enc_key *a_key, const void *a_in,
                                   size_t a_in_size, void *a_out, size_t a_out_size);

size_t dap_aes_ni_cbc_decrypt_fast(struct dap_enc_key *a_key, const void *a_in,
                                   size_t a_in_size, void *a_out, size_t a_out_size);

#endif
