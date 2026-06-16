/*
 * LoTRS — polynomial ring R_q = Z_q[X]/(X^d + 1) arithmetic.
 *
 * Schoolbook for d=32; NTT fast path for d=128 (future).
 */

#pragma once
#ifndef _LOTRS_RING_H_
#define _LOTRS_RING_H_

#include <stdint.h>
#include <stddef.h>

#include "lotrs_params.h"
#include "dap_serialize.h"

/* Maximum ring dimension across all parameter sets. */
#define LOTRS_D_MAX 128u

/* Polynomial: d coefficients in [0, q).
 * Wire-serializable wrapper with fixed-size array. */
typedef struct lotrs_poly {
    uint64_t coeffs[LOTRS_D_MAX];
} lotrs_poly_t;

/* Polynomial wire schema (dap_serialize). */
extern const dap_serialize_schema_t lotrs_poly_schema;

/* Polynomial vector: n polynomials. */
typedef struct lotrs_polyvec {
    lotrs_poly_t **polys;
    uint32_t       n;
} lotrs_polyvec_t;

/* Polynomial matrix: rows × cols. */
typedef struct lotrs_polymat {
    lotrs_polyvec_t *rows;
    uint32_t         nrows;
    uint32_t         ncols;
} lotrs_polymat_t;

/* --- Allocation --- */
lotrs_poly_t    *lotrs_poly_alloc(const lotrs_params_t *par);
void             lotrs_poly_free(lotrs_poly_t *a_p);
lotrs_polyvec_t  lotrs_polyvec_alloc(const lotrs_params_t *par, uint32_t n);
lotrs_polymat_t  lotrs_polymat_alloc(const lotrs_params_t *par,
                                     uint32_t nrows, uint32_t ncols);
void lotrs_polyvec_free(lotrs_polyvec_t *v);
void lotrs_polymat_free(lotrs_polymat_t *m);

/* --- Ring operations (in-place, mod q) --- */
void lotrs_poly_zero(lotrs_poly_t *p, const lotrs_params_t *par);
void lotrs_poly_copy(lotrs_poly_t *dst, const lotrs_poly_t *src,
                     const lotrs_params_t *par);
void lotrs_poly_add(lotrs_poly_t *out, const lotrs_poly_t *a,
                    const lotrs_poly_t *b, const lotrs_params_t *par);
void lotrs_poly_sub(lotrs_poly_t *out, const lotrs_poly_t *a,
                    const lotrs_poly_t *b, const lotrs_params_t *par);
void lotrs_poly_neg(lotrs_poly_t *out, const lotrs_poly_t *a,
                    const lotrs_params_t *par);
void lotrs_poly_mul(lotrs_poly_t *out, const lotrs_poly_t *a,
                    const lotrs_poly_t *b, const lotrs_params_t *par);
void lotrs_poly_scalar_mul(lotrs_poly_t *out, uint64_t scalar,
                           const lotrs_poly_t *a, const lotrs_params_t *par);

/* --- Vector operations --- */
void lotrs_polyvec_zero(lotrs_polyvec_t *v, const lotrs_params_t *par);
void lotrs_polyvec_add(lotrs_polyvec_t *out, const lotrs_polyvec_t *a,
                       const lotrs_polyvec_t *b, const lotrs_params_t *par);
void lotrs_polyvec_sub(lotrs_polyvec_t *out, const lotrs_polyvec_t *a,
                       const lotrs_polyvec_t *b, const lotrs_params_t *par);
void lotrs_polymat_vecmul(lotrs_polyvec_t *out, const lotrs_polymat_t *A,
                          const lotrs_polyvec_t *x, const lotrs_params_t *par);

/* --- Centered representation --- */
int64_t lotrs_center(uint64_t a, uint64_t q);

/* --- Serialization (dap_serialize) --- */
size_t lotrs_poly_bytes(const lotrs_params_t *par);
int lotrs_poly_pack(uint8_t *out, size_t out_len,
                    const lotrs_poly_t *p, const lotrs_params_t *par);
int lotrs_poly_unpack(lotrs_poly_t *p, const uint8_t *in, size_t in_len,
                      const lotrs_params_t *par);

/* Golomb-Rice compact serialization (smaller wire size). */
int lotrs_poly_pack_rice(uint8_t *out, size_t out_cap,
                         const lotrs_poly_t *p, const lotrs_params_t *par,
                         uint32_t rice_k, int64_t bound,
                         size_t *bytes_written);
int lotrs_poly_unpack_rice(lotrs_poly_t *p,
                           const uint8_t *in, size_t in_len,
                           const lotrs_params_t *par,
                           uint32_t rice_k, int64_t bound,
                           size_t *bytes_consumed);

size_t lotrs_polyvec_bytes(const lotrs_params_t *par, uint32_t n);
int lotrs_polyvec_pack(uint8_t *out, size_t out_len,
                       const lotrs_polyvec_t *v, const lotrs_params_t *par);
int lotrs_polyvec_unpack(lotrs_polyvec_t *v, const uint8_t *in, size_t in_len,
                         const lotrs_params_t *par);

#endif /* _LOTRS_RING_H_ */
