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

#include "dap_cpu_detect.h"  // Runtime CPU feature detection

// Import dap_json.h for dap_cpu_arch_t and manual selection API
// This header already includes dap_cpu_arch.h from core
#include "dap_json.h"


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
 * @brief Literal subtypes for TOKEN_TYPE_LITERAL
 */
typedef enum {
    DAP_JSON_LITERAL_TRUE = 0,
    DAP_JSON_LITERAL_FALSE = 1,
    DAP_JSON_LITERAL_NULL = 2,
    DAP_JSON_LITERAL_UNKNOWN = 3
} dap_json_literal_type_t;

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
    size_t array_count;         /**< Number of arrays found (Phase 2.1 - for pre-allocation) */
    size_t object_count;        /**< Number of objects found (Phase 2.1 - for pre-allocation) */
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
dap_json_stage1_t *dap_json_stage1_create(const uint8_t *input, size_t input_len);

/**
 * @brief Create Stage 1 parser without input buffer
 * 
 * Allocates parser for later use with dap_json_stage1_reset().
 * Useful for benchmarking where same parser is reused.
 * 
 * @param capacity Initial capacity for indices array (0 = default)
 * @return Allocated parser, or NULL on error
 */
dap_json_stage1_t *dap_json_stage1_new(size_t capacity);

/**
 * @brief Reset Stage 1 parser with new input
 * 
 * Reuses existing parser with new input buffer.
 * Resets all state but keeps allocated indices array.
 * 
 * @param stage1 Stage 1 parser
 * @param input New JSON input buffer
 * @param input_len Input buffer length
 * @return true on success, false on error
 */
bool dap_json_stage1_reset(dap_json_stage1_t *stage1, const uint8_t *input, size_t input_len);

/**
 * @brief Free Stage 1 parser
 * 
 * @param stage1 Stage 1 parser to free (can be NULL)
 */
void dap_json_stage1_free(dap_json_stage1_t *stage1);

/**
 * @brief Get token count
 * 
 * @param stage1 Stage 1 parser
 * @return Number of tokens found
 */
size_t dap_json_stage1_get_token_count(const dap_json_stage1_t *stage1);

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

/**
 * @brief Get Stage 1 token counts for pre-allocation (Phase 2.1)
 * 
 * Returns token counts for predictive memory allocation.
 * Used by Stage 2 to pre-size Arena based on expected DOM size.
 * 
 * @param stage1 Stage 1 parser
 * @param out_string_count Output: number of strings (can be NULL)
 * @param out_number_count Output: number of numbers (can be NULL)
 * @param out_literal_count Output: number of literals (can be NULL)
 * @param out_array_count Output: number of arrays (can be NULL)
 * @param out_object_count Output: number of objects (can be NULL)
 */
void dap_json_stage1_get_token_counts(
    const dap_json_stage1_t *stage1,
    size_t *out_string_count,
    size_t *out_number_count,
    size_t *out_literal_count,
    size_t *out_array_count,
    size_t *out_object_count
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
/*                          DISPATCH MECHANISM                                */
/* ========================================================================== */

// Include architecture-specific implementations for static inline dispatch
// These must be included AFTER all typedefs are complete
#include "dap_json_stage1_ref.h"

#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
#include "dap_json_stage1_sse2.h"
#include "dap_json_stage1_avx2.h"
#include "dap_json_stage1_avx512.h"

#elif defined(__arm__) || defined(__aarch64__)
#include "dap_json_stage1_neon.h"
#include "dap_json_stage1_sve.h"
#include "dap_json_stage1_sve2.h"
#endif

/**
 * @brief Initialize CPU features detection for Stage 1
 * @details Called automatically by dap_json_init(), but can be called explicitly
 * 
 * This function performs runtime CPU detection and caches the results
 * in global variables for fast dispatch in dap_json_stage1_run().
 * 
 * Thread-safety: Not thread-safe, must be called from single thread
 * during initialization phase.
 */
void dap_json_stage1_init(void);


/**
 * @brief Main Stage 1 tokenization entry point (dispatched to optimal implementation)
 * @param a_stage1 Initialized Stage 1 parser
 * @return STAGE1_SUCCESS on success, error code otherwise
 * 
 * This function uses runtime dispatch to select the best available
 * SIMD implementation for the current CPU architecture.
 * 
 * NOTE: Call dap_json_stage1_init() once at startup before using this function.
 */
/**
 * @brief Main entry point for Stage 1 tokenization with automatic SIMD dispatch
 * @details Automatically selects best available SIMD implementation based on CPU features
 *          or manual override set via dap_json_set_simd_arch().
 * 
 * Dispatch priority (when AUTO):
 *   x86/x64: AVX-512 → AVX2 → SSE2 → Reference C
 *   ARM:     SVE2 → SVE → NEON → Reference C
 * 
 * @param[in,out] a_stage1 Initialized Stage 1 parser
 * @return STAGE1_SUCCESS on success, error code otherwise
 */
static inline int dap_json_stage1_run(dap_json_stage1_t *a_stage1)
{
    if (!a_stage1) {
        return STAGE1_ERROR_INVALID_INPUT;
    }
    
    // Get current architecture (respects manual override if set)
    dap_cpu_arch_t arch = dap_cpu_arch_get();
    
    switch (arch) {
#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
        case DAP_CPU_ARCH_SSE2:
            return dap_json_stage1_run_sse2(a_stage1);
        case DAP_CPU_ARCH_AVX2:
            return dap_json_stage1_run_avx2(a_stage1);
        case DAP_CPU_ARCH_AVX512:
            return dap_json_stage1_run_avx512(a_stage1);
#elif defined(__arm__) || defined(__aarch64__)
        case DAP_CPU_ARCH_NEON:
            return dap_json_stage1_run_neon(a_stage1);
        case DAP_CPU_ARCH_SVE:
            return dap_json_stage1_run_sve(a_stage1);
        case DAP_CPU_ARCH_SVE2:
            return dap_json_stage1_run_sve2(a_stage1);
#endif
        case DAP_CPU_ARCH_REFERENCE:
        case DAP_CPU_ARCH_AUTO:
        default:
            return dap_json_stage1_run_ref(a_stage1);
    }
}

/**
 * @brief Get current implementation name (for debugging)
 * @return String description of active implementation
 */
static inline const char* dap_json_stage1_get_name(void)
{
    dap_cpu_arch_t arch = dap_cpu_arch_get();
    return dap_cpu_arch_get_name(arch);
}

#ifdef __cplusplus
}
#endif

#endif /* DAP_JSON_STAGE1_H */

