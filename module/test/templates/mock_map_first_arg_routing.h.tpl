// ============================================================================
// Direct macros for first_arg handling (param_count=0 case) - Universal template
// ============================================================================
// Handles special case when param_count=0: need to check first_arg for "void"
// "void" is a fixed C keyword, so we generate the check directly

// Routing macro for first_arg - uses exact parameter matching
#define _DAP_MOCK_MAP_CHECK_VOID_BY_PARAM_COUNT_0_CHECK(first_arg, macro, ...) \
    _DAP_MOCK_MAP_CHECK_VOID_BY_PARAM_COUNT_0_CHECK_ROUTE(first_arg, macro, __VA_ARGS__)

// Two-level expansion to ensure first_arg is fully expanded
#define _DAP_MOCK_MAP_CHECK_VOID_BY_PARAM_COUNT_0_CHECK_ROUTE(first_arg, macro, ...) \
    _DAP_MOCK_MAP_CHECK_VOID_BY_PARAM_COUNT_0_CHECK_ROUTE_EXPAND(first_arg, macro, __VA_ARGS__)
#define _DAP_MOCK_MAP_CHECK_VOID_BY_PARAM_COUNT_0_CHECK_ROUTE_EXPAND(first_arg, macro, ...) \
    _DAP_MOCK_MAP_CHECK_VOID_BY_PARAM_COUNT_0_CHECK_ROUTE_IMPL_##first_arg(first_arg, macro, __VA_ARGS__)

// Routing macro for first_arg="void" - "void" is a fixed C keyword
// Uses ##macro to dispatch to specific implementation for the macro being mapped
#define _DAP_MOCK_MAP_CHECK_VOID_BY_PARAM_COUNT_0_CHECK_ROUTE_IMPL_void(void, macro, ...) \
    _DAP_MOCK_MAP_CHECK_VOID_BY_PARAM_COUNT_0_CHECK_void_BY_MACRO_##macro()

// Routing macro for empty first_arg case (when ##first_arg results in empty suffix)
// This handles the case where first_arg is empty
#define _DAP_MOCK_MAP_CHECK_VOID_BY_PARAM_COUNT_0_CHECK_ROUTE_IMPL_(empty, macro, ...) \
    _DAP_MOCK_MAP_0(macro)

// Generate routing macros for known macro values when first_arg=void
// We use ##macro dispatching because macro names are safe identifiers
#define _DAP_MOCK_MAP_CHECK_VOID_BY_PARAM_COUNT_0_CHECK_void_BY_MACRO__DAP_MOCK_PARAM_DECL() void
#define _DAP_MOCK_MAP_CHECK_VOID_BY_PARAM_COUNT_0_CHECK_void_BY_MACRO__DAP_MOCK_PARAM_NAME()
#define _DAP_MOCK_MAP_CHECK_VOID_BY_PARAM_COUNT_0_CHECK_void_BY_MACRO__DAP_MOCK_PARAM_CAST()
