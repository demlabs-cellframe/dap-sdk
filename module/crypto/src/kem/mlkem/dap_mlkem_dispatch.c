/**
 * @file dap_mlkem_dispatch.c
 * @brief Aggregated SIMD dispatch init for all ML-KEM variants.
 *
 * Calls per-variant dispatch init for ML-KEM-512, ML-KEM-768, ML-KEM-1024.
 * Called once from dap_enc_init() before any crypto hot path.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "dap_mlkem_params.h"

/* Per-variant dispatch init declarations (defined in _ntt.c, _poly.c, etc.) */
extern void dap_mlkem512_ntt_dispatch_init(void);
extern void dap_mlkem512_poly_dispatch_init(void);
extern void dap_mlkem512_polyvec_dispatch_init(void);
extern void dap_mlkem512_rej_uniform_dispatch_init(void);

extern void dap_mlkem768_ntt_dispatch_init(void);
extern void dap_mlkem768_poly_dispatch_init(void);
extern void dap_mlkem768_polyvec_dispatch_init(void);
extern void dap_mlkem768_rej_uniform_dispatch_init(void);

extern void dap_mlkem1024_ntt_dispatch_init(void);
extern void dap_mlkem1024_poly_dispatch_init(void);
extern void dap_mlkem1024_polyvec_dispatch_init(void);
extern void dap_mlkem1024_rej_uniform_dispatch_init(void);

void dap_mlkem_dispatch_init(void)
{
    dap_mlkem512_ntt_dispatch_init();
    dap_mlkem512_poly_dispatch_init();
    dap_mlkem512_polyvec_dispatch_init();
    dap_mlkem512_rej_uniform_dispatch_init();

    dap_mlkem768_ntt_dispatch_init();
    dap_mlkem768_poly_dispatch_init();
    dap_mlkem768_polyvec_dispatch_init();
    dap_mlkem768_rej_uniform_dispatch_init();

    dap_mlkem1024_ntt_dispatch_init();
    dap_mlkem1024_poly_dispatch_init();
    dap_mlkem1024_polyvec_dispatch_init();
    dap_mlkem1024_rej_uniform_dispatch_init();
}
