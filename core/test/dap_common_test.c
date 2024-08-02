#include "dap_common_test.h"


#define LOG_TAG "dap_common_test"

static const uint64_t s_array_size = 1024 * 1024 * sizeof(long long) / sizeof(char); // benchmarks array size 8MB

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
    char l_char = dap_maxval(l_char);
    short l_short = dap_maxval(l_short);
    int l_int = dap_maxval(l_int);
    long l_long = dap_maxval(l_long);
    long long l_long_long = dap_maxval(l_long_long);
    signed char l_signed_char = dap_maxval(l_signed_char);
    unsigned char l_unsigned_char = dap_maxval(l_unsigned_char);
    unsigned short l_unsigned_short = dap_maxval(l_unsigned_short);
    unsigned int l_unsigned_int = dap_maxval(l_unsigned_int);
    unsigned long l_unsigned_long = dap_maxval(l_unsigned_long);
    unsigned long long l_unsigned_long_long = dap_maxval(l_unsigned_long_long);
// base tests
    // char ADD
    for (int i = dap_minval(l_char); i <= dap_maxval(l_char); ++i)
        for (int j = dap_minval(l_char); j <= dap_maxval(l_char); ++j) 
            dap_assert_PIF(dap_add((char)i, (char)j) == dap_add_builtin((char)i, (char)j), "Base char ADD test");
    // unsigned char ADD
    for (unsigned int i = dap_minval(l_unsigned_char); i <= dap_maxval(l_unsigned_char); ++i)
        for (unsigned int j = dap_minval(l_unsigned_char); j <= dap_maxval(l_unsigned_char); ++j)
            dap_assert_PIF(dap_add((unsigned char)i, (unsigned char)j) == dap_add_builtin((unsigned char)i, (unsigned char)j), "Base unsigned char ADD test");
    
    // char SUB
    for (int i = dap_minval(l_char); i <= dap_maxval(l_char); ++i)
        for (int j = dap_minval(l_char); j <= dap_maxval(l_char); ++j) 
            dap_assert_PIF(dap_sub((char)i, (char)j) == dap_sub_builtin((char)i, (char)j), "Base char SUB test");
    // unsigned char SUB
    for (unsigned int i = dap_minval(l_unsigned_char); i <= dap_maxval(l_unsigned_char); ++i)
        for (unsigned int j = dap_minval(l_unsigned_char); j <= dap_maxval(l_unsigned_char); ++j)
            dap_assert_PIF(dap_sub((unsigned char)i, (unsigned char)j) == dap_sub_builtin((unsigned char)i, (unsigned char)j), "Base unsigned char SUB test");
    
    // char MUL
    for (int i = dap_minval(l_char); i <= dap_maxval(l_char); ++i)
        for (int j = dap_minval(l_char); j <= dap_maxval(l_char); ++j) 
            dap_assert_PIF(dap_mul((char)i, (char)j) == dap_mul_builtin((char)i, (char)j), "Base char MUL test");
    // unsigned char MUL
    for (unsigned int i = dap_minval(l_unsigned_char); i <= dap_maxval(l_unsigned_char); ++i)
        for (unsigned int j = dap_minval(l_unsigned_char); j <= dap_maxval(l_unsigned_char); ++j)
            dap_assert_PIF(dap_mul((unsigned char)i, (unsigned char)j) == dap_mul_builtin((unsigned char)i, (unsigned char)j), "Base unsigned char MUL test");

