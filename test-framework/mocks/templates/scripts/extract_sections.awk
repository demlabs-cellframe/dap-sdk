# Extract sections from template files:
# - {{AWK:...}} - content generation via AWK
# - {{postproc:{{AWK:...}}}} - post-processing via AWK
# - {{#/bin/sh:...}} - content generation via shell script
# Output format: type|start_line|end_line|code
# Types: "awk_gen", "awk_postproc", "sh_gen"

BEGIN {
    in_section = 0
    section_type = ""
    section_code = ""
    start_line = 0
    line_num = 0
    brace_count = 0
}

{
    line_num++
}

# Match {{AWK: or {{postproc:{{AWK: or {{#/bin/sh:
/{{postproc:{{AWK:/ {
    # Start of postproc AWK section
    in_section = 1
    section_type = "awk_postproc"
    start_line = line_num
    brace_count = 2  # {{postproc:{{AWK:
    # Extract everything after {{postproc:{{AWK:
    if (match($0, /{{postproc:{{AWK:(.*)/)) {
        section_code = substr($0, RSTART + 19, RLENGTH - 19)
    } else {
        section_code = ""
    }
    next
}

/{{AWK:/ {
    # Start of AWK generation section (not postproc)
    if (!in_section) {
        in_section = 1
        section_type = "awk_gen"
        start_line = line_num
        brace_count = 1  # {{AWK:
        # Extract everything after {{AWK:
        if (match($0, /{{AWK:(.*)/)) {
            section_code = substr($0, RSTART + 6, RLENGTH - 6)
        } else {
            section_code = ""
        }
        next
    }
}

/{{#\/bin\/sh:/ {
    # Start of shell script generation section
    if (!in_section) {
        in_section = 1
        section_type = "sh_gen"
        start_line = line_num
        brace_count = 1  # {{#/bin/sh:
        # Extract everything after {{#/bin/sh: (if any on same line)
        if (match($0, /{{#\/bin\/sh:(.*)/)) {
            section_code = substr($0, RSTART + 11, RLENGTH - 11)
            # If there's content on the same line, check if it ends with }}
            if (section_code != "" && match(section_code, /}}$/)) {
                # Section ends on same line
                sub(/}}$/, "", section_code)
                brace_count = 0
                # Output section info
                print section_type "|" start_line "|" line_num "|" section_code
                in_section = 0
                section_type = ""
                section_code = ""
                start_line = 0
                brace_count = 0
                next
            }
        } else {
            section_code = ""
        }
        next
    }
}

in_section {
    # Count braces to handle nested sections
    for (i = 1; i <= length($0); i++) {
        char = substr($0, i, 1)
        if (char == "{") {
            brace_count++
        } else if (char == "}") {
            brace_count--
            if (brace_count == 0) {
                # End of section
                # Remove closing }} or }}}} from the end
                if (section_type == "awk_postproc") {
                    sub(/}}}}.*$/, "", $0)
                } else {
                    sub(/}}.*$/, "", $0)
                }
                if ($0 != "") {
                    section_code = section_code "\n" $0
                }
                
                # Output section info: type|start_line|end_line|code
                print section_type "|" start_line "|" line_num "|" section_code
                
                in_section = 0
                section_type = ""
                section_code = ""
                start_line = 0
                brace_count = 0
                next
            }
        }
    }
    
    # Accumulate section code
    if (section_code != "") {
        section_code = section_code "\n" $0
    } else {
        section_code = $0
    }
    next
}

# Default: do nothing (we only extract sections)

