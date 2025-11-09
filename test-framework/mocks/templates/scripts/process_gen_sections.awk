# Process generation sections: execute scripts and output generated content
# Input format: type|start_line|end_line|code (multi-line code preserved)
# Output format: type|start_line|end_line|generated_content
# This script executes AWK or shell scripts and captures their output

BEGIN {
    section_type = ""
    start_line = ""
    end_line = ""
    section_code = ""
    in_section = 0
}

/^(awk_gen|sh_gen)\|/ {
    # Save and process previous section if any
    if (section_type != "") {
        process_section()
    }
    # Start new section
    # Parse manually to avoid issues with | in code
    pos1 = index($0, "|")
    if (pos1 == 0) next
    
    pos2 = index(substr($0, pos1 + 1), "|")
    if (pos2 == 0) next
    pos2 = pos2 + pos1
    
    pos3 = index(substr($0, pos2 + 1), "|")
    if (pos3 == 0) next
    pos3 = pos3 + pos2
    
    # Extract fields
    section_type = substr($0, 1, pos1 - 1)
    start_line = substr($0, pos1 + 1, pos2 - pos1 - 1)
    end_line = substr($0, pos2 + 1, pos3 - pos2 - 1)
    section_code = substr($0, pos3 + 1)
    
    in_section = 1
    next
}

in_section {
    # Continue accumulating section code (preserve | characters and newlines)
    section_code = section_code "\n" $0
    next
}

END {
    # Process last section
    if (section_type != "") {
        process_section()
    }
}