// ADD
    dap_assert(
        l_char == dap_add(l_char, (char)1) &&
        l_char == dap_add_builtin(l_char, (char)1) &&
        dap_add(l_char, (char)-1) == dap_add_builtin(l_char, (char)-1),
        "Check char ADD overflow");
    dap_assert(
        l_short == dap_add(l_short, (short)1) && 
        l_short == dap_add_builtin(l_short, (short)1) && 
        dap_add(l_short, (short)-1) == dap_add_builtin(l_short, (short)-1), 
        "Check short ADD overflow");
    dap_assert(
        l_int == dap_add(l_int, (int)1) &&
        l_int == dap_add_builtin(l_int, (int)1),
        "Check int ADD overflow");
    dap_assert(
        l_long == dap_add(l_long, (long)1) &&
        l_long == dap_add_builtin(l_long, (long)1) &&
        dap_add(l_long, (long)-1) == dap_add_builtin(l_long, (long)-1),
        "Check long ADD overflow");
    dap_assert(
        l_long_long == dap_add(l_long_long, (long long)1) &&
        l_long_long == dap_add_builtin(l_long_long, (long long)1) &&
        dap_add(l_long_long, (long long)-1) == dap_add_builtin(l_long_long, (long long)-1),
        "Check long long ADD overflow");
    dap_assert(
        l_signed_char == dap_add(l_signed_char, (signed char)1) &&
        l_signed_char == dap_add_builtin(l_signed_char, (signed char)1) &&
        dap_add(l_signed_char, (signed char)-1) == dap_add_builtin(l_signed_char, (signed char)-1),
        "Check signed char ADD overflow");
    dap_assert(l_unsigned_char == dap_add(l_unsigned_char, (unsigned char)1) && l_unsigned_char == dap_add_builtin(l_unsigned_char, (unsigned char)1), "Check unsigned char ADD overflow");
    dap_assert(l_unsigned_short == dap_add(l_unsigned_short, (unsigned short)1) && l_unsigned_short == dap_add_builtin(l_unsigned_short, (unsigned short)1), "Check unsigned short ADD overflow");
    dap_assert(l_unsigned_int == dap_add(l_unsigned_int, (unsigned int)1) && l_unsigned_int == dap_add_builtin(l_unsigned_int, (unsigned int)1), "Check unsigned int ADD overflow");
    dap_assert(l_unsigned_long == dap_add(l_unsigned_long, (unsigned long)1) && l_unsigned_long == dap_add_builtin(l_unsigned_long, (unsigned long)1), "Check unsigned long ADD overflow");
    dap_assert(l_unsigned_long_long == dap_add(l_unsigned_long_long, (unsigned long long)1) && l_unsigned_long_long == dap_add_builtin(l_unsigned_long_long, (unsigned long long)1), "Check unsigned long long ADD overflow");

    l_char = dap_minval(l_char);
    l_short = dap_minval(l_short);
    l_int = dap_minval(l_int);
    l_long = dap_minval(l_long);
    l_long_long = dap_minval(l_long_long);
    l_signed_char = dap_minval(l_signed_char);
    l_unsigned_char = dap_minval(l_unsigned_char);
    l_unsigned_short = dap_minval(l_unsigned_short);
    l_unsigned_int = dap_minval(l_unsigned_int);
    l_unsigned_long = dap_minval(l_unsigned_long);
    l_unsigned_long_long = dap_minval(l_unsigned_long_long);

    dap_assert(
        l_char == dap_sub(l_char, (char)1) &&
        l_char == dap_sub_builtin(l_char, (char)1) &&
        dap_sub(l_char, (char)-1) == dap_sub_builtin(l_char, (char)-1),
        "Check char SUB overflow");
    dap_assert(
        l_short == dap_sub(l_short, (short)1) && 
        l_short == dap_sub_builtin(l_short, (short)1) && 
        dap_sub(l_short, (short)-1) == dap_sub_builtin(l_short, (short)-1), 
        "Check short SUB overflow");
    dap_assert(
        l_int == dap_sub(l_int, (int)1) &&
        l_int == dap_sub_builtin(l_int, (int)1) &&
        dap_sub(l_int, (int)-1) == dap_sub_builtin(l_int, (int)-1),
        "Check int SUB overflow");
    dap_assert(
        l_long == dap_sub(l_long, (long)1) &&
        l_long == dap_sub_builtin(l_long, (long)1) &&
        dap_sub(l_long, (long)-1) == dap_sub_builtin(l_long, (long)-1),
        "Check long SUB overflow");
    dap_assert(
        l_long_long == dap_sub(l_long_long, (long long)1) &&
        l_long_long == dap_sub_builtin(l_long_long, (long long)1) &&
        dap_sub(l_long_long, (long long)-1) == dap_sub_builtin(l_long_long, (long long)-1),
        "Check long long SUB overflow");
    dap_assert(
        l_signed_char == dap_sub(l_signed_char, (signed char)1) &&
        l_signed_char == dap_sub_builtin(l_signed_char, (signed char)1) &&
        dap_sub(l_signed_char, (signed char)-1) == dap_sub_builtin(l_signed_char, (signed char)-1),
        "Check signed char SUB overflow");
    dap_assert(l_unsigned_char == dap_sub(l_unsigned_char, (unsigned char)1) && l_unsigned_char == dap_sub_builtin(l_unsigned_char, (unsigned char)1), "Check unsigned char SUB overflow");
    dap_assert(l_unsigned_short == dap_sub(l_unsigned_short, (unsigned short)1) && l_unsigned_short == dap_sub_builtin(l_unsigned_short, (unsigned short)1), "Check unsigned short SUB overflow");
    dap_assert(l_unsigned_int == dap_sub(l_unsigned_int, (unsigned int)1) && l_unsigned_int == dap_sub_builtin(l_unsigned_int, (unsigned int)1), "Check unsigned int SUB overflow");
    dap_assert(l_unsigned_long == dap_sub(l_unsigned_long, (unsigned long)1) && l_unsigned_long == dap_sub_builtin(l_unsigned_long, (unsigned long)1), "Check unsigned long SUB overflow");
    dap_assert(l_unsigned_long_long == dap_sub(l_unsigned_long_long, (unsigned long long)1) && l_unsigned_long_long == dap_sub_builtin(l_unsigned_long_long, (unsigned long long)1), "Check unsigned long long SUB overflow");

