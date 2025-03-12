#include "circuit_formalism.h"


int dap_bigint_ripple_carry_adder(dap_bigint_t* a, dap_bigint_t* b,dap_bigint_t* sum){
    char is_signed_a=dap_bigint_is_signed(a);
    char is_signed_b=dap_bigint_is_signed(b);
    if (is_signed_a!=is_signed_b){
        return -1;//essential security check
    }

    if(is_signed_a==0){
        //immediately set the sum sign as no calculation is necessary to determine it
        //this sets the sign parameter in the sum header to unsigned
        dap_set_bigint_unsigned(sum);
        dap_bigint_unsigned_ripple_carry_adder(a,b,sum);
    }

    if(is_signed_b==0){
        dap_bigint_signed_ripple_carry_adder(a,b,sum);
    }

    return 0;
}

int dap_bigint_unsigned_ripple_carry_adder(dap_bigint_t* a,dap_bigint_t* b,dap_bigint_t* sum){

    int limb_counter;
    int sum_size=dap_get_sum_size_from_unsigned_terms(a,b);
    dap_full_adder_t ith_limb_adder;
    unsigned char carry_out_from_full_adder_for_next_limb=0;

    for(limb_counter = 1; limb_counter < sum_size; limb_counter++)
    {
        //take values from ith limb of a and b and put in full adder structure
        uint64_t a_ith_limb = get_val_at_ith_limb(a);
        uint64_t b_ith_limb = get_val_at_ith_limb(b);

        //set carry-in "bit" from previous limb iteration
        uint64_t carry_in_ith_limb = carry_out_from_full_adder_for_next_limb;

        //initialize full adder and set inputs
        dap_full_adder_t ith_limb_full_adder;
        dap_initialize_full_adder(ith_limb_full_adder);
        dap_set_adder_inputs(ith_limb_full_adder,a_ith_limb,b_ith_limb);

        //do the actual addition operation in the full adder
        dap_full_adder_execute(ith_limb_full_adder);

        //transfer outputs from full adder struct to bigint output and carry_out variable for next limb iteration
        dap_set_carry_out_from_full_adder_for_next_limb(ith_limb_full_adder,carry_out_from_full_adder_for_next_limb);
        dap_set_ith_limb_in_sum(ith_limb_full_adder,sum)
    }

    return 0;

}

int dap_bigint_signed_ripple_carry_adder(dap_bigint_t* a,dap_bigint_t* b,dap_bigint_t* sum){




}
