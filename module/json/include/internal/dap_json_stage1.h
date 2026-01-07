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
 * @brief Structural index entry
 * 
 * Одна запись в массиве structural indices.
 * Содержит позицию и тип structural character.
 */
typedef struct {
    uint32_t position;  /**< Byte offset in input buffer */
    uint8_t character;  /**< Actual character ({, }, [, ], :, ,) */
    uint8_t _padding[3]; /**< Alignment padding */
} dap_json_struct_index_t;

/**
 * @brief Stage 1 parser state
 * 
 * Внутреннее состояние Stage 1 parser.
 * Содержит input buffer, structural indices, и parser state.
 */
typedef struct {
    /* Input */
    const uint8_t *input;       /**< Input JSON buffer (not owned) */
    size_t input_len;           /**< Input buffer length in bytes */
    
    /* Structural indices array */
    dap_json_struct_index_t *indices;  /**< Structural indices array */
    size_t indices_capacity;    /**< Allocated capacity */
    size_t indices_count;       /**< Number of structural indices found */
    
    /* Parser state */
    size_t current_pos;         /**< Current position in input */
    bool in_string;             /**< Are we inside a string? */
    bool escape_next;           /**< Is next char escaped? */
    
    /* Statistics (for profiling) */
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

#ifdef __cplusplus
}
#endif

#endif /* DAP_JSON_STAGE1_H */

