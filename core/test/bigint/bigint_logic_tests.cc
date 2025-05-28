#include <gtest/gtest.h>
#include "bigint.h"
#include <random>
#include <vector>

class BigIntLogicTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize test data
        test_values_8 = {
            {0x00, 0x00},  // All zeros
            {0xFF, 0xFF},  // All ones
            {0x55, 0xAA},  // Alternating bits
            {0x0F, 0xF0},  // Half ones
            {0x33, 0xCC}   // Quarter ones
        };
        
        test_values_16 = {
            {0x0000, 0x0000},
            {0xFFFF, 0xFFFF},
            {0x5555, 0xAAAA},
            {0x0F0F, 0xF0F0},
            {0x3333, 0xCCCC}
        };
        
        test_values_32 = {
            {0x00000000, 0x00000000},
            {0xFFFFFFFF, 0xFFFFFFFF},
            {0x55555555, 0xAAAAAAAA},
            {0x0F0F0F0F, 0xF0F0F0F0},
            {0x33333333, 0xCCCCCCCC}
        };
        
        test_values_64 = {
            {0x0000000000000000, 0x0000000000000000},
            {0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF},
            {0x5555555555555555, 0xAAAAAAAAAAAAAAAA},
            {0x0F0F0F0F0F0F0F0F, 0xF0F0F0F0F0F0F0F0},
            {0x3333333333333333, 0xCCCCCCCCCCCCCCCC}
        };
    }

    std::vector<std::pair<uint8_t, uint8_t>> test_values_8;
    std::vector<std::pair<uint16_t, uint16_t>> test_values_16;
    std::vector<std::pair<uint32_t, uint32_t>> test_values_32;
    std::vector<std::pair<uint64_t, uint64_t>> test_values_64;

    void test_logical_op(int limb_size, int logical_op) {
        dap_bigint_t a, b, result;
        a.limb_size = limb_size;
        b.limb_size = limb_size;
        result.limb_size = limb_size;

        switch(limb_size) {
            case 8: {
                for (const auto& pair : test_values_8) {
                    for (long limb_count=0;limb_count<a.limb_size;limb_count++){
                        a.data.limb_8.body[limb_count] = static_cast<uint8_t>(pair.first);
                        b.data.limb_8.body[limb_count] = static_cast<uint8_t>(pair.second);
                    }

                    int ret = dap_bitwise_logical_op(&a, &b, &result, logical_op);
                    ASSERT_EQ(ret, 0) << "Logical operation failed";

                    uint8_t expected;
                    switch(logical_op) {
                        case AND_OP:
                            expected = static_cast<uint8_t>(pair.first & pair.second);
                            break;
                        case OR_OP:
                            expected = static_cast<uint8_t>(pair.first | pair.second);
                            break;
                        case XOR_OP:
                            expected = static_cast<uint8_t>(pair.first ^ pair.second);
                            break;
                        default:
                            FAIL() << "Invalid logical operation";
                    }
                    for (long limb_count=0;limb_count<a.limb_size;limb_count++){
                    ASSERT_EQ(result.data.limb_8.body[limb_count], expected) 
                        << "Logical operation failed for values: " 
                        << std::hex << static_cast<int>(pair.first) << " and " << static_cast<int>(pair.second);
                    }
                }
                break;
            }
            case 16: {
                for (const auto& pair : test_values_16) {
                    for (long limb_count=0;limb_count<a.limb_size;limb_count++){
                        a.data.limb_16.body[limb_count] = static_cast<uint16_t>(pair.first);
                        b.data.limb_16.body[limb_count] = static_cast<uint16_t>(pair.second);
                    }

                    int ret = dap_bitwise_logical_op(&a, &b, &result, logical_op);
                    ASSERT_EQ(ret, 0) << "Logical operation failed";

                    uint16_t expected;
                    switch(logical_op) {
                        case AND_OP:
                            expected = static_cast<uint16_t>(pair.first & pair.second);
                            break;
                        case OR_OP:
                            expected = static_cast<uint16_t>(pair.first | pair.second);
                            break;
                        case XOR_OP:
                            expected = static_cast<uint16_t>(pair.first ^ pair.second);
                            break;
                        default:
                            FAIL() << "Invalid logical operation";
                    }
                    for (long limb_count=0;limb_count<a.limb_size;limb_count++){
                        ASSERT_EQ(result.data.limb_16.body[limb_count], expected) 
                            << "Logical operation failed for values: " 
                            << std::hex << static_cast<int>(pair.first) << " and " << static_cast<int>(pair.second);
                    }
                }
                break;
            }
            case 32: {
                for (const auto& pair : test_values_32) {
                    for (long limb_count=0;limb_count<a.limb_size;limb_count++){
                        a.data.limb_32.body[limb_count] = static_cast<uint32_t>(pair.first);
                        b.data.limb_32.body[limb_count] = static_cast<uint32_t>(pair.second);
                    }

                    int ret = dap_bitwise_logical_op(&a, &b, &result, logical_op);
                    ASSERT_EQ(ret, 0) << "Logical operation failed";

                    uint32_t expected;
                    switch(logical_op) {
                        case AND_OP:
                            expected = static_cast<uint32_t>(pair.first & pair.second);
                            break;
                        case OR_OP:
                            expected = static_cast<uint32_t>(pair.first | pair.second);
                            break;
                        case XOR_OP:
                            expected = static_cast<uint32_t>(pair.first ^ pair.second);
                            break;
                        default:
                            FAIL() << "Invalid logical operation";
                    }
                    for (long limb_count=0;limb_count<a.limb_size;limb_count++){
                        ASSERT_EQ(result.data.limb_32.body[limb_count], expected) 
                            << "Logical operation failed for values: " 
                            << std::hex << static_cast<int>(pair.first) << " and " << static_cast<int>(pair.second);
                    }
                }
                break;
            }
            case 64: {
                for (const auto& pair : test_values_64) {
                    for (long limb_count=0;limb_count<a.limb_size;limb_count++){
                        a.data.limb_64.body[limb_count] = pair.first;
                        b.data.limb_64.body[limb_count] = pair.second;
                    }

                    int ret = dap_bitwise_logical_op(&a, &b, &result, logical_op);
                    ASSERT_EQ(ret, 0) << "Logical operation failed";

                    uint64_t expected;
                    switch(logical_op) {
                        case AND_OP:
                            expected = pair.first & pair.second;
                            break;
                        case OR_OP:
                            expected = pair.first | pair.second;
                            break;
                        case XOR_OP:
                            expected = pair.first ^ pair.second;
                            break;
                        default:
                            FAIL() << "Invalid logical operation";
                    }
                    for (long limb_count=0;limb_count<a.limb_size;limb_count++){
                        ASSERT_EQ(result.data.limb_64.body[limb_count], expected) 
                            << "Logical operation failed for values: " 
                            << std::hex << pair.first << " and " << pair.second;
                    }
                }
                break;
            }
            default:
                FAIL() << "Invalid limb size";
        }
    }
};

