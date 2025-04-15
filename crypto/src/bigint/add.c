#include "circuit_formalism.h"
#define LOG_TAG "large integer arithmetic"




//The below functions use two's complement representation.
//As a result, the "circuitry" for signed and unsigned is the SAME.
//The idea here is that we start out with two input and one output
//bigint structures. The main function loop is indexed on the limb
//of the sum, calculated as a function. At each limb index, an
//adder structure is initialized, and populated then with the operator
//limbs and the carry in (bool flag) from the previous iteration sum.
//The execution of the addition is called in dap_full_adder_execute.

int dap_bigint_2scompl_ripple_carry_adder_value(dap_bigint_t* a,dap_bigint_t* b,dap_bigint_t* sum){


    if(dap_run_3_bigint_security_checks(a,b,sum)!=0){
        return -1;
    }

    bool carry_out_from_full_adder_for_next_limb=0;
    bool carry_in_ith_limb = carry_out_from_full_adder_for_next_limb;

    //Initialize full adder.
    dap_full_adder_t ith_limb_full_adder;
    if (dap_initialize_full_adder(&ith_limb_full_adder)!=0){
        log_it(L_ERROR, "Adder failed to initialize");
    };

    //This iteration is the chaining of full adders, each calculating
    //a limb of the sum. The loop begins at 1 and goes up to the maximum
    //number of limbs that the sum can have, and therefore assumes an
    //LSB bit numbering. The carry out from the last adder is placed in
    //the last limb, which would reflect the overflow scenario.
    for(limb_counter = 1; limb_counter <size_sum ; limb_counter++)
    {
        //Take values from ith limb of a and b and put in full adder structure.
        uint64_t a_ith_limb = get_val_at_ith_limb(a, limb_counter);
        uint64_t b_ith_limb = get_val_at_ith_limb(b, limb_counter);

        //Set carry-in "bit" from previous limb iteration.
        bool carry_in_ith_limb = carry_out_from_full_adder_for_next_limb;

        //Set adder inputs.
        if (dap_set_adder_inputs(&ith_limb_full_adder,a_ith_limb,b_ith_limb,carry_in_ith_limb)!=0){
            log_it(L_ERROR, "failed to set Adder inputs");
        };

        //Do the actual addition operation in the full adder.
        if (dap_full_adder_execute(&ith_limb_full_adder)!=0){
            log_it(L_ERROR, "Adder failed to execute");
        };


        //Take uint64_t limb sum calculation from full adder and place into sum of correct limb_counter index.
        if (dap_set_ith_limb_in_sum(sum, limb_counter, ith_limb_full_adder.sum)!=0){
            log_it(L_ERROR, "Failed to set limb in sum structure");
        };

        //Transfer outputs from full adder struct to bigint output and carry_out variable for next limb iteration.
        //It is important to note that the carry out must be a separate variable as it will serve
        //as the carry in to the next adder, and is not copied over into the actual sum (output) structure.
        if (dap_set_carry_out_from_full_adder_for_next_limb(&ith_limb_full_adder,carry_out_from_full_adder_for_next_limb)!=0){
            log_it(L_ERROR, "Adder failed to set carry out");
        };
    };

    if (dap_set_highest_limb_in_sum(carry_out_from_full_adder_for_next_limb,sum)){
        log_it(L_ERROR, "Failed to set last (carry) limb in sum");

    }
    return 0;

}



int dap_bigint_2scompl_ripple_carry_adder_pointer(dap_bigint_t* a,dap_bigint_t* b,dap_bigint_t* sum){


    if(dap_run_3_bigint_security_checks(a,b,sum)!=0){
        return -1;
    }

    bool carry_out_from_full_adder_for_next_limb=0;
    bool carry_in_ith_limb = carry_out_from_full_adder_for_next_limb;

    //Initialize full adder.
    dap_full_adder_t ith_limb_full_adder;
    if (dap_initialize_full_adder(&ith_limb_full_adder)!=0){
        log_it(L_ERROR, "Adder failed to initialize");
    };

    uint64_t* pointer_loop_start=&(a->data.limb_64.body[0]);
    uint64_t* limb_pointer;
    for(limb_pointer =pointer_loop_start; *limb_pointer  ; limb_pointer++)
    {
        //Take values from ith limb of a and b and put in full adder structure.
        uint64_t a_ith_limb = get_val_at_ith_limb(a, limb_counter);
        uint64_t b_ith_limb = get_val_at_ith_limb(b, limb_counter);

        //Set carry-in "bit" from previous limb iteration.
        bool carry_in_ith_limb = carry_out_from_full_adder_for_next_limb;

        //Set adder inputs.
        if (dap_set_adder_inputs(&ith_limb_full_adder,a_ith_limb,b_ith_limb,carry_in_ith_limb)!=0){
            log_it(L_ERROR, "failed to set Adder inputs");
        };

        //Do the actual addition operation in the full adder.
        if (dap_full_adder_execute(&ith_limb_full_adder)!=0){
            log_it(L_ERROR, "Adder failed to execute");
        };


        //Take uint64_t limb sum calculation from full adder and place into sum of correct limb_counter index.
        if (dap_set_ith_limb_in_sum(sum, limb_counter, ith_limb_full_adder.sum)!=0){
            log_it(L_ERROR, "Failed to set limb in sum structure");
        };

        //Transfer outputs from full adder struct to bigint output and carry_out variable for next limb iteration.
        //It is important to note that the carry out must be a separate variable as it will serve
        //as the carry in to the next adder, and is not copied over into the actual sum (output) structure.
        if (dap_set_carry_out_from_full_adder_for_next_limb(&ith_limb_full_adder,carry_out_from_full_adder_for_next_limb)!=0){
            log_it(L_ERROR, "Adder failed to set carry out");
        };
    };

    if (dap_set_highest_limb_in_sum(carry_out_from_full_adder_for_next_limb,sum)){
        log_it(L_ERROR, "Failed to set last (carry) limb in sum");

    }
    return 0;

}
