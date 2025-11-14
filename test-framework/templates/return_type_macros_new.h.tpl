// ============================================================================
// Generated specialized macros for return types
// ============================================================================
// These macros are generated based on actual return types found in the code.
// Each macro routes to the appropriate void/non-void implementation.
// ALL return types MUST have a generated macro - no fallbacks are provided.

// ============================================================================
// Process data using dap_tpl native constructs (no AWK sections)
// ============================================================================

{{#if RETURN_TYPES}}
    {{! Process RETURN_TYPES and ORIGINAL_TYPES_DATA to generate SELECT macros }}
    {{! RETURN_TYPES is space-separated list of normalized types }}
    {{! ORIGINAL_TYPES_DATA is newline-separated normalized|original pairs }}

    {{! Build SELECT_MACROS_DATA from return types }}
    {{! RETURN_TYPES is space-separated - for loop will auto-detect }}
    {{#set SELECT_MACROS_DATA=}}
    {{#for type in RETURN_TYPES}}
        {{#if type}}
            {{#set type_normalized={{type|sanitize_name}}}}
            {{#set is_void=0}}
            {{#if type == void}}
                {{#set is_void=1}}
            {{/if}}
            {{#if SELECT_MACROS_DATA}}
                {{#set SELECT_MACROS_DATA={{SELECT_MACROS_DATA}}
{{type_normalized}}|{{type}}|{{is_void}}}}
            {{else}}
                {{#set SELECT_MACROS_DATA={{type_normalized}}|{{type}}|{{is_void}}}}
            {{/if}}
        {{/if}}
    {{/for}}

    {{! Add void if not already present }}
    {{#if SELECT_MACROS_DATA contains void}}
    {{else}}
        {{#if SELECT_MACROS_DATA}}
            {{#set SELECT_MACROS_DATA={{SELECT_MACROS_DATA}}
void|void|1}}
        {{else}}
            {{#set SELECT_MACROS_DATA=void|void|1}}
        {{/if}}
    {{/if}}

    {{! Process ORIGINAL_TYPES_DATA and BASIC_TYPES_RAW_DATA to build normalization data }}
    {{#set NORMALIZATION_RAW_DATA=}}

    {{! Process ORIGINAL_TYPES_DATA if present }}
    {{#if ORIGINAL_TYPES_DATA}}
        {{ORIGINAL_TYPES_DATA|split|newline}}
        {{#for entry in ORIGINAL_TYPES_DATA}}
            {{#if entry}}
                {{entry|split|pipe}}
                {{#set normalized_type={{entry|part|0}}}}
                {{#set original_type={{entry|part|1}}}}
                {{#set macro_key={{original_type}}}}
                {{#set base_type={{original_type}}}}
                {{#if original_type contains *}}
                    {{#set base_type={{original_type|escape_char|*|}}}}
                    {{#set base_type={{base_type|trim}}}}
                {{/if}}
                {{#if NORMALIZATION_RAW_DATA}}
                    {{#set NORMALIZATION_RAW_DATA={{NORMALIZATION_RAW_DATA}}
{{macro_key}}|{{base_type}}}}
                {{else}}
                    {{#set NORMALIZATION_RAW_DATA={{macro_key}}|{{base_type}}}}
                {{/if}}
            {{/if}}
        {{/for}}
    {{/if}}

    {{! Add basic types }}
    {{#if BASIC_TYPES_RAW_DATA}}
        {{BASIC_TYPES_RAW_DATA|split|newline}}
        {{#for entry in BASIC_TYPES_RAW_DATA}}
            {{#if entry}}
                {{entry|split|pipe}}
                {{#set basic_type={{entry|part|0}}}}
                {{! Check if not already in NORMALIZATION_RAW_DATA }}
                {{#if NORMALIZATION_RAW_DATA contains {{basic_type}}|}}
                {{else}}
                    {{#if NORMALIZATION_RAW_DATA}}
                        {{#set NORMALIZATION_RAW_DATA={{NORMALIZATION_RAW_DATA}}
{{basic_type}}|{{basic_type}}}}
                    {{else}}
                        {{#set NORMALIZATION_RAW_DATA={{basic_type}}|{{basic_type}}}}
                    {{/if}}
                {{/if}}
            {{/if}}
        {{/for}}
    {{/if}}

    {{! Process NORMALIZATION_RAW_DATA to generate NORMALIZATION_MACROS_DATA }}
    {{#set NORMALIZATION_MACROS_DATA=}}
    {{#if NORMALIZATION_RAW_DATA}}
        {{NORMALIZATION_RAW_DATA|split|newline}}
        {{#for entry in NORMALIZATION_RAW_DATA}}
            {{#if entry}}
                {{entry|split|pipe}}
                {{#set macro_key={{entry|part|0}}}}
                {{#set base_type={{entry|part|1}}}}
                
                {{#set normalized_key={{macro_key|normalize_name}}}}
                {{#set escaped_base_key={{base_type|escape_name}}}}
                {{#set escaped_original_key={{macro_key|normalize_name}}}}
                {{#set escaped_macro_key_for_escape={{macro_key|trim|normalize_name}}}}
                {{#set escaped_macro_key_for_raw_type={{macro_key|trim|escape_char|*|_STAR}}}}
                {{#set escaped_macro_key_for_raw_type={{escaped_macro_key_for_raw_type|sanitize_name}}}}
                
                {{#if NORMALIZATION_MACROS_DATA}}
                    {{#set NORMALIZATION_MACROS_DATA={{NORMALIZATION_MACROS_DATA}}
{{macro_key}}|{{base_type}}|{{normalized_key}}|{{escaped_base_key}}|{{escaped_original_key}}|{{escaped_macro_key_for_escape}}|{{escaped_macro_key_for_raw_type}}}}
                {{else}}
                    {{#set NORMALIZATION_MACROS_DATA={{macro_key}}|{{base_type}}|{{normalized_key}}|{{escaped_base_key}}|{{escaped_original_key}}|{{escaped_macro_key_for_escape}}|{{escaped_macro_key_for_raw_type}}}}
                {{/if}}
            {{/if}}
        {{/for}}
    {{/if}}

    {{! Process NORMALIZATION_RAW_DATA to generate TYPE_TO_SELECT_DATA }}
    {{#set TYPE_TO_SELECT_DATA=}}
    {{#if NORMALIZATION_RAW_DATA}}
        {{NORMALIZATION_RAW_DATA|split|newline}}
        {{#for entry in NORMALIZATION_RAW_DATA}}
            {{#if entry}}
                {{entry|split|pipe}}
                {{#set macro_key={{entry|part|0}}}}
                {{#set base_type={{entry|part|1}}}}
                
                {{#set normalized_key={{macro_key|normalize_name}}}}
                {{#set selector_name=_DAP_MOCK_WRAPPER_CUSTOM_SELECT_{{normalized_key}}}}
                {{#set is_pointer=0}}
                {{#if macro_key contains *}}
                    {{#set is_pointer=1}}
                {{/if}}
                
                {{#if TYPE_TO_SELECT_DATA}}
                    {{#set TYPE_TO_SELECT_DATA={{TYPE_TO_SELECT_DATA}}
{{macro_key}}|{{normalized_key}}|{{selector_name}}|{{is_pointer}}}}
                {{else}}
                    {{#set TYPE_TO_SELECT_DATA={{macro_key}}|{{normalized_key}}|{{selector_name}}|{{is_pointer}}}}
                {{/if}}
            {{/if}}
        {{/for}}
    {{/if}}

    {{! Process BASIC_TYPES_RAW_DATA to generate BASIC_TYPES_DATA }}
    {{#set BASIC_TYPES_DATA=}}
    {{#if BASIC_TYPES_RAW_DATA}}
        {{BASIC_TYPES_RAW_DATA|split|newline}}
        {{#for entry in BASIC_TYPES_RAW_DATA}}
            {{#if entry}}
                {{entry|split|pipe}}
                {{#set basic_type={{entry|part|0}}}}
                {{#set original_type={{entry|part|1}}}}
                
                {{#set normalized_key={{basic_type|normalize_name}}}}
                {{#set escaped_base_key={{basic_type|normalize_name}}}}
                {{#set selector_name=_DAP_MOCK_WRAPPER_CUSTOM_SELECT_{{normalized_key}}}}
                
                {{#if BASIC_TYPES_DATA}}
                    {{#set BASIC_TYPES_DATA={{BASIC_TYPES_DATA}}
{{basic_type}}|{{normalized_key}}|{{escaped_base_key}}|{{selector_name}}}}
                {{else}}
                    {{#set BASIC_TYPES_DATA={{basic_type}}|{{normalized_key}}|{{escaped_base_key}}|{{selector_name}}}}
                {{/if}}
            {{/if}}
        {{/for}}
    {{/if}}
{{/if}}

// ============================================================================
// SELECT macros - route to void/non-void implementation
// ============================================================================

{{#if SELECT_MACROS_DATA}}
    {{SELECT_MACROS_DATA|split|newline}}
    {{#for entry in SELECT_MACROS_DATA}}
        {{#if entry}}
            {{entry|split|pipe}}
            {{#set macro_name={{entry|part|0}}}}
            {{#set original_type={{entry|part|1}}}}
            {{#set is_void={{entry|part|2}}}}
            
            {{#if is_void == 1}}
// Specialized macro for return type: {{original_type}} (void)
#define _DAP_MOCK_WRAPPER_CUSTOM_SELECT_{{macro_name}}(func_name, return_type_full, ...) \
    _DAP_MOCK_WRAPPER_CUSTOM_VOID(func_name, __VA_ARGS__)
            {{else}}
// Specialized macro for return type: {{original_type}} (non-void)
#define _DAP_MOCK_WRAPPER_CUSTOM_SELECT_{{macro_name}}(func_name, return_type_full, ...) \
    _DAP_MOCK_WRAPPER_CUSTOM_NONVOID(func_name, return_type_full, ##__VA_ARGS__)
            {{/if}}
            
        {{/if}}
    {{/for}}
{{/if}}

// ============================================================================
// Type normalization macros
// ============================================================================
// These macros normalize pointer types by removing * from type names.

{{#if NORMALIZATION_MACROS_DATA}}
    {{NORMALIZATION_MACROS_DATA|split|newline}}
    {{#for entry in NORMALIZATION_MACROS_DATA}}
        {{#if entry}}
            {{entry|split|pipe}}
            {{#set macro_key={{entry|part|0}}}}
            {{#set base_type={{entry|part|1}}}}
            {{#set normalized_key={{entry|part|2}}}}
            {{#set escaped_base_key={{entry|part|3}}}}
            {{#set escaped_original_key={{entry|part|4}}}}
            {{#set escaped_macro_key_for_escape={{entry|part|5}}}}
            {{#set escaped_macro_key_for_raw_type={{entry|part|6}}}}
            
            {{#if macro_key contains *}}
// Normalize pointer type: {{escaped_base_key}} -> {{normalized_key}}
#define _DAP_MOCK_NORMALIZE_TYPE_{{escaped_base_key}} {{normalized_key}}

// Transform type helper (removes *): {{escaped_macro_key_for_raw_type}} -> {{escaped_base_key}}
#define _DAP_MOCK_TRANSFORM_TYPE_HELPER_{{escaped_macro_key_for_raw_type}} {{escaped_base_key}}

// Extract base from pointer: {{escaped_macro_key_for_raw_type}} -> {{escaped_base_key}}
#define _DAP_MOCK_BASE_{{escaped_macro_key_for_raw_type}} {{escaped_base_key}}

// Escape type for dispatcher: {{macro_key}} -> {{escaped_macro_key_for_escape}}
#define _DAP_MOCK_ESCAPE_TYPE_FOR_DISPATCHER_{{escaped_macro_key_for_escape}} {{escaped_original_key}}

// Dispatcher macro for pointer types - does entire chain: extract base -> normalize -> select
// Format: _DAP_MOCK_WRAPPER_CUSTOM_SELECT_DISPATCHER_{{escaped_macro_key_for_escape}}(func_name, return_type_full, ...)
#define _DAP_MOCK_WRAPPER_CUSTOM_SELECT_DISPATCHER_{{escaped_macro_key_for_escape}}(func_name, return_type_full, ...) \
    _DAP_MOCK_TYPE_TO_SELECT_NAME_{{normalized_key}}(func_name, return_type_full, ##__VA_ARGS__)
            {{else}}
// Normalize type: {{escaped_base_key}} -> {{normalized_key}}
#define _DAP_MOCK_NORMALIZE_TYPE_{{escaped_base_key}} {{normalized_key}}

// Transform type helper: {{escaped_base_key}} -> {{escaped_base_key}} (pass-through)
#define _DAP_MOCK_TRANSFORM_TYPE_HELPER_{{escaped_base_key}} {{escaped_base_key}}

// Extract base: {{escaped_base_key}} -> {{escaped_base_key}} (pass-through)
#define _DAP_MOCK_BASE_{{escaped_base_key}} {{escaped_base_key}}

// Escape type for dispatcher: {{macro_key}} -> {{escaped_macro_key_for_escape}}
#define _DAP_MOCK_ESCAPE_TYPE_FOR_DISPATCHER_{{escaped_macro_key_for_escape}} {{escaped_original_key}}

// Dispatcher macro for non-pointer types - does entire chain: extract base -> normalize -> select
// Format: _DAP_MOCK_WRAPPER_CUSTOM_SELECT_DISPATCHER_{{escaped_macro_key_for_escape}}(func_name, return_type_full, ...)
#define _DAP_MOCK_WRAPPER_CUSTOM_SELECT_DISPATCHER_{{escaped_macro_key_for_escape}}(func_name, return_type_full, ...) \
    _DAP_MOCK_TYPE_TO_SELECT_NAME_{{normalized_key}}(func_name, return_type_full, ##__VA_ARGS__)
            {{/if}}
        {{/if}}
    {{/for}}
{{/if}}

// ============================================================================
// Type-to-selector wrapper macros
// ============================================================================

{{#if TYPE_TO_SELECT_DATA}}
    {{TYPE_TO_SELECT_DATA|split|newline}}
    {{#for entry in TYPE_TO_SELECT_DATA}}
        {{#if entry}}
            {{entry|split|pipe}}
            {{#set macro_key={{entry|part|0}}}}
            {{#set normalized_key={{entry|part|1}}}}
            {{#set selector_name={{entry|part|2}}}}
            {{#set is_pointer={{entry|part|3}}}}
            
            {{#if is_pointer == 1}}
// Type-to-selector wrapper for normalized type {{normalized_key}} (from {{macro_key}}) -> calls {{selector_name}}
            {{else}}
// Type-to-selector wrapper for: {{normalized_key}} -> calls {{selector_name}}
            {{/if}}
#define _DAP_MOCK_TYPE_TO_SELECT_NAME_{{normalized_key}}(func_name, return_type_full, ...) \
    {{selector_name}}(func_name, return_type_full, ##__VA_ARGS__)
            
        {{/if}}
    {{/for}}
{{/if}}

{{#if BASIC_TYPES_DATA}}
    {{BASIC_TYPES_DATA|split|newline}}
    {{#for entry in BASIC_TYPES_DATA}}
        {{#if entry}}
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
            
        {{/if}}
    {{/for}}
{{/if}}

// ============================================================================
// Void/Non-void implementation macros
// ============================================================================
// _DAP_MOCK_WRAPPER_CUSTOM_VOID and _DAP_MOCK_WRAPPER_CUSTOM_NONVOID are defined in dap_mock_linker_wrapper.h
// These are the final implementation macros that do the actual wrapping
// They expect properly normalized and escaped type names from the chain above


