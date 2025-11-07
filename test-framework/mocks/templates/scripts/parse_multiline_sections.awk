# Parse multi-line sections from extract_sections.awk output
# Input format: type|start_line|end_line|code...
# Output format: type|start_line|end_line|code (with newlines preserved in code)
# Handles cases where section_code spans multiple lines
# Note: Do NOT use -F'|' because code may contain | characters

BEGIN {
    section_type = ""
    start_line = ""
    end_line = ""
    section_code = ""
    in_section = 0
}

/^(awk_gen|sh_gen)\|/ {
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
    if (pos3 == 0) next
    pos3 = pos3 + pos2
    
    # Extract fields
    section_type = substr($0, 1, pos1 - 1)
    start_line = substr($0, pos1 + 1, pos2 - pos1 - 1)
    end_line = substr($0, pos2 + 1, pos3 - pos2 - 1)
    section_code = substr($0, pos3 + 1)
    
    in_section = 1
    next
}

in_section {
    # Continue accumulating section code (preserve | characters)
    section_code = section_code "\n" $0
    next
}

END {
    # Output last section
    if (section_type != "") {
        print section_type "|" start_line "|" end_line "|" section_code
    }
}

