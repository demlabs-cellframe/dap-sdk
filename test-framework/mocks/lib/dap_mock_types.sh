#!/bin/bash
# Intelligently collect and process types from mock declarations

# Source common utilities (but don't initialize yet - that's done in main script)
LIB_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${LIB_DIR}/dap_mock_common.sh"

# Intelligently collect basic types from actual usage in code
# Input: ALL_TYPES_PAIRS (normalized|original format)
# Output: BASIC_TYPES associative array (base_type -> original_type)
collect_basic_types() {
    declare -gA BASIC_TYPES
    declare -A ALL_TYPES_MAP
    
    # Process all types found in code (return types + parameter types)
    if [ -n "$ALL_TYPES_PAIRS" ]; then
        while IFS='|' read -r normalized original; do
            [ -z "$normalized" ] && continue
            [ -z "$original" ] && continue
            
            # Store original type for this normalized type
            ALL_TYPES_MAP["$normalized"]="$original"
            
            # Extract base type (remove _PTR suffix for pointer types)
            base_type="$normalized"
            if [[ "$normalized" == *_PTR ]]; then
                base_type="${normalized%_PTR}"
            fi
            
            # Check if this is a basic C type (not a struct/typedef)
            # Basic types are: void, int, char, long, short, float, double, size_t, ssize_t, 
            # uint8_t, uint16_t, uint32_t, uint64_t, int8_t, int16_t, int32_t, int64_t, bool
            if [[ "$base_type" =~ ^(void|int|char|long|short|float|double|size_t|ssize_t|uint8_t|uint16_t|uint32_t|uint64_t|int8_t|int16_t|int32_t|int64_t|bool)$ ]]; then
                # Use original type if available, otherwise use normalized base type
                if [ -n "${ALL_TYPES_MAP[$base_type]:-}" ]; then
                    BASIC_TYPES["$base_type"]="${ALL_TYPES_MAP[$base_type]}"
                else
                    BASIC_TYPES["$base_type"]="$base_type"
                fi
            fi
        done <<< "$ALL_TYPES_PAIRS"
    fi
    
    # Always include void (required for void functions)
    BASIC_TYPES["void"]="void"
    
    export BASIC_TYPES
}

# Generate return type macros file - single template call
# Input: RETURN_TYPES, RETURN_TYPES_PAIRS, ALL_TYPES_PAIRS, ORIGINAL_TYPES
# Output: RETURN_TYPE_MACROS_FILE
generate_return_type_macros() {
    local return_type_macros_file="$1"
    
    # Collect basic types intelligently
    collect_basic_types
    
    # Prepare ORIGINAL_TYPES data as string for template
    local ORIGINAL_TYPES_DATA=""
    if [ -n "$RETURN_TYPES_PAIRS" ]; then
        ORIGINAL_TYPES_DATA="$RETURN_TYPES_PAIRS"
    fi
    
    # Prepare BASIC_TYPES data as string for template
    local BASIC_TYPES_RAW_DATA=""
    for basic_type in "${!BASIC_TYPES[@]}"; do
        original_type="${BASIC_TYPES[$basic_type]}"
        BASIC_TYPES_RAW_DATA="${BASIC_TYPES_RAW_DATA}${basic_type}|${original_type}"$'\n'
    done
    
    # Single template call - template processes all data using AWK sections
    # All raw data passed as strings, template does all processing
    replace_template_placeholders_with_mocking \
        "${TEMPLATES_DIR}/return_type_macros.h.tpl" \
        "$return_type_macros_file" \
        "RETURN_TYPES=$RETURN_TYPES" \
        "ORIGINAL_TYPES_DATA=$ORIGINAL_TYPES_DATA" \
        "BASIC_TYPES_RAW_DATA=$BASIC_TYPES_RAW_DATA"
}


