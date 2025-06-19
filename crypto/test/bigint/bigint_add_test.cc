
#include <gtest/gtest.h>
#include "bigint.h"
#include "add_specific_limb_size.h"
#include <boost/multiprecision/cpp_int.hpp>
#include <vector>
#include <cstdint>

using namespace boost::multiprecision;

class BigIntAddTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize test data for large integer addition using Boost's cpp_int
        // Each pair represents {a, b} where a and b are large integers
        // The values are chosen to test various overflow conditions for each limb size
        
        // For 8-bit limb size (values that span multiple limbs)
        test_values_8 = {
            // Basic addition
            {cpp_int("0"), cpp_int("0")},  // 0 + 0
            {cpp_int("1"), cpp_int("1")},  // 1 + 1
            
            // Single limb overflow
            {cpp_int("255"), cpp_int("1")},  // 255 + 1
            
            // Multi-limb addition
            {cpp_int("65535"), cpp_int("1")},  // 65535 + 1
            {cpp_int("4294967295"), cpp_int("1")},  // 2^32-1 + 1
            
            // Full range overflow
            {cpp_int("18446744073709551615"), cpp_int("1")},  // 2^64-1 + 1
            
            // Negative numbers (two's complement)
            {cpp_int("-1"), cpp_int("-1")},  // -1 + -1
            {cpp_int("-9223372036854775808"), cpp_int("-9223372036854775808")}   // -2^63 + -2^63
        };
        
        // For 16-bit limb size (values that span multiple limbs)
        test_values_16 = {
            // Basic addition
            {cpp_int("0"), cpp_int("0")},  // 0 + 0
            {cpp_int("1"), cpp_int("1")},  // 1 + 1
            
            // Single limb overflow
            {cpp_int("65535"), cpp_int("1")},  // 65535 + 1
            
            // Multi-limb addition
            {cpp_int("4294967295"), cpp_int("1")},  // 2^32-1 + 1
            {cpp_int("18446744073709551615"), cpp_int("1")},  // 2^64-1 + 1
            
            // Full range overflow
            {cpp_int("18446744073709551615"), cpp_int("18446744073709551615")},  // 2^64-1 + 2^64-1
            
            // Negative numbers (two's complement)
            {cpp_int("-1"), cpp_int("-1")},  // -1 + -1
            {cpp_int("-9223372036854775808"), cpp_int("-9223372036854775808")}   // -2^63 + -2^63
        };
        
        // For 32-bit limb size (values that span multiple limbs)
        test_values_32 = {
            // Basic addition
            {cpp_int("0"), cpp_int("0")},  // 0 + 0
            {cpp_int("1"), cpp_int("1")},  // 1 + 1
            
            // Single limb overflow
            {cpp_int("4294967295"), cpp_int("1")},  // 2^32-1 + 1
            
            // Multi-limb addition
            {cpp_int("18446744073709551615"), cpp_int("1")},  // 2^64-1 + 1
            {cpp_int("18446744073709551615"), cpp_int("18446744073709551615")},  // 2^64-1 + 2^64-1
            
            // Full range overflow
            {cpp_int("18446744073709551615"), cpp_int("18446744073709551615")},  // 2^64-1 + 2^64-1
            
            // Negative numbers (two's complement)
            {cpp_int("-1"), cpp_int("-1")},  // -1 + -1
            {cpp_int("-9223372036854775808"), cpp_int("-9223372036854775808")}   // -2^63 + -2^63
        };
        
        // For 64-bit limb size (values that span multiple limbs)
        test_values_64 = {
            // Basic addition
            {cpp_int("0"), cpp_int("0")},  // 0 + 0
            {cpp_int("1"), cpp_int("1")},  // 1 + 1
            
            // Single limb overflow
            {cpp_int("18446744073709551615"), cpp_int("1")},  // 2^64-1 + 1
            
            // Multi-limb addition
            {cpp_int("18446744073709551615"), cpp_int("18446744073709551615")},  // 2^64-1 + 2^64-1
            {cpp_int("9223372036854775808"), cpp_int("9223372036854775808")},  // 2^63 + 2^63
            
            // Full range overflow
            {cpp_int("18446744073709551615"), cpp_int("18446744073709551615")},  // 2^64-1 + 2^64-1
            
            // Negative numbers (two's complement)
            {cpp_int("-1"), cpp_int("-1")},  // -1 + -1
            {cpp_int("-9223372036854775808"), cpp_int("-9223372036854775808")}   // -2^63 + -2^63
        };
    }

    std::vector<std::pair<cpp_int, cpp_int>> test_values_8;
    std::vector<std::pair<cpp_int, cpp_int>> test_values_16;
    std::vector<std::pair<cpp_int, cpp_int>> test_values_32;
    std::vector<std::pair<cpp_int, cpp_int>> test_values_64;

    static void test_addition(int limb_size) {
        dap_bigint_t a, b, sum;
        a.limb_size = limb_size;
        b.limb_size = limb_size;
        sum.limb_size = limb_size;

        // Test case 1: Simple addition
        a.bigint_size = 64;
        b.bigint_size = 64;
        sum.bigint_size = 64;

        a.data.limb_64.body = new uint64_t[1];
        b.data.limb_64.body = new uint64_t[1];
        sum.data.limb_64.body = new uint64_t[1];

        a.data.limb_64.body[0] = 5;
        b.data.limb_64.body[0] = 3;

        int ret;
        switch(limb_size) {
            case 8:
                ret = dap_bigint_2scompl_ripple_carry_adder_value_8(&a, &b, &sum);
                break;
            case 16:
                ret = dap_bigint_2scompl_ripple_carry_adder_value_16(&a, &b, &sum);
                break;
            case 32:
                ret = dap_bigint_2scompl_ripple_carry_adder_value_32(&a, &b, &sum);
                break;
            case 64:
                ret = dap_bigint_2scompl_ripple_carry_adder_value_64(&a, &b, &sum);
                break;
            default:
                FAIL() << "Unsupported limb size: " << limb_size;
        }

        EXPECT_EQ(ret, 0);
        EXPECT_EQ(sum.data.limb_64.body[0], 8);

        delete[] a.data.limb_64.body;
        delete[] b.data.limb_64.body;
        delete[] sum.data.limb_64.body;

        // Test case 2: Addition with carry
        a.data.limb_64.body = new uint64_t[1];
        b.data.limb_64.body = new uint64_t[1];
        sum.data.limb_64.body = new uint64_t[1];

        a.data.limb_64.body[0] = UINT64_MAX;
        b.data.limb_64.body[0] = 1;

        switch(limb_size) {
            case 8:
                ret = dap_bigint_2scompl_ripple_carry_adder_value_8(&a, &b, &sum);
                break;
            case 16:
                ret = dap_bigint_2scompl_ripple_carry_adder_value_16(&a, &b, &sum);
                break;
            case 32:
                ret = dap_bigint_2scompl_ripple_carry_adder_value_32(&a, &b, &sum);
                break;
            case 64:
                ret = dap_bigint_2scompl_ripple_carry_adder_value_64(&a, &b, &sum);
                break;
            default:
                FAIL() << "Unsupported limb size: " << limb_size;
        }

        EXPECT_EQ(ret, 0);
        EXPECT_EQ(sum.data.limb_64.body[0], 0);

        delete[] a.data.limb_64.body;
        delete[] b.data.limb_64.body;
        delete[] sum.data.limb_64.body;
    }
};

