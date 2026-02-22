#!/usr/bin/awk -f
# Scan source files for DAP_MOCK_DECLARE and DAP_MOCK_DECLARE_CUSTOM declarations
# Output: newline-separated list of function names
# POSIX-compatible: no gawk-specific features (no 3-arg match, no \s)

{
    line = $0

    # Find DAP_MOCK_DECLARE(func_name) - but NOT DAP_MOCK_DECLARE_CUSTOM
    while (match(line, "DAP_MOCK_DECLARE[ \t]*\\([ \t]*[a-zA-Z_][a-zA-Z0-9_]*")) {
        matched = substr(line, RSTART, RLENGTH)
        # Skip if this is actually DAP_MOCK_DECLARE_CUSTOM
        if (RSTART > 1 && substr(line, RSTART - 1, 1) == "_") {
            line = substr(line, RSTART + RLENGTH)
            continue
        }
        prefix_check = substr(line, RSTART, 26)
        if (prefix_check ~ /^DAP_MOCK_DECLARE_CUSTOM/) {
            line = substr(line, RSTART + RLENGTH)
            continue
        }
        # Extract func_name: everything after "(" and optional whitespace
        sub(/.*\([ \t]*/, "", matched)
        if (matched != "") {
            print matched
        }
        line = substr(line, RSTART + RLENGTH)
    }

    line = $0

    # Find DAP_MOCK_DECLARE_CUSTOM(func_name, ...)
    while (match(line, "DAP_MOCK_DECLARE_CUSTOM[ \t]*\\([ \t]*[a-zA-Z_][a-zA-Z0-9_]*")) {
        matched = substr(line, RSTART, RLENGTH)
        sub(/.*\([ \t]*/, "", matched)
        if (matched != "") {
            print matched
        }
        line = substr(line, RSTART + RLENGTH)
    }
}
