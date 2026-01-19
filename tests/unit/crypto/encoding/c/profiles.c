/**
 * @file profiles.c
 * @brief Profile implementations
 */

#include "precis.h"
#include "stringclasses.h"
#include "common.h"
#include "bidi.h"
#include "error.h"
#include "profile.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* Helper function to allocate and copy string */
static int alloc_string(const char *input, size_t input_len, char **output, size_t *output_len) {
    if (!input || !output || !output_len) {
        return -1;
    }

    *output = (char *)malloc(input_len + 1);
    if (!*output) {
        return -1;
    }

    memcpy(*output, input, input_len);
    (*output)[input_len] = '\0';
    *output_len = input_len;
    return 0;
}

/* Width mapping rule - maps wide characters to narrow */
static int width_mapping_rule(const char *input, size_t input_len,
                               char **output, size_t *output_len,
                               precis_error_t *error) {
    (void)error;
    /* TODO: Implement width mapping using generated table */
    /* For now, just copy the input */
    return alloc_string(input, input_len, output, output_len);
}

/* Case mapping rule - convert to lowercase */
static int case_mapping_rule(const char *input, size_t input_len,
                             char **output, size_t *output_len,
                             precis_error_t *error) {
    (void)error;
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

/* Normalization rule - NFC */
static int normalization_nfc_rule(const char *input, size_t input_len,
                                   char **output, size_t *output_len,
                                   precis_error_t *error) {
    (void)error;
    /* TODO: Implement using ICU or similar */
    /* For now, just copy */
    return alloc_string(input, input_len, output, output_len);
}

/* Normalization rule - NFKC */
static int normalization_nfkc_rule(const char *input, size_t input_len,
                                    char **output, size_t *output_len,
                                    precis_error_t *error) {
    (void)error;
    /* TODO: Implement using ICU or similar */
    /* For now, just copy */
    return alloc_string(input, input_len, output, output_len);
}

/* Directionality rule */
static int directionality_rule(const char *input, size_t input_len,
                              char **output, size_t *output_len,
                              precis_error_t *error) {
    if (!input || !output || !output_len || !error) {
        return -1;
    }

    if (precis_has_rtl(input, input_len)) {
        if (!precis_satisfy_bidi_rule(input, input_len)) {
            precis_error_init(error, PRECIS_ERROR_INVALID);
            return -1;
        }
    }

    return alloc_string(input, input_len, output, output_len);
}

/* UsernameCaseMapped - prepare */
int precis_username_case_mapped_prepare(const char *input, size_t input_len,
                                         precis_string_t *output, precis_error_t *error) {
    if (!input || !output || !error) {
        return -1;
    }

    memset(output, 0, sizeof(*output));

    /* Apply width mapping */
    char *temp = NULL;
    size_t temp_len = 0;
    if (width_mapping_rule(input, input_len, &temp, &temp_len, error) != 0) {
        return -1;
    }

    /* Check if empty */
    if (temp_len == 0) {
        free(temp);
        precis_error_init(error, PRECIS_ERROR_INVALID);
        return -1;
    }

    /* Check string class allows */
    if (precis_string_class_allows(PRECIS_STRING_CLASS_IDENTIFIER, temp, temp_len, error) != 0) {
        free(temp);
        return -1;
    }

    output->data = temp;
    output->len = temp_len;
    output->owned = true;
    return 0;
}

/* UsernameCaseMapped - enforce */
int precis_username_case_mapped_enforce(const char *input, size_t input_len,
                                         precis_string_t *output, precis_error_t *error) {
    if (!input || !output || !error) {
        return -1;
    }

    memset(output, 0, sizeof(*output));

    /* Prepare first */
    precis_string_t prepared;
    memset(&prepared, 0, sizeof(prepared));
    if (precis_username_case_mapped_prepare(input, input_len, &prepared, error) != 0) {
        return -1;
    }

    /* Apply case mapping */
    char *temp1 = NULL;
    size_t temp1_len = 0;
    if (case_mapping_rule(prepared.data, prepared.len, &temp1, &temp1_len, error) != 0) {
        precis_string_free(&prepared);
        return -1;
    }
    precis_string_free(&prepared);

    /* Apply normalization */
    char *temp2 = NULL;
    size_t temp2_len = 0;
    if (normalization_nfc_rule(temp1, temp1_len, &temp2, &temp2_len, error) != 0) {
        free(temp1);
        return -1;
    }
    free(temp1);

    /* Check if empty */
    if (temp2_len == 0) {
        free(temp2);
        precis_error_init(error, PRECIS_ERROR_INVALID);
        return -1;
    }

    /* Apply directionality */
    char *temp3 = NULL;
    size_t temp3_len = 0;
    if (directionality_rule(temp2, temp2_len, &temp3, &temp3_len, error) != 0) {
        free(temp2);
        return -1;
    }
    free(temp2);

    output->data = temp3;
    output->len = temp3_len;
    output->owned = true;
    return 0;
}

/* UsernameCaseMapped - compare */
int precis_username_case_mapped_compare(const char *s1, size_t len1,
                                         const char *s2, size_t len2,
                                         bool *result, precis_error_t *error) {
    if (!s1 || !s2 || !result || !error) {
        return -1;
    }

    precis_string_t str1, str2;
    memset(&str1, 0, sizeof(str1));
    memset(&str2, 0, sizeof(str2));

    int r1 = precis_username_case_mapped_enforce(s1, len1, &str1, error);
    int r2 = precis_username_case_mapped_enforce(s2, len2, &str2, error);

    if (r1 != 0 || r2 != 0) {
        precis_string_free(&str1);
        precis_string_free(&str2);
        return -1;
    }

    if (str1.len == str2.len && memcmp(str1.data, str2.data, str1.len) == 0) {
        *result = true;
    } else {
        *result = false;
    }

    precis_string_free(&str1);
    precis_string_free(&str2);
    return 0;
}

/* UsernameCasePreserved - similar to CaseMapped but without case mapping */
int precis_username_case_preserved_prepare(const char *input, size_t input_len,
                                            precis_string_t *output, precis_error_t *error) {
    /* Same as CaseMapped prepare */
    return precis_username_case_mapped_prepare(input, input_len, output, error);
}

int precis_username_case_preserved_enforce(const char *input, size_t input_len,
                                             precis_string_t *output, precis_error_t *error) {
    if (!input || !output || !error) {
        return -1;
    }

    memset(output, 0, sizeof(*output));

    /* Prepare first */
    precis_string_t prepared;
    memset(&prepared, 0, sizeof(prepared));
    if (precis_username_case_preserved_prepare(input, input_len, &prepared, error) != 0) {
        return -1;
    }

    /* Apply normalization (no case mapping) */
    char *temp1 = NULL;
    size_t temp1_len = 0;
    if (normalization_nfc_rule(prepared.data, prepared.len, &temp1, &temp1_len, error) != 0) {
        precis_string_free(&prepared);
        return -1;
    }
    precis_string_free(&prepared);

    /* Check if empty */
    if (temp1_len == 0) {
        free(temp1);
        precis_error_init(error, PRECIS_ERROR_INVALID);
        return -1;
    }

    /* Apply directionality */
    char *temp2 = NULL;
    size_t temp2_len = 0;
    if (directionality_rule(temp1, temp1_len, &temp2, &temp2_len, error) != 0) {
        free(temp1);
        return -1;
    }
    free(temp1);

    output->data = temp2;
    output->len = temp2_len;
    output->owned = true;
    return 0;
}

int precis_username_case_preserved_compare(const char *s1, size_t len1,
                                             const char *s2, size_t len2,
                                             bool *result, precis_error_t *error) {
    if (!s1 || !s2 || !result || !error) {
        return -1;
    }

    precis_string_t str1, str2;
    memset(&str1, 0, sizeof(str1));
    memset(&str2, 0, sizeof(str2));

    int r1 = precis_username_case_preserved_enforce(s1, len1, &str1, error);
    int r2 = precis_username_case_preserved_enforce(s2, len2, &str2, error);

    if (r1 != 0 || r2 != 0) {
        precis_string_free(&str1);
        precis_string_free(&str2);
        return -1;
    }

    if (str1.len == str2.len && memcmp(str1.data, str2.data, str1.len) == 0) {
        *result = true;
    } else {
        *result = false;
    }

    precis_string_free(&str1);
    precis_string_free(&str2);
    return 0;
}

/* OpaqueString - stub implementations */
int precis_opaque_string_prepare(const char *input, size_t input_len,
                                  precis_string_t *output, precis_error_t *error) {
    if (!input || !output || !error) {
        return -1;
    }

    memset(output, 0, sizeof(*output));

    if (input_len == 0) {
        precis_error_init(error, PRECIS_ERROR_INVALID);
        return -1;
    }

    if (precis_string_class_allows(PRECIS_STRING_CLASS_FREEFORM, input, input_len, error) != 0) {
        return -1;
    }

    return alloc_string(input, input_len, &output->data, &output->len);
}

int precis_opaque_string_enforce(const char *input, size_t input_len,
                                  precis_string_t *output, precis_error_t *error) {
    /* TODO: Implement additional mapping and normalization */
    return precis_opaque_string_prepare(input, input_len, output, error);
}

int precis_opaque_string_compare(const char *s1, size_t len1,
                                  const char *s2, size_t len2,
                                  bool *result, precis_error_t *error) {
    if (!s1 || !s2 || !result || !error) {
        return -1;
    }

    precis_string_t str1, str2;
    memset(&str1, 0, sizeof(str1));
    memset(&str2, 0, sizeof(str2));

    int r1 = precis_opaque_string_enforce(s1, len1, &str1, error);
    int r2 = precis_opaque_string_enforce(s2, len2, &str2, error);

    if (r1 != 0 || r2 != 0) {
        precis_string_free(&str1);
        precis_string_free(&str2);
        return -1;
    }

    if (str1.len == str2.len && memcmp(str1.data, str2.data, str1.len) == 0) {
        *result = true;
    } else {
        *result = false;
    }

    precis_string_free(&str1);
    precis_string_free(&str2);
    return 0;
}

/* Nickname - stub implementations */
int precis_nickname_prepare(const char *input, size_t input_len,
                            precis_string_t *output, precis_error_t *error) {
    if (!input || !output || !error) {
        return -1;
    }

    memset(output, 0, sizeof(*output));

    if (input_len == 0) {
        precis_error_init(error, PRECIS_ERROR_INVALID);
        return -1;
    }

    if (precis_string_class_allows(PRECIS_STRING_CLASS_FREEFORM, input, input_len, error) != 0) {
        return -1;
    }

    return alloc_string(input, input_len, &output->data, &output->len);
}

int precis_nickname_enforce(const char *input, size_t input_len,
                             precis_string_t *output, precis_error_t *error) {
    /* TODO: Implement additional mapping, normalization, and stabilization */
    return precis_nickname_prepare(input, input_len, output, error);
}

int precis_nickname_compare(const char *s1, size_t len1,
                             const char *s2, size_t len2,
                             bool *result, precis_error_t *error) {
    if (!s1 || !s2 || !result || !error) {
        return -1;
    }

    precis_string_t str1, str2;
    memset(&str1, 0, sizeof(str1));
    memset(&str2, 0, sizeof(str2));

    int r1 = precis_nickname_enforce(s1, len1, &str1, error);
    int r2 = precis_nickname_enforce(s2, len2, &str2, error);

    if (r1 != 0 || r2 != 0) {
        precis_string_free(&str1);
        precis_string_free(&str2);
        return -1;
    }

    if (str1.len == str2.len && memcmp(str1.data, str2.data, str1.len) == 0) {
        *result = true;
    } else {
        *result = false;
    }

    precis_string_free(&str1);
    precis_string_free(&str2);
    return 0;
}

/* String management functions */
void precis_string_free(precis_string_t *str) {
    if (str && str->owned && str->data) {
        free(str->data);
        str->data = NULL;
        str->len = 0;
        str->owned = false;
    }
}

int precis_string_init(precis_string_t *str, const char *data, size_t len, bool owned) {
    if (!str || !data) {
        return -1;
    }

    str->data = (char *)data;
    str->len = len;
    str->owned = owned;
    return 0;
}

/* Profile creation - stub */
int precis_profile_create(precis_profile_type_t type, precis_profile_t **profile) {
    (void)type;
    (void)profile;
    /* TODO: Implement profile creation */
    return -1;
}

void precis_profile_free(precis_profile_t *profile) {
    (void)profile;
    /* TODO: Implement profile cleanup */
}

/* String class operations */
int precis_string_class_get_value_from_char(precis_string_class_type_t class_type,
                                            uint32_t cp,
                                            precis_derived_property_value_t *value) {
    if (!value) {
        return -1;
    }

    *value = precis_get_derived_property_value(cp, class_type);
    return 0;
}
