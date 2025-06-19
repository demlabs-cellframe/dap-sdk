#include <gtest/gtest.h>
#include <boost/multiprecision/cpp_int.hpp>
#include <vector>
#include <string>
#include <sstream>
#include <iomanip>
#include "bigint.h"
#include "bigint_test_utilities.h"

using namespace boost::multiprecision;

class BigIntLogicTest : public ::testing::TestWithParam<std::pair<int, int>> {
protected:
    void SetUp() override {
        limb_size = GetParam().first;
        logical_op = GetParam().second;
    }
    int limb_size;
    int logical_op;
};

TEST_P(BigIntLogicTest, CompareWithBoost) {
    // Test different bigint sizes from 1 to 50000
    for(int bigint_size = 1; bigint_size <= 50000; bigint_size++) {
        // Define interesting edge cases for this size
        std::vector<std::string> test_cases = {
            "0x0",  // Zero
            "0x1",  // One
            "0x" + std::string(bigint_size/4, 'F'),  // All ones
            "0x" + std::string(bigint_size/4, 'F').substr(0, bigint_size/4-1) + "E",  // All ones except last digit
            "0x" + std::string(bigint_size/4, '0') + "1"  // One at the end
        };

        // Test all combinations of test cases
        for(size_t i = 0; i < test_cases.size(); i++) {
            for(size_t j = 0; j < test_cases.size(); j++) {
                // Convert hex strings to cpp_int
                cpp_int a_boost = hex_to_cpp_int(test_cases[i]);
                cpp_int b_boost = hex_to_cpp_int(test_cases[j]);

                // Create dap_bigint_t instances
                dap_bigint_t a_dap, b_dap, result_dap;
                cpp_int_to_dap_bigint(a_boost, &a_dap, bigint_size, limb_size);
                cpp_int_to_dap_bigint(b_boost, &b_dap, bigint_size, limb_size);
                cpp_int_to_dap_bigint(cpp_int(0), &result_dap, bigint_size, limb_size);

                // Perform logical operation in Boost
                cpp_int result_boost;
                switch(logical_op) {
                    case AND_OP:
                        result_boost = a_boost & b_boost;
                        break;
                    case OR_OP:
                        result_boost = a_boost | b_boost;
                        break;
                    case XOR_OP:
                        result_boost = a_boost ^ b_boost;
                        break;
                    default:
                        FAIL() << "Invalid logical operation: " << logical_op;
                }

                // Perform logical operation in Libdap
                int ret = dap_bitwise_logical_op(&a_dap, &b_dap, &result_dap, logical_op);
                ASSERT_EQ(ret, 0) << "Logical operation failed";

                // Convert Libdap result back to cpp_int for comparison
                cpp_int result_dap_cpp = dap_bigint_to_cpp_int(&result_dap);

                // Compare results
                EXPECT_EQ(result_boost, result_dap_cpp) 
                    << "Failed for bigint_size=" << bigint_size 
                    << ", limb_size=" << limb_size
                    << ", logical_op=" << logical_op
                    << "\nInput a: " << test_cases[i]
                    << "\nInput b: " << test_cases[j]
                    << "\nBoost result: " << result_boost
                    << "\nLibdap result: " << result_dap_cpp;

                // Clean up allocated memory
                switch(limb_size) {
                    case 8:
                        delete[] a_dap.data.limb_8.body;
                        delete[] b_dap.data.limb_8.body;
                        delete[] result_dap.data.limb_8.body;
                        break;
                    case 16:
                        delete[] a_dap.data.limb_16.body;
                        delete[] b_dap.data.limb_16.body;
                        delete[] result_dap.data.limb_16.body;
                        break;
                    case 32:
                        delete[] a_dap.data.limb_32.body;
                        delete[] b_dap.data.limb_32.body;
                        delete[] result_dap.data.limb_32.body;
                        break;
                    case 64:
                        delete[] a_dap.data.limb_64.body;
                        delete[] b_dap.data.limb_64.body;
                        delete[] result_dap.data.limb_64.body;
                        break;
                }
            }
        }
    }
}

// Test invalid operation
TEST_F(BigIntLogicTest, InvalidOperation) {
    dap_bigint_t a, b, result;
    a.bigint_size = 8;
    b.bigint_size = 8;
    result.bigint_size = 8;
    
    // Test with invalid operation
    int ret = dap_bitwise_logical_op(&a, &b, &result, 999);
    ASSERT_EQ(ret, -1) << "Should fail with invalid operation";
}

// Test incompatible sizes
TEST_F(BigIntLogicTest, IncompatibleSizes) {
    dap_bigint_t a, b, result;
    a.bigint_size = 8;
    b.bigint_size = 16;  // Different size
    result.bigint_size = 8;
    
    int ret = dap_bitwise_logical_op(&a, &b, &result, AND_OP);
    ASSERT_EQ(ret, -1) << "Should fail with incompatible sizes";
}

// Instantiate test suite with all combinations of limb sizes and logical operations
INSTANTIATE_TEST_SUITE_P(
    LogicOperations,
    BigIntLogicTest,
    ::testing::Values(
        std::make_pair(8, AND_OP),
        std::make_pair(8, OR_OP),
        std::make_pair(8, XOR_OP),
        std::make_pair(16, AND_OP),
        std::make_pair(16, OR_OP),
        std::make_pair(16, XOR_OP),
        std::make_pair(32, AND_OP),
        std::make_pair(32, OR_OP),
        std::make_pair(32, XOR_OP),
        std::make_pair(64, AND_OP),
        std::make_pair(64, OR_OP),
        std::make_pair(64, XOR_OP)
    )
); 