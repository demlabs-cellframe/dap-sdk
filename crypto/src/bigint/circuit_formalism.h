#ifndef CIRCUIT_FORMALISM_H
#define CIRCUIT_FORMALISM_H

#endif // CIRCUIT_FORMALISM_H
#include "dap_common.h"

#include "bigint.h"


typedef struct dap_half_adder{
    uint64_t a;
    uint64_t b;
    bool carry_out:1;
    uint64_t sum;
} DAP_ALIGN_PACKED dap_half_adder_t;



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


int dap_initialize_full_adder(dap_full_adder_t* full_adder);
int dap_set_adder_inputs(dap_full_adder_t* full_adder, uint64_t sum_op_a, uint64_t sum_op_b);
int dap_full_adder_execute(dap_full_adder_t* full_adder);
int dap_set_ith_limb_in_sum(dap_bigint_t* sum,int limb_counter, uint64_t limb);
int dap_set_carry_out_from_full_adder_for_next_limb(dap_full_adder_t* full_adder,bool carry_out);
uint64_t get_val_at_ith_limb(dap_bigint_t* a, int limb_index);
int dap_bigint_get_size_sum(dap_bigint_t* a, dap_bigint_t*b);
int dap_get_bigint_limb_count(dap_bigint_t* a);






