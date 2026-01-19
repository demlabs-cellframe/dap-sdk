/**
 * @file common.c
 * @brief Common utilities implementation
 */

#include "common.h"
#include "error.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* Note: These tables would normally be generated from Unicode data.
 * For now, we provide stub implementations. The actual tables should
 * be generated from the same Unicode data files used by the Rust version.
 */

/* Forward declarations for generated tables - these would be in a separate
 * generated file in a real implementation */
extern const precis_codepoint_range_t LOWERCASE_LETTER[];
extern const size_t LOWERCASE_LETTER_SIZE;
extern const precis_codepoint_range_t UPPERCASE_LETTER[];
extern const size_t UPPERCASE_LETTER_SIZE;
extern const precis_codepoint_range_t OTHER_LETTER[];
extern const size_t OTHER_LETTER_SIZE;
extern const precis_codepoint_range_t DECIMAL_NUMBER[];
extern const size_t DECIMAL_NUMBER_SIZE;
extern const precis_codepoint_range_t MODIFIER_LETTER[];
extern const size_t MODIFIER_LETTER_SIZE;
extern const precis_codepoint_range_t NONSPACING_MARK[];
extern const size_t NONSPACING_MARK_SIZE;
extern const precis_codepoint_range_t SPACING_MARK[];
extern const size_t SPACING_MARK_SIZE;
extern const precis_codepoint_range_t JOIN_CONTROL[];
extern const size_t JOIN_CONTROL_SIZE;
extern const precis_codepoint_range_t LEADING_JAMO[];
extern const size_t LEADING_JAMO_SIZE;
extern const precis_codepoint_range_t VOWEL_JAMO[];
extern const size_t VOWEL_JAMO_SIZE;
extern const precis_codepoint_range_t TRAILING_JAMO[];
extern const size_t TRAILING_JAMO_SIZE;
extern const precis_codepoint_range_t UNASSIGNED[];
extern const size_t UNASSIGNED_SIZE;
extern const precis_codepoint_range_t NONCHARACTER_CODE_POINT[];
extern const size_t NONCHARACTER_CODE_POINT_SIZE;
extern const precis_codepoint_range_t ASCII7[];
extern const size_t ASCII7_SIZE;
extern const precis_codepoint_range_t CONTROL[];
extern const size_t CONTROL_SIZE;
extern const precis_codepoint_range_t DEFAULT_IGNORABLE_CODE_POINT[];
extern const size_t DEFAULT_IGNORABLE_CODE_POINT_SIZE;
extern const precis_codepoint_range_t SPACE_SEPARATOR[];
extern const size_t SPACE_SEPARATOR_SIZE;
extern const precis_codepoint_range_t MATH_SYMBOL[];
extern const size_t MATH_SYMBOL_SIZE;
extern const precis_codepoint_range_t CURRENCY_SYMBOL[];
extern const size_t CURRENCY_SYMBOL_SIZE;
extern const precis_codepoint_range_t MODIFIER_SYMBOL[];
extern const size_t MODIFIER_SYMBOL_SIZE;
extern const precis_codepoint_range_t OTHER_SYMBOL[];
extern const size_t OTHER_SYMBOL_SIZE;
extern const precis_codepoint_range_t CONNECTOR_PUNCTUATION[];
extern const size_t CONNECTOR_PUNCTUATION_SIZE;
extern const precis_codepoint_range_t DASH_PUNCTUATION[];
extern const size_t DASH_PUNCTUATION_SIZE;
extern const precis_codepoint_range_t OPEN_PUNCTUATION[];
extern const size_t OPEN_PUNCTUATION_SIZE;
extern const precis_codepoint_range_t CLOSE_PUNCTUATION[];
extern const size_t CLOSE_PUNCTUATION_SIZE;
extern const precis_codepoint_range_t INITIAL_PUNCTUATION[];
extern const size_t INITIAL_PUNCTUATION_SIZE;
extern const precis_codepoint_range_t FINAL_PUNCTUATION[];
extern const size_t FINAL_PUNCTUATION_SIZE;
extern const precis_codepoint_range_t OTHER_PUNCTUATION[];
extern const size_t OTHER_PUNCTUATION_SIZE;
extern const precis_codepoint_range_t TITLECASE_LETTER[];
extern const size_t TITLECASE_LETTER_SIZE;
extern const precis_codepoint_range_t LETTER_NUMBER[];
extern const size_t LETTER_NUMBER_SIZE;
extern const precis_codepoint_range_t OTHER_NUMBER[];
extern const size_t OTHER_NUMBER_SIZE;
extern const precis_codepoint_range_t ENCLOSING_MARK[];
extern const size_t ENCLOSING_MARK_SIZE;
extern const precis_codepoint_range_t VIRAMA[];
extern const size_t VIRAMA_SIZE;
extern const precis_codepoint_range_t GREEK[];
extern const size_t GREEK_SIZE;
extern const precis_codepoint_range_t HEBREW[];
extern const size_t HEBREW_SIZE;
extern const precis_codepoint_range_t HIRAGANA[];
extern const size_t HIRAGANA_SIZE;
extern const precis_codepoint_range_t KATAKANA[];
extern const size_t KATAKANA_SIZE;
extern const precis_codepoint_range_t HAN[];
extern const size_t HAN_SIZE;
extern const precis_codepoint_range_t DUAL_JOINING[];
extern const size_t DUAL_JOINING_SIZE;
extern const precis_codepoint_range_t LEFT_JOINING[];
extern const size_t LEFT_JOINING_SIZE;
extern const precis_codepoint_range_t RIGHT_JOINING[];
extern const size_t RIGHT_JOINING_SIZE;
extern const precis_codepoint_range_t TRANSPARENT[];
extern const size_t TRANSPARENT_SIZE;

