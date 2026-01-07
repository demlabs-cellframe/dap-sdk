/**
 * @file dap_json_stage1.h
 * @brief Stage 1: Structural Indexing - Internal API
 * 
 * Stage 1 выполняет быстрое сканирование JSON буфера и создаёт массив
 * structural indices - позиций важных символов ({, }, [, ], :, ,).
 * 
 * Reference implementation (pure C) служит baseline для correctness
 * и будет использоваться для верификации SIMD implementations.
 * 
 * @author DAP SDK Team
 * @date 2025-01-07
 */

#ifndef DAP_JSON_STAGE1_H
#define DAP_JSON_STAGE1_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Character classification types
 * 
 * Каждый байт в JSON классифицируется в одну из категорий.
 * SIMD implementations будут создавать bitmap для каждого класса.
 */
typedef enum {
    CHAR_CLASS_STRUCTURAL = 0,  /**< {, }, [, ], :, , - structural characters */
    CHAR_CLASS_WHITESPACE = 1,  /**< space, tab, \r, \n */
    CHAR_CLASS_QUOTE = 2,       /**< " - string delimiter */
    CHAR_CLASS_BACKSLASH = 3,   /**< \ - escape character */
    CHAR_CLASS_DIGIT = 4,       /**< 0-9 - number start/continuation */
    CHAR_CLASS_MINUS = 5,       /**< - - negative number */
    CHAR_CLASS_PLUS = 6,        /**< + - exponent sign */
    CHAR_CLASS_LETTER = 7,      /**< t, f, n - true/false/null */
    CHAR_CLASS_OTHER = 8        /**< all other characters */
} dap_json_char_class_t;

/**
 * @brief Structural character types
 * 
 * Детальная классификация structural characters для Stage 2.
 */
typedef enum {
    STRUCT_CHAR_OPEN_BRACE = '{',    /**< { - object start */
    STRUCT_CHAR_CLOSE_BRACE = '}',   /**< } - object end */
    STRUCT_CHAR_OPEN_BRACKET = '[',  /**< [ - array start */
    STRUCT_CHAR_CLOSE_BRACKET = ']', /**< ] - array end */
    STRUCT_CHAR_COLON = ':',         /**< : - key-value separator */
    STRUCT_CHAR_COMMA = ','          /**< , - element separator */
} dap_json_struct_char_t;

/**
 * @brief Token type (Phase 1.3 enhancement)
 * 
 * Stage 1 теперь индексирует не только structural characters,
 * но и все JSON values (strings, numbers, literals).
 */
typedef enum {
    TOKEN_TYPE_STRUCTURAL = 0,  /**< Structural character: { } [ ] : , */
    TOKEN_TYPE_STRING = 1,      /**< String value: "..." */
    TOKEN_TYPE_NUMBER = 2,      /**< Number value: 123, -45.67, 1.2e+10 */
    TOKEN_TYPE_LITERAL = 3      /**< Literal: true, false, null */
} dap_json_token_type_t;

/**
 * @brief Structural index entry (Phase 1.3 enhanced)
 * 
 * Универсальный token descriptor для всех JSON elements.
 * 
 * Phase 1.3: Добавлены поля length и type для support value tokens.
 * 
 * Размер: 12 bytes (cache-friendly, aligned to 4 bytes)
 * 
 * Examples:
 * 
 * Input: [123, "hello"]
 * 
 * Indices:
 * { position: 0, length: 0, type: STRUCTURAL, character: '[' }
 * { position: 1, length: 3, type: NUMBER, character: 0 }      // "123"
 * { position: 4, length: 0, type: STRUCTURAL, character: ',' }
 * { position: 6, length: 7, type: STRING, character: 0 }      // "hello" with quotes
 * { position: 13, length: 0, type: STRUCTURAL, character: ']' }
 */
