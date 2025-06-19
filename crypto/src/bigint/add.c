#include "add_specific_limb_size.h"


//The below functions use two's complement representation.
//As a result, the "circuitry" for signed and unsigned is te SAME.
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