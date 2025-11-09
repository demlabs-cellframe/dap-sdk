# Replace template constructs in template file
# Reads constructs file and processed content, replaces markers in template

BEGIN {
    # Read constructs mapping (start_line -> processed_content)
    while ((getline line < constructs_file) > 0) {
        split(line, parts, "|")
        if (length(parts) >= 5) {
            start_line = parts[2]
            end_line = parts[3]
            processed_content = parts[5]
            # Handle multi-line content
            if (length(parts) > 5) {
                for (i = 6; i <= length(parts); i++) {
                    processed_content = processed_content "|" parts[i]
                }
            }
            constructs[start_line] = processed_content
            construct_end[start_line] = end_line
        }
    }
    close(constructs_file)
    
    # Read processed content
    processed_count = 0
    while ((getline line < processed_file) > 0) {
        processed_count++
        processed_lines[processed_count] = line
    }
    close(processed_file)
    
    # Track which constructs we've output
    processed_idx = 1
    skip_until = 0
}

{
    current_line = NR
    
    # Check if we're in a construct that should be replaced
    replaced = 0
    for (start in constructs) {
        if (current_line >= start && current_line <= construct_end[start]) {
            # This line is part of a construct
            if (current_line == start) {
                # Output processed content instead
                if (processed_idx <= processed_count) {
                    print processed_lines[processed_idx]
                    processed_idx++
                }
                skip_until = construct_end[start]
                replaced = 1
                break
            } else if (current_line <= skip_until) {
                # Skip original construct lines
                replaced = 1
                break
            }
        }
    }
    
    if (!replaced) {
        # Output line as-is, but replace {{VAR}} markers
        output_line = $0
        
        # Replace variable markers {{VAR}} or {{VAR|function|args}}
        while (match(output_line, /{{[^#\/][^}]*}}/)) {
            var_expr = substr(output_line, RSTART + 2, RLENGTH - 4)
            gsub(/^[ \t]+|[ \t]+$/, "", var_expr)
            
            # Check if variable has function call: VAR|function|args
            var_value = ""
            if (match(var_expr, /^([^|]+)\|([^|]+)(\|(.+))?$/)) {
                split(var_expr, func_parts, "|")
                var_name = func_parts[1]
                func_name = func_parts[2]
                func_arg = ""
                if (length(func_parts) > 2) {
                    func_arg = func_parts[3]
                }
                
                # Get variable value from ENVIRON
                if (var_name in ENVIRON) {
                    base_value = ENVIRON[var_name]
                    
                    # Apply function
                    if (func_name == "split") {
                        # Split creates parts but returns original value
                        delimiter = func_arg
                        if (delimiter == "pipe" || delimiter == "|") {
                            delimiter = "|"
                        } else if (delimiter == "newline" || delimiter == "\\n") {
                            delimiter = "\n"
                        }
                        n = split(base_value, parts_array, delimiter)
                        # Store parts in ENVIRON for later access
                        for (p = 1; p <= n; p++) {
                            ENVIRON[var_name "_parts_" (p-1)] = parts_array[p]
                        }
                        ENVIRON[var_name "_parts_count"] = n
                        var_value = base_value
                    } else if (func_name == "part") {
                        # Get part by index
                        index_str = func_arg
                        gsub(/^[ \t]+|[ \t]+$/, "", index_str)
                        index_num = int(index_str)
                        part_var = var_name "_parts_" index_num
                        if (part_var in ENVIRON) {
                            var_value = ENVIRON[part_var]
                        }
                    } else if (func_name == "contains") {
                        # Check if contains substring
                        substring = func_arg
                        if (index(base_value, substring) > 0) {
                            var_value = "1"
                        } else {
                            var_value = "0"
                        }
                    } else if (func_name == "ne") {
                        # Not equal
                        compare_value = func_arg
                        if (base_value != compare_value) {
                            var_value = "1"
                        } else {
                            var_value = "0"
                        }
                    } else if (func_name == "in_list") {
                        # Check if value is in list
                        list_var_name = func_arg
                        gsub(/^[ \t]+|[ \t]+$/, "", list_var_name)
                        list_value = ""
                        if (list_var_name in ENVIRON) {
                            list_value = ENVIRON[list_var_name]
                        }
                        
                        found = 0
                        if (list_value != "" && base_value != "") {
                            n = split(list_value, items, /\n|\|/)
                            for (i = 1; i <= n; i++) {
                                gsub(/^[ \t]+|[ \t]+$/, "", items[i])
                                if (items[i] == base_value) {
                                    found = 1
                                    break
                                }
                            }
                        }
                        var_value = found ? "1" : "0"
                    } else {
                        var_value = base_value
                    }
                }
            } else {
                # Simple variable
                var_name = var_expr
            if (var_name in ENVIRON) {
                var_value = ENVIRON[var_name]
                }
            }
            
                # Replace marker with value
            if (var_value != "") {
                before = substr(output_line, 1, RSTART - 1)
                after = substr(output_line, RSTART + RLENGTH)
                output_line = before var_value after
            } else {
                # Variable not found - remove marker
                before = substr(output_line, 1, RSTART - 1)
                after = substr(output_line, RSTART + RLENGTH)
                output_line = before after
            }
        }
        
        print output_line
    }
}