typedef struct {
    uint32_t position;  /**< Byte offset in input buffer (start of token) */
    uint32_t length;    /**< Token length in bytes (0 for structural chars) */
    uint8_t type;       /**< Token type (dap_json_token_type_t) */
    uint8_t character;  /**< For structural: actual char, for values: subtype/flags */
    uint16_t _reserved; /**< Reserved for future use (alignment) */
} dap_json_struct_index_t;

/**
 * @brief Stage 1 parser state
 * 
 * Внутреннее состояние Stage 1 parser.
 * Содержит input buffer, structural indices (enhanced с value tokens), и parser state.
 * 
 * Phase 1.3: Добавлены поля для value detection.
 */
typedef struct {
    /* Input */
    const uint8_t *input;       /**< Input JSON buffer (not owned) */
    size_t input_len;           /**< Input buffer length in bytes */
    
    /* Structural indices array (enhanced - теперь включает value tokens) */
    dap_json_struct_index_t *indices;  /**< Token array (structural + values) */
    size_t indices_capacity;    /**< Allocated capacity */
    size_t indices_count;       /**< Number of tokens found */
    
    /* Parser state */
    size_t current_pos;         /**< Current position in input */
    bool in_string;             /**< Are we inside a string? */
    bool escape_next;           /**< Is next char escaped? */
    
    /* Value detection state (Phase 1.3) */
    bool in_number;             /**< Are we inside a number? */
    uint32_t number_start;      /**< Start position of current number */
    bool number_has_decimal;    /**< Number has decimal point? */
    bool number_has_exponent;   /**< Number has exponent? */
    
    uint32_t string_start;      /**< Start position of current string (opening quote) */
    uint32_t value_start;       /**< Start position of current literal value */
    
    /* Statistics (for profiling) */
    size_t string_count;        /**< Number of strings found (Phase 1.3) */
    size_t number_count;        /**< Number of numbers found (Phase 1.3) */
    size_t literal_count;       /**< Number of literals found (Phase 1.3) */
    size_t string_chars;        /**< Number of chars in strings */
    size_t whitespace_chars;    /**< Number of whitespace chars */
    size_t structural_chars;    /**< Number of structural chars */
    
    /* Error handling */
    int error_code;             /**< Error code (0 = success) */
    size_t error_position;      /**< Position of error */
    char error_message[256];    /**< Error description */
} dap_json_stage1_t;

/**
 * @brief Stage 1 error codes
 */
typedef enum {
    STAGE1_SUCCESS = 0,             /**< No error */
    STAGE1_ERROR_INVALID_UTF8 = 1,  /**< Invalid UTF-8 sequence */
    STAGE1_ERROR_UNTERMINATED_STRING = 2, /**< String not terminated */
    STAGE1_ERROR_INVALID_ESCAPE = 3, /**< Invalid escape sequence */
    STAGE1_ERROR_OUT_OF_MEMORY = 4,  /**< Memory allocation failed */
    STAGE1_ERROR_INVALID_INPUT = 5   /**< NULL input or zero length */
} dap_json_stage1_error_t;

/* ========================================================================== */
/*                           PUBLIC API                                       */
/* ========================================================================== */

/**
 * @brief Initialize Stage 1 parser
 * 
 * Allocates and initializes Stage 1 parser state.
 * Must be freed with dap_json_stage1_free().
 * 
 * @param input JSON input buffer (must remain valid during parsing)
 * @param input_len Input buffer length in bytes
 * @return Initialized Stage 1 parser, or NULL on error
 */
dap_json_stage1_t *dap_json_stage1_init(const uint8_t *input, size_t input_len);

/**
 * @brief Free Stage 1 parser
 * 
 * @param stage1 Stage 1 parser to free (can be NULL)
 */
void dap_json_stage1_free(dap_json_stage1_t *stage1);

/**
 * @brief Run Stage 1 structural indexing
 * 
 * Scans input buffer and extracts all structural characters.
 * После успешного выполнения indices array заполнен.
 * 
 * @param stage1 Initialized Stage 1 parser
 * @return STAGE1_SUCCESS on success, error code otherwise
 */
