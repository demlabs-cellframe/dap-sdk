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

/* ===== NTT algorithm class & CPU-specific tune rules ===== */

/*
 * AMD Zen4 (Raphael/Phoenix): family 0x19, model 0x60-0x7F
 *   AVX-512 uses double-pumping (256-bit units), causing frequency
 *   throttling on sustained SIMD workloads like NTT butterfly.
 *
 * AMD Zen5 (Granite Ridge / Strix Point): family 0x1A
 *   512-bit data paths present but still observed to throttle under
 *   sustained load.
 *
 * Intel Xeon / Core: no rules needed — AVX-512 runs at full width
 *   without throttling on server parts (Skylake-X+), and desktop
 *   Alder/Raptor Lake simply lacks AVX-512.
 */
static dap_algo_class_t s_ntt_class;

static void s_register_ntt_tune_rules(void)
{
    s_ntt_class = dap_algo_class_register("NTT");

    static const dap_cpu_tune_rule_t s_rules[] = {
        { DAP_CPU_VENDOR_AMD, 0x19, 0x19, 0x60, 0x7F,
          0 /* patched below */, DAP_CPU_ARCH_AVX2 },
        { DAP_CPU_VENDOR_AMD, 0x1A, 0x1A, 0x00, 0xFF,
          0, DAP_CPU_ARCH_AVX2 },
    };

    dap_cpu_tune_rule_t l_rules[sizeof(s_rules) / sizeof(s_rules[0])];
    for (size_t i = 0; i < sizeof(s_rules) / sizeof(s_rules[0]); i++) {
        l_rules[i] = s_rules[i];
        l_rules[i].algo_class = s_ntt_class;
    }
    dap_cpu_tune_add_rules(l_rules, sizeof(l_rules) / sizeof(l_rules[0]));
}

/* ===== One-shot lazy initialization ===== */

void dap_ntt_dispatch_init(void)
{
    s_register_ntt_tune_rules();

    DAP_DISPATCH_DEFAULT(dap_ntt_forward,               dap_ntt_forward_ref);
    DAP_DISPATCH_DEFAULT(dap_ntt_inverse,               dap_ntt_inverse_ref);
    DAP_DISPATCH_DEFAULT(dap_ntt_forward_mont,          dap_ntt_forward_mont_ref);
    DAP_DISPATCH_DEFAULT(dap_ntt_inverse_mont,          dap_ntt_inverse_mont_ref);
    DAP_DISPATCH_DEFAULT(dap_ntt_pointwise_montgomery,  dap_ntt_pointwise_montgomery_ref);

    DAP_DISPATCH_DEFAULT(dap_ntt16_forward, dap_ntt16_forward_ref);
    DAP_DISPATCH_DEFAULT(dap_ntt16_inverse, dap_ntt16_inverse_ref);
    DAP_DISPATCH_DEFAULT(dap_ntt16_basemul, dap_ntt16_basemul_ref);

    DAP_DISPATCH_ARCH_SELECT_FOR(s_ntt_class);

    /* 16-bit NTT SIMD backends */
    DAP_DISPATCH_X86(DAP_CPU_ARCH_SSE2,   dap_ntt16_forward, dap_ntt16_forward_sse2);
    DAP_DISPATCH_X86(DAP_CPU_ARCH_SSE2,   dap_ntt16_inverse, dap_ntt16_inverse_sse2);
    DAP_DISPATCH_X86(DAP_CPU_ARCH_SSE2,   dap_ntt16_basemul, dap_ntt16_basemul_sse2);

    DAP_DISPATCH_X86(DAP_CPU_ARCH_AVX2,   dap_ntt16_forward, dap_ntt16_forward_avx2);
    DAP_DISPATCH_X86(DAP_CPU_ARCH_AVX2,   dap_ntt16_inverse, dap_ntt16_inverse_avx2);
    DAP_DISPATCH_X86(DAP_CPU_ARCH_AVX2,   dap_ntt16_basemul, dap_ntt16_basemul_avx2);

    DAP_DISPATCH_X86(DAP_CPU_ARCH_AVX512, dap_ntt16_forward, dap_ntt16_forward_avx512);
    DAP_DISPATCH_X86(DAP_CPU_ARCH_AVX512, dap_ntt16_inverse, dap_ntt16_inverse_avx512);
    DAP_DISPATCH_X86(DAP_CPU_ARCH_AVX512, dap_ntt16_basemul, dap_ntt16_basemul_avx512);

    DAP_DISPATCH_ARM(DAP_CPU_ARCH_NEON,   dap_ntt16_forward, dap_ntt16_forward_neon);
    DAP_DISPATCH_ARM(DAP_CPU_ARCH_NEON,   dap_ntt16_inverse, dap_ntt16_inverse_neon);
    DAP_DISPATCH_ARM(DAP_CPU_ARCH_NEON,   dap_ntt16_basemul, dap_ntt16_basemul_neon);

    /* 32-bit Montgomery NTT SIMD backends */
    DAP_DISPATCH_X86(DAP_CPU_ARCH_AVX2,   dap_ntt_forward_mont,          dap_ntt_forward_mont_avx2);
    DAP_DISPATCH_X86(DAP_CPU_ARCH_AVX2,   dap_ntt_inverse_mont,          dap_ntt_inverse_mont_avx2);
    DAP_DISPATCH_X86(DAP_CPU_ARCH_AVX2,   dap_ntt_pointwise_montgomery,  dap_ntt_pointwise_montgomery_avx2);

    DAP_DISPATCH_X86(DAP_CPU_ARCH_AVX512, dap_ntt_forward_mont,          dap_ntt_forward_mont_avx512);
    DAP_DISPATCH_X86(DAP_CPU_ARCH_AVX512, dap_ntt_inverse_mont,          dap_ntt_inverse_mont_avx512);
    DAP_DISPATCH_X86(DAP_CPU_ARCH_AVX512, dap_ntt_pointwise_montgomery,  dap_ntt_pointwise_montgomery_avx512);

    DAP_DISPATCH_ARM(DAP_CPU_ARCH_NEON,   dap_ntt_forward_mont,          dap_ntt_forward_mont_neon);
    DAP_DISPATCH_ARM(DAP_CPU_ARCH_NEON,   dap_ntt_inverse_mont,          dap_ntt_inverse_mont_neon);
    DAP_DISPATCH_ARM(DAP_CPU_ARCH_NEON,   dap_ntt_pointwise_montgomery,  dap_ntt_pointwise_montgomery_neon);
}
