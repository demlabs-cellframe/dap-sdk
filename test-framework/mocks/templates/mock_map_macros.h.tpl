// Simplified _DAP_MOCK_MAP implementation
// Only handles the specific parameter counts we need
// Note: _DAP_MOCK_NARGS must be defined in dap_mock_linker_wrapper.h
// which is included after this file, so we forward-reference it here
// Each PARAM expands to 2 arguments, so we need to divide arg count by 2
// Use helper macro to compute number of PARAM entries
// Special handling for "void": when __VA_ARGS__ is "void", produce "void" instead of empty
// Note: _DAP_MOCK_NARGS() returns 1 for empty __VA_ARGS__, so we use param_count to distinguish
// Empty: param_count=0, arg_count=1 (but first_arg is empty)
// "void": param_count=0, arg_count=1, first_arg="void"
#define _DAP_MOCK_MAP(macro, ...) \
    _DAP_MOCK_MAP_CHECK_VOID(__VA_ARGS__, _DAP_MOCK_NARGS(__VA_ARGS__), _DAP_MOCK_MAP_COUNT_PARAMS(__VA_ARGS__), macro, __VA_ARGS__)
#define _DAP_MOCK_MAP_CHECK_VOID(first_arg, arg_count, param_count, macro, ...) \
    _DAP_MOCK_MAP_CHECK_VOID_IMPL(first_arg, arg_count, param_count, macro, __VA_ARGS__)
#define _DAP_MOCK_MAP_CHECK_VOID_IMPL(first_arg, arg_count, param_count, macro, ...) \
    _DAP_MOCK_MAP_CHECK_VOID_BY_PARAM_COUNT(param_count, first_arg, macro, __VA_ARGS__)
#define _DAP_MOCK_MAP_CHECK_VOID_BY_PARAM_COUNT(param_count, first_arg, macro, ...) \
    _DAP_MOCK_MAP_CHECK_VOID_BY_PARAM_COUNT_##param_count(first_arg, macro, __VA_ARGS__)
#define _DAP_MOCK_MAP_CHECK_VOID_BY_PARAM_COUNT_0(first_arg, macro, ...) \
    _DAP_MOCK_MAP_CHECK_VOID_BY_PARAM_COUNT_0_CHECK(first_arg, macro, __VA_ARGS__)
#define _DAP_MOCK_MAP_CHECK_VOID_BY_PARAM_COUNT_0_CHECK(first_arg, macro, ...) \
    _DAP_MOCK_MAP_CHECK_VOID_BY_PARAM_COUNT_0_CHECK_EXPAND(first_arg, macro, __VA_ARGS__)
#define _DAP_MOCK_MAP_CHECK_VOID_BY_PARAM_COUNT_0_CHECK_EXPAND(first_arg, macro, ...) \
    _DAP_MOCK_MAP_CHECK_VOID_BY_PARAM_COUNT_0_CHECK_##first_arg(macro, __VA_ARGS__)
#define _DAP_MOCK_MAP_CHECK_VOID_BY_PARAM_COUNT_0_CHECK_void(macro, ...) \
    _DAP_MOCK_MAP_CHECK_VOID_BY_PARAM_COUNT_0_CHECK_void_BY_MACRO_EXPAND(macro, __VA_ARGS__)
#define _DAP_MOCK_MAP_CHECK_VOID_BY_PARAM_COUNT_0_CHECK_void_BY_MACRO_EXPAND(macro, ...) \
    _DAP_MOCK_MAP_CHECK_VOID_BY_PARAM_COUNT_0_CHECK_void_BY_MACRO_##macro(__VA_ARGS__)
#define _DAP_MOCK_MAP_CHECK_VOID_BY_PARAM_COUNT_0_CHECK_void_BY_MACRO__DAP_MOCK_PARAM_DECL(...) void
#define _DAP_MOCK_MAP_CHECK_VOID_BY_PARAM_COUNT_0_CHECK_void_BY_MACRO__DAP_MOCK_PARAM_NAME(...)
#define _DAP_MOCK_MAP_CHECK_VOID_BY_PARAM_COUNT_0_CHECK_void_BY_MACRO__DAP_MOCK_PARAM_CAST(...)
#define _DAP_MOCK_MAP_CHECK_VOID_BY_PARAM_COUNT_0_CHECK_void_BY_MACRO_DEFAULT(...) void
#define _DAP_MOCK_MAP_CHECK_VOID_BY_PARAM_COUNT_0_CHECK_DEFAULT(macro, ...) \
    _DAP_MOCK_MAP_0(macro)
