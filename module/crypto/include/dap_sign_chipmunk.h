/*
 * Authors:
 * Dmitry A. Gerasimov <ceo@cellframe.net>
 * DeM Labs Inc.   https://demlabs.net
 * DeM Labs Open source community https://gitlab.demlabs.net/cellframe
 * Copyright  (c) 2017-2026
 * All rights reserved.
 *
 * This file is part of DAP (Distributed Applications Platform).
 * DAP SDK is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version.
 */

#pragma once

/*
 * Chipmunk-specific bridges between the generic dap_sign_t container
 * and the in-memory Chipmunk multi-signature structure.
 *
 * The generic dap_sign.h intentionally stays scheme-agnostic; every
 * scheme-specific adapter lives in its own header so that consumers
 * that do not touch Chipmunk do not drag chipmunk_aggregation.h and
 * its transitive dependencies into their translation units.
 */

#include "dap_sign.h"
#include "chipmunk/chipmunk_aggregation.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Wrap an in-memory Chipmunk multi-signature into a dap_sign_t blob.
 *
 * CR-D10 producer bridge: serialises the struct through the canonical
 * "CHMA" wire codec (see chipmunk_multi_signature_codec.h) and attaches
 * it as the sig payload of a fresh dap_sign_t with
 * type=SIG_TYPE_CHIPMUNK and sign_pkey_size=0 — every signer's HOTS
 * public key is already embedded in the blob, so there is no single
 * dap_sign-level pkey to attach.
 *
 * Ownership:
 *   - The caller retains ownership of `a_multi_sig` and its backing
 *     heap buffers.  The bridge makes a wire-byte copy, so the source
 *     may be freed immediately after this call.
 *   - The returned dap_sign_t is heap-allocated and must be released
 *     by the caller with DAP_DELETE.
 *
 * @return Newly-allocated dap_sign_t on success, NULL on allocation
 *         or codec failure (errors are logged).
 */
dap_sign_t *dap_sign_from_chipmunk_multi_signature(
    const chipmunk_multi_signature_t *a_multi_sig
);

#ifdef __cplusplus
}
#endif
