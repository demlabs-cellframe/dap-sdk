// ============================================================================
// Core _DAP_MAP macros - Universal template based on code analysis
// ============================================================================
// Main entry point for mapping macros over PARAM entries
// Each PARAM expands to 2 arguments (type, name), so we need to divide arg count by 2
// Special handling for "void": when __VA_ARGS__ is "void", produce "void" instead of empty
// Note: _DAP_MOCK_NARGS() returns 1 for empty __VA_ARGS__, so we use param_count to distinguish
// Empty: param_count=0, arg_count=1 (but first_arg is empty)
// "void": param_count=0, arg_count=1, first_arg="void"
// Generator creates all macro names directly based on PARAM_COUNTS_ARRAY from code analysis

#define _DAP_MOCK_MAP(macro, ...) \
    _DAP_MOCK_MAP_EXPAND_1(macro, __VA_ARGS__)

// Helper to extract first argument
#define _DAP_MOCK_GET_FIRST(first, ...) first

// Generate expansion levels using dap_tpl AWK section - ensures proper two-level expansion
// Each level expands to the next level, final level calls _DAP_MOCK_MAP_CHECK_VOID
{{AWK:}}
BEGIN {
    max_level = 6
    for (i = 1; i <= max_level; i++) {
        if (i < max_level) {
            printf "#define _DAP_MOCK_MAP_EXPAND_%d(macro, ...) \\\n", i
            printf "    _DAP_MOCK_MAP_EXPAND_%d(macro, __VA_ARGS__)\n", i + 1
        } else {
            # Pass arguments in safe order: counts first, then macro, then args
            printf "#define _DAP_MOCK_MAP_EXPAND_%d(macro, ...) \\\n", i
            printf "    _DAP_MOCK_MAP_CHECK_VOID(_DAP_MOCK_NARGS(__VA_ARGS__), _DAP_MOCK_MAP_COUNT_PARAMS(__VA_ARGS__), macro, __VA_ARGS__)\n"
        }
    }
}
{{/AWK}}

// CHECK_VOID receives arguments in safe order
#define _DAP_MOCK_MAP_CHECK_VOID(arg_count, param_count, macro, ...) \
    _DAP_MOCK_MAP_CHECK_VOID_IMPL(arg_count, param_count, macro, _DAP_MOCK_GET_FIRST(__VA_ARGS__), __VA_ARGS__)

// IMPL receives extracted first_arg
#define _DAP_MOCK_MAP_CHECK_VOID_IMPL(arg_count, param_count, macro, first_arg, ...) \
    _DAP_MOCK_MAP_CHECK_VOID_BY_PARAM_COUNT_EXPAND(param_count, first_arg, macro, __VA_ARGS__)

// Multi-level expansion to ensure param_count is fully expanded before routing
#define _DAP_MOCK_MAP_CHECK_VOID_BY_PARAM_COUNT_EXPAND(param_count, first_arg, macro, ...) \
    _DAP_MOCK_MAP_CHECK_VOID_BY_PARAM_COUNT_EXPAND2(param_count, first_arg, macro, __VA_ARGS__)
#define _DAP_MOCK_MAP_CHECK_VOID_BY_PARAM_COUNT_EXPAND2(param_count, first_arg, macro, ...) \
    _DAP_MOCK_MAP_CHECK_VOID_BY_PARAM_COUNT_EXPAND3(param_count, first_arg, macro, __VA_ARGS__)
#define _DAP_MOCK_MAP_CHECK_VOID_BY_PARAM_COUNT_EXPAND3(param_count, first_arg, macro, ...) \
    _DAP_MOCK_MAP_CHECK_VOID_BY_PARAM_COUNT_EXPAND4(param_count, first_arg, macro, __VA_ARGS__)
#define _DAP_MOCK_MAP_CHECK_VOID_BY_PARAM_COUNT_EXPAND4(param_count, first_arg, macro, ...) \
    _DAP_MOCK_MAP_CHECK_VOID_BY_PARAM_COUNT_ROUTE(param_count, first_arg, macro, __VA_ARGS__)

// Generator creates routing macros for all param_count values from PARAM_COUNTS_ARRAY
// All macros are generated directly based on code analysis - NO HARDCODED VALUES
// Fail-Fast: if param_count doesn't match any generated value, compilation will fail
#define _DAP_MOCK_MAP_CHECK_VOID_BY_PARAM_COUNT_ROUTE(param_count, first_arg, macro, ...) \
    _DAP_MOCK_MAP_CHECK_VOID_BY_PARAM_COUNT_ROUTE_EXPAND(param_count, first_arg, macro, __VA_ARGS__)

// Expand to ensure numeric value is pasted
#define _DAP_MOCK_MAP_CHECK_VOID_BY_PARAM_COUNT_ROUTE_EXPAND(param_count, first_arg, macro, ...) \
    _DAP_MOCK_MAP_CHECK_VOID_BY_PARAM_COUNT_ROUTE_##param_count(param_count, first_arg, macro, __VA_ARGS__)