#define _DAP_MOCK_MAP_CHECK_VOID_BY_PARAM_COUNT_DEFAULT(first_arg, macro, ...) \
    _DAP_MOCK_MAP_IMPL(_DAP_MOCK_MAP_COUNT_PARAMS(__VA_ARGS__), macro, __VA_ARGS__)

// Count number of PARAM entries (each PARAM is 2 args)
// Empty: 0 args -> 0 params
// "void": 1 arg "void" -> 0 params (special case)
// 1 param: 2 args -> 1 param
// 3 params: 6 args -> 3 params
#define _DAP_MOCK_MAP_COUNT_PARAMS(...) \
    _DAP_MOCK_MAP_COUNT_PARAMS_CHECK_VOID(__VA_ARGS__, _DAP_MOCK_NARGS(__VA_ARGS__))
#define _DAP_MOCK_MAP_COUNT_PARAMS_CHECK_VOID(first_arg, arg_count, ...) \
    _DAP_MOCK_MAP_COUNT_PARAMS_CHECK_VOID_IMPL(first_arg, arg_count, first_arg, ##__VA_ARGS__)
#define _DAP_MOCK_MAP_COUNT_PARAMS_CHECK_VOID_IMPL(first_arg, arg_count, saved_first_arg, ...) \
    _DAP_MOCK_MAP_COUNT_PARAMS_CHECK_VOID_BY_COUNT(arg_count, first_arg, saved_first_arg, saved_first_arg, ##__VA_ARGS__)
#define _DAP_MOCK_MAP_COUNT_PARAMS_CHECK_VOID_BY_COUNT(arg_count, first_arg, original_first_arg, saved_first_arg, ...) \
    _DAP_MOCK_MAP_COUNT_PARAMS_CHECK_VOID_BY_COUNT_##arg_count(first_arg, original_first_arg, saved_first_arg, ##__VA_ARGS__)
#define _DAP_MOCK_MAP_COUNT_PARAMS_CHECK_VOID_BY_COUNT_0(first_arg, original_first_arg, saved_first_arg, ...) 0
#define _DAP_MOCK_MAP_COUNT_PARAMS_CHECK_VOID_BY_COUNT_1(first_arg, original_first_arg, saved_first_arg, ...) \
    _DAP_MOCK_MAP_COUNT_PARAMS_CHECK_VOID_BY_COUNT_1_CHECK(first_arg, original_first_arg, saved_first_arg, ##__VA_ARGS__)
#define _DAP_MOCK_MAP_COUNT_PARAMS_CHECK_VOID_BY_COUNT_1_CHECK(first_arg, original_first_arg, saved_first_arg, ...) \
    _DAP_MOCK_MAP_COUNT_PARAMS_CHECK_VOID_BY_COUNT_1_CHECK_EXPAND(first_arg, original_first_arg, saved_first_arg, ##__VA_ARGS__)
#define _DAP_MOCK_MAP_COUNT_PARAMS_CHECK_VOID_BY_COUNT_1_CHECK_EXPAND(first_arg, original_first_arg, saved_first_arg, ...) \
    _DAP_MOCK_MAP_COUNT_PARAMS_CHECK_VOID_BY_COUNT_1_CHECK_EXPAND_IMPL(first_arg, original_first_arg, saved_first_arg, ##__VA_ARGS__)
#define _DAP_MOCK_MAP_COUNT_PARAMS_CHECK_VOID_BY_COUNT_1_CHECK_EXPAND_IMPL(first_arg, original_first_arg, saved_first_arg, ...) \
    _DAP_MOCK_MAP_COUNT_PARAMS_CHECK_VOID_BY_COUNT_1_CHECK_##first_arg(saved_first_arg, ##__VA_ARGS__)
#define _DAP_MOCK_MAP_COUNT_PARAMS_CHECK_VOID_BY_COUNT_1_CHECK_void(saved_first_arg, ...) 0
#define _DAP_MOCK_MAP_COUNT_PARAMS_CHECK_VOID_BY_COUNT_1_CHECK_DEFAULT(saved_first_arg, ...) \
    _DAP_MOCK_MAP_COUNT_PARAMS_RECALC(saved_first_arg, ##__VA_ARGS__)
// For arg_count >= 2, use normal logic (arg_count / 2)
// We need to recalculate arg_count from __VA_ARGS__ since we lost it in the chain
// Generate macros for specific arg_count values to avoid DEFAULT fallback issues
{{AWK:
BEGIN {
    max_args_count = int(ENVIRON["MAX_ARGS_COUNT"])
    if (max_args_count < 0) max_args_count = 0
    
    # Generate _DAP_MOCK_MAP_COUNT_PARAMS_CHECK_VOID_BY_COUNT_N macros for arg_count >= 2
    for (i = 2; i <= max_args_count; i++) {
        printf "#define _DAP_MOCK_MAP_COUNT_PARAMS_CHECK_VOID_BY_COUNT_%d(first_arg, original_first_arg, saved_first_arg, ...) \\\n", i
        print "    _DAP_MOCK_MAP_COUNT_PARAMS_RECALC(first_arg, ##__VA_ARGS__)"
    }
}
}}
#define _DAP_MOCK_MAP_COUNT_PARAMS_CHECK_VOID_BY_COUNT_DEFAULT(first_arg, original_first_arg, saved_first_arg, ...) \
    _DAP_MOCK_MAP_COUNT_PARAMS_RECALC(first_arg, ##__VA_ARGS__)
#define _DAP_MOCK_MAP_COUNT_PARAMS_RECALC(...) \
    _DAP_MOCK_MAP_COUNT_PARAMS_EXPAND(_DAP_MOCK_NARGS(__VA_ARGS__))
#define _DAP_MOCK_MAP_COUNT_PARAMS_EXPAND(arg_count) \
    _DAP_MOCK_MAP_COUNT_PARAMS_EXPAND_IMPL(arg_count)
#define _DAP_MOCK_MAP_COUNT_PARAMS_EXPAND_IMPL(arg_count) \
    _DAP_MOCK_MAP_COUNT_PARAMS_HELPER_##arg_count

// Helper to convert arg count to param count
#define _DAP_MOCK_MAP_COUNT_PARAMS_HELPER(N) \
    _DAP_MOCK_MAP_COUNT_PARAMS_HELPER_##N

// Generate mappings dynamically: 0 args -> 0 params, 2 args -> 1 param, 4 args -> 2 params, etc.
// Each PARAM expands to 2 args (type, name), so N args = N/2 params (rounded down)
{{AWK:
BEGIN {
    max_args_count = int(ENVIRON["MAX_ARGS_COUNT"])
    if (max_args_count < 0) max_args_count = 0
    
    # Generate _DAP_MOCK_MAP_COUNT_PARAMS_HELPER macros
    for (i = 0; i <= max_args_count; i++) {
        param_count = int(i / 2)
        printf "#define _DAP_MOCK_MAP_COUNT_PARAMS_HELPER_%d %d\n", i, param_count
    }
}
}}

// Default case for values beyond generated range (should never be reached)
#define _DAP_MOCK_MAP_COUNT_PARAMS_HELPER_DEFAULT 0

#define _DAP_MOCK_MAP_IMPL(N, macro, ...) \
    _DAP_MOCK_MAP_IMPL_COND(N, macro, __VA_ARGS__)

#define _DAP_MOCK_MAP_IMPL_COND(N, macro, ...) \
    _DAP_MOCK_MAP_IMPL_COND_EVAL(N, macro, __VA_ARGS__)

#define _DAP_MOCK_MAP_IMPL_COND_EVAL(N, macro, ...) \
    _DAP_MOCK_MAP_IMPL_COND_##N(macro, __VA_ARGS__)

// Handle empty case (0 params)
#define _DAP_MOCK_MAP_IMPL_COND_0(macro, ...) \
    _DAP_MOCK_MAP_0(macro)

// Handle N=1 case: check if empty and dispatch accordingly
// Now with PARAM expanding to 2 args, empty means 0 args, single param means 2 args
// So we can use _DAP_MOCK_NARGS directly: 0 for empty, 2 for single param
#define _DAP_MOCK_MAP_IMPL_COND_1(macro, ...) \
    _DAP_MOCK_MAP_IMPL_COND_1_EXPAND(_DAP_MOCK_NARGS(__VA_ARGS__), macro, __VA_ARGS__)

// Expand arg count before token concatenation
#define _DAP_MOCK_MAP_IMPL_COND_1_EXPAND(N, macro, ...) \
    _DAP_MOCK_MAP_IMPL_COND_1_EXPAND2(N, macro, __VA_ARGS__)

#define _DAP_MOCK_MAP_IMPL_COND_1_EXPAND2(N, macro, ...) \
    _DAP_MOCK_MAP_IMPL_COND_1_##N(macro, __VA_ARGS__)

// Empty case: 0 arguments
#define _DAP_MOCK_MAP_IMPL_COND_1_0(macro, ...) \
    _DAP_MOCK_MAP_0(macro)

{{AWK:
BEGIN {
    param_counts_str = ENVIRON["PARAM_COUNTS_ARRAY"]
    max_args_count = int(ENVIRON["MAX_ARGS_COUNT"])
    if (max_args_count < 0) max_args_count = 0
    
    # Parse PARAM_COUNTS_ARRAY (space-separated)
    delete param_counts_array
    param_count_idx = 0
    if (param_counts_str != "") {
        n = split(param_counts_str, parts, /[ \t]+/)
        for (i = 1; i <= n; i++) {
            count = int(parts[i])
            if (count >= 0) {
                param_counts_array[++param_count_idx] = count
            }
        }
    }
    
    # Track which counts we have
    delete has_count
    for (i = 1; i <= param_count_idx; i++) {
        has_count[param_counts_array[i]] = 1
    }
    
    # Generate _DAP_MOCK_MAP_IMPL_COND_1_N macros
    for (arg_count = 1; arg_count <= max_args_count; arg_count++) {
        param_count = int(arg_count / 2)
        
        # Build macro signature
        macro_sig = "_DAP_MOCK_MAP_IMPL_COND_1_" arg_count "(macro"
        for (i = 1; i <= arg_count; i++) {
            macro_sig = macro_sig ", p" i
        }
        macro_sig = macro_sig ", ...)"
        
        # Determine macro body
        if (has_count[param_count] || param_count == 0) {
            macro_body = "_DAP_MOCK_MAP_" param_count "(macro"
            for (i = 1; i <= arg_count; i++) {
                macro_body = macro_body ", p" i
            }
            macro_body = macro_body ")"
        } else {
            # Find fallback count
            fallback_count = 0
            for (i = 1; i <= param_count_idx; i++) {
                available_count = param_counts_array[i]
                if (available_count <= param_count && available_count > fallback_count) {
                    fallback_count = available_count
                }
            }
            macro_body = "_DAP_MOCK_MAP_" fallback_count "(macro"
            for (i = 1; i <= arg_count; i++) {
                macro_body = macro_body ", p" i
            }
            macro_body = macro_body ")"
        }
        
        printf "// %d param(s) case: %d arguments\n", param_count, arg_count
        printf "#define %s \\\n", macro_sig
        printf "    %s\n", macro_body
        print ""
    }
}
}}

{{AWK:
BEGIN {
    param_counts_str = ENVIRON["PARAM_COUNTS_ARRAY"]
    
    # Parse PARAM_COUNTS_ARRAY (space-separated)
    delete param_counts_array
    param_count_idx = 0
    if (param_counts_str != "") {
        n = split(param_counts_str, parts, /[ \t]+/)
        for (i = 1; i <= n; i++) {
            count = int(parts[i])
            if (count >= 0) {
                param_counts_array[++param_count_idx] = count
            }
        }
    }
    
    # Generate _DAP_MOCK_MAP_IMPL_COND_N macros for count > 1
    for (i = 1; i <= param_count_idx; i++) {
        count = param_counts_array[i]
        if (count > 1) {
            printf "// Conditional macro for %d parameters\n", count
            printf "#define _DAP_MOCK_MAP_IMPL_COND_%d(macro, ...) \\\n", count
            printf "    _DAP_MOCK_MAP_%d(macro, __VA_ARGS__)\n", count
            print ""
        }
    }
}
}}

{{postproc:{{AWK:
# Post-process: append content from external files if provided
# First, print all template content
{
    print
}

# Then append content from external files if provided
END {
    # Append return type macros if provided
    return_type_macros_file = ENVIRON["RETURN_TYPE_MACROS_FILE"]
    if (return_type_macros_file != "" && return_type_macros_file != "{{RETURN_TYPE_MACROS_FILE}}") {
        # Read and append content from the file
        while ((getline line < return_type_macros_file) > 0) {
            print line
        }
        close(return_type_macros_file)
    }
    
    # Append simple wrapper macros if provided
    simple_wrapper_macros_file = ENVIRON["SIMPLE_WRAPPER_MACROS_FILE"]
    if (simple_wrapper_macros_file != "" && simple_wrapper_macros_file != "{{SIMPLE_WRAPPER_MACROS_FILE}}") {
        # Read and append content from the file
        while ((getline line < simple_wrapper_macros_file) > 0) {
            print line
        }
        close(simple_wrapper_macros_file)
    }
}
}}}}
