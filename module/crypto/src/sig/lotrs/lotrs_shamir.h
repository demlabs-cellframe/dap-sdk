/*
 * LoTRS — Shamir secret sharing over R_q.
 *
 * Splits a polynomial secret into N shares with threshold T.
 * Any T shares can reconstruct the secret via Lagrange interpolation.
 */

#pragma once
#ifndef _LOTRS_SHAMIR_H_
#define _LOTRS_SHAMIR_H_

#include <stdint.h>

#include "lotrs_params.h"
#include "lotrs_ring.h"

/*
 * Split a polynomial secret into N shares with threshold T.
 *
 * @param a_shares  Receives N allocated polynomials (caller frees).
 * @param a_secret  The secret polynomial to split.
 * @param a_N       Number of shares.
 * @param a_T       Threshold (minimum shares to reconstruct).
 * @param a_par     Parameters.
 * @param a_xof     XOF for randomness (already seeded).
 *
 * @return 0 on success.
 */
int lotrs_shamir_split(lotrs_poly_t **a_shares,
                       const lotrs_poly_t *a_secret,
                       uint32_t a_N, uint32_t a_T,
                       const lotrs_params_t *a_par,
                       void *a_xof);

/*
 * Reconstruct secret from T shares via Lagrange interpolation.
 *
 * @param a_out     Receives reconstructed polynomial.
 * @param a_shares  Array of T share polynomials.
 * @param a_indices Array of T share indices (1-based).
 * @param a_T       Number of shares.
 * @param a_par     Parameters.
 *
 * @return 0 on success.
 */
int lotrs_shamir_reconstruct(lotrs_poly_t *a_out,
                             const lotrs_poly_t *const *a_shares,
                             const uint32_t *a_indices,
                             uint32_t a_T,
                             const lotrs_params_t *a_par);

#endif /* _LOTRS_SHAMIR_H_ */
