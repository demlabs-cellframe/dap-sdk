#include "circuit_formalism.h"

//set adder to all zeros to make sure values are as expected
int dap_initialize_full_adder(dap_full_adder_t* full_adder){
    full_adder->a=0;
    full_adder->b=0;
    full_adder->sum=0;
    full_adder->carry_in=0;
    full_adder->carry_out=0;

    return 0;
}

int dap_set_adder_inputs(dap_full_adder_t* full_adder, uint64_t sum_op_a, uint64_t sum_op_b){
    full_adder->a=sum_op_a;
    full_adder->b=sum_op_b;

    return 0;
}


//this code performs the addition operation for uint64_t "bits" like an ordinary full adder
int dap_full_adder_execute(dap_full_adder_t* full_adder){
    full_adder->sum=(full_adder->a)+(full_adder->b)+((uint64_t*)(full_adder->carry_in));
    full_adder->carry_out=((full_adder->sum) < (full_adder->a+full_adder->b)); //overflow predicate

    return 0;
}


int dap_set_ith_limb_in_sum(dap_bigint_t* sum,int limb_counter, uint64_t limb){
    sum->body[limb_counter]=limb;

    return 0;
}


int dap_set_carry_out_from_full_adder_for_next_limb(dap_full_adder_t* full_adder,unsigned char carry_in){


}


uint64_t get_val_at_ith_limb(dap_bigint_t* a, int limb_index){
    uint64_t val_at_ith_limb;
    return val_at_ith_limb;
}

