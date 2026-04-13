#include <stdio.h>
#include <stdint.h>
#include "chipmunk/chipmunk.h"

int main() {
    printf("sizeof(chipmunk_poly_t) = %zu\n", sizeof(chipmunk_poly_t));
    printf("sizeof(chipmunk_public_key_t) = %zu\n", sizeof(chipmunk_public_key_t));
    printf("CHIPMUNK_N = %d\n", CHIPMUNK_N);
    printf("CHIPMUNK_N * sizeof(int32_t) = %zu\n", CHIPMUNK_N * sizeof(int32_t));
    printf("CHIPMUNK_PUBLIC_KEY_SIZE = %d\n", CHIPMUNK_PUBLIC_KEY_SIZE);
    return 0;
} 