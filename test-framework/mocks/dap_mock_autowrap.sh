#!/bin/bash
# DAP SDK Mock Auto-Wrapper Generator (Bash version)
# Automatically scans test source files for mock declarations and generates:
# 1. Linker response file with --wrap options
# 2. CMake fragment with configuration
# 3. Wrapper template for missing wrappers
#
# Usage:
#   ./dap_mock_autowrap.sh <output_dir> <source1.c> <source2.h> ...

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

print_header() {
    echo "============================================================"
    echo "$1"
    echo "============================================================"
}

print_info() {
    echo -e "${BLUE}📋 $1${NC}"
}

print_success() {
    echo -e "${GREEN}✅ $1${NC}"
}

print_warning() {
    echo -e "${YELLOW}⚠️  $1${NC}"
}

print_error() {
    echo -e "${RED}❌ $1${NC}"
}

# Load template processing functions
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/dap_tpl.sh"
source "${SCRIPT_DIR}/dap_tpl_lib.sh"

# Check arguments
if [ $# -lt 3 ]; then
    echo "Usage: $0 <output_dir> <basename> <source1> <source2> ..."
    exit 1
fi

OUTPUT_DIR="$1"
BASENAME="$2"
shift 2  # Remove first two arguments
SOURCE_FILES=("$@")  # Remaining arguments are source files

# Get script directory for template files
# Note: SCRIPT_DIR is already set by dap_tpl.sh sourcing
TEMPLATES_DIR="${SCRIPT_DIR}/templates"
SCRIPTS_DIR="${TEMPLATES_DIR}/scripts"

# Export SCRIPTS_DIR for template processing functions
export SCRIPTS_DIR
export TEMPLATES_DIR

# Create output directory if it doesn't exist
mkdir -p "$OUTPUT_DIR"

# Output files
WRAP_FILE="$OUTPUT_DIR/${BASENAME}_wrap.txt"
CMAKE_FILE="$OUTPUT_DIR/${BASENAME}_mocks.cmake"
TEMPLATE_FILE="$OUTPUT_DIR/${BASENAME}_wrappers_template.c"
MACROS_FILE="$OUTPUT_DIR/${BASENAME}_mock_macros.h"

print_header "DAP SDK Mock Auto-Wrapper Generator (Bash)"
print_info "Scanning ${#SOURCE_FILES[@]} source files..."

# List all files being scanned
for SOURCE_FILE in "${SOURCE_FILES[@]}"; do
    if [ ! -f "$SOURCE_FILE" ]; then
        print_warning "File not found: $SOURCE_FILE"
        continue
    fi
    print_info "  - $(basename "$SOURCE_FILE")"
done

# Step 1: Scan all files for mock declarations
print_info "Scanning for mock declarations..."

# Temporary file for collecting functions
TMP_MOCKS="/tmp/mock_funcs_$$.txt"
> "$TMP_MOCKS"

for SOURCE_FILE in "${SOURCE_FILES[@]}"; do
    [ ! -f "$SOURCE_FILE" ] && continue
    # Find DAP_MOCK_DECLARE(func_name) or DAP_MOCK_DECLARE(func_name, ...)
    grep -o 'DAP_MOCK_DECLARE\s*(\s*[a-zA-Z_][a-zA-Z0-9_]*' "$SOURCE_FILE" | \
        sed 's/DAP_MOCK_DECLARE\s*(\s*\([a-zA-Z_][a-zA-Z0-9_]*\)/\1/' >> "$TMP_MOCKS" || true
    # Find DAP_MOCK_DECLARE_CUSTOM(func_name, ...)
    grep -o 'DAP_MOCK_DECLARE_CUSTOM\s*(\s*[a-zA-Z_][a-zA-Z0-9_]*' "$SOURCE_FILE" | \
        sed 's/DAP_MOCK_DECLARE_CUSTOM\s*(\s*\([a-zA-Z_][a-zA-Z0-9_]*\)/\1/' >> "$TMP_MOCKS" || true
done

MOCK_FUNCTIONS=$(sort -u "$TMP_MOCKS")
rm -f "$TMP_MOCKS"

if [ -z "$MOCK_FUNCTIONS" ]; then
    print_info "No mock declarations found in any source files"
    print_info "Creating empty wrap file: $WRAP_FILE"
    # Create truly empty file - linker response files cannot contain comments
    # Comments are interpreted as linker options and cause errors
    > "$WRAP_FILE"
    FUNC_COUNT=0
else
FUNC_COUNT=$(echo "$MOCK_FUNCTIONS" | wc -l)
print_success "Found $FUNC_COUNT mock declarations:"
echo "$MOCK_FUNCTIONS" | while read func; do
    echo "   - $func"
done
fi

# Step 2: Scan for existing wrapper definitions
print_info "Scanning for wrapper definitions..."

TMP_WRAPPERS="/tmp/wrapper_funcs_$$.txt"
> "$TMP_WRAPPERS"

for SOURCE_FILE in "${SOURCE_FILES[@]}"; do
    [ ! -f "$SOURCE_FILE" ] && continue
    
    # Find DAP_MOCK_WRAPPER_CUSTOM(return_type, func_name, ...)
    # Extract func_name which is the second argument after the return type
    grep -o 'DAP_MOCK_WRAPPER_CUSTOM\s*([^)]*' "$SOURCE_FILE" | \
        sed -n 's/.*,\s*\([a-zA-Z_][a-zA-Z0-9_]*\)\s*,.*/\1/p' >> "$TMP_WRAPPERS" || true
    
    # Find DAP_MOCK_WRAPPER_PASSTHROUGH(return_type, func_name, ...) - v2.1
    grep -o 'DAP_MOCK_WRAPPER_PASSTHROUGH\s*([^)]*' "$SOURCE_FILE" | \
        sed -n 's/.*,\s*\([a-zA-Z_][a-zA-Z0-9_]*\)\s*,.*/\1/p' >> "$TMP_WRAPPERS" || true
    
    # Find DAP_MOCK_WRAPPER_PASSTHROUGH_VOID(func_name, ...) - v2.1
    grep -o 'DAP_MOCK_WRAPPER_PASSTHROUGH_VOID\s*(\s*[a-zA-Z_][a-zA-Z0-9_]*' "$SOURCE_FILE" | \
        sed 's/DAP_MOCK_WRAPPER_PASSTHROUGH_VOID\s*(\s*\([a-zA-Z_][a-zA-Z0-9_]*\)/\1/' >> "$TMP_WRAPPERS" || true
    
    # Find explicit __wrap_ definitions
    grep -o '__wrap_[a-zA-Z_][a-zA-Z0-9_]*' "$SOURCE_FILE" | \
        sed 's/__wrap_//' >> "$TMP_WRAPPERS" || true
done

WRAPPER_FUNCTIONS=$(sort -u "$TMP_WRAPPERS")
rm -f "$TMP_WRAPPERS"

if [ -n "$WRAPPER_FUNCTIONS" ]; then
    echo "$WRAPPER_FUNCTIONS" | while read func; do
        echo "   ✅ $func"
    done
fi

# Step 3: Generate linker response file
print_info "Generating linker response file: $WRAP_FILE"

> "$WRAP_FILE"  # Clear file
if [ -n "$MOCK_FUNCTIONS" ]; then
echo "$MOCK_FUNCTIONS" | while read func; do
    # Note: For -Wl,@file usage, we need just --wrap=func (without -Wl,)
    echo "--wrap=$func" >> "$WRAP_FILE"
done
print_success "Generated $FUNC_COUNT --wrap options"
else
    # Create truly empty file - linker response files cannot contain comments
    # Comments are interpreted as linker options and cause errors
    > "$WRAP_FILE"
    print_info "Created empty wrap file (no mocks to wrap)"
fi

# Step 4: Generate CMake integration
print_info "Generating CMake integration: $CMAKE_FILE"

cat > "$CMAKE_FILE" << EOF
# Auto-generated mock configuration
# Generated by dap_mock_autowrap.sh

# Wrapped functions:
EOF

if [ -n "$MOCK_FUNCTIONS" ]; then
echo "$MOCK_FUNCTIONS" | while read func; do
    echo "#   - $func" >> "$CMAKE_FILE"
done
else
    echo "#   (none - no mocks declared)" >> "$CMAKE_FILE"
fi

print_success "Generated CMake integration"

# Step 5: Extract full information about custom mocks for direct function generation
print_info "Extracting full custom mock declarations for direct function generation..."

# Temporary file for collecting custom mock declarations
# Use fixed name instead of PID to ensure file exists for later steps
TMP_CUSTOM_MOCKS="${OUTPUT_DIR}/custom_mocks_list.txt"
> "$TMP_CUSTOM_MOCKS"

# Scan all source files for DAP_MOCK_WRAPPER_CUSTOM and extract full information
print_info "Scanning ${#SOURCE_FILES[@]} source files for custom mocks..."
for SOURCE_FILE in "${SOURCE_FILES[@]}"; do
    [ ! -f "$SOURCE_FILE" ] && continue
    print_info "  Scanning: $SOURCE_FILE"
    
    # Use awk script to parse DAP_MOCK_WRAPPER_CUSTOM declarations and extract:
    # - return_type (original, with *)
    # - func_name
    # - parameters list (type and name from PARAM(...) or void)
    awk -f "${SCRIPTS_DIR}/parse_custom_mocks.awk" "$SOURCE_FILE" >> "$TMP_CUSTOM_MOCKS" || true
    
    # Debug: show what was extracted
    if [ -s "$TMP_CUSTOM_MOCKS" ]; then
        EXTRACTED_COUNT=$(wc -l < "$TMP_CUSTOM_MOCKS" | tr -d ' ')
        print_info "    Extracted $EXTRACTED_COUNT custom mock(s) so far"
    fi
done

# Debug: show final count
if [ -f "$TMP_CUSTOM_MOCKS" ]; then
    FINAL_COUNT=$(wc -l < "$TMP_CUSTOM_MOCKS" 2>/dev/null | tr -d ' ' || echo "0")
    print_info "Total custom mocks extracted: $FINAL_COUNT"
    if [ "$FINAL_COUNT" -gt 0 ]; then
        print_info "Sample entries:"
        head -3 "$TMP_CUSTOM_MOCKS" | while IFS='|' read -r rt fn pl mt; do
            print_info "  - $fn ($rt)"
        done
    fi
fi

# Step 5.5: Generate named headers for each custom mock
if [ -f "$TMP_CUSTOM_MOCKS" ] && [ -s "$TMP_CUSTOM_MOCKS" ]; then
    CUSTOM_MOCKS_COUNT=$(wc -l < "$TMP_CUSTOM_MOCKS" | tr -d ' ')
    print_info "Generating named headers for custom mocks..."
    print_info "Found $CUSTOM_MOCKS_COUNT custom mock declarations"
    
    # Create directory for custom mock headers
    CUSTOM_MOCKS_DIR="${OUTPUT_DIR}/custom_mocks"
    mkdir -p "$CUSTOM_MOCKS_DIR"
    
    # Process each custom mock declaration
    while IFS='|' read -r return_type func_name param_list macro_type; do
        [ -z "$func_name" ] && continue
        
        # Skip if function already has a wrapper defined in source files
        # Check if function name is in WRAPPER_FUNCTIONS list
        if echo "$WRAPPER_FUNCTIONS" | grep -q "^${func_name}$"; then
            print_info "  Skipping $func_name - wrapper already defined in source"
            continue
        fi
        
        # Create header file name based on function name
        header_file="${CUSTOM_MOCKS_DIR}/${func_name}_mock.h"
        
        # Parse parameters from param_list
        # Handle PARAM(type, name) entries or void
        param_decl=""
        param_names=""
        param_array=""
        param_count=0
        
        if [ "$param_list" = "void" ] || [ -z "$param_list" ]; then
            param_decl="void"
            param_names=""
            param_array="NULL"
            param_count=0
        else
            # Extract PARAM(type, name) entries
            # Remove all whitespace for easier parsing
            clean_params=$(echo "$param_list" | tr -d ' \t\n')
            
            # Count PARAM entries
            param_count=$(echo "$clean_params" | grep -o "PARAM(" | wc -l)
            
            if [ "$param_count" -eq 0 ]; then
                # No PARAM entries, might be direct parameters or void
                param_decl="void"
                param_names=""
                param_array="NULL"
                param_count=0
            else
                # Extract each PARAM(type, name)
                param_decl_parts=()
                param_name_parts=()
                param_array_parts=()
                
                # Use awk script to extract PARAM entries
                echo "$param_list" | awk -f "${SCRIPTS_DIR}/parse_params.awk" > "/tmp/params_${func_name}_$$.txt"
                
                # Read extracted parameters
                param_idx=0
                while IFS='|' read -r param_type param_name; do
                    [ -z "$param_type" ] && continue
                    param_decl_parts+=("$param_type $param_name")
                    param_name_parts+=("$param_name")
                    param_array_parts+=("(void*)(intptr_t)$param_name")
                    param_idx=$((param_idx + 1))
                done < "/tmp/params_${func_name}_$$.txt"
                
                rm -f "/tmp/params_${func_name}_$$.txt"
                
                # Join parameters with proper formatting
                if [ ${#param_decl_parts[@]} -gt 0 ]; then
                    # Join with ", " for declarations and names
                    param_decl=$(IFS=','; printf '%s, ' "${param_decl_parts[@]}" | sed 's/, $//')
                    param_names=$(IFS=','; printf '%s, ' "${param_name_parts[@]}" | sed 's/, $//')
                    # Join array parts with ", "
                    param_array="((void*[]){$(IFS=','; printf '%s, ' "${param_array_parts[@]}" | sed 's/, $//')})"
                    param_count=${#param_decl_parts[@]}
                else
                    param_decl="void"
                    param_names=""
                    param_array="NULL"
                    param_count=0
                fi
            fi
        fi
        
        # Prepare template variables
        guard_name="${func_name^^}_MOCK_H"
        if [ "$return_type" = "void" ]; then
            wrapper_signature="void __wrap_${func_name}($param_decl)"
            result_declaration=""
            if [ "$param_count" -eq 0 ]; then
                mock_impl_call="        __mock_impl_${func_name}();"
                real_function_call="        __real_${func_name}();"
            else
                mock_impl_call="        __mock_impl_${func_name}($param_names);"
                real_function_call="        __real_${func_name}($param_names);"
            fi
            return_value_override=""
            record_call=""
            return_statement=""
        else
            wrapper_signature="$return_type __wrap_${func_name}($param_decl)"
            result_declaration="    $return_type __wrap_result = ($return_type){0};"
            if [ "$param_count" -eq 0 ]; then
                mock_impl_call="        __wrap_result = __mock_impl_${func_name}();"
                real_function_call="        __wrap_result = __real_${func_name}();"
            else
                mock_impl_call="        __wrap_result = __mock_impl_${func_name}($param_names);"
                real_function_call="        __wrap_result = __real_${func_name}($param_names);"
            fi
            # Create temporary files for multi-line values
            return_value_override_file=$(mktemp)
            record_call_file=$(mktemp)
            {
                echo "        if (__wrap_mock_state && __wrap_mock_state->return_value.ptr) {"
                echo "            __wrap_result = *($return_type*)__wrap_mock_state->return_value.ptr;"
                echo "        }"
            } > "$return_value_override_file"
            echo "        dap_mock_record_call(__wrap_mock_state, __wrap_args, __wrap_args_count, (void*)(intptr_t)__wrap_result);" > "$record_call_file"
            return_value_override="@$return_value_override_file"
            record_call="@$record_call_file"
            return_statement="    return __wrap_result;"
        fi
        
        # Generate header file using template
        # Note: Temporary files must exist during replace_template_placeholders call
        replace_template_placeholders \
            "${TEMPLATES_DIR}/custom_mock_header.h.tpl" \
            "$header_file" \
            "FUNC_NAME=$func_name" \
            "RETURN_TYPE=$return_type" \
            "PARAM_DECL=$param_decl" \
            "PARAM_NAMES=$param_names" \
            "PARAM_ARRAY=$param_array" \
            "PARAM_COUNT=$param_count" \
            "GUARD_NAME=$guard_name" \
            "WRAPPER_FUNCTION_SIGNATURE=$wrapper_signature" \
            "RESULT_DECLARATION=$result_declaration" \
            "MOCK_IMPL_CALL=$mock_impl_call" \
            "RETURN_VALUE_OVERRIDE=$return_value_override" \
            "RECORD_CALL=$record_call" \
            "REAL_FUNCTION_CALL=$real_function_call" \
            "RETURN_STATEMENT=$return_statement"
        
        # Clean up temporary files AFTER replacement is done
        if [ "$return_type" != "void" ]; then
            rm -f "$return_value_override_file" "$record_call_file"
        fi
        
        print_success "Generated mock header: $header_file"
    done < "$TMP_CUSTOM_MOCKS"
    
    # Create main include file that includes all custom mock headers
    # Also include the macros header so both can be included via single -include
    MAIN_CUSTOM_MOCKS_FILE="${OUTPUT_DIR}/${BASENAME}_custom_mocks.h"
    MACROS_HEADER_FILE="${OUTPUT_DIR}/${BASENAME}_mock_macros.h"
    
    # Generate main include file using template
    # Pass custom mocks list and wrapper functions for generation inside template
    # Export variables so they're available in replace_template_placeholders and child processes
    export CUSTOM_MOCKS_LIST="$(cat "$TMP_CUSTOM_MOCKS")"
    export WRAPPER_FUNCTIONS="$WRAPPER_FUNCTIONS"
    replace_template_placeholders \
        "${TEMPLATES_DIR}/custom_mocks_main.h.tpl" \
        "$MAIN_CUSTOM_MOCKS_FILE" \
        "BASENAME=$BASENAME"
    
    print_success "Generated main custom mocks include: $MAIN_CUSTOM_MOCKS_FILE"
else
    print_info "No custom mocks found - creating custom mocks header with macros only"
    # Create header file that includes macros header so CMake can always include it
    MAIN_CUSTOM_MOCKS_FILE="${OUTPUT_DIR}/${BASENAME}_custom_mocks.h"
    MACROS_HEADER_FILE="${OUTPUT_DIR}/${BASENAME}_mock_macros.h"
    
    # Generate empty main include file using template
    replace_template_placeholders \
        "${TEMPLATES_DIR}/custom_mocks_main_empty.h.tpl" \
        "$MAIN_CUSTOM_MOCKS_FILE" \
        "BASENAME=$BASENAME"
fi

# Step 6: Analyze DAP_MOCK_WRAPPER_CUSTOM usage and collect return types and parameter counts (for backward compatibility)
print_info "Analyzing DAP_MOCK_WRAPPER_CUSTOM usage for macro generation..."

# Temporary file for collecting param counts
TMP_PARAM_COUNTS="/tmp/param_counts_$$.txt"
> "$TMP_PARAM_COUNTS"

# Temporary file for collecting return types
TMP_RETURN_TYPES="/tmp/return_types_$$.txt"
> "$TMP_RETURN_TYPES"

# Scan all source files for DAP_MOCK_WRAPPER_CUSTOM and variants
# Parse multi-line DAP_MOCK_WRAPPER_CUSTOM declarations and count PARAM(...) entries
# Also search for _DAP_MOCK_WRAPPER_CUSTOM_NONVOID and _DAP_MOCK_WRAPPER_CUSTOM_VOID
for SOURCE_FILE in "${SOURCE_FILES[@]}"; do
    [ ! -f "$SOURCE_FILE" ] && continue
    
    # Use awk to parse DAP_MOCK_WRAPPER_CUSTOM declarations (and variants)
    # Extract return_type and count PARAM(...) entries between macro and opening brace {
    # Handle multi-line declarations
    awk -f "${SCRIPTS_DIR}/count_params.awk" "$SOURCE_FILE" >> "$TMP_PARAM_COUNTS" || true
    
    # Second pass: extract return types (both normalized and original)
    awk -f "${SCRIPTS_DIR}/extract_return_types.awk" "$SOURCE_FILE" >> "$TMP_RETURN_TYPES" || true
done

# Collect unique parameter counts
PARAM_COUNTS=$(sort -u -n "$TMP_PARAM_COUNTS" 2>/dev/null | tr '\n' ' ')
rm -f "$TMP_PARAM_COUNTS"

# Collect unique return types (normalized|original format)
RETURN_TYPES_PAIRS=$(sort -u "$TMP_RETURN_TYPES" 2>/dev/null | grep -v '^$' | tr '\n' ' ')
rm -f "$TMP_RETURN_TYPES"

# Extract normalized types and original types separately
RETURN_TYPES=""
declare -A ORIGINAL_TYPES
for pair in $RETURN_TYPES_PAIRS; do
    normalized=$(echo "$pair" | cut -d'|' -f1)
    original=$(echo "$pair" | cut -d'|' -f2)
    RETURN_TYPES="$RETURN_TYPES $normalized"
    ORIGINAL_TYPES["$normalized"]="$original"
done
RETURN_TYPES=$(echo "$RETURN_TYPES" | tr ' ' '\n' | sort -u | tr '\n' ' ')

if [ -z "$PARAM_COUNTS" ] || [ "$PARAM_COUNTS" = " " ]; then
    print_info "No DAP_MOCK_WRAPPER_CUSTOM found - will generate minimal macros"
    PARAM_COUNTS="0"  # At least need _DAP_MOCK_MAP_0 for empty case
fi

# Convert to array and ensure we have at least 0
PARAM_COUNTS_ARRAY=($PARAM_COUNTS)
if [ ${#PARAM_COUNTS_ARRAY[@]} -eq 0 ] || [ -z "${PARAM_COUNTS_ARRAY[0]}" ]; then
    PARAM_COUNTS_ARRAY=(0)
fi

print_success "Found parameter counts: ${PARAM_COUNTS_ARRAY[*]}"

# Find maximum parameter count to determine max args count
MAX_PARAM_COUNT=0
for count in "${PARAM_COUNTS_ARRAY[@]}"; do
    [ -z "$count" ] && continue
    [ "$count" -gt "$MAX_PARAM_COUNT" ] && MAX_PARAM_COUNT=$count
done

# Max args = max params * 2 (each PARAM expands to 2 args: type, name)
# Add some safety margin (1 extra param = 2 extra args) for edge cases
MAX_ARGS_COUNT=$((MAX_PARAM_COUNT * 2 + 2))
# Ensure at least 0 for empty case
[ "$MAX_ARGS_COUNT" -lt 0 ] && MAX_ARGS_COUNT=0

print_info "Max parameter count: $MAX_PARAM_COUNT, generating helpers for 0-$MAX_ARGS_COUNT args"

# Prepare all template data using library functions
prepare_nargs_data "$MAX_ARGS_COUNT"
prepare_map_count_params_by_count_data "$MAX_ARGS_COUNT"
prepare_map_count_params_helper_data "$MAX_ARGS_COUNT"
prepare_map_impl_cond_1_data "$MAX_ARGS_COUNT" "${PARAM_COUNTS_ARRAY[@]}"
prepare_map_impl_cond_data "${PARAM_COUNTS_ARRAY[@]}"
prepare_map_macros_data "${PARAM_COUNTS_ARRAY[@]}"

# Step 6: Generate specialized macros header file
print_info "Generating specialized macros header: $MACROS_FILE"

# Generate return type macros and simple wrapper macros in temporary files first
# These will be appended via AWK sections in mock_map_macros.h.tpl
RETURN_TYPE_MACROS_FILE="${MACROS_FILE}.return_types"
SIMPLE_WRAPPER_MACROS_FILE="${MACROS_FILE}.simple_wrappers"

# Generate return type macros
if [ -n "$RETURN_TYPES" ]; then
    {
        echo ""
        echo "// ============================================================================"
        echo "// Generated specialized macros for return types"
        echo "// ============================================================================"
        echo "// These macros are generated based on actual return types found in the code."
        echo "// Each macro routes to the appropriate void/non-void implementation."
        echo "// ALL return types MUST have a generated macro - no fallbacks are provided."
        echo ""
    } > "$RETURN_TYPE_MACROS_FILE"
    
    RETURN_TYPES_ARRAY=($RETURN_TYPES)
    declare -A GENERATED_MACROS
    declare -A TYPE_NORMALIZATION_TABLE
    TYPE_NORMALIZATION_TABLE["_Bool"]="bool"
    GENERATED_MACROS["void"]="void"
    
    for return_type in "${RETURN_TYPES_ARRAY[@]}"; do
        [ -z "$return_type" ] && continue
        normalized_type="$return_type"
        macro_name=$(echo "$normalized_type" | sed 's/[^a-zA-Z0-9_]/_/g')
        GENERATED_MACROS["$macro_name"]="$normalized_type"
        
        for expanded_type in "${!TYPE_NORMALIZATION_TABLE[@]}"; do
            if [ "${TYPE_NORMALIZATION_TABLE[$expanded_type]}" = "$normalized_type" ]; then
                expanded_macro_name=$(echo "$expanded_type" | sed 's/[^a-zA-Z0-9_]/_/g')
                GENERATED_MACROS["$expanded_macro_name"]="$normalized_type"
            fi
        done
        
        if [[ "$normalized_type" == *_PTR ]]; then
            base_type="${normalized_type%_PTR}"
            base_macro_name=$(echo "$base_type" | sed 's/[^a-zA-Z0-9_]/_/g')
            [ "$base_macro_name" != "$macro_name" ] && GENERATED_MACROS["$base_macro_name"]="$normalized_type"
        fi
    done
    
    for macro_name in "${!GENERATED_MACROS[@]}"; do
        original_type="${GENERATED_MACROS[$macro_name]}"
        echo "// Specialized macro for return type: ${original_type}" >> "$RETURN_TYPE_MACROS_FILE"
        if [ "$macro_name" = "void" ]; then
            echo "#define _DAP_MOCK_WRAPPER_CUSTOM_SELECT_${macro_name}(func_name, return_type_full, ...) \\" >> "$RETURN_TYPE_MACROS_FILE"
            echo "    _DAP_MOCK_WRAPPER_CUSTOM_VOID(func_name, __VA_ARGS__)" >> "$RETURN_TYPE_MACROS_FILE"
           else
            echo "#define _DAP_MOCK_WRAPPER_CUSTOM_SELECT_${macro_name}(func_name, return_type_full, ...) \\" >> "$RETURN_TYPE_MACROS_FILE"
            echo "    _DAP_MOCK_WRAPPER_CUSTOM_NONVOID(return_type_full, func_name, ##__VA_ARGS__)" >> "$RETURN_TYPE_MACROS_FILE"
        fi
        echo "" >> "$RETURN_TYPE_MACROS_FILE"
    done
    
    {
        echo ""
        echo "// ============================================================================"
        echo "// Generated type normalization macros"
        echo "// ============================================================================"
        echo "// These macros normalize pointer types by removing * from type names."
        echo ""
    } >> "$RETURN_TYPE_MACROS_FILE"
    
    declare -A NORMALIZATION_MACROS
    for normalized_type in $RETURN_TYPES; do
        [ -z "$normalized_type" ] && continue
        original_type="${ORIGINAL_TYPES[$normalized_type]}"
        [ -z "$original_type" ] && continue
        macro_key="$original_type"
        if [[ "$original_type" == *"*" ]]; then
            base_type=$(echo "$original_type" | sed 's/\*//g' | sed 's/[ \t]*$//')
            NORMALIZATION_MACROS["$macro_key"]="$base_type"
        else
            NORMALIZATION_MACROS["$macro_key"]="$original_type"
           fi
       done
       
    {
        echo ""
        echo "// ============================================================================"
        echo "// Type-to-selector wrapper macros"
        echo "// ============================================================================"
        echo ""
    } >> "$RETURN_TYPE_MACROS_FILE"
    
    declare -A TYPE_TO_SELECT_MAPPINGS
    declare -A NORMALIZE_TYPE_MAPPINGS
    
    # Collect normalization macros data for template processing
    NORMALIZATION_MACROS_DATA=""
    for macro_key in "${!NORMALIZATION_MACROS[@]}"; do
        base_type="${NORMALIZATION_MACROS[$macro_key]}"
        normalized_key=$(echo "$macro_key" | sed 's/\*/_PTR/g' | sed 's/[^a-zA-Z0-9_]/_/g')
        escaped_base_key=$(echo "$base_type" | sed 's/[^a-zA-Z0-9_]/_/g')
        escaped_original_key=$(echo "$macro_key" | sed 's/\*/_PTR/g' | sed 's/[^a-zA-Z0-9_]/_/g')
        escaped_macro_key_for_escape=$(echo "$macro_key" | sed 's/\*/_PTR/g' | sed 's/[ \t]*$//' | sed 's/[^a-zA-Z0-9_]/_/g')
        
        if [ -z "${NORMALIZE_TYPE_MAPPINGS[$escaped_base_key]}" ]; then
            if [ -n "$NORMALIZATION_MACROS_DATA" ]; then
                NORMALIZATION_MACROS_DATA="${NORMALIZATION_MACROS_DATA}"$'\n'
            fi
            NORMALIZATION_MACROS_DATA="${NORMALIZATION_MACROS_DATA}${macro_key}|${base_type}|${normalized_key}|${escaped_base_key}|${escaped_original_key}|${escaped_macro_key_for_escape}"
            NORMALIZE_TYPE_MAPPINGS["$escaped_base_key"]=1
        fi
    done
    
    # Generate type normalization macros using template
    if [ -n "$NORMALIZATION_MACROS_DATA" ]; then
        export NORMALIZATION_MACROS_DATA="$NORMALIZATION_MACROS_DATA"
        NORMALIZATION_MACROS_TEMP=$(mktemp)
        replace_template_placeholders \
            "${TEMPLATES_DIR}/type_normalization_macros.h.tpl" \
            "$NORMALIZATION_MACROS_TEMP"
        cat "$NORMALIZATION_MACROS_TEMP" >> "$RETURN_TYPE_MACROS_FILE"
        rm -f "$NORMALIZATION_MACROS_TEMP"
        unset NORMALIZATION_MACROS_DATA
    fi
    
    # Continue with type-to-selector wrapper macros generation
    for macro_key in "${!NORMALIZATION_MACROS[@]}"; do
        base_type="${NORMALIZATION_MACROS[$macro_key]}"
        normalized_key=$(echo "$macro_key" | sed 's/\*/_PTR/g' | sed 's/[^a-zA-Z0-9_]/_/g')
        selector_name="_DAP_MOCK_WRAPPER_CUSTOM_SELECT_${normalized_key}"
        escaped_base_key=$(echo "$base_type" | sed 's/[^a-zA-Z0-9_]/_/g')
        
        for expanded_type in "${!TYPE_NORMALIZATION_TABLE[@]}"; do
            if [ "${TYPE_NORMALIZATION_TABLE[$expanded_type]}" = "$base_type" ]; then
                expanded_macro_name=$(echo "$expanded_type" | sed 's/[^a-zA-Z0-9_]/_/g')
                if [ -z "${NORMALIZE_TYPE_MAPPINGS[$expanded_macro_name]}" ]; then
                    echo "// Normalize type: $expanded_macro_name -> $escaped_base_key (from type expansion)" >> "$RETURN_TYPE_MACROS_FILE"
                    echo "#define _DAP_MOCK_NORMALIZE_TYPE_${expanded_macro_name} ${escaped_base_key}" >> "$RETURN_TYPE_MACROS_FILE"
                    NORMALIZE_TYPE_MAPPINGS["$expanded_macro_name"]=1
                fi
                if [ -z "${TYPE_TO_SELECT_MAPPINGS[$expanded_macro_name]}" ]; then
                    echo "// Type-to-selector wrapper for: $expanded_macro_name -> calls $selector_name" >> "$RETURN_TYPE_MACROS_FILE"
                    echo "#define _DAP_MOCK_TYPE_TO_SELECT_NAME_${expanded_macro_name}(func_name, return_type_full, ...) \\" >> "$RETURN_TYPE_MACROS_FILE"
                    echo "    ${selector_name}(func_name, return_type_full, ##__VA_ARGS__)" >> "$RETURN_TYPE_MACROS_FILE"
                    TYPE_TO_SELECT_MAPPINGS["$expanded_macro_name"]=1
                fi
            fi
        done
        
        if [ -z "${TYPE_TO_SELECT_MAPPINGS[$normalized_key]}" ]; then
            if [[ "$macro_key" == *"*" ]]; then
                echo "// Type-to-selector wrapper for normalized type $normalized_key (from $macro_key) -> calls $selector_name" >> "$RETURN_TYPE_MACROS_FILE"
            else
                echo "// Type-to-selector wrapper for: $normalized_key -> calls $selector_name" >> "$RETURN_TYPE_MACROS_FILE"
            fi
            echo "#define _DAP_MOCK_TYPE_TO_SELECT_NAME_${normalized_key}(func_name, return_type_full, ...) \\" >> "$RETURN_TYPE_MACROS_FILE"
            echo "    ${selector_name}(func_name, return_type_full, ##__VA_ARGS__)" >> "$RETURN_TYPE_MACROS_FILE"
            TYPE_TO_SELECT_MAPPINGS["$normalized_key"]=1
        fi
    done
    echo "" >> "$RETURN_TYPE_MACROS_FILE"
else
    {
        echo ""
        echo "// ============================================================================"
        echo "// Generated specialized macros for return types"
        echo "// ============================================================================"
        echo "// No return types found in code, but void macro is always required"
        echo "// Specialized macro for return type: void"
        echo "#define _DAP_MOCK_WRAPPER_CUSTOM_SELECT_void(func_name, return_type_full, ...) \\"
        echo "    _DAP_MOCK_WRAPPER_CUSTOM_VOID(func_name, __VA_ARGS__)"
        echo ""
    } > "$RETURN_TYPE_MACROS_FILE"
fi

# Generate simple wrapper macros
if [ -n "$SIMPLE_WRAPPER_MACROS" ]; then
    {
        echo ""
        echo "// ============================================================================"
        echo "// Generated simple wrapper macros (DAP_MOCK_WRAPPER_INT, DAP_MOCK_WRAPPER_PTR, etc.)"
        echo "// ============================================================================"
        echo "// These macros are generated based on actual return types found in the code."
        echo "// They provide convenient shortcuts for creating simple wrappers without custom logic."
        echo ""
        echo "$SIMPLE_WRAPPER_MACROS"
        echo ""
    } > "$SIMPLE_WRAPPER_MACROS_FILE"
else
    > "$SIMPLE_WRAPPER_MACROS_FILE"
fi

# Generate mock_map_macros content with template language constructs
RETURN_TYPE_MACROS_FILE="$RETURN_TYPE_MACROS_FILE" \
SIMPLE_WRAPPER_MACROS_FILE="$SIMPLE_WRAPPER_MACROS_FILE" \
PARAM_COUNTS_ARRAY="${PARAM_COUNTS_ARRAY[*]}" \
MAX_ARGS_COUNT="$MAX_ARGS_COUNT" \
MAP_COUNT_PARAMS_BY_COUNT_DATA="$MAP_COUNT_PARAMS_BY_COUNT_DATA" \
MAP_COUNT_PARAMS_HELPER_DATA="$MAP_COUNT_PARAMS_HELPER_DATA" \
MAP_IMPL_COND_1_DATA="$MAP_IMPL_COND_1_DATA" \
MAP_IMPL_COND_DATA="$MAP_IMPL_COND_DATA" \
replace_template_placeholders \
    "${TEMPLATES_DIR}/mock_map_macros.h.tpl" \
    "${MACROS_FILE}.map_content" \
    "RETURN_TYPE_MACROS_FILE=$RETURN_TYPE_MACROS_FILE" \
    "SIMPLE_WRAPPER_MACROS_FILE=$SIMPLE_WRAPPER_MACROS_FILE" \
    "PARAM_COUNTS_ARRAY=${PARAM_COUNTS_ARRAY[*]}" \
    "MAX_ARGS_COUNT=$MAX_ARGS_COUNT" \
    "MAP_COUNT_PARAMS_BY_COUNT_DATA=$MAP_COUNT_PARAMS_BY_COUNT_DATA" \
    "MAP_COUNT_PARAMS_HELPER_DATA=$MAP_COUNT_PARAMS_HELPER_DATA" \
    "MAP_IMPL_COND_1_DATA=$MAP_IMPL_COND_1_DATA" \
    "MAP_IMPL_COND_DATA=$MAP_IMPL_COND_DATA"

# Generate header using template with AWK section that appends mock_map_macros content
MAP_MACROS_CONTENT_FILE="${MACROS_FILE}.map_content" \
RETURN_TYPE_MACROS_FILE="$RETURN_TYPE_MACROS_FILE" \
SIMPLE_WRAPPER_MACROS_FILE="$SIMPLE_WRAPPER_MACROS_FILE" \
PARAM_COUNTS_ARRAY="${PARAM_COUNTS_ARRAY[*]}" \
MAX_ARGS_COUNT="$MAX_ARGS_COUNT" \
NARGS_SEQUENCE="$NARGS_SEQUENCE" \
NARGS_IMPL_PARAMS="$NARGS_IMPL_PARAMS" \
MAP_MACROS_DATA="$MAP_MACROS_DATA" \
replace_template_placeholders \
    "${TEMPLATES_DIR}/mock_macros_header.h.tpl" \
    "$MACROS_FILE" \
    "MAX_ARGS_COUNT=$MAX_ARGS_COUNT" \
    "PARAM_COUNTS_ARRAY=${PARAM_COUNTS_ARRAY[*]}" \
    "NARGS_SEQUENCE=$NARGS_SEQUENCE" \
    "NARGS_IMPL_PARAMS=$NARGS_IMPL_PARAMS" \
    "MAP_MACROS_DATA=$MAP_MACROS_DATA" \
    "MAP_MACROS_CONTENT_FILE=${MACROS_FILE}.map_content" \
    "RETURN_TYPE_MACROS_FILE=$RETURN_TYPE_MACROS_FILE" \
    "SIMPLE_WRAPPER_MACROS_FILE=$SIMPLE_WRAPPER_MACROS_FILE"

# Clean up temporary files AFTER template processing is complete
# Note: Files must exist during replace_template_placeholders execution
# Debug: check if files exist before cleanup
if [ -f "$RETURN_TYPE_MACROS_FILE" ]; then
    echo "DEBUG: RETURN_TYPE_MACROS_FILE exists before cleanup: $RETURN_TYPE_MACROS_FILE" >&2
    echo "DEBUG: File size: $(wc -c < "$RETURN_TYPE_MACROS_FILE")" >&2
fi
rm -f "${MACROS_FILE}.map_content" "$RETURN_TYPE_MACROS_FILE" "$SIMPLE_WRAPPER_MACROS_FILE"

# End of generated macros file
# No closing #endif - file is included unconditionally via -include

print_success "Generated macros header with ${#PARAM_COUNTS_ARRAY[@]} parameter count(s)"
if [ -n "$RETURN_TYPES" ]; then
    RETURN_TYPES_COUNT=$(echo "$RETURN_TYPES" | wc -w)
    print_success "Generated specialized macros for $RETURN_TYPES_COUNT return type(s): $RETURN_TYPES"
fi

# Step 6.5: Generate simple wrapper macros (DAP_MOCK_WRAPPER_INT, DAP_MOCK_WRAPPER_PTR, etc.)
# based on found return types
print_info "Generating simple wrapper macros for return types..."

# Helper function to determine macro suffix and cast expression based on return type
get_wrapper_macro_info() {
    local original_type="$1"
    local normalized_type="$2"
    local macro_suffix=""
    local cast_expr=""
    local record_call_value=""
    
    # Normalize type name for macro suffix (uppercase, replace special chars with _)
    macro_suffix=$(echo "$normalized_type" | tr '[:lower:]' '[:upper:]' | sed 's/[^A-Z0-9_]/_/g')
    
    # Determine cast expression and record call value based on type
    if [[ "$normalized_type" == *"_PTR" ]] || [[ "$original_type" == *"*" ]]; then
        # Pointer type
        cast_expr="g_mock_##func_name->return_value.ptr"
        record_call_value="l_ret"
    elif [[ "$normalized_type" == "void" ]]; then
        # Void type - special handling
        macro_suffix="VOID_FUNC"
        cast_expr=""
        record_call_value="NULL"
    else
        # Integer or other scalar type - cast through intptr_t
        cast_expr="($original_type)(intptr_t)g_mock_##func_name->return_value.ptr"
        record_call_value="(void*)(intptr_t)l_ret"
    fi
    
    echo "$macro_suffix|$cast_expr|$record_call_value"
}

# Generate simple wrapper macros for each unique return type
# Note: These macros are now generated via AWK sections in mock_map_macros.h.tpl
# The SIMPLE_WRAPPER_MACROS variable is used to populate the temporary file that is appended
SIMPLE_WRAPPER_MACROS=""
if [ -n "$RETURN_TYPES" ]; then
    for normalized_type in $RETURN_TYPES; do
        [ -z "$normalized_type" ] && continue
        original_type="${ORIGINAL_TYPES[$normalized_type]}"
        [ -z "$original_type" ] && continue
        
        # Skip void - it's handled separately
        if [ "$normalized_type" = "void" ]; then
            continue
        fi
        
        # Get macro info
        macro_info=$(get_wrapper_macro_info "$original_type" "$normalized_type")
        macro_suffix=$(echo "$macro_info" | cut -d'|' -f1)
        cast_expr=$(echo "$macro_info" | cut -d'|' -f2)
        record_call_value=$(echo "$macro_info" | cut -d'|' -f3)
        
        # Prepare template variables
        return_declaration="${original_type} l_ret = "
        return_statement="return l_ret;"
        real_function_call="return __real_##func_name args;"
        semicolon=";"
        void_empty_line=""
        
        # Generate macro using template
        macro_file=$(mktemp)
        replace_template_placeholders \
            "${TEMPLATES_DIR}/simple_wrapper_macro.h.tpl" \
            "$macro_file" \
            "RETURN_TYPE=$original_type" \
            "MACRO_SUFFIX=$macro_suffix" \
            "RETURN_DECLARATION=$return_declaration" \
            "CAST_EXPRESSION=$cast_expr" \
            "SEMICOLON=$semicolon" \
            "VOID_EMPTY_LINE=$void_empty_line" \
            "RECORD_CALL_VALUE=$record_call_value" \
            "RETURN_STATEMENT=$return_statement" \
            "REAL_FUNCTION_CALL=$real_function_call"
        
        SIMPLE_WRAPPER_MACROS="${SIMPLE_WRAPPER_MACROS}$(cat "$macro_file")"$'\n'$'\n'
        rm -f "$macro_file"
    done
    
    # Always generate VOID_FUNC macro
    # AWK post-processing is handled automatically by the template's {{AWK:...}} section
    macro_file=$(mktemp)
    replace_template_placeholders \
        "${TEMPLATES_DIR}/simple_wrapper_macro.h.tpl" \
        "$macro_file" \
        "RETURN_TYPE=void" \
        "MACRO_SUFFIX=VOID_FUNC" \
        "RETURN_DECLARATION=" \
        "CAST_EXPRESSION=" \
        "SEMICOLON=" \
        "VOID_EMPTY_LINE=" \
        "RECORD_CALL_VALUE=NULL" \
        "RETURN_STATEMENT=return;" \
        "REAL_FUNCTION_CALL=__real_##func_name args;"
    
    SIMPLE_WRAPPER_MACROS="${SIMPLE_WRAPPER_MACROS}$(cat "$macro_file")"$'\n'$'\n'
    rm -f "$macro_file"
fi

# Note: Simple wrapper macros are now generated via AWK sections in mock_map_macros.h.tpl
# The SIMPLE_WRAPPER_MACROS variable is used to populate the temporary file that is appended
if [ -n "$SIMPLE_WRAPPER_MACROS" ]; then
    print_success "Generated simple wrapper macros for return types"
fi

# Step 7: Find missing wrappers and generate template
if [ -z "$MOCK_FUNCTIONS" ]; then
    print_info "No mocks declared - skipping template generation"
elif [ -n "$WRAPPER_FUNCTIONS" ]; then
    MISSING_FUNCTIONS=$(comm -23 <(echo "$MOCK_FUNCTIONS" | sort) <(echo "$WRAPPER_FUNCTIONS" | sort))

if [ -z "$MISSING_FUNCTIONS" ]; then
    print_success "All wrappers are defined"
else
    MISSING_COUNT=$(echo "$MISSING_FUNCTIONS" | wc -l)
    print_warning "Missing wrappers for $MISSING_COUNT functions"
    print_info "Generating template: $TEMPLATE_FILE"
    
    # Generate template file using main template
    MISSING_FUNCTIONS="$MISSING_FUNCTIONS" \
    replace_template_placeholders \
        "${TEMPLATES_DIR}/wrapper_template.h.tpl" \
        "$TEMPLATE_FILE"
    
    # Show missing functions
    echo "$MISSING_FUNCTIONS" | while read func; do
        [ -z "$func" ] && continue
        echo "   ⚠️  $func"
    done
    
    print_success "Template generated with $MISSING_COUNT function stubs"
    fi
else
    # No wrappers found but mocks exist - generate template for all
    FUNC_COUNT=$(echo "$MOCK_FUNCTIONS" | wc -l)
    print_warning "No wrappers found for $FUNC_COUNT functions"
    print_info "Generating template: $TEMPLATE_FILE"
    
    # Generate template file using main template
    MISSING_FUNCTIONS="$MOCK_FUNCTIONS" \
    replace_template_placeholders \
        "${TEMPLATES_DIR}/wrapper_template.h.tpl" \
        "$TEMPLATE_FILE"
    
    # Show missing functions
    echo "$MOCK_FUNCTIONS" | while read func; do
        [ -z "$func" ] && continue
        echo "   ⚠️  $func"
    done
    
    print_success "Template generated with $FUNC_COUNT function stubs"
fi

# Final summary
print_header "✅ Generation Complete!"
echo ""
echo "Generated files:"
echo "  📄 $WRAP_FILE"
echo "  📄 $CMAKE_FILE"
if [ -f "$MACROS_FILE" ]; then
    echo "  📄 $MACROS_FILE"
fi
if [ -f "$TEMPLATE_FILE" ]; then
    echo "  📄 $TEMPLATE_FILE"
fi
echo ""
echo "To use in CMakeLists.txt:"
echo "  dap_mock_autowrap(your_target)"
