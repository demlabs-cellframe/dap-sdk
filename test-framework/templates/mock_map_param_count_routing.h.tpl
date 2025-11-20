// ============================================================================
// Direct macros based on param_count - Universal template based on code analysis
// ============================================================================
// Generator creates macros directly for each param_count value from PARAM_COUNTS_ARRAY
// All macros are generated directly without dispatchers - NO HARDCODED VALUES
// Based on real code analysis: PARAM_COUNTS_ARRAY contains unique param_count values

// Generate direct macros for all param_count values from code analysis
{{#if PARAM_COUNTS_ARRAY}}
    {{#for param_count in PARAM_COUNTS_ARRAY}}
        {{#if param_count == '0'}}
// Direct macro for param_count=0 - generated from code analysis
// Routes to first_arg-specific macros for handling "void" vs empty
#define _DAP_MOCK_MAP_CHECK_VOID_BY_PARAM_COUNT_ROUTE_0(param_count_val, first_arg, macro, ...) \
    _DAP_MOCK_MAP_CHECK_VOID_BY_PARAM_COUNT_0_CHECK(first_arg, macro, __VA_ARGS__)
        {{else}}
// Direct macro for {{param_count}} parameter(s) - generated from code analysis
// Uses exact parameter matching - generator creates full macro name directly
#define _DAP_MOCK_MAP_CHECK_VOID_BY_PARAM_COUNT_ROUTE_{{param_count}}(param_count_val, first_arg, macro, ...) \
    _DAP_MOCK_MAP_IMPL_COND_{{param_count}}(macro, __VA_ARGS__)
        {{/if}}
    {{/for}}
{{/if}}