int dap_json_stage1_run(dap_json_stage1_t *stage1);

/**
 * @brief Get structural indices array
 * 
 * Returns pointer to structural indices array.
 * Array is valid until dap_json_stage1_free() or next dap_json_stage1_run().
 * 
 * @param stage1 Stage 1 parser
 * @param out_count Output: number of indices (can be NULL)
 * @return Pointer to indices array, or NULL if not run yet
 */
const dap_json_struct_index_t *dap_json_stage1_get_indices(
    const dap_json_stage1_t *stage1,
    size_t *out_count
);

/**
 * @brief Get Stage 1 statistics
 * 
 * Returns parsing statistics for profiling.
 * 
 * @param stage1 Stage 1 parser
 * @param out_string_chars Output: chars in strings (can be NULL)
 * @param out_whitespace_chars Output: whitespace chars (can be NULL)
 * @param out_structural_chars Output: structural chars (can be NULL)
 */
void dap_json_stage1_get_stats(
    const dap_json_stage1_t *stage1,
    size_t *out_string_chars,
    size_t *out_whitespace_chars,
    size_t *out_structural_chars
);

/* ========================================================================== */
/*                      CHARACTER CLASSIFICATION                              */
/* ========================================================================== */

/**
 * @brief Classify character
 * 
 * @param c Character to classify
 * @return Character class
 */
static inline dap_json_char_class_t dap_json_classify_char(uint8_t c)
{
    /* Structural characters */
    if (c == '{' || c == '}' || c == '[' || c == ']' || c == ':' || c == ',') {
        return CHAR_CLASS_STRUCTURAL;
    }
    
    /* Whitespace */
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
        return CHAR_CLASS_WHITESPACE;
    }
    
    /* String delimiter */
    if (c == '"') {
        return CHAR_CLASS_QUOTE;
    }
    
    /* Escape */
    if (c == '\\') {
        return CHAR_CLASS_BACKSLASH;
    }
    
    /* Digits */
    if (c >= '0' && c <= '9') {
        return CHAR_CLASS_DIGIT;
    }
    
    /* Minus */
    if (c == '-') {
        return CHAR_CLASS_MINUS;
    }
    
    /* Plus */
    if (c == '+') {
        return CHAR_CLASS_PLUS;
    }
    
    /* Letters (t, f, n для true/false/null) */
    if (c == 't' || c == 'f' || c == 'n' || c == 'T' || c == 'F' || c == 'N') {
        return CHAR_CLASS_LETTER;
    }
    
    return CHAR_CLASS_OTHER;
}

/**
 * @brief Check if character is structural
 * 
 * @param c Character to check
 * @return true if structural, false otherwise
 */
static inline bool dap_json_is_structural(uint8_t c)
{
    return (c == '{' || c == '}' || c == '[' || c == ']' || c == ':' || c == ',');
}

/**
 * @brief Check if character is whitespace
 * 
 * @param c Character to check
 * @return true if whitespace, false otherwise
 */
static inline bool dap_json_is_whitespace(uint8_t c)
{
    return (c == ' ' || c == '\t' || c == '\r' || c == '\n');
}

/* ========================================================================== */
/*                           UTF-8 VALIDATION                                 */
/* ========================================================================== */

/**
 * @brief Validate UTF-8 sequence
 * 
 * Sequential (non-SIMD) UTF-8 validation.
 * Reference implementation для correctness checking.
 * 
 * @param input Input buffer
 * @param len Buffer length
 * @param out_error_pos Output: position of error (if any)
 * @return true if valid UTF-8, false otherwise
 */
bool dap_json_validate_utf8_ref(
    const uint8_t *input,
    size_t len,
    size_t *out_error_pos
);

/**
 * @brief Get UTF-8 sequence length
 * 
 * Returns number of bytes in UTF-8 sequence starting at given byte.
 * Returns 0 if invalid start byte.
 * 
 * @param first_byte First byte of UTF-8 sequence
 * @return Sequence length (1-4), or 0 if invalid
 */
