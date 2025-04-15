#include "circuit_formalism.h"


// int dap_adder_sub_block(dap_full_adder_t* full_adder){

//     if (full_adder->a > full_adder->b){
//         full_adder->sum=full_adder->b-full_adder->a;
//     }

//     if (full_adder->a == full_adder->b){
//         full_adder->sum=full_adder->b-full_adder->a;
//     }

// }

// int dap_adder_add_sub_block(dap_full_adder_t* full_adder){

// }

// int dap_adder_carry_sub(dap_full_adder_t* full_adder){

// }

// int dap_adder_carry_add_sub(dap_full_adder_t* full_adder,){


// }

// int dap_adder_subtractor(dap_bigint_t* a,dap_bigint_t* b,dap_bigint_t* sum){
//     int limb_counter;
//     //int sum_size=dap_get_sum_size_from_unsigned_terms(a,b);
//     //dap_full_adder_t ith_limb_adder;
//     //unsigned char carry_out_from_full_adder_for_next_limb=0;

//     for(limb_counter = 1; limb_counter < sum_size; limb_counter++)
//     {
//         uint64_t a_ith_limb = get_val_at_ith_limb(a,limb_counter);
//         uint64_t b_ith_limb = get_val_at_ith_limb(b,limb_counter);

//         //initialize full adder and set inputs
//         dap_full_adder_t ith_limb_full_adder;
//         dap_initialize_full_adder(ith_limb_full_adder);
//         dap_set_adder_inputs(ith_limb_full_adder,a_ith_limb,b_ith_limb);

//         //do the actual addition operation in the full adder
//         dap_full_adder_execute(ith_limb_full_adder);

//         dap_adder_sub_block(limb);
//         dap_adder_add_sub_block();

//     }


//     return 0;
// }
