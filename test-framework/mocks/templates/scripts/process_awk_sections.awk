# Process {{AWK:...}} sections in template files
# Extracts AWK code from {{AWK:...}} sections and executes it on the current file
# The AWK code is executed with the current file as input

BEGIN {
    in_awk_section = 0
    awk_code = ""
    output_lines = ""
}

/{{AWK:/ {
    # Start of AWK section
    in_awk_section = 1
    # Extract everything after {{AWK:
    match($0, /{{AWK:(.*)/, arr)
    if (arr[1] != "") {
        awk_code = arr[1]
    }
    next
}

in_awk_section {
    if (/}}/) {
        # End of AWK section
        # Remove }} from the end
        sub(/}}.*$/, "", $0)
        if ($0 != "") {
            awk_code = awk_code "\n" $0
        }
        
        # Execute the AWK code
        # We need to save current file content, execute AWK, then continue
        # For now, we'll just remove the section - actual execution will be done in shell
        in_awk_section = 0
        awk_code = ""
        next
    } else {
        # Accumulate AWK code
        awk_code = awk_code "\n" $0
        next
    }
}

# Default: print line
{
    print
}

