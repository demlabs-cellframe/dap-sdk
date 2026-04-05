#include <stdint.h>
#ifdef DAP_DILITHIUM_PROFILE
#include <stdatomic.h>
#include <x86intrin.h>
#endif
#include "dilithium_sign.h"
#include "dilithium_poly.h"
#include "dap_common.h"
#include "dap_memwipe.h"

#include "dap_hash_sha3.h"
#include "dap_hash_shake128.h"
#include "dap_hash_shake256.h"
#include "dap_hash_shake_x4.h"

#define LOG_TAG "dap_crypto_sign_dilithium"

/********************************************************************************************/
void expand_mat(polyvecl mat[], const unsigned char rho[SEEDBYTES], dilithium_param_t *p)
{
  unsigned int total = p->PARAM_K * p->PARAM_L;
  unsigned int idx = 0;

  unsigned char inbuf[4][SEEDBYTES + 1];
  unsigned char outbuf[4][5 * DAP_SHAKE128_RATE];

  for (int k = 0; k < 4; k++)
      memcpy(inbuf[k], rho, SEEDBYTES);

  while (idx + 4 <= total) {
      for (int k = 0; k < 4; k++) {
          unsigned int ii = (idx + k) / p->PARAM_L;
          unsigned int jj = (idx + k) % p->PARAM_L;
          inbuf[k][SEEDBYTES] = ii + (jj << 4);
      }

      dap_keccak_x4_state_t l_state;
      dap_hash_shake128_x4_absorb(&l_state, inbuf[0], inbuf[1], inbuf[2], inbuf[3],
                                   SEEDBYTES + 1);
      dap_hash_shake128_x4_squeezeblocks(outbuf[0], outbuf[1], outbuf[2], outbuf[3],
                                          5, &l_state);

      for (int k = 0; k < 4; k++) {
          unsigned int ii = (idx + k) / p->PARAM_L;
          unsigned int jj = (idx + k) % p->PARAM_L;
          dilithium_poly_uniform(mat[ii].vec + jj, outbuf[k]);
          dilithium_poly_nttunpack((int32_t *)mat[ii].vec[jj].coeffs);
      }
      idx += 4;
  }

  while (idx < total) {
      unsigned int ii = idx / p->PARAM_L;
      unsigned int jj = idx % p->PARAM_L;
      inbuf[0][SEEDBYTES] = ii + (jj << 4);
      dap_hash_shake128(outbuf[0], sizeof(outbuf[0]), inbuf[0], SEEDBYTES + 1);
      dilithium_poly_uniform(mat[ii].vec + jj, outbuf[0]);
      dilithium_poly_nttunpack((int32_t *)mat[ii].vec[jj].coeffs);
      idx++;
  }
}

/********************************************************************************************/
void challenge(poly *c, const unsigned char mu[CRHBYTES], const polyveck *w1, dilithium_param_t *p)
{
    unsigned int i, b, pos;
    unsigned char inbuf[CRHBYTES + p->PARAM_K * p->PARAM_POLW1_SIZE_PACKED];
    unsigned char outbuf[DAP_SHAKE256_RATE];
    uint64_t state[25] = {0}, signs, mask;

    for(i = 0; i < CRHBYTES; ++i)
        inbuf[i] = mu[i];
    for(i = 0; i < p->PARAM_K; ++i)
        polyw1_pack(inbuf + CRHBYTES + i * p->PARAM_POLW1_SIZE_PACKED, w1->vec + i);

    dap_hash_shake256_absorb(state, inbuf, sizeof(inbuf));
    dap_hash_shake256_squeezeblocks(outbuf, 1, state);

    signs = 0;
    for(i = 0; i < 8; ++i)
        signs |= (uint64_t)outbuf[i] << 8*i;

    pos = 8;
    mask = 1;

    for(i = 0; i < NN; ++i)
        c->coeffs[i] = 0;

    for(i = 196; i < 256; ++i) {
        do {
            if(pos >= DAP_SHAKE256_RATE) {
                dap_hash_shake256_squeezeblocks(outbuf, 1, state);
                pos = 0;
            }

            b = outbuf[pos++];
        } while(b > i);

        c->coeffs[i] = c->coeffs[b];
        c->coeffs[b] = (signs & mask) ? Q - 1 : 1;
        mask <<= 1;
    }
}

