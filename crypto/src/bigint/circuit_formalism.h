#ifndef CIRCUIT_FORMALISM_H
#define CIRCUIT_FORMALISM_H

#endif // CIRCUIT_FORMALISM_H

#include "bigint.h"

typedef struct dap_half_adder{
    uint64_t a;
    uint64_t b;
    bool carry_out:1;
    uint64_t sum;
}DAP_ALIGN_PACKED dap_half_adder_t;



typedef struct dap_full_adder{
    uint64_t a;
    uint64_t b;
    uint64_t sum;
    struct {
        union {
            struct {
                bool carry_in:1;
                bool carry_out:1;
            } DAP_ALIGN_PACKED;
        };
    };

}DAP_ALIGN_PACKED dap_full_adder_t;



