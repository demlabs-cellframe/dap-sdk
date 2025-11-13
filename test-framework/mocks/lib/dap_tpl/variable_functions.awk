# Mock-specific Variable Functions
# C-specific functions for parsing and generating C code
# This file should be included after core/variable_functions.awk

# Process c_escape function: escapes C string characters
# Escapes: \n → \\n, \t → \\t, " → \", \ → \\
# Usage: {{VAR|c_escape}}
function process_c_escape(base_value, func_arg,    result, i, char) {
    result = ""
    for (i = 1; i <= length(base_value); i++) {
        char = substr(base_value, i, 1)
        if (char == "\\") {
            result = result "\\\\"
        } else if (char == "\n") {
            result = result "\\n"
        } else if (char == "\t") {
            result = result "\\t"
        } else if (char == "\"") {
            result = result "\\\""
        } else {
            result = result char
        }
    }
    return result
}

# Process normalize_name function: normalizes C identifier name (replaces special chars, removes invalid chars)
# func_arg can specify replacement for *: "*|_PTR" or just "" (default: * -> _PTR)
function process_normalize_name(base_value, func_arg,    result, replacement, parts, n) {
    result = base_value
    replacement = "_PTR"
    if (func_arg != "") {
        if (index(func_arg, "|") > 0) {
            n = split(func_arg, parts, "|")
            if (n >= 2) {
                # Format: "pattern|replacement" - use replacement (parts[2])
                replacement = parts[2]
            }
        }
    }
    gsub(/\*/, replacement, result)
    gsub(/[^a-zA-Z0-9_]/, "_", result)
    return result
}

# Process escape_name function: escapes C identifier name (replaces special chars, removes invalid chars)
# func_arg can specify replacement for *: "*|_PTR" or just "" (default: * -> _PTR)
function process_escape_name(base_value, func_arg,    result, replacement, parts, n) {
    result = base_value
    replacement = "_PTR"
    if (func_arg != "") {
        if (index(func_arg, "|") > 0) {
            n = split(func_arg, parts, "|")
            if (n >= 2) {
                # Format: "pattern|replacement" - use replacement (parts[2])
                replacement = parts[2]
            }
        }
    }
    gsub(/\*/, replacement, result)
    gsub(/[^a-zA-Z0-9_]/, "_", result)
    return result
}

# Process escape_char function: escapes specific character to replacement
# func_arg format: "char|replacement" or "char replacement"
function process_escape_char(base_value, func_arg,    result, char, replacement, parts, n) {
    result = base_value
    if (func_arg == "") return result
    
    if (index(func_arg, "|") > 0) {
        n = split(func_arg, parts, "|")
        char = parts[1]
        replacement = (n > 1) ? parts[2] : ""
    } else {
        n = split(func_arg, parts, " ")
        char = parts[1]
        replacement = (n > 1) ? parts[2] : ""
    }
    gsub(char, replacement, result)
    return result
}

# Process sanitize_name function: removes invalid characters from C identifier name
# Keeps only alphanumeric and underscore
function process_sanitize_name(base_value, func_arg,    result) {
    result = base_value
    gsub(/[^a-zA-Z0-9_]/, "_", result)
    return result
}

# Register mock-specific variable functions in dispatch table
BEGIN {
    register_variable_function("c_escape", "process_c_escape", 0)
    register_variable_function("normalize_name", "process_normalize_name", 0)
    register_variable_function("normalize_type", "process_normalize_name", 0)
    register_variable_function("escape_name", "process_escape_name", 0)
    register_variable_function("escape_type", "process_escape_name", 0)
    register_variable_function("escape_char", "process_escape_char", 0)
    register_variable_function("escape_star", "process_escape_char", 0)
    register_variable_function("sanitize_name", "process_sanitize_name", 0)
}

