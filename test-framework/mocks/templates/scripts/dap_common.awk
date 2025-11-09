# ============================================================================
# Common AWK Library for DAP Mock Template Processing
# ============================================================================
# This library provides reusable functions for AWK scripts used in template processing
# Include this file using: @include "dap_common.awk" (gawk 4.0+) or -f dap_common.awk
#
# Functions:
#   - parse_pipe_separated(entry, parts, delimiter) - parse pipe-separated data
#   - trim_string(str) - remove leading/trailing whitespace
#   - escape_quotes(str) - escape quotes in string for shell
#   - parse_type_entry(entry, parts) - parse type normalization entry
#   - read_file_lines(filename) - read all lines from file into string
#   - split_string(str, arr, delimiter) - split string into array
#   - join_array(arr, n, delimiter) - join array elements with delimiter
# ============================================================================

# Parse pipe-separated (or other delimiter) entry and extract fields
# Parameters:
#   entry - string to parse (e.g., "key|value|data")
#   parts - output array for parsed parts
#   delimiter - delimiter character (default: "|")
# Returns: number of parts parsed
function parse_pipe_separated(entry, parts, delimiter) {
    if (delimiter == "") delimiter = "|"
    n = split(entry, parts, delimiter)
    return n
}

# Remove leading and trailing whitespace from string
# Parameters:
#   str - string to trim
# Returns: trimmed string
function trim_string(str) {
    gsub(/^[ \t]+|[ \t]+$/, "", str)
    return str
}

# Escape quotes in string for safe use in shell commands
# Parameters:
#   str - string to escape
# Returns: escaped string
function escape_quotes(str) {
    gsub(/"/, "\\\"", str)
    return str
}

# Escape single quotes in string for safe use in shell commands
# Parameters:
#   str - string to escape
# Returns: escaped string
function escape_single_quotes(str) {
    gsub(/'/, "'\\''", str)
    return str
}

# Parse type normalization entry
# Input format: "macro_key|base_type|normalized_key|escaped_base_key|escaped_original_key|escaped_macro_key_for_escape"
# Parameters:
#   entry - pipe-separated entry string
#   parts - output array for parsed parts
# Returns: 1 if successful (6 parts), 0 otherwise
function parse_type_entry(entry, parts) {
    n = parse_pipe_separated(entry, parts, "|")
    if (n >= 6) {
        return 1  # Success
    }
    return 0  # Failure
}

# Read all lines from file into single string (newline-separated)
# Parameters:
#   filename - path to file
# Returns: string with all lines (empty if file doesn't exist or is empty)
function read_file_lines(filename) {
    if (filename == "") return ""
    
    content = ""
    if ((getline < filename) > 0) {
        content = $0
        while ((getline < filename) > 0) {
            content = content "\n" $0
        }
        close(filename)
    }
    return content
}

# Split string into array by delimiter
# Parameters:
#   str - string to split
#   arr - output array
#   delimiter - delimiter (default: "\n")
# Returns: number of elements
function split_string(str, arr, delimiter) {
    if (delimiter == "") delimiter = "\n"
    n = split(str, arr, delimiter)
    return n
}

# Join array elements with delimiter
# Parameters:
#   arr - array to join
#   n - number of elements
#   delimiter - delimiter (default: "\n")
# Returns: joined string
function join_array(arr, n, delimiter) {
    if (delimiter == "") delimiter = "\n"
    if (n == 0) return ""
    
    result = arr[1]
    for (i = 2; i <= n; i++) {
        result = result delimiter arr[i]
    }
    return result
}

# Extract variable name from template marker {{VAR}}
# Parameters:
#   marker - template marker (e.g., "{{VAR}}")
# Returns: variable name (e.g., "VAR")
function extract_var_name(marker) {
    # Remove {{ and }}
    name = marker
    sub(/^{{/, "", name)
    sub(/}}$/, "", name)
    return trim_string(name)
}

# Check if string is empty or whitespace-only
# Parameters:
#   str - string to check
# Returns: 1 if empty/whitespace, 0 otherwise
function is_empty(str) {
    trimmed = trim_string(str)
    return (trimmed == "")
}

# Check if string contains substring
# Parameters:
#   str - string to search
#   search_str - substring to find
# Returns: 1 if found, 0 otherwise
function contains(str, search_str) {
    return (index(str, search_str) > 0)
}

# Check if string starts with prefix
# Parameters:
#   str - string to check
#   prefix - prefix to check
# Returns: 1 if starts with prefix, 0 otherwise
function starts_with(str, prefix) {
    return (substr(str, 1, length(prefix)) == prefix)
}

# Check if string ends with suffix
# Parameters:
#   str - string to check
#   suffix - suffix to check
# Returns: 1 if ends with suffix, 0 otherwise
function ends_with(str, suffix) {
    len = length(str)
    suffix_len = length(suffix)
    if (len < suffix_len) return 0
    return (substr(str, len - suffix_len + 1) == suffix)
}