/* Exception and backward compatible tables */
typedef struct {
    uint32_t cp;
    precis_derived_property_value_t value;
} precis_exception_entry_t;

extern const precis_exception_entry_t EXCEPTIONS[];
extern const size_t EXCEPTIONS_SIZE;
extern const precis_exception_entry_t BACKWARD_COMPATIBLE[];
extern const size_t BACKWARD_COMPATIBLE_SIZE;

/* Binary search helper for sorted range table */
static bool binary_search_range(uint32_t cp, const precis_codepoint_range_t *table, size_t size) {
    if (!table || size == 0) {
        return false;
    }

    size_t left = 0;
    size_t right = size;

    while (left < right) {
        size_t mid = left + (right - left) / 2;
        const precis_codepoint_range_t *entry = &table[mid];

        if (cp < entry->start) {
            right = mid;
        } else if (cp > entry->end) {
            left = mid + 1;
        } else {
            return true;  /* cp is in range [start, end] */
        }
    }

    return false;
}

bool precis_is_in_table(uint32_t cp, const precis_codepoint_range_t *table, size_t table_size) {
    return binary_search_range(cp, table, table_size);
}

bool precis_is_letter_digit(uint32_t cp) {
    return precis_is_in_table(cp, LOWERCASE_LETTER, LOWERCASE_LETTER_SIZE) ||
           precis_is_in_table(cp, UPPERCASE_LETTER, UPPERCASE_LETTER_SIZE) ||
           precis_is_in_table(cp, OTHER_LETTER, OTHER_LETTER_SIZE) ||
           precis_is_in_table(cp, DECIMAL_NUMBER, DECIMAL_NUMBER_SIZE) ||
           precis_is_in_table(cp, MODIFIER_LETTER, MODIFIER_LETTER_SIZE) ||
           precis_is_in_table(cp, NONSPACING_MARK, NONSPACING_MARK_SIZE) ||
           precis_is_in_table(cp, SPACING_MARK, SPACING_MARK_SIZE);
}

bool precis_is_join_control(uint32_t cp) {
    return precis_is_in_table(cp, JOIN_CONTROL, JOIN_CONTROL_SIZE);
}

