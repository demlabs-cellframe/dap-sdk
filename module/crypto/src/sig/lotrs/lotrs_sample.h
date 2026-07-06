/*
 * LoTRS — XOF-based samplers.
 *
 * Uniform, short, ternary, and rejection sampling for LoTRS.
 */

#pragma once
#ifndef _LOTRS_SAMPLE_H_
#define _LOTRS_SAMPLE_H_

#include <stdint.h>
#include <stddef.h>

#include "lotrs_params.h"
#include "lotrs_ring.h"

/* XOF context (wraps SHAKE128). */
typedef struct lotrs_xof lotrs_xof_t;

lotrs_xof_t *lotrs_xof_new(const uint8_t *seed, size_t seed_len);
void         lotrs_xof_free(lotrs_xof_t *xof);
int          lotrs_xof_absorb(lotrs_xof_t *xof, const uint8_t *data, size_t len);
void         lotrs_xof_squeeze(lotrs_xof_t *xof, uint8_t *out, size_t out_len);

/* Sample uniform polynomial in [0, q). */
int lotrs_sample_uniform(lotrs_poly_t *out, lotrs_xof_t *xof,
                         const lotrs_params_t *par);

/* Sample short polynomial in {-eta, ..., eta}. */
int lotrs_sample_short(lotrs_poly_t *out, lotrs_xof_t *xof,
                       const lotrs_params_t *par, uint32_t eta);

/* Sample ternary polynomial with weight w (±1 coefficients). */
int lotrs_sample_ternary(lotrs_poly_t *out, lotrs_xof_t *xof,
                         const lotrs_params_t *par, uint32_t weight);

/* Sample short vector (multiple polynomials). */
int lotrs_sample_short_vec(lotrs_polyvec_t *out, lotrs_xof_t *xof,
                           const lotrs_params_t *par, uint32_t eta);

/* Rejection sampling: check ‖z‖∞ < bound. Returns 1 if accepted, 0 if rejected. */
int lotrs_reject_infinity_norm(const lotrs_poly_t *z, int64_t bound,
                               const lotrs_params_t *par);

/* Rejection sampling: check ‖z‖₂² < bound_sq. Returns 1 if accepted, 0 if rejected. */
int lotrs_reject_l2_norm_sq(const lotrs_polyvec_t *z, int64_t bound_sq,
                            const lotrs_params_t *par);

#endif /* _LOTRS_SAMPLE_H_ */
