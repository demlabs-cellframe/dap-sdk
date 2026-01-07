#!/usr/bin/gawk -f
# Prepare basic types data for template generation
# Input: BASIC_TYPES (associative array data as basic_type|original_type pairs)
# Output: BASIC_TYPES_DATA (newline-separated basic_type|normalized_key|escaped_base_key|selector_name)

BEGIN {
    FS = "|"
}

# Function to normalize type name (remove invalid chars)
function normalize_type(type) {
    gsub(/[^a-zA-Z0-9_]/, "_", type)
    return type
}

# Process input: basic_type|original_type
{
    if (NF == 2) {
        basic_type = $1
        original_type = $2
        
        if (basic_type == "" || original_type == "") {
            next
        }
        
        normalized_key = normalize_type(basic_type)
        escaped_base_key = normalize_type(basic_type)
        selector_name = "_DAP_MOCK_WRAPPER_CUSTOM_SELECT_" normalized_key
        
        # Output: basic_type|normalized_key|escaped_base_key|selector_name
        print basic_type "|" normalized_key "|" escaped_base_key "|" selector_name
    }
}

