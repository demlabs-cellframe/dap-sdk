#include "bigint.h"

//the below has been added for prototyping, testing, and reference purposes only.
//this code is not written for any production/release purposes

unsigned int dap_single_bit_get_bit(unsigned int a,unsigned int index){
    return ((a&(1<<index))>>index);
}

unsigned int dap_single_bit_add(unsigned int carry_in, unsigned int a, unsigned int b){
    return ((carry_in^a)^b);
}

unsigned int carry_out(unsigned int carry_in, unsigned int a, unsigned int b){
    return ((carry_in & a)&b);
}