// MUL
    l_char = dap_maxval(l_char) / 3 + 1;
    l_short = dap_maxval(l_short) / 3 + 1;
    l_int = dap_maxval(l_int) / 3 + 1;
    l_long = dap_maxval(l_long) / 3 + 1;
    l_long_long = dap_maxval(l_long_long) / 3 + 1;
    l_signed_char = dap_maxval(l_signed_char) / 3 + 1;
    l_unsigned_char = dap_maxval(l_unsigned_char) / 3 + 1;
    l_unsigned_short = dap_maxval(l_unsigned_short) / 3 + 1;
    l_unsigned_int = dap_maxval(l_unsigned_int) / 3 + 1;
    l_unsigned_long = dap_maxval(l_unsigned_long) / 3 + 1;
    l_unsigned_long_long = dap_maxval(l_unsigned_long_long) / 3 + 1;
    // signed
    dap_assert(
        0 == dap_mul(l_char, (char)0) &&
        l_char == dap_mul(l_char, (char)1) &&
        l_char * 2 == dap_mul(l_char, (char)2) &&
        l_char == dap_mul(l_char, (char)3) &&
        0 == dap_mul_builtin(l_char, (char)0) &&
        l_char == dap_mul_builtin(l_char, (char)1) &&
        l_char * 2 == dap_mul_builtin(l_char, (char)2) &&
        l_char == dap_mul_builtin(l_char, (char)3) &&
        dap_mul(l_char, (char)-1) == dap_mul_builtin(l_char, (char)-1) &&
        dap_mul(l_char, (char)-2) == dap_mul_builtin(l_char, (char)-2) &&
        dap_mul(l_char, (char)-3) == dap_mul_builtin(l_char, (char)-3),
        "Check char MUL overflow");
    dap_assert(
        0 == dap_mul(l_short, (short)0) &&
        l_short == dap_mul(l_short, (short)1) &&
        l_short * 2 == dap_mul(l_short, (short)2) &&
        l_short == dap_mul(l_short, (short)3) && 
        0 == dap_mul_builtin(l_short, (short)0) && 
        l_short == dap_mul_builtin(l_short, (short)1) && 
        l_short * 2 == dap_mul_builtin(l_short, (short)2) && 
        l_short == dap_mul_builtin(l_short, (short)3) && 
        dap_mul(l_short, (short)-1) == dap_mul_builtin(l_short, (short)-1) && 
        dap_mul(l_short, (short)-2) == dap_mul_builtin(l_short, (short)-2) &&
        dap_mul(l_short, (short)-3) == dap_mul_builtin(l_short, (short)-3), 
        "Check short MUL overflow");
    dap_assert(
        0 == dap_mul(l_int, (int)0) &&
        l_int == dap_mul(l_int, (int)1) &&
        l_int * 2 == dap_mul(l_int, (int)2) &&
        l_int == dap_mul(l_int, (int)3) &&
        0 == dap_mul_builtin(l_int, (int)0) &&
        l_int == dap_mul_builtin(l_int, (int)1) &&
        l_int * 2 == dap_mul_builtin(l_int, (int)2) &&
        l_int == dap_mul_builtin(l_int, (int)3) &&
        dap_mul(l_int, (int)-1) == dap_mul_builtin(l_int, (int)-1) &&
        dap_mul(l_int, (int)-2) == dap_mul_builtin(l_int, (int)-2) &&
        dap_mul(l_int, (int)-3) == dap_mul_builtin(l_int, (int)-3),
        "Check int MUL overflow");
    dap_assert(
        0 == dap_mul(l_long, (long)0) &&
        l_long == dap_mul(l_long, (long)1) &&
        l_long * 2== dap_mul(l_long, (long)2) &&
        l_long == dap_mul(l_long, (long)3) &&
        0 == dap_mul_builtin(l_long, (long)0) &&
        l_long == dap_mul_builtin(l_long, (long)1) &&
        l_long * 2== dap_mul_builtin(l_long, (long)2) &&
        l_long == dap_mul_builtin(l_long, (long)3) &&
        dap_mul(l_long, (long)-1) == dap_mul_builtin(l_long, (long)-1) &&
        dap_mul(l_long, (long)-2) == dap_mul_builtin(l_long, (long)-2) &&
        dap_mul(l_long, (long)-3) == dap_mul_builtin(l_long, (long)-3),
        "Check long MUL overflow");
    dap_assert(
        0 == dap_mul(l_long_long, (long long)0) &&
        l_long_long == dap_mul(l_long_long, (long long)1) &&
        l_long_long * 2 == dap_mul(l_long_long, (long long)2) &&
        l_long_long == dap_mul(l_long_long, (long long)3) &&
        0 == dap_mul_builtin(l_long_long, (long long)0) &&
        l_long_long == dap_mul_builtin(l_long_long, (long long)1) &&
        l_long_long * 2 == dap_mul_builtin(l_long_long, (long long)2) &&
        l_long_long == dap_mul_builtin(l_long_long, (long long)3) &&
        dap_mul(l_long_long, (long long)-1) == dap_mul_builtin(l_long_long, (long long)-1) &&
        dap_mul(l_long_long, (long long)-2) == dap_mul_builtin(l_long_long, (long long)-2) &&
        dap_mul(l_long_long, (long long)-3) == dap_mul_builtin(l_long_long, (long long)-3),
        "Check long long MUL overflow");
    dap_assert(
        0 == dap_mul(l_signed_char, (signed char)0) &&
        l_signed_char == dap_mul(l_signed_char, (signed char)1) &&
        l_signed_char * 2== dap_mul(l_signed_char, (signed char)2) &&
        l_signed_char == dap_mul(l_signed_char, (signed char)3) &&
        0 == dap_mul_builtin(l_signed_char, (signed char)0) &&
        l_signed_char == dap_mul_builtin(l_signed_char, (signed char)1) &&
        l_signed_char * 2 == dap_mul_builtin(l_signed_char, (signed char)2) &&
        l_signed_char == dap_mul_builtin(l_signed_char, (signed char)3) &&
        dap_mul(l_signed_char, (signed char)-1) == dap_mul_builtin(l_signed_char, (signed char)-1) &&
        dap_mul(l_signed_char, (signed char)-2) == dap_mul_builtin(l_signed_char, (signed char)-2) &&
        dap_mul(l_signed_char, (signed char)-3) == dap_mul_builtin(l_signed_char, (signed char)-3),
        "Check signed char MUL overflow");

    // unsigned
    dap_assert(
        0 == dap_mul(l_unsigned_char, (unsigned char)0) &&
        l_unsigned_char == dap_mul(l_unsigned_char, (unsigned char)1) &&
        l_unsigned_char * 2== dap_mul(l_unsigned_char, (unsigned char)2) &&
        l_unsigned_char == dap_mul(l_unsigned_char, (unsigned char)3) &&
        0 == dap_mul_builtin(l_unsigned_char, (unsigned char)0) &&
        l_unsigned_char == dap_mul_builtin(l_unsigned_char, (unsigned char)1) &&
        l_unsigned_char * 2 == dap_mul_builtin(l_unsigned_char, (unsigned char)2) &&
        l_unsigned_char == dap_mul_builtin(l_unsigned_char, (unsigned char)3),
        "Check unsigned char MUL overflow");
    dap_assert(
        0 == dap_mul(l_unsigned_short, (unsigned short)0) &&
        l_unsigned_short == dap_mul(l_unsigned_short, (unsigned short)1) &&
        l_unsigned_short * 2== dap_mul(l_unsigned_short, (unsigned short)2) &&
        l_unsigned_short == dap_mul(l_unsigned_short, (unsigned short)3) &&
        0 == dap_mul_builtin(l_unsigned_short, (unsigned short)0) &&
        l_unsigned_short == dap_mul_builtin(l_unsigned_short, (unsigned short)1) &&
        l_unsigned_short * 2 == dap_mul_builtin(l_unsigned_short, (unsigned short)2) &&
        l_unsigned_short == dap_mul_builtin(l_unsigned_short, (unsigned short)3),
        "Check unsigned short MUL overflow");
    dap_assert(
        0 == dap_mul(l_unsigned_int, (unsigned int)0) &&
        l_unsigned_int == dap_mul(l_unsigned_int, (unsigned int)1) &&
        l_unsigned_int * 2 == dap_mul(l_unsigned_int, (unsigned int)2) &&
        l_unsigned_int == dap_mul(l_unsigned_int, (unsigned int)3) &&
        0 == dap_mul_builtin(l_unsigned_int, (unsigned int)0) &&
        l_unsigned_int == dap_mul_builtin(l_unsigned_int, (unsigned int)1) &&
        l_unsigned_int * 2 == dap_mul_builtin(l_unsigned_int, (unsigned int)2) &&
        l_unsigned_int == dap_mul_builtin(l_unsigned_int, (unsigned int)3),
        "Check unsigned int MUL overflow");
    dap_assert(
        0 == dap_mul(l_unsigned_long, (unsigned long)0) &&
        l_unsigned_long == dap_mul(l_unsigned_long, (unsigned long)1) &&
        l_unsigned_long * 2 == dap_mul(l_unsigned_long, (unsigned long)2) &&
        l_unsigned_long == dap_mul(l_unsigned_long, (unsigned long)3) &&
        0 == dap_mul_builtin(l_unsigned_long, (unsigned long)0) &&
        l_unsigned_long == dap_mul_builtin(l_unsigned_long, (unsigned long)1) &&
        l_unsigned_long * 2 == dap_mul_builtin(l_unsigned_long, (unsigned long)2) &&
        l_unsigned_long == dap_mul_builtin(l_unsigned_long, (unsigned long)3),
        "Check unsigned long MUL overflow");
    dap_assert(
        0 == dap_mul(l_unsigned_long_long, (unsigned long long)0) &&
        l_unsigned_long_long == dap_mul(l_unsigned_long_long, (unsigned long long)1) &&
        l_unsigned_long_long * 2 == dap_mul(l_unsigned_long_long, (unsigned long long)2) &&
        l_unsigned_long_long == dap_mul(l_unsigned_long_long, (unsigned long long)3) &&
        0 == dap_mul_builtin(l_unsigned_long_long, (unsigned long long)0) &&
        l_unsigned_long_long == dap_mul_builtin(l_unsigned_long_long, (unsigned long long)1) &&
        l_unsigned_long_long * 2 == dap_mul_builtin(l_unsigned_long_long, (unsigned long long)2) &&
        l_unsigned_long_long == dap_mul_builtin(l_unsigned_long_long, (unsigned long long)3),
        "Check unsigned long long MUL overflow");
}

