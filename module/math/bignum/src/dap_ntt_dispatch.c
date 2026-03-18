/**
 * @file dap_ntt_dispatch.c
 * @brief Dispatch pointer storage and init for NTT: selects reference or SIMD backend.
 *
 * Public API is static inline in dap_ntt.h; this file only provides
 * the extern function pointer storage and the one-shot init function.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "dap_ntt_internal.h"

/* ===== Extern function pointer storage ===== */

DAP_DISPATCH_DEFINE(dap_ntt_forward);
DAP_DISPATCH_DEFINE(dap_ntt_inverse);
DAP_DISPATCH_DEFINE(dap_ntt_forward_mont);
DAP_DISPATCH_DEFINE(dap_ntt_inverse_mont);
DAP_DISPATCH_DEFINE(dap_ntt_pointwise_montgomery);

DAP_DISPATCH_DEFINE(dap_ntt16_forward);
DAP_DISPATCH_DEFINE(dap_ntt16_inverse);
DAP_DISPATCH_DEFINE(dap_ntt16_basemul);

/* ===== One-shot lazy initialization ===== */

void dap_ntt_dispatch_init(void)
{
    DAP_DISPATCH_DEFAULT(dap_ntt_forward,               dap_ntt_forward_ref);
    DAP_DISPATCH_DEFAULT(dap_ntt_inverse,               dap_ntt_inverse_ref);
    DAP_DISPATCH_DEFAULT(dap_ntt_forward_mont,          dap_ntt_forward_mont_ref);
    DAP_DISPATCH_DEFAULT(dap_ntt_inverse_mont,          dap_ntt_inverse_mont_ref);
    DAP_DISPATCH_DEFAULT(dap_ntt_pointwise_montgomery,  dap_ntt_pointwise_montgomery_ref);

    DAP_DISPATCH_DEFAULT(dap_ntt16_forward, dap_ntt16_forward_ref);
    DAP_DISPATCH_DEFAULT(dap_ntt16_inverse, dap_ntt16_inverse_ref);
    DAP_DISPATCH_DEFAULT(dap_ntt16_basemul, dap_ntt16_basemul_ref);

    DAP_DISPATCH_ARCH_SELECT;

    DAP_DISPATCH_X86(DAP_CPU_ARCH_AVX2,   dap_ntt16_forward, dap_ntt16_forward_avx2);
    DAP_DISPATCH_X86(DAP_CPU_ARCH_AVX2,   dap_ntt16_inverse, dap_ntt16_inverse_avx2);
    DAP_DISPATCH_X86(DAP_CPU_ARCH_AVX2,   dap_ntt16_basemul, dap_ntt16_basemul_avx2);

    DAP_DISPATCH_X86(DAP_CPU_ARCH_AVX512, dap_ntt16_forward, dap_ntt16_forward_avx512);
    DAP_DISPATCH_X86(DAP_CPU_ARCH_AVX512, dap_ntt16_inverse, dap_ntt16_inverse_avx512);
    DAP_DISPATCH_X86(DAP_CPU_ARCH_AVX512, dap_ntt16_basemul, dap_ntt16_basemul_avx512);

    DAP_DISPATCH_ARM(DAP_CPU_ARCH_NEON,   dap_ntt16_forward, dap_ntt16_forward_neon);
    DAP_DISPATCH_ARM(DAP_CPU_ARCH_NEON,   dap_ntt16_inverse, dap_ntt16_inverse_neon);
    DAP_DISPATCH_ARM(DAP_CPU_ARCH_NEON,   dap_ntt16_basemul, dap_ntt16_basemul_neon);
}
