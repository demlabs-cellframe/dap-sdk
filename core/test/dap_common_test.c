#include "dap_common_test.h"


#define LOG_TAG "dap_common_test"


typedef enum {
    TYPE_CHAR,
    TYPE_SHORT,
    TYPE_INT,
    TYPE_LONG,
    TYPE_LONG_LONG,
    TYPE_SCHAR,
    TYPE_UCHAR,
    TYPE_USHORT,
    TYPE_UINT,
    TYPE_ULONG,
    TYPE_ULONG_LONG,
    TYPE_COUNT
} s_data_type;

typedef enum {
    OP_ADD,
    OP_SUB,
    OP_MUL,
    OP_COUNT
} s_op_type;

typedef void (*benchmark_callback)(unsigned char *, unsigned char *, uint64_t, s_data_type);

#define dap_type_convert_to(a,b)                                \
    ({                                                          \
        typeof(a) _a = (a);                                     \
        typeof(b) _b = (b);                                     \
        _b == TYPE_CHAR ? (char)_a :                            \
        _b == TYPE_SHORT ? (short)_a :                          \
        _b == TYPE_INT ? (int)_a :                              \
        _b == TYPE_LONG ? (long)_a :                            \
        _b == TYPE_LONG_LONG ? (long long)_a :                  \
        _b == TYPE_SCHAR ? (signed char)_a :                    \
        _b == TYPE_UCHAR ? (unsigned char)_a :                  \
        _b == TYPE_USHORT ? (unsigned short)_a :                \
        _b == TYPE_UINT ? (unsigned int)_a :                    \
        _b == TYPE_ULONG ? (unsigned long)_a :                  \
        _b == TYPE_ULONG_LONG ? (unsigned long long)_a :        \
        (typeof(a))_a;                                          \
    })

static const uint64_t s_el_count = 100000;
static const uint64_t s_array_size = s_el_count * sizeof(long long) / sizeof(char); // benchmarks array size 8MB

DAP_STATIC_INLINE const char *s_data_type_to_str(s_data_type a_type)
{
    switch (a_type)
    {
        case TYPE_CHAR: return "CHAR";
        case TYPE_SHORT: return "SHORT";
        case TYPE_INT: return "INT";
        case TYPE_LONG: return "LONG";
        case TYPE_LONG_LONG: return "LONG LONG";
        case TYPE_SCHAR: return "SIGNED CHAR";
        case TYPE_UCHAR: return "UNSIGNED CHAR";
        case TYPE_USHORT: return "UNSIGNED SHORT";
        case TYPE_UINT: return "UNSIGNED INT";
        case TYPE_ULONG: return "UNSIGNED LONG";
        case TYPE_ULONG_LONG: return "UNSIGNED LONG LONG";
        default: return "UNDEFINED";
    }
}

DAP_STATIC_INLINE const char *s_op_type_to_str(s_op_type a_type)
{
    switch (a_type)
    {
        case OP_ADD: return "CHAR";
        case OP_SUB: return "SHORT";
        case OP_MUL: return "INT";
        default: return "UNDEFINED";
    }
}

DAP_STATIC_INLINE void s_randombytes(unsigned char *a_array, uint64_t a_len)
{
    srand(time(NULL));
    for (uint64_t i = 0; i < a_len; i += sizeof(int)) {
        *(int*)(a_array + i) = rand();
    }
}

static void s_test_put_int()
{
    dap_print_module_name("dap_common");
    const int INT_VAL = 10;
    const char * EXPECTED_RESULT = "10";
    char * result_arr = dap_itoa(INT_VAL);
    dap_assert(strcmp(result_arr, EXPECTED_RESULT) == 0,
               "Check string result from itoa");
}

DAP_STATIC_INLINE void s_overflow_add_custom(unsigned char *a_array_a, unsigned char *a_array_b, uint64_t a_pos, s_data_type a_type)
{
    dap_add(dap_type_convert_to(*(a_array_a + a_pos), a_type), dap_type_convert_to(*(a_array_b + a_pos), a_type));
}

DAP_STATIC_INLINE void s_overflow_add_builtin(unsigned char *a_array_a, unsigned char *a_array_b, uint64_t a_pos, s_data_type a_type)
{
    dap_add_builtin(dap_type_convert_to(*(a_array_a + a_pos), a_type), dap_type_convert_to(*(a_array_b + a_pos), a_type));
}

DAP_STATIC_INLINE void s_overflow_sub_custom(unsigned char *a_array_a, unsigned char *a_array_b, uint64_t a_pos, s_data_type a_type)
{
    dap_sub(dap_type_convert_to(*(a_array_a + a_pos), a_type), dap_type_convert_to(*(a_array_b + a_pos), a_type));
}

DAP_STATIC_INLINE void s_overflow_sub_builtin(unsigned char *a_array_a, unsigned char *a_array_b, uint64_t a_pos, s_data_type a_type)
{
    dap_sub_builtin(dap_type_convert_to(*(a_array_a + a_pos), a_type), dap_type_convert_to(*(a_array_b + a_pos), a_type));
}

DAP_STATIC_INLINE void s_overflow_mul_custom(unsigned char *a_array_a, unsigned char *a_array_b, uint64_t a_pos, s_data_type a_type)
{
    dap_mul(dap_type_convert_to(*(a_array_a + a_pos), a_type), dap_type_convert_to(*(a_array_b + a_pos), a_type));
}

