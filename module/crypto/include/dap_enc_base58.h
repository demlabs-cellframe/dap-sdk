/**
 * @file dap_enc_base58.h
 * @brief Legacy header - redirects to dap_base58.h in core module
 *
 * This file is kept for backward compatibility.
 * New code should use #include "dap_base58.h" directly.
 */

#pragma once

// Redirect to core module implementation
#include "dap_base58.h"

// For hash-related functions, include hash module
#include "dap_hash_sha3.h"
