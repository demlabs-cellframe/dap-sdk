#include "circuit_formalism.h"

//an important outstanding issue in the below half and full adder is the question of the
//unsigned char datatypes. First of all,  are we certain that the bitwise operations
//do not exceed the bit size? Secondly, what sort of type conversion do we need to do in
//C to maintain the small size and is it even worth it.
int dap_half_adder_execute(dap_half_adder_t* half_adder){
    half_adder->carry_out=(half_adder->a)^(half_adder->b);
    half_adder->sum=(half_adder->a)&(half_adder->b);
    return 0;
}

int dap_full_adder_execute(dap_full_adder_t* full_adder){
    full_adder->carry_out=((full_adder->a)&(full_adder->b))|((full_adder->b)&(full_adder->carry_in))|((full_adder->a)&(full_adder->carry_in));
    full_adder->sum=(full_adder->a)&(full_adder->b)&(full_adder->carry_in);
    return 0;
}

uint64_t get_val_at_ith_limb(dap_bigint_t* bigint_input_struct, int limb_index){
    uint64_t val_at_ith_limb;
    return val_at_ith_limb;
}

unsigned char dap_set_carry_out_from_full_adder_for_next_limb(dap_full_adder_t* full_adder_input_struct,unsigned char carry_in){


}


