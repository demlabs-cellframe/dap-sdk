#include <stdint.h>

#include "dilithium_poly.h"
#include "dilithium_polyvec.h"
#include "dap_cpu_arch.h"
#include "dap_cpu_detect.h"
#include "dap_arch_dispatch.h"

#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#endif

/*************************************************/
void polyvecl_freeze(polyvecl *v, dilithium_param_t *p) {
  unsigned int i;

  for(i = 0; i < p->PARAM_L; ++i)
    poly_freeze(v->vec + i);
}

/*************************************************/
void polyvecl_add(polyvecl *w, const polyvecl *u, const polyvecl *v, dilithium_param_t *p) {
  unsigned int i;

  for(i = 0; i < p->PARAM_L; ++i)
    dilithium_poly_add(w->vec+i, u->vec+i, v->vec+i);
}

/*************************************************/
void polyvecl_ntt(polyvecl *v, dilithium_param_t *p) {
  unsigned int i;

  for(i = 0; i < p->PARAM_L; ++i)
    dilithium_poly_ntt(v->vec+i);
}

/*************************************************/

#if defined(__x86_64__) || defined(_M_X64)
#define DIL_Q    8380417
#define DIL_QINV 4236238847U

/*
 * Deferred Montgomery: accumulate all L products in 64-bit,
 * perform ONE Montgomery reduction at the end.
 * pqcrystals approach: 4 insn/product + 10 insn/reduction vs
 * our old approach: 16 insn/product (Montgomery per multiply).
 *
 * Math: max accumulator = L * (Q-1)^2 = 7 * 7.02e13 ~ 4.9e14 << 2^63.
 */
__attribute__((target("avx512f,avx512vl")))
static void s_polyvecl_pw_acc_avx512(
    int32_t *w, const polyvecl *u, const polyvecl *v, unsigned count)
{
    const __m512i l_qinv = _mm512_set1_epi64((int64_t)(int32_t)DIL_QINV);
    const __m512i l_q    = _mm512_set1_epi64((int64_t)DIL_Q);

    for (unsigned blk = 0; blk < 256; blk += 16) {
        __m512i acc_ev = _mm512_setzero_si512();
        __m512i acc_od = _mm512_setzero_si512();

        for (unsigned k = 0; k < count; k++) {
            __m512i a = _mm512_loadu_si512(
                (const __m512i *)((const int32_t *)u->vec[k].coeffs + blk));
            __m512i b = _mm512_loadu_si512(
                (const __m512i *)((const int32_t *)v->vec[k].coeffs + blk));

            acc_ev = _mm512_add_epi64(acc_ev, _mm512_mul_epi32(a, b));

            __m512i a_od = _mm512_srli_epi64(a, 32);
            __m512i b_od = _mm512_srli_epi64(b, 32);
            acc_od = _mm512_add_epi64(acc_od, _mm512_mul_epi32(a_od, b_od));
        }

        __m512i u_ev = _mm512_mul_epi32(acc_ev, l_qinv);
        __m512i uq_ev = _mm512_mul_epu32(u_ev, l_q);
        __m512i r_ev = _mm512_srli_epi64(_mm512_add_epi64(acc_ev, uq_ev), 32);

        __m512i u_od = _mm512_mul_epi32(acc_od, l_qinv);
        __m512i uq_od = _mm512_mul_epu32(u_od, l_q);
        __m512i r_od = _mm512_add_epi64(acc_od, uq_od);

        __m512i result = _mm512_mask_blend_epi32((__mmask16)0xAAAA, r_ev, r_od);
        _mm512_storeu_si512((__m512i *)(w + blk), result);
    }
}

__attribute__((target("avx2")))
static void s_polyvecl_pw_acc_avx2(
    int32_t *w, const polyvecl *u, const polyvecl *v, unsigned count)
{
    const __m256i l_qinv = _mm256_set1_epi64x((int64_t)(int32_t)DIL_QINV);
    const __m256i l_q    = _mm256_set1_epi64x((int64_t)DIL_Q);

    for (unsigned blk = 0; blk < 256; blk += 8) {
        __m256i acc_ev = _mm256_setzero_si256();
        __m256i acc_od = _mm256_setzero_si256();

        for (unsigned k = 0; k < count; k++) {
            __m256i a = _mm256_loadu_si256(
                (const __m256i *)((const int32_t *)u->vec[k].coeffs + blk));
            __m256i b = _mm256_loadu_si256(
                (const __m256i *)((const int32_t *)v->vec[k].coeffs + blk));

            acc_ev = _mm256_add_epi64(acc_ev, _mm256_mul_epi32(a, b));

            __m256i a_od = _mm256_srli_epi64(a, 32);
            __m256i b_od = _mm256_srli_epi64(b, 32);
            acc_od = _mm256_add_epi64(acc_od, _mm256_mul_epi32(a_od, b_od));
        }

        __m256i u_ev = _mm256_mul_epi32(acc_ev, l_qinv);
        __m256i uq_ev = _mm256_mul_epu32(u_ev, l_q);
        __m256i r_ev = _mm256_srli_epi64(_mm256_add_epi64(acc_ev, uq_ev), 32);

        __m256i u_od = _mm256_mul_epi32(acc_od, l_qinv);
        __m256i uq_od = _mm256_mul_epu32(u_od, l_q);
        __m256i r_od = _mm256_add_epi64(acc_od, uq_od);

        __m256i result = _mm256_blend_epi32(r_ev, r_od, 0xAA);
        _mm256_storeu_si256((__m256i *)(w + blk), result);
    }
}
#endif

