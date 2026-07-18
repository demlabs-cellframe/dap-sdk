/* Standalone debug test for NTT multiply correctness */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "chipmunk/chipmunk_ntt.h"
#include "chipmunk/chipmunk_poly.h"
#include "chipmunk/chipmunk.h"

#define N CHIPMUNK_N
#define Q ((uint64_t)CHIPMUNK_Q)

static void time_domain_mul(int32_t *out, const int32_t *a, const int32_t *b) {
    memset(out, 0, N * sizeof(int32_t));
    for (int k = 0; k < N; k++) {
        for (int m = 0; m < N; m++) {
            int sum = k + m;
            int idx = sum & (N - 1);
            int64_t prod = (int64_t)a[k] * b[m];
            if (sum >= N) prod = -prod;
            int64_t val = (int64_t)out[idx] + prod;
            out[idx] = (int32_t)(val % (int64_t)Q);
            if (out[idx] < 0) out[idx] += (int32_t)Q;
        }
    }
}

int main(void) {
    chipmunk_ntt_ctx_t ctx;
    chipmunk_ntt_params_compute(&ctx, Q);

    int pass = 0, fail = 0;

    /* Test 1: X * 1 = X */
    {
        chipmunk_poly_t a = {0}, b = {0};
        a.coeffs[1] = 1; b.coeffs[0] = 1;
        chipmunk_ntt(a.coeffs); chipmunk_ntt(b.coeffs);
        chipmunk_poly_t prod;
        chipmunk_poly_mul_ntt_q(&prod, &a, &b, Q);
        chipmunk_invntt(prod.coeffs);
        int ok = (prod.coeffs[1] == 1);
        for (int i = 0; i < N && ok; i++) if (i != 1 && prod.coeffs[i] != 0) ok = 0;
        printf("Test X*1=X: %s\n", ok ? "PASS" : "FAIL"); ok ? pass++ : fail++;
    }

    /* Test 2: X * X = X^2 */
    {
        chipmunk_poly_t a = {0}, b = {0};
        a.coeffs[1] = 1; b.coeffs[1] = 1;
        chipmunk_ntt(a.coeffs); chipmunk_ntt(b.coeffs);
        chipmunk_poly_t prod;
        chipmunk_poly_mul_ntt_q(&prod, &a, &b, Q);
        chipmunk_invntt(prod.coeffs);
        int ok = (prod.coeffs[2] == 1);
        for (int i = 0; i < N && ok; i++) if (i != 2 && prod.coeffs[i] != 0) ok = 0;
        printf("Test X*X=X^2: %s  [0..4]=%d %d %d %d %d\n",
               ok ? "PASS" : "FAIL",
               prod.coeffs[0], prod.coeffs[1], prod.coeffs[2], prod.coeffs[3], prod.coeffs[4]);
        ok ? pass++ : fail++;
    }

    /* Test 3: (X+1)*(X+1) = X^2+2X+1 */
    {
        chipmunk_poly_t a = {0}, b = {0};
        a.coeffs[0] = 1; a.coeffs[1] = 1;
        b.coeffs[0] = 1; b.coeffs[1] = 1;
        chipmunk_ntt(a.coeffs); chipmunk_ntt(b.coeffs);
        chipmunk_poly_t prod;
        chipmunk_poly_mul_ntt_q(&prod, &a, &b, Q);
        chipmunk_invntt(prod.coeffs);
        int ok = (prod.coeffs[0] == 1 && prod.coeffs[1] == 2 && prod.coeffs[2] == 1);
        for (int i = 3; i < N && ok; i++) if (prod.coeffs[i] != 0) ok = 0;
        printf("Test (X+1)^2: %s  [0..4]=%d %d %d %d %d\n",
               ok ? "PASS" : "FAIL",
               prod.coeffs[0], prod.coeffs[1], prod.coeffs[2], prod.coeffs[3], prod.coeffs[4]);
        ok ? pass++ : fail++;
    }

    /* Test 4: X^256 * X^256 = X^512 = -1 (negacyclic) */
    {
        chipmunk_poly_t a = {0}, b = {0};
        a.coeffs[256] = 1; b.coeffs[256] = 1;
        chipmunk_ntt(a.coeffs); chipmunk_ntt(b.coeffs);
        chipmunk_poly_t prod;
        chipmunk_poly_mul_ntt_q(&prod, &a, &b, Q);
        chipmunk_invntt(prod.coeffs);
        int32_t neg1 = (int32_t)Q - 1;
        int ok = (prod.coeffs[0] == neg1);
        for (int i = 1; i < N && ok; i++) if (prod.coeffs[i] != 0) ok = 0;
        printf("Test X^256*X^256=-1: %s  [0]=%d (expect %d)\n",
               ok ? "PASS" : "FAIL", prod.coeffs[0], neg1);
        ok ? pass++ : fail++;
    }

    /* Test 5: HOTS-style — sparse ternary * bounded */
    {
        chipmunk_poly_t a = {0}, b = {0};
        a.coeffs[0] = 1; a.coeffs[5] = -1; a.coeffs[17] = 1; a.coeffs[100] = -1;
        b.coeffs[0] = 7; b.coeffs[1] = -3; b.coeffs[2] = 11;

        int32_t td[N];
        time_domain_mul(td, a.coeffs, b.coeffs);

        chipmunk_poly_t na = a, nb = b, prod;
        chipmunk_ntt(na.coeffs); chipmunk_ntt(nb.coeffs);
        chipmunk_poly_mul_ntt_q(&prod, &na, &nb, Q);
        chipmunk_invntt(prod.coeffs);

        int mismatch = 0;
        for (int k = 0; k < N; k++) {
            int32_t n = prod.coeffs[k] % (int32_t)Q;
            if (n < 0) n += (int32_t)Q;
            if (n != td[k]) mismatch++;
        }
        printf("Test sparse*bounded: %s  mismatches=%d/%d\n",
               mismatch == 0 ? "PASS" : "FAIL", mismatch, N);
        mismatch == 0 ? pass++ : fail++;
    }

    printf("\n%d/%d passed\n", pass, pass + fail);
    return fail > 0 ? 1 : 0;
}
