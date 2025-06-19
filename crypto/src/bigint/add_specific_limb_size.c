
#define LOG_TAG "large integer arithmetic"
#include "add_specific_limb_size.h"


//The below functions use two's complement representation.
//As a result, the "circuitry" for signed and unsigned is the SAME.
//The idea here is that we start out with two input and one output
//bigint structures. The main function loop is indexed on the limb
//of the sum, calculated as a function. At each limb index, an
//adder structure is initialized, and populated then with the operator
//limbs and the carry in (bool flag) from llthe previous iteration sum.
//The execution of the addition is called in dap_full_adder_execute.

int dap_bigint_2scompl_ripple_carry_adder_value_64(dap_bigint_t* a,dap_bigint_t* b,dap_bigint_t* sum){


    int limb_size=64;

    bool carry_out_from_full_adder_for_next_limb=0;
    bool carry_in_ith_limb = carry_out_from_full_adder_for_next_limb;

    long size_sum=dap_bigint_get_size_sum_in_limbs(a,b);


    //Initialize full adder.
    dap_full_adder_t* ith_limb_full_adder = DAP_NEW_Z(dap_full_adder_t);
    if (dap_initialize_full_adder(ith_limb_full_adder)!=0){
        log_it(L_ERROR, "Adder failed to initialize");
    };

    //This iteration is the chaining of full adders, each calculating
    //a limb of the sum. The loop begins at 1 and goes up to the maximum
    //number of limbs that the sum can have, and therefore assumes an
    //LSB bit numbering. The carry out from the last adder is placed in
    //the last limb, which would reflect the overflow scenario.
    for(long limb_counter = 1; limb_counter <size_sum-1; limb_counter++)
    {
        //Take values from ith limb of a and b and put in full adder structure.
        uint64_t a_ith_limb = get_val_at_ith_limb_64(a, limb_counter);
        uint64_t b_ith_limb = get_val_at_ith_limb_64(b, limb_counter);

        //Set carry-in "bit" from previous limb iteration.
        bool carry_in_ith_limb = carry_out_from_full_adder_for_next_limb;

        //Set adder inputs.
        if (dap_set_adder_inputs(ith_limb_full_adder,&a_ith_limb,&b_ith_limb,carry_in_ith_limb)!=0){
            log_it(L_ERROR, "failed to set Adder inputs");
        };

        //Do the actual addition operation in the full adder.
        if (dap_full_adder_execute(ith_limb_full_adder)!=0){
            log_it(L_ERROR, "Adder failed to execute");
        };


        uint64_t* adder_sum_ptr_64;
        adder_sum_ptr_64=&(ith_limb_full_adder->specific_adder_for_limb_size.adder_64.adder_sum);
        //Take uint64_t limb sum calculation from full adder and place into sum of correct limb_counter index.
        if (dap_set_ith_limb_in_bigint(sum, limb_counter,adder_sum_ptr_64)!=0){
            log_it(L_ERROR, "Failed to set limb in sum structure");
        };


        //Transfer outputs from full adder struct to bigint output and carry_out variable for next limb iteration.
        //It is important to note that the carry out must be a separate variable as it will serve
        //as the carry in to the next adder, and is not copied over into the actual sum (output) structure.
        if (dap_set_carry_out_from_full_adder_for_next_limb(ith_limb_full_adder,carry_out_from_full_adder_for_next_limb)!=0){
            log_it(L_ERROR, "Adder failed to set carry out");
        };
    };

    if (dap_set_highest_limb_in_sum(carry_out_from_full_adder_for_next_limb,sum)){
        log_it(L_ERROR, "Failed to set last (carry) limb in sum");

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



//The below functions use two's complement representation.
//As a result, the "circuitry" for signed and unsigned is the SAME.
//The idea here is that we start out with two input and one output
//bigint structures. The main function loop is indexed on the limb
//of the sum, calculated as a function. At each limb index, an
//adder structure is initialized, and populated then with the operator
//limbs and the carry in (bool flag) from the previous iteration sum.
//The execution of the addition is called in dap_full_adder_execute.

int dap_bigint_2scompl_ripple_carry_adder_value_32(dap_bigint_t* a,dap_bigint_t* b,dap_bigint_t* sum){

    int limb_size=32;

    bool carry_out_from_full_adder_for_next_limb=0;
    bool carry_in_ith_limb = carry_out_from_full_adder_for_next_limb;

    long size_sum=dap_bigint_get_size_sum_in_limbs(a,b);


    //Initialize full adder.
    dap_full_adder_t* ith_limb_full_adder = NULL;
    if (dap_initialize_full_adder(ith_limb_full_adder)!=0){
        log_it(L_ERROR, "Adder failed to initialize");
    };

    //This iteration is the chaining of full adders, each calculating
    //a limb of the sum. The loop begins at 1 and goes up to the maximum
    //number of limbs that the sum can have, and therefore assumes an
    //LSB bit numbering. The carry out from the last adder is placed in
    //the last limb, which would reflect the overflow scenario.
    for(long limb_counter = 1; limb_counter <size_sum-1; limb_counter++)
    {
        //Take values from ith limb of a and b and put in full adder structure.
        uint64_t a_ith_limb = get_val_at_ith_limb_32(a, limb_counter);
        uint64_t b_ith_limb = get_val_at_ith_limb_32(b, limb_counter);

        //Set carry-in "bit" from previous limb iteration.
        bool carry_in_ith_limb = carry_out_from_full_adder_for_next_limb;

        //Set adder inputs.
        if (dap_set_adder_inputs(ith_limb_full_adder,&a_ith_limb,&b_ith_limb,carry_in_ith_limb)!=0){
            log_it(L_ERROR, "failed to set Adder inputs");
        };

        //Do the actual addition operation in the full adder.
        if (dap_full_adder_execute(ith_limb_full_adder)!=0){
            log_it(L_ERROR, "Adder failed to execute");
        };


        uint32_t* adder_sum_ptr_32;
        adder_sum_ptr_32=&(ith_limb_full_adder->specific_adder_for_limb_size.adder_32.adder_sum);
        //Take uint32_t limb sum calculation from full adder and place into sum of correct limb_counter index.
        if (dap_set_ith_limb_in_bigint(sum, limb_counter,adder_sum_ptr_32)!=0){
            log_it(L_ERROR, "Failed to set limb in sum structure");
        };


        //Transfer outputs from full adder struct to bigint output and carry_out variable for next limb iteration.
        //It is important to note that the carry out must be a separate variable as it will serve
        //as the carry in to the next adder, and is not copied over into the actual sum (output) structure.
        if (dap_set_carry_out_from_full_adder_for_next_limb(ith_limb_full_adder,carry_out_from_full_adder_for_next_limb)!=0){
            log_it(L_ERROR, "Adder failed to set carry out");
        };
    };

    if (dap_set_highest_limb_in_sum(carry_out_from_full_adder_for_next_limb,sum)){
        log_it(L_ERROR, "Failed to set last (carry) limb in sum");

    }
    return 0;

}


//at the moment we will only do this for 64 bits as a test
int dap_bigint_2scompl_ripple_carry_adder_pointer_32(dap_bigint_t* a,dap_bigint_t* b,dap_bigint_t* sum){

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
        ith_limb_full_adder->specific_adder_for_limb_size.adder_32.adder_a=a->data.limb_32.body[limb_counter];
        ith_limb_full_adder->specific_adder_for_limb_size.adder_32.adder_b=b->data.limb_32.body[limb_counter];
        ith_limb_full_adder->specific_adder_for_limb_size.adder_32.adder_carry_in=carry_in_ith_limb;


        //Do the actual addition operation in the full adder.
        if (dap_full_adder_execute(ith_limb_full_adder)!=0){
            log_it(L_ERROR, "Adder failed to execute");
        };


        //Take uint32_t limb sum calculation from full adder and place into sum of correct limb_counter index.
        sum->data.limb_32.body[limb_counter]=ith_limb_full_adder->specific_adder_for_limb_size.adder_32.adder_sum;


        //Transfer outputs from full adder struct to bigint output and carry_out variable for next limb iteration.
        //It is important to note that the carry out must be a separate variable as it will serve
        //as the carry in to the next adder, and is not copied over into the actual sum (output) structure.

        carry_out_from_full_adder_for_next_limb=ith_limb_full_adder->specific_adder_for_limb_size.adder_32.adder_carry_out;
    };

    sum->data.limb_32.body[size_sum]=carry_out_from_full_adder_for_next_limb;

    return 0;

}


//The below functions use two's complement representation.
//As a result, the "circuitry" for signed and unsigned is the SAME.
//The idea here is that we start out with two input and one output
//bigint structures. The main function loop is indexed on the limb
//of the sum, calculated as a function. At each limb index, an
//adder structure is initialized, and populated then with the operator
//limbs and the carry in (bool flag) from the previous iteration sum.
//The execution of the addition is called in dap_full_adder_execute.

int dap_bigint_2scompl_ripple_carry_adder_value_16(dap_bigint_t* a,dap_bigint_t* b,dap_bigint_t* sum){

    int limb_size=16;

    bool carry_out_from_full_adder_for_next_limb=0;
    bool carry_in_ith_limb = carry_out_from_full_adder_for_next_limb;

    long size_sum=dap_bigint_get_size_sum_in_limbs(a,b);


    //Initialize full adder.
    dap_full_adder_t* ith_limb_full_adder = NULL;
    if (dap_initialize_full_adder(ith_limb_full_adder)!=0){
        log_it(L_ERROR, "Adder failed to initialize");
    };

    //This iteration is the chaining of full adders, each calculating
    //a limb of the sum. The loop begins at 1 and goes up to the maximum
    //number of limbs that the sum can have, and therefore assumes an
    //LSB bit numbering. The carry out from the last adder is placed in
    //the last limb, which would reflect the overflow scenario.
    for(long limb_counter = 1; limb_counter <size_sum-1; limb_counter++)
    {
        //Take values from ith limb of a and b and put in full adder structure.
        uint64_t a_ith_limb = get_val_at_ith_limb_16(a, limb_counter);
        uint64_t b_ith_limb = get_val_at_ith_limb_16(b, limb_counter);

        //Set carry-in "bit" from previous limb iteration.
        bool carry_in_ith_limb = carry_out_from_full_adder_for_next_limb;

        //Set adder inputs.
        if (dap_set_adder_inputs(ith_limb_full_adder,&a_ith_limb,&b_ith_limb,carry_in_ith_limb)!=0){
            log_it(L_ERROR, "failed to set Adder inputs");
        };

        //Do the actual addition operation in the full adder.
        if (dap_full_adder_execute(ith_limb_full_adder)!=0){
            log_it(L_ERROR, "Adder failed to execute");
        };


        uint16_t* adder_sum_ptr_16;
        adder_sum_ptr_16=&(ith_limb_full_adder->specific_adder_for_limb_size.adder_16.adder_sum);
        //Take uint16_t limb sum calculation from full adder and place into sum of correct limb_counter index.
        if (dap_set_ith_limb_in_bigint(sum, limb_counter,adder_sum_ptr_16)!=0){
            log_it(L_ERROR, "Failed to set limb in sum structure");
        };


        //Transfer outputs from full adder struct to bigint output and carry_out variable for next limb iteration.
        //It is important to note that the carry out must be a separate variable as it will serve
        //as the carry in to the next adder, and is not copied over into the actual sum (output) structure.
        if (dap_set_carry_out_from_full_adder_for_next_limb(ith_limb_full_adder,carry_out_from_full_adder_for_next_limb)!=0){
            log_it(L_ERROR, "Adder failed to set carry out");
        };
    };

    if (dap_set_highest_limb_in_sum(carry_out_from_full_adder_for_next_limb,sum)){
        log_it(L_ERROR, "Failed to set last (carry) limb in sum");

    }
    return 0;

}



int dap_bigint_2scompl_ripple_carry_adder_pointer_16(dap_bigint_t* a,dap_bigint_t* b,dap_bigint_t* sum){

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
        ith_limb_full_adder->specific_adder_for_limb_size.adder_16.adder_a=a->data.limb_16.body[limb_counter];
        ith_limb_full_adder->specific_adder_for_limb_size.adder_16.adder_b=b->data.limb_16.body[limb_counter];
        ith_limb_full_adder->specific_adder_for_limb_size.adder_16.adder_carry_in=carry_in_ith_limb;


        //Do the actual addition operation in the full adder.
        if (dap_full_adder_execute(ith_limb_full_adder)!=0){
            log_it(L_ERROR, "Adder failed to execute");
        };


        //Take uint16_t limb sum calculation from full adder and place into sum of correct limb_counter index.
        sum->data.limb_16.body[limb_counter]=ith_limb_full_adder->specific_adder_for_limb_size.adder_16.adder_sum;


        //Transfer outputs from full adder struct to bigint output and carry_out variable for next limb iteration.
        //It is important to note that the carry out must be a separate variable as it will serve
        //as the carry in to the next adder, and is not copied over into the actual sum (output) structure.

        carry_out_from_full_adder_for_next_limb=ith_limb_full_adder->specific_adder_for_limb_size.adder_16.adder_carry_out;
    };

    sum->data.limb_16.body[size_sum]=carry_out_from_full_adder_for_next_limb;

    return 0;

}





