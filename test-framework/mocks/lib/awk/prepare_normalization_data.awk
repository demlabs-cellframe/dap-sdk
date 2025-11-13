#!/usr/bin/gawk -f
# Prepare normalization macros data from NORMALIZATION_MACROS associative array
# Input: macro_key|base_type (one per line)
# Output: NORMALIZATION_MACROS_DATA (newline-separated macro_key|base_type|normalized_key|escaped_base_key|escaped_original_key|escaped_macro_key_for_escape)

BEGIN {
    FS = "|"
}

# Function to escape type name (replace * with _PTR, remove invalid chars)
function escape_type(type) {
    gsub(/\*/, "_PTR", type)
    gsub(/[^a-zA-Z0-9_]/, "_", type)
    return type
}

# Function to normalize type name (replace * with _PTR, remove invalid chars)
function normalize_type(type) {
    gsub(/\*/, "_PTR", type)
    gsub(/[^a-zA-Z0-9_]/, "_", type)
    return type
}

# Function to get escaped macro key for escape (remove trailing spaces before escaping)
function escape_macro_key_for_escape(type) {
    gsub(/[ \t]+$/, "", type)
    gsub(/\*/, "_PTR", type)
    gsub(/[^a-zA-Z0-9_]/, "_", type)
    return type
}

# Function to escape macro key for raw type (for _DAP_MOCK_TRANSFORM_TYPE_HELPER with *)
# This creates a valid macro name from a type with * by replacing * with _STAR
function escape_macro_key_for_raw_type(type) {
    gsub(/[ \t]+$/, "", type)
    gsub(/\*/, "_STAR", type)
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
        escaped_base_key = escape_type(base_type)
        escaped_original_key = normalize_type(macro_key)
        escaped_macro_key_for_escape = escape_macro_key_for_escape(macro_key)
        escaped_macro_key_for_raw_type = escape_macro_key_for_raw_type(macro_key)
        
        # Output: macro_key|base_type|normalized_key|escaped_base_key|escaped_original_key|escaped_macro_key_for_escape|escaped_macro_key_for_raw_type
        print macro_key "|" base_type "|" normalized_key "|" escaped_base_key "|" escaped_original_key "|" escaped_macro_key_for_escape "|" escaped_macro_key_for_raw_type
    }
}
