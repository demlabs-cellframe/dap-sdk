#include <gtest/gtest.h>
#include "bigint.h"
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

    void test_addition(int limb_size) {
        dap_bigint_t a, b, sum;
        a.limb_size = limb_size;
        b.limb_size = limb_size;
        sum.limb_size = limb_size;

        // Calculate number of limbs needed for 64-bit values
        int num_limbs = 64 / limb_size;
        
        switch(limb_size) {
            case 8: {
                for (const auto& pair : test_values_8) {
                    // Initialize big integers with the test values
                    for (int i = 0; i < num_limbs; i++) {
                        a.data.limb_8.body[i] = static_cast<uint8_t>((pair.first >> (i * 8)) & 0xFF);
                        b.data.limb_8.body[i] = static_cast<uint8_t>((pair.second >> (i * 8)) & 0xFF);
                    }

                    int ret = dap_bigint_2scompl_ripple_carry_adder_value(&a, &b, &sum);
                    ASSERT_EQ(ret, 0) << "Addition operation failed";

                    // Verify the result matches expected large integer addition
                    cpp_int expected = pair.first + pair.second;
                    for (int i = 0; i < num_limbs; i++) {
                        uint8_t expected_limb = static_cast<uint8_t>((expected >> (i * 8)) & 0xFF);
                        ASSERT_EQ(sum.data.limb_8.body[i], expected_limb)
                            << "Addition failed at limb " << i << " for values: "
                            << pair.first.str() << " and " << pair.second.str();
                    }
                }
                break;
            }
            case 16: {
                for (const auto& pair : test_values_16) {
                    for (int i = 0; i < num_limbs; i++) {
                        a.data.limb_16.body[i] = static_cast<uint16_t>((pair.first >> (i * 16)) & 0xFFFF);
                        b.data.limb_16.body[i] = static_cast<uint16_t>((pair.second >> (i * 16)) & 0xFFFF);
                    }

                    int ret = dap_bigint_2scompl_ripple_carry_adder_value(&a, &b, &sum);
                    ASSERT_EQ(ret, 0) << "Addition operation failed";

                    cpp_int expected = pair.first + pair.second;
                    for (int i = 0; i < num_limbs; i++) {
                        uint16_t expected_limb = static_cast<uint16_t>((expected >> (i * 16)) & 0xFFFF);
                        ASSERT_EQ(sum.data.limb_16.body[i], expected_limb)
                            << "Addition failed at limb " << i << " for values: "
                            << pair.first.str() << " and " << pair.second.str();
                    }
                }
                break;
            }
            case 32: {
                for (const auto& pair : test_values_32) {
                    for (int i = 0; i < num_limbs; i++) {
                        a.data.limb_32.body[i] = static_cast<uint32_t>((pair.first >> (i * 32)) & 0xFFFFFFFF);
                        b.data.limb_32.body[i] = static_cast<uint32_t>((pair.second >> (i * 32)) & 0xFFFFFFFF);
                    }

                    int ret = dap_bigint_2scompl_ripple_carry_adder_value(&a, &b, &sum);
                    ASSERT_EQ(ret, 0) << "Addition operation failed";

                    cpp_int expected = pair.first + pair.second;
                    for (int i = 0; i < num_limbs; i++) {
                        uint32_t expected_limb = static_cast<uint32_t>((expected >> (i * 32)) & 0xFFFFFFFF);
                        ASSERT_EQ(sum.data.limb_32.body[i], expected_limb)
                            << "Addition failed at limb " << i << " for values: "
                            << pair.first.str() << " and " << pair.second.str();
                    }
                }
                break;
            }
            case 64: {
                for (const auto& pair : test_values_64) {
                    a.data.limb_64.body[0] = static_cast<uint64_t>(pair.first & 0xFFFFFFFFFFFFFFFF);
                    b.data.limb_64.body[0] = static_cast<uint64_t>(pair.second & 0xFFFFFFFFFFFFFFFF);

                    int ret = dap_bigint_2scompl_ripple_carry_adder_value(&a, &b, &sum);
                    ASSERT_EQ(ret, 0) << "Addition operation failed";

                    cpp_int expected = pair.first + pair.second;
                    ASSERT_EQ(sum.data.limb_64.body[0], static_cast<uint64_t>(expected & 0xFFFFFFFFFFFFFFFF))
                        << "Addition failed for values: "
                        << pair.first.str() << " and " << pair.second.str();
                }
                break;
            }
            default:
                FAIL() << "Invalid limb size";
        }
    }
};

TEST_F(BigIntAddTest, Addition8) {
    test_addition(8);
}

TEST_F(BigIntAddTest, Addition16) {
    test_addition(16);
}

TEST_F(BigIntAddTest, Addition32) {
    test_addition(32);
}

TEST_F(BigIntAddTest, Addition64) {
    test_addition(64);
}

TEST_F(BigIntAddTest, IncompatibleSizes) {
    dap_bigint_t a, b, sum;
    a.limb_size = 8;
    b.limb_size = 16;
    sum.limb_size = 8;
    
    int ret = dap_bigint_2scompl_ripple_carry_adder_value(&a, &b, &sum);
    ASSERT_EQ(ret, -1) << "Should fail with incompatible sizes";
}

TEST_F(BigIntAddTest, NullPointers) {
    dap_bigint_t a, b, sum;
    a.limb_size = 8;
    b.limb_size = 8;
    sum.limb_size = 8;
    
    int ret = dap_bigint_2scompl_ripple_carry_adder_value(nullptr, &b, &sum);
    ASSERT_EQ(ret, -1) << "Should fail with null pointer";
    
    ret = dap_bigint_2scompl_ripple_carry_adder_value(&a, nullptr, &sum);
    ASSERT_EQ(ret, -1) << "Should fail with null pointer";
    
    ret = dap_bigint_2scompl_ripple_carry_adder_value(&a, &b, nullptr);
    ASSERT_EQ(ret, -1) << "Should fail with null pointer";
} 