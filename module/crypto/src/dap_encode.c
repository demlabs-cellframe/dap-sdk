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
{
    if (NULL == a_in || NULL == a_out || NULL == a_table || a_base_size == 0) {
        return 0;
    }

    char* l_in_bytes = (char*) a_in;
    int l_index_bit_in_buffer=0;

    while (l_index_bit_in_buffer < a_in_size*8) {
        



}
