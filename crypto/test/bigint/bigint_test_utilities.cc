#include "bigint_test_utilities.h"
#include <sstream>
#include <iomanip>

cpp_int hex_to_cpp_int(const std::string& hex_str) {
    cpp_int result;
    std::stringstream ss;
    ss << std::hex << hex_str;
    ss >> result;
    return result;
}

std::string cpp_int_to_hex(const cpp_int& num) {
    std::stringstream ss;
    ss << std::hex << num;
    return ss.str();
}

void cpp_int_to_dap_bigint(const cpp_int& num, dap_bigint_t* dap_num, int bigint_size, int limb_size) {
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
        switch(limb_size) {
            case 8: {
                uint8_t limb_value = static_cast<uint8_t>(temp & ((cpp_int(1) << 8) - 1));
                dap_set_ith_limb_in_bigint(dap_num, i, &limb_value);
                break;
            }
            case 16: {
                uint16_t limb_value = static_cast<uint16_t>(temp & ((cpp_int(1) << 16) - 1));
                dap_set_ith_limb_in_bigint(dap_num, i, &limb_value);
                break;
            }
            case 32: {
                uint32_t limb_value = static_cast<uint32_t>(temp & ((cpp_int(1) << 32) - 1));
                dap_set_ith_limb_in_bigint(dap_num, i, &limb_value);
                break;
            }
            case 64: {
                uint64_t limb_value = static_cast<uint64_t>(temp & ((cpp_int(1) << 64) - 1));
                dap_set_ith_limb_in_bigint(dap_num, i, &limb_value);
                break;
            }
        }
        temp >>= limb_size;
    }
}

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