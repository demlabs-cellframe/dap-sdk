#ifndef BIGINT_H
#define BIGINT_H
#endif // BIGINT_H
#include <stdint.h>
#include <stdbool.h>




//we will initially test a similar structure as GMP
//little endian in the sense that the first limb of
//body is the least significant and the last limb is the
//most significant.
//The header contains the sign bit, the body is the
//magnitude

typedef struct dap_bigint {
    uint64_t header, *body;
} dap_bigint_t;


int dap_bigint_is_signed(dap_bigint_t* a){
    return (a->header & (~(a->header) + 1));
}

int dap_bigint_get_size_sum(dap_bigint_t* a,dap_bigint_t* b){

    return 0;
}







//dap_get_sum_size_from_unsigned_terms

