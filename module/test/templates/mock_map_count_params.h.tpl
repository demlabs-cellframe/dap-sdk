// ============================================================================
// Parameter counting macros - Universal template based on code analysis
// ============================================================================
// Count number of PARAM entries (each PARAM is 2 args)
// Empty: 0 args -> 0 params
// "void": 1 arg "void" -> 0 params (special case)
// 1 param: 2 args -> 1 param
// 3 params: 6 args -> 3 params
// Generator creates macros for all cases directly based on MAP_COUNT_PARAMS_HELPER_DATA

#define _DAP_MOCK_MAP_COUNT_PARAMS(...) \
    _DAP_MOCK_MAP_COUNT_PARAMS_EXPAND(__VA_ARGS__)
#define _DAP_MOCK_MAP_COUNT_PARAMS_EXPAND(...) \
    _DAP_MOCK_MAP_COUNT_PARAMS_EXPAND2(__VA_ARGS__)
#define _DAP_MOCK_MAP_COUNT_PARAMS_EXPAND2(...) \
    _DAP_MOCK_MAP_COUNT_PARAMS_IMPL(_DAP_MOCK_NARGS(__VA_ARGS__), __VA_ARGS__)

// Generate macros for all arg_count values directly from code analysis
// Format: arg_count|param_count from MAP_COUNT_PARAMS_HELPER_DATA
{{#if MAP_COUNT_PARAMS_HELPER_DATA}}
    {{#for entry in MAP_COUNT_PARAMS_HELPER_DATA|newline}}
        {{entry|split|pipe}}
        {{#set arg_count={{entry|part|0}}}}
        {{#set param_count={{entry|part|1}}}}
        {{#if arg_count == '0'}}
// Special case: 0 arguments -> 0 params - generated from code analysis
#define _DAP_MOCK_MAP_COUNT_PARAMS_IMPL_0(arg_count_val, ...) 0
        {{else}}
            {{#if arg_count == '1'}}
// Special case: 1 argument -> check for "void" - generated from code analysis
// "void" is a fixed C keyword, so we generate the check directly
#define _DAP_MOCK_MAP_COUNT_PARAMS_IMPL_1(arg_count_val, first_arg, ...) \
    _DAP_MOCK_MAP_COUNT_PARAMS_IMPL_1_CHECK(first_arg)

// Use ## concatenation to dispatch based on first_arg value
#define _DAP_MOCK_MAP_COUNT_PARAMS_IMPL_1_CHECK(first_arg) \
    _DAP_MOCK_MAP_COUNT_PARAMS_IMPL_1_CHECK_EXPAND(first_arg)
#define _DAP_MOCK_MAP_COUNT_PARAMS_IMPL_1_CHECK_EXPAND(first_arg) \
    _DAP_MOCK_MAP_COUNT_PARAMS_IMPL_1_CHECK_##first_arg

// Check if first_arg is "void" -> return 0 params
#define _DAP_MOCK_MAP_COUNT_PARAMS_IMPL_1_CHECK_void 0

// Handle empty argument (occurs when macro is called with empty __VA_ARGS__)
// In GCC/Clang, empty __VA_ARGS__ results in NARGS=1 and an empty first_arg
#define _DAP_MOCK_MAP_COUNT_PARAMS_IMPL_1_CHECK_ 0

// NO FALLBACK - Fail-Fast: if first_arg is not "void" or empty, compilation will fail with UNDEFINED error
// This happens if someone passes 1 argument that is NOT "void" (which is invalid for PARAM macros)
            {{else}}
// Case: {{arg_count}} arguments -> {{param_count}} params - generated from code analysis
#define _DAP_MOCK_MAP_COUNT_PARAMS_IMPL_{{arg_count}}(arg_count_val, ...) {{param_count}}
            {{/if}}
        {{/if}}
    {{/for}}
{{/if}}

// Main dispatcher - uses two-level expansion to expand arg_count before routing
// Generator creates macros for all arg_count values from code analysis
// If not found, undefined macro error (Fail-Fast)
#define _DAP_MOCK_MAP_COUNT_PARAMS_IMPL(arg_count, ...) \
    _DAP_MOCK_MAP_COUNT_PARAMS_IMPL_EXPAND(arg_count, ##__VA_ARGS__)
#define _DAP_MOCK_MAP_COUNT_PARAMS_IMPL_EXPAND(arg_count, ...) \
    _DAP_MOCK_MAP_COUNT_PARAMS_IMPL_EXPAND2(arg_count, ##__VA_ARGS__)
#define _DAP_MOCK_MAP_COUNT_PARAMS_IMPL_EXPAND2(arg_count, ...) \
    _DAP_MOCK_MAP_COUNT_PARAMS_IMPL_##arg_count(arg_count, ##__VA_ARGS__)
