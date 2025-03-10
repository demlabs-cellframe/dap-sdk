#ifndef CIRCUIT_FORMALISM_H
#define CIRCUIT_FORMALISM_H

#endif // CIRCUIT_FORMALISM_H

#include "bigint.h"

struct dap_half_adder{
    uint64_t a;
    uint64_t b;
    unsigned char carry_out;
    uint64_t sum;
};

typedef struct dap_half_adder dap_half_adder_t;

struct dap_full_adder{
    uint64_t a;
    uint64_t b;
    unsigned char carry_in;
    unsigned char carry_out;
    uint64_t sum;
};


typedef struct dap_full_adder dap_full_adder_t;
