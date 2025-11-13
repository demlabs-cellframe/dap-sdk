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

# Track temporary files for cleanup
declare -a TEMP_FILES=()

# Cleanup function for temporary files
cleanup_on_exit() {
    cleanup_temp_files "${TEMP_FILES[@]}"
}

# Set trap for cleanup on exit or error
trap cleanup_on_exit EXIT ERR

# Load modules
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LIB_DIR="${SCRIPT_DIR}/lib"

# Load common utilities and initialize
source "${LIB_DIR}/dap_mock_common.sh"
init_mock_common

# Load module functions
source "${LIB_DIR}/dap_mock_scan.sh"
source "${LIB_DIR}/dap_mock_parse.sh"
source "${LIB_DIR}/dap_mock_types.sh"
source "${LIB_DIR}/dap_mock_generate.sh"

# Check arguments
if [ $# -lt 3 ]; then
    echo "Usage: $0 <output_dir> <basename> <source1> <source2> ..."
    exit 1
fi

OUTPUT_DIR="$1"
BASENAME="$2"
shift 2  # Remove first two arguments
SOURCE_FILES=("$@")  # Remaining arguments are source files

# Export SCRIPTS_DIR for template processing functions
export SCRIPTS_DIR
export TEMPLATES_DIR
export MOCK_AWK_DIR

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
MOCK_FUNCTIONS=$(scan_mock_declarations "${SOURCE_FILES[@]}")

if [ -z "$MOCK_FUNCTIONS" ]; then
    print_info "No mock declarations found in any source files"
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
WRAPPER_FUNCTIONS=$(scan_wrapper_definitions "${SOURCE_FILES[@]}")

if [ -n "$WRAPPER_FUNCTIONS" ]; then
    echo "$WRAPPER_FUNCTIONS" | while read func; do
        echo "   ✅ $func"
    done
fi

# Step 3: Generate linker response file
print_info "Generating linker response file: $WRAP_FILE"
generate_wrap_file "$WRAP_FILE" "$MOCK_FUNCTIONS"

# Step 4: Generate CMake integration
print_info "Generating CMake integration: $CMAKE_FILE"
generate_cmake_file "$CMAKE_FILE" "$MOCK_FUNCTIONS"

# Step 5: Extract full information about custom mocks for direct function generation
print_info "Extracting full custom mock declarations for direct function generation..."

# Temporary file for collecting custom mock declarations
# Use fixed name instead of PID to ensure file exists for later steps
TMP_CUSTOM_MOCKS="${OUTPUT_DIR}/custom_mocks_list.txt"
> "$TMP_CUSTOM_MOCKS"
TEMP_FILES+=("$TMP_CUSTOM_MOCKS")

# Scan all source files for DAP_MOCK_WRAPPER_CUSTOM and extract full information
print_info "Scanning ${#SOURCE_FILES[@]} source files for custom mocks..."
extract_custom_mocks "$TMP_CUSTOM_MOCKS" "${SOURCE_FILES[@]}" || {
    print_error "Failed to extract custom mocks"
        exit 1
    }

# Step 5.5: Generate named headers for each custom mock
generate_custom_mock_headers "$OUTPUT_DIR" "$BASENAME" "$TMP_CUSTOM_MOCKS" "$WRAPPER_FUNCTIONS"

# Step 6: Analyze DAP_MOCK_WRAPPER_CUSTOM usage and collect return types and parameter counts
print_info "Analyzing DAP_MOCK_WRAPPER_CUSTOM usage for macro generation..."

# Parse mock declarations to extract types and parameter counts
parse_mock_declarations "${SOURCE_FILES[@]}" || {
    print_error "Failed to parse mock declarations"
    exit 1
}

# Ensure variables are set even if no mocks found
: "${MAX_ARGS_COUNT:=2}"
: "${PARAM_COUNTS_ARRAY[0]:=0}"

# Prepare all template data using library functions
prepare_nargs_data "$MAX_ARGS_COUNT" || {
    print_error "Failed to prepare NARGS data"
    exit 1
}
prepare_map_count_params_by_count_data "$MAX_ARGS_COUNT" || {
    print_error "Failed to prepare map count params by count data"
    exit 1
}
prepare_map_count_params_helper_data "$MAX_ARGS_COUNT" || {
    print_error "Failed to prepare map count params helper data"
    exit 1
}
prepare_map_impl_cond_1_data "$MAX_ARGS_COUNT" "${PARAM_COUNTS_ARRAY[@]}" || {
    print_error "Failed to prepare map impl cond 1 data"
    exit 1
}
prepare_map_impl_cond_data "${PARAM_COUNTS_ARRAY[@]}" || {
    print_error "Failed to prepare map impl cond data"
    exit 1
}
prepare_map_macros_data "${PARAM_COUNTS_ARRAY[@]}" || {
    print_error "Failed to prepare map macros data"
    exit 1
}

# Step 7: Generate specialized macros header file
generate_macros_file "$MACROS_FILE" || {
    print_error "Failed to generate macros file"
    exit 1
}

# Step 8: Generate wrapper template file
generate_template_file "$TEMPLATE_FILE" "$MOCK_FUNCTIONS" "$WRAPPER_FUNCTIONS"

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
