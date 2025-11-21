#!/bin/bash
# Intelligently collect and process types from mock declarations

# Source common utilities (but don't initialize yet - that's done in main script)
LIB_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${LIB_DIR}/dap_mock_common.sh"

# Generate dispatcher macros file - single template call
# Input: RETURN_TYPES_PAIRS (normalized|original format)
# Output: RETURN_TYPE_MACROS_FILE
generate_return_type_macros() {
    local return_type_macros_file="$1"
    
    # Prepare ORIGINAL_TYPES_DATA as string for template (normalized|original pairs)
    local ORIGINAL_TYPES_DATA=""
    if [ -n "$RETURN_TYPES_PAIRS" ]; then
        ORIGINAL_TYPES_DATA="$RETURN_TYPES_PAIRS"
    fi
    
    # Generate dispatcher macros - generator creates _DAP_MOCK_WRAPPER_FOR_<type> for all types
    replace_template_placeholders_with_mocking \
        "${TEMPLATES_DIR}/dispatcher_macros.h.tpl" \
        "$return_type_macros_file" \
        "ORIGINAL_TYPES_DATA=$ORIGINAL_TYPES_DATA"
}

# Generate function-specific wrapper macros file
# Input: TMP_CUSTOM_MOCKS file (return_type|func_name|param_list|macro_type format)
# Output: FUNCTION_WRAPPERS_FILE
generate_function_wrappers() {
    local function_wrappers_file="$1"
    local custom_mocks_file="$2"
    
    # Prepare FUNCTIONS_DATA as newline-separated string for template (return_type|func_name|param_list format)
    # Template expects newline-separated entries for {{#for entry in FUNCTIONS_DATA|newline}}
    local FUNCTIONS_DATA=""
    if [ -f "$custom_mocks_file" ] && [ -s "$custom_mocks_file" ]; then
        # Read custom mocks file and keep as newline-separated (dap_tpl handles newlines)
        FUNCTIONS_DATA=$(cat "$custom_mocks_file" 2>/dev/null | grep -v '^$' || true)
    fi
    
    # Generate function wrapper macros - generator creates _DAP_MOCK_WRAPPER_CUSTOM_FOR_<func_name> for all functions
    replace_template_placeholders_with_mocking \
        "${TEMPLATES_DIR}/function_wrappers.h.tpl" \
        "$function_wrappers_file" \
        "FUNCTIONS_DATA=$FUNCTIONS_DATA"
}