static inline int dap_json_utf8_sequence_length(uint8_t first_byte)
{
    /* Single-byte (ASCII: 0xxxxxxx) */
    if ((first_byte & 0x80) == 0) {
        return 1;
    }
    
    /* Two-byte (110xxxxx 10xxxxxx) */
    if ((first_byte & 0xE0) == 0xC0) {
        return 2;
    }
    
    /* Three-byte (1110xxxx 10xxxxxx 10xxxxxx) */
    if ((first_byte & 0xF0) == 0xE0) {
        return 3;
    }
    
    /* Four-byte (11110xxx 10xxxxxx 10xxxxxx 10xxxxxx) */
    if ((first_byte & 0xF8) == 0xF0) {
        return 4;
    }
    
    /* Invalid */
    return 0;
}

/* ========================================================================== */
/*                 SHARED FUNCTIONS (for SIMD implementations)                */
/* ========================================================================== */

/**
 * @brief Add token to indices array (shared by all implementations)
 * @details Grows array if needed, adds token with all metadata
 * @param[in,out] a_stage1 Stage 1 parser state
 * @param[in] a_position Byte position in input
 * @param[in] a_length Token length (0 for structural chars)
 * @param[in] a_type Token type (structural/string/number/literal)
 * @param[in] a_character_or_subtype Character for structural, or subtype for literal
 * @return true on success, false on allocation failure
 */
bool dap_json_stage1_add_token(
    dap_json_stage1_t *a_stage1,
    uint32_t a_position,
    uint32_t a_length,
    dap_json_token_type_t a_type,
    uint8_t a_character_or_subtype
);

/**
 * @brief Scan and validate string from opening quote to closing quote (reference implementation)
 * @details Handles escape sequences, returns end position
 * @param[in,out] a_stage1 Stage 1 parser state
 * @param[in] a_start_pos Position of opening quote "
 * @return Position after closing quote on success, a_start_pos on error
 */
size_t dap_json_stage1_scan_string_ref(
    dap_json_stage1_t *a_stage1,
    size_t a_start_pos
);

/**
 * @brief Scan and validate number (reference implementation)
 * @details Handles integers, decimals, scientific notation
 * @param[in,out] a_stage1 Stage 1 parser state
 * @param[in] a_start_pos Position of first digit or minus sign
 * @return Position after last number character on success, a_start_pos on error
 */
size_t dap_json_stage1_scan_number_ref(
    dap_json_stage1_t *a_stage1,
    size_t a_start_pos
);

/**
 * @brief Scan and validate literal (reference implementation)
 * @details Exact match with boundary check for true/false/null
 * @param[in,out] a_stage1 Stage 1 parser state
 * @param[in] a_start_pos Position of first character (t/f/n)
 * @return Position after literal on success, a_start_pos if not a literal
 */
size_t dap_json_stage1_scan_literal_ref(
    dap_json_stage1_t *a_stage1,
    size_t a_start_pos
);

/**
 * @brief Inline wrapper for string scanning (selects best implementation)
 */
static inline size_t dap_json_stage1_scan_string(
    dap_json_stage1_t *a_stage1,
    size_t a_start_pos
)
{
    // For now, always use reference
    // Future: dispatch to SIMD-optimized version
    return dap_json_stage1_scan_string_ref(a_stage1, a_start_pos);
}

/**
 * @brief Inline wrapper for number scanning (selects best implementation)
 */
static inline size_t dap_json_stage1_scan_number(
    dap_json_stage1_t *a_stage1,
    size_t a_start_pos
)
{
    // For now, always use reference
    // Future: dispatch to SIMD-optimized version
    return dap_json_stage1_scan_number_ref(a_stage1, a_start_pos);
}

/**
 * @brief Inline wrapper for literal scanning (selects best implementation)
 */
static inline size_t dap_json_stage1_scan_literal(
    dap_json_stage1_t *a_stage1,
    size_t a_start_pos
)
{
    // For now, always use reference
    // Future: dispatch to SIMD-optimized version
    return dap_json_stage1_scan_literal_ref(a_stage1, a_start_pos);
}

