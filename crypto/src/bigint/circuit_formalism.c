#include "circuit_formalism.h"

//set adder to all zeros to make sure values are as expected
int dap_initialize_full_adder(dap_full_adder_t* full_adder){
    full_adder->specific_adder_for_limb_size.adder_64.adder_a=0;
    full_adder->specific_adder_for_limb_size.adder_64.adder_b=0;
    full_adder->specific_adder_for_limb_size.adder_64.adder_sum=0;
    full_adder->specific_adder_for_limb_size.adder_64.adder_carry_in=0;
    full_adder->specific_adder_for_limb_size.adder_64.adder_carry_out=0;

    return 0;
}

int dap_set_adder_inputs(dap_full_adder_t* full_adder, void*  sum_op_a, void* sum_op_b,bool carry_in){
    full_adder->specific_adder_for_limb_size.adder_64.adder_a=(uint64_t) sum_op_a;
    full_adder->specific_adder_for_limb_size.adder_64.adder_b=(uint64_t) sum_op_b;
    full_adder->specific_adder_for_limb_size.adder_64.adder_carry_in=carry_in;

    return 0;
}

//this code performs the addition operation for uint64_t "bits" like an ordinary full adder
int dap_full_adder_execute(dap_full_adder_t* full_adder){
    uint64_t full_adder_a=full_adder->specific_adder_for_limb_size.adder_64.adder_a;
    uint64_t full_adder_b=full_adder->specific_adder_for_limb_size.adder_64.adder_b;
    uint64_t full_adder_carry_in=(uint64_t)full_adder->specific_adder_for_limb_size.adder_64.adder_carry_in;

    full_adder->specific_adder_for_limb_size.adder_64.adder_sum=(full_adder_a)+(full_adder_b)+(full_adder_carry_in);
    full_adder->specific_adder_for_limb_size.adder_64.adder_carry_out=((full_adder->specific_adder_for_limb_size.adder_64.adder_sum) < (full_adder_a+full_adder_b)); //overflow predicate

    return 0;
}


//This function simply takes the carry out present in the full adder, and
//populates it into a variable that can be then used in the adder chaining
//calculations.
int dap_set_carry_out_from_full_adder_for_next_limb(dap_full_adder_t* full_adder,bool carry_out){
    full_adder->specific_adder_for_limb_size.adder_64.adder_carry_out=carry_out;
    return 0;
}



//Sets the size, in count of limbs of the bigints, of the sum of two bigints.
//This function does not do a security check and ASSUMES that the register size of
//both bigints are the same.
//The logic of this function is that the largest possible size of a sum of two
//integers (in limbs) is the sum of the size of the larger of the operands
//with one, because this is the scenario where the addition has generated a
//carry.
long dap_bigint_get_max_size_sum_in_limbs(dap_bigint_t* a, dap_bigint_t*b){
    long a_count_limbs=dap_get_bigint_limb_count(a);
    long b_count_limbs=dap_get_bigint_limb_count(b);
    long sum_len=MAX(a_count_limbs,b_count_limbs)+1;

    return sum_len;
}

//The below function assumes that bigint is in LSB at the limb level
//A Ripple Carry Adder chains adders from LSB to MSB so
//it is logically consistent to have an LSB bigint type.
int dap_set_highest_limb_in_sum(bool carry_in,dap_bigint_t* sum){
    int last_limb_before_carry=dap_get_bigint_limb_count(sum);
    switch(sum->limb_size){

    case(64):
            sum->data.limb_64.body[last_limb_before_carry+1]=carry_in;
        break;

    case(32):
        sum->data.limb_32.body[last_limb_before_carry+1]=carry_in;
        break;

    case(16):
        sum->data.limb_16.body[last_limb_before_carry+1]=carry_in;
        break;

    case(8):
        sum->data.limb_8.body[last_limb_before_carry+1]=carry_in;
        break;
    }

    return 0;
}
