#include "circuit_formalism.h"

//The below functions use two's complement representation.
//As a result, the "circuitry" for signed and unsigned is the SAME.
//The idea here is that we start out with two input and one output
//bigint structures. The main function loop is indexed on the limb
//of the sum, calculated as a function. At each limb index, an
//adder structure is initialized, and populated then with the operator
//limbs and the carry in (bool flag) from the previous iteration sum.
//The execution of the addition is called in dap_full_adder_execute.



int dap_bigint_2scompl_ripple_carry_adder(dap_bigint_t* a,dap_bigint_t* b,dap_bigint_t* sum){

    char is_signed_a=dap_bigint_is_signed(a);
    char is_signed_b=dap_bigint_is_signed(b);
    if (is_signed_a!=is_signed_b){
        return -1;//essential security check
    }

    int limb_counter;
    dap_full_adder_t ith_limb_adder;
    unsigned char carry_out_from_full_adder_for_next_limb=0;
    int size_sum=dap_bigint_get_size_sum(a,b);

    for(limb_counter = 1; limb_counter <size_sum ; limb_counter++)
    {
        //take values from ith limb of a and b and put in full adder structure
        uint64_t a_ith_limb = get_val_at_ith_limb(a, limb_counter);
        uint64_t b_ith_limb = get_val_at_ith_limb(b, limb_counter);

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

