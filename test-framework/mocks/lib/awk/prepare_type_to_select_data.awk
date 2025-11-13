#!/usr/bin/gawk -f
# Prepare type-to-selector wrapper macros data
# Input: NORMALIZATION_MACROS (associative array data as macro_key|base_type pairs)
# Output: TYPE_TO_SELECT_DATA (newline-separated macro_key|normalized_key|selector_name|is_pointer)

BEGIN {
    FS = "|"
    TYPE_NORMALIZATION_TABLE["_Bool"] = "bool"
}

# Function to normalize type name (replace * with _PTR, remove invalid chars)
function normalize_type(type) {
    gsub(/\*/, "_PTR", type)
    gsub(/[^a-zA-Z0-9_]/, "_", type)
    return type
}

# Function to escape type name (remove invalid chars)
function escape_type(type) {
    gsub(/[^a-zA-Z0-9_]/, "_", type)
    return type
}

# Process input: macro_key|base_type
{
    if (NF == 2) {
        macro_key = $1
        base_type = $2
        
        if (macro_key == "" || base_type == "") {
            next
        }
        
        normalized_key = normalize_type(macro_key)
        selector_name = "_DAP_MOCK_WRAPPER_CUSTOM_SELECT_" normalized_key
        is_pointer = (macro_key ~ /\*/) ? "1" : "0"
        
        # Output: macro_key|normalized_key|selector_name|is_pointer
        print macro_key "|" normalized_key "|" selector_name "|" is_pointer
        
        # Handle type expansion (e.g., _Bool -> bool)
        for (expanded_type in TYPE_NORMALIZATION_TABLE) {
            if (TYPE_NORMALIZATION_TABLE[expanded_type] == base_type) {
                expanded_macro_name = escape_type(expanded_type)
                expanded_normalized_key = normalize_type(expanded_type)
                expanded_selector_name = "_DAP_MOCK_WRAPPER_CUSTOM_SELECT_" expanded_normalized_key
                
                # Output expanded type mapping
                print expanded_type "|" expanded_normalized_key "|" expanded_selector_name "|0"
            }
        }
    }
}