/********************************************************************************************/
void mldsa_sample_in_ball(poly *c, const unsigned char *c_tilde, const dilithium_param_t *p)
{
    unsigned int i, b, pos;
    uint32_t tau = dil_tau(p);
    uint32_t ctilde_bytes = dil_ctildebytes(p);
    unsigned char outbuf[DAP_SHAKE256_RATE];
    uint64_t state[25] = {0}, signs, mask;

    dap_hash_shake256_absorb(state, c_tilde, ctilde_bytes);
    dap_hash_shake256_squeezeblocks(outbuf, 1, state);

    signs = 0;
    for (i = 0; i < 8; ++i)
        signs |= (uint64_t)outbuf[i] << 8 * i;

    pos = 8;
    mask = 1;

    for (i = 0; i < NN; ++i)
        c->coeffs[i] = 0;

    for (i = NN - tau; i < NN; ++i) {
        do {
            if (pos >= DAP_SHAKE256_RATE) {
                dap_hash_shake256_squeezeblocks(outbuf, 1, state);
                pos = 0;
            }
            b = outbuf[pos++];
        } while (b > i);

        c->coeffs[i] = c->coeffs[b];
        c->coeffs[b] = (signs & mask) ? Q - 1 : 1;
        mask <<= 1;
    }
}

/* FIPS 204: compute c_tilde = H(mu || w1_encode) — returns c_tilde and challenge poly */
static void mldsa_challenge_hash(unsigned char *c_tilde, poly *c,
                                  const unsigned char *mu, const polyveck *w1,
                                  const dilithium_param_t *p)
{
    uint32_t crh = dil_crhbytes(p);
    uint32_t ctilde_bytes = dil_ctildebytes(p);
    unsigned char inbuf[crh + p->PARAM_K * p->PARAM_POLW1_SIZE_PACKED];

    memcpy(inbuf, mu, crh);
    for (unsigned i = 0; i < p->PARAM_K; ++i)
        polyw1_pack_p(inbuf + crh + i * p->PARAM_POLW1_SIZE_PACKED, w1->vec + i, p);

    dap_hash_shake256(c_tilde, ctilde_bytes, inbuf, sizeof(inbuf));
    mldsa_sample_in_ball(c, c_tilde, p);
}

/********************************************************************************************/
void dilithium_private_key_delete(void *private_key)
{
    dap_return_if_pass(!private_key);
    dilithium_private_key_t *l_skey = (dilithium_private_key_t *)private_key;
    if (l_skey->data) {
        dilithium_param_t p;
        if (dilithium_params_init(&p, l_skey->kind))
            dap_memwipe(l_skey->data, p.CRYPTO_SECRETKEYBYTES);
    }
    DAP_DEL_MULTY(l_skey->data, private_key);
}

void dilithium_public_key_delete(void *public_key)
{
    dap_return_if_pass(!public_key);
    DAP_DEL_MULTY(((dilithium_public_key_t *)public_key)->data, public_key);
}

void dilithium_private_and_public_keys_delete(void *a_skey, void *a_pkey)
{
    if (a_skey)
        dilithium_private_key_delete(a_skey);
    if (a_pkey)
        dilithium_public_key_delete(a_pkey);
}

/********************************************************************************************/

static int32_t dilithium_private_and_public_keys_init(dilithium_private_key_t *private_key, dilithium_public_key_t *public_key, dilithium_param_t *p){

    if (p == NULL)
        return -1;

    unsigned char *f = NULL, *g = NULL;

    f = calloc(p->CRYPTO_PUBLICKEYBYTES, sizeof(unsigned char));
    if (f == NULL) {
        return -1;
    }
    public_key->kind = p->kind;
    public_key->data = f;

    g = calloc(p->CRYPTO_SECRETKEYBYTES, sizeof(unsigned char));
    if (g == NULL) {
        free(f);
        return -1;
    }

    private_key->kind = p->kind;
    private_key->data = g;

    return 0;
}

