#include "add_specific_limb_size.h"
#define LOG_TAG "large integer arithmetic"

//The below functions use two's complement representation.
//As a result, the "circuitry" for signed and unsigned is the SAME.
//The idea here is that we start out with two input and one output
//bigint structures. The main function loop is indexed on the limb
//of the sum, calculated as a function. At each limb index, an
//adder structure is initialized, and populated then with the operator
//limbs and the carry in (bool flag) from llthe previous iteration sum.
//The execution of the addition is called in dap_full_adder_execute.

int dap_bigint_2scompl_ripple_carry_adder_value(dap_bigint_t* a,dap_bigint_t* b,dap_bigint_t* sum){

    //Essential security check.
    if (dap_run_3_bigint_security_checks(a,b,sum)!=0){
        log_it(L_ERROR, "Incompatible big integer parameters");
        return -1;
    }

    switch(a->limb_size){
    case 64:
        dap_bigint_2scompl_ripple_carry_adder_value_64(a,b,sum);
        break;
    case 32:
        dap_bigint_2scompl_ripple_carry_adder_value_32(a,b,sum);
        break;
    case 16:
        dap_bigint_2scompl_ripple_carry_adder_value_16(a,b,sum);
        break;
    case 8:
        dap_bigint_2scompl_ripple_carry_adder_value_8(a,b,sum);
        break;

    }

    return 0;

}


//at the moment we will only do this for 64 bits as a test
int dap_bigint_2scompl_ripple_carry_adder_pointer_64(dap_bigint_t* a,dap_bigint_t* b,dap_bigint_t* sum){

    long size_sum=dap_bigint_get_size_sum_in_limbs(a,b);
    bool carry_out_from_full_adder_for_next_limb=0;
    bool carry_in_ith_limb = carry_out_from_full_adder_for_next_limb;

    //Initialize full adder.
    dap_full_adder_t* ith_limb_full_adder;
    if (dap_initialize_full_adder(ith_limb_full_adder)!=0){
        log_it(L_ERROR, "Adder failed to initialize");
    };


    long limb_counter;
    for(limb_counter =0; limb_counter<size_sum-1; limb_counter++)
    {

        //Set carry-in "bit" from previous limb iteration.
        bool carry_in_ith_limb = carry_out_from_full_adder_for_next_limb;


        //Set adder inputs
        ith_limb_full_adder->specific_adder_for_limb_size.adder_64.adder_a=a->data.limb_64.body[limb_counter];
        ith_limb_full_adder->specific_adder_for_limb_size.adder_64.adder_b=b->data.limb_64.body[limb_counter];
        ith_limb_full_adder->specific_adder_for_limb_size.adder_64.adder_carry_in=carry_in_ith_limb;


        //Do the actual addition operation in the full adder.
        if (dap_full_adder_execute(ith_limb_full_adder)!=0){
            log_it(L_ERROR, "Adder failed to execute");
        };


        //Take uint64_t limb sum calculation from full adder and place into sum of correct limb_counter index.
        sum->data.limb_64.body[limb_counter]=ith_limb_full_adder->specific_adder_for_limb_size.adder_64.adder_sum;


        //Transfer outputs from full adder struct to bigint output and carry_out variable for next limb iteration.
        //It is important to note that the carry out must be a separate variable as it will serve
        //as the carry in to the next adder, and is not copied over into the actual sum (output) structure.

        carry_out_from_full_adder_for_next_limb=ith_limb_full_adder->specific_adder_for_limb_size.adder_64.adder_carry_out;
    };

    sum->data.limb_64.body[size_sum]=carry_out_from_full_adder_for_next_limb;

    return 0;

}

