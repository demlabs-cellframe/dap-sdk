# Generate MAP macros content
# Usage: awk -f generate_map_macros.awk <<< "param_counts"
# Input: newline separated param counts
# Output: MAP macros definitions

{
    count = $1
    if (count == "") next
    
    # Generate _DAP_MOCK_MAP_N macro
    printf "#define _DAP_MOCK_MAP_%d(macro", count
    
    # Generate arguments list
    for (i = 1; i <= count; i++) {
        printf ", type%d, name%d", i, i
    }
    printf ") \\\n    "
    
    if (count == 0) {
        printf "\n"
    } else {
        # Generate macro calls
        for (i = 1; i <= count; i++) {
            if (i > 1) printf ", "
            printf "macro(type%d, name%d)", i, i
        }
        printf "\n"
    }
}

