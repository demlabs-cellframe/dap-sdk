/**
 * @brief dap_encode_char_by_char Encodes input buffer byte by byte using a moving window of bits
 * @param[in] a_in Pointer to input buffer
 * @param[in] a_in_size Size of input buffer
 * @param[in] a_base_size Encoding base size (e.g., 64 for base64, 32 for base32)
 * @param[in] a_table Conversion table for mapping bit patterns to encoded characters
 * @param[out] a_out Pointer to output buffer
 * @return Size of the encoded output
 */
size_t dap_encode_char_by_char_ai(const char * a_in, size_t a_in_size, uint8_t a_base_size, const char * a_table, char * a_out)
{
    if (NULL == a_in || NULL == a_out || NULL == a_table || a_base_size == 0) {
        return 0;
    }

    // Calculate bits per character: log2(base_size)
    uint8_t l_bits_per_char = 0;
    uint8_t l_temp_base = a_base_size;
    while (l_temp_base > 1) {
        l_temp_base >>= 1;
        l_bits_per_char++;
    }

    if (l_bits_per_char == 0 || l_bits_per_char > 8) {
        return 0; // Invalid base size
    }

    const uint8_t * l_in_bytes = (const uint8_t *) a_in;
    size_t l_out_size = 0;
    uint8_t l_window_input_bit = 0; // Bit position within the current byte

    // Iterate byte by byte through the input buffer
    for (size_t i = 0; i < a_in_size; i++) {
        uint8_t l_current_byte = l_in_bytes[i];
        uint8_t l_next_byte = (i + 1 < a_in_size) ? l_in_bytes[i + 1] : 0;

        // Extract bits from the moving window that spans current and next byte
        // The window size is l_bits_per_char bits
        uint8_t l_bits_remaining_in_current = 8 - l_window_input_bit;
        
        if (l_bits_remaining_in_current >= l_bits_per_char) {
            // All bits needed are in the current byte
            uint8_t l_mask = ((1 << l_bits_per_char) - 1) << (8 - l_window_input_bit - l_bits_per_char);
            uint8_t l_bit_pattern = (l_current_byte & l_mask) >> (8 - l_window_input_bit - l_bits_per_char);
            a_out[l_out_size++] = a_table[l_bit_pattern];
            l_window_input_bit += l_bits_per_char;
            
            // If we've consumed the entire byte, move to next byte
            if (l_window_input_bit >= 8) {
                l_window_input_bit -= 8;
                // Continue processing if there are more bits to extract from this byte
                if (l_window_input_bit > 0 && i + 1 < a_in_size) {
                    // Extract remaining bits spanning to next byte
                    uint8_t l_bits_from_current = 8 - l_window_input_bit;
                    uint8_t l_bits_from_next = l_bits_per_char - l_bits_from_current;
                    
                    uint8_t l_part1 = (l_current_byte & ((1 << l_bits_from_current) - 1)) << l_bits_from_next;
                    uint8_t l_part2 = (l_next_byte >> (8 - l_bits_from_next)) & ((1 << l_bits_from_next) - 1);
                    uint8_t l_bit_pattern = l_part1 | l_part2;
                    
                    a_out[l_out_size++] = a_table[l_bit_pattern];
                    l_window_input_bit = l_bits_from_next;
                } else {
                    l_window_input_bit = 0;
                }
            }
        } else {
            // Bits span across current and next byte
            uint8_t l_bits_from_current = l_bits_remaining_in_current;
            uint8_t l_bits_from_next = l_bits_per_char - l_bits_from_current;
            
            // Extract bits from current byte
            uint8_t l_part1 = (l_current_byte & ((1 << l_bits_from_current) - 1)) << l_bits_from_next;
            
            // Extract bits from next byte
            uint8_t l_part2 = (l_next_byte >> (8 - l_bits_from_next)) & ((1 << l_bits_from_next) - 1);
            
            // Combine and map to encoded character
            uint8_t l_bit_pattern = l_part1 | l_part2;
            a_out[l_out_size++] = a_table[l_bit_pattern];
            
            l_window_input_bit = l_bits_from_next;
        }
    }

    return l_out_size;
}


size_t dap_encode_char_by_char_anton(const char * a_in, size_t a_in_size, uint8_t a_base_size, const char * a_table, char * a_out)
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
    int l_index_bit_in_buffer=0;

    for (int l_index_bit_in_buffer=0; l_index_bit_in_buffer<a_in_size*8; l_index_bit_in_buffer+=a_base_size) {

        int l_index_byte_input_buffer=l_index_bit_in_buffer/8;
        int l_index_bit_in_byte_input_buffer=l_index_bit_in_buffer%8;

        uint8_t l_left_byte= l_index_bit_in_buffer[l_index_byte_input_buffer];
        uint8_t l_right_byte= l_index_bit_in_buffer[l_index_byte_input_buffer+1];

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
