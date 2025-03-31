#include "bigint.h"


int dap_bigint_is_signed(dap_bigint_t* a){
    return (a->header & (~(a->header) + 1));
}



