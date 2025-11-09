# Process template constructs: evaluate conditions, execute loops, set variables
# Input format: type|start_line|end_line|condition/var/script|content
# Output: processed content with constructs evaluated

# Function to recursively process nested template constructs in content
function process_nested_constructs(content_text,    temp_file, constructs_file, processed_file, scripts_dir, env_vars, cmd, line, result) {
    # Check if content contains template constructs
    if (content_text !~ /{{#(if|for|set|awk)/) {
        return content_text
    }
    
    # Write content to temporary file
    # Use printf to ensure proper handling of multi-line content
    temp_file = "/tmp/nested_content_" NR "_" rand() ".tpl"
    # Split content by newlines and write line by line to preserve formatting
    n_content_lines = split(content_text, content_lines, "\n")
    for (i = 1; i <= n_content_lines; i++) {
        if (content_lines[i] != "") {
            printf "%s\n", content_lines[i] > temp_file
        } else {
            print "" > temp_file
        }
    }
    close(temp_file)
    
    # Parse constructs
    scripts_dir = ENVIRON["SCRIPTS_DIR"]
    if (scripts_dir == "") scripts_dir = "."
    
    # Build environment variables string for child processes
    env_vars = ""
    for (var_name in ENVIRON) {
        var_value = ENVIRON[var_name]
        # Escape special characters for shell
        gsub(/"/, "\\\"", var_value)
        gsub(/`/, "\\`", var_value)
        gsub(/\$/, "\\$", var_value)
        gsub(/\|/, "\\|", var_value)
        env_vars = env_vars " " var_name "=\"" var_value "\""
    }
    
    constructs_file = "/tmp/nested_constructs_" NR "_" rand() ".txt"
    if (env_vars != "") {
        cmd = "env" env_vars " awk -f \"" scripts_dir "/parse_template_constructs.awk\" \"" temp_file "\" > \"" constructs_file "\" 2>/dev/null"
    } else {
        cmd = "awk -f \"" scripts_dir "/parse_template_constructs.awk\" \"" temp_file "\" > \"" constructs_file "\" 2>/dev/null"
    }
    system(cmd)
    
    # Process constructs
    processed_file = "/tmp/nested_processed_" NR "_" rand() ".txt"
    if (env_vars != "") {
        cmd = "env" env_vars " awk -f \"" scripts_dir "/process_template_constructs.awk\" \"" constructs_file "\" > \"" processed_file "\" 2>/dev/null"
    } else {
        cmd = "awk -f \"" scripts_dir "/process_template_constructs.awk\" \"" constructs_file "\" > \"" processed_file "\" 2>/dev/null"
    }
    system(cmd)
    
    # Read processed content
    result = ""
    while ((getline line < processed_file) > 0) {
        if (result != "") {
            result = result "\n" line
        } else {
            result = line
        }
    }
    close(processed_file)
    
    # Cleanup
    system("rm -f " temp_file " " constructs_file " " processed_file)
    
    return result
}

# Function to process AWK sections in content
function process_awk_sections_in_content(content_text,    lines, n_lines, i, j, in_awk, awk_code, script_file, cmd, line, env_vars, scripts_dir, has_include, include_file, common_lib_file, preprocessed_script, lib_exists, lib_read_error) {
    # Split content into lines for easier processing
    n_lines = split(content_text, lines, "\n")
    in_awk = 0
    awk_code = ""
    output_before_awk = ""
    
    for (i = 1; i <= n_lines; i++) {
        line = lines[i]
        
        # Check for start of AWK section
        if (line ~ /{{AWK:/) {
            # Output accumulated content before AWK
            if (output_before_awk != "") {
                print output_before_awk
                output_before_awk = ""
            }
            
            in_awk = 1
            # Extract code after {{AWK:
            if (match(line, /{{AWK:(.*)/)) {
                awk_code = substr(line, RSTART + 6)
            } else {
                awk_code = ""
            }
            
            # Check if AWK section ends on same line
            if (line ~ /}}$/) {
                # Remove }} from end
                sub(/}}$/, "", awk_code)
                # Execute AWK code
                execute_awk_section(awk_code)
                awk_code = ""
                in_awk = 0
            }
            continue
        }
        
        # Check for end of AWK section
        if (in_awk) {
            # Check if line contains closing }}
            if (line ~ /^}}$/) {
                # Line is exactly }}
                # Execute AWK code
                execute_awk_section(awk_code)
                awk_code = ""
                in_awk = 0
                # Don't output this line - it's just the closing marker
                continue
            } else if (line ~ /}}$/) {
                # Line ends with }}
                # Extract part before }}
                if (match(line, /^(.*)}}$/)) {
                    before_close = substr(line, RSTART, RLENGTH - 2)
                    if (before_close != "") {
                        awk_code = awk_code "\n" before_close
                    }
                }
                # Execute AWK code
                execute_awk_section(awk_code)
                awk_code = ""
                in_awk = 0
                # Don't output this line - it's just the closing marker
                continue
            } else {
                # Accumulate AWK code
                if (awk_code != "") {
                    awk_code = awk_code "\n" line
                } else {
                    awk_code = line
                }
            }
            continue
        }
        
        # Regular content line
        # Skip template construct markers ({{/if}}, {{/for}}, etc.) - they're handled by replace_template_constructs.awk
        if (line ~ /^{{(\/if|\/for|\/set)}}/) {
            # Skip template construct closing markers
            continue
        }
        if (output_before_awk != "") {
            output_before_awk = output_before_awk "\n" line
        } else {
            output_before_awk = line
        }
    }
    
    # Output remaining content
    if (output_before_awk != "") {
        print output_before_awk
    }
}

