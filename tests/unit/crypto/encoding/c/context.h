/**
 * @file context.h
 * @brief Context rules for PRECIS
 */

#ifndef PRECIS_CONTEXT_H
#define PRECIS_CONTEXT_H

#include "precis.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PRECIS_CONTEXT_RULE_OK,
    PRECIS_CONTEXT_RULE_NOT_APPLICABLE,
    PRECIS_CONTEXT_RULE_UNDEFINED
} precis_context_rule_result_t;

typedef int (*precis_context_rule_fn)(const char *label, size_t label_len,
                                       size_t offset, bool *allowed);

/* Context rule functions */
int precis_context_rule_zero_width_nonjoiner(const char *label, size_t label_len,
                                               size_t offset, bool *allowed);
int precis_context_rule_zero_width_joiner(const char *label, size_t label_len,
                                            size_t offset, bool *allowed);
int precis_context_rule_middle_dot(const char *label, size_t label_len,
                                     size_t offset, bool *allowed);
int precis_context_rule_greek_lower_numeral_sign_keraia(const char *label, size_t label_len,
                                                           size_t offset, bool *allowed);
int precis_context_rule_hebrew_punctuation(const char *label, size_t label_len,
                                            size_t offset, bool *allowed);
int precis_context_rule_katakana_middle_dot(const char *label, size_t label_len,
                                              size_t offset, bool *allowed);
int precis_context_rule_arabic_indic_digits(const char *label, size_t label_len,
                                             size_t offset, bool *allowed);
int precis_context_rule_extended_arabic_indic_digits(const char *label, size_t label_len,
                                                       size_t offset, bool *allowed);

/* Get context rule for a codepoint */
precis_context_rule_fn precis_get_context_rule(uint32_t cp);

#ifdef __cplusplus
}
#endif

#endif /* PRECIS_CONTEXT_H */
