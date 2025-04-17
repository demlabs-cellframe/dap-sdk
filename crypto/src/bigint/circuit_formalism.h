#ifndef CIRCUIT_FORMALISM_H
#define CIRCUIT_FORMALISM_H

#endif // CIRCUIT_FORMALISM_H
#include "dap_common.h"

#include "bigint.h"

#define MAX(a,b) \
({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a > _b ? _a : _b; })

typedef struct dap_full_adder_64{
        uint64_t adder_a;
        uint64_t adder_b;
        uint64_t adder_sum;
        struct {
            union {
                struct {
                    bool adder_carry_in:1;
                    bool adder_carry_out:1;
                } DAP_ALIGN_PACKED;
            };

        };
}DAP_ALIGN_PACKED dap_full_adder_64_t;

typedef struct dap_full_adder_32{
    uint32_t adder_a;
    uint32_t adder_b;
    uint32_t adder_sum;
    struct {
        union {
            struct {
                bool adder_carry_in:1;
                bool adder_carry_out:1;
            } DAP_ALIGN_PACKED;
        };
    };
}DAP_ALIGN_PACKED dap_full_adder_32_t;

typedef struct dap_full_adder_16{
    uint16_t adder_a;
    uint16_t adder_b;
    uint16_t adder_sum;
    struct {
        union {
            struct {
                bool adder_carry_in:1;
                bool adder_carry_out:1;
            } DAP_ALIGN_PACKED;
        };

    };
}DAP_ALIGN_PACKED dap_full_adder_16_t;

typedef struct dap_full_adder_8{
    uint8_t adder_a;
    uint8_t adder_b;
    uint8_t adder_sum;
    struct {
        union {
            struct {
                bool adder_carry_in:1;
                bool adder_carry_out:1;
            } DAP_ALIGN_PACKED;
        };

    };
}DAP_ALIGN_PACKED dap_full_adder_8_t;

typedef struct dap_full_adder {
    int operation;
    union {
        dap_full_adder_64_t adder_64;
        dap_full_adder_32_t adder_32;
        dap_full_adder_16_t adder_16;
        dap_full_adder_8_t adder_8;
    } specific_adder_for_limb_size;
} dap_full_adder_t;


int dap_initialize_full_adder(dap_full_adder_t* full_adder);
int dap_set_adder_inputs(dap_full_adder_t* full_adder, void*  sum_op_a, void* sum_op_b,bool carry_in);
int dap_full_adder_execute(dap_full_adder_t* full_adder);
int dap_set_carry_out_from_full_adder_for_next_limb(dap_full_adder_t* full_adder,bool carry_out);
long dap_bigint_get_size_sum_in_limbs(dap_bigint_t* a, dap_bigint_t*b);
int dap_set_highest_limb_in_sum(bool carry_in,dap_bigint_t* a);






