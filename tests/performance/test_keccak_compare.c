#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "dap_hash_keccak.h"

extern void dap_hash_keccak_permute_avx512vl_asm(dap_hash_keccak_state_t *state);

int main(void)
{
    dap_hash_keccak_state_t ref_state, asm_state;

    for (int i = 0; i < 25; i++) {
        ref_state.lanes[i] = (uint64_t)i * 0x0123456789ABCDEFULL;
        asm_state.lanes[i] = ref_state.lanes[i];
    }

    dap_hash_keccak_permute_ref(&ref_state);
    dap_hash_keccak_permute_avx512vl_asm(&asm_state);

    int mismatch = 0;
    for (int i = 0; i < 25; i++) {
        if (ref_state.lanes[i] != asm_state.lanes[i]) {
            printf("MISMATCH lane %2d: ref=%016lx  asm=%016lx\n",
                   i, ref_state.lanes[i], asm_state.lanes[i]);
            mismatch++;
        }
    }

    if (mismatch == 0) {
        printf("PASS: all 25 lanes match\n");
    } else {
        printf("FAIL: %d mismatches\n", mismatch);

        printf("\n--- Single round test ---\n");
        for (int i = 0; i < 25; i++) {
            ref_state.lanes[i] = (uint64_t)i;
            asm_state.lanes[i] = (uint64_t)i;
        }

        dap_hash_keccak_permute_ref(&ref_state);
        dap_hash_keccak_permute_avx512vl_asm(&asm_state);

        for (int i = 0; i < 25; i++) {
            if (ref_state.lanes[i] != asm_state.lanes[i]) {
                printf("  lane %2d: ref=%016lx  asm=%016lx\n",
                       i, ref_state.lanes[i], asm_state.lanes[i]);
            }
        }
    }

    return mismatch ? 1 : 0;
}