//The below functions use two's complement representation.
//As a result, the "circuitry" for signed and unsigned is the SAME.
//The idea here is that we start out with two input and one output
//bigint structures. The main function loop is indexed on the limb
//of the sum, calculated as a function. At each limb index, an
//adder structure is initialized, and populated then with the operator
//limbs and the carry in (bool flag) from the previous iteration sum.
//The execution of the addition is called in dap_full_adder_execute.

int dap_bigint_2scompl_ripple_carry_adder_value_8(dap_bigint_t* a,dap_bigint_t* b,dap_bigint_t* sum){

    int limb_size=8;

    bool carry_out_from_full_adder_for_next_limb=0;
    bool carry_in_ith_limb = carry_out_from_full_adder_for_next_limb;

    long size_sum=dap_bigint_get_size_sum_in_limbs(a,b);


    //Initialize full adder.
    dap_full_adder_t* ith_limb_full_adder = NULL;
    if (dap_initialize_full_adder(ith_limb_full_adder)!=0){
        log_it(L_ERROR, "Adder failed to initialize");
    };

    //This iteration is the chaining of full adders, each calculating
    //a limb of the sum. The loop begins at 1 and goes up to the maximum
    //number of limbs that the sum can have, and therefore assumes an
    //LSB bit numbering. The carry out from the last adder is placed in
    //the last limb, which would reflect the overflow scenario.
    for(long limb_counter = 1; limb_counter <size_sum-1; limb_counter++)
    {
        //Take values from ith limb of a and b and put in full adder structure.
        uint8_t a_ith_limb = get_val_at_ith_limb_8(a, limb_counter);
        uint8_t b_ith_limb = get_val_at_ith_limb_8(b, limb_counter);

        //Set carry-in "bit" from previous limb iteration.
        bool carry_in_ith_limb = carry_out_from_full_adder_for_next_limb;

        //Set adder inputs.
        if (dap_set_adder_inputs(ith_limb_full_adder,&a_ith_limb,&b_ith_limb,carry_in_ith_limb)!=0){
            log_it(L_ERROR, "failed to set Adder inputs");
        };

        //Do the actual addition operation in the full adder.
        if (dap_full_adder_execute(ith_limb_full_adder)!=0){
            log_it(L_ERROR, "Adder failed to execute");
        };


        uint8_t* adder_sum_ptr_8;
        adder_sum_ptr_8=&(ith_limb_full_adder->specific_adder_for_limb_size.adder_8.adder_sum);
        //Take uint8_t limb sum calculation from full adder and place into sum of correct limb_counter index.
        if (dap_set_ith_limb_in_bigint(sum, limb_counter,adder_sum_ptr_8)!=0){
            log_it(L_ERROR, "Failed to set limb in sum structure");
        };


        //Transfer outputs from full adder struct to bigint output and carry_out variable for next limb iteration.
        //It is important to note that the carry out must be a separate variable as it will serve
        //as the carry in to the next adder, and is not copied over into the actual sum (output) structure.
        if (dap_set_carry_out_from_full_adder_for_next_limb(ith_limb_full_adder,carry_out_from_full_adder_for_next_limb)!=0){
            log_it(L_ERROR, "Adder failed to set carry out");
        };
    };

    if (dap_set_highest_limb_in_sum(carry_out_from_full_adder_for_next_limb,sum)){
        log_it(L_ERROR, "Failed to set last (carry) limb in sum");

    }
    return 0;

}