function process_section() {
    if (section_type == "awk_gen") {
        # Write AWK script to temp file
        script_file = "/tmp/awk_script_" NR "_" rand() ".awk"
        print section_code > script_file
        close(script_file)
        
        # Execute AWK script (pass temp_file as input)
        # Pass environment variables explicitly via env command
        # Also include common library if @include directive is used
        env_vars = ""
        
        # Pass NORMALIZATION_MACROS_DATA for type normalization templates
        if ("NORMALIZATION_MACROS_DATA" in ENVIRON && ENVIRON["NORMALIZATION_MACROS_DATA"] != "") {
            norm_data = ENVIRON["NORMALIZATION_MACROS_DATA"]
            gsub(/"/, "\\\"", norm_data)
            env_vars = env_vars " NORMALIZATION_MACROS_DATA=\"" norm_data "\""
        }
        
        # Pass file paths for template constructs
        if ("POINTER_TYPES_FILE" in ENVIRON && ENVIRON["POINTER_TYPES_FILE"] != "") {
            env_vars = env_vars " POINTER_TYPES_FILE=\"" ENVIRON["POINTER_TYPES_FILE"] "\""
        }
        if ("NON_POINTER_TYPES_FILE" in ENVIRON && ENVIRON["NON_POINTER_TYPES_FILE"] != "") {
            env_vars = env_vars " NON_POINTER_TYPES_FILE=\"" ENVIRON["NON_POINTER_TYPES_FILE"] "\""
        }
        
        # Check if script uses @include - need to preprocess and include common library
        # For @include to work, we need to manually include the library file
        scripts_dir = ENVIRON["SCRIPTS_DIR"]
        if (scripts_dir == "") scripts_dir = "."
        
        # Read script file once to check for @include and collect all lines
        has_include = 0
        include_file = ""
        script_lines_count = 0
        delete script_lines
        
        while ((getline line < script_file) > 0) {
            script_lines_count++
            script_lines[script_lines_count] = line
            
            if (!has_include && line ~ /^@include[ \t]+"/) {
                has_include = 1
                # Extract filename from @include "filename"
                if (match(line, /"([^"]+)"/)) {
                    include_file = substr(line, RSTART + 1, RLENGTH - 2)
                }
            }
        }
        close(script_file)
        
        if (has_include && include_file != "") {
            # Preprocess: create new script file with included library
            common_lib_file = scripts_dir "/" include_file
            preprocessed_script = "/tmp/awk_preprocessed_" NR "_" rand() ".awk"
            
            # Check if common library file exists and copy it
            # If library not found, fail immediately - no fallback
            lib_exists = 0
            lib_read_error = 0
            while ((getline line < common_lib_file) > 0) {
                lib_exists = 1
                print line > preprocessed_script
            }
            if ((getline < common_lib_file) < 0 && lib_exists == 0) {
                # File doesn't exist or can't be read
                lib_read_error = 1
            }
            close(common_lib_file)
            
            if (lib_read_error || !lib_exists) {
                print "ERROR: Common library file not found: " common_lib_file > "/dev/stderr"
                print "ERROR: Script requires @include \"" include_file "\" but library is missing" > "/dev/stderr"
                print "ERROR: Check SCRIPTS_DIR environment variable (current: " scripts_dir ")" > "/dev/stderr"
                exit 1
            }
            
            # Add separator comment
            print "" > preprocessed_script
            print "# ============================================================================" > preprocessed_script
            print "# Main script (from template)" > preprocessed_script
            print "# ============================================================================" > preprocessed_script
            print "" > preprocessed_script
            
            # Copy original script lines (skip @include line)
            for (i = 1; i <= script_lines_count; i++) {
                if (!(script_lines[i] ~ /^@include/)) {
                    print script_lines[i] > preprocessed_script
                }
            }
            close(preprocessed_script)
            
            # Use preprocessed script
            if (env_vars != "") {
                cmd = "env" env_vars " awk -f " preprocessed_script " " temp_file " 2>&1"
            } else {
                cmd = "awk -f " preprocessed_script " " temp_file " 2>&1"
            }
        } else {
            # Regular awk (no @include)
            if (env_vars != "") {
                cmd = "env" env_vars " awk -f " script_file " " temp_file " 2>&1"
            } else {
        cmd = "awk -f " script_file " " temp_file " 2>&1"
            }
        }
        generated_content = ""
        while ((cmd | getline line) > 0) {
            if (generated_content != "") {
                generated_content = generated_content "\n"
            }
            generated_content = generated_content line
        }
        close(cmd)
        
        print "AWK_GEN|" start_line "|" end_line "|" generated_content
        
        # Cleanup: remove script file and preprocessed script if it exists
        system("rm -f " script_file)
        if (has_include && include_file != "") {
            system("rm -f " preprocessed_script)
        }
        } else if (section_type == "sh_gen") {
        # Write shell script to temp file
        script_file = "/tmp/sh_script_" NR "_" rand() ".sh"
        # Write section code as-is (no debug output)
        print section_code > script_file
        close(script_file)
        
        # Write environment variables to a file
        env_file = "/tmp/sh_env_" NR "_" rand() ".sh"
        env_count = 0
        
        # Always include these variables if they exist in ENVIRON
        vars_to_include["CUSTOM_MOCKS_LIST"] = 1
        vars_to_include["WRAPPER_FUNCTIONS"] = 1
        vars_to_include["PARAM_COUNTS_ARRAY"] = 1
        vars_to_include["MAX_ARGS_COUNT"] = 1
        vars_to_include["MISSING_FUNCTIONS"] = 1
        vars_to_include["temp_file"] = 1
        
        for (key in vars_to_include) {
            if (key in ENVIRON && length(ENVIRON[key]) > 0) {
                value = ENVIRON[key]
                # Debug: save info about CUSTOM_MOCKS_LIST
                if (key == "CUSTOM_MOCKS_LIST") {
                    debug_info = "/tmp/debug_custom_mocks_env_" start_line "_" rand() ".txt"
                    print "CUSTOM_MOCKS_LIST in ENVIRON: yes" > debug_info
                    print "Length: " length(value) > debug_info
                    print "First 100 chars: " substr(value, 1, 100) > debug_info
                    close(debug_info)
                }
                # Escape for shell script - escape single quotes
                gsub(/'/, "'\\''", value)  # Escape single quotes
                # Write multi-line value properly
                print "export " key "='" value "'" > env_file
                env_count++
            } else if (key == "CUSTOM_MOCKS_LIST") {
                # Debug: CUSTOM_MOCKS_LIST not in ENVIRON
                debug_info = "/tmp/debug_custom_mocks_env_" start_line "_" rand() ".txt"
                print "CUSTOM_MOCKS_LIST in ENVIRON: no" > debug_info
                close(debug_info)
            }
        }
        close(env_file)
        
        # Check if we have CUSTOM_MOCKS_LIST or other relevant vars
        # Note: CUSTOM_MOCKS_LIST can be multi-line, so check length, not just != ""
        has_vars = 0
        if ("CUSTOM_MOCKS_LIST" in ENVIRON && length(ENVIRON["CUSTOM_MOCKS_LIST"]) > 0) has_vars = 1
        if ("PARAM_COUNTS_ARRAY" in ENVIRON && length(ENVIRON["PARAM_COUNTS_ARRAY"]) > 0) has_vars = 1
        if ("MAX_ARGS_COUNT" in ENVIRON && length(ENVIRON["MAX_ARGS_COUNT"]) > 0 && ENVIRON["MAX_ARGS_COUNT"] != "0") has_vars = 1
        if ("MISSING_FUNCTIONS" in ENVIRON && length(ENVIRON["MISSING_FUNCTIONS"]) > 0) has_vars = 1
        
        if (has_vars) {
            # Write wrapper script that sources env and executes script
            wrapper_file = "/tmp/sh_wrapper_" NR "_" rand() ".sh"
            print "#!/bin/bash" > wrapper_file
            print ". " env_file > wrapper_file
            print "bash " script_file > wrapper_file
            close(wrapper_file)
            system("chmod +x " wrapper_file)
            
            # Execute wrapper and capture output to temp file
            # Redirect stderr to separate file to avoid mixing with stdout
            output_file = "/tmp/sh_output_" NR "_" rand() ".txt"
            error_file = "/tmp/sh_error_" NR "_" rand() ".txt"
            # Execute wrapper directly - system() waits for completion
            exit_code = system(wrapper_file " > " output_file " 2> " error_file)
            
            # Debug: check exit code and file sizes
            if (exit_code != 0) {
                # Script failed - save for debugging
                debug_error_file = "/tmp/debug_wrapper_error_" start_line "_" rand() ".txt"
                print "Wrapper exit code: " exit_code > debug_error_file
                print "Output file: " output_file > debug_error_file
                print "Error file: " error_file > debug_error_file
                close(debug_error_file)
            }
            
            # Read output file (only stdout)
            generated_content = ""
            line_count = 0
            raw_content = ""
            # First, read all content
            while ((getline line < output_file) > 0) {
                line_count++
                if (raw_content != "") {
                    raw_content = raw_content "\n"
                }
                raw_content = raw_content line
            }
            close(output_file)
            
            # Now filter the content - skip debug lines and variable assignments
            if (raw_content != "") {
                split(raw_content, lines, "\n")
                for (i = 1; i <= length(lines); i++) {
                    line = lines[i]
                    # Skip debug output lines and variable assignments
                    # But keep lines starting with #include (our generated content)
                    # Skip DEBUG: lines but keep everything else
                    # Don't filter lines starting with #include - these are our generated content
                    if (line ~ /^#include/) {
                        # Always keep #include lines
                        if (generated_content != "") {
                            generated_content = generated_content "\n"
                        }
                        generated_content = generated_content line
                    } else if (line !~ /^\+/ && line !~ /^CUSTOM_MOCKS_LIST=/ && line !~ /^WRAPPER_FUNCTIONS=/ && line !~ /^set -x/ && line !~ /^DEBUG:/ && line != "") {
                        # Keep other non-empty lines that are not debug output
                        if (generated_content != "") {
                            generated_content = generated_content "\n"
                        }
                        generated_content = generated_content line
                    }
                }
            }
            
            # Debug: save raw output if content is empty but we have CUSTOM_MOCKS_LIST
            if ("CUSTOM_MOCKS_LIST" in ENVIRON && length(ENVIRON["CUSTOM_MOCKS_LIST"]) > 0 && generated_content == "") {
                debug_raw_file = "/tmp/debug_raw_output_" start_line "_" rand() ".txt"
                print "Raw content length: " length(raw_content) > debug_raw_file
                print "Line count: " line_count > debug_raw_file
                print "Exit code: " exit_code > debug_raw_file
                print "Raw content:" > debug_raw_file
                print raw_content > debug_raw_file
                print "---" > debug_raw_file
                print "Generated content length: " length(generated_content) > debug_raw_file
                print "Generated content:" > debug_raw_file
                print generated_content > debug_raw_file
                print "--- Filtered lines ---" > debug_raw_file
                # Show what was filtered
                split(raw_content, lines, "\n")
                for (i = 1; i <= length(lines); i++) {
                    line = lines[i]
                    if (line ~ /^#include/) {
                        print "KEPT (include): " line > debug_raw_file
                    } else if (line ~ /^\+/ || line ~ /^CUSTOM_MOCKS_LIST=/ || line ~ /^WRAPPER_FUNCTIONS=/ || line ~ /^set -x/ || line ~ /^DEBUG:/ || line == "") {
                        print "FILTERED: " line > debug_raw_file
                    } else {
                        print "KEPT: " line > debug_raw_file
                    }
                }
                close(debug_raw_file)
            }
            
            # If content is still empty, check error file for debugging
            if (generated_content == "" && line_count == 0) {
                # Try reading error file to see what went wrong
                error_content = ""
                while ((getline line < error_file) > 0) {
                    error_content = error_content line "\n"
                }
                close(error_file)
                # Save error for debugging (only for custom_mocks_main template)
                if (error_content != "") {
                    debug_file = "/tmp/sh_debug_" NR "_" rand() ".txt"
                    print "Script: " script_file > debug_file
                    print "Wrapper: " wrapper_file > debug_file
                    print "Env file: " env_file > debug_file
                    print "Exit code: " exit_code > debug_file
                    print "Error output:" > debug_file
                    print error_content > debug_file
                    close(debug_file)
                }
            }
            system("rm -f " error_file)
        } else {
            # No relevant vars - empty content
            generated_content = ""
        }
        
        print "SH_GEN|" start_line "|" end_line "|" generated_content
        
        # Debug: save files if content is empty but we have CUSTOM_MOCKS_LIST
        if ("CUSTOM_MOCKS_LIST" in ENVIRON && length(ENVIRON["CUSTOM_MOCKS_LIST"]) > 0 && generated_content == "") {
            debug_id = start_line "_" rand()
            system("cp " script_file " /tmp/debug_script_" debug_id ".sh 2>/dev/null || true")
            system("cp " env_file " /tmp/debug_env_" debug_id ".sh 2>/dev/null || true")
            system("cp " wrapper_file " /tmp/debug_wrapper_" debug_id ".sh 2>/dev/null || true")
            system("cp " output_file " /tmp/debug_output_" debug_id ".txt 2>/dev/null || true")
            system("cp " error_file " /tmp/debug_error_" debug_id ".txt 2>/dev/null || true")
            # Don't delete files immediately - keep them for debugging
        }
        # Always clean up temp files after a delay (they're copied for debugging if needed)
        system("rm -f " script_file " " env_file " " wrapper_file " " output_file " " error_file)
    }
    
    # Reset for next section
    section_type = ""
    start_line = ""
    end_line = ""
    section_code = ""
    in_section = 0
}

