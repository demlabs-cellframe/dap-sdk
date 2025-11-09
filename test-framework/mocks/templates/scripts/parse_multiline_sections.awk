# Parse multi-line sections from extract_sections.awk output
# Input format: type|start_line|end_line|code...
# Output format: type|start_line|end_line|code (with newlines preserved in code)
# Handles cases where section_code spans multiple lines
# Note: Do NOT use -F'|' because code may contain | characters
# extract_sections.awk outputs code on multiple lines after the third |

BEGIN {
    section_type = ""
    start_line = ""
    end_line = ""
    section_code = ""
    in_section = 0
    header_processed = 0
}

/^(awk_gen|sh_gen|awk_postproc)\|/ {
    # Save previous section if any
    if (section_type != "") {
        print section_type "|" start_line "|" end_line "|" section_code
    }
    # Start new section
    # Parse manually to avoid issues with | in code
    # Find positions of first three |
    pos1 = index($0, "|")
    if (pos1 == 0) next
    
    pos2 = index(substr($0, pos1 + 1), "|")
    if (pos2 == 0) next
    pos2 = pos2 + pos1
    
    pos3 = index(substr($0, pos2 + 1), "|")
    if (pos3 == 0) {
        # No third | found - this means code is on the same line
        # Extract fields up to end of line
        section_type = substr($0, 1, pos1 - 1)
        start_line = substr($0, pos1 + 1, pos2 - pos1 - 1)
        end_line = substr($0, pos2 + 1)
        section_code = ""
        in_section = 1
        header_processed = 1
        next
    }
    pos3 = pos3 + pos2
    
    # Extract fields
    section_type = substr($0, 1, pos1 - 1)
    start_line = substr($0, pos1 + 1, pos2 - pos1 - 1)
    end_line = substr($0, pos2 + 1, pos3 - pos2 - 1)
    # Get code part after third |
    code_part = substr($0, pos3 + 1)
    
    # If there's code on the same line, add it
    if (code_part != "") {
        section_code = code_part
    } else {
        section_code = ""
    }
    
    # Code continues on next lines - start accumulating
    # DO NOT output section here - wait for next section header or END
    in_section = 1
    header_processed = 1
    next
}

in_section {
    # Check if this line starts a new section (has | separator pattern)
    # If it matches pattern type|num|num|, it's a new section header
    if (/^(awk_gen|sh_gen|awk_postproc)\|/) {
        # This is a new section - output previous and start new
        print section_type "|" start_line "|" end_line "|" section_code
        # Reset and process new section
        section_type = ""
        start_line = ""
        end_line = ""
        section_code = ""
        header_processed = 0
        # Process new header
        pos1 = index($0, "|")
        if (pos1 != 0) {
            pos2 = index(substr($0, pos1 + 1), "|")
            if (pos2 != 0) {
                pos2 = pos2 + pos1
                pos3 = index(substr($0, pos2 + 1), "|")
                if (pos3 != 0) {
                    pos3 = pos3 + pos2
                    section_type = substr($0, 1, pos1 - 1)
                    start_line = substr($0, pos1 + 1, pos2 - pos1 - 1)
                    end_line = substr($0, pos2 + 1, pos3 - pos2 - 1)
                    section_code = substr($0, pos3 + 1)
                    header_processed = 1
                    next
                }
            }
        }
    }
    
    # Continue accumulating section code (preserve | characters and newlines)
    if (header_processed) {
        # After header, accumulate all lines as code
        if (section_code != "") {
            section_code = section_code "\n" $0
        } else {
            section_code = $0
        }
    }
    next
}

END {
    # Output last section
    if (section_type != "") {
        print section_type "|" start_line "|" end_line "|" section_code
    }
}

