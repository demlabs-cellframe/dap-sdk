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
LINKER_WRAPPER_FILE="$OUTPUT_DIR/dap_mock_linker_wrapper.h"

# Check if verbose output is enabled (default: disabled for performance)
VERBOSE="${DAP_MOCK_VERBOSE:-0}"

if [ "$VERBOSE" = "1" ]; then
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
fi

# Step 1: Scan all files for mock declarations
[ "$VERBOSE" = "1" ] && print_info "Scanning for mock declarations..."
MOCK_FUNCTIONS=$(scan_mock_declarations "${SOURCE_FILES[@]}")

if [ -z "$MOCK_FUNCTIONS" ]; then
    [ "$VERBOSE" = "1" ] && print_info "No mock declarations found in any source files"
    FUNC_COUNT=0
else
    FUNC_COUNT=$(echo "$MOCK_FUNCTIONS" | wc -l)
    if [ "$VERBOSE" = "1" ]; then
        print_success "Found $FUNC_COUNT mock declarations:"
        echo "$MOCK_FUNCTIONS" | while read func; do
            echo "   - $func"
        done
    fi
fi

# Step 2: Scan for existing wrapper definitions
[ "$VERBOSE" = "1" ] && print_info "Scanning for wrapper definitions..."
WRAPPER_FUNCTIONS=$(scan_wrapper_definitions "${SOURCE_FILES[@]}")

if [ "$VERBOSE" = "1" ] && [ -n "$WRAPPER_FUNCTIONS" ]; then
    echo "$WRAPPER_FUNCTIONS" | while read func; do
        echo "   ✅ $func"
    done
fi

# Step 3: Extract full information about custom mocks (MUST be done before generate_wrap_file)
[ "$VERBOSE" = "1" ] && print_info "Extracting full custom mock declarations for direct function generation..."

# Temporary file for collecting custom mock declarations
# Use fixed name instead of PID to ensure file exists for later steps
TMP_CUSTOM_MOCKS="${OUTPUT_DIR}/custom_mocks_list.txt"
> "$TMP_CUSTOM_MOCKS"
# NOTE: Do NOT add to TEMP_FILES - this file is needed for CMake dependencies

# Scan all source files for DAP_MOCK_WRAPPER_CUSTOM and extract full information
[ "$VERBOSE" = "1" ] && print_info "Scanning ${#SOURCE_FILES[@]} source files for custom mocks..."
extract_custom_mocks "$TMP_CUSTOM_MOCKS" "${SOURCE_FILES[@]}" || {
    print_error "Failed to extract custom mocks"
        exit 1
    }

# Step 4: Generate linker response file (uses TMP_CUSTOM_MOCKS for typed wrappers on macOS)
[ "$VERBOSE" = "1" ] && print_info "Generating linker response file: $WRAP_FILE"
generate_wrap_file "$WRAP_FILE" "$MOCK_FUNCTIONS" "$TMP_CUSTOM_MOCKS"

# Step 5: Generate CMake integration
[ "$VERBOSE" = "1" ] && print_info "Generating CMake integration: $CMAKE_FILE"
generate_cmake_file "$CMAKE_FILE" "$MOCK_FUNCTIONS"

# Step 6: Generate named headers for each custom mock
# Also generate default wrappers for DAP_MOCK_DECLARE functions without custom wrappers
generate_custom_mock_headers "$OUTPUT_DIR" "$BASENAME" "$TMP_CUSTOM_MOCKS" "$WRAPPER_FUNCTIONS" "$MOCK_FUNCTIONS"

# Step 7: Analyze DAP_MOCK_WRAPPER_CUSTOM usage and collect return types and parameter counts
[ "$VERBOSE" = "1" ] && print_info "Analyzing DAP_MOCK_WRAPPER_CUSTOM usage for macro generation..."

# Parse mock declarations to extract types and parameter counts
parse_mock_declarations "${SOURCE_FILES[@]}" || {
    print_error "Failed to parse mock declarations"
    exit 1
}

# Add param_counts from custom_mocks_list.txt (5th field after |)
# Format: return_type|func_name|param_list|macro_type|param_count
if [ -f "$TMP_CUSTOM_MOCKS" ] && [ -s "$TMP_CUSTOM_MOCKS" ]; then
    while IFS='|' read -r _ _ _ _ param_count _; do
        if [ -n "$param_count" ] && [[ "$param_count" =~ ^[0-9]+$ ]]; then
            # Add to array if not already present
            found=0
            for existing in "${PARAM_COUNTS_ARRAY[@]}"; do
                [ "$existing" = "$param_count" ] && found=1 && break
            done
            [ "$found" -eq 0 ] && PARAM_COUNTS_ARRAY+=("$param_count")
        fi
    done < "$TMP_CUSTOM_MOCKS"
    [ "$VERBOSE" = "1" ] && print_info "Added param_counts from custom mocks: ${PARAM_COUNTS_ARRAY[*]}"
fi

# Ensure variables are set even if no mocks found
: "${MAX_ARGS_COUNT:=2}"
: "${PARAM_COUNTS_ARRAY[0]:=0}"

# Update MAX_ARGS_COUNT based on param counts
for count in "${PARAM_COUNTS_ARRAY[@]}"; do
    if [ -n "$count" ] && [ "$count" -gt "$MAX_ARGS_COUNT" ]; then
        MAX_ARGS_COUNT=$((count * 2))  # Each param has type and name
    fi
done

# Step 8: Generate specialized macros header file
# All template data preparation (NARGS, MAP params, etc.) is done inside generate_macros_file()
generate_macros_file "$MACROS_FILE" "$TMP_CUSTOM_MOCKS" || {
    print_error "Failed to generate macros file"
    exit 1
}

# Generate linker wrapper header from template
[ "$VERBOSE" = "1" ] && print_info "Generating linker wrapper header: $LINKER_WRAPPER_FILE"
generate_linker_wrapper_header "$LINKER_WRAPPER_FILE" || {
    print_error "Failed to generate linker wrapper header"
    exit 1
}

# Step 9: Generate wrapper template file
generate_template_file "$TEMPLATE_FILE" "$MOCK_FUNCTIONS" "$WRAPPER_FUNCTIONS"

# Final summary (only if verbose)
if [ "$VERBOSE" = "1" ]; then
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
fi
