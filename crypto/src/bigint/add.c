#include "bigint.h"


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


    for(limb_counter = 1; limb_counter < sum_size; limb_counter++)
    {
        //Take Bit from Register A and B and put in adder
        am = getBitAt(this->A, i, &error);
        b = getBitAt(this->B, i, &error);
        c_in = getCarryOutBit(&this->adders[i-1]);
        setInputBits(&this->adders[i], a, b, c_in);
        //Compute ADD for that bit in the adder
        dap_full_adder(&this->adders[i]);
        //Take result from ADD and place in Result register
        setBitAt(&this->R, i, getResultBit(&this->adders[i]));
    }


}

int dap_bigint_signed_ripple_carry_adder(dap_bigint_t* a,dap_bigint_t* b,dap_bigint_t* sum){




}
