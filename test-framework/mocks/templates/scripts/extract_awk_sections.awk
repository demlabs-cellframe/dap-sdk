# Extract {{AWK:...}} sections from template files
# Outputs AWK code sections with their line numbers for processing
# Format: start_line|end_line|awk_code

BEGIN {
    in_awk_section = 0
    awk_code = ""
    start_line = 0
    line_num = 0
}

{
    line_num++
}

/{{AWK:/ {
    # Start of AWK section
    in_awk_section = 1
    start_line = line_num
    # Extract everything after {{AWK:
    if (match($0, /{{AWK:(.*)/)) {
        awk_code = substr($0, RSTART + 6, RLENGTH - 6)
    } else {
        awk_code = ""
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
        
        # Output section info: start_line|end_line|awk_code
        print start_line "|" line_num "|" awk_code
        
        in_awk_section = 0
        awk_code = ""
        start_line = 0
        next
    } else {
        # Accumulate AWK code
        if (awk_code != "") {
            awk_code = awk_code "\n" $0
        } else {
            awk_code = $0
        }
        next
    }
}

