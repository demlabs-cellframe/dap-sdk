#include <boost/program_options/options_description.hpp>
#include <boost/program_options/option.hpp>
#include <boost/multiprecision/cpp_bin_float.hpp>
#include <boost/multiprecision/cpp_int.hpp>
#include <boost/random.hpp>
using namespace std;
#include <iostream>

#include "dap_math_ops.h"
#include "dap_math_convert.h"



#define MAX64STR "18446744073709551615"
#define MIN128STR "18446744073709551616"
#define MAX128STR "340282366920938463463374607431768211455"
#define MIN256STR "340282366920938463463374607431768211456"
#define MAX256STR "115792089237316195423570985008687907853269984665640564039457584007913129639935"


uint64_t one_bits[] = {0, 0x1, 0x2, 0x4, 0x8, 0x10, 0x20, 0x40, 0x80,
                          0x100, 0x200, 0x400, 0x800, 0x1000, 0x2000,
                          0x4000, 0x8000, 0x10000, 0x20000, 0x40000,
                          0x80000, 0x100000, 0x200000, 0x400000,
                          0x800000, 0x1000000, 0x2000000, 0x4000000,
                          0x8000000, 0x10000000, 0x20000000, 0x40000000,
                          0x80000000, 0x100000000, 0x200000000, 0x400000000,
                          0x800000000, 0x1000000000, 0x2000000000,
                          0x4000000000, 0x8000000000, 0x10000000000,
                          0x20000000000, 0x40000000000, 0x80000000000,
                          0x100000000000, 0x200000000000, 0x400000000000,
                          0x800000000000, 0x1000000000000, 0x2000000000000,
                          0x4000000000000, 0x8000000000000, 0x10000000000000,
                          0x20000000000000, 0x40000000000000, 0x80000000000000,
                          0x100000000000000, 0x200000000000000,
                          0x400000000000000, 0x800000000000000,
                          0x1000000000000000, 0x2000000000000000,
                          0x4000000000000000, 0x8000000000000000};

uint64_t all_bits[] = {0, 0x1, 0x3, 0x7, 0xf, 0x1f, 0x3f, 0x7f, 0xff,
                       0x1ff, 0x3ff, 0x7ff, 0xfff, 0x1fff, 0x3fff, 0x7fff,
                       0xffff, 0x1ffff, 0x3ffff, 0x7ffff, 0xfffff, 0x1fffff,
                       0x3fffff, 0x7fffff, 0xffffff, 0x1ffffff, 0x3ffffff, 0x7ffffff,
                       0xfffffff, 0x1fffffff, 0x3fffffff, 0x7fffffff, 0xffffffff,
                       0x1ffffffff, 0x3ffffffff, 0x7ffffffff, 0xfffffffff, 0x1fffffffff,
                       0x3fffffffff, 0x7fffffffff, 0xffffffffff, 0x1ffffffffff,
                       0x3ffffffffff, 0x7ffffffffff, 0xfffffffffff, 0x1fffffffffff,
                       0x3fffffffffff, 0x7fffffffffff, 0xffffffffffff, 0x1ffffffffffff,
                       0x3ffffffffffff, 0x7ffffffffffff, 0xfffffffffffff, 0x1fffffffffffff,
                       0x3fffffffffffff, 0x7fffffffffffff, 0xffffffffffffff, 0x1ffffffffffffff,
                       0x3ffffffffffffff, 0x7ffffffffffffff, 0xfffffffffffffff, 0x1fffffffffffffff,
                       0x3fffffffffffffff, 0x7fffffffffffffff, 0xffffffffffffffff};

namespace bmp = boost::multiprecision;

// Comparison functions
// void check_equality256(uint256_t a, bmp::uint256_t boost_a) {
// #ifdef DAP_GLOBAL_IS_INT128
//     ASSERT_EQ(a.lo, ((boost_a & bmp::uint256_t("0x00000000000000000000000000000000ffffffffffffffffffffffffffffffff")) >> 0)) << "boost_a is: " << boost_a;
//     ASSERT_EQ(a.hi, ((boost_a & bmp::uint256_t("0xffffffffffffffffffffffffffffffff00000000000000000000000000000000")) >> 128)) << "boost_a is: " << boost_a;
// #else
//     ASSERT_EQ(a.lo.lo, ((boost_a & bmp::uint256_t("0x000000000000000000000000000000000000000000000000ffffffffffffffff")) >> 0)) << "boost_a is: " << boost_a;
//     ASSERT_EQ(a.lo.hi, ((boost_a & bmp::uint256_t("0x00000000000000000000000000000000ffffffffffffffff0000000000000000")) >> 64)) << "boost_a is: " << boost_a;
//     ASSERT_EQ(a.hi.lo, ((boost_a & bmp::uint256_t("0x0000000000000000ffffffffffffffff00000000000000000000000000000000")) >> 128)) << "boost_a is: " << boost_a;
//     ASSERT_EQ(a.hi.hi, ((boost_a & bmp::uint256_t("0xffffffffffffffff000000000000000000000000000000000000000000000000")) >> 192)) << "boost_a is: " << boost_a;
// #endif
// }

// void check_equality256(uint256_t a, uint64_t aa) {
//     bmp::uint256_t boost_a = bmp::uint256_t(aa);
//     check_equality256(a, boost_a);
// }

// void check_equality256(uint256_t a, string aa) {
//     bmp::uint256_t boost_a = bmp::uint256_t(aa);
//     check_equality256(a, boost_a);
// }



// TEST(InputTests, ZeroInputBase) {
//     uint256_t a = uint256_0;

//     check_equality256(a, 0);
// }


