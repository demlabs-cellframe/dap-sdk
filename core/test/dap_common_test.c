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
// ADD
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

    l_char_a = dap_minval(l_char_a);
    l_short_a = dap_minval(l_short_a);
    l_int_a = dap_minval(l_int_a);
    l_long_a = dap_minval(l_long_a);
    l_long_long_a = dap_minval(l_long_long_a);
    l_signed_char_a = dap_minval(l_signed_char_a);
    l_unsigned_char_a = dap_minval(l_unsigned_char_a);
    l_unsigned_short_a = dap_minval(l_unsigned_short_a);
    l_unsigned_int_a = dap_minval(l_unsigned_int_a);
    l_unsigned_long_a = dap_minval(l_unsigned_long_a);
    l_unsigned_long_long_a = dap_minval(l_unsigned_long_long_a);

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
        l_int_a == dap_sub_builtin(l_int_a, (int)1) &&
        dap_sub(l_int_a, (int)-1) == dap_sub_builtin(l_int_a, (int)-1),
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

// MUL
    l_char_a = dap_maxval(l_char_a) / 3 + 1;
    l_short_a = dap_maxval(l_short_a) / 3 + 1;
    l_int_a = dap_maxval(l_int_a) / 3 + 1;
    l_long_a = dap_maxval(l_long_a) / 3 + 1;
    l_long_long_a = dap_maxval(l_long_long_a) / 3 + 1;
    l_signed_char_a = dap_maxval(l_signed_char_a) / 3 + 1;
    l_unsigned_char_a = dap_maxval(l_unsigned_char_a) / 3 + 1;
    l_unsigned_short_a = dap_maxval(l_unsigned_short_a) / 3 + 1;
    l_unsigned_int_a = dap_maxval(l_unsigned_int_a) / 3 + 1;
    l_unsigned_long_a = dap_maxval(l_unsigned_long_a) / 3 + 1;
    l_unsigned_long_long_a = dap_maxval(l_unsigned_long_long_a) / 3 + 1;
    // signed
    dap_assert(
        0 == dap_mul(l_char_a, (char)0) &&
        l_char_a == dap_mul(l_char_a, (char)1) &&
        l_char_a * 2 == dap_mul(l_char_a, (char)2) &&
        l_char_a == dap_mul(l_char_a, (char)3) &&
        0 == dap_mul_builtin(l_char_a, (char)0) &&
        l_char_a == dap_mul_builtin(l_char_a, (char)1) &&
        l_char_a * 2 == dap_mul_builtin(l_char_a, (char)2) &&
        l_char_a == dap_mul_builtin(l_char_a, (char)3) &&
        dap_mul(l_char_a, (char)-1) == dap_mul_builtin(l_char_a, (char)-1) &&
        dap_mul(l_char_a, (char)-2) == dap_mul_builtin(l_char_a, (char)-2) &&
        dap_mul(l_char_a, (char)-3) == dap_mul_builtin(l_char_a, (char)-3),
        "Check char MUL overflow");
    dap_assert(
        0 == dap_mul(l_short_a, (short)0) &&
        l_short_a == dap_mul(l_short_a, (short)1) &&
        l_short_a * 2 == dap_mul(l_short_a, (short)2) &&
        l_short_a == dap_mul(l_short_a, (short)3) && 
        0 == dap_mul_builtin(l_short_a, (short)0) && 
        l_short_a == dap_mul_builtin(l_short_a, (short)1) && 
        l_short_a * 2 == dap_mul_builtin(l_short_a, (short)2) && 
        l_short_a == dap_mul_builtin(l_short_a, (short)3) && 
        dap_mul(l_short_a, (short)-1) == dap_mul_builtin(l_short_a, (short)-1) && 
        dap_mul(l_short_a, (short)-2) == dap_mul_builtin(l_short_a, (short)-2) &&
        dap_mul(l_short_a, (short)-3) == dap_mul_builtin(l_short_a, (short)-3), 
        "Check short MUL overflow");
    dap_assert(
        0 == dap_mul(l_int_a, (int)0) &&
        l_int_a == dap_mul(l_int_a, (int)1) &&
        l_int_a * 2 == dap_mul(l_int_a, (int)2) &&
        l_int_a == dap_mul(l_int_a, (int)3) &&
        0 == dap_mul_builtin(l_int_a, (int)0) &&
        l_int_a == dap_mul_builtin(l_int_a, (int)1) &&
        l_int_a * 2 == dap_mul_builtin(l_int_a, (int)2) &&
        l_int_a == dap_mul_builtin(l_int_a, (int)3) &&
        dap_mul(l_int_a, (int)-1) == dap_mul_builtin(l_int_a, (int)-1) &&
        dap_mul(l_int_a, (int)-2) == dap_mul_builtin(l_int_a, (int)-2) &&
        dap_mul(l_int_a, (int)-3) == dap_mul_builtin(l_int_a, (int)-3),
        "Check int MUL overflow");
    dap_assert(
        0 == dap_mul(l_long_a, (long)0) &&
        l_long_a == dap_mul(l_long_a, (long)1) &&
        l_long_a * 2== dap_mul(l_long_a, (long)2) &&
        l_long_a == dap_mul(l_long_a, (long)3) &&
        0 == dap_mul_builtin(l_long_a, (long)0) &&
        l_long_a == dap_mul_builtin(l_long_a, (long)1) &&
        l_long_a * 2== dap_mul_builtin(l_long_a, (long)2) &&
        l_long_a == dap_mul_builtin(l_long_a, (long)3) &&
        dap_mul(l_long_a, (long)-1) == dap_mul_builtin(l_long_a, (long)-1) &&
        dap_mul(l_long_a, (long)-2) == dap_mul_builtin(l_long_a, (long)-2) &&
        dap_mul(l_long_a, (long)-3) == dap_mul_builtin(l_long_a, (long)-3),
        "Check long MUL overflow");
    dap_assert(
        0 == dap_mul(l_long_long_a, (long long)0) &&
        l_long_long_a == dap_mul(l_long_long_a, (long long)1) &&
        l_long_long_a * 2 == dap_mul(l_long_long_a, (long long)2) &&
        l_long_long_a == dap_mul(l_long_long_a, (long long)3) &&
        0 == dap_mul_builtin(l_long_long_a, (long long)0) &&
        l_long_long_a == dap_mul_builtin(l_long_long_a, (long long)1) &&
        l_long_long_a * 2 == dap_mul_builtin(l_long_long_a, (long long)2) &&
        l_long_long_a == dap_mul_builtin(l_long_long_a, (long long)3) &&
        dap_mul(l_long_long_a, (long long)-1) == dap_mul_builtin(l_long_long_a, (long long)-1) &&
        dap_mul(l_long_long_a, (long long)-2) == dap_mul_builtin(l_long_long_a, (long long)-2) &&
        dap_mul(l_long_long_a, (long long)-3) == dap_mul_builtin(l_long_long_a, (long long)-3),
        "Check long long MUL overflow");
    dap_assert(
        0 == dap_mul(l_signed_char_a, (signed char)0) &&
        l_signed_char_a == dap_mul(l_signed_char_a, (signed char)1) &&
        l_signed_char_a * 2== dap_mul(l_signed_char_a, (signed char)2) &&
        l_signed_char_a == dap_mul(l_signed_char_a, (signed char)3) &&
        0 == dap_mul_builtin(l_signed_char_a, (signed char)0) &&
        l_signed_char_a == dap_mul_builtin(l_signed_char_a, (signed char)1) &&
        l_signed_char_a * 2 == dap_mul_builtin(l_signed_char_a, (signed char)2) &&
        l_signed_char_a == dap_mul_builtin(l_signed_char_a, (signed char)3) &&
        dap_mul(l_signed_char_a, (signed char)-1) == dap_mul_builtin(l_signed_char_a, (signed char)-1) &&
        dap_mul(l_signed_char_a, (signed char)-2) == dap_mul_builtin(l_signed_char_a, (signed char)-2) &&
        dap_mul(l_signed_char_a, (signed char)-3) == dap_mul_builtin(l_signed_char_a, (signed char)-3),
        "Check signed char MUL overflow");

    // unsigned
    dap_assert(
        0 == dap_mul(l_unsigned_char_a, (unsigned char)0) &&
        l_unsigned_char_a == dap_mul(l_unsigned_char_a, (unsigned char)1) &&
        l_unsigned_char_a * 2== dap_mul(l_unsigned_char_a, (unsigned char)2) &&
        l_unsigned_char_a == dap_mul(l_unsigned_char_a, (unsigned char)3) &&
        0 == dap_mul_builtin(l_unsigned_char_a, (unsigned char)0) &&
        l_unsigned_char_a == dap_mul_builtin(l_unsigned_char_a, (unsigned char)1) &&
        l_unsigned_char_a * 2 == dap_mul_builtin(l_unsigned_char_a, (unsigned char)2) &&
        l_unsigned_char_a == dap_mul_builtin(l_unsigned_char_a, (unsigned char)3),
        "Check unsigned char MUL overflow");
    dap_assert(
        0 == dap_mul(l_unsigned_short_a, (unsigned short)0) &&
        l_unsigned_short_a == dap_mul(l_unsigned_short_a, (unsigned short)1) &&
        l_unsigned_short_a * 2== dap_mul(l_unsigned_short_a, (unsigned short)2) &&
        l_unsigned_short_a == dap_mul(l_unsigned_short_a, (unsigned short)3) &&
        0 == dap_mul_builtin(l_unsigned_short_a, (unsigned short)0) &&
        l_unsigned_short_a == dap_mul_builtin(l_unsigned_short_a, (unsigned short)1) &&
        l_unsigned_short_a * 2 == dap_mul_builtin(l_unsigned_short_a, (unsigned short)2) &&
        l_unsigned_short_a == dap_mul_builtin(l_unsigned_short_a, (unsigned short)3),
        "Check unsigned short MUL overflow");
    dap_assert(
        0 == dap_mul(l_unsigned_int_a, (unsigned int)0) &&
        l_unsigned_int_a == dap_mul(l_unsigned_int_a, (unsigned int)1) &&
        l_unsigned_int_a * 2 == dap_mul(l_unsigned_int_a, (unsigned int)2) &&
        l_unsigned_int_a == dap_mul(l_unsigned_int_a, (unsigned int)3) &&
        0 == dap_mul_builtin(l_unsigned_int_a, (unsigned int)0) &&
        l_unsigned_int_a == dap_mul_builtin(l_unsigned_int_a, (unsigned int)1) &&
        l_unsigned_int_a * 2 == dap_mul_builtin(l_unsigned_int_a, (unsigned int)2) &&
        l_unsigned_int_a == dap_mul_builtin(l_unsigned_int_a, (unsigned int)3),
        "Check unsigned int MUL overflow");
    dap_assert(
        0 == dap_mul(l_unsigned_long_a, (unsigned long)0) &&
        l_unsigned_long_a == dap_mul(l_unsigned_long_a, (unsigned long)1) &&
        l_unsigned_long_a * 2 == dap_mul(l_unsigned_long_a, (unsigned long)2) &&
        l_unsigned_long_a == dap_mul(l_unsigned_long_a, (unsigned long)3) &&
        0 == dap_mul_builtin(l_unsigned_long_a, (unsigned long)0) &&
        l_unsigned_long_a == dap_mul_builtin(l_unsigned_long_a, (unsigned long)1) &&
        l_unsigned_long_a * 2 == dap_mul_builtin(l_unsigned_long_a, (unsigned long)2) &&
        l_unsigned_long_a == dap_mul_builtin(l_unsigned_long_a, (unsigned long)3),
        "Check unsigned long MUL overflow");
    dap_assert(
        0 == dap_mul(l_unsigned_long_long_a, (unsigned long long)0) &&
        l_unsigned_long_long_a == dap_mul(l_unsigned_long_long_a, (unsigned long long)1) &&
        l_unsigned_long_long_a * 2 == dap_mul(l_unsigned_long_long_a, (unsigned long long)2) &&
        l_unsigned_long_long_a == dap_mul(l_unsigned_long_long_a, (unsigned long long)3) &&
        0 == dap_mul_builtin(l_unsigned_long_long_a, (unsigned long long)0) &&
        l_unsigned_long_long_a == dap_mul_builtin(l_unsigned_long_long_a, (unsigned long long)1) &&
        l_unsigned_long_long_a * 2 == dap_mul_builtin(l_unsigned_long_long_a, (unsigned long long)2) &&
        l_unsigned_long_long_a == dap_mul_builtin(l_unsigned_long_long_a, (unsigned long long)3),
        "Check unsigned long long MUL overflow");
}

