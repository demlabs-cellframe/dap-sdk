# Extract PARAM(type, name) entries from parameter list
# Output format: type|name (one per line)
BEGIN {
    param_idx = 0
}
{
    # Find all PARAM(type, name) entries
    line = $0
    pos = 1
    while (match(substr(line, pos), /PARAM\s*\(/)) {
        # Found PARAM(, now extract type and name
        param_start = pos + RSTART + RLENGTH - 1
        paren_level = 1
        type_start = param_start
        type_end = 0
        name_start = 0
        name_end = 0
        
        # Find comma separating type and name
        for (i = param_start; i <= length(line) && paren_level > 0; i++) {
            char = substr(line, i, 1)
            if (char == "(") paren_level++
            if (char == ")") paren_level--
            if (char == "," && paren_level == 1 && type_end == 0) {
                type_end = i - 1
                name_start = i + 1
            }
            if (char == ")" && paren_level == 0 && name_start > 0) {
                name_end = i - 1
                break
            }
        }
        
        if (type_end > 0 && name_end > 0) {
            type = substr(line, type_start, type_end - type_start + 1)
            name = substr(line, name_start, name_end - name_start + 1)
            
            # Clean up type and name
            gsub(/^[ \t\n]+|[ \t\n]+$/, "", type)
            gsub(/^[ \t\n]+|[ \t\n]+$/, "", name)
            
            if (type != "" && name != "") {
                printf "%s|%s\n", type, name
                param_idx++
            }
        }
        
        # Move to next PARAM
        if (name_end > 0) {
            pos = name_end + 2
        } else {
            break
        }
    }
}

