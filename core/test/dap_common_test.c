#include "dap_common_test.h"
#include <math.h>


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

typedef void (*benchmark_callback)(void *, void *, uint64_t, s_data_type);

static const uint64_t s_times = 100;
static const uint64_t s_el_count = 100;
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

DAP_STATIC_INLINE void s_randombytes(unsigned char *a_array, uint64_t a_len)
{
    static int l_rand_add = 0;
    srand(time(NULL)  + l_rand_add);
    for (uint64_t i = 0; i < a_len; i += sizeof(int)) {
        *(int*)(a_array + i) = rand();
    }
    l_rand_add++;
}

static void s_test_put_int()
{
    dap_print_module_name("dap_common");

    const long long ten = 10, minus_twenty = -20, maxv = LLONG_MAX, minv = LLONG_MIN;
    const char ten_str[] = "10", minus_twenty_str[] = "-20", maxv_str[] = "9223372036854775807", minv_str[] = "-9223372036854775808";
    char *res_ten = dap_itoa(ten), *res_minus_20 = dap_itoa(minus_twenty), *res_maxv = dap_itoa(maxv), *res_minv = dap_itoa(minv);
    dap_assert(!strcmp(res_ten, ten_str) && !strcmp(minus_twenty_str, res_minus_20) 
            && !strcmp(maxv_str, res_maxv) && !strcmp(minv_str, res_minv), "Check string result from itoa");
}

DAP_STATIC_INLINE void s_overflow_add_custom(void *a_array_a, void *a_array_b, uint64_t a_pos, s_data_type a_type)
{
    switch (a_type)
    {
        case TYPE_CHAR:
            dap_add(*((char *)a_array_a + a_pos), *((char *)a_array_b + a_pos));
            break;
        case TYPE_SHORT:
            dap_add(*((short *)a_array_a + a_pos), *((short *)a_array_b + a_pos));
            break;
        case TYPE_INT:
            dap_add(*((int *)a_array_a + a_pos), *((int *)a_array_b + a_pos));
            break;
        case TYPE_LONG:
            dap_add(*((long *)a_array_a + a_pos), *((long *)a_array_b + a_pos));
            break;
        case TYPE_LONG_LONG:
            dap_add(*((long long *)a_array_a + a_pos), *((long long *)a_array_b + a_pos));
            break;
        case TYPE_SCHAR:
            dap_add(*((signed char *)a_array_a + a_pos), *((signed char *)a_array_b + a_pos));
            break;
        case TYPE_UCHAR:
            dap_add(*((unsigned char *)a_array_a + a_pos), *((unsigned char *)a_array_b + a_pos));
            break;
        case TYPE_USHORT:
            dap_add(*((unsigned short *)a_array_a + a_pos), *((unsigned short *)a_array_b + a_pos));
            break;
        case TYPE_UINT:
            dap_add(*((unsigned int *)a_array_a + a_pos), *((unsigned int *)a_array_b + a_pos));
            break;
        case TYPE_ULONG:
            dap_add(*((unsigned long *)a_array_a + a_pos), *((unsigned long *)a_array_b + a_pos));
            break;
        case TYPE_ULONG_LONG:
            dap_add(*((unsigned long long *)a_array_a + a_pos), *((unsigned long long *)a_array_b + a_pos));
            break;
        default:
            break;
    }
}

DAP_STATIC_INLINE void s_overflow_add_builtin(void *a_array_a, void *a_array_b, uint64_t a_pos, s_data_type a_type)
{
    switch (a_type)
    {
        case TYPE_CHAR:
            dap_add_builtin(*((char *)a_array_a + a_pos), *((char *)a_array_b + a_pos));
            break;
        case TYPE_SHORT:
            dap_add_builtin(*((short *)a_array_a + a_pos), *((short *)a_array_b + a_pos));
            break;
        case TYPE_INT:
            dap_add_builtin(*((int *)a_array_a + a_pos), *((int *)a_array_b + a_pos));
            break;
        case TYPE_LONG:
            dap_add_builtin(*((long *)a_array_a + a_pos), *((long *)a_array_b + a_pos));
            break;
        case TYPE_LONG_LONG:
            dap_add_builtin(*((long long *)a_array_a + a_pos), *((long long *)a_array_b + a_pos));
            break;
        case TYPE_SCHAR:
            dap_add_builtin(*((signed char *)a_array_a + a_pos), *((signed char *)a_array_b + a_pos));
            break;
        case TYPE_UCHAR:
            dap_add_builtin(*((unsigned char *)a_array_a + a_pos), *((unsigned char *)a_array_b + a_pos));
            break;
        case TYPE_USHORT:
            dap_add_builtin(*((unsigned short *)a_array_a + a_pos), *((unsigned short *)a_array_b + a_pos));
            break;
        case TYPE_UINT:
            dap_add_builtin(*((unsigned int *)a_array_a + a_pos), *((unsigned int *)a_array_b + a_pos));
            break;
        case TYPE_ULONG:
            dap_add_builtin(*((unsigned long *)a_array_a + a_pos), *((unsigned long *)a_array_b + a_pos));
            break;
        case TYPE_ULONG_LONG:
            dap_add_builtin(*((unsigned long long *)a_array_a + a_pos), *((unsigned long long *)a_array_b + a_pos));
            break;
        default:
            break;
    }
}

DAP_STATIC_INLINE void s_overflow_sub_custom(void *a_array_a, void *a_array_b, uint64_t a_pos, s_data_type a_type)
{
    switch (a_type)
    {
        case TYPE_CHAR:
            dap_sub(*((char *)a_array_a + a_pos), *((char *)a_array_b + a_pos));
            break;
        case TYPE_SHORT:
            dap_sub(*((short *)a_array_a + a_pos), *((short *)a_array_b + a_pos));
            break;
        case TYPE_INT:
            dap_sub(*((int *)a_array_a + a_pos), *((int *)a_array_b + a_pos));
            break;
        case TYPE_LONG:
            dap_sub(*((long *)a_array_a + a_pos), *((long *)a_array_b + a_pos));
            break;
        case TYPE_LONG_LONG:
            dap_sub(*((long long *)a_array_a + a_pos), *((long long *)a_array_b + a_pos));
            break;
        case TYPE_SCHAR:
            dap_sub(*((signed char *)a_array_a + a_pos), *((signed char *)a_array_b + a_pos));
            break;
        case TYPE_UCHAR:
            dap_sub(*((unsigned char *)a_array_a + a_pos), *((unsigned char *)a_array_b + a_pos));
            break;
        case TYPE_USHORT:
            dap_sub(*((unsigned short *)a_array_a + a_pos), *((unsigned short *)a_array_b + a_pos));
            break;
        case TYPE_UINT:
            dap_sub(*((unsigned int *)a_array_a + a_pos), *((unsigned int *)a_array_b + a_pos));
            break;
        case TYPE_ULONG:
            dap_sub(*((unsigned long *)a_array_a + a_pos), *((unsigned long *)a_array_b + a_pos));
            break;
        case TYPE_ULONG_LONG:
            dap_sub(*((unsigned long long *)a_array_a + a_pos), *((unsigned long long *)a_array_b + a_pos));
            break;
        default:
            break;
    }
}

DAP_STATIC_INLINE void s_overflow_sub_builtin(void *a_array_a, void *a_array_b, uint64_t a_pos, s_data_type a_type)
{
    switch (a_type)
    {
        case TYPE_CHAR:
            dap_sub_builtin(*((char *)a_array_a + a_pos), *((char *)a_array_b + a_pos));
            break;
        case TYPE_SHORT:
            dap_sub_builtin(*((short *)a_array_a + a_pos), *((short *)a_array_b + a_pos));
            break;
        case TYPE_INT:
            dap_sub_builtin(*((int *)a_array_a + a_pos), *((int *)a_array_b + a_pos));
            break;
        case TYPE_LONG:
            dap_sub_builtin(*((long *)a_array_a + a_pos), *((long *)a_array_b + a_pos));
            break;
        case TYPE_LONG_LONG:
            dap_sub_builtin(*((long long *)a_array_a + a_pos), *((long long *)a_array_b + a_pos));
            break;
        case TYPE_SCHAR:
            dap_sub_builtin(*((signed char *)a_array_a + a_pos), *((signed char *)a_array_b + a_pos));
            break;
        case TYPE_UCHAR:
            dap_sub_builtin(*((unsigned char *)a_array_a + a_pos), *((unsigned char *)a_array_b + a_pos));
            break;
        case TYPE_USHORT:
            dap_sub_builtin(*((unsigned short *)a_array_a + a_pos), *((unsigned short *)a_array_b + a_pos));
            break;
        case TYPE_UINT:
            dap_sub_builtin(*((unsigned int *)a_array_a + a_pos), *((unsigned int *)a_array_b + a_pos));
            break;
        case TYPE_ULONG:
            dap_sub_builtin(*((unsigned long *)a_array_a + a_pos), *((unsigned long *)a_array_b + a_pos));
            break;
        case TYPE_ULONG_LONG:
            dap_sub_builtin(*((unsigned long long *)a_array_a + a_pos), *((unsigned long long *)a_array_b + a_pos));
            break;
        default:
            break;
    }
}

DAP_STATIC_INLINE void s_overflow_mul_custom(void *a_array_a, void *a_array_b, uint64_t a_pos, s_data_type a_type)
{
    switch (a_type)
    {
        case TYPE_CHAR:
            dap_mul(*((char *)a_array_a + a_pos), *((char *)a_array_b + a_pos));
            break;
        case TYPE_SHORT:
            dap_mul(*((short *)a_array_a + a_pos), *((short *)a_array_b + a_pos));
            break;
        case TYPE_INT:
            dap_mul(*((int *)a_array_a + a_pos), *((int *)a_array_b + a_pos));
            break;
        case TYPE_LONG:
            dap_mul(*((long *)a_array_a + a_pos), *((long *)a_array_b + a_pos));
            break;
        case TYPE_LONG_LONG:
            dap_mul(*((long long *)a_array_a + a_pos), *((long long *)a_array_b + a_pos));
            break;
        case TYPE_SCHAR:
            dap_mul(*((signed char *)a_array_a + a_pos), *((signed char *)a_array_b + a_pos));
            break;
        case TYPE_UCHAR:
            dap_mul(*((unsigned char *)a_array_a + a_pos), *((unsigned char *)a_array_b + a_pos));
            break;
        case TYPE_USHORT:
            dap_mul(*((unsigned short *)a_array_a + a_pos), *((unsigned short *)a_array_b + a_pos));
            break;
        case TYPE_UINT:
            dap_mul(*((unsigned int *)a_array_a + a_pos), *((unsigned int *)a_array_b + a_pos));
            break;
        case TYPE_ULONG:
            dap_mul(*((unsigned long *)a_array_a + a_pos), *((unsigned long *)a_array_b + a_pos));
            break;
        case TYPE_ULONG_LONG:
            dap_mul(*((unsigned long long *)a_array_a + a_pos), *((unsigned long long *)a_array_b + a_pos));
            break;
        default:
            break;
    }
}