static void s_test_benchmark_overflow_add(long a_times)
{
    dap_print_module_name("dap_benchmark_overflow_add");
    char l_char_a = dap_maxval(l_char_a);
    long long l_long_long_a = dap_maxval(l_long_long_a);
    unsigned char l_unsigned_char_a = dap_maxval(l_unsigned_char_a);
    unsigned long long l_unsigned_long_long_a = dap_maxval(l_unsigned_long_long_a);
    
    char l_msg[120] = {0};
    int l_cur_1 = 0, l_cur_2 = 0, l_builtin = 0;


    l_cur_1 = get_cur_time_msec();
    for (long i = 0; i < a_times; ++i)
        dap_add(l_char_a, (char)1);
    l_cur_2 = get_cur_time_msec();
    for (long i = 0; i < a_times; ++i)
        dap_add_builtin(l_char_a, (char)1);
    l_builtin = get_cur_time_msec();

    sprintf(l_msg, "Check overflow %d times to custom char", a_times);
    benchmark_mgs_time(l_msg, l_cur_2 - l_cur_1);
    sprintf(l_msg, "Check overflow %d times to __builtin char", a_times);
    benchmark_mgs_time(l_msg, l_builtin - l_cur_2);


    l_cur_1 = get_cur_time_msec();
    for (long i = 0; i < a_times; ++i)
        dap_add(l_long_long_a, (long long)1);
    l_cur_2 = get_cur_time_msec();
    for (long i = 0; i < a_times; ++i)
        dap_add_builtin(l_long_long_a, (long long)1);
    l_builtin = get_cur_time_msec();

    sprintf(l_msg, "Check overflow %d times to custom long long", a_times);
    benchmark_mgs_time(l_msg, l_cur_2 - l_cur_1);
    sprintf(l_msg, "Check overflow %d times to __builtin long long", a_times);
    benchmark_mgs_time(l_msg, l_builtin - l_cur_2);


    l_cur_1 = get_cur_time_msec();
    for (long i = 0; i < a_times; ++i)
        dap_add(l_unsigned_char_a, (unsigned char)1);
    l_cur_2 = get_cur_time_msec();
    for (long i = 0; i < a_times; ++i)
        dap_add_builtin(l_unsigned_char_a, (unsigned char)1);
    l_builtin = get_cur_time_msec();

    sprintf(l_msg, "Check overflow %d times to custom  unsigned char", a_times);
    benchmark_mgs_time(l_msg, l_cur_2 - l_cur_1);
    sprintf(l_msg, "Check overflow %d times to __builtin unsigned char", a_times);
    benchmark_mgs_time(l_msg, l_builtin - l_cur_2);


    l_cur_1 = get_cur_time_msec();
    for (long i = 0; i < a_times; ++i)
        dap_add(l_unsigned_long_long_a, (unsigned long long)1);
    l_cur_2 = get_cur_time_msec();
    for (long i = 0; i < a_times; ++i)
        dap_add_builtin(l_unsigned_long_long_a, (unsigned long long)1);
    l_builtin = get_cur_time_msec();

    sprintf(l_msg, "Check overflow %d times to custom unsigned long long", a_times);
    benchmark_mgs_time(l_msg, l_cur_2 - l_cur_1);
    sprintf(l_msg, "Check overflow %d times to __builtin unsigned long long", a_times);
    benchmark_mgs_time(l_msg, l_builtin - l_cur_2);

}