TEST_F(BigIntAddTest, Addition8Bit) {
    test_addition(8);
}

TEST_F(BigIntAddTest, Addition16Bit) {
    test_addition(16);
}

TEST_F(BigIntAddTest, Addition32Bit) {
    test_addition(32);
}

TEST_F(BigIntAddTest, Addition64Bit) {
    test_addition(64);
}

TEST_F(BigIntAddTest, IncompatibleSizes) {
    dap_bigint_t a, b, sum;
    a.limb_size = 32;
    b.limb_size = 64;
    sum.limb_size = 64;

    a.bigint_size = 32;
    b.bigint_size = 64;
    sum.bigint_size = 64;

    a.data.limb_32.body = new uint32_t[1];
    b.data.limb_64.body = new uint64_t[1];
    sum.data.limb_64.body = new uint64_t[1];

    int ret = dap_bigint_2scompl_ripple_carry_adder_value_32(&a, &b, &sum);
    EXPECT_EQ(ret, -1);

    delete[] a.data.limb_32.body;
    delete[] b.data.limb_64.body;
    delete[] sum.data.limb_64.body;
}

TEST_F(BigIntAddTest, NullPointers) {
    dap_bigint_t b, sum;
    b.limb_size = 64;
    sum.limb_size = 64;

    b.bigint_size = 64;
    sum.bigint_size = 64;

    b.data.limb_64.body = new uint64_t[1];
    sum.data.limb_64.body = new uint64_t[1];

    int ret = dap_bigint_2scompl_ripple_carry_adder_value_64(nullptr, &b, &sum);
    EXPECT_EQ(ret, -1);

    delete[] b.data.limb_64.body;
    delete[] sum.data.limb_64.body;
} 