bool precis_is_old_hangul_jamo(uint32_t cp) {
    return precis_is_in_table(cp, LEADING_JAMO, LEADING_JAMO_SIZE) ||
           precis_is_in_table(cp, VOWEL_JAMO, VOWEL_JAMO_SIZE) ||
           precis_is_in_table(cp, TRAILING_JAMO, TRAILING_JAMO_SIZE);
}

bool precis_is_unassigned(uint32_t cp) {
    return !precis_is_in_table(cp, NONCHARACTER_CODE_POINT, NONCHARACTER_CODE_POINT_SIZE) &&
           precis_is_in_table(cp, UNASSIGNED, UNASSIGNED_SIZE);
}

bool precis_is_ascii7(uint32_t cp) {
    return precis_is_in_table(cp, ASCII7, ASCII7_SIZE);
}

bool precis_is_control(uint32_t cp) {
    return precis_is_in_table(cp, CONTROL, CONTROL_SIZE);
}

bool precis_is_precis_ignorable_property(uint32_t cp) {
    return precis_is_in_table(cp, DEFAULT_IGNORABLE_CODE_POINT, DEFAULT_IGNORABLE_CODE_POINT_SIZE) ||
           precis_is_in_table(cp, NONCHARACTER_CODE_POINT, NONCHARACTER_CODE_POINT_SIZE);
}

bool precis_is_space(uint32_t cp) {
    return precis_is_in_table(cp, SPACE_SEPARATOR, SPACE_SEPARATOR_SIZE);
}

bool precis_is_symbol(uint32_t cp) {
    return precis_is_in_table(cp, MATH_SYMBOL, MATH_SYMBOL_SIZE) ||
           precis_is_in_table(cp, CURRENCY_SYMBOL, CURRENCY_SYMBOL_SIZE) ||
           precis_is_in_table(cp, MODIFIER_SYMBOL, MODIFIER_SYMBOL_SIZE) ||
           precis_is_in_table(cp, OTHER_SYMBOL, OTHER_SYMBOL_SIZE);
}

bool precis_is_punctuation(uint32_t cp) {
    return precis_is_in_table(cp, CONNECTOR_PUNCTUATION, CONNECTOR_PUNCTUATION_SIZE) ||
           precis_is_in_table(cp, DASH_PUNCTUATION, DASH_PUNCTUATION_SIZE) ||
           precis_is_in_table(cp, OPEN_PUNCTUATION, OPEN_PUNCTUATION_SIZE) ||
           precis_is_in_table(cp, CLOSE_PUNCTUATION, CLOSE_PUNCTUATION_SIZE) ||
           precis_is_in_table(cp, INITIAL_PUNCTUATION, INITIAL_PUNCTUATION_SIZE) ||
           precis_is_in_table(cp, FINAL_PUNCTUATION, FINAL_PUNCTUATION_SIZE) ||
           precis_is_in_table(cp, OTHER_PUNCTUATION, OTHER_PUNCTUATION_SIZE);
}

bool precis_is_other_letter_digit(uint32_t cp) {
    return precis_is_in_table(cp, TITLECASE_LETTER, TITLECASE_LETTER_SIZE) ||
           precis_is_in_table(cp, LETTER_NUMBER, LETTER_NUMBER_SIZE) ||
           precis_is_in_table(cp, OTHER_NUMBER, OTHER_NUMBER_SIZE) ||
           precis_is_in_table(cp, ENCLOSING_MARK, ENCLOSING_MARK_SIZE);
}

/* Check if a codepoint has compatibility decomposition */
bool precis_has_compat(uint32_t cp) {
    /* This requires Unicode normalization. For now, return false.
     * In a full implementation, this would check if NFKC decomposition
     * changes the codepoint. */
    /* TODO: Implement using a Unicode normalization library like ICU */
    (void)cp;
    return false;
}

bool precis_is_virama(uint32_t cp) {
    return precis_is_in_table(cp, VIRAMA, VIRAMA_SIZE);
}