static void s_test_benchmark_overflow_sub(long a_times)
{
    dap_print_module_name("dap_benchmark_overflow_sub");
    char l_char_a = dap_minval(l_char_a);
    long long l_long_long_a = dap_minval(l_long_long_a);
    unsigned char l_unsigned_char_a = dap_minval(l_unsigned_char_a);
    unsigned long long l_unsigned_long_long_a = dap_minval(l_unsigned_long_long_a);
    
    char l_msg[120] = {0};
    int l_cur_1 = 0, l_cur_2 = 0, l_builtin = 0;


    l_cur_1 = get_cur_time_msec();
    for (long i = 0; i < a_times; ++i)
        dap_sub(l_char_a, (char)1);
    l_cur_2 = get_cur_time_msec();
    for (long i = 0; i < a_times; ++i)
        dap_sub_builtin(l_char_a, (char)1);
    l_builtin = get_cur_time_msec();

    sprintf(l_msg, "Check overflow %d times to custom char", a_times);
    benchmark_mgs_time(l_msg, l_cur_2 - l_cur_1);
    sprintf(l_msg, "Check overflow %d times to __builtin char", a_times);
    benchmark_mgs_time(l_msg, l_builtin - l_cur_2);


    l_cur_1 = get_cur_time_msec();
    for (long i = 0; i < a_times; ++i)
        dap_sub(l_long_long_a, (long long)1);
    l_cur_2 = get_cur_time_msec();
    for (long i = 0; i < a_times; ++i)
        dap_sub_builtin(l_long_long_a, (long long)1);
    l_builtin = get_cur_time_msec();

    sprintf(l_msg, "Check overflow %d times to custom long long", a_times);
    benchmark_mgs_time(l_msg, l_cur_2 - l_cur_1);
    sprintf(l_msg, "Check overflow %d times to __builtin long long", a_times);
    benchmark_mgs_time(l_msg, l_builtin - l_cur_2);


    l_cur_1 = get_cur_time_msec();
    for (long i = 0; i < a_times; ++i)
        dap_sub(l_unsigned_char_a, (unsigned char)1);
    l_cur_2 = get_cur_time_msec();
    for (long i = 0; i < a_times; ++i)
        dap_sub_builtin(l_unsigned_char_a, (unsigned char)1);
    l_builtin = get_cur_time_msec();

    sprintf(l_msg, "Check overflow %d times to custom  unsigned char", a_times);
    benchmark_mgs_time(l_msg, l_cur_2 - l_cur_1);
    sprintf(l_msg, "Check overflow %d times to __builtin unsigned char", a_times);
    benchmark_mgs_time(l_msg, l_builtin - l_cur_2);


    l_cur_1 = get_cur_time_msec();
    for (long i = 0; i < a_times; ++i)
        dap_sub(l_unsigned_long_long_a, (unsigned long long)1);
    l_cur_2 = get_cur_time_msec();
    for (long i = 0; i < a_times; ++i)
        dap_sub_builtin(l_unsigned_long_long_a, (unsigned long long)1);
    l_builtin = get_cur_time_msec();

    sprintf(l_msg, "Check overflow %d times to custom unsigned long long", a_times);
    benchmark_mgs_time(l_msg, l_cur_2 - l_cur_1);
    sprintf(l_msg, "Check overflow %d times to __builtin unsigned long long", a_times);
    benchmark_mgs_time(l_msg, l_builtin - l_cur_2);

}

