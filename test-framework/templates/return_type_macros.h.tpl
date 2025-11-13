// ============================================================================
// Generated specialized macros for return types
// ============================================================================
// These macros are generated based on actual return types found in the code.
// Each macro routes to the appropriate void/non-void implementation.
// ALL return types MUST have a generated macro - no fallbacks are provided.

{{SELECTOR_MACROS}}

// ============================================================================
// Generated type normalization macros
// ============================================================================
// These macros normalize pointer types by removing * from type names.
// For example: _DAP_MOCK_STRIP_PTR_dap_client_http_t* expands to dap_client_http_t
// For non-pointer types, these macros provide pass-through (type -> type)

{{NORMALIZATION_MACROS}}

// ============================================================================
// Type-to-selector wrapper macros
// ============================================================================
// These macros wrap selector macros and handle type normalization.
// All type normalization logic is handled here - the header just uses these wrappers.

{{TYPE_TO_SELECT_MACROS}}