static void s_test_benchmark_overflow_add(uint64_t a_times)
{
    dap_print_module_name("dap_benchmark_overflow_add");
    char l_char = dap_maxval(l_char);
    long long l_long_long = dap_maxval(l_long_long);
    unsigned char l_unsigned_char = dap_maxval(l_unsigned_char);
    unsigned long long l_unsigned_long_long = dap_maxval(l_unsigned_long_long);
    
    char l_msg[120] = {0};
    int l_cur_1 = 0, l_cur_2 = 0, l_custom = 0, l_builtin = 0;
    unsigned char
        *l_chars_array_a = NULL,
        *l_chars_array_b = NULL;
    DAP_NEW_Z_SIZE_RET(l_chars_array_a, unsigned char, s_array_size, NULL);
    DAP_NEW_Z_SIZE_RET(l_chars_array_b, unsigned char, s_array_size, l_chars_array_a);

    for (uint64_t i = 0; i < a_times; i += s_array_size) {
        randombytes(l_chars_array_a, s_array_size);
        randombytes(l_chars_array_b, s_array_size);
        l_cur_1 = get_cur_time_msec();
        for (uint64_t j = 0; j < s_array_size; ++j)
            dap_add((char)l_chars_array_a[j], (char)l_chars_array_b[j]);
        l_cur_2 = get_cur_time_msec();
        for (uint64_t j = 0; j < s_array_size; ++j)
            dap_add_builtin((char)l_chars_array_a[j], (char)l_chars_array_b[j]);
        l_builtin += get_cur_time_msec() - l_cur_2;
        l_custom += l_cur_2 - l_cur_1;
    }

    
    sprintf(l_msg, "Check overflow %"DAP_UINT64_FORMAT_U" times to custom char", a_times);
    benchmark_mgs_time(l_msg, l_custom);
    sprintf(l_msg, "Check overflow %"DAP_UINT64_FORMAT_U" times to __builtin char", a_times);
    benchmark_mgs_time(l_msg, l_builtin);

    l_custom = 0;
    l_builtin = 0;
    for (uint64_t i = 0; i < a_times; i += s_array_size) {
        randombytes(l_chars_array_a, s_array_size);
        randombytes(l_chars_array_b, s_array_size);
        l_cur_1 = get_cur_time_msec();
        for (uint64_t j = 0; j < s_array_size; ++j)
            dap_add((long long)l_chars_array_a[j], (long long)l_chars_array_b[j]);
        l_cur_2 = get_cur_time_msec();
        for (uint64_t j = 0; j < s_array_size; ++j)
            dap_add_builtin((long long)l_chars_array_a[j], (long long)l_chars_array_b[j]);
        l_builtin += get_cur_time_msec() - l_cur_2;
        l_custom += l_cur_2 - l_cur_1;
    }

    sprintf(l_msg, "Check overflow %"DAP_UINT64_FORMAT_U" times to custom long long", a_times);
    benchmark_mgs_time(l_msg, l_custom);
    sprintf(l_msg, "Check overflow %"DAP_UINT64_FORMAT_U" times to __builtin long long", a_times);
    benchmark_mgs_time(l_msg, l_builtin);

    l_custom = 0;
    l_builtin = 0;
    for (uint64_t i = 0; i < a_times; i += s_array_size) {
        randombytes(l_chars_array_a, s_array_size);
        randombytes(l_chars_array_b, s_array_size);
        l_cur_1 = get_cur_time_msec();
        for (uint64_t j = 0; j < s_array_size; ++j)
            dap_add((unsigned char)l_chars_array_a[j], (unsigned char)l_chars_array_b[j]);
        l_cur_2 = get_cur_time_msec();
        for (uint64_t j = 0; j < s_array_size; ++j)
            dap_add_builtin((unsigned char)l_chars_array_a[j], (unsigned char)l_chars_array_b[j]);
        l_builtin += get_cur_time_msec() - l_cur_2;
        l_custom += l_cur_2 - l_cur_1;
    }

    sprintf(l_msg, "Check overflow %"DAP_UINT64_FORMAT_U" times to custom  unsigned char", a_times);
    benchmark_mgs_time(l_msg, l_custom);
    sprintf(l_msg, "Check overflow %"DAP_UINT64_FORMAT_U" times to __builtin unsigned char", a_times);
    benchmark_mgs_time(l_msg, l_builtin);

    l_custom = 0;
    l_builtin = 0;
    for (uint64_t i = 0; i < a_times; i += s_array_size) {
        randombytes(l_chars_array_a, s_array_size);
        randombytes(l_chars_array_b, s_array_size);
        l_cur_1 = get_cur_time_msec();
        for (uint64_t j = 0; j < s_array_size; ++j)
            dap_add((unsigned long long)l_chars_array_a[j], (unsigned long long)l_chars_array_b[j]);
        l_cur_2 = get_cur_time_msec();
        for (uint64_t j = 0; j < s_array_size; ++j)
            dap_add_builtin((unsigned long long)l_chars_array_a[j], (unsigned long long)l_chars_array_b[j]);
        l_builtin += get_cur_time_msec() - l_cur_2;
        l_custom += l_cur_2 - l_cur_1;
    }

    sprintf(l_msg, "Check overflow %"DAP_UINT64_FORMAT_U" times to custom unsigned long long", a_times);
    benchmark_mgs_time(l_msg, l_custom);
    sprintf(l_msg, "Check overflow %"DAP_UINT64_FORMAT_U" times to __builtin unsigned long long", a_times);
    benchmark_mgs_time(l_msg, l_builtin);

    DAP_DEL_MULTY(l_chars_array_a, l_chars_array_b);
}

