/*
 * Regression test for CR-D5: chipmunk_hots_setup must produce a bit-identical
 * set of public matrix A polynomials across repeated invocations, even when
 * the surrounding stack is known to be dirty.
 *
 * Background:
 *   An earlier implementation fed a 36-byte seed buffer into SHA3-256 while
 *   initialising only 8 of those bytes, leaving 28 uninitialised stack bytes
 *   as part of the hash pre-image.  That made chipmunk_hots_setup silently
 *   non-deterministic across processes (and, in the multi-signer
 *   aggregation path, across signers inside the same process), which in turn
 *   caused every aggregate HOTS identity check to fail.
 *
 * This test guards against any future regression of that bug by:
 *   1. Priming the stack with a non-zero pattern
 *   2. Running chipmunk_hots_setup many times
 *   3. Asserting bit-equality of every polynomial coefficient across runs
 *   4. Priming the stack with a *different* pattern and repeating
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "dap_common.h"
#include "chipmunk/chipmunk_hots.h"
#include "chipmunk/chipmunk.h"

#define ITERATIONS 16

/* Fill a large stack region with a given byte pattern so that any reliance
 * on uninitialised stack memory inside chipmunk_hots_setup would be detected
 * as a mismatch between iterations priming with different patterns. */
__attribute__((noinline))
static void s_prime_stack(uint8_t a_pattern)
{
    volatile uint8_t l_scratch[4096];
    memset((void *)l_scratch, a_pattern, sizeof(l_scratch));
    /* touch the buffer so the compiler cannot optimise it away */
    volatile uint8_t l_sink = 0;
    for (size_t i = 0; i < sizeof(l_scratch); i += 64) {
        l_sink ^= l_scratch[i];
    }
    (void)l_sink;
}

__attribute__((noinline))
static int s_run_setup(chipmunk_hots_params_t *a_out, uint8_t a_stack_pattern)
{
    s_prime_stack(a_stack_pattern);
    return chipmunk_hots_setup(a_out);
}

static int s_params_equal(const chipmunk_hots_params_t *a_lhs,
                          const chipmunk_hots_params_t *a_rhs,
                          int *a_first_diff_i, int *a_first_diff_j)
{
    for (int i = 0; i < CHIPMUNK_GAMMA; i++) {
        for (int j = 0; j < CHIPMUNK_N; j++) {
            if (a_lhs->a[i].coeffs[j] != a_rhs->a[i].coeffs[j]) {
                if (a_first_diff_i) *a_first_diff_i = i;
                if (a_first_diff_j) *a_first_diff_j = j;
                return 0;
            }
        }
    }
    return 1;
}

int main(void)
{
    dap_common_init("chipmunk-hots-setup-determinism", NULL);

    chipmunk_hots_params_t l_reference;
    if (s_run_setup(&l_reference, 0xAA) != 0) {
        fprintf(stderr, "FAIL: reference chipmunk_hots_setup failed\n");
        return 1;
    }

    for (int it = 0; it < ITERATIONS; it++) {
        chipmunk_hots_params_t l_candidate;
        /* Alternate between wildly different stack patterns so that any
         * residual UB would almost certainly flip at least one coefficient. */
        uint8_t l_pattern = (it & 1) ? 0x55 : 0xA5;
        l_pattern ^= (uint8_t)(it * 0x13);

        if (s_run_setup(&l_candidate, l_pattern) != 0) {
            fprintf(stderr, "FAIL: iteration %d chipmunk_hots_setup failed\n", it);
            return 1;
        }

        int l_diff_i = -1, l_diff_j = -1;
        if (!s_params_equal(&l_reference, &l_candidate, &l_diff_i, &l_diff_j)) {
            fprintf(stderr,
                    "FAIL: chipmunk_hots_setup is non-deterministic at iteration %d "
                    "(stack pattern 0x%02x): diverged at a[%d].coeffs[%d] "
                    "reference=%d candidate=%d\n",
                    it, l_pattern, l_diff_i, l_diff_j,
                    l_reference.a[l_diff_i].coeffs[l_diff_j],
                    l_candidate.a[l_diff_i].coeffs[l_diff_j]);
            return 1;
        }
    }

    printf("PASS: chipmunk_hots_setup produced identical A matrices across %d "
           "independent invocations with adversarially dirty stacks\n",
           ITERATIONS);
    return 0;
}