/*************************************************/
int dilithium_crypto_sign_keypair(dilithium_public_key_t *public_key, dilithium_private_key_t *private_key,
        dilithium_kind_t kind, const void * seed, size_t seed_size)
{

    dilithium_param_t l_p_buf;
    dilithium_param_t *p = &l_p_buf;

    if (!dilithium_params_init(p, kind))
        return -1;

    assert(private_key != NULL);

    if(dilithium_private_and_public_keys_init( private_key, public_key, p) != 0)
        return -1;

    unsigned int i;
    unsigned char seedbuf[3*SEEDBYTES];
    uint32_t crh = dil_crhbytes(p);
    unsigned char tr[crh];
    unsigned char *rho, *rhoprime, *key;
    uint16_t nonce = 0;
    polyvecl mat[p->PARAM_K];
    polyvecl s1, s1hat;
    polyveck s2, t, t1, t0;

    if(seed && seed_size > 0) {
        assert(SEEDBYTES==32);
        dap_hash_sha3_256_raw((unsigned char *) seedbuf, (const unsigned char *) seed, seed_size);
    }
    else {
        dap_random_bytes(seedbuf, SEEDBYTES);
    }

    dap_hash_shake256(seedbuf, 3*SEEDBYTES, seedbuf, SEEDBYTES);
    rho = seedbuf;
    rhoprime = rho + SEEDBYTES;
    key = rho + 2*SEEDBYTES;

    expand_mat(mat, rho, p);

    for(i = 0; i + 4 <= p->PARAM_L; i += 4) {
        poly_uniform_eta_x4(&s1.vec[i], &s1.vec[i+1], &s1.vec[i+2], &s1.vec[i+3],
                             rhoprime, nonce, nonce+1, nonce+2, nonce+3, p);
        nonce += 4;
    }
    for(; i < p->PARAM_L; ++i)
        poly_uniform_eta(s1.vec + i, rhoprime, nonce++, p);

    for(i = 0; i + 4 <= p->PARAM_K; i += 4) {
        poly_uniform_eta_x4(&s2.vec[i], &s2.vec[i+1], &s2.vec[i+2], &s2.vec[i+3],
                             rhoprime, nonce, nonce+1, nonce+2, nonce+3, p);
        nonce += 4;
    }
    for(; i < p->PARAM_K; ++i)
        poly_uniform_eta(s2.vec + i, rhoprime, nonce++, p);

    s1hat = s1;
    polyvecl_ntt(&s1hat, p);
    for(i = 0; i < p->PARAM_K; ++i) {
        polyvecl_pointwise_acc_invmontgomery(t.vec+i, mat+i, &s1hat, p);
        poly_reduce(t.vec+i);
        poly_invntt_montgomery(t.vec+i);
    }

    polyveck_add(&t, &t, &s2, p);
    polyveck_freeze(&t, p);

    if (p->is_fips204) {
        polyveck_power2round_p(&t1, &t0, &t, p);
    } else {
        polyveck_power2round(&t1, &t0, &t, p);
    }

    dilithium_pack_pk(public_key->data, rho, &t1, p);

    dap_hash_shake256(tr, crh, public_key->data, p->CRYPTO_PUBLICKEYBYTES);

    if (p->is_fips204) {
        mldsa_pack_sk(private_key->data, rho, key, tr, &s1, &s2, &t0, p);
    } else {
        dilithium_pack_sk(private_key->data, rho, key, tr, &s1, &s2, &t0, p);
    }

    return 0;
}

