// ============================================================================
// Generated function-specific wrapper macros for DAP_MOCK_WRAPPER_CUSTOM
// ============================================================================
// For each function found in code, generator creates a simple passthrough macro:
//   _DAP_MOCK_WRAPPER_CUSTOM_FOR_<func_name>(return_type_full, ...)
//
// The macro declares __real_ (original) and opens __wrap_ (replacement).
// __VA_ARGS__ carries the parenthesized parameter list from user code,
// e.g. (dap_ledger_t *a_ledger, dap_hash_fast_t *a_tx_hash).
// Parentheses are preserved and serve as the function's parameter list.
//
// No preprocessor tricks (_DAP_MOCK_MAP, PARAM, etc.) — just passthrough.

{{#if FUNCTIONS_DATA}}
    {{#for entry in FUNCTIONS_DATA|newline}}
        {{entry|split|pipe}}
        {{#set return_type={{entry|part|0}}}}
        {{#set func_name={{entry|part|1}}}}
        
        {{#if func_name}}
            #define _DAP_MOCK_WRAPPER_CUSTOM_FOR_{{func_name}}(return_type_full, ...) \
                extern return_type_full __real_{{func_name}} __VA_ARGS__; \
                return_type_full __wrap_{{func_name}} __VA_ARGS__
        {{/if}}
    {{/for}}
{{/if}}
