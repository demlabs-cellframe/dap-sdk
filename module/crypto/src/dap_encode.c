#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "dap_common.h"
#include "dap_strfuncs.h"
#include "dap_string.h"
#include "dap_encode.h"

#define LOG_TAG "dap_encode"


size_t dap_encode_char_by_char(const char * a_in, size_t a_in_size, uint8_t a_base_size, const char * a_table, char * a_out)
{   // The idea behind this function is that we are moving character by character through the input buffer, considered
    // not as the original char array, but effectively as a bit array.
    // For this reason the algorithm must identify the index of the byte in the initial char array.
    // From there, bitmasks can be applied to the byte to extract the bits needed for the encoding.
    // This is obviously because we cannot access the bits of the char array directly.
    // As inputs to this masking, we need to know:
    // 1. The byte that we need to access in the initial char array.
    // 2. The position of the bit in the byte that we need to access.
    // From there, the core masking operation is quite simple.
    if (NULL == a_in || NULL == a_out || NULL == a_table || a_base_size == 0) {
        return 0;
    }

    size_t l_out_size=a_in_size*8/a_base_size;

    uint8_t* l_in_bytes = (uint8_t*) a_in;

    for (size_t l_index_bit_in_buffer=0; l_index_bit_in_buffer<a_in_size*8; l_index_bit_in_buffer+=a_base_size) {

        size_t l_index_byte_input_buffer=l_index_bit_in_buffer/8;
        size_t l_index_bit_in_byte_input_buffer=l_index_bit_in_buffer%8;

        uint8_t l_left_byte= l_in_bytes[l_index_byte_input_buffer];
        uint8_t l_right_byte= l_in_bytes[l_index_byte_input_buffer+1];

        // Here we need to apply the bitmask to the left and right bytes to extract the bits needed for the encoding.
        // We want the l_index_bit_in_byte_input_buffer bits of the right in the left byte, and 8 minus the 
        // l_index_bit_in_byte_input_buffer bits of the left in the right byte.
        // Then we need to combine the two bytes and apply the bitmask to the combined byte to extract the bits needed for the encoding.

        uint8_t l_left_byte_masked=l_left_byte<<(8-l_index_bit_in_byte_input_buffer);
        uint8_t l_right_byte_masked=l_right_byte>>(l_index_bit_in_byte_input_buffer);
        uint8_t l_combined_byte=l_left_byte_masked|l_right_byte_masked;

 

        // Now we need to map the bits to the output character.
        // The output character is the one that corresponds to the bits in the combined byte.
        
        a_out[l_index_bit_in_buffer/a_base_size]=a_table[l_combined_byte];

    }

    return l_out_size;



}
