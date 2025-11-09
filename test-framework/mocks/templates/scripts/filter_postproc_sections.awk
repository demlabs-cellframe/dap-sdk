# Filter post-processing sections (awk_postproc) from extract_sections.awk output
# extract_sections.awk outputs code on MULTIPLE LINES (not in one line)
# First line: awk_postproc|92|111|code...
# Next lines: more code...
# We need to collect all lines until next section header or EOF
# Output format: awk_postproc|start_line|end_line|code (with newlines preserved in code)

/^awk_postproc\|/ {
    if (section_line != "") {
        # Output previous section
        print section_line
    }
    section_line = $0
    found = 1
    next
}

found && /^(awk_gen|sh_gen|awk_postproc)\|/ {
    # New section header - output previous and start new
    print section_line
    section_line = $0
    found = (/^awk_postproc\|/)
    next
}

found {
    # Accumulate code lines (not section headers)
    section_line = section_line "\n" $0
    next
}

END {
    if (found && section_line != "") {
        print section_line
    }
}

