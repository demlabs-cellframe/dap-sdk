// ============================================================================
// Generated function-specific wrapper macros for DAP_MOCK_WRAPPER_CUSTOM
// ============================================================================
// Both platforms define __wrap_func_name as the mock implementation.
// On Linux: GNU ld --wrap=func redirects calls and provides __real_ automatically.
// On macOS: DYLD_INSERT interpose dylib redirects calls and provides __real_ via dlsym.
//
// For each function, generator creates:
// - _DAP_MOCK_WRAPPER_CUSTOM_FOR_<func_name>(return_type, ...)

{{#if FUNCTIONS_DATA}}
    {{#for entry in FUNCTIONS_DATA|newline}}
        {{entry|split|pipe}}
        {{#set return_type={{entry|part|0}}}}
        {{#set func_name={{entry|part|1}}}}
        {{#set param_count={{entry|part|4}}}}
        
        {{#if func_name}}
            // ============================================================================
            // Wrapper for function: {{func_name}}
            // Return type: {{return_type}}
            // ============================================================================
            {{#if return_type == 'void'}}
                // Void function wrapper
                // param_count={{param_count}}
                #define _DAP_MOCK_WRAPPER_CUSTOM_FOR_{{func_name}}(return_type_full, ...) \
                    extern void __real_{{func_name}}(_DAP_MOCK_MAP_{{param_count}}(_DAP_MOCK_PARAM_DECL, ##__VA_ARGS__)); \
                    static void __mock_impl_{{func_name}}(_DAP_MOCK_MAP_{{param_count}}(_DAP_MOCK_PARAM_DECL, ##__VA_ARGS__)); \
                    void __wrap_{{func_name}}(_DAP_MOCK_MAP_{{param_count}}(_DAP_MOCK_PARAM_DECL, ##__VA_ARGS__)) { \
                        void **__wrap_args = ((void*[]){_DAP_MOCK_MAP_{{param_count}}(_DAP_MOCK_PARAM_CAST, ##__VA_ARGS__)}); \
                        int __wrap_args_count = {{param_count}}; \
                        bool __wrap_mock_enabled = dap_mock_prepare_call(g_mock_{{func_name}}, __wrap_args, __wrap_args_count); \
                        if (__wrap_mock_enabled) { \
                            __mock_impl_{{func_name}}(_DAP_MOCK_MAP_{{param_count}}(_DAP_MOCK_PARAM_NAME, ##__VA_ARGS__)); \
                        } else { \
                            __real_{{func_name}}(_DAP_MOCK_MAP_{{param_count}}(_DAP_MOCK_PARAM_NAME, ##__VA_ARGS__)); \
                        } \
                    } \
                    static void __mock_impl_{{func_name}}(_DAP_MOCK_MAP_{{param_count}}(_DAP_MOCK_PARAM_DECL, ##__VA_ARGS__))
            {{else}}
                // Non-void function wrapper
                // param_count={{param_count}}
                #define _DAP_MOCK_WRAPPER_CUSTOM_FOR_{{func_name}}(return_type_full, ...) \
                    extern return_type_full __real_{{func_name}}(_DAP_MOCK_MAP_{{param_count}}(_DAP_MOCK_PARAM_DECL, ##__VA_ARGS__)); \
                    static return_type_full __mock_impl_{{func_name}}(_DAP_MOCK_MAP_{{param_count}}(_DAP_MOCK_PARAM_DECL, ##__VA_ARGS__)); \
                    return_type_full __wrap_{{func_name}}(_DAP_MOCK_MAP_{{param_count}}(_DAP_MOCK_PARAM_DECL, ##__VA_ARGS__)) { \
                        void **__wrap_args = ((void*[]){_DAP_MOCK_MAP_{{param_count}}(_DAP_MOCK_PARAM_CAST, ##__VA_ARGS__)}); \
                        int __wrap_args_count = {{param_count}}; \
                        return_type_full __wrap_result = (return_type_full){0}; \
                        bool __wrap_mock_enabled = dap_mock_prepare_call(g_mock_{{func_name}}, __wrap_args, __wrap_args_count); \
                        if (__wrap_mock_enabled) { \
                            __wrap_result = __mock_impl_{{func_name}}(_DAP_MOCK_MAP_{{param_count}}(_DAP_MOCK_PARAM_NAME, ##__VA_ARGS__)); \
                            dap_mock_function_state_t *__wrap_mock_state = g_mock_{{func_name}}; \
                            if (__wrap_mock_state) { \
                                void *__wrap_override = __wrap_mock_state->return_value.ptr; \
                                if (__wrap_override != NULL) { \
                                    __wrap_result = (return_type_full)(intptr_t)__wrap_override; \
                                } \
                            } \
                            dap_mock_record_call(__wrap_mock_state, __wrap_args, __wrap_args_count, (void*)(intptr_t)__wrap_result); \
                        } else { \
                            __wrap_result = __real_{{func_name}}(_DAP_MOCK_MAP_{{param_count}}(_DAP_MOCK_PARAM_NAME, ##__VA_ARGS__)); \
                        } \
                        return __wrap_result; \
                    } \
                    static return_type_full __mock_impl_{{func_name}}(_DAP_MOCK_MAP_{{param_count}}(_DAP_MOCK_PARAM_DECL, ##__VA_ARGS__))
            {{/if}}
        {{/if}}
    {{/for}}
{{/if}}
