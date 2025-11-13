#!/bin/bash
# Parse DAP_MOCK_WRAPPER_CUSTOM declarations and extract type information

# Source common utilities (but don't initialize yet - that's done in main script)
LIB_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${LIB_DIR}/dap_mock_common.sh"

# Parse source files and extract parameter counts, return types, and all types
# Sets global variables: PARAM_COUNTS_ARRAY, RETURN_TYPES, RETURN_TYPES_PAIRS, ALL_TYPES_PAIRS, ORIGINAL_TYPES
parse_mock_declarations() {
    local source_files=("$@")
    
    local tmp_param_counts=$(create_temp_file "param_counts")
    local tmp_return_types=$(create_temp_file "return_types")
    local tmp_all_types=$(create_temp_file "all_types")
    
    > "$tmp_param_counts"
    > "$tmp_return_types"
    > "$tmp_all_types"
    
    # Scan all source files for DAP_MOCK_WRAPPER_CUSTOM and variants
    for source_file in "${source_files[@]}"; do
        [ ! -f "$source_file" ] && continue
        
        # Use awk to parse DAP_MOCK_WRAPPER_CUSTOM declarations (and variants)
        # Extract return_type and count PARAM(...) entries between macro and opening brace {
        # Handle multi-line declarations
        gawk -f "${MOCK_AWK_DIR}/count_params.awk" "$source_file" >> "$tmp_param_counts"
        
        # Second pass: extract return types (both normalized and original)
        gawk -f "${MOCK_AWK_DIR}/extract_return_types.awk" "$source_file" >> "$tmp_return_types"
        
        # Third pass: extract all types (return types + parameter types from PARAM(...))
        gawk -f "${MOCK_AWK_DIR}/extract_all_types.awk" "$source_file" >> "$tmp_all_types"
    done
    
    # Collect unique parameter counts
    local param_counts=$(sort -u -n "$tmp_param_counts" 2>/dev/null | tr '\n' ' ')
    rm -f "$tmp_param_counts"
    
    # Collect unique return types (normalized|original format)
    # Keep as newline-separated list to avoid issues with spaces in type names
    RETURN_TYPES_PAIRS=$(sort -u "$tmp_return_types" 2>/dev/null | grep -v '^$')
    rm -f "$tmp_return_types"
    
    # Collect all unique types (return types + parameter types)
    # This will be used to intelligently determine which basic types to generate macros for
    ALL_TYPES_PAIRS=$(sort -u "$tmp_all_types" 2>/dev/null | grep -v '^$')
    rm -f "$tmp_all_types"
    
    # Extract normalized types and original types separately
    RETURN_TYPES=""
    declare -gA ORIGINAL_TYPES
    if [ -n "$RETURN_TYPES_PAIRS" ]; then
        while IFS='|' read -r normalized original; do
            [ -z "$normalized" ] && continue
            RETURN_TYPES="$RETURN_TYPES $normalized"
            ORIGINAL_TYPES["$normalized"]="$original"
        done <<< "$RETURN_TYPES_PAIRS"
        RETURN_TYPES=$(echo "$RETURN_TYPES" | tr ' ' '\n' | sort -u | tr '\n' ' ')
    fi
    
    # Process parameter counts
    if [ -z "$param_counts" ] || [ "$param_counts" = " " ]; then
        param_counts="0"  # At least need _DAP_MOCK_MAP_0 for empty case
    fi
    
    # Convert to array and ensure we have at least 0
    declare -ga PARAM_COUNTS_ARRAY
    PARAM_COUNTS_ARRAY=($param_counts)
    if [ ${#PARAM_COUNTS_ARRAY[@]} -eq 0 ] || [ -z "${PARAM_COUNTS_ARRAY[0]}" ]; then
        PARAM_COUNTS_ARRAY=(0)
    fi
    
    # Find maximum parameter count to determine max args count
    local max_param_count=0
    for count in "${PARAM_COUNTS_ARRAY[@]}"; do
        [ -z "$count" ] && continue
        # Convert to integer, default to 0 if invalid
        local count_int=$((count + 0))
        [ "$count_int" -gt "$max_param_count" ] && max_param_count=$count_int
    done
    
    # Max args = max params * 2 (each PARAM expands to 2 args: type, name)
    # Add some safety margin (1 extra param = 2 extra args) for edge cases
    # Ensure at least 2 for empty case (need at least 2 args for _DAP_MOCK_NARGS to work)
    declare -gi MAX_ARGS_COUNT=$((max_param_count * 2 + 2))
    # Ensure minimum of 2 for empty case
    [ "$MAX_ARGS_COUNT" -lt 2 ] && MAX_ARGS_COUNT=2
    
    print_success "Found parameter counts: ${PARAM_COUNTS_ARRAY[*]}"
    
    # Export variables for use in other modules
    export PARAM_COUNTS_ARRAY
    export RETURN_TYPES
    export RETURN_TYPES_PAIRS
    export ALL_TYPES_PAIRS
    export MAX_ARGS_COUNT
}

