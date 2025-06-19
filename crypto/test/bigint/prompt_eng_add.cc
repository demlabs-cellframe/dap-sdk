#include <gtest/gtest.h>
#include <boost/multiprecision/cpp_int.hpp>
#include <vector>
#include <string>
#include <sstream>
#include <iomanip>
#include "bigint.h"
#include "add_specific_limb_size.h"
#include "bigint_test_utilities.h"

using namespace boost::multiprecision;

class BigIntAdditionTest : public ::testing::TestWithParam<int> {
protected:
    void SetUp() override {
        limb_size = GetParam();
    }
    int limb_size;

};

TEST_P(BigIntAdditionTest, CompareWithBoost) {
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

        //The reason why there are two nested loops is because we want to test all possible combinations of the test cases.
        //We are taking two different values from the test cases and adding them together.
      
        for(size_t i = 0; i < test_cases.size(); i++) {
            for(size_t j = 0; j < test_cases.size(); j++) {

                // Convert hex strings to cpp_int
                cpp_int a_boost = hex_to_cpp_int(test_cases[i]);
                cpp_int b_boost = hex_to_cpp_int(test_cases[j]);

                // Create dap_bigint_t instances
                // It is important to note that the i and j indices here are looping through 
                // the test cases, 
                dap_bigint_t a_dap, b_dap, sum_dap;
                cpp_int_to_dap_bigint(a_boost, &a_dap, bigint_size, limb_size);
                cpp_int_to_dap_bigint(b_boost, &b_dap, bigint_size, limb_size);
                cpp_int_to_dap_bigint(cpp_int(0), &sum_dap, bigint_size, limb_size);

                // Perform addition in Boost
                cpp_int sum_boost = a_boost + b_boost;

                // Perform addition in Libdap
                int ret;
                switch(limb_size) {
                    case 8:
                        ret = dap_bigint_2scompl_ripple_carry_adder_value_8(&a_dap, &b_dap, &sum_dap);
                        break;
                    case 16:
                        ret = dap_bigint_2scompl_ripple_carry_adder_value_16(&a_dap, &b_dap, &sum_dap);
                        break;
                    case 32:
                        ret = dap_bigint_2scompl_ripple_carry_adder_value_32(&a_dap, &b_dap, &sum_dap);
                        break;
                    case 64:
                        ret = dap_bigint_2scompl_ripple_carry_adder_value_64(&a_dap, &b_dap, &sum_dap);
                        break;
                    default:
                        FAIL() << "Unsupported limb size: " << limb_size;
                }

                // Convert Libdap result back to cpp_int for comparison
                cpp_int sum_dap_cpp = dap_bigint_to_cpp_int(&sum_dap);

                // Compare results
                EXPECT_EQ(sum_boost, sum_dap_cpp) 
                    << "Failed for bigint_size=" << bigint_size 
                    << ", limb_size=" << limb_size
                    << "\nInput a: " << test_cases[i]
                    << "\nInput b: " << test_cases[j]
                    << "\nBoost result: " << sum_boost
                    << "\nLibdap result: " << sum_dap_cpp;

                // Clean up allocated memory
                switch(limb_size) {
                    case 8:
                        delete[] a_dap.data.limb_8.body;
                        delete[] b_dap.data.limb_8.body;
                        delete[] sum_dap.data.limb_8.body;
                        break;
                    case 16:
                        delete[] a_dap.data.limb_16.body;
                        delete[] b_dap.data.limb_16.body;
                        delete[] sum_dap.data.limb_16.body;
                        break;
                    case 32:
                        delete[] a_dap.data.limb_32.body;
                        delete[] b_dap.data.limb_32.body;
                        delete[] sum_dap.data.limb_32.body;
                        break;
                    case 64:
                        delete[] a_dap.data.limb_64.body;
                        delete[] b_dap.data.limb_64.body;
                        delete[] sum_dap.data.limb_64.body;
                        break;
                }
            }
        }
    }
}

INSTANTIATE_TEST_SUITE_P(
    LimbSizes,
    BigIntAdditionTest,
    ::testing::Values(8, 16, 32, 64)
); 