bool precis_is_greek(uint32_t cp) {
    return precis_is_in_table(cp, GREEK, GREEK_SIZE);
}

bool precis_is_hebrew(uint32_t cp) {
    return precis_is_in_table(cp, HEBREW, HEBREW_SIZE);
}

bool precis_is_hiragana(uint32_t cp) {
    return precis_is_in_table(cp, HIRAGANA, HIRAGANA_SIZE);
}

bool precis_is_katakana(uint32_t cp) {
    return precis_is_in_table(cp, KATAKANA, KATAKANA_SIZE);
}

bool precis_is_han(uint32_t cp) {
    return precis_is_in_table(cp, HAN, HAN_SIZE);
}

bool precis_is_dual_joining(uint32_t cp) {
    return precis_is_in_table(cp, DUAL_JOINING, DUAL_JOINING_SIZE);
}

bool precis_is_left_joining(uint32_t cp) {
    return precis_is_in_table(cp, LEFT_JOINING, LEFT_JOINING_SIZE);
}

bool precis_is_right_joining(uint32_t cp) {
    return precis_is_in_table(cp, RIGHT_JOINING, RIGHT_JOINING_SIZE);
}

bool precis_is_transparent(uint32_t cp) {
    return precis_is_in_table(cp, TRANSPARENT, TRANSPARENT_SIZE);
}

/* Binary search for exception table */
static precis_derived_property_value_t binary_search_exception(uint32_t cp,
                                                               const precis_exception_entry_t *table,
                                                               size_t size) {
    if (!table || size == 0) {
        return PRECIS_PROPERTY_PVALID; /* Default, but should be checked by caller */
    }

    size_t left = 0;
    size_t right = size;

    while (left < right) {
        size_t mid = left + (right - left) / 2;
        const precis_exception_entry_t *entry = &table[mid];

        if (cp < entry->cp) {
            right = mid;
        } else if (cp > entry->cp) {
            left = mid + 1;
        } else {
            return entry->value;
        }
    }

    return PRECIS_PROPERTY_PVALID; /* Not found */
}

precis_derived_property_value_t precis_get_exception_val(uint32_t cp) {
    /* Return a sentinel value to indicate "not found" */
    /* In a real implementation, we'd need a way to distinguish "not found"
     * from a valid value. For now, we'll use a separate function that returns
     * a boolean to check existence. */
    precis_derived_property_value_t val = binary_search_exception(cp, EXCEPTIONS, EXCEPTIONS_SIZE);
    /* If the value is the default, we need to check if it was actually found */
    /* This is a limitation - we'd need a better design */
    return val;
}

precis_derived_property_value_t precis_get_backward_compatible_val(uint32_t cp) {
    return binary_search_exception(cp, BACKWARD_COMPATIBLE, BACKWARD_COMPATIBLE_SIZE);
}

/* Unicode normalization stubs - would need ICU or similar library */
int precis_normalize_nfc(const char *input, size_t input_len,
                         char **output, size_t *output_len) {
    /* TODO: Implement using ICU or similar Unicode library */
    (void)input;
    (void)input_len;
    (void)output;
    (void)output_len;
    return -1; /* Not implemented */
}

int precis_normalize_nfkc(const char *input, size_t input_len,
                          char **output, size_t *output_len) {
    /* TODO: Implement using ICU or similar Unicode library */
    (void)input;
    (void)input_len;
    (void)output;
    (void)output_len;
    return -1; /* Not implemented */
}

/* Case mapping - simplified implementation */
int precis_to_lowercase(const char *input, size_t input_len,
                         char **output, size_t *output_len) {
    if (!input || !output || !output_len) {
        return -1;
    }

    *output = (char *)malloc(input_len + 1);
    if (!*output) {
        return -1;
    }

    /* Simple ASCII case conversion - full Unicode would need ICU */
    for (size_t i = 0; i < input_len; i++) {
        (*output)[i] = (char)tolower((unsigned char)input[i]);
    }
    (*output)[input_len] = '\0';
    *output_len = input_len;

    return 0;
}
