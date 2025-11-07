/**
 * Create wrapper for function returning {{RETURN_TYPE}}
 * Automatically forwards to real function if mock not enabled
 */
#define DAP_MOCK_WRAPPER_{{MACRO_SUFFIX}}(func_name, params, args) \
    extern {{RETURN_TYPE}} __real_##func_name params; \
    {{RETURN_TYPE}} __wrap_##func_name params { \
        if (g_mock_##func_name && g_mock_##func_name->enabled) { \
            void *l_args_array[] = args; \
            {{RETURN_DECLARATION}}{{CAST_EXPRESSION}}{{SEMICOLON}}{{VOID_EMPTY_LINE}} \
            dap_mock_record_call(g_mock_##func_name, l_args_array, \
                                sizeof(l_args_array)/sizeof(void*), {{RECORD_CALL_VALUE}}); \
            {{RETURN_STATEMENT}} \
        } \
        {{REAL_FUNCTION_CALL}} \
    }

{{postproc:{{AWK:
# Post-process void wrapper macro to remove empty continuation line
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
}}}}