DAP_STATIC_INLINE void s_overflow_mul_builtin(unsigned char *a_array_a, unsigned char *a_array_b, uint64_t a_pos, s_data_type a_type)
{
    dap_mul_builtin(dap_type_convert_to(*(a_array_a + a_pos), a_type), dap_type_convert_to(*(a_array_b + a_pos), a_type));
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

static void s_test_overflow_diff_types(uint64_t a_times)
{
    dap_print_module_name("dap_overflow_add_diff_types");
    unsigned long long
        *l_a = NULL,
        *l_b = NULL; 
        DAP_NEW_Z_COUNT_RET(l_a, __typeof__(l_a), 2, NULL);
        l_b = l_a + 1;
    char q = 54;
    for (uint64_t i = 0; i < a_times; ++i) {
        s_randombytes((unsigned char *)l_a, sizeof(l_a) * 2);
        for (s_data_type t1 = 0; t1 < TYPE_COUNT; ++t1) {
            char l_msg[100];
            for (s_data_type t2 = 0; t2 < TYPE_COUNT; ++t2) {
                sprintf(l_msg, "ADD %s and %s", s_data_type_to_str(t1), s_data_type_to_str(t2));
                dap_assert_PIF(dap_add(dap_type_convert_to(*l_a, t1), dap_type_convert_to(*l_b, t2)) == dap_add_builtin(dap_type_convert_to(*l_a, t1), dap_type_convert_to(*l_b, t2)), l_msg);
                sprintf(l_msg, "SUB %s and %s", s_data_type_to_str(t1), s_data_type_to_str(t2));
                dap_assert_PIF(dap_sub(dap_type_convert_to(*l_a, t1), dap_type_convert_to(*l_b, t2)) == dap_sub_builtin(dap_type_convert_to(*l_a, t1), dap_type_convert_to(*l_b, t2)), l_msg);
                sprintf(l_msg, "MUL %s and %s", s_data_type_to_str(t1), s_data_type_to_str(t2));
                dap_assert_PIF(dap_mul(dap_type_convert_to(*l_a, t1), dap_type_convert_to(*l_b, t2)) == dap_mul_builtin(dap_type_convert_to(*l_a, t1), dap_type_convert_to(*l_b, t2)), l_msg);
            }
            if (i + 1 == a_times) {
                sprintf(l_msg, "%s check with others", s_data_type_to_str(t1));
                dap_assert(true, l_msg);
            }
        }
    }
    DAP_DELETE(l_a);
}

static void s_test_benchmark_overflow_one(uint64_t a_times, benchmark_callback a_custom_func, benchmark_callback a_builtin_func)
{
    char l_msg[120] = {0};
    int l_cur_1 = 0, l_cur_2 = 0, l_custom = 0, l_builtin = 0;
    unsigned char
        *l_chars_array_a = NULL,
        *l_chars_array_b = NULL;
    DAP_NEW_Z_SIZE_RET(l_chars_array_a, unsigned char, s_array_size, NULL);
    DAP_NEW_Z_SIZE_RET(l_chars_array_b, unsigned char, s_array_size, l_chars_array_a);

    for (s_data_type t = 0; t < TYPE_COUNT; ++t) {
        if (t == TYPE_CHAR || t == TYPE_LONG_LONG || t == TYPE_UCHAR || t == TYPE_ULONG_LONG) {
            l_custom = 0;
            l_builtin = 0;
            for (uint64_t total = 0; total < a_times; ) {
                s_randombytes(l_chars_array_a, s_array_size);
                s_randombytes(l_chars_array_b, s_array_size);
                l_cur_1 = get_cur_time_msec();
                for (uint64_t i = 0; i < s_el_count; ++i)
                    a_custom_func(l_chars_array_a, l_chars_array_b, i, t);
                l_cur_2 = get_cur_time_msec();
                for (uint64_t i = 0; i < s_el_count; ++i, ++total)
                    a_builtin_func(l_chars_array_a, l_chars_array_b, i, t);
                l_builtin += get_cur_time_msec() - l_cur_2;
                l_custom += l_cur_2 - l_cur_1;
            }
            sprintf(l_msg, "Check overflow %"DAP_UINT64_FORMAT_U" times to custom %s", a_times, s_data_type_to_str(t));
            benchmark_mgs_time(l_msg, l_custom);
            sprintf(l_msg, "Check overflow %"DAP_UINT64_FORMAT_U" times to __builtin %s", a_times, s_data_type_to_str(t));
            benchmark_mgs_time(l_msg, l_builtin);
        }
    }
    DAP_DEL_MULTY(l_chars_array_a, l_chars_array_b);
}

static void s_test_benchmark_overflow(uint64_t a_times)
{
    dap_print_module_name("dap_benchmark_overflow_add");
    s_test_benchmark_overflow_one(a_times, s_overflow_add_custom, s_overflow_add_builtin);
    dap_print_module_name("dap_benchmark_overflow_sub");
    s_test_benchmark_overflow_one(a_times, s_overflow_sub_custom, s_overflow_sub_builtin);
    dap_print_module_name("dap_benchmark_overflow_mul");
    s_test_benchmark_overflow_one(a_times, s_overflow_mul_custom, s_overflow_mul_builtin);
}

static void s_test_benchmark(uint64_t a_times)
{
    s_test_benchmark_overflow(a_times);
}
void dap_common_test_run()
{
    s_test_put_int();
    s_test_overflow();
    s_test_overflow_diff_types(1000);
    s_test_benchmark(s_el_count * 1000);
}