static void s_test_benchmark_overflow_sub(uint64_t a_times)
{
    dap_print_module_name("dap_benchmark_overflow_sub");
    char l_char = dap_minval(l_char);
    long long l_long_long = dap_minval(l_long_long);
    unsigned char l_unsigned_char = dap_minval(l_unsigned_char);
    unsigned long long l_unsigned_long_long = dap_minval(l_unsigned_long_long);
    
    char l_msg[120] = {0};
    int l_cur_1 = 0, l_cur_2 = 0, l_builtin = 0;


    l_cur_1 = get_cur_time_msec();
    for (uint64_t i = 0; i < a_times; ++i)
        dap_sub(l_char, (char)1);
    l_cur_2 = get_cur_time_msec();
    for (uint64_t i = 0; i < a_times; ++i)
        dap_sub_builtin(l_char, (char)1);
    l_builtin = get_cur_time_msec();

    sprintf(l_msg, "Check overflow %"DAP_UINT64_FORMAT_U" times to custom char", a_times);
    benchmark_mgs_time(l_msg, l_cur_2 - l_cur_1);
    sprintf(l_msg, "Check overflow %"DAP_UINT64_FORMAT_U" times to __builtin char", a_times);
    benchmark_mgs_time(l_msg, l_builtin - l_cur_2);


    l_cur_1 = get_cur_time_msec();
    for (uint64_t i = 0; i < a_times; ++i)
        dap_sub(l_long_long, (long long)1);
    l_cur_2 = get_cur_time_msec();
    for (uint64_t i = 0; i < a_times; ++i)
        dap_sub_builtin(l_long_long, (long long)1);
    l_builtin = get_cur_time_msec();

    sprintf(l_msg, "Check overflow %"DAP_UINT64_FORMAT_U" times to custom long long", a_times);
    benchmark_mgs_time(l_msg, l_cur_2 - l_cur_1);
    sprintf(l_msg, "Check overflow %"DAP_UINT64_FORMAT_U" times to __builtin long long", a_times);
    benchmark_mgs_time(l_msg, l_builtin - l_cur_2);


    l_cur_1 = get_cur_time_msec();
    for (uint64_t i = 0; i < a_times; ++i)
        dap_sub(l_unsigned_char, (unsigned char)1);
    l_cur_2 = get_cur_time_msec();
    for (uint64_t i = 0; i < a_times; ++i)
        dap_sub_builtin(l_unsigned_char, (unsigned char)1);
    l_builtin = get_cur_time_msec();

    sprintf(l_msg, "Check overflow %"DAP_UINT64_FORMAT_U" times to custom  unsigned char", a_times);
    benchmark_mgs_time(l_msg, l_cur_2 - l_cur_1);
    sprintf(l_msg, "Check overflow %"DAP_UINT64_FORMAT_U" times to __builtin unsigned char", a_times);
    benchmark_mgs_time(l_msg, l_builtin - l_cur_2);


    l_cur_1 = get_cur_time_msec();
    for (uint64_t i = 0; i < a_times; ++i)
        dap_sub(l_unsigned_long_long, (unsigned long long)1);
    l_cur_2 = get_cur_time_msec();
    for (uint64_t i = 0; i < a_times; ++i)
        dap_sub_builtin(l_unsigned_long_long, (unsigned long long)1);
    l_builtin = get_cur_time_msec();

    sprintf(l_msg, "Check overflow %"DAP_UINT64_FORMAT_U" times to custom unsigned long long", a_times);
    benchmark_mgs_time(l_msg, l_cur_2 - l_cur_1);
    sprintf(l_msg, "Check overflow %"DAP_UINT64_FORMAT_U" times to __builtin unsigned long long", a_times);
    benchmark_mgs_time(l_msg, l_builtin - l_cur_2);

}

