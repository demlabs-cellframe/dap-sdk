#!/bin/bash
# Scan source files for mock declarations and wrapper definitions

# Source common utilities (but don't initialize yet - that's done in main script)
LIB_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${LIB_DIR}/dap_mock_common.sh"

# Scan for mock declarations (DAP_MOCK_DECLARE, DAP_MOCK_DECLARE_CUSTOM)
# Output: newline-separated list of function names
# Optimized: single gawk invocation for all files instead of per-file
scan_mock_declarations() {
    local source_files=("$@")
    local tmp_mocks=$(create_temp_file "mock_funcs")
    
    # Filter existing files
    local existing_files=()
    for source_file in "${source_files[@]}"; do
        [ -f "$source_file" ] && existing_files+=("$source_file")
    done
    
    if [ ${#existing_files[@]} -eq 0 ]; then
        echo ""
        return
    fi
    
    # Single gawk invocation for all files (much faster than per-file)
    gawk -f "${MOCK_AWK_DIR}/scan_mock_declarations.awk" "${existing_files[@]}" > "$tmp_mocks" 2>/dev/null || true
    
    local mock_functions=$(sort -u "$tmp_mocks" 2>/dev/null | grep -v '^$')
    rm -f "$tmp_mocks"
    
    echo "$mock_functions"
}

# Scan for existing wrapper definitions
# Output: newline-separated list of function names that have wrappers
# Optimized: single gawk invocation for all files instead of per-file
scan_wrapper_definitions() {
    local source_files=("$@")
    local tmp_wrappers=$(create_temp_file "wrapper_funcs")
    
    # Filter existing files
    local existing_files=()
    for source_file in "${source_files[@]}"; do
        [ -f "$source_file" ] && existing_files+=("$source_file")
    done
    
    if [ ${#existing_files[@]} -eq 0 ]; then
        echo ""
        return
    fi
    
    # Single gawk invocation for all files (much faster than per-file)
    gawk -f "${MOCK_AWK_DIR}/scan_wrapper_definitions.awk" "${existing_files[@]}" > "$tmp_wrappers" 2>/dev/null || true
    
    local wrapper_functions=$(sort -u "$tmp_wrappers" 2>/dev/null | grep -v '^$')
    rm -f "$tmp_wrappers"
    
    echo "$wrapper_functions"
}

# Extract custom mock declarations with full information
# Output: return_type|func_name|param_list|macro_type (one per declaration)
# Usage: extract_custom_mocks <output_file> <source_file1> <source_file2> ...
# Optimized: single gawk invocation for all files instead of per-file
extract_custom_mocks() {
    local output_file="$1"
    shift
    local source_files=("$@")
    
    > "$output_file"
    
    # Filter existing files
    local existing_files=()
    for source_file in "${source_files[@]}"; do
        [ -f "$source_file" ] && existing_files+=("$source_file")
    done
    
    if [ ${#existing_files[@]} -eq 0 ]; then
        return 0
    fi
    
    # Single gawk invocation for all files (much faster than per-file)
    # Use awk script to parse DAP_MOCK_WRAPPER_CUSTOM declarations and extract:
    # - return_type (original, with *)
    # - func_name
    # - parameters list (type and name from PARAM(...) or void)
    gawk -f "${MOCK_AWK_DIR}/parse_custom_mocks.awk" "${existing_files[@]}" > "$output_file" 2>/dev/null || {
        print_error "Failed to parse custom mocks"
        return 1
    }
    
    return 0
}

