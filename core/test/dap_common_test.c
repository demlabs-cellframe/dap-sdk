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
    signed char l_signed_char_a = dap_maxval(l_signed_char_a);
    char l_char_a = dap_maxval(l_char_a);
    unsigned char l_unsigned_char_a = dap_maxval(l_unsigned_char_a);
    short l_short_a = dap_maxval(l_short_a);
    unsigned short l_unsigned_short_a = dap_maxval(l_unsigned_short_a);
    int l_int_a = dap_maxval(l_int_a);
    unsigned int l_unsigned_int_a = dap_maxval(l_unsigned_int_a);
    long l_long_a = dap_maxval(l_long_a);
    unsigned long l_unsigned_long_a = dap_maxval(l_unsigned_long_a);
    long long l_long_long_a = dap_maxval(l_long_long_a);
    unsigned long long l_unsigned_long_long_a = dap_maxval(l_unsigned_long_long_a);

    dap_assert(l_signed_char_a == dap_add(l_signed_char_a, 1), "Check signed char add overflow");
    dap_assert(l_char_a == dap_add(l_char_a, 1), "Check char add overflow");
    dap_assert(l_unsigned_char_a == dap_add(l_unsigned_char_a, 1), "Check unsigned char add overflow");
    dap_assert(l_short_a == dap_add(l_short_a, 1), "Check short add overflow");
    dap_assert(l_unsigned_short_a == dap_add(l_unsigned_short_a, 1), "Check unsigned short add overflow");
    dap_assert(l_int_a == dap_add(l_int_a, 1), "Check int add overflow");
    dap_assert(l_unsigned_int_a == dap_add(l_unsigned_int_a, 1), "Check unsigned int add overflow");
    dap_assert(l_long_a == dap_add(l_long_a, 1), "Check long add overflow");
    dap_assert(l_unsigned_long_a == dap_add(l_unsigned_long_a, 1), "Check unsigned long add overflow");
    dap_assert(l_long_long_a == dap_add(l_long_long_a, 1), "Check long long add overflow");
    dap_assert(l_unsigned_long_long_a == dap_add(l_unsigned_long_long_a, 1), "Check unsigned long long add overflow");

    l_signed_char_a = dap_minval(l_signed_char_a);
    l_char_a = dap_minval(l_char_a);
    l_unsigned_char_a = dap_minval(l_unsigned_char_a);
    l_short_a = dap_minval(l_short_a);
    l_unsigned_short_a = dap_minval(l_unsigned_short_a);
    l_int_a = dap_minval(l_int_a);
    l_unsigned_int_a = dap_minval(l_unsigned_int_a);
    l_long_a = dap_minval(l_long_a);
    l_unsigned_long_a = dap_minval(l_unsigned_long_a);
    l_long_long_a = dap_minval(l_long_long_a);
    l_unsigned_long_long_a = dap_minval(l_unsigned_long_long_a);

    dap_assert(l_signed_char_a == dap_sub(l_signed_char_a, 1), "Check signed char sub overflow");
    dap_assert(l_char_a == dap_sub(l_char_a, 1), "Check char sub overflow");
    dap_assert(l_unsigned_char_a == dap_sub(l_unsigned_char_a, 1), "Check unsigned char sub overflow");
    dap_assert(l_short_a == dap_sub(l_short_a, 1), "Check short sub overflow");
    dap_assert(l_unsigned_short_a == dap_sub(l_unsigned_short_a, 1), "Check unsigned short sub overflow");
    dap_assert(l_int_a == dap_sub(l_int_a, 1), "Check int sub overflow");
    dap_assert(l_unsigned_int_a == dap_sub(l_unsigned_int_a, 1), "Check unsigned int sub overflow");
    dap_assert(l_long_a == dap_sub(l_long_a, 1), "Check long sub overflow");
    dap_assert(l_unsigned_long_a == dap_sub(l_unsigned_long_a, 1), "Check unsigned long sub overflow");
    dap_assert(l_long_long_a == dap_sub(l_long_long_a, 1), "Check long long sub overflow");
    dap_assert(l_unsigned_long_long_a == dap_sub(l_unsigned_long_long_a, 1), "Check unsigned long long sub overflow");
}

void dap_common_test_run() {
    s_test_put_int();
    s_test_overflow();
}