static void s_test_benchmark_overflow_mul(uint64_t a_times)
{
    dap_print_module_name("dap_benchmark_overflow_mul");
    char l_char = dap_maxval(l_char) / 2 + 1;
    long long l_long_long = dap_maxval(l_long_long) / 2 + 1;
    unsigned char l_unsigned_char = dap_maxval(l_unsigned_char) / 2 + 1;
    unsigned long long l_unsigned_long_long = dap_maxval(l_unsigned_long_long) / 2 + 1;
    
    char l_msg[120] = {0};
    int l_cur_1 = 0, l_cur_2 = 0, l_builtin = 0;


    l_cur_1 = get_cur_time_msec();
    for (uint64_t i = 0; i < a_times; ++i)
        dap_mul(l_char, (char)2);
    l_cur_2 = get_cur_time_msec();
    for (uint64_t i = 0; i < a_times; ++i)
        dap_mul_builtin(l_char, (char)2);
    l_builtin = get_cur_time_msec();

    sprintf(l_msg, "Check overflow %"DAP_UINT64_FORMAT_U" times to custom char", a_times);
    benchmark_mgs_time(l_msg, l_cur_2 - l_cur_1);
    sprintf(l_msg, "Check overflow %"DAP_UINT64_FORMAT_U" times to __builtin char", a_times);
    benchmark_mgs_time(l_msg, l_builtin - l_cur_2);


    l_cur_1 = get_cur_time_msec();
    for (uint64_t i = 0; i < a_times; ++i)
        dap_mul(l_long_long, (long long)2);
    l_cur_2 = get_cur_time_msec();
    for (uint64_t i = 0; i < a_times; ++i)
        dap_mul_builtin(l_long_long, (long long)2);
    l_builtin = get_cur_time_msec();

    sprintf(l_msg, "Check overflow %"DAP_UINT64_FORMAT_U" times to custom long long", a_times);
    benchmark_mgs_time(l_msg, l_cur_2 - l_cur_1);
    sprintf(l_msg, "Check overflow %"DAP_UINT64_FORMAT_U" times to __builtin long long", a_times);
    benchmark_mgs_time(l_msg, l_builtin - l_cur_2);


    l_cur_1 = get_cur_time_msec();
    for (uint64_t i = 0; i < a_times; ++i)
        dap_mul(l_unsigned_char, (unsigned char)2);
    l_cur_2 = get_cur_time_msec();
    for (uint64_t i = 0; i < a_times; ++i)
        dap_mul_builtin(l_unsigned_char, (unsigned char)2);
    l_builtin = get_cur_time_msec();

    sprintf(l_msg, "Check overflow %"DAP_UINT64_FORMAT_U" times to custom  unsigned char", a_times);
    benchmark_mgs_time(l_msg, l_cur_2 - l_cur_1);
    sprintf(l_msg, "Check overflow %"DAP_UINT64_FORMAT_U" times to __builtin unsigned char", a_times);
    benchmark_mgs_time(l_msg, l_builtin - l_cur_2);


    l_cur_1 = get_cur_time_msec();
    for (uint64_t i = 0; i < a_times; ++i)
        dap_mul(l_unsigned_long_long, (unsigned long long)2);
    l_cur_2 = get_cur_time_msec();
    for (uint64_t i = 0; i < a_times; ++i)
        dap_mul_builtin(l_unsigned_long_long, (unsigned long long)2);
    l_builtin = get_cur_time_msec();

    sprintf(l_msg, "Check overflow %"DAP_UINT64_FORMAT_U" times to custom unsigned long long", a_times);
    benchmark_mgs_time(l_msg, l_cur_2 - l_cur_1);
    sprintf(l_msg, "Check overflow %"DAP_UINT64_FORMAT_U" times to __builtin unsigned long long", a_times);
    benchmark_mgs_time(l_msg, l_builtin - l_cur_2);

}

static void s_test_benchmark_overflow(uint64_t a_times)
{
    s_test_benchmark_overflow_add(a_times);
    s_test_benchmark_overflow_sub(a_times);
    s_test_benchmark_overflow_mul(a_times);
}

static void s_test_benchmark(uint64_t a_times)
{
    s_test_benchmark_overflow(a_times);
}
void dap_common_test_run()
{
    s_test_put_int();
    s_test_overflow();
    s_test_benchmark(s_array_size * 100);
}