/* ========================================================================== */
/*            DISPATCH MECHANISM (static inline for zero overhead)            */
/* ========================================================================== */

// Forward declarations for SIMD implementations (conditionally compiled)
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    #if defined(DAP_JSON_HAVE_AVX2) || defined(__AVX2__)
        extern int dap_json_stage1_run_avx2(dap_json_stage1_t *a_stage1);
    #endif
    
    #if defined(DAP_JSON_HAVE_SSE2) || defined(__SSE2__)
        extern int dap_json_stage1_run_sse2(dap_json_stage1_t *a_stage1);
    #endif
    
    #if defined(DAP_JSON_HAVE_AVX512)
        extern int dap_json_stage1_run_avx512(dap_json_stage1_t *a_stage1);
    #endif
#endif

#if defined(__ARM_NEON) || defined(__aarch64__)
    #if defined(DAP_JSON_HAVE_NEON) || defined(__ARM_NEON)
        extern int dap_json_stage1_run_neon(dap_json_stage1_t *a_stage1);
    #endif
#endif

/**
 * @brief Main dispatch function (static inline for zero overhead)
 * @details Selects optimal implementation at compile time or runtime
 * @param[in,out] a_stage1 Stage 1 parser state
 * @return STAGE1_SUCCESS or error code
 */
static inline int dap_json_stage1_run_dispatched(dap_json_stage1_t *a_stage1)
{
    if (!a_stage1) {
        return STAGE1_ERROR_INVALID_INPUT;
    }
    
    // Compile-time dispatch: use the best available SIMD implementation
    // If compiled with specific SIMD flags, use that implementation directly
    
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    // x86/x86_64: prefer AVX2 > SSE2 > reference
    
    #if defined(__AVX2__)
        // Compiled with -mavx2: use AVX2 directly (assume CPU supports it)
        return dap_json_stage1_run_avx2(a_stage1);
    #elif defined(__SSE2__)
        // Compiled with -msse2: use SSE2 directly
        return dap_json_stage1_run_sse2(a_stage1);
    #else
        // No SIMD flags: use reference
        return dap_json_stage1_run(a_stage1);
    #endif
    
#elif defined(__ARM_NEON) || defined(__aarch64__)
    // ARM: prefer NEON > reference
    
    #if defined(__ARM_NEON)
        // Compiled with NEON: use it directly
        return dap_json_stage1_run_neon(a_stage1);
    #else
        // No NEON: use reference
        return dap_json_stage1_run(a_stage1);
    #endif
    
#else
    // Other architectures: always use reference
    return dap_json_stage1_run(a_stage1);
#endif
}

/**
 * @brief Get dispatch implementation name (for debugging)
 * @return String description of active implementation
 */
static inline const char* dap_json_stage1_get_dispatch_name(void)
{
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    #if defined(__AVX2__)
        return "AVX2 (32 bytes/iteration)";
    #elif defined(__SSE2__)
        return "SSE2 (16 bytes/iteration)";
    #else
        return "Reference C (portable)";
    #endif
#elif defined(__ARM_NEON) || defined(__aarch64__)
    #if defined(__ARM_NEON)
        return "NEON (16 bytes/iteration)";
    #else
        return "Reference C (portable)";
    #endif
#else
    return "Reference C (portable)";
#endif
}

/* ========================================================================== */
/*                 LEGACY API (for compatibility)                             */
/* ========================================================================== */

// These are kept for backward compatibility but deprecated
// New code should use dap_json_stage1_run_dispatched()

/**
 * @brief Reset dispatch mechanism (deprecated - no-op for static inline dispatch)
 */
static inline void dap_json_stage1_reset_dispatch(void)
{
    // No-op: static inline dispatch doesn't need runtime initialization
}

#ifdef __cplusplus
}
#endif

#endif /* DAP_JSON_STAGE1_H */

