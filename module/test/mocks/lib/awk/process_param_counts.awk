#!/usr/bin/awk -f
# Process parameter counts and compute MAX_ARGS_COUNT
# Input: space-separated or newline-separated parameter counts
# Output: space-separated PARAM_COUNTS_ARRAY and MAX_ARGS_COUNT

BEGIN {
    max_param_count = 0
    param_counts_str = ""
}

{
    # Process each line - counts can be space-separated or newline-separated
    for (i = 1; i <= NF; i++) {
        count = $i
        gsub(/^[ \t]+|[ \t]+$/, "", count)
        
        if (count == "" || count == " ") {
            continue
        }
        
        # Convert to integer
        count_int = int(count)
        if (count_int < 0) {
            count_int = 0
        }
        
        # Track unique counts
        if (!(count_int in seen_counts)) {
            seen_counts[count_int] = 1
            if (param_counts_str == "") {
                param_counts_str = count_int
            } else {
                param_counts_str = param_counts_str " " count_int
            }
        }
        
        # Track maximum
        if (count_int > max_param_count) {
            max_param_count = count_int
        }
    }
}

END {
    # Ensure we have at least 0
    if (param_counts_str == "") {
        param_counts_str = "0"
    }
    
    # Ensure at least 0 in seen_counts
    if (!(0 in seen_counts)) {
        if (param_counts_str == "0") {
            # Already have 0
        } else {
            param_counts_str = "0 " param_counts_str
        }
    }
    
    # Max args = max params * 2 (each PARAM expands to 2 args: type, name)
    # Add some safety margin (1 extra param = 2 extra args) for edge cases
    # Ensure at least 2 for empty case (need at least 2 args for _DAP_MOCK_NARGS to work)
    max_args_count = max_param_count * 2 + 2
    if (max_args_count < 2) {
        max_args_count = 2
    }
    
    # Output: PARAM_COUNTS_ARRAY (space-separated) and MAX_ARGS_COUNT (on separate line)
    print param_counts_str
    print max_args_count
}

