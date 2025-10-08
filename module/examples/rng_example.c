#include <stdio.h>
#include "../crypto/src/rand/dap_rand.h"
#include "dap_math_ops.h"
#include "dap_chain_common.h"

#define NUMBER_OF_BITSTREAMS 100
#define NUMBER_OF_ELEMENTS 1024 * 4
#define LEN_OF_BITSTREAM (256 * NUMBER_OF_ELEMENTS)

int to_file(uint256_t* a, int n, FILE* f) {
    for (int i = 0; i < n; i++) {
        fwrite(a, sizeof(uint256_t), 1, f);
        a += 1;
    }
    return 0;
}

int main() {
    FILE* f = fopen("bitstreams.bin", "wb");
    if (!f) {
        return -1;
    }
    for (int i = 0; i < NUMBER_OF_BITSTREAMS; i++ ){
        uint256_t seed;
        randombytes(&seed, sizeof(seed));
//        printf("seed = %s", dap_chain_balance_print(seed));
        dap_pseudo_random_seed(seed);
        uint256_t* a = malloc(NUMBER_OF_ELEMENTS * sizeof(uint256_t));
        for (int j = 0; j < NUMBER_OF_ELEMENTS; j++) {
            a[j] = dap_pseudo_random_get(uint256_max, NULL);
        }
        to_file(a, NUMBER_OF_ELEMENTS, f);
        free(a);
    }
    fclose(f);
}
