// ============================================================================
// Generated specialized macros for return types
// ============================================================================
// These macros are generated based on actual return types found in the code.
// Each macro routes to the appropriate void/non-void implementation.
// ALL return types MUST have a generated macro - no fallbacks are provided.

// ============================================================================
// Process raw data using pure dap_tpl constructs
// ============================================================================

{{#if RETURN_TYPES}}
    // Process RETURN_TYPES (space-separated list)
    {{#for type in RETURN_TYPES}}
        {{#set macro_name={{type|sanitize_name}}}}
        
        {{#if macro_name}}
            // Specialized macro for return type: {{type}}
            {{#if type == 'void'}}
                #define _DAP_MOCK_WRAPPER_CUSTOM_SELECT_{{macro_name}}(func_name, return_type_full, ...) \
                    _DAP_MOCK_WRAPPER_CUSTOM_VOID(func_name, __VA_ARGS__)
            {{else}}
                #define _DAP_MOCK_WRAPPER_CUSTOM_SELECT_{{macro_name}}(func_name, return_type_full, ...) \
                    _DAP_MOCK_WRAPPER_CUSTOM_NONVOID(return_type_full, func_name, ##__VA_ARGS__)
            {{/if}}
        {{/if}}
    {{/for}}
    
    // Process ORIGINAL_TYPES_DATA (newline-separated normalized|original pairs)
    {{#if ORIGINAL_TYPES_DATA}}
        {{#for entry in ORIGINAL_TYPES_DATA}}
            {{entry|split|pipe}}
            {{#set normalized_type={{entry|part|0}}}}
            {{#set original_type={{entry|part|1}}}}
            
            {{#if normalized_type}}
                {{#set macro_key={{original_type|sanitize_name}}}}
                {{#set base_type={{original_type|remove_suffix|*}}}}
                {{#set normalized_key={{normalized_type|normalize_name}}}}
                {{#set escaped_base_key={{base_type|normalize_name}}}}
                
                {{#if macro_key}}
                    {{#if original_type|contains|*}}
                        // Pointer type: {{original_type}}
                        // Base type: {{base_type}}
                        #define _DAP_MOCK_NORMALIZE_TYPE_{{escaped_base_key}} {{normalized_key}}
                        #define _DAP_MOCK_EXTRACT_BASE_{{macro_key}} {{escaped_base_key}}
                    {{else}}
                        // Non-pointer type: {{original_type}}
                        #define _DAP_MOCK_NORMALIZE_TYPE_{{escaped_base_key}} {{escaped_base_key}}
                        #define _DAP_MOCK_EXTRACT_BASE_{{macro_key}} {{escaped_base_key}}
                    {{/if}}
                {{/if}}
            {{/if}}
        {{/for}}
    {{/if}}
    
    // Process BASIC_TYPES_RAW_DATA (newline-separated basic_type|original_type pairs)
    {{#if BASIC_TYPES_RAW_DATA}}
        {{#for entry in BASIC_TYPES_RAW_DATA}}
            {{entry|split|pipe}}
            {{#set basic_type={{entry|part|0}}}}
            {{#set original_type={{entry|part|1}}}}
            
            {{#if basic_type}}
                {{#set normalized_key={{basic_type|normalize_name}}}}
                {{#set escaped_base_key={{basic_type|normalize_name}}}}
                {{#set selector_name=_DAP_MOCK_WRAPPER_CUSTOM_SELECT_{{normalized_key}}}}
                
                // Normalize basic type: {{escaped_base_key}} -> {{normalized_key}}
                #define _DAP_MOCK_NORMALIZE_TYPE_{{escaped_base_key}} {{normalized_key}}
                
                // Type-to-selector wrapper for basic type: {{normalized_key}} -> calls {{selector_name}}
                #define _DAP_MOCK_TYPE_TO_SELECT_NAME_{{normalized_key}}(func_name, return_type_full, ...) \
                    {{selector_name}}(func_name, return_type_full, ##__VA_ARGS__)
            {{/if}}
        {{/for}}
    {{/if}}
{{/if}}