int dap_bigint_2scompl_ripple_carry_adder_pointer_8(dap_bigint_t* a,dap_bigint_t* b,dap_bigint_t* sum){

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
        ith_limb_full_adder->specific_adder_for_limb_size.adder_8.adder_a=a->data.limb_8.body[limb_counter];
        ith_limb_full_adder->specific_adder_for_limb_size.adder_8.adder_b=b->data.limb_8.body[limb_counter];
        ith_limb_full_adder->specific_adder_for_limb_size.adder_8.adder_carry_in=carry_in_ith_limb;


        //Do the actual addition operation in the full adder.
        if (dap_full_adder_execute(ith_limb_full_adder)!=0){
            log_it(L_ERROR, "Adder failed to execute");
        };


        //Take uint16_t limb sum calculation from full adder and place into sum of correct limb_counter index.
        sum->data.limb_8.body[limb_counter]=ith_limb_full_adder->specific_adder_for_limb_size.adder_8.adder_sum;


        //Transfer outputs from full adder struct to bigint output and carry_out variable for next limb iteration.
        //It is important to note that the carry out must be a separate variable as it will serve
        //as the carry in to the next adder, and is not copied over into the actual sum (output) structure.

        carry_out_from_full_adder_for_next_limb=ith_limb_full_adder->specific_adder_for_limb_size.adder_8.adder_carry_out;
    };

    sum->data.limb_8.body[size_sum]=carry_out_from_full_adder_for_next_limb;

    return 0;

}






