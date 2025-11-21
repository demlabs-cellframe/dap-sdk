// ============================================================================
// Generated dispatcher macros for DAP_MOCK_WRAPPER_CUSTOM
// ============================================================================
// Generator creates dispatcher macros for ALL types directly using dap_tpl.
// NO ## USED - generator creates all macro names directly, including types with *
// NO FALLBACKS - if type not found, compilation will fail (Fail-Fast principle)
//
// For each type found in code, generator creates:
// - _DAP_MOCK_WRAPPER_CUSTOM_DISPATCH_<normalized_type>(func_name, return_type_full, ...)
//
// Example: for "dap_client_http_t*", generator creates:
// - _DAP_MOCK_WRAPPER_CUSTOM_DISPATCH_dap_client_http_t_STAR(func_name, return_type_full, ...)
//
// For "void", generator creates:
// - _DAP_MOCK_WRAPPER_CUSTOM_DISPATCH_void(func_name, return_type_full, ...)
//
// All dispatchers route to appropriate void/non-void implementations directly.

{{#if ORIGINAL_TYPES_DATA}}
    {{#for entry in ORIGINAL_TYPES_DATA|newline}}
        {{entry|split|pipe}}
        {{#set normalized_type={{entry|part|0|trim}}}}
        {{#set original_type={{entry|part|1|trim}}}}
        
        {{#if normalized_type}}
            {{#if original_type}}
                // Dispatcher for type: {{original_type}} (normalized: {{normalized_type}})
                
                {{#if normalized_type == 'void'}}
                    // Special handling for void
                    // Only generate once for void (avoid duplicates)
                    {{#if original_type == 'void'}}
                        #define _DAP_MOCK_WRAPPER_CUSTOM_DISPATCH_void_VOID_HELPER(func_name, ignored, ...) \
                            _DAP_MOCK_WRAPPER_CUSTOM_VOID(func_name, __VA_ARGS__)
                        #define _DAP_MOCK_WRAPPER_CUSTOM_DISPATCH_void(func_name, return_type_full, ...) \
                            _DAP_MOCK_WRAPPER_CUSTOM_DISPATCH_void_VOID_HELPER(func_name, return_type_full, __VA_ARGS__)
                    {{/if}}
                {{else}}
                    // Always generate normalized dispatcher for non-void types
                    // If original_type == normalized_type, this is the only macro needed
                    // If different, this is the target for the mapping
                    {{#if original_type == normalized_type}}
                        #define _DAP_MOCK_WRAPPER_CUSTOM_DISPATCH_{{normalized_type}}(func_name, return_type_full, ...) \
                            _DAP_MOCK_WRAPPER_CUSTOM_NONVOID(return_type_full, func_name, __VA_ARGS__)
                    {{else}}
                        // If types differ (e.g. pointer *), we need the normalized version first
                        // But we can't check if it was already generated in a previous iteration
                        // So we generate it here if we are sure it won't cause duplicates
                        // But duplicates are possible if multiple original types map to same normalized type
                        // So we should generate normalized dispatchers in a separate pass or loop?
                        
                        // Strategy: Generate normalized dispatcher ONLY if original == normalized.
                        // But wait, if we have "int|int" AND "int*|int_STAR", we need both.
                        // "int" will generate "_DISPATCH_int".
                        // "int*" will generate mapping "_DISPATCH_int* -> _DISPATCH_int_STAR".
                        // AND we need "_DISPATCH_int_STAR".
                        
                        // So we need to generate "_DISPATCH_int_STAR".
                        // Does ORIGINAL_TYPES_DATA contain "int_STAR|int_STAR"? No.
                        
                        // So for every entry, we MUST generate the normalized dispatcher!
                        // But if we have duplicates (e.g. multiple types mapping to same normalized), we get redefinition.
                        
                        // We can wrap in #ifndef to prevent redefinition?
                        #ifndef _DAP_MOCK_WRAPPER_CUSTOM_DISPATCH_{{normalized_type}}_DEFINED
                        #define _DAP_MOCK_WRAPPER_CUSTOM_DISPATCH_{{normalized_type}}_DEFINED
                        #define _DAP_MOCK_WRAPPER_CUSTOM_DISPATCH_{{normalized_type}}(func_name, return_type_full, ...) \
                            _DAP_MOCK_WRAPPER_CUSTOM_NONVOID(return_type_full, func_name, __VA_ARGS__)
                        #endif
                    {{/if}}
                {{/if}}
                
                // Generate mapping if original type differs from normalized
                {{#if original_type != normalized_type}}
                    // Mapping for {{original_type}} -> {{normalized_type}}
                    // Note: We cannot define a macro with * in the name in C preprocessor!
                    // So we actually CANNOT generate mapping macros for pointer types.
                    // The dispatcher logic must use the NORMALIZED type name constructed via ##.
                {{/if}}
            {{/if}}
        {{/if}}
    {{/for}}
{{/if}}
