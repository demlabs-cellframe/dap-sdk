// ============================================================================
// Generated specialized macros for return types
// ============================================================================
// These macros are generated based on actual return types found in the code.
// Each macro routes to the appropriate void/non-void implementation.
// ALL return types MUST have a generated macro - no fallbacks are provided.

{{#if SELECT_MACROS_DATA}}
    {{#for entry in SELECT_MACROS_DATA}}
        {{entry|split|pipe}}
        {{#set macro_name={{entry|part|0}}}}
        {{#set original_type={{entry|part|1}}}}
        {{#set is_void={{entry|part|2}}}}
        
        // Specialized macro for return type: {{original_type}}
        {{#if is_void|eq|1}}
            #define _DAP_MOCK_WRAPPER_CUSTOM_SELECT_{{macro_name}}(func_name, return_type_full, ...) \
                _DAP_MOCK_WRAPPER_CUSTOM_VOID(func_name, __VA_ARGS__)
        {{else}}
            #define _DAP_MOCK_WRAPPER_CUSTOM_SELECT_{{macro_name}}(func_name, return_type_full, ...) \
                _DAP_MOCK_WRAPPER_CUSTOM_NONVOID(return_type_full, func_name, ##__VA_ARGS__)
        {{/if}}
        
    {{/for}}
{{/if}}

