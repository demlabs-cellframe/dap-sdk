// ============================================================================
// Generated type normalization macros for pointer types
// ============================================================================
// These macros handle normalization of pointer types (e.g., "dap_server_t*" -> "dap_server_t_PTR")
// and extraction of base types from pointer types.
//
// For each pointer type found in the code, the generator creates:
// 1. _DAP_MOCK_NORMALIZE_TYPE_<base_type> -> <normalized_key> (for base type normalization)
// 2. _DAP_MOCK_NORMALIZE_TYPE_<escaped_pointer_type> -> <normalized_key> (for escaped pointer type)
// 3. _DAP_MOCK_GET_BASE_<escaped_pointer_type> -> <base_type> (for base type extraction)
// 4. _DAP_MOCK_BASE_<escaped_pointer_type> -> <base_type> (for base type extraction via _DAP_MOCK_GET_BASE_TYPE)
// 5. _DAP_MOCK_BASE_ESCAPE_<escaped_pointer_type> -> <escaped_pointer_type> (for type escaping)
//
// Example for "dap_server_t*":
// - _DAP_MOCK_NORMALIZE_TYPE_dap_server_t -> dap_server_t_PTR
// - _DAP_MOCK_NORMALIZE_TYPE_dap_server_t_PTR -> dap_server_t_PTR
// - _DAP_MOCK_GET_BASE_dap_server_t_PTR -> dap_server_t
// - _DAP_MOCK_BASE_dap_server_t_PTR -> dap_server_t
// - _DAP_MOCK_BASE_ESCAPE_dap_server_t_PTR -> dap_server_t_PTR

{{#if NORMALIZATION_MACROS_DATA}}
    {{#for entry in NORMALIZATION_MACROS_DATA}}
        {{entry|split|pipe}}
        {{#set macro_key={{entry|part|0}}}}
        {{#set base_type={{entry|part|1}}}}
        {{#set normalized_key={{entry|part|2}}}}
        {{#set escaped_base_key={{entry|part|3}}}}
        {{#set escaped_original_key={{entry|part|4}}}}
        {{#set escaped_macro_key_for_escape={{entry|part|5}}}}
        
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
            
        {{else}}
            // Non-pointer type: {{macro_key}}
            // Normalize type: {{escaped_base_key}} -> {{escaped_base_key}} (pass-through)
            #define _DAP_MOCK_NORMALIZE_TYPE_{{escaped_base_key}} {{escaped_base_key}}
            // Transform non-pointer type name to escaped form: {{escaped_macro_key_for_escape}} -> {{escaped_base_key}} (pass-through)
            #define _DAP_MOCK_TRANSFORM_TYPE_HELPER_{{escaped_macro_key_for_escape}} {{escaped_base_key}}
            // Convert non-pointer type to escaped form: {{macro_key}} -> {{escaped_base_key}} (pass-through)
            #define _DAP_MOCK_BASE_ESCAPE_{{escaped_base_key}} {{escaped_base_key}}
            
        {{/if}}
    {{/for}}
{{/if}}
