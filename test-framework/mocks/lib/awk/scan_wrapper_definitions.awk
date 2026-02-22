#!/usr/bin/awk -f
# Scan source files for wrapper definitions
# Output: newline-separated list of function names that have wrappers
# POSIX-compatible: no gawk-specific features (no 3-arg match, no \s)

{
    line = $0

    # Find DAP_MOCK_WRAPPER_CUSTOM(return_type, func_name, ...)
    while (match(line, "DAP_MOCK_WRAPPER_CUSTOM[ \t]*\\([^,]*,[ \t]*[a-zA-Z_][a-zA-Z0-9_]*")) {
        matched = substr(line, RSTART, RLENGTH)
        # Extract func_name: everything after the comma and optional whitespace
        sub(/.*,[ \t]*/, "", matched)
        if (matched != "") {
            print matched
        }
        line = substr(line, RSTART + RLENGTH)
    }

    line = $0

    # Find DAP_MOCK_WRAPPER_PASSTHROUGH(return_type, func_name, ...)
    while (match(line, "DAP_MOCK_WRAPPER_PASSTHROUGH[ \t]*\\([^,]*,[ \t]*[a-zA-Z_][a-zA-Z0-9_]*")) {
        matched = substr(line, RSTART, RLENGTH)
        sub(/.*,[ \t]*/, "", matched)
        if (matched != "") {
            print matched
        }
        line = substr(line, RSTART + RLENGTH)
    }

    line = $0

    # Find DAP_MOCK_WRAPPER_PASSTHROUGH_VOID(func_name, ...)
    while (match(line, "DAP_MOCK_WRAPPER_PASSTHROUGH_VOID[ \t]*\\([ \t]*[a-zA-Z_][a-zA-Z0-9_]*")) {
        matched = substr(line, RSTART, RLENGTH)
        sub(/.*\([ \t]*/, "", matched)
        if (matched != "") {
            print matched
        }
        line = substr(line, RSTART + RLENGTH)
    }

    line = $0

    # Find explicit __wrap_ definitions
    while (match(line, "__wrap_[a-zA-Z_][a-zA-Z0-9_]*")) {
        matched = substr(line, RSTART, RLENGTH)
        sub(/^__wrap_/, "", matched)
        if (matched != "") {
            print matched
        }
        line = substr(line, RSTART + RLENGTH)
    }
}
