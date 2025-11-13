// ============================================================================
// Generated type normalization macros
// ============================================================================
// These macros normalize pointer types by removing * from type names.

{{#if NORMALIZATION_MACROS_DATA}}
    {{#for entry in NORMALIZATION_MACROS_DATA}}
        {{entry|split|pipe}}
        {{#set macro_key={{entry|part|0}}}}
        {{#set base_type={{entry|part|1}}}}
        {{#set normalized_key={{entry|part|2}}}}
        {{#set escaped_base_key={{entry|part|3}}}}
        {{#set escaped_original_key={{entry|part|4}}}}
        {{#set escaped_macro_key_for_escape={{entry|part|5}}}}
        {{#set escaped_macro_key_for_raw_type={{entry|part|6}}}}
        
        {{#if macro_key}}
            {{#if macro_key|contains|*}}
            // Pointer type: {{macro_key}}
            // Base type: {{base_type}}
            // Escaped pointer type: {{escaped_original_key}}
            // Normalized key: {{normalized_key}}
            
            // Normalize base type: {{escaped_base_key}} -> {{normalized_key}} (from pointer type {{macro_key}})
            #define _DAP_MOCK_NORMALIZE_TYPE_{{escaped_base_key}} {{normalized_key}}
            
            {{#if escaped_original_key|ne|escaped_base_key}}
                // Normalize escaped pointer type: {{escaped_original_key}} -> {{normalized_key}} (from {{macro_key}})
                #define _DAP_MOCK_NORMALIZE_TYPE_{{escaped_original_key}} {{normalized_key}}
            {{/if}}
            
            // Extract base type from pointer type: {{escaped_original_key}} -> {{escaped_base_key}}
            #define _DAP_MOCK_GET_BASE_{{escaped_original_key}} {{escaped_base_key}}
            // Extract base type from escaped pointer type: {{escaped_original_key}} -> {{escaped_base_key}}
            #define _DAP_MOCK_BASE_{{escaped_original_key}} {{escaped_base_key}}
            // Transform escaped type name to escaped type value: {{escaped_macro_key_for_escape}} -> {{escaped_original_key}}
            #define _DAP_MOCK_TRANSFORM_TYPE_HELPER_{{escaped_macro_key_for_escape}} {{escaped_original_key}}
            #define _DAP_MOCK_BASE_ESCAPE_{{escaped_macro_key_for_escape}} {{escaped_original_key}}
            // Direct base extraction dispatcher for pointer types (with *)
            // Generator creates this macro directly with full type name including *
            // Format: _DAP_MOCK_EXTRACT_BASE_dap_list_t* -> dap_list_t
            // This macro is called directly without using ##
            // Generator creates it for each pointer type found in the code
            #define _DAP_MOCK_EXTRACT_BASE_{{macro_key}} {{escaped_base_key}}
            // Also create _DAP_MOCK_GET_BASE_TYPE for direct lookup
            #define _DAP_MOCK_GET_BASE_TYPE_{{macro_key}} {{escaped_base_key}}
            // Escape type for dispatcher - maps raw type with * to escaped version with _STAR
            // Format: _DAP_MOCK_ESCAPE_TYPE_FOR_DISPATCHER_dap_list_t* -> dap_list_t_STAR
            // This macro has * in name, so it cannot be created via ##
            // Generator creates it directly for each pointer type
            #define _DAP_MOCK_ESCAPE_TYPE_FOR_DISPATCHER_{{macro_key}} {{escaped_macro_key_for_raw_type}}
            // Dispatcher macro for pointer types - does entire chain: extract base -> normalize -> select
            // Format: _DAP_MOCK_WRAPPER_CUSTOM_SELECT_DISPATCHER_dap_list_t_STAR(func_name, return_type_full, ...)
            // Generator creates this macro directly with escaped type name (without *) so it can be called via ##
            // Generator creates all intermediate macros: normalize type and type-to-selector
            #define _DAP_MOCK_WRAPPER_CUSTOM_SELECT_DISPATCHER_{{escaped_macro_key_for_raw_type}}(func_name, return_type_full, ...) \
                _DAP_MOCK_TYPE_TO_SELECT_NAME_{{normalized_key}}(func_name, return_type_full, ##__VA_ARGS__)
            
            {{else}}
            // Non-pointer type: {{macro_key}}
            // Normalize type: {{escaped_base_key}} -> {{escaped_base_key}} (pass-through)
            #define _DAP_MOCK_NORMALIZE_TYPE_{{escaped_base_key}} {{escaped_base_key}}
            // Transform non-pointer type name to escaped form: {{escaped_macro_key_for_escape}} -> {{escaped_base_key}} (pass-through)
            #define _DAP_MOCK_TRANSFORM_TYPE_HELPER_{{escaped_macro_key_for_escape}} {{escaped_base_key}}
            // Convert non-pointer type to escaped form: {{macro_key}} -> {{escaped_base_key}} (pass-through)
            #define _DAP_MOCK_BASE_ESCAPE_{{escaped_base_key}} {{escaped_base_key}}
            // Extract base from non-pointer type: {{escaped_base_key}} -> {{escaped_base_key}} (pass-through)
            #define _DAP_MOCK_BASE_{{escaped_base_key}} {{escaped_base_key}}
            // Direct base extraction dispatcher for non-pointer type
            // Generator creates this macro directly for each non-pointer type
            // Format: _DAP_MOCK_EXTRACT_BASE_int -> int
            // This macro is called directly without using ##
            // Generator creates it for each non-pointer type found in the code
            #define _DAP_MOCK_EXTRACT_BASE_{{macro_key}} {{escaped_base_key}}
            // Also create _DAP_MOCK_GET_BASE_TYPE for direct lookup
            #define _DAP_MOCK_GET_BASE_TYPE_{{macro_key}} {{escaped_base_key}}
            // Escape type for dispatcher - pass-through for non-pointer types
            // Format: _DAP_MOCK_ESCAPE_TYPE_FOR_DISPATCHER_int -> int
            #define _DAP_MOCK_ESCAPE_TYPE_FOR_DISPATCHER_{{macro_key}} {{escaped_base_key}}
            // Dispatcher macro for non-pointer types - does entire chain: extract base -> normalize -> select
            // Format: _DAP_MOCK_WRAPPER_CUSTOM_SELECT_DISPATCHER_int(func_name, return_type_full, ...)
            // Generator creates this macro directly for each non-pointer type
            // Generator creates all intermediate macros: normalize type and type-to-selector
            #define _DAP_MOCK_WRAPPER_CUSTOM_SELECT_DISPATCHER_{{escaped_base_key}}(func_name, return_type_full, ...) \
                _DAP_MOCK_TYPE_TO_SELECT_NAME_{{escaped_base_key}}(func_name, return_type_full, ##__VA_ARGS__)
            
            {{/if}}
        {{/if}}
    {{/for}}
{{/if}}

// ============================================================================
// Type-to-selector wrapper macros
// ============================================================================

{{#if TYPE_TO_SELECT_DATA}}
    {{#for entry in TYPE_TO_SELECT_DATA}}
        {{entry|split|pipe}}
        {{#set macro_key={{entry|part|0}}}}
        {{#set normalized_key={{entry|part|1}}}}
        {{#set selector_name={{entry|part|2}}}}
        {{#set is_pointer={{entry|part|3}}}}
        
        {{#if is_pointer|eq|1}}
            // Type-to-selector wrapper for normalized type {{normalized_key}} (from {{macro_key}}) -> calls {{selector_name}}
        {{else}}
            // Type-to-selector wrapper for: {{normalized_key}} -> calls {{selector_name}}
        {{/if}}
        #define _DAP_MOCK_TYPE_TO_SELECT_NAME_{{normalized_key}}(func_name, return_type_full, ...) \
            {{selector_name}}(func_name, return_type_full, ##__VA_ARGS__)
        
    {{/for}}
{{/if}}

{{#if BASIC_TYPES_DATA}}
    {{#for entry in BASIC_TYPES_DATA}}
        {{entry|split|pipe}}
        {{#set basic_type={{entry|part|0}}}}
        {{#set normalized_key={{entry|part|1}}}}
        {{#set escaped_base_key={{entry|part|2}}}}
        {{#set selector_name={{entry|part|3}}}}
        
        // Normalize basic type: {{escaped_base_key}} -> {{normalized_key}}
        #define _DAP_MOCK_NORMALIZE_TYPE_{{escaped_base_key}} {{normalized_key}}
        
        // Type-to-selector wrapper for basic type: {{normalized_key}} -> calls {{selector_name}}
        #define _DAP_MOCK_TYPE_TO_SELECT_NAME_{{normalized_key}}(func_name, return_type_full, ...) \
            {{selector_name}}(func_name, return_type_full, ##__VA_ARGS__)
        
        // Transform type helper for basic type: {{escaped_base_key}} -> {{escaped_base_key}} (pass-through)
        #define _DAP_MOCK_TRANSFORM_TYPE_HELPER_{{escaped_base_key}} {{escaped_base_key}}
        // Extract base from escaped type: {{escaped_base_key}} -> {{escaped_base_key}} (pass-through)
        #define _DAP_MOCK_BASE_{{escaped_base_key}} {{escaped_base_key}}
        
        // Escape type for dispatcher - pass-through for basic types
        // Format: _DAP_MOCK_ESCAPE_TYPE_FOR_DISPATCHER_int -> int
        #define _DAP_MOCK_ESCAPE_TYPE_FOR_DISPATCHER_{{basic_type}} {{escaped_base_key}}
        // Dispatcher macro for basic types - does entire chain: extract base -> normalize -> select
        // Format: _DAP_MOCK_WRAPPER_CUSTOM_SELECT_DISPATCHER_int(func_name, return_type_full, ...)
        // Generator creates this macro directly for each basic type
        // Generator creates all intermediate macros: normalize type and type-to-selector
        #define _DAP_MOCK_WRAPPER_CUSTOM_SELECT_DISPATCHER_{{escaped_base_key}}(func_name, return_type_full, ...) \
            _DAP_MOCK_TYPE_TO_SELECT_NAME_{{normalized_key}}(func_name, return_type_full, ##__VA_ARGS__)
        
    {{/for}}
{{/if}}
