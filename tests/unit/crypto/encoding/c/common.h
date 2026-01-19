/**
 * @file common.h
 * @brief Common utilities for Unicode property checks
 */

#ifndef PRECIS_COMMON_H
#define PRECIS_COMMON_H

#include "precis.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Codepoint range structure */
typedef struct {
    uint32_t start;
    uint32_t end;
} precis_codepoint_range_t;

/* Check if codepoint is in a sorted table */
bool precis_is_in_table(uint32_t cp, const precis_codepoint_range_t *table, size_t table_size);

/* Unicode property checks */
bool precis_is_letter_digit(uint32_t cp);
bool precis_is_join_control(uint32_t cp);
bool precis_is_old_hangul_jamo(uint32_t cp);
bool precis_is_unassigned(uint32_t cp);
bool precis_is_ascii7(uint32_t cp);
bool precis_is_control(uint32_t cp);
bool precis_is_precis_ignorable_property(uint32_t cp);
bool precis_is_space(uint32_t cp);
bool precis_is_symbol(uint32_t cp);
bool precis_is_punctuation(uint32_t cp);
bool precis_is_other_letter_digit(uint32_t cp);
bool precis_has_compat(uint32_t cp);

/* Context rule helpers */
bool precis_is_virama(uint32_t cp);
bool precis_is_greek(uint32_t cp);
bool precis_is_hebrew(uint32_t cp);
bool precis_is_hiragana(uint32_t cp);
bool precis_is_katakana(uint32_t cp);
bool precis_is_han(uint32_t cp);
bool precis_is_dual_joining(uint32_t cp);
bool precis_is_left_joining(uint32_t cp);
bool precis_is_right_joining(uint32_t cp);
bool precis_is_transparent(uint32_t cp);

/* Exception and backward compatible lookups */
precis_derived_property_value_t precis_get_exception_val(uint32_t cp);
precis_derived_property_value_t precis_get_backward_compatible_val(uint32_t cp);

/* Unicode normalization helpers */
int precis_normalize_nfc(const char *input, size_t input_len,
                         char **output, size_t *output_len);
int precis_normalize_nfkc(const char *input, size_t input_len,
                          char **output, size_t *output_len);

/* Case mapping */
int precis_to_lowercase(const char *input, size_t input_len,
                         char **output, size_t *output_len);

#ifdef __cplusplus
}
#endif

#endif /* PRECIS_COMMON_H */
