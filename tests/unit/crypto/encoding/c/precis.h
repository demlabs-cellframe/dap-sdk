/**
 * @file precis.h
 * @brief PRECIS Framework: Preparation, Enforcement, and Comparison of
 *        Internationalized Strings in Application Protocols
 * 
 * This library implements the PRECIS Framework as defined in RFC 8264.
 * It provides APIs for preparing, enforcing, and comparing internationalized strings.
 */

#ifndef PRECIS_H
#define PRECIS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct precis_error precis_error_t;
typedef struct precis_codepoint_info precis_codepoint_info_t;
typedef struct precis_string_class precis_string_class_t;
typedef struct precis_profile precis_profile_t;

/* Error types */
typedef enum {
    PRECIS_ERROR_NONE = 0,
    PRECIS_ERROR_INVALID,
    PRECIS_ERROR_BAD_CODEPOINT,
    PRECIS_ERROR_UNEXPECTED
} precis_error_type_t;

typedef enum {
    PRECIS_UNEXPECTED_CONTEXT_RULE_NOT_APPLICABLE,
    PRECIS_UNEXPECTED_MISSING_CONTEXT_RULE,
    PRECIS_UNEXPECTED_PROFILE_RULE_NOT_APPLICABLE,
    PRECIS_UNEXPECTED_UNDEFINED
} precis_unexpected_error_t;

/* Derived property values */
typedef enum {
    PRECIS_PROPERTY_PVALID,
    PRECIS_PROPERTY_SPEC_CLASS_PVAL,
    PRECIS_PROPERTY_SPEC_CLASS_DIS,
    PRECIS_PROPERTY_DISALLOWED,
    PRECIS_PROPERTY_UNASSIGNED,
    PRECIS_PROPERTY_CONTEXTJ,
    PRECIS_PROPERTY_CONTEXTO
} precis_derived_property_value_t;

/* String result structure */
typedef struct {
    char *data;
    size_t len;
    bool owned;  /* true if data was allocated, false if borrowed */
} precis_string_t;

/* Error structure */
struct precis_codepoint_info {
    uint32_t cp;
    size_t position;
    precis_derived_property_value_t property;
};

struct precis_error {
    precis_error_type_t type;
    union {
        precis_codepoint_info_t codepoint_info;
        precis_unexpected_error_t unexpected;
    } u;
};

/* String class types */
typedef enum {
    PRECIS_STRING_CLASS_IDENTIFIER,
    PRECIS_STRING_CLASS_FREEFORM
} precis_string_class_type_t;

/* Profile types */
typedef enum {
    PRECIS_PROFILE_USERNAME_CASE_MAPPED,
    PRECIS_PROFILE_USERNAME_CASE_PRESERVED,
    PRECIS_PROFILE_OPAQUE_STRING,
    PRECIS_PROFILE_NICKNAME
} precis_profile_type_t;

/* Profile operations */
typedef int (*precis_prepare_fn)(const char *input, size_t input_len,
                                 precis_string_t *output, precis_error_t *error);
typedef int (*precis_enforce_fn)(const char *input, size_t input_len,
                                 precis_string_t *output, precis_error_t *error);
typedef int (*precis_compare_fn)(const char *s1, size_t len1,
                                  const char *s2, size_t len2,
                                  bool *result, precis_error_t *error);

/* Profile structure */
struct precis_profile {
    precis_profile_type_t type;
    precis_prepare_fn prepare;
    precis_enforce_fn enforce;
    precis_compare_fn compare;
};

/* Error handling */
const char *precis_error_message(const precis_error_t *error);
void precis_error_free(precis_error_t *error);

/* String management */
void precis_string_free(precis_string_t *str);
int precis_string_init(precis_string_t *str, const char *data, size_t len, bool owned);

/* String class operations */
int precis_string_class_get_value_from_char(precis_string_class_type_t class_type,
                                            uint32_t cp,
                                            precis_derived_property_value_t *value);
int precis_string_class_allows(precis_string_class_type_t class_type,
                               const char *label, size_t len,
                               precis_error_t *error);

/* Profile creation */
int precis_profile_create(precis_profile_type_t type, precis_profile_t **profile);
void precis_profile_free(precis_profile_t *profile);

/* Fast invocation functions (static profile instances) */
int precis_username_case_mapped_prepare(const char *input, size_t input_len,
                                         precis_string_t *output, precis_error_t *error);
int precis_username_case_mapped_enforce(const char *input, size_t input_len,
                                       precis_string_t *output, precis_error_t *error);
int precis_username_case_mapped_compare(const char *s1, size_t len1,
                                       const char *s2, size_t len2,
                                       bool *result, precis_error_t *error);

int precis_username_case_preserved_prepare(const char *input, size_t input_len,
                                           precis_string_t *output, precis_error_t *error);
int precis_username_case_preserved_enforce(const char *input, size_t input_len,
                                           precis_string_t *output, precis_error_t *error);
int precis_username_case_preserved_compare(const char *s1, size_t len1,
                                            const char *s2, size_t len2,
                                            bool *result, precis_error_t *error);

int precis_opaque_string_prepare(const char *input, size_t input_len,
                                 precis_string_t *output, precis_error_t *error);
int precis_opaque_string_enforce(const char *input, size_t input_len,
                                 precis_string_t *output, precis_error_t *error);
int precis_opaque_string_compare(const char *s1, size_t len1,
                                  const char *s2, size_t len2,
                                  bool *result, precis_error_t *error);

int precis_nickname_prepare(const char *input, size_t input_len,
                            precis_string_t *output, precis_error_t *error);
int precis_nickname_enforce(const char *input, size_t input_len,
                            precis_string_t *output, precis_error_t *error);
int precis_nickname_compare(const char *s1, size_t len1,
                            const char *s2, size_t len2,
                            bool *result, precis_error_t *error);

#ifdef __cplusplus
}
#endif

#endif /* PRECIS_H */
