#!/bin/bash
# DAP Template Processing Library
# Provides functions for processing template files with placeholders and embedded scripts
#
# Usage:
#   source dap_tpl.sh
#   replace_template_placeholders <template_file> <output_file> <var1=value1> <var2=value2> ...

set -e

# Colors for output (if not already defined)
if [ -z "${RED:-}" ]; then
    RED='\033[0;31m'
    GREEN='\033[0;32m'
    YELLOW='\033[1;33m'
    BLUE='\033[0;34m'
    NC='\033[0m' # No Color
fi

# Print error message
tpl_print_error() {
    echo -e "${RED}❌ $1${NC}" >&2
}

# Function to replace placeholders in template file
# Usage: replace_template_placeholders <template_file> <output_file> <var1=value1> <var2=value2> ...
# For multi-line values, use a temporary file: replace_template_placeholders <template> <output> <var1=@file1> <var2=value2>
# 
# Environment variables:
#   SCRIPTS_DIR - directory containing AWK scripts for template processing
#   TEMPLATES_DIR - directory containing template files (optional)
replace_template_placeholders() {
    local template_file="$1"
    local output_file="$2"
    shift 2
    
    if [ ! -f "$template_file" ]; then
        tpl_print_error "Template file not found: $template_file"
        return 1
    fi
    
    if [ -z "${SCRIPTS_DIR:-}" ]; then
        tpl_print_error "SCRIPTS_DIR is not set"
        return 1
    fi
    
    # Ensure environment variables are exported for child processes
    # These are read by AWK scripts via ENVIRON array
    # Always export (even if empty) - AWK scripts check for empty themselves
    # Use direct assignment to preserve multi-line values
    export CUSTOM_MOCKS_LIST="${CUSTOM_MOCKS_LIST}"
    export WRAPPER_FUNCTIONS="${WRAPPER_FUNCTIONS}"
    export PARAM_COUNTS_ARRAY="${PARAM_COUNTS_ARRAY}"
    export MAX_ARGS_COUNT="${MAX_ARGS_COUNT}"
    export MISSING_FUNCTIONS="${MISSING_FUNCTIONS}"
    
    # Read template and remove all sections for placeholder processing
    local temp_file=$(mktemp)
    awk -f "${SCRIPTS_DIR}/remove_sections.awk" "$template_file" > "$temp_file"
    
    # Extract all sections before processing placeholders
    local sections_file=$(mktemp)
    awk -f "${SCRIPTS_DIR}/extract_sections.awk" "$template_file" > "$sections_file"
    
    # Replace each placeholder
    while [ $# -gt 0 ]; do
        local var_value="$1"
        local var_name="${var_value%%=*}"
        local var_val="${var_value#*=}"
        
        # Check if value is a file reference (@filename)
        if [ "${var_val:0:1}" = "@" ]; then
            local val_file="${var_val:1}"
            if [ ! -f "$val_file" ]; then
                tpl_print_error "Value file not found: $val_file"
                rm -f "$temp_file" "$sections_file"
                return 1
            fi
            # Use awk script to replace placeholder with file content directly
            awk -v placeholder="{{${var_name}}}" -v val_file="$val_file" \
                -f "${SCRIPTS_DIR}/replace_template_file.awk" \
                "$temp_file" > "${temp_file}.new"
        else
            # Simple value replacement using awk script
            awk -v placeholder="{{${var_name}}}" -v val="$var_val" \
                -f "${SCRIPTS_DIR}/replace_template_value.awk" \
                "$temp_file" > "${temp_file}.new"
        fi
        
        mv "${temp_file}.new" "$temp_file"
        shift
    done
    
    # Process content generation sections first ({{AWK:...}} and {{#/bin/sh:...}})
    if [ -s "$sections_file" ]; then
        # Use awk to filter gen sections while preserving multi-line content
        # This replaces grep which would break multi-line sections
        local gen_sections_file=$(mktemp)
        awk '
            BEGIN {
                in_gen_section = 0
            }
            /^awk_gen\|/ {
                # Start of gen section
                print
                in_gen_section = 1
                next
            }
            /^sh_gen\|/ {
                # Start of gen section
                print
                in_gen_section = 1
                next
            }
            in_gen_section == 1 {
                # Continue outputting lines until we hit next section header
                if (/^awk_gen\|/ || /^sh_gen\|/) {
                    # New gen section - output and continue
                    print
                    in_gen_section = 1
                } else if (/^awk_postproc\|/) {
                    # Postproc section - stop current gen section
                    in_gen_section = 0
                } else {
                    # Continue current gen section
                    print
                }
                next
            }
        ' "$sections_file" > "$gen_sections_file" || true
        
        if [ -s "$gen_sections_file" ]; then
            # Use awk script to properly parse multi-line sections
            awk -f "${SCRIPTS_DIR}/parse_multiline_sections.awk" "$gen_sections_file" > "${gen_sections_file}.parsed" || true
            
            if [ -s "${gen_sections_file}.parsed" ]; then
                # Process each section using awk script to handle multi-line section_code and execute scripts
                # Re-export variables right before AWK call to ensure they're available
                # AWK reads ENVIRON array at startup, so variables must be exported before AWK starts
                # Always export (even if empty) - AWK scripts check for empty themselves
                # Use direct assignment to preserve multi-line values
                export CUSTOM_MOCKS_LIST="${CUSTOM_MOCKS_LIST}"
                export WRAPPER_FUNCTIONS="${WRAPPER_FUNCTIONS}"
                export PARAM_COUNTS_ARRAY="${PARAM_COUNTS_ARRAY}"
                export MAX_ARGS_COUNT="${MAX_ARGS_COUNT}"
                export MISSING_FUNCTIONS="${MISSING_FUNCTIONS}"
                awk -f "${SCRIPTS_DIR}/process_gen_sections.awk" \
                    -v temp_file="$temp_file" \
                    "${gen_sections_file}.parsed" > "${gen_sections_file}.processed" || true
                
                if [ -s "${gen_sections_file}.processed" ]; then
                    # Debug: save processed file for custom_mocks_main template
                    if [ "$template_file" = "${TEMPLATES_DIR}/custom_mocks_main.h.tpl" ]; then
                        cp "${gen_sections_file}.processed" "/tmp/last_processed_custom_mocks.txt" 2>/dev/null || true
                    fi
                    
                    # Use awk to read processed sections and replace markers
                    # This handles multi-line generated_content properly
                    awk -f "${SCRIPTS_DIR}/replace_gen_sections.awk" \
                        -v template_file="$template_file" \
                        "${gen_sections_file}.processed" "$temp_file" > "${temp_file}.gen" && mv "${temp_file}.gen" "$temp_file"
                fi
                rm -f "${gen_sections_file}.processed"
            fi
            rm -f "${gen_sections_file}.parsed" "$gen_sections_file"
        fi
        
        # Process post-processing sections ({{postproc:{{AWK:...}}}})
        local postproc_sections_file=$(mktemp)
        grep "^awk_postproc|" "$sections_file" > "$postproc_sections_file" || true
        
        if [ -s "$postproc_sections_file" ]; then
            while IFS='|' read -r section_type start_line end_line section_code; do
                [ -z "$section_code" ] && continue
                
                # Create temporary AWK script file for post-processing
                local awk_script_file=$(mktemp)
                echo "$section_code" > "$awk_script_file"
                
                # Execute AWK script on the current file (post-processing modifies existing content)
                awk -f "$awk_script_file" "$temp_file" > "${temp_file}.postproc" && mv "${temp_file}.postproc" "$temp_file"
                
                rm -f "$awk_script_file"
            done < "$postproc_sections_file"
        fi
        rm -f "$postproc_sections_file"
    fi
    
    cat "$temp_file" > "$output_file"
    rm -f "$temp_file" "$sections_file"
}

