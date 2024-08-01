#include "dap_common_test.h"

static void s_test_put_int()
{
    dap_print_module_name("dap_common");
    const int INT_VAL = 10;
    const char * EXPECTED_RESULT = "10";
    char * result_arr = dap_itoa(INT_VAL);
    dap_assert(strcmp(result_arr, EXPECTED_RESULT) == 0,
               "Check string result from itoa");
}

static void s_test_overflow()
{
    dap_print_module_name("dap_overflow");
    char l_char_a = dap_maxval(l_char_a);
    short l_short_a = dap_maxval(l_short_a);
    int l_int_a = dap_maxval(l_int_a);
    long l_long_a = dap_maxval(l_long_a);
    long long l_long_long_a = dap_maxval(l_long_long_a);
    signed char l_signed_char_a = dap_maxval(l_signed_char_a);
    unsigned char l_unsigned_char_a = dap_maxval(l_unsigned_char_a);
    unsigned short l_unsigned_short_a = dap_maxval(l_unsigned_short_a);
    unsigned int l_unsigned_int_a = dap_maxval(l_unsigned_int_a);
    unsigned long l_unsigned_long_a = dap_maxval(l_unsigned_long_a);
    unsigned long long l_unsigned_long_long_a = dap_maxval(l_unsigned_long_long_a);

#ifdef DAP_CORE_TESTS
    int q = 1;
#endif
    q++;
    dap_assert(
        l_char_a == dap_add(l_char_a, (char)1) &&
        l_char_a == dap_add_builtin(l_char_a, (char)1) &&
        dap_add(l_char_a, (char)-1) == dap_add_builtin(l_char_a, (char)-1),
        "Check char ADD overflow");
    dap_assert(
        l_short_a == dap_add(l_short_a, (short)1) && 
        l_short_a == dap_add_builtin(l_short_a, (short)1) && 
        dap_add(l_short_a, (short)-1) == dap_add_builtin(l_short_a, (short)-1), 
        "Check short ADD overflow");
    dap_assert(
        l_int_a == dap_add(l_int_a, (int)1) &&
        l_int_a == dap_add_builtin(l_int_a, (int)1),
        "Check int ADD overflow");
    dap_assert(
        l_long_a == dap_add(l_long_a, (long)1) &&
        l_long_a == dap_add_builtin(l_long_a, (long)1) &&
        dap_add(l_long_a, (long)-1) == dap_add_builtin(l_long_a, (long)-1),
        "Check long ADD overflow");
    dap_assert(
        l_long_long_a == dap_add(l_long_long_a, (long long)1) &&
        l_long_long_a == dap_add_builtin(l_long_long_a, (long long)1) &&
        dap_add(l_long_long_a, (long long)-1) == dap_add_builtin(l_long_long_a, (long long)-1),
        "Check long long ADD overflow");
    dap_assert(
        l_signed_char_a == dap_add(l_signed_char_a, (signed char)1) &&
        l_signed_char_a == dap_add_builtin(l_signed_char_a, (signed char)1) &&
        dap_add(l_signed_char_a, (signed char)-1) == dap_add_builtin(l_signed_char_a, (signed char)-1),
        "Check signed char ADD overflow");
    dap_assert(l_unsigned_char_a == dap_add(l_unsigned_char_a, (unsigned char)1) && l_unsigned_char_a == dap_add_builtin(l_unsigned_char_a, (unsigned char)1), "Check unsigned char ADD overflow");
    dap_assert(l_unsigned_short_a == dap_add(l_unsigned_short_a, (unsigned short)1) && l_unsigned_short_a == dap_add_builtin(l_unsigned_short_a, (unsigned short)1), "Check unsigned short ADD overflow");
    dap_assert(l_unsigned_int_a == dap_add(l_unsigned_int_a, (unsigned int)1) && l_unsigned_int_a == dap_add_builtin(l_unsigned_int_a, (unsigned int)1), "Check unsigned int ADD overflow");
    dap_assert(l_unsigned_long_a == dap_add(l_unsigned_long_a, (unsigned long)1) && l_unsigned_long_a == dap_add_builtin(l_unsigned_long_a, (unsigned long)1), "Check unsigned long ADD overflow");
    dap_assert(l_unsigned_long_long_a == dap_add(l_unsigned_long_long_a, (unsigned long long)1) && l_unsigned_long_long_a == dap_add_builtin(l_unsigned_long_long_a, (unsigned long long)1), "Check unsigned long long ADD overflow");

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

    printf("%d %d %d %d\n", dap_sub(l_char_a, (char)1), dap_sub_builtin(l_char_a, (char)1), dap_sub(l_char_a, (char)-1), dap_sub_builtin(l_char_a, (char)-1));
    dap_assert(
        l_char_a == dap_sub(l_char_a, (char)1) &&
        l_char_a == dap_sub_builtin(l_char_a, (char)1) &&
        dap_sub(l_char_a, (char)-1) == dap_sub_builtin(l_char_a, (char)-1),
        "Check char SUB overflow");
    dap_assert(
        l_short_a == dap_sub(l_short_a, (short)1) && 
        l_short_a == dap_sub_builtin(l_short_a, (short)1) && 
        dap_sub(l_short_a, (short)-1) == dap_sub_builtin(l_short_a, (short)-1), 
        "Check short SUB overflow");
    dap_assert(
        l_int_a == dap_sub(l_int_a, (int)1) &&
        l_int_a == dap_sub_builtin(l_int_a, (int)1),
        "Check int SUB overflow");
    dap_assert(
        l_long_a == dap_sub(l_long_a, (long)1) &&
        l_long_a == dap_sub_builtin(l_long_a, (long)1) &&
        dap_sub(l_long_a, (long)-1) == dap_sub_builtin(l_long_a, (long)-1),
        "Check long SUB overflow");
    dap_assert(
        l_long_long_a == dap_sub(l_long_long_a, (long long)1) &&
        l_long_long_a == dap_sub_builtin(l_long_long_a, (long long)1) &&
        dap_sub(l_long_long_a, (long long)-1) == dap_sub_builtin(l_long_long_a, (long long)-1),
        "Check long long SUB overflow");
    dap_assert(
        l_signed_char_a == dap_sub(l_signed_char_a, (signed char)1) &&
        l_signed_char_a == dap_sub_builtin(l_signed_char_a, (signed char)1) &&
        dap_sub(l_signed_char_a, (signed char)-1) == dap_sub_builtin(l_signed_char_a, (signed char)-1),
        "Check signed char SUB overflow");
    dap_assert(l_unsigned_char_a == dap_sub(l_unsigned_char_a, (unsigned char)1) && l_unsigned_char_a == dap_sub_builtin(l_unsigned_char_a, (unsigned char)1), "Check unsigned char SUB overflow");
    dap_assert(l_unsigned_short_a == dap_sub(l_unsigned_short_a, (unsigned short)1) && l_unsigned_short_a == dap_sub_builtin(l_unsigned_short_a, (unsigned short)1), "Check unsigned short SUB overflow");
    dap_assert(l_unsigned_int_a == dap_sub(l_unsigned_int_a, (unsigned int)1) && l_unsigned_int_a == dap_sub_builtin(l_unsigned_int_a, (unsigned int)1), "Check unsigned int SUB overflow");
    dap_assert(l_unsigned_long_a == dap_sub(l_unsigned_long_a, (unsigned long)1) && l_unsigned_long_a == dap_sub_builtin(l_unsigned_long_a, (unsigned long)1), "Check unsigned long SUB overflow");
    dap_assert(l_unsigned_long_long_a == dap_sub(l_unsigned_long_long_a, (unsigned long long)1) && l_unsigned_long_long_a == dap_sub_builtin(l_unsigned_long_long_a, (unsigned long long)1), "Check unsigned long long SUB overflow");



}

static void s_test_benchmark_overflow(int a_times)
{
}

static void s_test_benchmark(int a_times)
{
    s_test_benchmark_overflow(a_times);
}
void dap_common_test_run()
{
    s_test_put_int();
    s_test_overflow();
    s_test_benchmark(1000000);
}
