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
    _DAP_MOCK_MAP_COUNT_PARAMS_CHECK_VOID_IMPL(first_arg, arg_count, __VA_ARGS__)
#define _DAP_MOCK_MAP_COUNT_PARAMS_CHECK_VOID_IMPL(first_arg, arg_count, ...) \
    _DAP_MOCK_MAP_COUNT_PARAMS_CHECK_VOID_BY_COUNT(arg_count, first_arg, __VA_ARGS__)
#define _DAP_MOCK_MAP_COUNT_PARAMS_CHECK_VOID_BY_COUNT(arg_count, first_arg, ...) \
    _DAP_MOCK_MAP_COUNT_PARAMS_CHECK_VOID_BY_COUNT_##arg_count(first_arg, __VA_ARGS__)
#define _DAP_MOCK_MAP_COUNT_PARAMS_CHECK_VOID_BY_COUNT_0(first_arg, ...) 0
#define _DAP_MOCK_MAP_COUNT_PARAMS_CHECK_VOID_BY_COUNT_1(first_arg, ...) \
    _DAP_MOCK_MAP_COUNT_PARAMS_CHECK_VOID_BY_COUNT_1_CHECK(first_arg)
#define _DAP_MOCK_MAP_COUNT_PARAMS_CHECK_VOID_BY_COUNT_1_CHECK(first_arg) \
    _DAP_MOCK_MAP_COUNT_PARAMS_CHECK_VOID_BY_COUNT_1_CHECK_##first_arg
#define _DAP_MOCK_MAP_COUNT_PARAMS_CHECK_VOID_BY_COUNT_1_CHECK_void 0
#define _DAP_MOCK_MAP_COUNT_PARAMS_CHECK_VOID_BY_COUNT_1_CHECK_DEFAULT \
    _DAP_MOCK_MAP_COUNT_PARAMS_EXPAND(1)
