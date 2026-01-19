#!/usr/bin/awk -f
# Scan source files for wrapper definitions
# Output: newline-separated list of function names that have wrappers
# MAWK COMPATIBLE - no capture groups
#
# NOTE: DAP_MOCK_WRAPPER_CUSTOM is a DECLARATION macro, not a full definition.
# We only scan for actual __wrap_ function implementations.
# This allows custom mock headers to be generated even when WRAPPER_CUSTOM macros exist.

{
    # REMOVED: DAP_MOCK_WRAPPER_CUSTOM scanning
    # These are declarations for the generator, not actual wrappers
    # If user defines __wrap_ functions manually, those will be detected below
    
    # REMOVED: DAP_MOCK_WRAPPER_DEFAULT scanning
    # These generate wrappers automatically, so we don't need to generate custom headers
    
    # REMOVED: DAP_MOCK_WRAPPER_DEFAULT_VOID scanning
    
    # REMOVED: DAP_MOCK_WRAPPER_PASSTHROUGH scanning
    
    # REMOVED: DAP_MOCK_WRAPPER_PASSTHROUGH_VOID scanning
    
    # Find explicit __wrap_ definitions ONLY
    # These indicate that the user has manually implemented a wrapper
    # and we should not generate a duplicate
    while (match($0, /__wrap_[a-zA-Z_][a-zA-Z0-9_]*/)) {
        matched = substr($0, RSTART, RLENGTH)
        # Remove __wrap_ prefix
        gsub(/^__wrap_/, "", matched)
        if (matched != "") {
            print matched
        }
        $0 = substr($0, RSTART + RLENGTH)
    }
}

