#include <gtest/gtest.h>
#include <boost/multiprecision/cpp_int.hpp>
#include <vector>
#include <string>
#include <sstream>
#include <iomanip>
#include "bigint.h"
#include "add.h"

using namespace boost::multiprecision;

class BigIntAdditionTest : public ::testing::TestWithParam<int> {
protected:
    void SetUp() override {
        limb_size = GetParam();
    }
    int limb_size;
};

// Helper function to convert hex string to cpp_int
cpp_int hex_to_cpp_int(const std::string& hex_str) {
    cpp_int result;
    std::stringstream ss;
    ss << std::hex << hex_str;
    ss >> result;
    return result;
}

// Helper function to convert cpp_int to hex string
std::string cpp_int_to_hex(const cpp_int& num) {
    std::stringstream ss;
    ss << std::hex << num;
    return ss.str();
}

// Helper function to convert cpp_int to dap_bigint_t
void cpp_int_to_dap_bigint(const cpp_int& num, dap_bigint_t* dap_num, int bigint_size) {
    dap_num->bigint_size = bigint_size;
    dap_num->limb_size = limb_size;
    dap_num->signedness = SIGNED;
    dap_num->sign = POSITIVE;
    
    // Allocate memory for the body based on limb size
    size_t num_limbs = (bigint_size + limb_size - 1) / limb_size;
    switch(limb_size) {
        case 8:
            dap_num->data.limb_8.body = new uint8_t[num_limbs];
            break;
        case 16:
            dap_num->data.limb_16.body = new uint16_t[num_limbs];
            break;
        case 32:
            dap_num->data.limb_32.body = new uint32_t[num_limbs];
            break;
        case 64:
            dap_num->data.limb_64.body = new uint64_t[num_limbs];
            break;
    }

    // Convert cpp_int to limbs
    cpp_int temp = num;
    for(size_t i = 0; i < num_limbs; i++) {
        uint64_t limb_value = static_cast<uint64_t>(temp & ((cpp_int(1) << limb_size) - 1));
        dap_set_ith_limb_in_bigint(dap_num, i, &limb_value);
        temp >>= limb_size;
    }
}

// Helper function to convert dap_bigint_t to cpp_int
cpp_int dap_bigint_to_cpp_int(const dap_bigint_t* dap_num) {
    cpp_int result = 0;
    size_t num_limbs = dap_num->bigint_size / dap_num->limb_size;
    
    for(size_t i = 0; i < num_limbs; i++) {
        uint64_t limb_value;
        switch(dap_num->limb_size) {
            case 8:
                limb_value = dap_num->data.limb_8.body[i];
                break;
            case 16:
                limb_value = dap_num->data.limb_16.body[i];
                break;
            case 32:
                limb_value = dap_num->data.limb_32.body[i];
                break;
            case 64:
                limb_value = dap_num->data.limb_64.body[i];
                break;
        }
        result |= (cpp_int(limb_value) << (i * dap_num->limb_size));
    }
    return result;
}

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

        for(size_t i = 0; i < test_cases.size(); i++) {
            for(size_t j = 0; j < test_cases.size(); j++) {
                // Convert hex strings to cpp_int
                cpp_int a_boost = hex_to_cpp_int(test_cases[i]);
                cpp_int b_boost = hex_to_cpp_int(test_cases[j]);

                // Create dap_bigint_t instances
                dap_bigint_t a_dap, b_dap, sum_dap;
                cpp_int_to_dap_bigint(a_boost, &a_dap, bigint_size);
                cpp_int_to_dap_bigint(b_boost, &b_dap, bigint_size);
                cpp_int_to_dap_bigint(cpp_int(0), &sum_dap, bigint_size);

                // Perform addition in Boost
                cpp_int sum_boost = a_boost + b_boost;

                // Perform addition in Libdap
                dap_bigint_2scompl_ripple_carry_adder_value(&a_dap, &b_dap, &sum_dap);

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