DAP_DISPATCH_LOCAL(s_pw_acc, void,
    int32_t *, const polyvecl *, const polyvecl *, unsigned);

static void s_polyvecl_pw_acc_ref(
    int32_t *w, const polyvecl *u, const polyvecl *v, unsigned count)
{
    poly *l_w = (poly *)w;
    poly l_t;
    poly_pointwise_invmontgomery(l_w, u->vec + 0, v->vec + 0);
    for (unsigned i = 1; i < count; i++) {
        poly_pointwise_invmontgomery(&l_t, u->vec + i, v->vec + i);
        dilithium_poly_add(l_w, l_w, &l_t);
    }
}

static void s_pw_acc_dispatch_init(void)
{
    dap_algo_class_t l_pw_acc32 = dap_algo_class_register("PW_ACC32");

    DAP_DISPATCH_DEFAULT(s_pw_acc, s_polyvecl_pw_acc_ref);

    DAP_DISPATCH_ARCH_SELECT_FOR(l_pw_acc32);

    DAP_DISPATCH_X86(DAP_CPU_ARCH_AVX2,   s_pw_acc, s_polyvecl_pw_acc_avx2);
    DAP_DISPATCH_X86(DAP_CPU_ARCH_AVX512, s_pw_acc, s_polyvecl_pw_acc_avx512);
}

void polyvecl_pointwise_acc_invmontgomery(poly *w, const polyvecl *u, const polyvecl *v, dilithium_param_t *p)
{
    DAP_DISPATCH_ENSURE(s_pw_acc, s_pw_acc_dispatch_init);
    s_pw_acc_ptr((int32_t *)w->coeffs, u, v, p->PARAM_L);
}

/*************************************************/
int polyvecl_chknorm(const polyvecl *v, uint32_t bound, dilithium_param_t *p)  {
  unsigned int i;
  int ret = 0;

  for(i = 0; i < p->PARAM_L; ++i)
    ret |= poly_chknorm(v->vec+i, bound);

  return ret;
}

/*************************************************/
void polyveck_reduce(polyveck *v, dilithium_param_t *p) {
  unsigned int i;

  for(i = 0; i < p->PARAM_K; ++i)
    poly_reduce(v->vec+i);
}

/*************************************************/
void polyveck_csubq(polyveck *v, dilithium_param_t *p) {
  unsigned int i;

  for(i = 0; i < p->PARAM_K; ++i)
    poly_csubq(v->vec+i);
}

/*************************************************/
void polyveck_freeze(polyveck *v, dilithium_param_t *p)  {
  unsigned int i;

  for(i = 0; i < p->PARAM_K; ++i)
    poly_freeze(v->vec+i);
}

/*************************************************/
void polyveck_add(polyveck *w, const polyveck *u, const polyveck *v, dilithium_param_t *p) {
  unsigned int i;

  for(i = 0; i < p->PARAM_K; ++i)
    dilithium_poly_add(w->vec+i, u->vec+i, v->vec+i);
}

/*************************************************/
void polyveck_sub(polyveck *w, const polyveck *u, const polyveck *v, dilithium_param_t *p) {
  unsigned int i;

  for(i = 0; i < p->PARAM_K; ++i)
    dilithium_poly_sub(w->vec+i, u->vec+i, v->vec+i);
}

/*************************************************/
void polyveck_shiftl(polyveck *v, unsigned int k, dilithium_param_t *p) {
  unsigned int i;

  for(i = 0; i < p->PARAM_K; ++i)
    poly_shiftl(v->vec + i, k);
}

/*************************************************/
void polyveck_ntt(polyveck *v, dilithium_param_t *p) {
  unsigned int i;

  for(i = 0; i < p->PARAM_K; ++i)
    dilithium_poly_ntt(v->vec + i);
}

/*************************************************/
void polyveck_invntt_montgomery(polyveck *v, dilithium_param_t *p) {
  unsigned int i;

  for(i = 0; i < p->PARAM_K; ++i)
    poly_invntt_montgomery(v->vec + i);
}

/*************************************************/
int polyveck_chknorm(const polyveck *v, uint32_t bound, dilithium_param_t *p) {
  unsigned int i;
  int ret = 0;

  for(i = 0; i < p->PARAM_K; ++i)
    ret |= poly_chknorm(v->vec+i, bound);

  return ret;
}

/*************************************************/
void polyveck_power2round(polyveck *v1, polyveck *v0, const polyveck *v, dilithium_param_t *p) {
  unsigned int i;

  for(i = 0; i < p->PARAM_K; ++i)
    poly_power2round(v1->vec+i, v0->vec+i, v->vec+i);
}

/*************************************************/
void polyveck_decompose(polyveck *v1, polyveck *v0, const polyveck *v, dilithium_param_t *p) {
  unsigned int i;

  for(i = 0; i < p->PARAM_K; ++i)
    poly_decompose(v1->vec+i, v0->vec+i, v->vec+i);
}

/*************************************************/
unsigned int polyveck_make_hint(polyveck *h, const polyveck *u, const polyveck *v, dilithium_param_t *p)
{
  unsigned int i, s = 0;

  for(i = 0; i < p->PARAM_K; ++i)
    s += poly_make_hint(h->vec+i, u->vec+i, v->vec+i);

  return s;
}

/*************************************************/
void polyveck_use_hint(polyveck *w, const polyveck *u, const polyveck *h, dilithium_param_t *p) {
  unsigned int i;

  for(i = 0; i < p->PARAM_K; ++i)
    poly_use_hint(w->vec+i, u->vec+i, h->vec+i);
}
