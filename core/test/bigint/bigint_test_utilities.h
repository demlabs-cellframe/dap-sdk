#ifndef BIGINT_TEST_UTILITIES_H
#define BIGINT_TEST_UTILITIES_H

#include <boost/multiprecision/cpp_int.hpp>
#include <string>
#include "bigint.h"

using namespace boost::multiprecision;

// Helper function to convert hex string to cpp_int
cpp_int hex_to_cpp_int(const std::string& hex_str);

// Helper function to convert cpp_int to hex string
std::string cpp_int_to_hex(const cpp_int& num);

// Helper function to convert cpp_int to dap_bigint_t
void cpp_int_to_dap_bigint(const cpp_int& num, dap_bigint_t* dap_num, int bigint_size, int limb_size);

// Helper function to convert dap_bigint_t to cpp_int
cpp_int dap_bigint_to_cpp_int(const dap_bigint_t* dap_num);

#endif // BIGINT_TEST_UTILITIES_H 