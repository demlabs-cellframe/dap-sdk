// ============================================================================
// Generated function-specific wrapper macros for DAP_MOCK_CUSTOM
// ============================================================================
// Generator creates wrapper macros for ALL functions directly using dap_tpl.
// NO ## USED - generator creates all macro names directly, including function names
// NO FALLBACKS - if function not found, compilation will fail (Fail-Fast principle)
//
// For each function found in code, generator creates:
// - _DAP_MOCK_CUSTOM_FOR_<func_name>(return_type, params)
//
// Example: for "dap_client_http_request", generator creates:
// - _DAP_MOCK_CUSTOM_FOR_dap_client_http_request(return_type, params)
//
// Dispatching logic:
// The base macro DAP_MOCK_CUSTOM uses ## concatenation to call the specific wrapper directly:
// _DAP_MOCK_CUSTOM_FOR_##func_name(return_type, params)
// This eliminates the need for complex dispatch chains and exact parameter matching.

{{#if FUNCTIONS_DATA}}
    {{#for entry in FUNCTIONS_DATA|newline}}
        {{entry|split|pipe}}
        {{#set return_type={{entry|part|0}}}}
        {{#set func_name={{entry|part|1}}}}
        
        {{#if func_name}}
            // ============================================================================
            // Wrapper for function: {{func_name}}
            // Return type: {{return_type}}
            // ============================================================================
            {{#if return_type == 'void'}}
                // Void function wrapper - simple params, no PARAM macros
                #define _DAP_MOCK_CUSTOM_FOR_{{func_name}}(return_type_full, params) \
                    extern void __real_{{func_name}} params; \
                    void __wrap_{{func_name}} params
            {{else}}
                // Non-void function wrapper - simple params, no PARAM macros
                #define _DAP_MOCK_CUSTOM_FOR_{{func_name}}(return_type_full, params) \
                    extern return_type_full __real_{{func_name}} params; \
                    return_type_full __wrap_{{func_name}} params
            {{/if}}
        {{/if}}
    {{/for}}
{{/if}}
