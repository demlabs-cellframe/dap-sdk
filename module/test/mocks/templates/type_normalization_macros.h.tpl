// ============================================================================
// Generated type normalization macros for pointer types
// ============================================================================
// These macros handle normalization of pointer types (e.g., "dap_server_t*" -> "dap_server_t_STAR").
// All pointer types are normalized to use _STAR suffix (no intermediate _PTR step).
//
// For each pointer type found in the code, the generator creates:
// 1. _DAP_MOCK_NORMALIZE_TYPE_<base_type> -> <normalized_key> (for base type normalization, uses _STAR)
// 2. _DAP_MOCK_NORMALIZE_TYPE_<escaped_pointer_type> -> <normalized_key> (for escaped pointer type, uses _STAR)
// 3. _DAP_MOCK_GET_BASE_<escaped_pointer_type> -> <base_type> (for base type extraction, uses _STAR)
// 4. _DAP_MOCK_BASE_<escaped_pointer_type> -> <base_type> (for base type extraction via _DAP_MOCK_GET_BASE_TYPE, uses _STAR)
// 5. _DAP_MOCK_BASE_ESCAPE_<escaped_pointer_type> -> <escaped_pointer_type> (for type escaping, uses _STAR)
// 6. _DAP_MOCK_TRANSFORM_TYPE_HELPER_<escaped_with_STAR> -> <normalized_key> (for macro name conversion, uses _STAR)
// 7. _DAP_MOCK_TYPE_TO_STAR_IDENTIFIER_<raw_type_with_*> -> <escaped_with_STAR> (for converting * to _STAR)
// 8. _DAP_MOCK_ESCAPE_TYPE_FOR_DISPATCHER_<escaped_with_STAR> -> <normalized_key> (for dispatcher, uses _STAR)
//
// Example for "dap_server_t*":
// - _DAP_MOCK_NORMALIZE_TYPE_dap_server_t -> dap_server_t_STAR (value normalization)
// - _DAP_MOCK_NORMALIZE_TYPE_dap_server_t_STAR -> dap_server_t_STAR (value normalization)
// - _DAP_MOCK_GET_BASE_dap_server_t_STAR -> dap_server_t (base extraction)
// - _DAP_MOCK_BASE_dap_server_t_STAR -> dap_server_t (base extraction)
// - _DAP_MOCK_BASE_ESCAPE_dap_server_t_STAR -> dap_server_t_STAR (value escaping)
// - _DAP_MOCK_TRANSFORM_TYPE_HELPER_dap_server_t_STAR -> dap_server_t_STAR (macro name -> value)
// - _DAP_MOCK_TYPE_TO_STAR_IDENTIFIER_dap_server_t* -> dap_server_t_STAR (raw type -> macro name)
// - _DAP_MOCK_ESCAPE_TYPE_FOR_DISPATCHER_dap_server_t_STAR -> dap_server_t_STAR (dispatcher lookup)

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
// Transform raw type with * to valid identifier: {{escaped_macro_key_for_raw_type}} -> {{escaped_original_key}}
// This is used by _DAP_MOCK_ESCAPE_TYPE_FOR_DISPATCHER to convert types with * to valid macro names
#define _DAP_MOCK_TRANSFORM_TYPE_HELPER_{{escaped_macro_key_for_raw_type}} {{escaped_original_key}}
// Convert type with * to identifier with _STAR: {{macro_key}} -> {{escaped_macro_key_for_raw_type}}
#define _DAP_MOCK_TYPE_TO_STAR_IDENTIFIER_{{macro_key}} {{escaped_macro_key_for_raw_type}}
// Escape type for dispatcher: {{escaped_macro_key_for_raw_type}} -> {{escaped_original_key}}
#define _DAP_MOCK_ESCAPE_TYPE_FOR_DISPATCHER_{{escaped_macro_key_for_raw_type}} {{escaped_original_key}}
            
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
