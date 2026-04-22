/**
 * @file dap_mlkem_cbd.c
 * @brief Centered Binomial Distribution sampling for ML-KEM.
 *
 * All SIMD bodies live in arch/{x86,arm}/poly_fast_*.inc,
 * dispatched via dap_mlkem_poly_simd.h generated from .tpl.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdint.h>
#include "dap_mlkem_cbd.h"
#include "dap_mlkem_poly_simd.h"

MLKEM_HOTFN void MLKEM_NAMESPACE(_cbd_eta1)(dap_mlkem_poly *a_r,
                                 const uint8_t a_buf[MLKEM_ETA1 * MLKEM_N / 4])
{
#if MLKEM_ETA1 == 2
    dap_mlkem_cbd2_fast(a_r->coeffs, a_buf);
#elif MLKEM_ETA1 == 3
    dap_mlkem_cbd3_fast(a_r->coeffs, a_buf);
#else
#error "MLKEM_ETA1 must be 2 or 3"
#endif
}

MLKEM_HOTFN void MLKEM_NAMESPACE(_cbd_eta2)(dap_mlkem_poly *a_r,
                                 const uint8_t a_buf[MLKEM_ETA2 * MLKEM_N / 4])
{
    dap_mlkem_cbd2_fast(a_r->coeffs, a_buf);
}