# Function to execute AWK section
function execute_awk_section(awk_code,    script_file, env_vars, scripts_dir, has_include, include_file, common_lib_file, preprocessed_script, lib_exists, lib_read_error, cmd, line, entries_data, norm_data, n_lines, code_lines, i) {
    # Execute AWK code with proper environment variable handling
    script_file = "/tmp/template_construct_awk_" NR "_" rand() ".awk"
    # Write AWK code line by line - split by newlines but preserve literal "\n" in strings
    # The issue is that when awk_code contains a line like: split_string(entries_data, entries, "\n")
    # and we write it with print, AWK interprets the \n in the string as a newline
    # Solution: write line by line, and AWK print will preserve the literal "\n" in the string
    # Use printf with %s to ensure proper line handling
    n_lines = split(awk_code, code_lines, "\n")
    for (i = 1; i <= n_lines; i++) {
        # Each code_lines[i] is a complete line - use printf to ensure proper formatting
        # This prevents issues with special characters and ensures lines are written correctly
        if (code_lines[i] != "") {
            printf "%s\n", code_lines[i] > script_file
        } else {
            # Empty line - write as-is
            print "" > script_file
        }
    }
    close(script_file)
    
    # Build environment variables string - pass ALL variables from ENVIRON
    # This includes variables from loops (e.g., "entry" from {{#for entry in ARRAY}})
    env_vars = ""
    for (var_name in ENVIRON) {
        var_value = ENVIRON[var_name]
        # Escape special characters for shell: quotes, dollar signs, backticks, pipes
        gsub(/"/, "\\\"", var_value)
        gsub(/`/, "\\`", var_value)
        gsub(/\$/, "\\$", var_value)
        gsub(/\|/, "\\|", var_value)
        # Escape the variable name itself (though it should be safe)
        var_name_escaped = var_name
        gsub(/"/, "\\\"", var_name_escaped)
        gsub(/`/, "\\`", var_name_escaped)
        gsub(/\$/, "\\$", var_name_escaped)
        env_vars = env_vars " " var_name_escaped "=\"" var_value "\""
    }
    
    # Check if script uses @include - need to preprocess
    scripts_dir = ENVIRON["SCRIPTS_DIR"]
    if (scripts_dir == "") scripts_dir = "."
    
    has_include = 0
    include_file = ""
    script_lines_count = 0
    delete script_lines
    
    # Read script file line by line - getline preserves the line content including literal "\n" in strings
    while ((getline line < script_file) > 0) {
        script_lines_count++
        script_lines[script_lines_count] = line
        
        if (!has_include && line ~ /^@include[ \t]+"/) {
            has_include = 1
            if (match(line, /"([^"]+)"/)) {
                include_file = substr(line, RSTART + 1, RLENGTH - 2)
            }
        }
    }
    close(script_file)
    
    if (has_include && include_file != "") {
        # Preprocess: create new script file with included library
        common_lib_file = scripts_dir "/" include_file
        preprocessed_script = "/tmp/awk_preprocessed_construct_" NR "_" rand() ".awk"
        # DEBUG: Also save to a known location for debugging
        system("echo 'DEBUG: Saving preprocessed script to /tmp/debug_preprocessed.awk' > /dev/stderr")
        debug_script = "/tmp/debug_preprocessed.awk"
        
        # Check if common library file exists and copy it
        lib_exists = 0
        lib_read_error = 0
        while ((getline line < common_lib_file) > 0) {
            lib_exists = 1
            print line > preprocessed_script
            print line > debug_script
        }
        if ((getline < common_lib_file) < 0 && lib_exists == 0) {
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
        print "# Main script (from template construct)" > preprocessed_script
        print "# ============================================================================" > preprocessed_script
        print "" > preprocessed_script
        print "" > debug_script
        print "# ============================================================================" > debug_script
        print "# Main script (from template construct)" > debug_script
        print "# ============================================================================" > debug_script
        print "" > debug_script
        
        # Copy original script lines (skip @include line)
        for (i = 1; i <= script_lines_count; i++) {
            if (!(script_lines[i] ~ /^@include/)) {
                # Write line as-is
                printf "%s\n", script_lines[i] > preprocessed_script
                printf "%s\n", script_lines[i] > debug_script
            }
        }
        close(preprocessed_script)
        close(debug_script)
        
        # Use preprocessed script
        if (env_vars != "") {
            # Escape quotes and special characters in preprocessed_script for shell command
            gsub(/"/, "\\\"", preprocessed_script)
            # env_vars already contains properly escaped values, just use them directly
            cmd = "env" env_vars " awk -f \"" preprocessed_script "\" 2>&1"
        } else {
            # Escape quotes in preprocessed_script for shell command
            gsub(/"/, "\\\"", preprocessed_script)
            cmd = "awk -f \"" preprocessed_script "\" 2>&1"
        }
        
        while ((cmd | getline line) > 0) {
            print line
        }
        close(cmd)
        
        system("rm -f " preprocessed_script)
    } else {
        # Regular awk (no @include)
        if (env_vars != "") {
            # Escape quotes in script_file for shell command
            gsub(/"/, "\\\"", script_file)
            # env_vars already contains properly escaped values, just use them directly
            cmd = "env" env_vars " awk -f \"" script_file "\" 2>&1"
        } else {
            # Escape quotes in script_file for shell command
            gsub(/"/, "\\\"", script_file)
            cmd = "awk -f \"" script_file "\" 2>&1"
        }
        
        while ((cmd | getline line) > 0) {
            print line
        }
        close(cmd)
    }
    
    system("rm -f " script_file)
}

BEGIN {
    # Initialize variable storage
    delete vars
    delete arrays
    delete array_items
    
    # Read all constructs first
    construct_count = 0
    while ((getline line) > 0) {
        construct_count++
        split(line, parts, "|")
        if (length(parts) >= 5) {
            constructs[construct_count, "type"] = parts[1]
            constructs[construct_count, "start"] = parts[2]
            constructs[construct_count, "end"] = parts[3]
            constructs[construct_count, "condition"] = parts[4]
            constructs[construct_count, "content"] = parts[5]
            # Handle multi-line content (if there are more parts)
            # Content is pipe-separated, need to join with newlines
            if (length(parts) > 5) {
                for (i = 6; i <= length(parts); i++) {
                    constructs[construct_count, "content"] = constructs[construct_count, "content"] "\n" parts[i]
                }
            }
            # Restore newlines from special marker (\001 -> \n)
            # Use gsub with proper escaping to ensure all markers are replaced
            gsub(/\001/, "\n", constructs[construct_count, "content"])
            # Ensure content is properly formatted - no broken lines
            # This fixes issues where || operators might be split across lines
        }
    }
    
    # Process constructs
    for (i = 1; i <= construct_count; i++) {
        type = constructs[i, "type"]
        condition = constructs[i, "condition"]
        content = constructs[i, "content"]
        
        if (type == "set") {
            # {{#set VAR=value}} or {{#set VAR={{VAR2|function|args}}}}
            if (match(condition, /^([^=]+)=(.*)$/)) {
                var_name = substr(condition, RSTART, RLENGTH)
                split(var_name, parts, "=")
                var_name = parts[1]
                gsub(/^[ \t]+|[ \t]+$/, "", var_name)
                var_value_expr = substr(condition, RSTART + length(parts[1]) + 1)
                gsub(/^[ \t]+|[ \t]+$/, "", var_value_expr)
                
                # Check if value expression contains function call
                var_value = ""
                if (match(var_value_expr, /^{{([^}]+)}}$/)) {
                    # Extract inner expression
                    inner_expr = substr(var_value_expr, 3, length(var_value_expr) - 4)
                    gsub(/^[ \t]+|[ \t]+$/, "", inner_expr)
                    
                    # Check if it's a function call
                    if (match(inner_expr, /^([^|]+)\|([^|]+)(\|(.+))?$/)) {
                        split(inner_expr, func_parts, "|")
                        source_var = func_parts[1]
                        func_name = func_parts[2]
                        func_arg = ""
                        if (length(func_parts) > 2) {
                            func_arg = func_parts[3]
                        }
                        
                        # Get source variable value
                        source_value = ""
                        if (source_var in vars) {
                            source_value = vars[source_var]
                        } else if (source_var in ENVIRON) {
                            source_value = ENVIRON[source_var]
                        }
                        
                        # Apply function
                        if (func_name == "split") {
                            delimiter = func_arg
                            if (delimiter == "pipe" || delimiter == "|") {
                                delimiter = "|"
                            } else if (delimiter == "newline" || delimiter == "\n") {
                                delimiter = "\n"
                            }
                            n = split(source_value, parts_array, delimiter)
                            for (p = 1; p <= n; p++) {
                                vars[source_var "_parts_" (p-1)] = parts_array[p]
                            }
                            vars[source_var "_parts_count"] = n
                            var_value = source_value
                        } else if (func_name == "part") {
                            index_str = func_arg
                            gsub(/^[ \t]+|[ \t]+$/, "", index_str)
                            index_num = int(index_str)
                            part_var = source_var "_parts_" index_num
                            if (part_var in vars) {
                                var_value = vars[part_var]
                            } else if (part_var in ENVIRON) {
                                var_value = ENVIRON[part_var]
                            }
                        } else if (func_name == "contains") {
                            substring = func_arg
                            if (index(source_value, substring) > 0) {
                                var_value = "1"
                            } else {
                                var_value = "0"
                            }
                        } else if (func_name == "ne") {
                            compare_value = func_arg
                            if (source_value != compare_value) {
                                var_value = "1"
                            } else {
                                var_value = "0"
                            }
                        } else {
                            var_value = source_value
                        }
                    } else {
                        # Simple variable reference
                        if (inner_expr in vars) {
                            var_value = vars[inner_expr]
                        } else if (inner_expr in ENVIRON) {
                            var_value = ENVIRON[inner_expr]
                        }
                    }
                } else {
                    # Simple literal value
                    var_value = var_value_expr
                }
                
                if (var_value != "") {
                vars[var_name] = var_value
                }
            }
        } else if (type == "if") {
            # {{#if VAR}} or {{#if VAR|function|args}}...{{/if}}
            condition_expr = condition
            gsub(/^[ \t]+|[ \t]+$/, "", condition_expr)
            
            # Check if condition contains function call (e.g., VAR|contains|*)
            condition_value = ""
            if (match(condition_expr, /\|/)) {
                # Condition has function - evaluate it
                split(condition_expr, func_parts, "|")
                cond_var = func_parts[1]
                gsub(/^[ \t]+|[ \t]+$/, "", cond_var)
                func_name = func_parts[2]
                gsub(/^[ \t]+|[ \t]+$/, "", func_name)
                func_arg = ""
                if (length(func_parts) > 2) {
                    func_arg = func_parts[3]
                    gsub(/^[ \t]+|[ \t]+$/, "", func_arg)
                }
                
                # Get variable value
                cond_var_value = ""
                if (cond_var in vars) {
                    cond_var_value = vars[cond_var]
                } else if (cond_var in ENVIRON) {
                    cond_var_value = ENVIRON[cond_var]
                }
                
                # Apply function
                if (func_name == "contains") {
                    if (index(cond_var_value, func_arg) > 0) {
                        condition_value = "1"
                    } else {
                        condition_value = "0"
                    }
                } else if (func_name == "ne") {
                    if (cond_var_value != func_arg) {
                        condition_value = "1"
                    } else {
                        condition_value = "0"
                    }
                } else if (func_name == "in_list") {
                    list_var_name = func_arg
                    gsub(/^[ \t]+|[ \t]+$/, "", list_var_name)
                    list_value = ""
                    if (list_var_name in vars) {
                        list_value = vars[list_var_name]
                    } else if (list_var_name in ENVIRON) {
                        list_value = ENVIRON[list_var_name]
                    }
                    
                    found = 0
                    if (list_value != "" && cond_var_value != "") {
                        n = split(list_value, items, /\n|\|/)
                        for (i = 1; i <= n; i++) {
                            gsub(/^[ \t]+|[ \t]+$/, "", items[i])
                            if (items[i] == cond_var_value) {
                                found = 1
                                break
                            }
                        }
                    }
                    condition_value = found ? "1" : "0"
                } else {
                    condition_value = cond_var_value
                }
            } else {
                # Simple variable condition
                var_name = condition_expr
            if (var_name in vars) {
                    condition_value = vars[var_name]
            } else if (var_name in ENVIRON) {
                    condition_value = ENVIRON[var_name]
                }
            }
            
            # Check if variable exists and is non-empty
            if (condition_value != "" && condition_value != "0" && condition_value != "false" && condition_value != "no") {
                # True condition - output content
                # First, process nested constructs recursively
                processed_content = process_nested_constructs(content)
                # Then check if content contains AWK sections that need to be processed
                if (processed_content ~ /{{AWK:/) {
                    # Process AWK sections in content
                    process_awk_sections_in_content(processed_content)
                } else {
                    print processed_content
                }
            } else {
                # False condition - check for else
                if (i < construct_count && constructs[i+1, "type"] == "else") {
                    else_content = constructs[i+1, "content"]
                    # Process nested constructs in else content
                    processed_else = process_nested_constructs(else_content)
                    if (processed_else ~ /{{AWK:/) {
                        process_awk_sections_in_content(processed_else)
                    } else {
                        print processed_else
                    }
                    i++  # Skip else construct
                }
            }
        } else if (type == "for") {
            # {{#for item in ARRAY}}...{{/for}}
            # Parse: item in ARRAY
            if (match(condition, /^([^ \t]+)[ \t]+in[ \t]+(.+)$/)) {
                item_var = substr(condition, RSTART, RLENGTH)
                split(item_var, parts, /[ \t]+in[ \t]+/)
                item_name = parts[1]
                gsub(/^[ \t]+|[ \t]+$/, "", item_name)
                array_name = parts[2]
                gsub(/^[ \t]+|[ \t]+$/, "", array_name)
                
                # Check if array exists in ENVIRON (passed from shell)
                if (array_name in ENVIRON) {
                    array_data = ENVIRON[array_name]
                    # Parse array data (pipe-separated or newline-separated)
                    n = split(array_data, items, /\n|\|/)
                    for (j = 1; j <= n; j++) {
                        if (items[j] != "") {
                            # Set item variable
                            vars[item_name] = items[j]
                            # Process content with item variable
                            processed_content = content
                            # Replace {{item_name}} in content
                            gsub("{{" item_name "}}", items[j], processed_content)
                            
                            # Pass item variable to vars and ENVIRON before processing
                            # Save current values if exist
                            old_entry_var = ""
                            old_entry_env = ""
                            if (item_name in vars) {
                                old_entry_var = vars[item_name]
                            }
                                if (item_name in ENVIRON) {
                                old_entry_env = ENVIRON[item_name]
                                }
                            vars[item_name] = items[j]
                                ENVIRON[item_name] = items[j]
                            
                            # First, process nested constructs recursively (this will handle AWK sections inside)
                            nested_processed = process_nested_constructs(processed_content)
                            
                            # Replace variables in processed content (including function calls)
                            # This handles {{VAR}} and {{VAR|function|args}} patterns
                            final_content = nested_processed
                            while (match(final_content, /{{[^#\/][^}]*}}/)) {
                                var_expr = substr(final_content, RSTART + 2, RLENGTH - 4)
                                gsub(/^[ \t]+|[ \t]+$/, "", var_expr)
                                
                                var_value = ""
                                # Check if it's a function call
                                if (match(var_expr, /\|/)) {
                                    split(var_expr, func_parts, "|")
                                    var_name_func = func_parts[1]
                                    gsub(/^[ \t]+|[ \t]+$/, "", var_name_func)
                                    func_name = func_parts[2]
                                    gsub(/^[ \t]+|[ \t]+$/, "", func_name)
                                    func_arg = ""
                                    if (length(func_parts) > 2) {
                                        func_arg = func_parts[3]
                                        gsub(/^[ \t]+|[ \t]+$/, "", func_arg)
                                    }
                                    
                                    # Get variable value
                                    source_value = ""
                                    if (var_name_func in vars) {
                                        source_value = vars[var_name_func]
                                    } else if (var_name_func in ENVIRON) {
                                        source_value = ENVIRON[var_name_func]
                                    }
                                    
                                    # Apply function
                                    if (func_name == "split") {
                                        delimiter = func_arg
                                        if (delimiter == "pipe" || delimiter == "|") {
                                            delimiter = "|"
                                        } else if (delimiter == "newline" || delimiter == "\n") {
                                            delimiter = "\n"
                                        }
                                        n = split(source_value, parts_array, delimiter)
                                        for (p = 1; p <= n; p++) {
                                            vars[var_name_func "_parts_" (p-1)] = parts_array[p]
                                            ENVIRON[var_name_func "_parts_" (p-1)] = parts_array[p]
                                        }
                                        vars[var_name_func "_parts_count"] = n
                                        var_value = source_value
                                    } else if (func_name == "part") {
                                        index_str = func_arg
                                        gsub(/^[ \t]+|[ \t]+$/, "", index_str)
                                        index_num = int(index_str)
                                        part_var = var_name_func "_parts_" index_num
                                        if (part_var in vars) {
                                            var_value = vars[part_var]
                                        } else if (part_var in ENVIRON) {
                                            var_value = ENVIRON[part_var]
                                        }
                                    } else if (func_name == "contains") {
                                        if (index(source_value, func_arg) > 0) {
                                            var_value = "1"
                                        } else {
                                            var_value = "0"
                                        }
                                    } else if (func_name == "ne") {
                                        if (source_value != func_arg) {
                                            var_value = "1"
                                        } else {
                                            var_value = "0"
                                        }
                                    } else if (func_name == "in_list") {
                                        list_var_name = func_arg
                                        gsub(/^[ \t]+|[ \t]+$/, "", list_var_name)
                                        list_value = ""
                                        if (list_var_name in vars) {
                                            list_value = vars[list_var_name]
                                        } else if (list_var_name in ENVIRON) {
                                            list_value = ENVIRON[list_var_name]
                                        }
                                        
                                        found = 0
                                        if (list_value != "" && source_value != "") {
                                            n = split(list_value, items, /\n|\|/)
                                            for (i = 1; i <= n; i++) {
                                                gsub(/^[ \t]+|[ \t]+$/, "", items[i])
                                                if (items[i] == source_value) {
                                                    found = 1
                                                    break
                                                }
                                            }
                                        }
                                        var_value = found ? "1" : "0"
                                    } else {
                                        var_value = source_value
                                    }
                                } else {
                                    # Simple variable
                                    if (var_expr in vars) {
                                        var_value = vars[var_expr]
                                    } else if (var_expr in ENVIRON) {
                                        var_value = ENVIRON[var_expr]
                                    }
                                }
                                
                                # Replace marker
                                if (var_value != "") {
                                    before = substr(final_content, 1, RSTART - 1)
                                    after = substr(final_content, RSTART + RLENGTH)
                                    final_content = before var_value after
                                } else {
                                    before = substr(final_content, 1, RSTART - 1)
                                    after = substr(final_content, RSTART + RLENGTH)
                                    final_content = before after
                                }
                            }
                            
                            # Then check if content still contains AWK sections (after nested processing)
                            if (final_content ~ /{{AWK:/) {
                                # Process AWK sections in content
                                process_awk_sections_in_content(final_content)
                            } else {
                                print final_content
                            }
                            
                            # Restore ENVIRON and vars
                            if (old_entry_var != "") {
                                vars[item_name] = old_entry_var
                            } else {
                                delete vars[item_name]
                            }
                            if (old_entry_env != "") {
                                ENVIRON[item_name] = old_entry_env
                            } else {
                                delete ENVIRON[item_name]
                            }
                        }
                    }
                } else if (array_name in arrays) {
                    # Array stored in arrays[] (from previous processing)
                    n = arrays[array_name, "count"]
                    for (j = 1; j <= n; j++) {
                        item_value = arrays[array_name, j]
                        vars[item_name] = item_value
                        processed_content = content
                        gsub("{{" item_name "}}", item_value, processed_content)
                        print processed_content
                    }
                }
            }
        } else if (type == "var") {
            # {{VAR}} or {{VAR|function|args}} - output variable with optional function
            var_expr = condition
            gsub(/^[ \t]+|[ \t]+$/, "", var_expr)
            
            # Check if variable has function call: VAR|function|args
            if (match(var_expr, /^([^|]+)\|([^|]+)(\|(.+))?$/)) {
                var_name = substr(var_expr, RSTART, RLENGTH)
                split(var_expr, func_parts, "|")
                var_name = func_parts[1]
                func_name = func_parts[2]
                func_arg = ""
                if (length(func_parts) > 2) {
                    func_arg = func_parts[3]
                }
                
                # Get variable value
                var_value = ""
                if (var_name in vars) {
                    var_value = vars[var_name]
                } else if (var_name in ENVIRON) {
                    var_value = ENVIRON[var_name]
                }
                
                # Apply function
                result = ""
                if (func_name == "split") {
                    # Split variable by delimiter (pipe, newline, etc.)
                    delimiter = func_arg
                    if (delimiter == "pipe" || delimiter == "|") {
                        delimiter = "|"
                    } else if (delimiter == "newline" || delimiter == "\n") {
                        delimiter = "\n"
                    }
                    n = split(var_value, parts_array, delimiter)
                    # Store parts in vars as var_name_parts_0, var_name_parts_1, etc.
                    for (p = 1; p <= n; p++) {
                        vars[var_name "_parts_" (p-1)] = parts_array[p]
                    }
                    vars[var_name "_parts_count"] = n
                    result = var_value  # Return original for display
                } else if (func_name == "part") {
                    # Get part by index: VAR|part|index
                    index_str = func_arg
                    gsub(/^[ \t]+|[ \t]+$/, "", index_str)
                    index_num = int(index_str)
                    part_var = var_name "_parts_" index_num
                    if (part_var in vars) {
                        result = vars[part_var]
                    }
                } else if (func_name == "contains") {
                    # Check if variable contains substring: VAR|contains|substring
                    substring = func_arg
                    if (index(var_value, substring) > 0) {
                        result = "1"
                    } else {
                        result = "0"
                    }
                } else if (func_name == "ne") {
                    # Not equal: VAR|ne|value
                    compare_value = func_arg
                    if (var_value != compare_value) {
                        result = "1"
                    } else {
                        result = "0"
                    }
                } else if (func_name == "in_list") {
                    # Check if variable value is in list: VAR|in_list|LIST_VAR
                    list_var_name = func_arg
                    gsub(/^[ \t]+|[ \t]+$/, "", list_var_name)
                    list_value = ""
                    if (list_var_name in vars) {
                        list_value = vars[list_var_name]
                    } else if (list_var_name in ENVIRON) {
                        list_value = ENVIRON[list_var_name]
                    }
                    
                    # Check if var_value is in list_value (newline or pipe separated)
                    found = 0
                    if (list_value != "" && var_value != "") {
                        n = split(list_value, items, /\n|\|/)
                        for (i = 1; i <= n; i++) {
                            gsub(/^[ \t]+|[ \t]+$/, "", items[i])
                            if (items[i] == var_value) {
                                found = 1
                                break
                            }
                        }
                    }
                    result = found ? "1" : "0"
                } else {
                    # Unknown function - return original value
                    result = var_value
                }
                
                if (result != "") {
                    print result
                }
            } else {
                # Simple variable output
                var_name = var_expr
            if (var_name in vars) {
                print vars[var_name]
            } else if (var_name in ENVIRON) {
                print ENVIRON[var_name]
                }
            }
        } else if (type == "include") {
            # {{#include FILE}} - include file content
            file_path = condition
            gsub(/^[ \t]+|[ \t]+$/, "", file_path)
            
            # Check if file_path is a variable name (not a path)
            # First check vars, then ENVIRON
            if (file_path in vars) {
                file_path = vars[file_path]
            } else if (file_path in ENVIRON) {
                file_path = ENVIRON[file_path]
            }
            
            # Check if file exists and is not empty/placeholder
            if (file_path != "" && file_path != "{{" file_path "}}") {
                # Read and output file content
                file_found = 0
                while ((getline line < file_path) > 0) {
                    print line
                    file_found = 1
                }
                close(file_path)
                # If file was empty or doesn't exist, do nothing (silent fail)
            }
        } else if (type == "awk") {
            # {{#awk:...}} - inline AWK script
            # Execute AWK script
            script = condition
            # Create temp file for script
            script_file = "/tmp/template_awk_" NR "_" rand() ".awk"
            print script > script_file
            close(script_file)
            
            # Execute script
            cmd = "awk -f " script_file " 2>&1"
            while ((cmd | getline line) > 0) {
                print line
            }
            close(cmd)
            system("rm -f " script_file)
        } else if (type == "else") {
            # Else block - already handled in if processing
            # Do nothing here
        } else {
            # Unknown type - output as-is
            print content
        }
    }
}

