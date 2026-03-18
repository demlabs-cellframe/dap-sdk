/*
 * Secure memory wiping — prevents compiler from optimizing out zeroing.
 *
 * Uses memset_s (C11 Annex K), explicit_bzero (POSIX), or inline asm barrier.
 * Originally based on Monero/Bitcoin Core memory_cleanse; rewritten for DAP SDK.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once

#include <stddef.h>

void *dap_memwipe(void *src, size_t n);
