#include "bigint.h"

//the below has been added primarily for conceptualization, and to a
//lesser extent prototyping, testing, and reference purposes.
//THIS CODE IS NOT INTENDED FOR ANY PRODUCTION/RELEASE USE

unsigned int dap_single_bit_get_bit(unsigned int a,unsigned int index){
    return ((a&(1<<index))>>index);
}

unsigned int dap_single_bit_add(unsigned int carry_in, unsigned int a, unsigned int b){
    return ((carry_in^a)^b);
}

unsigned int carry_out(unsigned int carry_in, unsigned int a, unsigned int b){
    return ((carry_in & a)&b);
}


int dap_8_bit_unsigned_adder(unsigned int a, unsigned int b, unsigned int sum){

    unsigned int carry_in_var=0;
    unsigned int carry_out_var=0;
    unsigned int bit_index=0;
    for (bit_index=0;bit_index<sizeof(a)*8;bit_index++) {
        unsigned int a_ith_bit=a&(1<<bit_index);
        unsigned int b_ith_bit=b&(1<<bit_index);
        unsigned int sum_ith_bit=a_ith_bit^b_ith_bit^carry_in_var;
        carry_out_var=(a_ith_bit&b_ith_bit)|(b_ith_bit&carry_in_var)|(a_ith_bit&carry_in_var);
        sum=sum|sum_ith_bit;
        carry_in_var=carry_out_var;
    }
    return 0;

}
