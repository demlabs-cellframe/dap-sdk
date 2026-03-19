/**
 * @file dap_mlkem_cbd.c
 * @brief Centered Binomial Distribution sampling for ML-KEM.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdint.h>
#include "dap_mlkem_cbd.h"

static inline uint32_t s_load32_le(const uint8_t x[4])
{
    return (uint32_t)x[0]
         | (uint32_t)x[1] << 8
         | (uint32_t)x[2] << 16
         | (uint32_t)x[3] << 24;
}

static void s_cbd2(dap_mlkem_poly *a_r, const uint8_t *a_buf)
{
    for (unsigned i = 0; i < MLKEM_N / 8; i++) {
        uint32_t t = s_load32_le(a_buf + 4 * i);
        uint32_t d = (t & 0x55555555) + ((t >> 1) & 0x55555555);
        for (unsigned j = 0; j < 8; j++) {
            int16_t a = (int16_t)((d >> (4 * j))     & 0x3);
            int16_t b = (int16_t)((d >> (4 * j + 2)) & 0x3);
            a_r->coeffs[8 * i + j] = a - b;
        }
    }
}

#if MLKEM_ETA1 == 3
static inline uint32_t s_load24_le(const uint8_t x[3])
{
    return (uint32_t)x[0]
         | (uint32_t)x[1] << 8
         | (uint32_t)x[2] << 16;
}

static void s_cbd3(dap_mlkem_poly *a_r, const uint8_t *a_buf)
{
    for (unsigned i = 0; i < MLKEM_N / 4; i++) {
        uint32_t t = s_load24_le(a_buf + 3 * i);
        uint32_t d = (t & 0x00249249)
                   + ((t >> 1) & 0x00249249)
                   + ((t >> 2) & 0x00249249);
        for (unsigned j = 0; j < 4; j++) {
            int16_t a = (int16_t)((d >> (6 * j))     & 0x7);
            int16_t b = (int16_t)((d >> (6 * j + 3)) & 0x7);
            a_r->coeffs[4 * i + j] = a - b;
        }
    }
}
#endif

void MLKEM_NAMESPACE(_cbd_eta1)(dap_mlkem_poly *a_r,
                                 const uint8_t a_buf[MLKEM_ETA1 * MLKEM_N / 4])
{
#if MLKEM_ETA1 == 2
    s_cbd2(a_r, a_buf);
#elif MLKEM_ETA1 == 3
    s_cbd3(a_r, a_buf);
#else
#error "MLKEM_ETA1 must be 2 or 3"
#endif
}

void MLKEM_NAMESPACE(_cbd_eta2)(dap_mlkem_poly *a_r,
                                 const uint8_t a_buf[MLKEM_ETA2 * MLKEM_N / 4])
{
    s_cbd2(a_r, a_buf);
}
