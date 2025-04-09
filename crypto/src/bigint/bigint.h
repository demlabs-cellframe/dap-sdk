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





int dap_get_bigint_size_limbs(dap_bigint_t* a){
    //returns the size of the bigint, in limb count

    return 0;
}