DAP_STATIC_INLINE void s_overflow_mul_builtin(void *a_array_a, void *a_array_b, uint64_t a_pos, s_data_type a_type)
{
    switch (a_type)
    {
        case TYPE_CHAR:
            dap_mul_builtin(*((char *)a_array_a + a_pos), *((char *)a_array_b + a_pos));
            break;
        case TYPE_SHORT:
            dap_mul_builtin(*((short *)a_array_a + a_pos), *((short *)a_array_b + a_pos));
            break;
        case TYPE_INT:
            dap_mul_builtin(*((int *)a_array_a + a_pos), *((int *)a_array_b + a_pos));
            break;
        case TYPE_LONG:
            dap_mul_builtin(*((long *)a_array_a + a_pos), *((long *)a_array_b + a_pos));
            break;
        case TYPE_LONG_LONG:
            dap_mul_builtin(*((long long *)a_array_a + a_pos), *((long long *)a_array_b + a_pos));
            break;
        case TYPE_SCHAR:
            dap_mul_builtin(*((signed char *)a_array_a + a_pos), *((signed char *)a_array_b + a_pos));
            break;
        case TYPE_UCHAR:
            dap_mul_builtin(*((unsigned char *)a_array_a + a_pos), *((unsigned char *)a_array_b + a_pos));
            break;
        case TYPE_USHORT:
            dap_mul_builtin(*((unsigned short *)a_array_a + a_pos), *((unsigned short *)a_array_b + a_pos));
            break;
        case TYPE_UINT:
            dap_mul_builtin(*((unsigned int *)a_array_a + a_pos), *((unsigned int *)a_array_b + a_pos));
            break;
        case TYPE_ULONG:
            dap_mul_builtin(*((unsigned long *)a_array_a + a_pos), *((unsigned long *)a_array_b + a_pos));
            break;
        case TYPE_ULONG_LONG:
            dap_mul_builtin(*((unsigned long long *)a_array_a + a_pos), *((unsigned long long *)a_array_b + a_pos));
            break;
        default:
            break;
    }
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
    // char
    for (int i = dap_minval(l_char); i <= dap_maxval(l_char); ++i) {
        for (int j = dap_minval(l_char); j <= dap_maxval(l_char); ++j)  {
            dap_assert_PIF(dap_add((char)i, (char)j) == dap_add_builtin((char)i, (char)j), "Base CHAR ADD test");
            dap_assert_PIF(dap_sub((char)i, (char)j) == dap_sub_builtin((char)i, (char)j), "Base CHAR SUB test");
            dap_assert_PIF(dap_mul((char)i, (char)j) == dap_mul_builtin((char)i, (char)j), "Base CHAR MUL test");
        }
    }
    // unsigned char
    for (int i = dap_minval(l_unsigned_char); i <= dap_maxval(l_unsigned_char); ++i) {
        for (int j = dap_minval(l_unsigned_char); j <= dap_maxval(l_unsigned_char); ++j)  {
            dap_assert_PIF(dap_add((unsigned char)i, (unsigned char)j) == dap_add_builtin((unsigned char)i, (unsigned char)j), "Base UNSIGNED CHAR ADD test");
            dap_assert_PIF(dap_sub((unsigned char)i, (unsigned char)j) == dap_sub_builtin((unsigned char)i, (unsigned char)j), "Base UNSIGNED CHAR SUB test");
            dap_assert_PIF(dap_mul((unsigned char)i, (unsigned char)j) == dap_mul_builtin((unsigned char)i, (unsigned char)j), "Base UNSIGNED CHAR MUL test");
        }
    }

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

static void s_test_overflow_diff_types(unsigned long long *l_a, unsigned long long *l_b)
{
// ADD
    // char
    dap_assert_PIF(dap_add(*(char*)l_a, *(char*)l_b) == dap_add_builtin(*(char*)l_a, *(char*)l_b), "ADD CHAR and CHAR");
    dap_assert_PIF(dap_add(*(short*)l_a, *(char*)l_b) == dap_add_builtin(*(short*)l_a, *(char*)l_b), "ADD SHORT and CHAR");
    dap_assert_PIF(dap_add(*(int*)l_a, *(char*)l_b) == dap_add_builtin(*(int*)l_a, *(char*)l_b), "ADD INT and CHAR");
    dap_assert_PIF(dap_add(*(long*)l_a, *(char*)l_b) == dap_add_builtin(*(long*)l_a, *(char*)l_b), "ADD LONG and CHAR");
    dap_assert_PIF(dap_add(*(long long*)l_a, *(char*)l_b) == dap_add_builtin(*(long long*)l_a, *(char*)l_b), "ADD LONG LONG and CHAR");
    dap_assert_PIF(dap_add(*(signed char*)l_a, *(char*)l_b) == dap_add_builtin(*(signed char*)l_a, *(char*)l_b), "ADD SIGNED CHAR and CHAR");
    dap_assert_PIF(dap_add(*(unsigned char*)l_a, *(char*)l_b) == dap_add_builtin(*(unsigned char*)l_a, *(char*)l_b), "ADD UNSIGNED CHAR and CHAR");
    dap_assert_PIF(dap_add(*(unsigned short*)l_a, *(char*)l_b) == dap_add_builtin(*(unsigned short*)l_a, *(char*)l_b), "ADD UNSIGNED SHORT and CHAR");
    dap_assert_PIF(dap_add(*(unsigned int*)l_a, *(char*)l_b) == dap_add_builtin(*(unsigned int*)l_a, *(char*)l_b), "ADD UNSIGNED INT and CHAR");
    dap_assert_PIF(dap_add(*(unsigned long*)l_a, *(char*)l_b) == dap_add_builtin(*(unsigned long*)l_a, *(char*)l_b), "ADD UNSIGNED LONG and CHAR");
    dap_assert_PIF(dap_add(*(unsigned long long*)l_a, *(char*)l_b) == dap_add_builtin(*(unsigned long long*)l_a, *(char*)l_b), "ADD UNSIGNED LONG LONG and CHAR");
    // short
    dap_assert_PIF(dap_add(*(char*)l_a, *(short*)l_b) == dap_add_builtin(*(char*)l_a, *(short*)l_b), "ADD CHAR and SHORT");
    dap_assert_PIF(dap_add(*(short*)l_a, *(short*)l_b) == dap_add_builtin(*(short*)l_a, *(short*)l_b), "ADD SHORT and SHORT");
    dap_assert_PIF(dap_add(*(int*)l_a, *(short*)l_b) == dap_add_builtin(*(int*)l_a, *(short*)l_b), "ADD INT and SHORT");
    dap_assert_PIF(dap_add(*(long*)l_a, *(short*)l_b) == dap_add_builtin(*(long*)l_a, *(short*)l_b), "ADD LONG and SHORT");
    dap_assert_PIF(dap_add(*(long long*)l_a, *(short*)l_b) == dap_add_builtin(*(long long*)l_a, *(short*)l_b), "ADD LONG LONG and SHORT");
    dap_assert_PIF(dap_add(*(signed char*)l_a, *(short*)l_b) == dap_add_builtin(*(signed char*)l_a, *(short*)l_b), "ADD SIGNED CHAR and SHORT");
    dap_assert_PIF(dap_add(*(unsigned char*)l_a, *(short*)l_b) == dap_add_builtin(*(unsigned char*)l_a, *(short*)l_b), "ADD UNSIGNED CHAR and SHORT");
    dap_assert_PIF(dap_add(*(unsigned short*)l_a, *(short*)l_b) == dap_add_builtin(*(unsigned short*)l_a, *(short*)l_b), "ADD UNSIGNED SHORT and SHORT");
    dap_assert_PIF(dap_add(*(unsigned int*)l_a, *(short*)l_b) == dap_add_builtin(*(unsigned int*)l_a, *(short*)l_b), "ADD UNSIGNED INT and SHORT");
    dap_assert_PIF(dap_add(*(unsigned long*)l_a, *(short*)l_b) == dap_add_builtin(*(unsigned long*)l_a, *(short*)l_b), "ADD UNSIGNED LONG and SHORT");
    dap_assert_PIF(dap_add(*(unsigned long long*)l_a, *(short*)l_b) == dap_add_builtin(*(unsigned long long*)l_a, *(short*)l_b), "ADD UNSIGNED LONG LONG and SHORT");
    // int
    dap_assert_PIF(dap_add(*(char*)l_a, *(int*)l_b) == dap_add_builtin(*(char*)l_a, *(int*)l_b), "ADD CHAR and INT");
    dap_assert_PIF(dap_add(*(short*)l_a, *(int*)l_b) == dap_add_builtin(*(short*)l_a, *(int*)l_b), "ADD SHORT and INT");
    dap_assert_PIF(dap_add(*(int*)l_a, *(int*)l_b) == dap_add_builtin(*(int*)l_a, *(int*)l_b), "ADD INT and INT");
    dap_assert_PIF(dap_add(*(long*)l_a, *(int*)l_b) == dap_add_builtin(*(long*)l_a, *(int*)l_b), "ADD LONG and INT");
    dap_assert_PIF(dap_add(*(long long*)l_a, *(int*)l_b) == dap_add_builtin(*(long long*)l_a, *(int*)l_b), "ADD LONG LONG and INT");
    dap_assert_PIF(dap_add(*(signed char*)l_a, *(int*)l_b) == dap_add_builtin(*(signed char*)l_a, *(int*)l_b), "ADD SIGNED CHAR and INT");
    dap_assert_PIF(dap_add(*(unsigned char*)l_a, *(int*)l_b) == dap_add_builtin(*(unsigned char*)l_a, *(int*)l_b), "ADD UNSIGNED CHAR and INT");
    dap_assert_PIF(dap_add(*(unsigned short*)l_a, *(int*)l_b) == dap_add_builtin(*(unsigned short*)l_a, *(int*)l_b), "ADD UNSIGNED SHORT and INT");
    dap_assert_PIF(dap_add(*(unsigned int*)l_a, *(int*)l_b) == dap_add_builtin(*(unsigned int*)l_a, *(int*)l_b), "ADD UNSIGNED INT and INT");
    dap_assert_PIF(dap_add(*(unsigned long*)l_a, *(int*)l_b) == dap_add_builtin(*(unsigned long*)l_a, *(int*)l_b), "ADD UNSIGNED LONG and INT");
    dap_assert_PIF(dap_add(*(unsigned long long*)l_a, *(int*)l_b) == dap_add_builtin(*(unsigned long long*)l_a, *(int*)l_b), "ADD UNSIGNED LONG LONG and INT");
    // long
    dap_assert_PIF(dap_add(*(char*)l_a, *(long*)l_b) == dap_add_builtin(*(char*)l_a, *(long*)l_b), "ADD CHAR and LONG");
    dap_assert_PIF(dap_add(*(short*)l_a, *(long*)l_b) == dap_add_builtin(*(short*)l_a, *(long*)l_b), "ADD SHORT and LONG");
    dap_assert_PIF(dap_add(*(int*)l_a, *(long*)l_b) == dap_add_builtin(*(int*)l_a, *(long*)l_b), "ADD INT and LONG");
    dap_assert_PIF(dap_add(*(long*)l_a, *(long*)l_b) == dap_add_builtin(*(long*)l_a, *(long*)l_b), "ADD LONG and LONG");
    dap_assert_PIF(dap_add(*(long long*)l_a, *(long*)l_b) == dap_add_builtin(*(long long*)l_a, *(long*)l_b), "ADD LONG LONG and LONG");
    dap_assert_PIF(dap_add(*(signed char*)l_a, *(long*)l_b) == dap_add_builtin(*(signed char*)l_a, *(long*)l_b), "ADD SIGNED CHAR and LONG");
    dap_assert_PIF(dap_add(*(unsigned char*)l_a, *(long*)l_b) == dap_add_builtin(*(unsigned char*)l_a, *(long*)l_b), "ADD UNSIGNED CHAR and LONG");
    dap_assert_PIF(dap_add(*(unsigned short*)l_a, *(long*)l_b) == dap_add_builtin(*(unsigned short*)l_a, *(long*)l_b), "ADD UNSIGNED SHORT and LONG");
    dap_assert_PIF(dap_add(*(unsigned int*)l_a, *(long*)l_b) == dap_add_builtin(*(unsigned int*)l_a, *(long*)l_b), "ADD UNSIGNED INT and LONG");
    dap_assert_PIF(dap_add(*(unsigned long*)l_a, *(long*)l_b) == dap_add_builtin(*(unsigned long*)l_a, *(long*)l_b), "ADD UNSIGNED LONG and LONG");
    dap_assert_PIF(dap_add(*(unsigned long long*)l_a, *(long*)l_b) == dap_add_builtin(*(unsigned long long*)l_a, *(long*)l_b), "ADD UNSIGNED LONG LONG and LONG");
    // long long
    dap_assert_PIF(dap_add(*(char*)l_a, *(long long*)l_b) == dap_add_builtin(*(char*)l_a, *(long long*)l_b), "ADD CHAR and LONG LONG");
    dap_assert_PIF(dap_add(*(short*)l_a, *(long long*)l_b) == dap_add_builtin(*(short*)l_a, *(long long*)l_b), "ADD SHORT and LONG LONG");
    dap_assert_PIF(dap_add(*(int*)l_a, *(long long*)l_b) == dap_add_builtin(*(int*)l_a, *(long long*)l_b), "ADD INT and LONG LONG");
    dap_assert_PIF(dap_add(*(long*)l_a, *(long long*)l_b) == dap_add_builtin(*(long*)l_a, *(long long*)l_b), "ADD LONG and LONG LONG");
    dap_assert_PIF(dap_add(*(long long*)l_a, *(long long*)l_b) == dap_add_builtin(*(long long*)l_a, *(long long*)l_b), "ADD LONG LONG and LONG LONG");
    dap_assert_PIF(dap_add(*(signed char*)l_a, *(long long*)l_b) == dap_add_builtin(*(signed char*)l_a, *(long long*)l_b), "ADD SIGNED CHAR and LONG LONG");
    dap_assert_PIF(dap_add(*(unsigned char*)l_a, *(long long*)l_b) == dap_add_builtin(*(unsigned char*)l_a, *(long long*)l_b), "ADD UNSIGNED CHAR and LONG LONG");
    dap_assert_PIF(dap_add(*(unsigned short*)l_a, *(long long*)l_b) == dap_add_builtin(*(unsigned short*)l_a, *(long long*)l_b), "ADD UNSIGNED SHORT and LONG LONG");
    dap_assert_PIF(dap_add(*(unsigned int*)l_a, *(long long*)l_b) == dap_add_builtin(*(unsigned int*)l_a, *(long long*)l_b), "ADD UNSIGNED INT and LONG LONG");
    dap_assert_PIF(dap_add(*(unsigned long*)l_a, *(long long*)l_b) == dap_add_builtin(*(unsigned long*)l_a, *(long long*)l_b), "ADD UNSIGNED LONG and LONG LONG");
    dap_assert_PIF(dap_add(*(unsigned long long*)l_a, *(long long*)l_b) == dap_add_builtin(*(unsigned long long*)l_a, *(long long*)l_b), "ADD UNSIGNED LONG LONG and LONG LONG");
    // signed char
    dap_assert_PIF(dap_add(*(char*)l_a, *(signed char*)l_b) == dap_add_builtin(*(char*)l_a, *(signed char*)l_b), "ADD CHAR and SIGNED CHAR");
    dap_assert_PIF(dap_add(*(short*)l_a, *(signed char*)l_b) == dap_add_builtin(*(short*)l_a, *(signed char*)l_b), "ADD SHORT and SIGNED CHAR");
    dap_assert_PIF(dap_add(*(int*)l_a, *(signed char*)l_b) == dap_add_builtin(*(int*)l_a, *(signed char*)l_b), "ADD INT and SIGNED CHAR");
    dap_assert_PIF(dap_add(*(long*)l_a, *(signed char*)l_b) == dap_add_builtin(*(long*)l_a, *(signed char*)l_b), "ADD LONG and CHAR");
    dap_assert_PIF(dap_add(*(long long*)l_a, *(signed char*)l_b) == dap_add_builtin(*(long long*)l_a, *(signed char*)l_b), "ADD LONG LONG and SIGNED CHAR");
    dap_assert_PIF(dap_add(*(signed char*)l_a, *(signed char*)l_b) == dap_add_builtin(*(signed char*)l_a, *(signed char*)l_b), "ADD SIGNED CHAR and SIGNED CHAR");
    dap_assert_PIF(dap_add(*(unsigned char*)l_a, *(signed char*)l_b) == dap_add_builtin(*(unsigned char*)l_a, *(signed char*)l_b), "ADD UNSIGNED CHAR and SIGNED CHAR");
    dap_assert_PIF(dap_add(*(unsigned short*)l_a, *(signed char*)l_b) == dap_add_builtin(*(unsigned short*)l_a, *(signed char*)l_b), "ADD UNSIGNED SHORT and SIGNED CHAR");
    dap_assert_PIF(dap_add(*(unsigned int*)l_a, *(signed char*)l_b) == dap_add_builtin(*(unsigned int*)l_a, *(signed char*)l_b), "ADD UNSIGNED INT and SIGNED CHAR");
    dap_assert_PIF(dap_add(*(unsigned long*)l_a, *(signed char*)l_b) == dap_add_builtin(*(unsigned long*)l_a, *(signed char*)l_b), "ADD UNSIGNED LONG and CHAR");
    dap_assert_PIF(dap_add(*(unsigned long long*)l_a, *(signed char*)l_b) == dap_add_builtin(*(unsigned long long*)l_a, *(signed char*)l_b), "ADD UNSIGNED LONG LONG and SIGNED CHAR");
    // unsigned char
    dap_assert_PIF(dap_add(*(char*)l_a, *(unsigned char*)l_b) == dap_add_builtin(*(char*)l_a, *(unsigned char*)l_b), "ADD CHAR and UNSIGNED CHAR");
    dap_assert_PIF(dap_add(*(short*)l_a, *(unsigned char*)l_b) == dap_add_builtin(*(short*)l_a, *(unsigned char*)l_b), "ADD SHORT and UNSIGNED CHAR");
    dap_assert_PIF(dap_add(*(int*)l_a, *(unsigned char*)l_b) == dap_add_builtin(*(int*)l_a, *(unsigned char*)l_b), "ADD INT and UNSIGNED CHAR");
    dap_assert_PIF(dap_add(*(long*)l_a, *(unsigned char*)l_b) == dap_add_builtin(*(long*)l_a, *(unsigned char*)l_b), "ADD LONG and UNSIGNED CHAR");
    dap_assert_PIF(dap_add(*(long long*)l_a, *(unsigned char*)l_b) == dap_add_builtin(*(long long*)l_a, *(unsigned char*)l_b), "ADD LONG LONG and UNSIGNED CHAR");
    dap_assert_PIF(dap_add(*(signed char*)l_a, *(unsigned char*)l_b) == dap_add_builtin(*(signed char*)l_a, *(unsigned char*)l_b), "ADD SIGNED CHAR and UNSIGNED CHAR");
    dap_assert_PIF(dap_add(*(unsigned char*)l_a, *(unsigned char*)l_b) == dap_add_builtin(*(unsigned char*)l_a, *(unsigned char*)l_b), "ADD UNSIGNED CHAR and UNSIGNED CHAR");
    dap_assert_PIF(dap_add(*(unsigned short*)l_a, *(unsigned char*)l_b) == dap_add_builtin(*(unsigned short*)l_a, *(unsigned char*)l_b), "ADD UNSIGNED SHORT and UNSIGNED CHAR");
    dap_assert_PIF(dap_add(*(unsigned int*)l_a, *(unsigned char*)l_b) == dap_add_builtin(*(unsigned int*)l_a, *(unsigned char*)l_b), "ADD UNSIGNED INT and UNSIGNED CHAR");
    dap_assert_PIF(dap_add(*(unsigned long*)l_a, *(unsigned char*)l_b) == dap_add_builtin(*(unsigned long*)l_a, *(unsigned char*)l_b), "ADD UNSIGNED LONG and UNSIGNED CHAR");
    dap_assert_PIF(dap_add(*(unsigned long long*)l_a, *(unsigned char*)l_b) == dap_add_builtin(*(unsigned long long*)l_a, *(unsigned char*)l_b), "ADD UNSIGNED LONG LONG and UNSIGNED CHAR");
    // unsigned short
    dap_assert_PIF(dap_add(*(char*)l_a, *(unsigned short*)l_b) == dap_add_builtin(*(char*)l_a, *(unsigned short*)l_b), "ADD CHAR and UNSIGNED SHORT");
    dap_assert_PIF(dap_add(*(short*)l_a, *(unsigned short*)l_b) == dap_add_builtin(*(short*)l_a, *(unsigned short*)l_b), "ADD SHORT and UNSIGNED SHORT");
    dap_assert_PIF(dap_add(*(int*)l_a, *(unsigned short*)l_b) == dap_add_builtin(*(int*)l_a, *(unsigned short*)l_b), "ADD INT and UNSIGNED SHORT");
    dap_assert_PIF(dap_add(*(long*)l_a, *(unsigned short*)l_b) == dap_add_builtin(*(long*)l_a, *(unsigned short*)l_b), "ADD LONG and UNSIGNED SHORT");
    dap_assert_PIF(dap_add(*(long long*)l_a, *(unsigned short*)l_b) == dap_add_builtin(*(long long*)l_a, *(unsigned short*)l_b), "ADD LONG LONG and UNSIGNED SHORT");
    dap_assert_PIF(dap_add(*(signed char*)l_a, *(unsigned short*)l_b) == dap_add_builtin(*(signed char*)l_a, *(unsigned short*)l_b), "ADD SIGNED CHAR and UNSIGNED SHORT");
    dap_assert_PIF(dap_add(*(unsigned char*)l_a, *(unsigned short*)l_b) == dap_add_builtin(*(unsigned char*)l_a, *(unsigned short*)l_b), "ADD UNSIGNED CHAR and UNSIGNED SHORT");
    dap_assert_PIF(dap_add(*(unsigned short*)l_a, *(unsigned short*)l_b) == dap_add_builtin(*(unsigned short*)l_a, *(unsigned short*)l_b), "ADD UNSIGNED SHORT and UNSIGNED SHORT");
    dap_assert_PIF(dap_add(*(unsigned int*)l_a, *(unsigned short*)l_b) == dap_add_builtin(*(unsigned int*)l_a, *(unsigned short*)l_b), "ADD UNSIGNED INT and UNSIGNED SHORT");
    dap_assert_PIF(dap_add(*(unsigned long*)l_a, *(unsigned short*)l_b) == dap_add_builtin(*(unsigned long*)l_a, *(unsigned short*)l_b), "ADD UNSIGNED LONG and UNSIGNED SHORT");
    dap_assert_PIF(dap_add(*(unsigned long long*)l_a, *(unsigned short*)l_b) == dap_add_builtin(*(unsigned long long*)l_a, *(unsigned short*)l_b), "ADD UNSIGNED LONG LONG and UNSIGNED SHORT");
    // unsigned int
    dap_assert_PIF(dap_add(*(char*)l_a, *(unsigned int*)l_b) == dap_add_builtin(*(char*)l_a, *(unsigned int*)l_b), "ADD CHAR and UNSIGNED INT");
    dap_assert_PIF(dap_add(*(short*)l_a, *(unsigned int*)l_b) == dap_add_builtin(*(short*)l_a, *(unsigned int*)l_b), "ADD SHORT and UNSIGNED INT");
    dap_assert_PIF(dap_add(*(int*)l_a, *(unsigned int*)l_b) == dap_add_builtin(*(int*)l_a, *(unsigned int*)l_b), "ADD INT and UNSIGNED INT");
    dap_assert_PIF(dap_add(*(long*)l_a, *(unsigned int*)l_b) == dap_add_builtin(*(long*)l_a, *(unsigned int*)l_b), "ADD LONG and UNSIGNED INT");
    dap_assert_PIF(dap_add(*(long long*)l_a, *(unsigned int*)l_b) == dap_add_builtin(*(long long*)l_a, *(unsigned int*)l_b), "ADD LONG LONG and UNSIGNED INT");
    dap_assert_PIF(dap_add(*(signed char*)l_a, *(unsigned int*)l_b) == dap_add_builtin(*(signed char*)l_a, *(unsigned int*)l_b), "ADD SIGNED CHAR and UNSIGNED INT");
    dap_assert_PIF(dap_add(*(unsigned char*)l_a, *(unsigned int*)l_b) == dap_add_builtin(*(unsigned char*)l_a, *(unsigned int*)l_b), "ADD UNSIGNED CHAR and UNSIGNED INT");
    dap_assert_PIF(dap_add(*(unsigned short*)l_a, *(unsigned int*)l_b) == dap_add_builtin(*(unsigned short*)l_a, *(unsigned int*)l_b), "ADD UNSIGNED SHORT and UNSIGNED INT");
    dap_assert_PIF(dap_add(*(unsigned int*)l_a, *(unsigned int*)l_b) == dap_add_builtin(*(unsigned int*)l_a, *(unsigned int*)l_b), "ADD UNSIGNED INT and UNSIGNED INT");
    dap_assert_PIF(dap_add(*(unsigned long*)l_a, *(unsigned int*)l_b) == dap_add_builtin(*(unsigned long*)l_a, *(unsigned int*)l_b), "ADD UNSIGNED LONG and UNSIGNED INT");
    dap_assert_PIF(dap_add(*(unsigned long long*)l_a, *(unsigned int*)l_b) == dap_add_builtin(*(unsigned long long*)l_a, *(unsigned int*)l_b), "ADD UNSIGNED LONG LONG and UNSIGNED INT");
    // unsigned long
    dap_assert_PIF(dap_add(*(char*)l_a, *(unsigned long*)l_b) == dap_add_builtin(*(char*)l_a, *(unsigned long*)l_b), "ADD CHAR and UNSIGNED LONG");
    dap_assert_PIF(dap_add(*(short*)l_a, *(unsigned long*)l_b) == dap_add_builtin(*(short*)l_a, *(unsigned long*)l_b), "ADD SHORT and UNSIGNED LONG");
    dap_assert_PIF(dap_add(*(int*)l_a, *(unsigned long*)l_b) == dap_add_builtin(*(int*)l_a, *(unsigned long*)l_b), "ADD INT and UNSIGNED LONG");
    dap_assert_PIF(dap_add(*(long*)l_a, *(unsigned long*)l_b) == dap_add_builtin(*(long*)l_a, *(unsigned long*)l_b), "ADD LONG and UNSIGNED LONG");
    dap_assert_PIF(dap_add(*(long long*)l_a, *(unsigned long*)l_b) == dap_add_builtin(*(long long*)l_a, *(unsigned long*)l_b), "ADD LONG LONG and UNSIGNED LONG");
    dap_assert_PIF(dap_add(*(signed char*)l_a, *(unsigned long*)l_b) == dap_add_builtin(*(signed char*)l_a, *(unsigned long*)l_b), "ADD SIGNED CHAR and UNSIGNED LONG");
    dap_assert_PIF(dap_add(*(unsigned char*)l_a, *(unsigned long*)l_b) == dap_add_builtin(*(unsigned char*)l_a, *(unsigned long*)l_b), "ADD UNSIGNED CHAR and UNSIGNED LONG");
    dap_assert_PIF(dap_add(*(unsigned short*)l_a, *(unsigned long*)l_b) == dap_add_builtin(*(unsigned short*)l_a, *(unsigned long*)l_b), "ADD UNSIGNED SHORT and UNSIGNED LONG");
    dap_assert_PIF(dap_add(*(unsigned int*)l_a, *(unsigned long*)l_b) == dap_add_builtin(*(unsigned int*)l_a, *(unsigned long*)l_b), "ADD UNSIGNED INT and UNSIGNED LONG");
    dap_assert_PIF(dap_add(*(unsigned long*)l_a, *(unsigned long*)l_b) == dap_add_builtin(*(unsigned long*)l_a, *(unsigned long*)l_b), "ADD UNSIGNED LONG and UNSIGNED LONG");
    dap_assert_PIF(dap_add(*(unsigned long long*)l_a, *(unsigned long*)l_b) == dap_add_builtin(*(unsigned long long*)l_a, *(unsigned long*)l_b), "ADD UNSIGNED LONG LONG and UNSIGNED LONG");
    // unsigned long long
    dap_assert_PIF(dap_add(*(char*)l_a, *(unsigned long long*)l_b) == dap_add_builtin(*(char*)l_a, *(unsigned long long*)l_b), "ADD CHAR and UNSIGNED LONG LONG");
    dap_assert_PIF(dap_add(*(short*)l_a, *(unsigned long long*)l_b) == dap_add_builtin(*(short*)l_a, *(unsigned long long*)l_b), "ADD SHORT and UNSIGNED LONG LONG");
    dap_assert_PIF(dap_add(*(int*)l_a, *(unsigned long long*)l_b) == dap_add_builtin(*(int*)l_a, *(unsigned long long*)l_b), "ADD INT and UNSIGNED LONG LONG");
    dap_assert_PIF(dap_add(*(long*)l_a, *(unsigned long long*)l_b) == dap_add_builtin(*(long*)l_a, *(unsigned long long*)l_b), "ADD LONG and UNSIGNED LONG LONG");
    dap_assert_PIF(dap_add(*(long long*)l_a, *(unsigned long long*)l_b) == dap_add_builtin(*(long long*)l_a, *(unsigned long long*)l_b), "ADD LONG LONG and UNSIGNED LONG LONG");
    dap_assert_PIF(dap_add(*(signed char*)l_a, *(unsigned long long*)l_b) == dap_add_builtin(*(signed char*)l_a, *(unsigned long long*)l_b), "ADD SIGNED CHAR and UNSIGNED LONG LONG");
    dap_assert_PIF(dap_add(*(unsigned char*)l_a, *(unsigned long long*)l_b) == dap_add_builtin(*(unsigned char*)l_a, *(unsigned long long*)l_b), "ADD UNSIGNED CHAR and UNSIGNED LONG LONG");
    dap_assert_PIF(dap_add(*(unsigned short*)l_a, *(unsigned long long*)l_b) == dap_add_builtin(*(unsigned short*)l_a, *(unsigned long long*)l_b), "ADD UNSIGNED SHORT and UNSIGNED LONG LONG");
    dap_assert_PIF(dap_add(*(unsigned int*)l_a, *(unsigned long long*)l_b) == dap_add_builtin(*(unsigned int*)l_a, *(unsigned long long*)l_b), "ADD UNSIGNED INT and UNSIGNED LONG LONG");
    dap_assert_PIF(dap_add(*(unsigned long*)l_a, *(unsigned long long*)l_b) == dap_add_builtin(*(unsigned long*)l_a, *(unsigned long long*)l_b), "ADD UNSIGNED LONG and UNSIGNED LONG LONG");
    dap_assert_PIF(dap_add(*(unsigned long long*)l_a, *(unsigned long long*)l_b) == dap_add_builtin(*(unsigned long long*)l_a, *(unsigned long long*)l_b), "ADD UNSIGNED LONG LONG and UNSIGNED LONG LONG");

// SUB
    // char
    dap_assert_PIF(dap_sub(*(char*)l_a, *(char*)l_b) == dap_sub_builtin(*(char*)l_a, *(char*)l_b), "SUB CHAR and CHAR");
    dap_assert_PIF(dap_sub(*(short*)l_a, *(char*)l_b) == dap_sub_builtin(*(short*)l_a, *(char*)l_b), "SUB SHORT and CHAR");
    dap_assert_PIF(dap_sub(*(int*)l_a, *(char*)l_b) == dap_sub_builtin(*(int*)l_a, *(char*)l_b), "SUB INT and CHAR");
    dap_assert_PIF(dap_sub(*(long*)l_a, *(char*)l_b) == dap_sub_builtin(*(long*)l_a, *(char*)l_b), "SUB LONG and CHAR");
    dap_assert_PIF(dap_sub(*(long long*)l_a, *(char*)l_b) == dap_sub_builtin(*(long long*)l_a, *(char*)l_b), "SUB LONG LONG and CHAR");
    dap_assert_PIF(dap_sub(*(signed char*)l_a, *(char*)l_b) == dap_sub_builtin(*(signed char*)l_a, *(char*)l_b), "SUB SIGNED CHAR and CHAR");
    dap_assert_PIF(dap_sub(*(unsigned char*)l_a, *(char*)l_b) == dap_sub_builtin(*(unsigned char*)l_a, *(char*)l_b), "SUB UNSIGNED CHAR and CHAR");
    dap_assert_PIF(dap_sub(*(unsigned short*)l_a, *(char*)l_b) == dap_sub_builtin(*(unsigned short*)l_a, *(char*)l_b), "SUB UNSIGNED SHORT and CHAR");
    dap_assert_PIF(dap_sub(*(unsigned int*)l_a, *(char*)l_b) == dap_sub_builtin(*(unsigned int*)l_a, *(char*)l_b), "SUB UNSIGNED INT and CHAR");
    dap_assert_PIF(dap_sub(*(unsigned long*)l_a, *(char*)l_b) == dap_sub_builtin(*(unsigned long*)l_a, *(char*)l_b), "SUB UNSIGNED LONG and CHAR");
    dap_assert_PIF(dap_sub(*(unsigned long long*)l_a, *(char*)l_b) == dap_sub_builtin(*(unsigned long long*)l_a, *(char*)l_b), "SUB UNSIGNED LONG LONG and CHAR");
    // short
    dap_assert_PIF(dap_sub(*(char*)l_a, *(short*)l_b) == dap_sub_builtin(*(char*)l_a, *(short*)l_b), "SUB CHAR and SHORT");
    dap_assert_PIF(dap_sub(*(short*)l_a, *(short*)l_b) == dap_sub_builtin(*(short*)l_a, *(short*)l_b), "SUB SHORT and SHORT");
    dap_assert_PIF(dap_sub(*(int*)l_a, *(short*)l_b) == dap_sub_builtin(*(int*)l_a, *(short*)l_b), "SUB INT and SHORT");
    dap_assert_PIF(dap_sub(*(long*)l_a, *(short*)l_b) == dap_sub_builtin(*(long*)l_a, *(short*)l_b), "SUB LONG and SHORT");
    dap_assert_PIF(dap_sub(*(long long*)l_a, *(short*)l_b) == dap_sub_builtin(*(long long*)l_a, *(short*)l_b), "SUB LONG LONG and SHORT");
    dap_assert_PIF(dap_sub(*(signed char*)l_a, *(short*)l_b) == dap_sub_builtin(*(signed char*)l_a, *(short*)l_b), "SUB SIGNED CHAR and SHORT");
    dap_assert_PIF(dap_sub(*(unsigned char*)l_a, *(short*)l_b) == dap_sub_builtin(*(unsigned char*)l_a, *(short*)l_b), "SUB UNSIGNED CHAR and SHORT");
    dap_assert_PIF(dap_sub(*(unsigned short*)l_a, *(short*)l_b) == dap_sub_builtin(*(unsigned short*)l_a, *(short*)l_b), "SUB UNSIGNED SHORT and SHORT");
    dap_assert_PIF(dap_sub(*(unsigned int*)l_a, *(short*)l_b) == dap_sub_builtin(*(unsigned int*)l_a, *(short*)l_b), "SUB UNSIGNED INT and SHORT");
    dap_assert_PIF(dap_sub(*(unsigned long*)l_a, *(short*)l_b) == dap_sub_builtin(*(unsigned long*)l_a, *(short*)l_b), "SUB UNSIGNED LONG and SHORT");
    dap_assert_PIF(dap_sub(*(unsigned long long*)l_a, *(short*)l_b) == dap_sub_builtin(*(unsigned long long*)l_a, *(short*)l_b), "SUB UNSIGNED LONG LONG and SHORT");
    // int
    dap_assert_PIF(dap_sub(*(char*)l_a, *(int*)l_b) == dap_sub_builtin(*(char*)l_a, *(int*)l_b), "SUB CHAR and INT");
    dap_assert_PIF(dap_sub(*(short*)l_a, *(int*)l_b) == dap_sub_builtin(*(short*)l_a, *(int*)l_b), "SUB SHORT and INT");
    dap_assert_PIF(dap_sub(*(int*)l_a, *(int*)l_b) == dap_sub_builtin(*(int*)l_a, *(int*)l_b), "SUB INT and INT");
    dap_assert_PIF(dap_sub(*(long*)l_a, *(int*)l_b) == dap_sub_builtin(*(long*)l_a, *(int*)l_b), "SUB LONG and INT");
    dap_assert_PIF(dap_sub(*(long long*)l_a, *(int*)l_b) == dap_sub_builtin(*(long long*)l_a, *(int*)l_b), "SUB LONG LONG and INT");
    dap_assert_PIF(dap_sub(*(signed char*)l_a, *(int*)l_b) == dap_sub_builtin(*(signed char*)l_a, *(int*)l_b), "SUB SIGNED CHAR and INT");
    dap_assert_PIF(dap_sub(*(unsigned char*)l_a, *(int*)l_b) == dap_sub_builtin(*(unsigned char*)l_a, *(int*)l_b), "SUB UNSIGNED CHAR and INT");
    dap_assert_PIF(dap_sub(*(unsigned short*)l_a, *(int*)l_b) == dap_sub_builtin(*(unsigned short*)l_a, *(int*)l_b), "SUB UNSIGNED SHORT and INT");
    dap_assert_PIF(dap_sub(*(unsigned int*)l_a, *(int*)l_b) == dap_sub_builtin(*(unsigned int*)l_a, *(int*)l_b), "SUB UNSIGNED INT and INT");
    dap_assert_PIF(dap_sub(*(unsigned long*)l_a, *(int*)l_b) == dap_sub_builtin(*(unsigned long*)l_a, *(int*)l_b), "SUB UNSIGNED LONG and INT");
    dap_assert_PIF(dap_sub(*(unsigned long long*)l_a, *(int*)l_b) == dap_sub_builtin(*(unsigned long long*)l_a, *(int*)l_b), "SUB UNSIGNED LONG LONG and INT");
    // long
    dap_assert_PIF(dap_sub(*(char*)l_a, *(long*)l_b) == dap_sub_builtin(*(char*)l_a, *(long*)l_b), "SUB CHAR and LONG");
    dap_assert_PIF(dap_sub(*(short*)l_a, *(long*)l_b) == dap_sub_builtin(*(short*)l_a, *(long*)l_b), "SUB SHORT and LONG");
    dap_assert_PIF(dap_sub(*(int*)l_a, *(long*)l_b) == dap_sub_builtin(*(int*)l_a, *(long*)l_b), "SUB INT and LONG");
    dap_assert_PIF(dap_sub(*(long*)l_a, *(long*)l_b) == dap_sub_builtin(*(long*)l_a, *(long*)l_b), "SUB LONG and LONG");
    dap_assert_PIF(dap_sub(*(long long*)l_a, *(long*)l_b) == dap_sub_builtin(*(long long*)l_a, *(long*)l_b), "SUB LONG LONG and LONG");
    dap_assert_PIF(dap_sub(*(signed char*)l_a, *(long*)l_b) == dap_sub_builtin(*(signed char*)l_a, *(long*)l_b), "SUB SIGNED CHAR and LONG");
    dap_assert_PIF(dap_sub(*(unsigned char*)l_a, *(long*)l_b) == dap_sub_builtin(*(unsigned char*)l_a, *(long*)l_b), "SUB UNSIGNED CHAR and LONG");
    dap_assert_PIF(dap_sub(*(unsigned short*)l_a, *(long*)l_b) == dap_sub_builtin(*(unsigned short*)l_a, *(long*)l_b), "SUB UNSIGNED SHORT and LONG");
    dap_assert_PIF(dap_sub(*(unsigned int*)l_a, *(long*)l_b) == dap_sub_builtin(*(unsigned int*)l_a, *(long*)l_b), "SUB UNSIGNED INT and LONG");
    dap_assert_PIF(dap_sub(*(unsigned long*)l_a, *(long*)l_b) == dap_sub_builtin(*(unsigned long*)l_a, *(long*)l_b), "SUB UNSIGNED LONG and LONG");
    dap_assert_PIF(dap_sub(*(unsigned long long*)l_a, *(long*)l_b) == dap_sub_builtin(*(unsigned long long*)l_a, *(long*)l_b), "SUB UNSIGNED LONG LONG and LONG");
    // long long
    dap_assert_PIF(dap_sub(*(char*)l_a, *(long long*)l_b) == dap_sub_builtin(*(char*)l_a, *(long long*)l_b), "SUB CHAR and LONG LONG");
    dap_assert_PIF(dap_sub(*(short*)l_a, *(long long*)l_b) == dap_sub_builtin(*(short*)l_a, *(long long*)l_b), "SUB SHORT and LONG LONG");
    dap_assert_PIF(dap_sub(*(int*)l_a, *(long long*)l_b) == dap_sub_builtin(*(int*)l_a, *(long long*)l_b), "SUB INT and LONG LONG");
    dap_assert_PIF(dap_sub(*(long*)l_a, *(long long*)l_b) == dap_sub_builtin(*(long*)l_a, *(long long*)l_b), "SUB LONG and LONG LONG");
    dap_assert_PIF(dap_sub(*(long long*)l_a, *(long long*)l_b) == dap_sub_builtin(*(long long*)l_a, *(long long*)l_b), "SUB LONG LONG and LONG LONG");
    dap_assert_PIF(dap_sub(*(signed char*)l_a, *(long long*)l_b) == dap_sub_builtin(*(signed char*)l_a, *(long long*)l_b), "SUB SIGNED CHAR and LONG LONG");
    dap_assert_PIF(dap_sub(*(unsigned char*)l_a, *(long long*)l_b) == dap_sub_builtin(*(unsigned char*)l_a, *(long long*)l_b), "SUB UNSIGNED CHAR and LONG LONG");
    dap_assert_PIF(dap_sub(*(unsigned short*)l_a, *(long long*)l_b) == dap_sub_builtin(*(unsigned short*)l_a, *(long long*)l_b), "SUB UNSIGNED SHORT and LONG LONG");
    dap_assert_PIF(dap_sub(*(unsigned int*)l_a, *(long long*)l_b) == dap_sub_builtin(*(unsigned int*)l_a, *(long long*)l_b), "SUB UNSIGNED INT and LONG LONG");
    dap_assert_PIF(dap_sub(*(unsigned long*)l_a, *(long long*)l_b) == dap_sub_builtin(*(unsigned long*)l_a, *(long long*)l_b), "SUB UNSIGNED LONG and LONG LONG");
    dap_assert_PIF(dap_sub(*(unsigned long long*)l_a, *(long long*)l_b) == dap_sub_builtin(*(unsigned long long*)l_a, *(long long*)l_b), "SUB UNSIGNED LONG LONG and LONG LONG");
    // signed char
    dap_assert_PIF(dap_sub(*(char*)l_a, *(signed char*)l_b) == dap_sub_builtin(*(char*)l_a, *(signed char*)l_b), "SUB CHAR and SIGNED CHAR");
    dap_assert_PIF(dap_sub(*(short*)l_a, *(signed char*)l_b) == dap_sub_builtin(*(short*)l_a, *(signed char*)l_b), "SUB SHORT and SIGNED CHAR");
    dap_assert_PIF(dap_sub(*(int*)l_a, *(signed char*)l_b) == dap_sub_builtin(*(int*)l_a, *(signed char*)l_b), "SUB INT and SIGNED CHAR");
    dap_assert_PIF(dap_sub(*(long*)l_a, *(signed char*)l_b) == dap_sub_builtin(*(long*)l_a, *(signed char*)l_b), "SUB LONG and CHAR");
    dap_assert_PIF(dap_sub(*(long long*)l_a, *(signed char*)l_b) == dap_sub_builtin(*(long long*)l_a, *(signed char*)l_b), "SUB LONG LONG and SIGNED CHAR");
    dap_assert_PIF(dap_sub(*(signed char*)l_a, *(signed char*)l_b) == dap_sub_builtin(*(signed char*)l_a, *(signed char*)l_b), "SUB SIGNED CHAR and SIGNED CHAR");
    dap_assert_PIF(dap_sub(*(unsigned char*)l_a, *(signed char*)l_b) == dap_sub_builtin(*(unsigned char*)l_a, *(signed char*)l_b), "SUB UNSIGNED CHAR and SIGNED CHAR");
    dap_assert_PIF(dap_sub(*(unsigned short*)l_a, *(signed char*)l_b) == dap_sub_builtin(*(unsigned short*)l_a, *(signed char*)l_b), "SUB UNSIGNED SHORT and SIGNED CHAR");
    dap_assert_PIF(dap_sub(*(unsigned int*)l_a, *(signed char*)l_b) == dap_sub_builtin(*(unsigned int*)l_a, *(signed char*)l_b), "SUB UNSIGNED INT and SIGNED CHAR");
    dap_assert_PIF(dap_sub(*(unsigned long*)l_a, *(signed char*)l_b) == dap_sub_builtin(*(unsigned long*)l_a, *(signed char*)l_b), "SUB UNSIGNED LONG and CHAR");
    dap_assert_PIF(dap_sub(*(unsigned long long*)l_a, *(signed char*)l_b) == dap_sub_builtin(*(unsigned long long*)l_a, *(signed char*)l_b), "SUB UNSIGNED LONG LONG and SIGNED CHAR");
    // unsigned char
    dap_assert_PIF(dap_sub(*(char*)l_a, *(unsigned char*)l_b) == dap_sub_builtin(*(char*)l_a, *(unsigned char*)l_b), "SUB CHAR and UNSIGNED CHAR");
    dap_assert_PIF(dap_sub(*(short*)l_a, *(unsigned char*)l_b) == dap_sub_builtin(*(short*)l_a, *(unsigned char*)l_b), "SUB SHORT and UNSIGNED CHAR");
    dap_assert_PIF(dap_sub(*(int*)l_a, *(unsigned char*)l_b) == dap_sub_builtin(*(int*)l_a, *(unsigned char*)l_b), "SUB INT and UNSIGNED CHAR");
    dap_assert_PIF(dap_sub(*(long*)l_a, *(unsigned char*)l_b) == dap_sub_builtin(*(long*)l_a, *(unsigned char*)l_b), "SUB LONG and UNSIGNED CHAR");
    dap_assert_PIF(dap_sub(*(long long*)l_a, *(unsigned char*)l_b) == dap_sub_builtin(*(long long*)l_a, *(unsigned char*)l_b), "SUB LONG LONG and UNSIGNED CHAR");
    dap_assert_PIF(dap_sub(*(signed char*)l_a, *(unsigned char*)l_b) == dap_sub_builtin(*(signed char*)l_a, *(unsigned char*)l_b), "SUB SIGNED CHAR and UNSIGNED CHAR");
    dap_assert_PIF(dap_sub(*(unsigned char*)l_a, *(unsigned char*)l_b) == dap_sub_builtin(*(unsigned char*)l_a, *(unsigned char*)l_b), "SUB UNSIGNED CHAR and UNSIGNED CHAR");
    dap_assert_PIF(dap_sub(*(unsigned short*)l_a, *(unsigned char*)l_b) == dap_sub_builtin(*(unsigned short*)l_a, *(unsigned char*)l_b), "SUB UNSIGNED SHORT and UNSIGNED CHAR");
    dap_assert_PIF(dap_sub(*(unsigned int*)l_a, *(unsigned char*)l_b) == dap_sub_builtin(*(unsigned int*)l_a, *(unsigned char*)l_b), "SUB UNSIGNED INT and UNSIGNED CHAR");
    dap_assert_PIF(dap_sub(*(unsigned long*)l_a, *(unsigned char*)l_b) == dap_sub_builtin(*(unsigned long*)l_a, *(unsigned char*)l_b), "SUB UNSIGNED LONG and UNSIGNED CHAR");
    dap_assert_PIF(dap_sub(*(unsigned long long*)l_a, *(unsigned char*)l_b) == dap_sub_builtin(*(unsigned long long*)l_a, *(unsigned char*)l_b), "SUB UNSIGNED LONG LONG and UNSIGNED CHAR");
    // unsigned short
    dap_assert_PIF(dap_sub(*(char*)l_a, *(unsigned short*)l_b) == dap_sub_builtin(*(char*)l_a, *(unsigned short*)l_b), "SUB CHAR and UNSIGNED SHORT");
    dap_assert_PIF(dap_sub(*(short*)l_a, *(unsigned short*)l_b) == dap_sub_builtin(*(short*)l_a, *(unsigned short*)l_b), "SUB SHORT and UNSIGNED SHORT");
    dap_assert_PIF(dap_sub(*(int*)l_a, *(unsigned short*)l_b) == dap_sub_builtin(*(int*)l_a, *(unsigned short*)l_b), "SUB INT and UNSIGNED SHORT");
    dap_assert_PIF(dap_sub(*(long*)l_a, *(unsigned short*)l_b) == dap_sub_builtin(*(long*)l_a, *(unsigned short*)l_b), "SUB LONG and UNSIGNED SHORT");
    dap_assert_PIF(dap_sub(*(long long*)l_a, *(unsigned short*)l_b) == dap_sub_builtin(*(long long*)l_a, *(unsigned short*)l_b), "SUB LONG LONG and UNSIGNED SHORT");
    dap_assert_PIF(dap_sub(*(signed char*)l_a, *(unsigned short*)l_b) == dap_sub_builtin(*(signed char*)l_a, *(unsigned short*)l_b), "SUB SIGNED CHAR and UNSIGNED SHORT");
    dap_assert_PIF(dap_sub(*(unsigned char*)l_a, *(unsigned short*)l_b) == dap_sub_builtin(*(unsigned char*)l_a, *(unsigned short*)l_b), "SUB UNSIGNED CHAR and UNSIGNED SHORT");
    dap_assert_PIF(dap_sub(*(unsigned short*)l_a, *(unsigned short*)l_b) == dap_sub_builtin(*(unsigned short*)l_a, *(unsigned short*)l_b), "SUB UNSIGNED SHORT and UNSIGNED SHORT");
    dap_assert_PIF(dap_sub(*(unsigned int*)l_a, *(unsigned short*)l_b) == dap_sub_builtin(*(unsigned int*)l_a, *(unsigned short*)l_b), "SUB UNSIGNED INT and UNSIGNED SHORT");
    dap_assert_PIF(dap_sub(*(unsigned long*)l_a, *(unsigned short*)l_b) == dap_sub_builtin(*(unsigned long*)l_a, *(unsigned short*)l_b), "SUB UNSIGNED LONG and UNSIGNED SHORT");
    dap_assert_PIF(dap_sub(*(unsigned long long*)l_a, *(unsigned short*)l_b) == dap_sub_builtin(*(unsigned long long*)l_a, *(unsigned short*)l_b), "SUB UNSIGNED LONG LONG and UNSIGNED SHORT");
    // unsigned int
    dap_assert_PIF(dap_sub(*(char*)l_a, *(unsigned int*)l_b) == dap_sub_builtin(*(char*)l_a, *(unsigned int*)l_b), "SUB CHAR and UNSIGNED INT");
    dap_assert_PIF(dap_sub(*(short*)l_a, *(unsigned int*)l_b) == dap_sub_builtin(*(short*)l_a, *(unsigned int*)l_b), "SUB SHORT and UNSIGNED INT");
    dap_assert_PIF(dap_sub(*(int*)l_a, *(unsigned int*)l_b) == dap_sub_builtin(*(int*)l_a, *(unsigned int*)l_b), "SUB INT and UNSIGNED INT");
    dap_assert_PIF(dap_sub(*(long*)l_a, *(unsigned int*)l_b) == dap_sub_builtin(*(long*)l_a, *(unsigned int*)l_b), "SUB LONG and UNSIGNED INT");
    dap_assert_PIF(dap_sub(*(long long*)l_a, *(unsigned int*)l_b) == dap_sub_builtin(*(long long*)l_a, *(unsigned int*)l_b), "SUB LONG LONG and UNSIGNED INT");
    dap_assert_PIF(dap_sub(*(signed char*)l_a, *(unsigned int*)l_b) == dap_sub_builtin(*(signed char*)l_a, *(unsigned int*)l_b), "SUB SIGNED CHAR and UNSIGNED INT");
    dap_assert_PIF(dap_sub(*(unsigned char*)l_a, *(unsigned int*)l_b) == dap_sub_builtin(*(unsigned char*)l_a, *(unsigned int*)l_b), "SUB UNSIGNED CHAR and UNSIGNED INT");
    dap_assert_PIF(dap_sub(*(unsigned short*)l_a, *(unsigned int*)l_b) == dap_sub_builtin(*(unsigned short*)l_a, *(unsigned int*)l_b), "SUB UNSIGNED SHORT and UNSIGNED INT");
    dap_assert_PIF(dap_sub(*(unsigned int*)l_a, *(unsigned int*)l_b) == dap_sub_builtin(*(unsigned int*)l_a, *(unsigned int*)l_b), "SUB UNSIGNED INT and UNSIGNED INT");
    dap_assert_PIF(dap_sub(*(unsigned long*)l_a, *(unsigned int*)l_b) == dap_sub_builtin(*(unsigned long*)l_a, *(unsigned int*)l_b), "SUB UNSIGNED LONG and UNSIGNED INT");
    dap_assert_PIF(dap_sub(*(unsigned long long*)l_a, *(unsigned int*)l_b) == dap_sub_builtin(*(unsigned long long*)l_a, *(unsigned int*)l_b), "SUB UNSIGNED LONG LONG and UNSIGNED INT");
    // unsigned long
    dap_assert_PIF(dap_sub(*(char*)l_a, *(unsigned long*)l_b) == dap_sub_builtin(*(char*)l_a, *(unsigned long*)l_b), "SUB CHAR and UNSIGNED LONG");
    dap_assert_PIF(dap_sub(*(short*)l_a, *(unsigned long*)l_b) == dap_sub_builtin(*(short*)l_a, *(unsigned long*)l_b), "SUB SHORT and UNSIGNED LONG");
    dap_assert_PIF(dap_sub(*(int*)l_a, *(unsigned long*)l_b) == dap_sub_builtin(*(int*)l_a, *(unsigned long*)l_b), "SUB INT and UNSIGNED LONG");
    dap_assert_PIF(dap_sub(*(long*)l_a, *(unsigned long*)l_b) == dap_sub_builtin(*(long*)l_a, *(unsigned long*)l_b), "SUB LONG and UNSIGNED LONG");
    dap_assert_PIF(dap_sub(*(long long*)l_a, *(unsigned long*)l_b) == dap_sub_builtin(*(long long*)l_a, *(unsigned long*)l_b), "SUB LONG LONG and UNSIGNED LONG");
    dap_assert_PIF(dap_sub(*(signed char*)l_a, *(unsigned long*)l_b) == dap_sub_builtin(*(signed char*)l_a, *(unsigned long*)l_b), "SUB SIGNED CHAR and UNSIGNED LONG");
    dap_assert_PIF(dap_sub(*(unsigned char*)l_a, *(unsigned long*)l_b) == dap_sub_builtin(*(unsigned char*)l_a, *(unsigned long*)l_b), "SUB UNSIGNED CHAR and UNSIGNED LONG");
    dap_assert_PIF(dap_sub(*(unsigned short*)l_a, *(unsigned long*)l_b) == dap_sub_builtin(*(unsigned short*)l_a, *(unsigned long*)l_b), "SUB UNSIGNED SHORT and UNSIGNED LONG");
    dap_assert_PIF(dap_sub(*(unsigned int*)l_a, *(unsigned long*)l_b) == dap_sub_builtin(*(unsigned int*)l_a, *(unsigned long*)l_b), "SUB UNSIGNED INT and UNSIGNED LONG");
    dap_assert_PIF(dap_sub(*(unsigned long*)l_a, *(unsigned long*)l_b) == dap_sub_builtin(*(unsigned long*)l_a, *(unsigned long*)l_b), "SUB UNSIGNED LONG and UNSIGNED LONG");
    dap_assert_PIF(dap_sub(*(unsigned long long*)l_a, *(unsigned long*)l_b) == dap_sub_builtin(*(unsigned long long*)l_a, *(unsigned long*)l_b), "SUB UNSIGNED LONG LONG and UNSIGNED LONG");
    // unsigned long long
    dap_assert_PIF(dap_sub(*(char*)l_a, *(unsigned long long*)l_b) == dap_sub_builtin(*(char*)l_a, *(unsigned long long*)l_b), "SUB CHAR and UNSIGNED LONG LONG");
    dap_assert_PIF(dap_sub(*(short*)l_a, *(unsigned long long*)l_b) == dap_sub_builtin(*(short*)l_a, *(unsigned long long*)l_b), "SUB SHORT and UNSIGNED LONG LONG");
    dap_assert_PIF(dap_sub(*(int*)l_a, *(unsigned long long*)l_b) == dap_sub_builtin(*(int*)l_a, *(unsigned long long*)l_b), "SUB INT and UNSIGNED LONG LONG");
    dap_assert_PIF(dap_sub(*(long*)l_a, *(unsigned long long*)l_b) == dap_sub_builtin(*(long*)l_a, *(unsigned long long*)l_b), "SUB LONG and UNSIGNED LONG LONG");
    dap_assert_PIF(dap_sub(*(long long*)l_a, *(unsigned long long*)l_b) == dap_sub_builtin(*(long long*)l_a, *(unsigned long long*)l_b), "SUB LONG LONG and UNSIGNED LONG LONG");
    dap_assert_PIF(dap_sub(*(signed char*)l_a, *(unsigned long long*)l_b) == dap_sub_builtin(*(signed char*)l_a, *(unsigned long long*)l_b), "SUB SIGNED CHAR and UNSIGNED LONG LONG");
    dap_assert_PIF(dap_sub(*(unsigned char*)l_a, *(unsigned long long*)l_b) == dap_sub_builtin(*(unsigned char*)l_a, *(unsigned long long*)l_b), "SUB UNSIGNED CHAR and UNSIGNED LONG LONG");
    dap_assert_PIF(dap_sub(*(unsigned short*)l_a, *(unsigned long long*)l_b) == dap_sub_builtin(*(unsigned short*)l_a, *(unsigned long long*)l_b), "SUB UNSIGNED SHORT and UNSIGNED LONG LONG");
    dap_assert_PIF(dap_sub(*(unsigned int*)l_a, *(unsigned long long*)l_b) == dap_sub_builtin(*(unsigned int*)l_a, *(unsigned long long*)l_b), "SUB UNSIGNED INT and UNSIGNED LONG LONG");
    dap_assert_PIF(dap_sub(*(unsigned long*)l_a, *(unsigned long long*)l_b) == dap_sub_builtin(*(unsigned long*)l_a, *(unsigned long long*)l_b), "SUB UNSIGNED LONG and UNSIGNED LONG LONG");
    dap_assert_PIF(dap_sub(*(unsigned long long*)l_a, *(unsigned long long*)l_b) == dap_sub_builtin(*(unsigned long long*)l_a, *(unsigned long long*)l_b), "SUB UNSIGNED LONG LONG and UNSIGNED LONG LONG");

// MUL
    // char
    dap_assert_PIF(dap_mul(*(char*)l_a, *(char*)l_b) == dap_mul_builtin(*(char*)l_a, *(char*)l_b), "MUL CHAR and CHAR");
    dap_assert_PIF(dap_mul(*(short*)l_a, *(char*)l_b) == dap_mul_builtin(*(short*)l_a, *(char*)l_b), "MUL SHORT and CHAR");
    dap_assert_PIF(dap_mul(*(int*)l_a, *(char*)l_b) == dap_mul_builtin(*(int*)l_a, *(char*)l_b), "MUL INT and CHAR");
    dap_assert_PIF(dap_mul(*(long*)l_a, *(char*)l_b) == dap_mul_builtin(*(long*)l_a, *(char*)l_b), "MUL LONG and CHAR");
    dap_assert_PIF(dap_mul(*(long long*)l_a, *(char*)l_b) == dap_mul_builtin(*(long long*)l_a, *(char*)l_b), "MUL LONG LONG and CHAR");
    dap_assert_PIF(dap_mul(*(signed char*)l_a, *(char*)l_b) == dap_mul_builtin(*(signed char*)l_a, *(char*)l_b), "MUL SIGNED CHAR and CHAR");
    dap_assert_PIF(dap_mul(*(unsigned char*)l_a, *(char*)l_b) == dap_mul_builtin(*(unsigned char*)l_a, *(char*)l_b), "MUL UNSIGNED CHAR and CHAR");
    dap_assert_PIF(dap_mul(*(unsigned short*)l_a, *(char*)l_b) == dap_mul_builtin(*(unsigned short*)l_a, *(char*)l_b), "MUL UNSIGNED SHORT and CHAR");
    dap_assert_PIF(dap_mul(*(unsigned int*)l_a, *(char*)l_b) == dap_mul_builtin(*(unsigned int*)l_a, *(char*)l_b), "MUL UNSIGNED INT and CHAR");
    dap_assert_PIF(dap_mul(*(unsigned long*)l_a, *(char*)l_b) == dap_mul_builtin(*(unsigned long*)l_a, *(char*)l_b), "MUL UNSIGNED LONG and CHAR");
    dap_assert_PIF(dap_mul(*(unsigned long long*)l_a, *(char*)l_b) == dap_mul_builtin(*(unsigned long long*)l_a, *(char*)l_b), "MUL UNSIGNED LONG LONG and CHAR");
    // short
    dap_assert_PIF(dap_mul(*(char*)l_a, *(short*)l_b) == dap_mul_builtin(*(char*)l_a, *(short*)l_b), "MUL CHAR and SHORT");
    dap_assert_PIF(dap_mul(*(short*)l_a, *(short*)l_b) == dap_mul_builtin(*(short*)l_a, *(short*)l_b), "MUL SHORT and SHORT");
    dap_assert_PIF(dap_mul(*(int*)l_a, *(short*)l_b) == dap_mul_builtin(*(int*)l_a, *(short*)l_b), "MUL INT and SHORT");
    dap_assert_PIF(dap_mul(*(long*)l_a, *(short*)l_b) == dap_mul_builtin(*(long*)l_a, *(short*)l_b), "MUL LONG and SHORT");
    dap_assert_PIF(dap_mul(*(long long*)l_a, *(short*)l_b) == dap_mul_builtin(*(long long*)l_a, *(short*)l_b), "MUL LONG LONG and SHORT");
    dap_assert_PIF(dap_mul(*(signed char*)l_a, *(short*)l_b) == dap_mul_builtin(*(signed char*)l_a, *(short*)l_b), "MUL SIGNED CHAR and SHORT");
    dap_assert_PIF(dap_mul(*(unsigned char*)l_a, *(short*)l_b) == dap_mul_builtin(*(unsigned char*)l_a, *(short*)l_b), "MUL UNSIGNED CHAR and SHORT");
    dap_assert_PIF(dap_mul(*(unsigned short*)l_a, *(short*)l_b) == dap_mul_builtin(*(unsigned short*)l_a, *(short*)l_b), "MUL UNSIGNED SHORT and SHORT");
    dap_assert_PIF(dap_mul(*(unsigned int*)l_a, *(short*)l_b) == dap_mul_builtin(*(unsigned int*)l_a, *(short*)l_b), "MUL UNSIGNED INT and SHORT");
    dap_assert_PIF(dap_mul(*(unsigned long*)l_a, *(short*)l_b) == dap_mul_builtin(*(unsigned long*)l_a, *(short*)l_b), "MUL UNSIGNED LONG and SHORT");
    dap_assert_PIF(dap_mul(*(unsigned long long*)l_a, *(short*)l_b) == dap_mul_builtin(*(unsigned long long*)l_a, *(short*)l_b), "MUL UNSIGNED LONG LONG and SHORT");
    // int
    dap_assert_PIF(dap_mul(*(char*)l_a, *(int*)l_b) == dap_mul_builtin(*(char*)l_a, *(int*)l_b), "MUL CHAR and INT");
    dap_assert_PIF(dap_mul(*(short*)l_a, *(int*)l_b) == dap_mul_builtin(*(short*)l_a, *(int*)l_b), "MUL SHORT and INT");
    dap_assert_PIF(dap_mul(*(int*)l_a, *(int*)l_b) == dap_mul_builtin(*(int*)l_a, *(int*)l_b), "MUL INT and INT");
    dap_assert_PIF(dap_mul(*(long*)l_a, *(int*)l_b) == dap_mul_builtin(*(long*)l_a, *(int*)l_b), "MUL LONG and INT");
    dap_assert_PIF(dap_mul(*(long long*)l_a, *(int*)l_b) == dap_mul_builtin(*(long long*)l_a, *(int*)l_b), "MUL LONG LONG and INT");
    dap_assert_PIF(dap_mul(*(signed char*)l_a, *(int*)l_b) == dap_mul_builtin(*(signed char*)l_a, *(int*)l_b), "MUL SIGNED CHAR and INT");
    dap_assert_PIF(dap_mul(*(unsigned char*)l_a, *(int*)l_b) == dap_mul_builtin(*(unsigned char*)l_a, *(int*)l_b), "MUL UNSIGNED CHAR and INT");
    dap_assert_PIF(dap_mul(*(unsigned short*)l_a, *(int*)l_b) == dap_mul_builtin(*(unsigned short*)l_a, *(int*)l_b), "MUL UNSIGNED SHORT and INT");
    dap_assert_PIF(dap_mul(*(unsigned int*)l_a, *(int*)l_b) == dap_mul_builtin(*(unsigned int*)l_a, *(int*)l_b), "MUL UNSIGNED INT and INT");
    dap_assert_PIF(dap_mul(*(unsigned long*)l_a, *(int*)l_b) == dap_mul_builtin(*(unsigned long*)l_a, *(int*)l_b), "MUL UNSIGNED LONG and INT");
    dap_assert_PIF(dap_mul(*(unsigned long long*)l_a, *(int*)l_b) == dap_mul_builtin(*(unsigned long long*)l_a, *(int*)l_b), "MUL UNSIGNED LONG LONG and INT");
    // long
    dap_assert_PIF(dap_mul(*(char*)l_a, *(long*)l_b) == dap_mul_builtin(*(char*)l_a, *(long*)l_b), "MUL CHAR and LONG");
    dap_assert_PIF(dap_mul(*(short*)l_a, *(long*)l_b) == dap_mul_builtin(*(short*)l_a, *(long*)l_b), "MUL SHORT and LONG");
    dap_assert_PIF(dap_mul(*(int*)l_a, *(long*)l_b) == dap_mul_builtin(*(int*)l_a, *(long*)l_b), "MUL INT and LONG");
    dap_assert_PIF(dap_mul(*(long*)l_a, *(long*)l_b) == dap_mul_builtin(*(long*)l_a, *(long*)l_b), "MUL LONG and LONG");
    dap_assert_PIF(dap_mul(*(long long*)l_a, *(long*)l_b) == dap_mul_builtin(*(long long*)l_a, *(long*)l_b), "MUL LONG LONG and LONG");
    dap_assert_PIF(dap_mul(*(signed char*)l_a, *(long*)l_b) == dap_mul_builtin(*(signed char*)l_a, *(long*)l_b), "MUL SIGNED CHAR and LONG");
    dap_assert_PIF(dap_mul(*(unsigned char*)l_a, *(long*)l_b) == dap_mul_builtin(*(unsigned char*)l_a, *(long*)l_b), "MUL UNSIGNED CHAR and LONG");
    dap_assert_PIF(dap_mul(*(unsigned short*)l_a, *(long*)l_b) == dap_mul_builtin(*(unsigned short*)l_a, *(long*)l_b), "MUL UNSIGNED SHORT and LONG");
    dap_assert_PIF(dap_mul(*(unsigned int*)l_a, *(long*)l_b) == dap_mul_builtin(*(unsigned int*)l_a, *(long*)l_b), "MUL UNSIGNED INT and LONG");
    dap_assert_PIF(dap_mul(*(unsigned long*)l_a, *(long*)l_b) == dap_mul_builtin(*(unsigned long*)l_a, *(long*)l_b), "MUL UNSIGNED LONG and LONG");
    dap_assert_PIF(dap_mul(*(unsigned long long*)l_a, *(long*)l_b) == dap_mul_builtin(*(unsigned long long*)l_a, *(long*)l_b), "MUL UNSIGNED LONG LONG and LONG");
    // long long
    dap_assert_PIF(dap_mul(*(char*)l_a, *(long long*)l_b) == dap_mul_builtin(*(char*)l_a, *(long long*)l_b), "MUL CHAR and LONG LONG");
    dap_assert_PIF(dap_mul(*(short*)l_a, *(long long*)l_b) == dap_mul_builtin(*(short*)l_a, *(long long*)l_b), "MUL SHORT and LONG LONG");
    dap_assert_PIF(dap_mul(*(int*)l_a, *(long long*)l_b) == dap_mul_builtin(*(int*)l_a, *(long long*)l_b), "MUL INT and LONG LONG");
    dap_assert_PIF(dap_mul(*(long*)l_a, *(long long*)l_b) == dap_mul_builtin(*(long*)l_a, *(long long*)l_b), "MUL LONG and LONG LONG");
    dap_assert_PIF(dap_mul(*(long long*)l_a, *(long long*)l_b) == dap_mul_builtin(*(long long*)l_a, *(long long*)l_b), "MUL LONG LONG and LONG LONG");
    dap_assert_PIF(dap_mul(*(signed char*)l_a, *(long long*)l_b) == dap_mul_builtin(*(signed char*)l_a, *(long long*)l_b), "MUL SIGNED CHAR and LONG LONG");
    dap_assert_PIF(dap_mul(*(unsigned char*)l_a, *(long long*)l_b) == dap_mul_builtin(*(unsigned char*)l_a, *(long long*)l_b), "MUL UNSIGNED CHAR and LONG LONG");
    dap_assert_PIF(dap_mul(*(unsigned short*)l_a, *(long long*)l_b) == dap_mul_builtin(*(unsigned short*)l_a, *(long long*)l_b), "MUL UNSIGNED SHORT and LONG LONG");
    dap_assert_PIF(dap_mul(*(unsigned int*)l_a, *(long long*)l_b) == dap_mul_builtin(*(unsigned int*)l_a, *(long long*)l_b), "MUL UNSIGNED INT and LONG LONG");
    dap_assert_PIF(dap_mul(*(unsigned long*)l_a, *(long long*)l_b) == dap_mul_builtin(*(unsigned long*)l_a, *(long long*)l_b), "MUL UNSIGNED LONG and LONG LONG");
    dap_assert_PIF(dap_mul(*(unsigned long long*)l_a, *(long long*)l_b) == dap_mul_builtin(*(unsigned long long*)l_a, *(long long*)l_b), "MUL UNSIGNED LONG LONG and LONG LONG");
    // signed char
    dap_assert_PIF(dap_mul(*(char*)l_a, *(signed char*)l_b) == dap_mul_builtin(*(char*)l_a, *(signed char*)l_b), "MUL CHAR and SIGNED CHAR");
    dap_assert_PIF(dap_mul(*(short*)l_a, *(signed char*)l_b) == dap_mul_builtin(*(short*)l_a, *(signed char*)l_b), "MUL SHORT and SIGNED CHAR");
    dap_assert_PIF(dap_mul(*(int*)l_a, *(signed char*)l_b) == dap_mul_builtin(*(int*)l_a, *(signed char*)l_b), "MUL INT and SIGNED CHAR");
    dap_assert_PIF(dap_mul(*(long*)l_a, *(signed char*)l_b) == dap_mul_builtin(*(long*)l_a, *(signed char*)l_b), "MUL LONG and CHAR");
    dap_assert_PIF(dap_mul(*(long long*)l_a, *(signed char*)l_b) == dap_mul_builtin(*(long long*)l_a, *(signed char*)l_b), "MUL LONG LONG and SIGNED CHAR");
    dap_assert_PIF(dap_mul(*(signed char*)l_a, *(signed char*)l_b) == dap_mul_builtin(*(signed char*)l_a, *(signed char*)l_b), "MUL SIGNED CHAR and SIGNED CHAR");
    dap_assert_PIF(dap_mul(*(unsigned char*)l_a, *(signed char*)l_b) == dap_mul_builtin(*(unsigned char*)l_a, *(signed char*)l_b), "MUL UNSIGNED CHAR and SIGNED CHAR");
    dap_assert_PIF(dap_mul(*(unsigned short*)l_a, *(signed char*)l_b) == dap_mul_builtin(*(unsigned short*)l_a, *(signed char*)l_b), "MUL UNSIGNED SHORT and SIGNED CHAR");
    dap_assert_PIF(dap_mul(*(unsigned int*)l_a, *(signed char*)l_b) == dap_mul_builtin(*(unsigned int*)l_a, *(signed char*)l_b), "MUL UNSIGNED INT and SIGNED CHAR");
    dap_assert_PIF(dap_mul(*(unsigned long*)l_a, *(signed char*)l_b) == dap_mul_builtin(*(unsigned long*)l_a, *(signed char*)l_b), "MUL UNSIGNED LONG and CHAR");
    dap_assert_PIF(dap_mul(*(unsigned long long*)l_a, *(signed char*)l_b) == dap_mul_builtin(*(unsigned long long*)l_a, *(signed char*)l_b), "MUL UNSIGNED LONG LONG and SIGNED CHAR");
    // unsigned char
    dap_assert_PIF(dap_mul(*(char*)l_a, *(unsigned char*)l_b) == dap_mul_builtin(*(char*)l_a, *(unsigned char*)l_b), "MUL CHAR and UNSIGNED CHAR");
    dap_assert_PIF(dap_mul(*(short*)l_a, *(unsigned char*)l_b) == dap_mul_builtin(*(short*)l_a, *(unsigned char*)l_b), "MUL SHORT and UNSIGNED CHAR");
    dap_assert_PIF(dap_mul(*(int*)l_a, *(unsigned char*)l_b) == dap_mul_builtin(*(int*)l_a, *(unsigned char*)l_b), "MUL INT and UNSIGNED CHAR");
    dap_assert_PIF(dap_mul(*(long*)l_a, *(unsigned char*)l_b) == dap_mul_builtin(*(long*)l_a, *(unsigned char*)l_b), "MUL LONG and UNSIGNED CHAR");
    dap_assert_PIF(dap_mul(*(long long*)l_a, *(unsigned char*)l_b) == dap_mul_builtin(*(long long*)l_a, *(unsigned char*)l_b), "MUL LONG LONG and UNSIGNED CHAR");
    dap_assert_PIF(dap_mul(*(signed char*)l_a, *(unsigned char*)l_b) == dap_mul_builtin(*(signed char*)l_a, *(unsigned char*)l_b), "MUL SIGNED CHAR and UNSIGNED CHAR");
    dap_assert_PIF(dap_mul(*(unsigned char*)l_a, *(unsigned char*)l_b) == dap_mul_builtin(*(unsigned char*)l_a, *(unsigned char*)l_b), "MUL UNSIGNED CHAR and UNSIGNED CHAR");
    dap_assert_PIF(dap_mul(*(unsigned short*)l_a, *(unsigned char*)l_b) == dap_mul_builtin(*(unsigned short*)l_a, *(unsigned char*)l_b), "MUL UNSIGNED SHORT and UNSIGNED CHAR");
    dap_assert_PIF(dap_mul(*(unsigned int*)l_a, *(unsigned char*)l_b) == dap_mul_builtin(*(unsigned int*)l_a, *(unsigned char*)l_b), "MUL UNSIGNED INT and UNSIGNED CHAR");
    dap_assert_PIF(dap_mul(*(unsigned long*)l_a, *(unsigned char*)l_b) == dap_mul_builtin(*(unsigned long*)l_a, *(unsigned char*)l_b), "MUL UNSIGNED LONG and UNSIGNED CHAR");
    dap_assert_PIF(dap_mul(*(unsigned long long*)l_a, *(unsigned char*)l_b) == dap_mul_builtin(*(unsigned long long*)l_a, *(unsigned char*)l_b), "MUL UNSIGNED LONG LONG and UNSIGNED CHAR");
    // unsigned short
    dap_assert_PIF(dap_mul(*(char*)l_a, *(unsigned short*)l_b) == dap_mul_builtin(*(char*)l_a, *(unsigned short*)l_b), "MUL CHAR and UNSIGNED SHORT");
    dap_assert_PIF(dap_mul(*(short*)l_a, *(unsigned short*)l_b) == dap_mul_builtin(*(short*)l_a, *(unsigned short*)l_b), "MUL SHORT and UNSIGNED SHORT");
    dap_assert_PIF(dap_mul(*(int*)l_a, *(unsigned short*)l_b) == dap_mul_builtin(*(int*)l_a, *(unsigned short*)l_b), "MUL INT and UNSIGNED SHORT");
    dap_assert_PIF(dap_mul(*(long*)l_a, *(unsigned short*)l_b) == dap_mul_builtin(*(long*)l_a, *(unsigned short*)l_b), "MUL LONG and UNSIGNED SHORT");
    dap_assert_PIF(dap_mul(*(long long*)l_a, *(unsigned short*)l_b) == dap_mul_builtin(*(long long*)l_a, *(unsigned short*)l_b), "MUL LONG LONG and UNSIGNED SHORT");
    dap_assert_PIF(dap_mul(*(signed char*)l_a, *(unsigned short*)l_b) == dap_mul_builtin(*(signed char*)l_a, *(unsigned short*)l_b), "MUL SIGNED CHAR and UNSIGNED SHORT");
    dap_assert_PIF(dap_mul(*(unsigned char*)l_a, *(unsigned short*)l_b) == dap_mul_builtin(*(unsigned char*)l_a, *(unsigned short*)l_b), "MUL UNSIGNED CHAR and UNSIGNED SHORT");
    dap_assert_PIF(dap_mul(*(unsigned short*)l_a, *(unsigned short*)l_b) == dap_mul_builtin(*(unsigned short*)l_a, *(unsigned short*)l_b), "MUL UNSIGNED SHORT and UNSIGNED SHORT");
    dap_assert_PIF(dap_mul(*(unsigned int*)l_a, *(unsigned short*)l_b) == dap_mul_builtin(*(unsigned int*)l_a, *(unsigned short*)l_b), "MUL UNSIGNED INT and UNSIGNED SHORT");
    dap_assert_PIF(dap_mul(*(unsigned long*)l_a, *(unsigned short*)l_b) == dap_mul_builtin(*(unsigned long*)l_a, *(unsigned short*)l_b), "MUL UNSIGNED LONG and UNSIGNED SHORT");
    dap_assert_PIF(dap_mul(*(unsigned long long*)l_a, *(unsigned short*)l_b) == dap_mul_builtin(*(unsigned long long*)l_a, *(unsigned short*)l_b), "MUL UNSIGNED LONG LONG and UNSIGNED SHORT");
    // unsigned int
    dap_assert_PIF(dap_mul(*(char*)l_a, *(unsigned int*)l_b) == dap_mul_builtin(*(char*)l_a, *(unsigned int*)l_b), "MUL CHAR and UNSIGNED INT");
    dap_assert_PIF(dap_mul(*(short*)l_a, *(unsigned int*)l_b) == dap_mul_builtin(*(short*)l_a, *(unsigned int*)l_b), "MUL SHORT and UNSIGNED INT");
    dap_assert_PIF(dap_mul(*(int*)l_a, *(unsigned int*)l_b) == dap_mul_builtin(*(int*)l_a, *(unsigned int*)l_b), "MUL INT and UNSIGNED INT");
    dap_assert_PIF(dap_mul(*(long*)l_a, *(unsigned int*)l_b) == dap_mul_builtin(*(long*)l_a, *(unsigned int*)l_b), "MUL LONG and UNSIGNED INT");
    dap_assert_PIF(dap_mul(*(long long*)l_a, *(unsigned int*)l_b) == dap_mul_builtin(*(long long*)l_a, *(unsigned int*)l_b), "MUL LONG LONG and UNSIGNED INT");
    dap_assert_PIF(dap_mul(*(signed char*)l_a, *(unsigned int*)l_b) == dap_mul_builtin(*(signed char*)l_a, *(unsigned int*)l_b), "MUL SIGNED CHAR and UNSIGNED INT");
    dap_assert_PIF(dap_mul(*(unsigned char*)l_a, *(unsigned int*)l_b) == dap_mul_builtin(*(unsigned char*)l_a, *(unsigned int*)l_b), "MUL UNSIGNED CHAR and UNSIGNED INT");
    dap_assert_PIF(dap_mul(*(unsigned short*)l_a, *(unsigned int*)l_b) == dap_mul_builtin(*(unsigned short*)l_a, *(unsigned int*)l_b), "MUL UNSIGNED SHORT and UNSIGNED INT");
    dap_assert_PIF(dap_mul(*(unsigned int*)l_a, *(unsigned int*)l_b) == dap_mul_builtin(*(unsigned int*)l_a, *(unsigned int*)l_b), "MUL UNSIGNED INT and UNSIGNED INT");
    dap_assert_PIF(dap_mul(*(unsigned long*)l_a, *(unsigned int*)l_b) == dap_mul_builtin(*(unsigned long*)l_a, *(unsigned int*)l_b), "MUL UNSIGNED LONG and UNSIGNED INT");
    dap_assert_PIF(dap_mul(*(unsigned long long*)l_a, *(unsigned int*)l_b) == dap_mul_builtin(*(unsigned long long*)l_a, *(unsigned int*)l_b), "MUL UNSIGNED LONG LONG and UNSIGNED INT");
    // unsigned long
    dap_assert_PIF(dap_mul(*(char*)l_a, *(unsigned long*)l_b) == dap_mul_builtin(*(char*)l_a, *(unsigned long*)l_b), "MUL CHAR and UNSIGNED LONG");
    dap_assert_PIF(dap_mul(*(short*)l_a, *(unsigned long*)l_b) == dap_mul_builtin(*(short*)l_a, *(unsigned long*)l_b), "MUL SHORT and UNSIGNED LONG");
    dap_assert_PIF(dap_mul(*(int*)l_a, *(unsigned long*)l_b) == dap_mul_builtin(*(int*)l_a, *(unsigned long*)l_b), "MUL INT and UNSIGNED LONG");
    dap_assert_PIF(dap_mul(*(long*)l_a, *(unsigned long*)l_b) == dap_mul_builtin(*(long*)l_a, *(unsigned long*)l_b), "MUL LONG and UNSIGNED LONG");
    dap_assert_PIF(dap_mul(*(long long*)l_a, *(unsigned long*)l_b) == dap_mul_builtin(*(long long*)l_a, *(unsigned long*)l_b), "MUL LONG LONG and UNSIGNED LONG");
    dap_assert_PIF(dap_mul(*(signed char*)l_a, *(unsigned long*)l_b) == dap_mul_builtin(*(signed char*)l_a, *(unsigned long*)l_b), "MUL SIGNED CHAR and UNSIGNED LONG");
    dap_assert_PIF(dap_mul(*(unsigned char*)l_a, *(unsigned long*)l_b) == dap_mul_builtin(*(unsigned char*)l_a, *(unsigned long*)l_b), "MUL UNSIGNED CHAR and UNSIGNED LONG");
    dap_assert_PIF(dap_mul(*(unsigned short*)l_a, *(unsigned long*)l_b) == dap_mul_builtin(*(unsigned short*)l_a, *(unsigned long*)l_b), "MUL UNSIGNED SHORT and UNSIGNED LONG");
    dap_assert_PIF(dap_mul(*(unsigned int*)l_a, *(unsigned long*)l_b) == dap_mul_builtin(*(unsigned int*)l_a, *(unsigned long*)l_b), "MUL UNSIGNED INT and UNSIGNED LONG");
    dap_assert_PIF(dap_mul(*(unsigned long*)l_a, *(unsigned long*)l_b) == dap_mul_builtin(*(unsigned long*)l_a, *(unsigned long*)l_b), "MUL UNSIGNED LONG and UNSIGNED LONG");
    dap_assert_PIF(dap_mul(*(unsigned long long*)l_a, *(unsigned long*)l_b) == dap_mul_builtin(*(unsigned long long*)l_a, *(unsigned long*)l_b), "MUL UNSIGNED LONG LONG and UNSIGNED LONG");
    // unsigned long long
    dap_assert_PIF(dap_mul(*(char*)l_a, *(unsigned long long*)l_b) == dap_mul_builtin(*(char*)l_a, *(unsigned long long*)l_b), "MUL CHAR and UNSIGNED LONG LONG");
    dap_assert_PIF(dap_mul(*(short*)l_a, *(unsigned long long*)l_b) == dap_mul_builtin(*(short*)l_a, *(unsigned long long*)l_b), "MUL SHORT and UNSIGNED LONG LONG");
    dap_assert_PIF(dap_mul(*(int*)l_a, *(unsigned long long*)l_b) == dap_mul_builtin(*(int*)l_a, *(unsigned long long*)l_b), "MUL INT and UNSIGNED LONG LONG");
    dap_assert_PIF(dap_mul(*(long*)l_a, *(unsigned long long*)l_b) == dap_mul_builtin(*(long*)l_a, *(unsigned long long*)l_b), "MUL LONG and UNSIGNED LONG LONG");
    dap_assert_PIF(dap_mul(*(long long*)l_a, *(unsigned long long*)l_b) == dap_mul_builtin(*(long long*)l_a, *(unsigned long long*)l_b), "MUL LONG LONG and UNSIGNED LONG LONG");
    dap_assert_PIF(dap_mul(*(signed char*)l_a, *(unsigned long long*)l_b) == dap_mul_builtin(*(signed char*)l_a, *(unsigned long long*)l_b), "MUL SIGNED CHAR and UNSIGNED LONG LONG");
    dap_assert_PIF(dap_mul(*(unsigned char*)l_a, *(unsigned long long*)l_b) == dap_mul_builtin(*(unsigned char*)l_a, *(unsigned long long*)l_b), "MUL UNSIGNED CHAR and UNSIGNED LONG LONG");
    dap_assert_PIF(dap_mul(*(unsigned short*)l_a, *(unsigned long long*)l_b) == dap_mul_builtin(*(unsigned short*)l_a, *(unsigned long long*)l_b), "MUL UNSIGNED SHORT and UNSIGNED LONG LONG");
    dap_assert_PIF(dap_mul(*(unsigned int*)l_a, *(unsigned long long*)l_b) == dap_mul_builtin(*(unsigned int*)l_a, *(unsigned long long*)l_b), "MUL UNSIGNED INT and UNSIGNED LONG LONG");
    dap_assert_PIF(dap_mul(*(unsigned long*)l_a, *(unsigned long long*)l_b) == dap_mul_builtin(*(unsigned long*)l_a, *(unsigned long long*)l_b), "MUL UNSIGNED LONG and UNSIGNED LONG LONG");
    dap_assert_PIF(dap_mul(*(unsigned long long*)l_a, *(unsigned long long*)l_b) == dap_mul_builtin(*(unsigned long long*)l_a, *(unsigned long long*)l_b), "MUL UNSIGNED LONG LONG and UNSIGNED LONG LONG");
}

static void s_test_overflow_diff_types_boundary_min(unsigned long long *l_a, unsigned long long *l_b)
{
    *((char *)l_a) = dap_minval((char)0);
    s_test_overflow_diff_types(l_a, l_b);
    s_test_overflow_diff_types(l_b, l_a);
    
    *((short *)l_a) = dap_minval((short)0);
    s_test_overflow_diff_types(l_a, l_b);
    s_test_overflow_diff_types(l_b, l_a);
    
    *((int *)l_a) = dap_minval((int)0);
    s_test_overflow_diff_types(l_a, l_b);
    s_test_overflow_diff_types(l_b, l_a);
    
    *((long *)l_a) = dap_minval((long)0);
    s_test_overflow_diff_types(l_a, l_b);
    s_test_overflow_diff_types(l_b, l_a);
    
    *((long long *)l_a) = dap_minval((long long)0);
    s_test_overflow_diff_types(l_a, l_b);
    s_test_overflow_diff_types(l_b, l_a);
    
    *((signed char *)l_a) = dap_minval((signed char)0);
    s_test_overflow_diff_types(l_a, l_b);
    s_test_overflow_diff_types(l_b, l_a);
    
    *((unsigned char *)l_a) = dap_minval((unsigned char)0);
    s_test_overflow_diff_types(l_a, l_b);
    s_test_overflow_diff_types(l_b, l_a);
    
    *((unsigned short *)l_a) = dap_minval((unsigned short)0);
    s_test_overflow_diff_types(l_a, l_b);
    s_test_overflow_diff_types(l_b, l_a);
    
    *((unsigned int *)l_a) = dap_minval((unsigned int)0);
    s_test_overflow_diff_types(l_a, l_b);
    s_test_overflow_diff_types(l_b, l_a);
    
    *((unsigned long *)l_a) = dap_minval((unsigned long)0);
    s_test_overflow_diff_types(l_a, l_b);
    s_test_overflow_diff_types(l_b, l_a);
    
    *((unsigned long long*)l_a) = dap_minval((unsigned long long)0);
    s_test_overflow_diff_types(l_a, l_b);
    s_test_overflow_diff_types(l_b, l_a);
}

static void s_test_overflow_diff_types_boundary_max(unsigned long long *l_a, unsigned long long *l_b)
{
    *((char *)l_a) = dap_maxval((char)0);
    s_test_overflow_diff_types(l_a, l_b);
    s_test_overflow_diff_types(l_b, l_a);
    
    *((short *)l_a) = dap_maxval((short)0);
    s_test_overflow_diff_types(l_a, l_b);
    s_test_overflow_diff_types(l_b, l_a);
    
    *((int *)l_a) = dap_maxval((int)0);
    s_test_overflow_diff_types(l_a, l_b);
    s_test_overflow_diff_types(l_b, l_a);
    
    *((long *)l_a) = dap_maxval((long)0);
    s_test_overflow_diff_types(l_a, l_b);
    s_test_overflow_diff_types(l_b, l_a);
    
    *((long long *)l_a) = dap_maxval((long long)0);
    s_test_overflow_diff_types(l_a, l_b);
    s_test_overflow_diff_types(l_b, l_a);
    
    *((signed char *)l_a) = dap_maxval((signed char)0);
    s_test_overflow_diff_types(l_a, l_b);
    s_test_overflow_diff_types(l_b, l_a);
    
    *((unsigned char *)l_a) = dap_maxval((unsigned char)0);
    s_test_overflow_diff_types(l_a, l_b);
    s_test_overflow_diff_types(l_b, l_a);
    
    *((unsigned short *)l_a) = dap_maxval((unsigned short)0);
    s_test_overflow_diff_types(l_a, l_b);
    s_test_overflow_diff_types(l_b, l_a);
    
    *((unsigned int *)l_a) = dap_maxval((unsigned int)0);
    s_test_overflow_diff_types(l_a, l_b);
    s_test_overflow_diff_types(l_b, l_a);
    
    *((unsigned long *)l_a) = dap_maxval((unsigned long)0);
    s_test_overflow_diff_types(l_a, l_b);
    s_test_overflow_diff_types(l_b, l_a);
    
    *((unsigned long long*)l_a) = dap_maxval((unsigned long long)0);
    s_test_overflow_diff_types(l_a, l_b);
    s_test_overflow_diff_types(l_b, l_a);
}

static void s_test_overflow_diff_types_boundary()
{
    unsigned long long l_a[2], *l_b = l_a + 1;
    dap_print_module_name("dap_overflow_add_diff_types_boundary_min");
    *l_b = 0;
    s_test_overflow_diff_types_boundary_min(l_a, l_b);
    dap_assert(true, "b = 0");
    *l_b = 1;
    s_test_overflow_diff_types_boundary_min(l_a, l_b);
    dap_assert(true, "b = 1");
    (*(char *)l_b) = -1;
    s_test_overflow_diff_types_boundary_min(l_a, l_b);
    (*(short *)l_b) = -1;
    s_test_overflow_diff_types_boundary_min(l_a, l_b);
    (*(int *)l_b) = -1;
    s_test_overflow_diff_types_boundary_min(l_a, l_b);
    (*(long *)l_b) = -1;
    s_test_overflow_diff_types_boundary_min(l_a, l_b);
    (*(long long *)l_b) = -1;
    s_test_overflow_diff_types_boundary_min(l_a, l_b);
    (*(signed char *)l_b) = -1;
    s_test_overflow_diff_types_boundary_min(l_a, l_b);
    dap_assert(true, "b = -1");

    (*(char *)l_b) = dap_minval((char)0);
    s_test_overflow_diff_types_boundary_min(l_a, l_b);
    (*(short *)l_b) = dap_minval((short)0);
    s_test_overflow_diff_types_boundary_min(l_a, l_b);
    (*(int *)l_b) = dap_minval((int)0);
    s_test_overflow_diff_types_boundary_min(l_a, l_b);
    (*(long *)l_b) = dap_minval((long)0);
    s_test_overflow_diff_types_boundary_min(l_a, l_b);
    (*(long long *)l_b) = dap_minval((long long)0);
    s_test_overflow_diff_types_boundary_min(l_a, l_b);
    (*(signed char *)l_b) = dap_minval((signed char)0);
    s_test_overflow_diff_types_boundary_min(l_a, l_b);
    dap_assert(true, "b = min");

    (*(char *)l_b) = dap_maxval((char)0);
    s_test_overflow_diff_types_boundary_min(l_a, l_b);
    (*(short *)l_b) = dap_maxval((short)0);
    s_test_overflow_diff_types_boundary_min(l_a, l_b);
    (*(int *)l_b) = dap_maxval((int)0);
    s_test_overflow_diff_types_boundary_min(l_a, l_b);
    (*(long *)l_b) = dap_maxval((long)0);
    s_test_overflow_diff_types_boundary_min(l_a, l_b);
    (*(long long *)l_b) = dap_maxval((long long)0);
    s_test_overflow_diff_types_boundary_min(l_a, l_b);
    (*(signed char *)l_b) = dap_maxval((signed char)0);
    s_test_overflow_diff_types_boundary_min(l_a, l_b);
    dap_assert(true, "b = max");

    dap_print_module_name("dap_overflow_add_diff_types_boundary_max");
    *l_b = 0;
    s_test_overflow_diff_types_boundary_max(l_a, l_b);
    dap_assert(true, "b = 0");
    *l_b = 1;
    s_test_overflow_diff_types_boundary_max(l_a, l_b);
    dap_assert(true, "b = 1");
    (*(char *)l_b) = -1;
    s_test_overflow_diff_types_boundary_max(l_a, l_b);
    (*(short *)l_b) = -1;
    s_test_overflow_diff_types_boundary_max(l_a, l_b);
    (*(int *)l_b) = -1;
    s_test_overflow_diff_types_boundary_max(l_a, l_b);
    (*(long *)l_b) = -1;
    s_test_overflow_diff_types_boundary_max(l_a, l_b);
    (*(long long *)l_b) = -1;
    s_test_overflow_diff_types_boundary_max(l_a, l_b);
    (*(signed char *)l_b) = -1;
    s_test_overflow_diff_types_boundary_max(l_a, l_b);
    dap_assert(true, "b = -1");

    (*(char *)l_b) = dap_minval((char)0);
    s_test_overflow_diff_types_boundary_max(l_a, l_b);
    (*(short *)l_b) = dap_minval((short)0);
    s_test_overflow_diff_types_boundary_max(l_a, l_b);
    (*(int *)l_b) = dap_minval((int)0);
    s_test_overflow_diff_types_boundary_max(l_a, l_b);
    (*(long *)l_b) = dap_minval((long)0);
    s_test_overflow_diff_types_boundary_max(l_a, l_b);
    (*(long long *)l_b) = dap_minval((long long)0);
    s_test_overflow_diff_types_boundary_max(l_a, l_b);
    (*(signed char *)l_b) = dap_minval((signed char)0);
    s_test_overflow_diff_types_boundary_max(l_a, l_b);
    dap_assert(true, "b = min");

    (*(char *)l_b) = dap_maxval((char)0);
    s_test_overflow_diff_types_boundary_max(l_a, l_b);
    (*(short *)l_b) = dap_maxval((short)0);
    s_test_overflow_diff_types_boundary_max(l_a, l_b);
    (*(int *)l_b) = dap_maxval((int)0);
    s_test_overflow_diff_types_boundary_max(l_a, l_b);
    (*(long *)l_b) = dap_maxval((long)0);
    s_test_overflow_diff_types_boundary_max(l_a, l_b);
    (*(long long *)l_b) = dap_maxval((long long)0);
    s_test_overflow_diff_types_boundary_max(l_a, l_b);
    (*(signed char *)l_b) = dap_maxval((signed char)0);
    s_test_overflow_diff_types_boundary_max(l_a, l_b);
    dap_assert(true, "b = max");
}

static void s_test_overflow_diff_types_rand(uint64_t a_times)
{
    dap_print_module_name("dap_overflow_add_diff_types_rand");
    unsigned long long l_a[2], *l_b = l_a + 1;
    for (uint64_t i = 0; i < a_times; ++i) {
        s_randombytes((unsigned char*)l_a, sizeof(l_a) * 2);
        s_test_overflow_diff_types(l_a, l_b);
    }
    char l_msg[100];
    snprintf(l_msg, sizeof(l_msg), "ADD SUB MUL %"DAP_UINT64_FORMAT_U" times", a_times);
    dap_assert(true, l_msg);
}

static void s_test_benchmark_overflow_one(uint64_t a_times, benchmark_callback a_custom_func, benchmark_callback a_builtin_func)
{
    char l_msg[120] = {0};
    int l_cur_1 = 0, l_cur_2 = 0, l_custom = 0, l_builtin = 0;
    unsigned char l_chars_array_a[s_array_size + 1], l_chars_array_b[s_array_size + 1];
    l_chars_array_a[s_array_size] = l_chars_array_b[s_array_size] = '\0';
    for (s_data_type t = 0; t < TYPE_COUNT; ++t) {
        // if (t == TYPE_CHAR || t == TYPE_INT || t == TYPE_LONG_LONG || t == TYPE_UCHAR || t == TYPE_ULONG_LONG) {
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
        // }
    }
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
    s_test_overflow_diff_types_boundary();
    s_test_overflow_diff_types_rand(s_times);
    s_test_benchmark(s_el_count * s_times);
}
