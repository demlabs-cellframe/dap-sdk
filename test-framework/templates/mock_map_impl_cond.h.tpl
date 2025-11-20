// ============================================================================
// Implementation conditional macros (_DAP_MOCK_MAP_IMPL_COND) - Universal template
// ============================================================================
// Routes to appropriate macro based on number of parameters
// Generator creates macros for all N values from code analysis - NO HARDCODED VALUES
// Based on real code analysis: MAP_IMPL_COND_DATA and MAP_IMPL_COND_1_DATA

#define _DAP_MOCK_MAP_IMPL(N, macro, ...) \
    _DAP_MOCK_MAP_IMPL_COND(N, macro, __VA_ARGS__)

#define _DAP_MOCK_MAP_IMPL_COND(N, macro, ...) \
    _DAP_MOCK_MAP_IMPL_COND_EVAL(N, macro, __VA_ARGS__)

// ============================================================================
// Routing based on N (param_count) - Generated from MAP_IMPL_COND_DATA
// ============================================================================
#define _DAP_MOCK_MAP_IMPL_COND_EVAL(N, macro, ...) \
    _DAP_MOCK_MAP_IMPL_COND_ROUTE(N, macro, __VA_ARGS__)

// Generate routing macros for all N values from code analysis
{{#if MAP_IMPL_COND_DATA}}
    {{#for entry in MAP_IMPL_COND_DATA|newline}}
        {{#set param_count={{entry}}}}
        {{#if param_count}}
// Routing macro for {{param_count}} parameters - generated from code analysis
#define _DAP_MOCK_MAP_IMPL_COND_ROUTE_{{param_count}}(N_val, macro, ...) \
    _DAP_MOCK_MAP_IMPL_COND_{{param_count}}(macro, __VA_ARGS__)
        {{/if}}
    {{/for}}
{{/if}}

// Expand to ensure numeric value is pasted
#define _DAP_MOCK_MAP_IMPL_COND_ROUTE(N, macro, ...) \
    _DAP_MOCK_MAP_IMPL_COND_ROUTE_EXPAND(N, macro, __VA_ARGS__)

#define _DAP_MOCK_MAP_IMPL_COND_ROUTE_EXPAND(N, macro, ...) \
    _DAP_MOCK_MAP_IMPL_COND_ROUTE_##N(N, macro, __VA_ARGS__)

// ============================================================================
// Special cases - Generated from code analysis
// ============================================================================
// Handle empty case (0 params)
{{#if MAP_IMPL_COND_DATA}}
    {{#for entry in MAP_IMPL_COND_DATA|newline}}
        {{#set param_count={{entry}}}}
        {{#if param_count == '0'}}
#define _DAP_MOCK_MAP_IMPL_COND_0(macro, ...) \
    _DAP_MOCK_MAP_0(macro)
        {{/if}}
    {{/for}}
{{/if}}

// Handle N=1 case: check if empty and dispatch accordingly
// Now with PARAM expanding to 2 args, empty means 0 args, single param means 2 args
// So we can use _DAP_MOCK_NARGS directly: 0 for empty, 2 for single param
#define _DAP_MOCK_MAP_IMPL_COND_1(macro, ...) \
    _DAP_MOCK_MAP_IMPL_COND_1_EXPAND(_DAP_MOCK_NARGS(__VA_ARGS__), macro, __VA_ARGS__)

#define _DAP_MOCK_MAP_IMPL_COND_1_EXPAND(N, macro, ...) \
    _DAP_MOCK_MAP_IMPL_COND_1_EXPAND2(N, macro, __VA_ARGS__)

// ============================================================================
// Routing for N=1 case based on actual arg_count - Generated from MAP_IMPL_COND_1_DATA
// ============================================================================
#define _DAP_MOCK_MAP_IMPL_COND_1_EXPAND2(N, macro, ...) \
    _DAP_MOCK_MAP_IMPL_COND_1_ROUTE(N, macro, __VA_ARGS__)

// Generate routing macros for all N values in _DAP_MOCK_MAP_IMPL_COND_1_EXPAND2
// Format: arg_count|param_count|fallback_count|macro_params from MAP_IMPL_COND_1_DATA
{{#if MAP_IMPL_COND_1_DATA}}
    {{#for entry in MAP_IMPL_COND_1_DATA|newline}}
        {{entry|split|pipe}}
        {{#set arg_count={{entry|part|0}}}}
        {{#set param_count={{entry|part|1}}}}
        {{#set fallback_count={{entry|part|3}}}}
        {{#set macro_params={{entry|part|4}}}}
        {{#if arg_count}}
            {{#if arg_count == '0'}}
// Empty case: 0 arguments - generated from code analysis
#define _DAP_MOCK_MAP_IMPL_COND_1_0(macro, ...) \
    _DAP_MOCK_MAP_0(macro)
            {{else}}
// Routing macro for {{arg_count}} arguments ({{param_count}} params) - generated from code analysis
#define _DAP_MOCK_MAP_IMPL_COND_1_ROUTE_{{arg_count}}(N_val, macro, ...) \
    _DAP_MOCK_MAP_IMPL_COND_1_{{arg_count}}(macro, __VA_ARGS__)

// Implementation macro for {{arg_count}} arguments ({{param_count}} params)
                {{#if macro_params}}
#define _DAP_MOCK_MAP_IMPL_COND_1_{{arg_count}}(macro, ...) \
    _DAP_MOCK_MAP_IMPL_COND_1_{{arg_count}}_IMPL(macro, __VA_ARGS__)
#define _DAP_MOCK_MAP_IMPL_COND_1_{{arg_count}}_IMPL(macro, ...) \
    _DAP_MOCK_MAP_{{fallback_count}}(macro, __VA_ARGS__)
                {{else}}
#define _DAP_MOCK_MAP_IMPL_COND_1_{{arg_count}}(macro, ...) \
    _DAP_MOCK_MAP_{{fallback_count}}(macro)
                {{/if}}
            {{/if}}
        {{/if}}
    {{/for}}
{{/if}}

// Expand to ensure numeric value is pasted
#define _DAP_MOCK_MAP_IMPL_COND_1_ROUTE(N, macro, ...) \
    _DAP_MOCK_MAP_IMPL_COND_1_ROUTE_EXPAND(N, macro, __VA_ARGS__)

#define _DAP_MOCK_MAP_IMPL_COND_1_ROUTE_EXPAND(N, macro, ...) \
    _DAP_MOCK_MAP_IMPL_COND_1_ROUTE_##N(N, macro, __VA_ARGS__)

// ============================================================================
// Conditional macros for specific param_count values - Generated from MAP_IMPL_COND_DATA
// ============================================================================
{{#if MAP_IMPL_COND_DATA}}
    {{#for entry in MAP_IMPL_COND_DATA|newline}}
        {{#set param_count={{entry}}}}
        {{#if param_count}}
            {{#if param_count != '0'}}
                {{#if param_count != '1'}}
// Conditional macro for {{param_count}} parameters - generated from code analysis
#define _DAP_MOCK_MAP_IMPL_COND_{{param_count}}(macro, ...) \
    _DAP_MOCK_MAP_{{param_count}}(macro, __VA_ARGS__)
                {{/if}}
            {{/if}}
        {{/if}}
    {{/for}}
{{/if}}
