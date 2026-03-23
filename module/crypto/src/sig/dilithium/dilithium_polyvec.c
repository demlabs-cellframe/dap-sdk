#include <stdint.h>

#include "dilithium_poly.h"
#include "dilithium_polyvec.h"
#include "dap_cpu_arch.h"
#include "dap_cpu_detect.h"

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

__attribute__((target("avx512f,avx512vl")))
static void s_polyvecl_pw_acc_avx512(
    int32_t *w, const polyvecl *u, const polyvecl *v, unsigned count)
{
    const __m512i l_qinv = _mm512_set1_epi32((int32_t)DIL_QINV);
    const __m512i l_q    = _mm512_set1_epi32(DIL_Q);

    for (unsigned blk = 0; blk < 256; blk += 16) {
        __m512i acc = _mm512_setzero_si512();
        for (unsigned k = 0; k < count; k++) {
            __m512i a = _mm512_loadu_si512((const __m512i *)((const int32_t *)u->vec[k].coeffs + blk));
            __m512i b = _mm512_loadu_si512((const __m512i *)((const int32_t *)v->vec[k].coeffs + blk));
            __m512i ab_lo = _mm512_mullo_epi32(a, b);
            __m512i u_val = _mm512_mullo_epi32(ab_lo, l_qinv);
            __m512i ab_ev = _mm512_mul_epi32(a, b);
            __m512i uq_ev = _mm512_mul_epu32(u_val, l_q);
            __m512i s_ev  = _mm512_srli_epi64(_mm512_add_epi64(ab_ev, uq_ev), 32);
            __m512i a_od  = _mm512_srli_epi64(a, 32);
            __m512i b_od  = _mm512_srli_epi64(b, 32);
            __m512i u_od  = _mm512_srli_epi64(u_val, 32);
            __m512i ab_od = _mm512_mul_epi32(a_od, b_od);
            __m512i uq_od = _mm512_mul_epu32(u_od, l_q);
            __m512i s_od  = _mm512_add_epi64(ab_od, uq_od);
            __m512i pw = _mm512_mask_blend_epi32((__mmask16)0xAAAA, s_ev, s_od);
            acc = _mm512_add_epi32(acc, pw);
        }
        _mm512_storeu_si512((__m512i *)(w + blk), acc);
    }
}
#endif

void polyvecl_pointwise_acc_invmontgomery(poly *w, const polyvecl *u, const polyvecl *v, dilithium_param_t *p)
{
#if DAP_CPU_DETECT_X86
    if (dap_cpu_arch_get() >= DAP_CPU_ARCH_AVX512) {
        s_polyvecl_pw_acc_avx512((int32_t *)w->coeffs, u, v, p->PARAM_L);
        return;
    }
#endif
  unsigned int i;
  poly t;

  poly_pointwise_invmontgomery(w, u->vec+0, v->vec+0);

  for(i = 1; i < p->PARAM_L; ++i) {
    poly_pointwise_invmontgomery(&t, u->vec+i, v->vec+i);
    dilithium_poly_add(w, w, &t);
  }
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
