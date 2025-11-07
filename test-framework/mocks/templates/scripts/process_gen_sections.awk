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
        cmd = "awk -f " script_file " " temp_file " 2>&1"
        generated_content = ""
        while ((cmd | getline line) > 0) {
            if (generated_content != "") {
                generated_content = generated_content "\n"
            }
            generated_content = generated_content line
        }
        close(cmd)
        
        print "AWK_GEN|" start_line "|" end_line "|" generated_content
        system("rm -f " script_file)
        } else if (section_type == "sh_gen") {
        # Write shell script to temp file
        script_file = "/tmp/sh_script_" NR "_" rand() ".sh"
        # Add debug output to script if CUSTOM_MOCKS_LIST is involved
        if (section_code ~ /CUSTOM_MOCKS_LIST/) {
            # Prepend debug output to stdout (will be filtered later)
            print "echo 'DEBUG: Script starting, CUSTOM_MOCKS_LIST length='${#CUSTOM_MOCKS_LIST}" > script_file
            print "echo 'DEBUG: CUSTOM_MOCKS_LIST first 50 chars:'${CUSTOM_MOCKS_LIST:0:50}" > script_file
            print "echo 'DEBUG: Testing if condition...'" > script_file
            print "if [ -n \"$CUSTOM_MOCKS_LIST\" ]; then echo 'DEBUG: CUSTOM_MOCKS_LIST is not empty'; else echo 'DEBUG: CUSTOM_MOCKS_LIST is empty'; fi" > script_file
            # Add debug output inside the while loop
            print "echo 'DEBUG: About to enter while loop'" > script_file
        }
        # Write section code as-is
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
                # Escape for shell script - escape single quotes
                gsub(/'/, "'\\''", value)  # Escape single quotes
                # Write multi-line value properly
                print "export " key "='" value "'" > env_file
                env_count++
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
            # Debug: verify variables are set and output to stdout (not stderr)
            print "echo 'DEBUG: CUSTOM_MOCKS_LIST length='${#CUSTOM_MOCKS_LIST}" > wrapper_file
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
                    if (line !~ /^\+/ && line !~ /^CUSTOM_MOCKS_LIST=/ && line !~ /^WRAPPER_FUNCTIONS=/ && line !~ /^set -x/ && line !~ /^DEBUG:/) {
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
        } else {
            system("rm -f " script_file " " env_file " " wrapper_file " " output_file " " error_file)
        }
    }
    
    # Reset for next section
    section_type = ""
    start_line = ""
    end_line = ""
    section_code = ""
    in_section = 0
}

