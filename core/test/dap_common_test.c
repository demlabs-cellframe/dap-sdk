#include "dap_common_test.h"

static void s_test_put_int() {
    dap_print_module_name("dap_common");
    const int INT_VAL = 10;
    const char * EXPECTED_RESULT = "10";
    char * result_arr = dap_itoa(INT_VAL);
    dap_assert(strcmp(result_arr, EXPECTED_RESULT) == 0,
               "Check string result from itoa");
}

static void s_test_overflow() {

}

void dap_common_test_run() {
    s_test_put_int();
    s_test_overflow();
}
