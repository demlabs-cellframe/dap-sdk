/*
 * Authors:
 * Dmitry A. Gerasimov <ceo@cellframe.net>
 * DeM Labs Ltd
 * Copyright (c) 2025-2026
 *
 * Chipmunk-specific adapters between dap_sign_t and the lower-level
 * Chipmunk multi-signature codec (CR-D10).  Confined to the aggregated
 * SIG_TYPE_CHIPMUNK path; the linkable ring signature (CHIPMUNK_RING /
 * chipmunk_lrs) does not go through this header.
 */

#pragma once

#include "dap_sign.h"
#include "chipmunk/chipmunk_multi_signature_codec.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Wrap a Chipmunk multi-signature blob into a dap_sign_t shell
 *        (SIG_TYPE_CHIPMUNK, sign_pkey_size = 0).
 */
dap_sign_t *dap_sign_from_chipmunk_multi_signature(const chipmunk_multi_signature_t *a_multi_sig);

#ifdef __cplusplus
}
#endif