static void s_test_benchmark_overflow_mul(long a_times)
{
    dap_print_module_name("dap_benchmark_overflow_mul");
    char l_char_a = dap_maxval(l_char_a) / 2 + 1;
    long long l_long_long_a = dap_maxval(l_long_long_a) / 2 + 1;
    unsigned char l_unsigned_char_a = dap_maxval(l_unsigned_char_a) / 2 + 1;
    unsigned long long l_unsigned_long_long_a = dap_maxval(l_unsigned_long_long_a) / 2 + 1;
    
    char l_msg[120] = {0};
    int l_cur_1 = 0, l_cur_2 = 0, l_builtin = 0;


    l_cur_1 = get_cur_time_msec();
    for (long i = 0; i < a_times; ++i)
        dap_mul(l_char_a, (char)2);
    l_cur_2 = get_cur_time_msec();
    for (long i = 0; i < a_times; ++i)
        dap_mul_builtin(l_char_a, (char)2);
    l_builtin = get_cur_time_msec();

    sprintf(l_msg, "Check overflow %d times to custom char", a_times);
    benchmark_mgs_time(l_msg, l_cur_2 - l_cur_1);
    sprintf(l_msg, "Check overflow %d times to __builtin char", a_times);
    benchmark_mgs_time(l_msg, l_builtin - l_cur_2);


    l_cur_1 = get_cur_time_msec();
    for (long i = 0; i < a_times; ++i)
        dap_mul(l_long_long_a, (long long)2);
    l_cur_2 = get_cur_time_msec();
    for (long i = 0; i < a_times; ++i)
        dap_mul_builtin(l_long_long_a, (long long)2);
    l_builtin = get_cur_time_msec();

    sprintf(l_msg, "Check overflow %d times to custom long long", a_times);
    benchmark_mgs_time(l_msg, l_cur_2 - l_cur_1);
    sprintf(l_msg, "Check overflow %d times to __builtin long long", a_times);
    benchmark_mgs_time(l_msg, l_builtin - l_cur_2);


    l_cur_1 = get_cur_time_msec();
    for (long i = 0; i < a_times; ++i)
        dap_mul(l_unsigned_char_a, (unsigned char)2);
    l_cur_2 = get_cur_time_msec();
    for (long i = 0; i < a_times; ++i)
        dap_mul_builtin(l_unsigned_char_a, (unsigned char)2);
    l_builtin = get_cur_time_msec();

    sprintf(l_msg, "Check overflow %d times to custom  unsigned char", a_times);
    benchmark_mgs_time(l_msg, l_cur_2 - l_cur_1);
    sprintf(l_msg, "Check overflow %d times to __builtin unsigned char", a_times);
    benchmark_mgs_time(l_msg, l_builtin - l_cur_2);


    l_cur_1 = get_cur_time_msec();
    for (long i = 0; i < a_times; ++i)
        dap_mul(l_unsigned_long_long_a, (unsigned long long)2);
    l_cur_2 = get_cur_time_msec();
    for (long i = 0; i < a_times; ++i)
        dap_mul_builtin(l_unsigned_long_long_a, (unsigned long long)2);
    l_builtin = get_cur_time_msec();

    sprintf(l_msg, "Check overflow %d times to custom unsigned long long", a_times);
    benchmark_mgs_time(l_msg, l_cur_2 - l_cur_1);
    sprintf(l_msg, "Check overflow %d times to __builtin unsigned long long", a_times);
    benchmark_mgs_time(l_msg, l_builtin - l_cur_2);

}

static void s_test_benchmark_overflow(long a_times)
{
    s_test_benchmark_overflow_add(a_times);
    s_test_benchmark_overflow_sub(a_times);
    s_test_benchmark_overflow_mul(a_times);
}

static void s_test_benchmark(int a_times)
{
    s_test_benchmark_overflow(a_times);
}
void dap_common_test_run()
{
    s_test_put_int();
    s_test_overflow();
    s_test_benchmark(100);
}
