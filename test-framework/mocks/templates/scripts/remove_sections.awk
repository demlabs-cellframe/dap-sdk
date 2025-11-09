# Remove/process sections from template files:
# - {{AWK:...}} - keep marker for content generation
# - {{postproc:{{AWK:...}}}} - remove (post-processing happens later)
# - {{#/bin/sh:...}} - keep marker for content generation
# Also removes AWK code blocks that look like postproc content (without markers)
# This is used to clean the template before processing placeholders

BEGIN {
    in_section = 0
    brace_count = 0
    section_type = ""
    in_postproc_content = 0
    postproc_brace_count = 0
}

# Match start of any section
/{{postproc:{{AWK:/ {
    in_section = 1
    section_type = "postproc"
    brace_count = 2  # {{postproc:{{AWK:
    next
}

# Detect postproc content without markers (AWK code that looks like postproc)
/^# Post-process:/ {
    if (!in_section && !in_postproc_content) {
        in_postproc_content = 1
        postproc_brace_count = 0
        next
    }
}

/{{AWK:/ {
    if (!in_section && !in_postproc_content) {
        # Content generation section - keep marker, remove content
        print $0  # Print the marker line
        in_section = 1
        section_type = "gen"
        brace_count = 1  # {{AWK:
        next
    }
}

/{{#\/bin\/sh:/ {
    if (!in_section && !in_postproc_content) {
        # Content generation section - keep marker, remove content
        print $0  # Print the marker line
        in_section = 1
        section_type = "gen"
        brace_count = 1  # {{#/bin/sh:
        next
    }
}

in_postproc_content {
    # Count braces to find end of AWK code block
    for (i = 1; i <= length($0); i++) {
        char = substr($0, i, 1)
        if (char == "{") {
            postproc_brace_count++
        } else if (char == "}") {
            postproc_brace_count--
            if (postproc_brace_count < 0) {
                # End of AWK code block (closing braces like }}})
                in_postproc_content = 0
                postproc_brace_count = 0
                next
            }
        }
    }
    # Skip lines inside postproc content
    next
}

in_section {
    if (section_type == "postproc") {
        # Post-processing section - skip all lines
        # Count braces to handle nested sections
        for (i = 1; i <= length($0); i++) {
            char = substr($0, i, 1)
            if (char == "{") {
                brace_count++
            } else if (char == "}") {
                brace_count--
                if (brace_count == 0) {
                    # End of section - skip this line too
                    in_section = 0
                    section_type = ""
                    brace_count = 0
                    next
                }
            }
        }
        # Skip lines inside postproc section
        next
    } else {
        # Content generation section - skip content but keep track of braces
        for (i = 1; i <= length($0); i++) {
            char = substr($0, i, 1)
            if (char == "{") {
                brace_count++
            } else if (char == "}") {
                brace_count--
                if (brace_count == 0) {
                    # End of section - skip closing }}
                    in_section = 0
                    section_type = ""
                    brace_count = 0
                    next
                }
            }
        }
        # Skip lines inside generation section
        next
    }
}

# Default: print line
{
    print
}
