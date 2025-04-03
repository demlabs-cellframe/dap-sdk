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

//This function sets the sum limb of index limb_index
int dap_set_ith_limb_in_sum(dap_bigint_t* sum,int limb_index, uint64_t limb_value){
    sum->body[limb_index]=limb_value;
    return 0;
}

//This function simply takes the carry out present in the full adder, and
//populates it into a variable that can be then used in the adder chaining
//calculations.
int dap_set_carry_out_from_full_adder_for_next_limb(dap_full_adder_t* full_adder,bool carry_out){


}

//This function takes the limb of index "limb_index" from the bigint structure
//and returns it as a uint64_t value. This value is then used to populate the
//full adder structure for calculation.
uint64_t get_val_at_ith_limb(dap_bigint_t* a, int limb_index){
    uint64_t val_at_ith_limb=a->body[limb_index];

    return val_at_ith_limb;
}

//Sets the size, in count of 64 BIT LIMBS, of the sum of two bigints.
int dap_bigint_get_size_sum(dap_bigint_t* a, dap_bigint_t*b){
    int a_count_limbs=dap_get_bigint_limb_count(a);
    int b_count_limbs=dap_get_bigint_limb_count(b);

    return MAX(a_count_limbs,b_count_limbs);
}

//Returns the length of the bigint, in uint64_t limb count.
int dap_get_bigint_limb_count(dap_bigint_t* a){

    return 0;
}
