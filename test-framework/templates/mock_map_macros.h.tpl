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

// Generate _DAP_MOCK_MAP_CHECK_VOID_BY_PARAM_COUNT_N macros for all param_count values
// PARAM_COUNTS_ARRAY is space-separated string like "1 2 3"
{{#if PARAM_COUNTS_ARRAY}}
    {{#for param_count in PARAM_COUNTS_ARRAY}}
        {{#if param_count}}
            {{#set test_val={{param_count}}}}
            {{#if test_val}}
                {{#if test_val|ne|0}}
// Macro for {{param_count}} parameter(s)
#define _DAP_MOCK_MAP_CHECK_VOID_BY_PARAM_COUNT_{{param_count}}(first_arg, macro, ...) \
    _DAP_MOCK_MAP_IMPL_COND_{{param_count}}(macro, __VA_ARGS__)
                {{/if}}
            {{/if}}
        {{/if}}
    {{/for}}
{{/if}}
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
    _DAP_MOCK_MAP_CHECK_VOID_BY_PARAM_COUNT_DEFAULT_IMPL(_DAP_MOCK_MAP_COUNT_PARAMS(__VA_ARGS__), first_arg, macro, __VA_ARGS__)
// Handle case when param_count is not a number (fallback to recalculate)
#define _DAP_MOCK_MAP_CHECK_VOID_BY_PARAM_COUNT_DEFAULT_IMPL(param_count, first_arg, macro, ...) \
    _DAP_MOCK_MAP_CHECK_VOID_BY_PARAM_COUNT_DEFAULT_CHECK(param_count, first_arg, macro, __VA_ARGS__)
#define _DAP_MOCK_MAP_CHECK_VOID_BY_PARAM_COUNT_DEFAULT_CHECK(param_count, first_arg, macro, ...) \
    _DAP_MOCK_MAP_CHECK_VOID_BY_PARAM_COUNT_DEFAULT_EXPAND(param_count, first_arg, macro, __VA_ARGS__)
#define _DAP_MOCK_MAP_CHECK_VOID_BY_PARAM_COUNT_DEFAULT_EXPAND(param_count, first_arg, macro, ...) \
    _DAP_MOCK_MAP_CHECK_VOID_BY_PARAM_COUNT_##param_count(first_arg, macro, __VA_ARGS__)

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
{{#if MAP_COUNT_PARAMS_BY_COUNT_DATA}}
    {{#for entry in MAP_COUNT_PARAMS_BY_COUNT_DATA}}
        {{#set arg_count={{entry}}}}
        {{#if entry}}
#define _DAP_MOCK_MAP_COUNT_PARAMS_CHECK_VOID_BY_COUNT_{{arg_count}}(first_arg, original_first_arg, saved_first_arg, ...) \
    _DAP_MOCK_MAP_COUNT_PARAMS_RECALC(first_arg, ##__VA_ARGS__)
        {{/if}}
    {{/for}}
{{/if}}
#define _DAP_MOCK_MAP_COUNT_PARAMS_CHECK_VOID_BY_COUNT_DEFAULT(first_arg, original_first_arg, saved_first_arg, ...) \
    _DAP_MOCK_MAP_COUNT_PARAMS_RECALC(first_arg, ##__VA_ARGS__)
#define _DAP_MOCK_MAP_COUNT_PARAMS_RECALC(...) \
    _DAP_MOCK_MAP_COUNT_PARAMS_EXPAND(_DAP_MOCK_NARGS(__VA_ARGS__))
#define _DAP_MOCK_MAP_COUNT_PARAMS_EXPAND(arg_count) \
    _DAP_MOCK_MAP_COUNT_PARAMS_EXPAND_IMPL(arg_count)
#define _DAP_MOCK_MAP_COUNT_PARAMS_EXPAND_IMPL(arg_count) \
    _DAP_MOCK_MAP_COUNT_PARAMS_EXPAND_IMPL_CHECK(arg_count)
// Two-stage expansion to handle non-numeric arg_count
#define _DAP_MOCK_MAP_COUNT_PARAMS_EXPAND_IMPL_CHECK(arg_count) \
    _DAP_MOCK_MAP_COUNT_PARAMS_EXPAND_IMPL_EXPAND(arg_count)
#define _DAP_MOCK_MAP_COUNT_PARAMS_EXPAND_IMPL_EXPAND(arg_count) \
    _DAP_MOCK_MAP_COUNT_PARAMS_HELPER_##arg_count

// Helper to convert arg count to param count
#define _DAP_MOCK_MAP_COUNT_PARAMS_HELPER(N) \
    _DAP_MOCK_MAP_COUNT_PARAMS_HELPER_##N

// Generate mappings dynamically: 0 args -> 0 params, 2 args -> 1 param, 4 args -> 2 params, etc.
// Each PARAM expands to 2 args (type, name), so N args = N/2 params (rounded down)
{{#if MAP_COUNT_PARAMS_HELPER_DATA}}
    {{#for entry in MAP_COUNT_PARAMS_HELPER_DATA}}
        {{entry|split|pipe}}
        {{#set arg_count={{entry|part|0}}}}
        {{#set param_count={{entry|part|1}}}}
        {{#if entry}}
#define _DAP_MOCK_MAP_COUNT_PARAMS_HELPER_{{arg_count}} {{param_count}}
        {{/if}}
    {{/for}}
{{/if}}

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

{{#if MAP_IMPL_COND_1_DATA}}
    {{#for entry in MAP_IMPL_COND_1_DATA}}
        {{entry|split|pipe}}
        {{#set arg_count={{entry|part|0}}}}
        {{#set param_count={{entry|part|1}}}}
        {{#set has_count={{entry|part|2}}}}
        {{#set fallback_count={{entry|part|3}}}}
        {{#set macro_params={{entry|part|4}}}}
        {{#if entry}}
// {{param_count}} param(s) case: {{arg_count}} arguments
#define _DAP_MOCK_MAP_IMPL_COND_1_{{arg_count}}({{macro_params}}, ...) \
    _DAP_MOCK_MAP_{{fallback_count}}({{macro_params}})

        {{/if}}
    {{/for}}
{{/if}}

{{#if MAP_IMPL_COND_DATA}}
    {{#for entry in MAP_IMPL_COND_DATA}}
        {{#set param_count={{entry}}}}
        {{#if entry}}
// Conditional macro for {{param_count}} parameters
#define _DAP_MOCK_MAP_IMPL_COND_{{param_count}}(macro, ...) \
    _DAP_MOCK_MAP_{{param_count}}(macro, __VA_ARGS__)

        {{/if}}
    {{/for}}
{{/if}}

{{#if RETURN_TYPE_MACROS_FILE}}
{{#include RETURN_TYPE_MACROS_FILE}}
{{/if}}

{{#if SIMPLE_WRAPPER_MACROS_FILE}}
{{#include SIMPLE_WRAPPER_MACROS_FILE}}
{{/if}}
