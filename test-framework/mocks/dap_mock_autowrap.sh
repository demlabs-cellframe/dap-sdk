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

# Check arguments
if [ $# -lt 3 ]; then
    echo "Usage: $0 <output_dir> <basename> <source1> <source2> ..."
    exit 1
fi

OUTPUT_DIR="$1"
BASENAME="$2"
shift 2  # Remove first two arguments
SOURCE_FILES=("$@")  # Remaining arguments are source files

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

# Step 5: Analyze DAP_MOCK_WRAPPER_CUSTOM usage and collect parameter counts
print_info "Analyzing DAP_MOCK_WRAPPER_CUSTOM usage for macro generation..."

# Temporary file for collecting param counts
TMP_PARAM_COUNTS="/tmp/param_counts_$$.txt"
> "$TMP_PARAM_COUNTS"

# Scan all source files for DAP_MOCK_WRAPPER_CUSTOM
# Parse multi-line DAP_MOCK_WRAPPER_CUSTOM declarations and count PARAM(...) entries
for SOURCE_FILE in "${SOURCE_FILES[@]}"; do
    [ ! -f "$SOURCE_FILE" ] && continue
    
    # Use awk to parse DAP_MOCK_WRAPPER_CUSTOM declarations
    # Count PARAM(...) entries between DAP_MOCK_WRAPPER_CUSTOM and opening brace {
    # Handle multi-line declarations
    awk '
    BEGIN {
        in_custom = 0
        param_count = 0
        paren_level = 0
        found_opening_paren = 0
    }
    /DAP_MOCK_WRAPPER_CUSTOM/ {
        in_custom = 1
        param_count = 0
        paren_level = 0
        found_opening_paren = 0
        
        # Check if this line contains opening parenthesis
        if (match($0, /DAP_MOCK_WRAPPER_CUSTOM\s*\(/)) {
            found_opening_paren = 1
            paren_level = 1  # Opening paren of DAP_MOCK_WRAPPER_CUSTOM(
            # Count PARAM( entries in rest of line
            rest = substr($0, RSTART + RLENGTH)
            while (match(rest, /PARAM\s*\(/)) {
                param_count++
                rest = substr(rest, RSTART + RLENGTH)
            }
            # Check for closing paren on same line
            while (match(rest, /[()]/)) {
                char = substr(rest, RSTART, 1)
                if (char == "(") paren_level++
                if (char == ")") {
                    paren_level--
                    if (paren_level == 0) {
                        # Found closing paren - output count and reset
                        print param_count
                        in_custom = 0
                        param_count = 0
                        paren_level = 0
                        found_opening_paren = 0
                        next
                    }
                }
                rest = substr(rest, RSTART + RLENGTH)
            }
        }
        next
    }
    in_custom {
        # Count parentheses to track when we exit the macro parameter list
        for (i = 1; i <= length($0); i++) {
            char = substr($0, i, 1)
            if (char == "(") {
                paren_level++
                if (!found_opening_paren) {
                    found_opening_paren = 1
                    paren_level = 1
                }
            }
            if (char == ")") {
                paren_level--
                if (paren_level <= 0 && found_opening_paren) {
                    # We found the closing parenthesis of DAP_MOCK_WRAPPER_CUSTOM
                    # Output param count
                    print param_count
                    in_custom = 0
                    param_count = 0
                    paren_level = 0
                    found_opening_paren = 0
                    next
                }
            }
            if (char == "{" && found_opening_paren && paren_level == 0) {
                # We found opening brace - this means no parameters (or already closed)
                # Output param count
                print param_count
                in_custom = 0
                param_count = 0
                paren_level = 0
                found_opening_paren = 0
                next
            }
        }
        
        # Count PARAM( entries in current line
        line = $0
        while (match(line, /PARAM\s*\(/)) {
            param_count++
            line = substr(line, RSTART + RLENGTH)
        }
    }
    ' "$SOURCE_FILE" >> "$TMP_PARAM_COUNTS" || true
done

# Collect unique parameter counts
PARAM_COUNTS=$(sort -u -n "$TMP_PARAM_COUNTS" 2>/dev/null | tr '\n' ' ')
rm -f "$TMP_PARAM_COUNTS"

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

# Step 6: Generate specialized macros header file
print_info "Generating specialized macros header: $MACROS_FILE"

cat > "$MACROS_FILE" << 'EOF'
/**
 * Auto-generated mock macros for DAP_MOCK_WRAPPER_CUSTOM
 * Generated by dap_mock_autowrap.sh
 * 
 * This file contains only the macros needed for this specific test target.
 * Do not modify manually - it will be regenerated.
 * 
 * This file is included via CMake's -include flag before dap_mock_linker_wrapper.h
 * No include guards needed - file is included unconditionally via -include
 */

// Include standard headers for size_t and other basic types
#include <stddef.h>

// Include base macros we need from dap_mock_linker_wrapper.h
// Since this file is included first, we need the base macros here
#ifndef _DAP_MOCK_NARGS_DEFINED
#define _DAP_MOCK_NARGS_DEFINED
EOF

# Generate _DAP_MOCK_NARGS dynamically based on MAX_ARGS_COUNT
# _DAP_MOCK_NARGS needs a sequence: N, N-1, N-2, ..., 1, 0
echo "// Dynamically generated _DAP_MOCK_NARGS supporting up to $MAX_ARGS_COUNT arguments" >> "$MACROS_FILE"
echo -n "#define _DAP_MOCK_NARGS(...) \\" >> "$MACROS_FILE"
echo "" >> "$MACROS_FILE"
echo -n "    _DAP_MOCK_NARGS_IMPL(__VA_ARGS__" >> "$MACROS_FILE"
# Generate descending sequence: MAX_ARGS_COUNT, MAX_ARGS_COUNT-1, ..., 1, 0
for i in $(seq $MAX_ARGS_COUNT -1 0); do
    echo -n ", $i" >> "$MACROS_FILE"
done
echo ")" >> "$MACROS_FILE"

# Generate _DAP_MOCK_NARGS_IMPL with enough parameters
echo -n "#define _DAP_MOCK_NARGS_IMPL(" >> "$MACROS_FILE"
for i in $(seq 1 $MAX_ARGS_COUNT); do
    echo -n "_$i" >> "$MACROS_FILE"
    [ $i -lt $MAX_ARGS_COUNT ] && echo -n "," >> "$MACROS_FILE"
done
echo -n ", N, ...) N" >> "$MACROS_FILE"
echo "" >> "$MACROS_FILE"

cat >> "$MACROS_FILE" << 'EOF'

#define _DAP_MOCK_IS_EMPTY(...) \
    (_DAP_MOCK_NARGS(__VA_ARGS__) == 0)
#endif

EOF

       # Generate _DAP_MOCK_MAP_N macros for each needed count
       # Note: count is the number of PARAM() entries, but each PARAM expands to 2 arguments
       # So _DAP_MOCK_MAP_N needs to handle 2*count arguments
       for count in "${PARAM_COUNTS_ARRAY[@]}"; do
           # Skip empty entries
           [ -z "$count" ] && continue
           
           # Generate _DAP_MOCK_MAP_N macro
           echo "// Macro for $count parameter(s) (PARAM entries)" >> "$MACROS_FILE"
           echo -n "#define _DAP_MOCK_MAP_${count}(macro" >> "$MACROS_FILE"
           
           if [ "$count" -eq 0 ]; then
               echo -n ", ...) \\" >> "$MACROS_FILE"
               echo "" >> "$MACROS_FILE"
               echo "" >> "$MACROS_FILE"
           else
               # Generate parameter list: p1, p2, p3, p4, ... (pairs: type1, name1, type2, name2, ...)
               # Each PARAM expands to 2 arguments, so we need 2*count arguments
               total_args=$((count * 2))
               for i in $(seq 1 $total_args); do
                   echo -n ", p$i" >> "$MACROS_FILE"
               done
               echo -n ", ...) \\" >> "$MACROS_FILE"
               echo "" >> "$MACROS_FILE"
               
               # Generate macro body: macro(p1, p2), macro(p3, p4), ... (apply macro to pairs)
               echo -n "    macro(p1, p2)" >> "$MACROS_FILE"
               for i in $(seq 2 $count); do
                   type_idx=$((i * 2 - 1))
                   name_idx=$((i * 2))
                   echo -n ", macro(p${type_idx}, p${name_idx})" >> "$MACROS_FILE"
               done
               echo "" >> "$MACROS_FILE"
           fi
       done
       
       # Always generate _DAP_MOCK_MAP_1 if we have _DAP_MOCK_MAP_IMPL_COND_1_0 (for single parameter case)
       # Check if 1 is in the array
       if [[ ! " ${PARAM_COUNTS_ARRAY[@]} " =~ " 1 " ]]; then
           echo "// Macro for 1 parameter(s) - needed for _DAP_MOCK_MAP_IMPL_COND_1_0" >> "$MACROS_FILE"
           echo "#define _DAP_MOCK_MAP_1(macro, p1, p2, ...) \\" >> "$MACROS_FILE"
           echo "    macro(p1, p2)" >> "$MACROS_FILE"
       fi

# Generate _DAP_MOCK_MAP_IMPL_COND_N macros for each needed count
cat >> "$MACROS_FILE" << 'EOF'

// Simplified _DAP_MOCK_MAP implementation
// Only handles the specific parameter counts we need
// Note: _DAP_MOCK_NARGS and _DAP_MOCK_IS_EMPTY must be defined in dap_mock_linker_wrapper.h
// which is included after this file, so we forward-reference them here
// Each PARAM expands to 2 arguments, so we need to divide arg count by 2
// Use helper macro to compute number of PARAM entries
#define _DAP_MOCK_MAP(macro, ...) \
    _DAP_MOCK_MAP_IMPL(_DAP_MOCK_MAP_COUNT_PARAMS(__VA_ARGS__), macro, __VA_ARGS__)

// Count number of PARAM entries (each PARAM is 2 args)
// Empty: 0 args -> 0 params
// 1 param: 2 args -> 1 param
// 3 params: 6 args -> 3 params
#define _DAP_MOCK_MAP_COUNT_PARAMS(...) \
    _DAP_MOCK_MAP_COUNT_PARAMS_EXPAND(_DAP_MOCK_NARGS(__VA_ARGS__))

// Expand arg count before token concatenation
#define _DAP_MOCK_MAP_COUNT_PARAMS_EXPAND(N) \
    _DAP_MOCK_MAP_COUNT_PARAMS_EXPAND2(N)

#define _DAP_MOCK_MAP_COUNT_PARAMS_EXPAND2(N) \
    _DAP_MOCK_MAP_COUNT_PARAMS_HELPER_##N

// Helper to convert arg count to param count
#define _DAP_MOCK_MAP_COUNT_PARAMS_HELPER(N) \
    _DAP_MOCK_MAP_COUNT_PARAMS_HELPER_##N

// Generate mappings dynamically: 0 args -> 0 params, 2 args -> 1 param, 4 args -> 2 params, etc.
// Each PARAM expands to 2 args (type, name), so N args = N/2 params (rounded down)
EOF

# Generate _DAP_MOCK_MAP_COUNT_PARAMS_HELPER_N macros dynamically
# Generate ONLY for arg counts from 0 to MAX_ARGS_COUNT (no hardcoded limit)
for i in $(seq 0 $MAX_ARGS_COUNT); do
    # Calculate param count: N args / 2 (integer division)
    param_count=$((i / 2))
    echo "#define _DAP_MOCK_MAP_COUNT_PARAMS_HELPER_${i} ${param_count}" >> "$MACROS_FILE"
done

# Add default case for safety (should never be reached if generation is correct)
cat >> "$MACROS_FILE" << 'EOF'
// Default case for values beyond generated range (should never be reached)
#define _DAP_MOCK_MAP_COUNT_PARAMS_HELPER_DEFAULT 0

#define _DAP_MOCK_MAP_IMPL(N, macro, ...) \
    _DAP_MOCK_MAP_IMPL_COND(N, macro, __VA_ARGS__)

#define _DAP_MOCK_MAP_IMPL_COND(N, macro, ...) \
    _DAP_MOCK_MAP_IMPL_COND_EVAL(N, macro, __VA_ARGS__)

#define _DAP_MOCK_MAP_IMPL_COND_EVAL(N, macro, ...) \
    _DAP_MOCK_MAP_IMPL_COND_##N(macro, __VA_ARGS__)

// Handle empty case (0 params)
#define _DAP_MOCK_MAP_IMPL_COND_0(macro, ...) \
    _DAP_MOCK_MAP_0(macro)

// Handle empty case (N=1 and is_empty=1)
// For N=1, check if empty and dispatch accordingly
// Now with PARAM expanding to 2 args, empty means 0 args, single param means 2 args
// So we can use _DAP_MOCK_NARGS directly: 0 for empty, 2 for single param
#define _DAP_MOCK_MAP_IMPL_COND_1(macro, ...) \
    _DAP_MOCK_MAP_IMPL_COND_1_EXPAND(_DAP_MOCK_NARGS(__VA_ARGS__), macro, __VA_ARGS__)

// Expand arg count before token concatenation
#define _DAP_MOCK_MAP_IMPL_COND_1_EXPAND(N, macro, ...) \
    _DAP_MOCK_MAP_IMPL_COND_1_EXPAND2(N, macro, __VA_ARGS__)

#define _DAP_MOCK_MAP_IMPL_COND_1_EXPAND2(N, macro, ...) \
    _DAP_MOCK_MAP_IMPL_COND_1_##N(macro, __VA_ARGS__)

// Empty case: 0 arguments
#define _DAP_MOCK_MAP_IMPL_COND_1_0(macro, ...) \
    _DAP_MOCK_MAP_0(macro)

// Single param case: 2 arguments (type, name)
#define _DAP_MOCK_MAP_IMPL_COND_1_2(macro, p1, p2, ...) \
    _DAP_MOCK_MAP_1(macro, p1, p2)

EOF

# Generate _DAP_MOCK_MAP_IMPL_COND_N for each needed count > 1
for count in "${PARAM_COUNTS_ARRAY[@]}"; do
    [ -z "$count" ] && continue
    [ "$count" -le 1 ] && continue
    
    echo "// Conditional macro for $count parameters" >> "$MACROS_FILE"
    echo "#define _DAP_MOCK_MAP_IMPL_COND_${count}(macro, ...) \\" >> "$MACROS_FILE"
    echo "    _DAP_MOCK_MAP_${count}(macro, __VA_ARGS__)" >> "$MACROS_FILE"
done

# End of generated macros file
# No closing #endif - file is included unconditionally via -include

print_success "Generated macros header with ${#PARAM_COUNTS_ARRAY[@]} parameter count(s)"

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
    
    cat > "$TEMPLATE_FILE" << 'EOF'
/**
 * Auto-generated wrapper template
 * Copy the needed wrappers to your test file and customize as needed
 */

#include "dap_mock.h"
#include "dap_mock_linker_wrapper.h"

EOF
    
    echo "$MISSING_FUNCTIONS" | while read func; do
        cat >> "$TEMPLATE_FILE" << EOF
// Wrapper for $func
DAP_MOCK_WRAPPER_CUSTOM(void*, $func,
    (/* add parameters here */))
{
    if (g_mock_$func && g_mock_$func->enabled) {
        // Add your mock logic here
        return g_mock_$func->return_value.ptr;
    }
    return __real_$func(/* forward parameters */);
}

EOF
        echo "   ⚠️  $func"
    done
    
    print_success "Template generated with $MISSING_COUNT function stubs"
    fi
else
    # No wrappers found but mocks exist - generate template for all
    FUNC_COUNT=$(echo "$MOCK_FUNCTIONS" | wc -l)
    print_warning "No wrappers found for $FUNC_COUNT functions"
    print_info "Generating template: $TEMPLATE_FILE"
    
    cat > "$TEMPLATE_FILE" << 'EOF'
/**
 * Auto-generated wrapper template
 * Copy the needed wrappers to your test file and customize as needed
 */

#include "dap_mock.h"
#include "dap_mock_linker_wrapper.h"

EOF
    
    echo "$MOCK_FUNCTIONS" | while read func; do
        cat >> "$TEMPLATE_FILE" << EOF
// Wrapper for $func
DAP_MOCK_WRAPPER_CUSTOM(void*, $func,
    (/* add parameters here */))
{
    if (g_mock_$func && g_mock_$func->enabled) {
        // Add your mock logic here
        return g_mock_$func->return_value.ptr;
    }
    return __real_$func(/* forward parameters */);
}

EOF
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
