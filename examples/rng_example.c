#include <stdio.h>
#include "../crypto/src/rand/dap_rand.h"
#include "dap_math_ops.h"
#include "dap_chain_common.h"



int main() {
    uint256_t a = GET_256_FROM_64(0x1234567890abcdef);

    dap_pseudo_random_seed(a);

    uint256_t b = dap_pseudo_random_get(uint256_max);

    printf("b = %s", dap_chain_balance_print(b));
}