// For arg_count >= 2, use normal logic (arg_count / 2)
// We need to recalculate arg_count from __VA_ARGS__ since we lost it in the chain
// Generate macros for specific arg_count values to avoid DEFAULT fallback issues
{{#/bin/sh:
# Generate _DAP_MOCK_MAP_COUNT_PARAMS_CHECK_VOID_BY_COUNT_N macros for arg_count >= 2
MAX_ARGS_COUNT="${MAX_ARGS_COUNT:-0}"
MAP_COUNT_PARAMS_CHECK_VOID_BY_COUNT_MACROS=""
for i in $(seq 2 $MAX_ARGS_COUNT); do
    MAP_COUNT_PARAMS_CHECK_VOID_BY_COUNT_MACROS="${MAP_COUNT_PARAMS_CHECK_VOID_BY_COUNT_MACROS}#define _DAP_MOCK_MAP_COUNT_PARAMS_CHECK_VOID_BY_COUNT_${i}(first_arg, ...) \\"$'\n'
    MAP_COUNT_PARAMS_CHECK_VOID_BY_COUNT_MACROS="${MAP_COUNT_PARAMS_CHECK_VOID_BY_COUNT_MACROS}    _DAP_MOCK_MAP_COUNT_PARAMS_RECALC(__VA_ARGS__)"$'\n'
done
echo -n "$MAP_COUNT_PARAMS_CHECK_VOID_BY_COUNT_MACROS"
}}
#define _DAP_MOCK_MAP_COUNT_PARAMS_CHECK_VOID_BY_COUNT_DEFAULT(first_arg, ...) \
    _DAP_MOCK_MAP_COUNT_PARAMS_RECALC(__VA_ARGS__)
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
{{#/bin/sh:
# Generate _DAP_MOCK_MAP_COUNT_PARAMS_HELPER macros
MAX_ARGS_COUNT="${MAX_ARGS_COUNT:-0}"
MAP_COUNT_PARAMS_HELPER_MACROS=""
for i in $(seq 0 $MAX_ARGS_COUNT); do
    param_count=$((i / 2))
    MAP_COUNT_PARAMS_HELPER_MACROS="${MAP_COUNT_PARAMS_HELPER_MACROS}#define _DAP_MOCK_MAP_COUNT_PARAMS_HELPER_${i} ${param_count}"$'\n'
done
echo -n "$MAP_COUNT_PARAMS_HELPER_MACROS"
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

{{#/bin/sh:
# Generate _DAP_MOCK_MAP_IMPL_COND_1_N macros
PARAM_COUNTS="${PARAM_COUNTS_ARRAY:-0}"
MAX_ARGS_COUNT="${MAX_ARGS_COUNT:-0}"

IFS=' ' read -ra PARAM_COUNTS_ARRAY <<< "$PARAM_COUNTS"

MAP_IMPL_COND_1_N_MACROS=""
for arg_count in $(seq 1 $MAX_ARGS_COUNT); do
    param_count=$((arg_count / 2))
    
    macro_sig="_DAP_MOCK_MAP_IMPL_COND_1_${arg_count}(macro"
    for i in $(seq 1 $arg_count); do
        macro_sig="${macro_sig}, p$i"
    done
    macro_sig="${macro_sig}, ...)"
    
    if [[ " ${PARAM_COUNTS_ARRAY[@]} " =~ " ${param_count} " ]] || [ "$param_count" -eq 0 ]; then
        macro_body="_DAP_MOCK_MAP_${param_count}(macro"
        for i in $(seq 1 $arg_count); do
            macro_body="${macro_body}, p$i"
        done
        macro_body="${macro_body})"
    else
        fallback_count=0
        for available_count in "${PARAM_COUNTS_ARRAY[@]}"; do
            [ -z "$available_count" ] && continue
            [ "$available_count" -le "$param_count" ] && [ "$available_count" -gt "$fallback_count" ] && fallback_count=$available_count
        done
        macro_body="_DAP_MOCK_MAP_${fallback_count}(macro"
        for i in $(seq 1 $arg_count); do
            macro_body="${macro_body}, p$i"
        done
        macro_body="${macro_body})"
    fi
    
    MAP_IMPL_COND_1_N_MACROS="${MAP_IMPL_COND_1_N_MACROS}// ${param_count} param(s) case: ${arg_count} arguments"$'\n'
    MAP_IMPL_COND_1_N_MACROS="${MAP_IMPL_COND_1_N_MACROS}#define ${macro_sig} \\"$'\n'
    MAP_IMPL_COND_1_N_MACROS="${MAP_IMPL_COND_1_N_MACROS}    ${macro_body}"$'\n'
    MAP_IMPL_COND_1_N_MACROS="${MAP_IMPL_COND_1_N_MACROS}"$'\n'
done

echo -n "$MAP_IMPL_COND_1_N_MACROS"
}}

{{#/bin/sh:
# Generate _DAP_MOCK_MAP_IMPL_COND_N macros for count > 1
PARAM_COUNTS="${PARAM_COUNTS_ARRAY:-0}"

IFS=' ' read -ra PARAM_COUNTS_ARRAY <<< "$PARAM_COUNTS"

MAP_IMPL_COND_N_MACROS=""
for count in "${PARAM_COUNTS_ARRAY[@]}"; do
    [ -z "$count" ] && continue
    [ "$count" -le 1 ] && continue
    
    MAP_IMPL_COND_N_MACROS="${MAP_IMPL_COND_N_MACROS}// Conditional macro for $count parameters"$'\n'
    MAP_IMPL_COND_N_MACROS="${MAP_IMPL_COND_N_MACROS}#define _DAP_MOCK_MAP_IMPL_COND_${count}(macro, ...) \\"$'\n'
    MAP_IMPL_COND_N_MACROS="${MAP_IMPL_COND_N_MACROS}    _DAP_MOCK_MAP_${count}(macro, __VA_ARGS__)"$'\n'
    MAP_IMPL_COND_N_MACROS="${MAP_IMPL_COND_N_MACROS}"$'\n'
done

echo -n "$MAP_IMPL_COND_N_MACROS"
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

