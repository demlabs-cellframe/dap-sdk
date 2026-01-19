/**
 * @file tables_stub.c
 * @brief Stub implementations for generated Unicode tables
 * 
 * NOTE: In a real implementation, these tables would be generated from
 * Unicode data files using a build script similar to the Rust version's
 * build.rs. This file provides empty stubs so the code compiles.
 */

#include "common.h"
#include "bidi.h"

/* Codepoint range tables - empty stubs */
const precis_codepoint_range_t LOWERCASE_LETTER[] = {};
const size_t LOWERCASE_LETTER_SIZE = 0;

const precis_codepoint_range_t UPPERCASE_LETTER[] = {};
const size_t UPPERCASE_LETTER_SIZE = 0;

const precis_codepoint_range_t OTHER_LETTER[] = {};
const size_t OTHER_LETTER_SIZE = 0;

const precis_codepoint_range_t DECIMAL_NUMBER[] = {};
const size_t DECIMAL_NUMBER_SIZE = 0;

const precis_codepoint_range_t MODIFIER_LETTER[] = {};
const size_t MODIFIER_LETTER_SIZE = 0;

const precis_codepoint_range_t NONSPACING_MARK[] = {};
const size_t NONSPACING_MARK_SIZE = 0;

const precis_codepoint_range_t SPACING_MARK[] = {};
const size_t SPACING_MARK_SIZE = 0;

const precis_codepoint_range_t JOIN_CONTROL[] = {};
const size_t JOIN_CONTROL_SIZE = 0;

const precis_codepoint_range_t LEADING_JAMO[] = {};
const size_t LEADING_JAMO_SIZE = 0;

const precis_codepoint_range_t VOWEL_JAMO[] = {};
const size_t VOWEL_JAMO_SIZE = 0;

const precis_codepoint_range_t TRAILING_JAMO[] = {};
const size_t TRAILING_JAMO_SIZE = 0;

const precis_codepoint_range_t UNASSIGNED[] = {};
const size_t UNASSIGNED_SIZE = 0;

const precis_codepoint_range_t NONCHARACTER_CODE_POINT[] = {};
const size_t NONCHARACTER_CODE_POINT_SIZE = 0;

/* ASCII7: 0x0021..0x007E */
const precis_codepoint_range_t ASCII7[] = {{0x0021, 0x007E}};
const size_t ASCII7_SIZE = 1;

const precis_codepoint_range_t CONTROL[] = {};
const size_t CONTROL_SIZE = 0;

const precis_codepoint_range_t DEFAULT_IGNORABLE_CODE_POINT[] = {};
const size_t DEFAULT_IGNORABLE_CODE_POINT_SIZE = 0;

const precis_codepoint_range_t SPACE_SEPARATOR[] = {};
const size_t SPACE_SEPARATOR_SIZE = 0;

const precis_codepoint_range_t MATH_SYMBOL[] = {};
const size_t MATH_SYMBOL_SIZE = 0;

const precis_codepoint_range_t CURRENCY_SYMBOL[] = {};
const size_t CURRENCY_SYMBOL_SIZE = 0;

const precis_codepoint_range_t MODIFIER_SYMBOL[] = {};
const size_t MODIFIER_SYMBOL_SIZE = 0;

const precis_codepoint_range_t OTHER_SYMBOL[] = {};
const size_t OTHER_SYMBOL_SIZE = 0;

const precis_codepoint_range_t CONNECTOR_PUNCTUATION[] = {};
const size_t CONNECTOR_PUNCTUATION_SIZE = 0;

const precis_codepoint_range_t DASH_PUNCTUATION[] = {};
const size_t DASH_PUNCTUATION_SIZE = 0;

const precis_codepoint_range_t OPEN_PUNCTUATION[] = {};
const size_t OPEN_PUNCTUATION_SIZE = 0;

const precis_codepoint_range_t CLOSE_PUNCTUATION[] = {};
const size_t CLOSE_PUNCTUATION_SIZE = 0;

const precis_codepoint_range_t INITIAL_PUNCTUATION[] = {};
const size_t INITIAL_PUNCTUATION_SIZE = 0;

const precis_codepoint_range_t FINAL_PUNCTUATION[] = {};
const size_t FINAL_PUNCTUATION_SIZE = 0;

const precis_codepoint_range_t OTHER_PUNCTUATION[] = {};
const size_t OTHER_PUNCTUATION_SIZE = 0;

const precis_codepoint_range_t TITLECASE_LETTER[] = {};
const size_t TITLECASE_LETTER_SIZE = 0;

const precis_codepoint_range_t LETTER_NUMBER[] = {};
const size_t LETTER_NUMBER_SIZE = 0;

const precis_codepoint_range_t OTHER_NUMBER[] = {};
const size_t OTHER_NUMBER_SIZE = 0;

const precis_codepoint_range_t ENCLOSING_MARK[] = {};
const size_t ENCLOSING_MARK_SIZE = 0;

const precis_codepoint_range_t VIRAMA[] = {};
const size_t VIRAMA_SIZE = 0;

const precis_codepoint_range_t GREEK[] = {};
const size_t GREEK_SIZE = 0;

const precis_codepoint_range_t HEBREW[] = {};
const size_t HEBREW_SIZE = 0;

const precis_codepoint_range_t HIRAGANA[] = {};
const size_t HIRAGANA_SIZE = 0;

const precis_codepoint_range_t KATAKANA[] = {};
const size_t KATAKANA_SIZE = 0;

const precis_codepoint_range_t HAN[] = {};
const size_t HAN_SIZE = 0;

const precis_codepoint_range_t DUAL_JOINING[] = {};
const size_t DUAL_JOINING_SIZE = 0;

const precis_codepoint_range_t LEFT_JOINING[] = {};
const size_t LEFT_JOINING_SIZE = 0;

const precis_codepoint_range_t RIGHT_JOINING[] = {};
const size_t RIGHT_JOINING_SIZE = 0;

const precis_codepoint_range_t TRANSPARENT[] = {};
const size_t TRANSPARENT_SIZE = 0;

/* Exception and backward compatible tables */
typedef struct {
    uint32_t cp;
    precis_derived_property_value_t value;
} precis_exception_entry_t;

const precis_exception_entry_t EXCEPTIONS[] = {};
const size_t EXCEPTIONS_SIZE = 0;

const precis_exception_entry_t BACKWARD_COMPATIBLE[] = {};
const size_t BACKWARD_COMPATIBLE_SIZE = 0;

/* Bidi class table */
typedef struct {
    uint32_t cp;
    precis_bidi_class_t class;
} precis_bidi_class_entry_t;

const precis_bidi_class_entry_t BIDI_CLASS_TABLE[] = {};
const size_t BIDI_CLASS_TABLE_SIZE = 0;