TEST_F(BigIntLogicTest, AndOperation8) {
    test_logical_op(8, AND_OP);
}

TEST_F(BigIntLogicTest, OrOperation8) {
    test_logical_op(8, OR_OP);
}

TEST_F(BigIntLogicTest, XorOperation8) {
    test_logical_op(8, XOR_OP);
}

TEST_F(BigIntLogicTest, AndOperation16) {
    test_logical_op(16, AND_OP);
}

TEST_F(BigIntLogicTest, OrOperation16) {
    test_logical_op(16, OR_OP);
}

TEST_F(BigIntLogicTest, XorOperation16) {
    test_logical_op(16, XOR_OP);
}

TEST_F(BigIntLogicTest, AndOperation32) {
    test_logical_op(32, AND_OP);
}

TEST_F(BigIntLogicTest, OrOperation32) {
    test_logical_op(32, OR_OP);
}

TEST_F(BigIntLogicTest, XorOperation32) {
    test_logical_op(32, XOR_OP);
}

TEST_F(BigIntLogicTest, AndOperation64) {
    test_logical_op(64, AND_OP);
}

TEST_F(BigIntLogicTest, OrOperation64) {
    test_logical_op(64, OR_OP);
}

TEST_F(BigIntLogicTest, XorOperation64) {
    test_logical_op(64, XOR_OP);
}

TEST_F(BigIntLogicTest, InvalidOperation) {
    dap_bigint_t a, b, result;
    a.bigint_size = 8;
    b.bigint_size = 8;
    result.bigint_size = 8;
    
    // Test with invalid operation
    int ret = dap_bitwise_logical_op(&a, &b, &result, 999);
    ASSERT_EQ(ret, -1) << "Should fail with invalid operation";
}

TEST_F(BigIntLogicTest, IncompatibleSizes) {
    dap_bigint_t a, b, result;
    a.bigint_size = 8;
    b.bigint_size = 16;  // Different size
    result.bigint_size = 8;
    
    int ret = dap_bitwise_logical_op(&a, &b, &result, AND_OP);
    ASSERT_EQ(ret, -1) << "Should fail with incompatible sizes";
} 