#!/usr/bin/awk -f
# Process all mock data: parameter counts, return types, and all types
# Input: three files passed as arguments:
#   ARGV[1] = tmp_param_counts file
#   ARGV[2] = tmp_return_types file  
#   ARGV[3] = tmp_all_types file
# Output: shell-compatible code that sets all environment variables:
#   - PARAM_COUNTS_ARRAY (array)
#   - MAX_ARGS_COUNT (integer)
#   - RETURN_TYPES (string)
#   - RETURN_TYPES_PAIRS (multiline string)
#   - ALL_TYPES_PAIRS (multiline string)
#   - ORIGINAL_TYPES (associative array)
#
# The output can be executed via: eval "$(gawk -f process_mock_data.awk ...)"

# Function to escape shell special characters
function shell_escape(str) {
    # Replace single quotes with '\''
    gsub(/'/, "'\\''", str)
    return str
}

# Function to escape array key (for associative array)
function shell_escape_key(str) {
    # Replace special characters that could break array key syntax
    gsub(/[\[\]"]/, "", str)
    gsub(/'/, "'\\''", str)
    return str
}

BEGIN {
    # Process parameter counts file
    param_counts_file = ARGV[1]
    return_types_file = ARGV[2]
    all_types_file = ARGV[3]
    
    # Remove file arguments from ARGC so awk doesn't try to process them as data
    ARGC = 1
    
    # Process parameter counts
    max_param_count = 0
    param_counts_str = ""
    while ((getline line < param_counts_file) > 0) {
        gsub(/^[ \t]+|[ \t]+$/, "", line)
        if (line == "" || line == " ") {
            continue
        }
        
        count_int = int(line)
        if (count_int < 0) {
            count_int = 0
        }
        
        if (!(count_int in seen_param_counts)) {
            seen_param_counts[count_int] = 1
            if (param_counts_str == "") {
                param_counts_str = count_int
            } else {
                param_counts_str = param_counts_str " " count_int
            }
        }
        
        if (count_int > max_param_count) {
            max_param_count = count_int
        }
    }
    close(param_counts_file)
    
    # Ensure we have at least 0
    if (param_counts_str == "") {
        param_counts_str = "0"
    }
    if (!(0 in seen_param_counts)) {
        param_counts_str = "0 " param_counts_str
    }
    
    # Max args = max params * 2 (each PARAM expands to 2 args: type, name)
    # Add some safety margin (1 extra param = 2 extra args) for edge cases
    # Ensure at least 2 for empty case (need at least 2 args for _DAP_MOCK_NARGS to work)
    max_args_count = max_param_count * 2 + 2
    if (max_args_count < 2) {
        max_args_count = 2
    }
    
    # Process return types file
    return_types_str = ""
    return_types_pairs_str = ""
    while ((getline line < return_types_file) > 0) {
        if (line == "" || line ~ /^[ \t]*$/) {
            continue
        }
        
        # Split by pipe separator
        split(line, parts, "|")
        if (length(parts) < 2) {
            continue
        }
        
        normalized = parts[1]
        original = parts[2]
        
        # Remove leading/trailing whitespace
        gsub(/^[ \t]+|[ \t]+$/, "", normalized)
        gsub(/^[ \t]+|[ \t]+$/, "", original)
        
        if (normalized == "" || original == "") {
            continue
        }
        
        # Store unique normalized types for RETURN_TYPES (sorted for consistency)
        if (!(normalized in seen_return_types)) {
            seen_return_types[normalized] = 1
            return_types_array[++return_types_count] = normalized
        }
        
        # Store unique pairs for RETURN_TYPES_PAIRS (sorted for consistency)
        pair_key = normalized "|" original
        if (!(pair_key in seen_return_pairs)) {
            seen_return_pairs[pair_key] = 1
            return_pairs_array[++return_pairs_count] = pair_key
        }
    }
    close(return_types_file)
    
    # Sort and join return types
    asort(return_types_array)
    for (i = 1; i <= return_types_count; i++) {
        if (return_types_str == "") {
            return_types_str = return_types_array[i]
        } else {
            return_types_str = return_types_str " " return_types_array[i]
        }
    }
    
    # Sort and join return pairs
    asort(return_pairs_array)
    for (i = 1; i <= return_pairs_count; i++) {
        if (return_types_pairs_str == "") {
            return_types_pairs_str = return_pairs_array[i]
        } else {
            return_types_pairs_str = return_types_pairs_str "\n" return_pairs_array[i]
        }
    }
    
    # Process all types file
    all_types_pairs_str = ""
    all_pairs_count = 0
    while ((getline line < all_types_file) > 0) {
        if (line == "" || line ~ /^[ \t]*$/) {
            continue
        }
        
        # Split by pipe separator
        split(line, parts, "|")
        if (length(parts) < 2) {
            continue
        }
        
        normalized = parts[1]
        original = parts[2]
        
        # Remove leading/trailing whitespace
        gsub(/^[ \t]+|[ \t]+$/, "", normalized)
        gsub(/^[ \t]+|[ \t]+$/, "", original)
        
        if (normalized == "" || original == "") {
            continue
        }
        
        # Store unique pairs for ALL_TYPES_PAIRS (sorted for consistency)
        pair_key = normalized "|" original
        if (!(pair_key in seen_all_pairs)) {
            seen_all_pairs[pair_key] = 1
            all_pairs_array[++all_pairs_count] = pair_key
        }
    }
    close(all_types_file)
    
    # Sort and join all pairs
    asort(all_pairs_array)
    for (i = 1; i <= all_pairs_count; i++) {
        if (all_types_pairs_str == "") {
            all_types_pairs_str = all_pairs_array[i]
        } else {
            all_types_pairs_str = all_types_pairs_str "\n" all_pairs_array[i]
        }
    }
    
    # Output shell-compatible code to set all variables
    # Set PARAM_COUNTS_ARRAY
    if (param_counts_str == "") {
        print "declare -ga PARAM_COUNTS_ARRAY=(0)"
    } else {
        # Split and output as array
        split(param_counts_str, param_array, " ")
        printf "declare -ga PARAM_COUNTS_ARRAY=("
        for (i = 1; i <= length(param_array); i++) {
            if (i > 1) printf " "
            printf "%s", param_array[i]
        }
        print ")"
    }
    
    # Set MAX_ARGS_COUNT
    print "declare -gi MAX_ARGS_COUNT=" max_args_count
    
    # Set RETURN_TYPES (trimmed)
    gsub(/^[ \t]+|[ \t]+$/, "", return_types_str)
    printf "RETURN_TYPES='"
    printf "%s", shell_escape(return_types_str)
    print "'"
    
    # Set RETURN_TYPES_PAIRS (multiline string)
    printf "RETURN_TYPES_PAIRS=$'"
    if (return_types_pairs_str != "") {
        # Escape newlines and special characters
        gsub(/\\/, "\\\\", return_types_pairs_str)
        gsub(/'/, "'\\''", return_types_pairs_str)
        printf "%s", return_types_pairs_str
    }
    print "'"
    
    # Set ALL_TYPES_PAIRS (multiline string)
    printf "ALL_TYPES_PAIRS=$'"
    if (all_types_pairs_str != "") {
        # Escape newlines and special characters
        gsub(/\\/, "\\\\", all_types_pairs_str)
        gsub(/'/, "'\\''", all_types_pairs_str)
        printf "%s", all_types_pairs_str
    }
    print "'"
    
    # Set ORIGINAL_TYPES associative array from RETURN_TYPES_PAIRS
    print "declare -gA ORIGINAL_TYPES"
    if (return_types_pairs_str != "") {
        # Split by newlines and process each pair
        split(return_types_pairs_str, pairs_array, "\n")
        for (i = 1; i <= length(pairs_array); i++) {
            if (pairs_array[i] == "") continue
            split(pairs_array[i], parts, "|")
            if (length(parts) >= 2) {
                normalized = parts[1]
                original = parts[2]
                gsub(/^[ \t]+|[ \t]+$/, "", normalized)
                gsub(/^[ \t]+|[ \t]+$/, "", original)
                if (normalized != "" && original != "") {
                    normalized_escaped = shell_escape_key(normalized)
                    original_escaped = shell_escape(original)
                    printf "ORIGINAL_TYPES[\"%s\"]='%s'\n", normalized_escaped, original_escaped
                }
            }
        }
    }
    
    # Export all variables
    print "export PARAM_COUNTS_ARRAY"
    print "export MAX_ARGS_COUNT"
    print "export RETURN_TYPES"
    print "export RETURN_TYPES_PAIRS"
    print "export ALL_TYPES_PAIRS"
    print "export ORIGINAL_TYPES"
}

