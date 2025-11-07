# Remove {{AWK:...}} sections from template files
# This is used to clean the template before processing placeholders

BEGIN {
    in_awk_section = 0
}

/{{AWK:/ {
    # Start of AWK section - skip this line
    in_awk_section = 1
    next
}

in_awk_section {
    if (/}}/) {
        # End of AWK section - skip this line too
        in_awk_section = 0
        next
    } else {
        # Skip lines inside AWK section
        next
    }
}

# Default: print line
{
    print
}

