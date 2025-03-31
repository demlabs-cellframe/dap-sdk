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


/* Return position (0, 1, ...) of rightmost (least-significant) one bit in n.
 *
 * This code uses a 32-bit version of algorithm to find the rightmost
 * one bit in Knuth, _The Art of Computer Programming_, volume 4A
 * (draft fascicle), section 7.1.3, "Bitwise tricks and
 * techniques."
 *
 * Assumes n has a 1 bit, i.e. n != 0
 *
 */
static unsigned rightone32(uint32_t n)
{
    const uint32_t a = 0x05f66a47;      /* magic number, found by brute force */
    static const unsigned decode[32] = { 0, 1, 2, 26, 23, 3, 15, 27, 24, 21, 19, 4, 12, 16, 28, 6, 31, 25, 22, 14, 20, 18, 11, 5, 30, 13, 17, 10, 29, 9, 8, 7 };
    n = a * (n & (-n));
    return decode[n >> 27];
}
