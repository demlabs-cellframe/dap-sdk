// ============================================================================
// Generated specialized macros for return types
// ============================================================================
// These macros are generated based on actual return types found in the code.
// Each macro routes to the appropriate void/non-void implementation.
// ALL return types MUST have a generated macro - no fallbacks are provided.

// ============================================================================
// Process raw data using AWK sections - template does all processing
// ============================================================================

{{#if RETURN_TYPES}}
    {{#set SELECT_MACROS_DATA={{AWK:
BEGIN {
    FS = "|"
    # Process RETURN_TYPES and ORIGINAL_TYPES_DATA to generate SELECT macros
    # RETURN_TYPES is space-separated list of normalized types
    # ORIGINAL_TYPES_DATA is newline-separated normalized|original pairs
    
    # Build ORIGINAL_TYPES map from ORIGINAL_TYPES_DATA
    split(ENVIRON["ORIGINAL_TYPES_DATA"], orig_lines, "\n")
    for (i in orig_lines) {
        if (orig_lines[i] == "") continue
        split(orig_lines[i], parts, "|")
        if (length(parts) == 2) {
            ORIGINAL_TYPES[parts[1]] = parts[2]
        }
    }
    
    # Process RETURN_TYPES
    split(ENVIRON["RETURN_TYPES"], return_types, " ")
    TYPE_NORMALIZATION_TABLE["_Bool"] = "bool"
    GENERATED_MACROS["void"] = "void"
    
    for (i in return_types) {
        normalized_type = return_types[i]
        if (normalized_type == "") continue
        
        macro_name = normalized_type
        gsub(/[^a-zA-Z0-9_]/, "_", macro_name)
        GENERATED_MACROS[macro_name] = normalized_type
        
        # Handle type expansion
        for (expanded_type in TYPE_NORMALIZATION_TABLE) {
            if (TYPE_NORMALIZATION_TABLE[expanded_type] == normalized_type) {
                expanded_macro_name = expanded_type
                gsub(/[^a-zA-Z0-9_]/, "_", expanded_macro_name)
                GENERATED_MACROS[expanded_macro_name] = normalized_type
            }
        }
        
        # Handle pointer types
        if (normalized_type ~ /_PTR$/) {
            base_type = normalized_type
            sub(/_PTR$/, "", base_type)
            base_macro_name = base_type
            gsub(/[^a-zA-Z0-9_]/, "_", base_macro_name)
            if (base_macro_name != macro_name) {
                GENERATED_MACROS[base_macro_name] = normalized_type
            }
        }
    }
    
    # Add basic types
    split(ENVIRON["BASIC_TYPES_RAW_DATA"], basic_lines, "\n")
    for (i in basic_lines) {
        if (basic_lines[i] == "") continue
        split(basic_lines[i], parts, "|")
        if (length(parts) == 2) {
            basic_type = parts[1]
            macro_name = basic_type
            gsub(/[^a-zA-Z0-9_]/, "_", macro_name)
            if (!(macro_name in GENERATED_MACROS)) {
                GENERATED_MACROS[macro_name] = basic_type
            }
        }
    }
    
    # Generate SELECT macros data
    for (macro_name in GENERATED_MACROS) {
        original_type = GENERATED_MACROS[macro_name]
        is_void = (macro_name == "void") ? "1" : "0"
        print macro_name "|" original_type "|" is_void
    }
}}
    }}
    
    {{#set NORMALIZATION_RAW_DATA={{AWK:
BEGIN {
    FS = "|"
    # Process RETURN_TYPES and ORIGINAL_TYPES_DATA to generate normalization data
    split(ENVIRON["ORIGINAL_TYPES_DATA"], orig_lines, "\n")
    for (i in orig_lines) {
        if (orig_lines[i] == "") continue
        split(orig_lines[i], parts, "|")
        if (length(parts) == 2) {
            normalized_type = parts[1]
            original_type = parts[2]
            
            macro_key = original_type
            if (original_type ~ /\*/) {
                base_type = original_type
                gsub(/\*/, "", base_type)
                gsub(/[ \t]+$/, "", base_type)
                NORMALIZATION_MACROS[macro_key] = base_type
            } else {
                NORMALIZATION_MACROS[macro_key] = original_type
            }
        }
    }
    
    # Add basic types
    split(ENVIRON["BASIC_TYPES_RAW_DATA"], basic_lines, "\n")
    for (i in basic_lines) {
        if (basic_lines[i] == "") continue
        split(basic_lines[i], parts, "|")
        if (length(parts) == 2) {
            basic_type = parts[1]
            if (!(basic_type in NORMALIZATION_MACROS)) {
                NORMALIZATION_MACROS[basic_type] = basic_type
            }
        }
    }
    
    # Output normalization data
    for (macro_key in NORMALIZATION_MACROS) {
        base_type = NORMALIZATION_MACROS[macro_key]
        print macro_key "|" base_type
    }
}}
    }}
    
    {{#set NORMALIZATION_MACROS_DATA={{AWK:
BEGIN {
    FS = "|"
    # Include AWK script functions
    @include "test-framework/mocks/lib/awk/prepare_normalization_data.awk"
}
# Process NORMALIZATION_RAW_DATA from environment
{
    if (NF == 2) {
        macro_key = $1
        base_type = $2
        if (macro_key == "" || base_type == "") next
        
        normalized_key = normalize_type(macro_key)
        escaped_base_key = escape_type(base_type)
        escaped_original_key = normalize_type(macro_key)
        escaped_macro_key_for_escape = escape_macro_key_for_escape(macro_key)
        escaped_macro_key_for_raw_type = escape_macro_key_for_raw_type(macro_key)
        
        print macro_key "|" base_type "|" normalized_key "|" escaped_base_key "|" escaped_original_key "|" escaped_macro_key_for_escape "|" escaped_macro_key_for_raw_type
    }
}
    }}}}
    
    {{#set TYPE_TO_SELECT_DATA={{AWK:
BEGIN {
    FS = "|"
    # Include AWK script functions
    @include "test-framework/mocks/lib/awk/prepare_type_to_select_data.awk"
}
# Process NORMALIZATION_RAW_DATA from environment
{
    if (NF == 2) {
        macro_key = $1
        base_type = $2
        if (macro_key == "" || base_type == "") next
        
        normalized_key = normalize_type(macro_key)
        selector_name = "_DAP_MOCK_WRAPPER_CUSTOM_SELECT_" normalized_key
        is_pointer = (macro_key ~ /\*/) ? "1" : "0"
        
        print macro_key "|" normalized_key "|" selector_name "|" is_pointer
        
        # Handle type expansion
        TYPE_NORMALIZATION_TABLE["_Bool"] = "bool"
        for (expanded_type in TYPE_NORMALIZATION_TABLE) {
            if (TYPE_NORMALIZATION_TABLE[expanded_type] == base_type) {
                expanded_macro_name = expanded_type
                gsub(/[^a-zA-Z0-9_]/, "_", expanded_macro_name)
                expanded_normalized_key = normalize_type(expanded_type)
                expanded_selector_name = "_DAP_MOCK_WRAPPER_CUSTOM_SELECT_" expanded_normalized_key
                print expanded_type "|" expanded_normalized_key "|" expanded_selector_name "|0"
            }
        }
    }
}
    }}}}
    
    {{#set BASIC_TYPES_DATA={{AWK:
BEGIN {
    FS = "|"
    # Include AWK script functions
    @include "test-framework/mocks/lib/awk/prepare_basic_types_data.awk"
}
# Process BASIC_TYPES_RAW_DATA from environment
{
    if (NF == 2) {
        basic_type = $1
        original_type = $2
        if (basic_type == "" || original_type == "") next
        
        normalized_key = normalize_type(basic_type)
        escaped_base_key = normalize_type(basic_type)
        selector_name = "_DAP_MOCK_WRAPPER_CUSTOM_SELECT_" normalized_key
        
        print basic_type "|" normalized_key "|" escaped_base_key "|" selector_name
    }
}
    }}}}
{{/if}}

// ============================================================================
// SELECT macros - route to void/non-void implementation
// ============================================================================

{{#if SELECT_MACROS_DATA}}
    {{#for entry in SELECT_MACROS_DATA}}
        {{entry|split|pipe}}
        {{#set macro_name={{entry|part|0}}}}
        {{#set original_type={{entry|part|1}}}}
        {{#set is_void={{entry|part|2}}}}
        
        {{#if macro_name}}
            // Specialized macro for return type: {{original_type}}
            {{#if is_void|eq|1}}
                #define _DAP_MOCK_WRAPPER_CUSTOM_SELECT_{{macro_name}}(func_name, return_type_full, ...) \
                    _DAP_MOCK_WRAPPER_CUSTOM_VOID(func_name, __VA_ARGS__)
            {{else}}
                #define _DAP_MOCK_WRAPPER_CUSTOM_SELECT_{{macro_name}}(func_name, return_type_full, ...) \
                    _DAP_MOCK_WRAPPER_CUSTOM_NONVOID(return_type_full, func_name, ##__VA_ARGS__)
            {{/if}}
        {{/if}}
        
    {{/for}}
{{/if}}

// ============================================================================
// Type normalization macros
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

// ============================================================================
// Basic types macros
// ============================================================================

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