/*************************************************/
int dilithium_crypto_sign( dilithium_signature_t *sig, const unsigned char *m, unsigned long long mlen, const dilithium_private_key_t *private_key)
{
    dilithium_param_t l_p_buf;
    dilithium_param_t *p = &l_p_buf;
    if (!dilithium_params_init(p, private_key->kind))
        return 1;

    const int fips = p->is_fips204;
    const uint32_t crh = dil_crhbytes(p);
    const uint32_t gamma1 = dil_gamma1(p);
    const uint32_t gamma2 = dil_gamma2(p);

    unsigned long long i, j;
    unsigned int n;
    byte_t seedbuf[2*SEEDBYTES + 64]={0};
    byte_t tr[64]={0};
    unsigned char *rho, *key, *mu;
    uint16_t nonce = 0;
    poly c, chat;
    polyvecl mat[p->PARAM_K], s1, y, yhat, z;
    polyveck s2, t0, w, w1;
    polyveck h, wcs2, wcs20, ct0, tmp;
    unsigned char c_tilde[64];

    rho = seedbuf;
    key = seedbuf + SEEDBYTES;
    mu = seedbuf + 2*SEEDBYTES;

    if (fips) {
        mldsa_unpack_sk(rho, key, tr, &s1, &s2, &t0, private_key->data, p);
    } else {
        dilithium_unpack_sk(rho, key, tr, &s1, &s2, &t0, private_key->data, p);
    }

    sig->sig_len = mlen + p->CRYPTO_BYTES;
    sig->sig_data = DAP_NEW_Z_SIZE(unsigned char, sig->sig_len);

    memcpy(sig->sig_data + p->CRYPTO_BYTES, m, mlen);
    memcpy(sig->sig_data + p->CRYPTO_BYTES - crh, tr, crh);

    dap_hash_shake256(mu, crh, sig->sig_data + p->CRYPTO_BYTES - crh, crh + mlen);

    expand_mat(mat, rho, p);
    polyvecl_ntt(&s1, p);
    polyveck_ntt(&s2, p);
    polyveck_ntt(&t0, p);

    while(1){
        if (fips) {
            for (i = 0; i < p->PARAM_L; ++i)
                poly_uniform_gamma1m1_p(y.vec + i, key, nonce++, p);
        } else {
            for(i = 0; i + 4 <= p->PARAM_L; i += 4) {
                poly_uniform_gamma1m1_x4(&y.vec[i], &y.vec[i+1], &y.vec[i+2], &y.vec[i+3],
                                          key, nonce, nonce+1, nonce+2, nonce+3);
                nonce += 4;
            }
            for(; i < p->PARAM_L; ++i)
                poly_uniform_gamma1m1(y.vec+i, key, nonce++);
        }

        yhat = y;
        polyvecl_ntt(&yhat, p);
        for(i = 0; i < p->PARAM_K; ++i) {
            polyvecl_pointwise_acc_invmontgomery(w.vec+i, mat + i, &yhat, p);
            poly_reduce(w.vec + i);
            poly_invntt_montgomery(w.vec + i);
        }

        polyveck_csubq(&w, p);
        if (fips) {
            polyveck_decompose_p(&w1, &tmp, &w, p);
            mldsa_challenge_hash(c_tilde, &c, mu, &w1, p);
        } else {
            polyveck_decompose(&w1, &tmp, &w, p);
            challenge(&c, mu, &w1, p);
        }

        /* DEBUG removed */

        chat = c;
        dilithium_poly_ntt(&chat);
        for(i = 0; i < p->PARAM_L; ++i) {
            poly_pointwise_invmontgomery(z.vec + i, &chat, s1.vec + i);
            poly_invntt_montgomery(z.vec + i);
        }
        polyvecl_add(&z, &z, &y, p);
        polyvecl_freeze(&z, p);
        if(!polyvecl_chknorm(&z, gamma1 - p->PARAM_BETA, p)){

            for(i = 0; i < p->PARAM_K; ++i) {
                poly_pointwise_invmontgomery(wcs2.vec + i, &chat, s2.vec + i);
                poly_invntt_montgomery(wcs2.vec + i);
            }
            polyveck_sub(&wcs2, &w, &wcs2, p);
            polyveck_freeze(&wcs2, p);
            if (fips) {
                polyveck_decompose_p(&tmp, &wcs20, &wcs2, p);
            } else {
                polyveck_decompose(&tmp, &wcs20, &wcs2, p);
            }
            polyveck_csubq(&wcs20, p);
            if(!polyveck_chknorm(&wcs20, gamma2 - p->PARAM_BETA, p)){

                unsigned int S = 0;
                for(i = 0; i < p->PARAM_K; ++i)
                    for(j = 0; j < NN; ++j)
                        if(tmp.vec[i].coeffs[j] == w1.vec[i].coeffs[j])
                            S++;
                if(S == p->PARAM_K * NN){

                    for(i = 0; i < p->PARAM_K; ++i) {
                        poly_pointwise_invmontgomery(ct0.vec + i, &chat, t0.vec + i);
                        poly_invntt_montgomery(ct0.vec + i);
                    }

                    polyveck_csubq(&ct0, p);
                    if(!polyveck_chknorm(&ct0, gamma2, p)){

                        polyveck_add(&tmp, &wcs2, &ct0, p);
                        polyveck_csubq(&tmp, p);
                        if (fips) {
                            n = polyveck_make_hint_p(&h, &wcs2, &tmp, p);
                        } else {
                            n = polyveck_make_hint(&h, &wcs2, &tmp, p);
                        }
                        if(n <= p->PARAM_OMEGA){

                            if (fips) {
                                mldsa_pack_sig(sig->sig_data, c_tilde, &z, &h, p);
                            } else {
                                dilithium_pack_sig(sig->sig_data, &z, &h, &c, p);
                            }

                            sig->kind = p->kind;

                            break;
                        }
                    }
                }
            }
        }
    }

    return 0;
}
#include "dap_hash.h"
/*************************************************/
int dilithium_crypto_sign_open( unsigned char *m, unsigned long long mlen, dilithium_signature_t *sig, const dilithium_public_key_t * public_key)
{
    if(public_key->kind != sig->kind) {
        log_it(L_ERROR, "Verify failed: kind mismatch pk=%d sig=%d", public_key->kind, sig->kind);
        return -1;
    }

    dilithium_param_t l_p_buf;
    dilithium_param_t *p = &l_p_buf;
    if (!dilithium_params_init(p, public_key->kind)) {
        log_it(L_ERROR, "Verify failed: params_init failed kind=%d", public_key->kind);
        return -2;
    }

    const int fips = p->is_fips204;
    const uint32_t crh = dil_crhbytes(p);
    const uint32_t gamma1 = dil_gamma1(p);
    const uint32_t d_val = dil_d(p);

    if (sig->sig_len < p->CRYPTO_BYTES) {
        log_it(L_ERROR, "Verify failed: sig_len=%zu < CRYPTO_BYTES=%u", sig->sig_len, p->CRYPTO_BYTES);
        return -3;
    }

    unsigned long long i;
    unsigned char rho[SEEDBYTES];
    unsigned char mu[crh];
    poly c, chat, cp;
    polyvecl mat[p->PARAM_K], z;
    polyveck t1, w1, h, tmp1, tmp2;
    unsigned char c_tilde[64];

    if((sig->sig_len - p->CRYPTO_BYTES) != mlen) {
        log_it(L_ERROR, "Verify failed: length mismatch sig_len=%zu CRYPTO_BYTES=%u mlen=%llu",
               sig->sig_len, p->CRYPTO_BYTES, mlen);
        return -4;
    }

#ifdef DAP_DILITHIUM_PROFILE
    static _Atomic(uint64_t) s_prof_unpack, s_prof_mu, s_prof_expand, s_prof_ntt,
        s_prof_hint, s_prof_chal, s_prof_total, s_prof_cnt;
    uint64_t _pt0 = __rdtsc(), _pt1;
#endif

    dilithium_unpack_pk(rho, &t1, public_key->data, p);

    if (fips) {
        if (mldsa_unpack_sig(c_tilde, &z, &h, sig->sig_data, p)) {
            log_it(L_ERROR, "Verify failed: mldsa_unpack_sig failed");
            return -5;
        }
        mldsa_sample_in_ball(&c, c_tilde, p);
    } else {
        if (dilithium_unpack_sig(&z, &h, &c, sig->sig_data, p)) {
            log_it(L_ERROR, "Verify failed: unpack_sig failed");
            return -5;
        }
    }

    if(polyvecl_chknorm(&z, gamma1 - p->PARAM_BETA, p)) {
        log_it(L_ERROR, "Verify failed: z norm check failed");
        return -6;
    }

#ifdef DAP_DILITHIUM_PROFILE
    _pt1 = __rdtsc(); s_prof_unpack += _pt1 - _pt0; _pt0 = _pt1;
#endif

    unsigned char tmp_m[crh + mlen];
    if(sig->sig_data != m)
        for(i = 0; i < mlen; ++i)
            tmp_m[crh + i] = m[i];

    dap_hash_shake256(tmp_m, crh, public_key->data, p->CRYPTO_PUBLICKEYBYTES);
    dap_hash_shake256(mu, crh, tmp_m, crh + mlen);

#ifdef DAP_DILITHIUM_PROFILE
    _pt1 = __rdtsc(); s_prof_mu += _pt1 - _pt0; _pt0 = _pt1;
#endif

    expand_mat(mat, rho, p);

#ifdef DAP_DILITHIUM_PROFILE
    _pt1 = __rdtsc(); s_prof_expand += _pt1 - _pt0; _pt0 = _pt1;
#endif

    polyvecl_ntt(&z, p);
    for(i = 0; i < p->PARAM_K ; ++i)
        polyvecl_pointwise_acc_invmontgomery(tmp1.vec + i, mat+i, &z, p);

    chat = c;
    dilithium_poly_ntt(&chat);
    polyveck_shiftl(&t1, d_val, p);
    polyveck_ntt(&t1, p);
    for(i = 0; i < p->PARAM_K; ++i)
        poly_pointwise_invmontgomery(tmp2.vec + i, &chat, t1.vec + i);

    polyveck_sub(&tmp1, &tmp1, &tmp2, p);
    polyveck_reduce(&tmp1, p);
    polyveck_invntt_montgomery(&tmp1, p);

#ifdef DAP_DILITHIUM_PROFILE
    _pt1 = __rdtsc(); s_prof_ntt += _pt1 - _pt0; _pt0 = _pt1;
#endif

    polyveck_csubq(&tmp1, p);
    if (fips) {
        polyveck_use_hint_p(&w1, &tmp1, &h, p);
    } else {
        polyveck_use_hint(&w1, &tmp1, &h, p);
    }

    /* DEBUG removed */

#ifdef DAP_DILITHIUM_PROFILE
    _pt1 = __rdtsc(); s_prof_hint += _pt1 - _pt0; _pt0 = _pt1;
#endif

    if (fips) {
        uint32_t ctilde_bytes = dil_ctildebytes(p);
        unsigned char inbuf[crh + p->PARAM_K * p->PARAM_POLW1_SIZE_PACKED];
        unsigned char c_tilde_check[64];

        memcpy(inbuf, mu, crh);
        for (i = 0; i < p->PARAM_K; ++i)
            polyw1_pack_p(inbuf + crh + i * p->PARAM_POLW1_SIZE_PACKED, w1.vec + i, p);

        dap_hash_shake256(c_tilde_check, ctilde_bytes, inbuf, sizeof(inbuf));

        if (memcmp(c_tilde, c_tilde_check, ctilde_bytes) != 0) {
            log_it(L_ERROR, "Verify failed: c_tilde mismatch (K=%u L=%u)", p->PARAM_K, p->PARAM_L);
                return -7;
            }
    } else {
        challenge(&cp, mu, &w1, p);
        for(i = 0; i < NN; ++i)
            if(c.coeffs[i] != cp.coeffs[i]) {
                log_it(L_ERROR, "Verify failed: challenge mismatch at i=%llu c=%d cp=%d (K=%u L=%u)",
                       i, c.coeffs[i], cp.coeffs[i], p->PARAM_K, p->PARAM_L);
                return -7;
            }
    }

#ifdef DAP_DILITHIUM_PROFILE
    _pt1 = __rdtsc(); s_prof_chal += _pt1 - _pt0;
    uint64_t cnt = ++s_prof_cnt;
    if (cnt == 500) {
        fprintf(stderr, "\n[PROF] ML-DSA verify (K=%u L=%u, %lu calls):\n"
                "  unpack+sib : %7lu cyc (%5.2f us)\n"
                "  mu(shake)  : %7lu cyc (%5.2f us)\n"
                "  expand_mat : %7lu cyc (%5.2f us)\n"
                "  ntt+mul    : %7lu cyc (%5.2f us)\n"
                "  hint       : %7lu cyc (%5.2f us)\n"
                "  chal+pack  : %7lu cyc (%5.2f us)\n",
                p->PARAM_K, p->PARAM_L, (unsigned long)cnt,
                (unsigned long)(s_prof_unpack/cnt), (double)s_prof_unpack/cnt/4180.0,
                (unsigned long)(s_prof_mu/cnt), (double)s_prof_mu/cnt/4180.0,
                (unsigned long)(s_prof_expand/cnt), (double)s_prof_expand/cnt/4180.0,
                (unsigned long)(s_prof_ntt/cnt), (double)s_prof_ntt/cnt/4180.0,
                (unsigned long)(s_prof_hint/cnt), (double)s_prof_hint/cnt/4180.0,
                (unsigned long)(s_prof_chal/cnt), (double)s_prof_chal/cnt/4180.0);
        s_prof_unpack = s_prof_mu = s_prof_expand = s_prof_ntt =
            s_prof_hint = s_prof_chal = s_prof_total = s_prof_cnt = 0;
    }
#endif
    return 0;
}

/*************************************************/
void dilithium_signature_delete(void *sig){
    dap_return_if_pass(!sig);
    DAP_DEL_Z(((dilithium_signature_t *)sig)->sig_data);
}
