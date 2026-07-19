/*
 * test_ntt_ring_hom.c — Verify NTT is a ring homomorphism.
 *
 * For a correct negacyclic NTT over Z_q[X]/(X^N+1):
 *   ntt(a * b) = ntt(a) ⊙ ntt(b)   (pointwise)
 *
 * This is the defining property that makes NTT-based polynomial
 * multiplication correct. If it fails, pointwise multiply in NTT
 * domain does NOT correspond to polynomial multiplication.
 */
#include <dap_common.h>
#include <dap_enc.h>
#include <dap_test.h>
#include <stdint.h>
#include <string.h>
#include "chipmunk/chipmunk_ntt.h"
#include "chipmunk/chipmunk_poly.h"
#include "chipmunk/chipmunk.h"

#define LOG_TAG "test_ntt_ring_hom"
#define N  CHIPMUNK_N
#define Q  ((uint64_t)CHIPMUNK_Q)

/* Time-domain negacyclic convolution — ground truth. */
static void negacyclic_conv(int32_t *out, const int32_t *a, const int32_t *b)
{
    memset(out, 0, N * sizeof(int32_t));
    for (int k = 0; k < N; k++) {
        for (int m = 0; m < N; m++) {
            int sum = k + m;
            int idx = sum & (N - 1);
            int64_t prod = (int64_t)a[k] * (int64_t)b[m];
            if (sum >= N) prod = -prod;  /* X^N = -1 */
            int64_t val = (int64_t)out[idx] + prod;
            out[idx] = (int32_t)(val % (int64_t)Q);
            if (out[idx] < 0) out[idx] += (int32_t)Q;
        }
    }
}

static int32_t norm_mod(int32_t v)
{
    v %= (int32_t)Q;
    if (v < 0) v += (int32_t)Q;
    return v;
}

/* Test: ntt(a*b) == ntt(a) ⊙ ntt(b) for specific polynomials */
static bool test_ring_hom_X_sq(void)
{
    /* X * X = X^2 in Z_q[X]/(X^N+1) */
    chipmunk_poly_t a = {0}, b = {0};
    a.coeffs[1] = 1;
    b.coeffs[1] = 1;

    chipmunk_poly_t na = a, nb = b, prod;
    chipmunk_ntt(na.coeffs);
    chipmunk_ntt(nb.coeffs);
    chipmunk_poly_mul_ntt_q(&prod, &na, &nb, Q);
    chipmunk_invntt(prod.coeffs);

    /* Expected: coeff[2] = 1, rest = 0 */
    bool ok = true;
    for (int i = 0; i < N; i++) {
        int32_t exp = (i == 2) ? 1 : 0;
        if (norm_mod(prod.coeffs[i]) != exp) {
            log_it(L_ERROR, "FAIL X*X coeff[%d]: got %d, expected %d",
                   i, prod.coeffs[i], exp);
            ok = false;
            break;
        }
    }
    dap_assert(ok, "X*X = X^2 (negacyclic multiply)");
    return ok;
}

static bool test_ring_hom_X256_sq(void)
{
    /* X^256 * X^256 = X^512 = -1 in Z_q[X]/(X^512+1) */
    chipmunk_poly_t a = {0}, b = {0};
    a.coeffs[256] = 1;
    b.coeffs[256] = 1;

    chipmunk_poly_t na = a, nb = b, prod;
    chipmunk_ntt(na.coeffs);
    chipmunk_ntt(nb.coeffs);
    chipmunk_poly_mul_ntt_q(&prod, &na, &nb, Q);
    chipmunk_invntt(prod.coeffs);

    int32_t neg1 = (int32_t)Q - 1;
    bool ok = true;
    for (int i = 0; i < N; i++) {
        int32_t exp = (i == 0) ? neg1 : 0;
        if (norm_mod(prod.coeffs[i]) != exp) {
            log_it(L_ERROR, "FAIL X^256*X^256 coeff[%d]: got %d, expected %d",
                   i, prod.coeffs[i], exp);
            ok = false;
            break;
        }
    }
    dap_assert(ok, "X^256*X^256 = -1 (negacyclic X^N = -1)");
    return ok;
}

static bool test_ring_hom_general(void)
{
    /* (3 + 5X + 7X^2) * (2 + 4X) in Z_q[X]/(X^N+1)
     * = 6 + (12+10)X + (14+20)X^2 + 28X^3
     * = 6 + 22X + 34X^2 + 28X^3 */
    chipmunk_poly_t a = {0}, b = {0};
    a.coeffs[0] = 3; a.coeffs[1] = 5; a.coeffs[2] = 7;
    b.coeffs[0] = 2; b.coeffs[1] = 4;

    /* Time-domain ground truth */
    int32_t td[N];
    negacyclic_conv(td, a.coeffs, b.coeffs);

    /* NTT-domain multiply */
    chipmunk_poly_t na = a, nb = b, prod;
    chipmunk_ntt(na.coeffs);
    chipmunk_ntt(nb.coeffs);
    chipmunk_poly_mul_ntt_q(&prod, &na, &nb, Q);
    chipmunk_invntt(prod.coeffs);

    int mismatch = 0;
    for (int i = 0; i < N; i++) {
        int32_t ntt_val = norm_mod(prod.coeffs[i]);
        if (ntt_val != td[i]) {
            if (mismatch < 5)
                log_it(L_DEBUG, "  mismatch[%d]: ntt=%d td=%d", i, ntt_val, td[i]);
            mismatch++;
        }
    }
    dap_assert(mismatch == 0, "(3+5X+7X^2)*(2+4X) negacyclic conv");
    return mismatch == 0;
}

int main(void)
{
    dap_set_appname("test_ntt_ring_hom");
    dap_common_init("test_ntt_ring_hom", NULL);
    dap_enc_init();

    int rc = 0;
    if (!test_ring_hom_X_sq())      rc = 1;
    if (!test_ring_hom_X256_sq())   rc = 1;
    if (!test_ring_hom_general())   rc = 1;

    if (rc == 0)
        log_it(L_INFO, "=== ALL NTT ring homomorphism tests PASSED ===");
    else
        log_it(L_ERROR, "=== NTT ring homomorphism tests FAILED ===");

    dap_common_deinit();
    return rc;
}
