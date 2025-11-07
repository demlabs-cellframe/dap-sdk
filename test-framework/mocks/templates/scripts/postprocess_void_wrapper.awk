# Post-process generated void wrapper macro to remove empty line
# Removes empty continuation line after "void *l_args_array[] = args; \"
# This happens when RETURN_DECLARATION, CAST_EXPRESSION, and SEMICOLON are all empty

/void \*l_args_array\[\] = args; \\/ {
    print
    getline
    # If next line is just whitespace and backslash continuation, skip it
    if (/^[[:space:]]*\\$/) {
        next
    }
    # Otherwise print the line we just read
    print
    next
}

# Default action: print all other lines
{
    print
}

