/**
 * @file stringclasses.c
 * @brief String classes implementation
 */

#include "stringclasses.h"
#include "context.h"
#include "error.h"
#include <string.h>

/* Get derived property value based on string class type */
static precis_derived_property_value_t get_specific_value(
    precis_string_class_type_t class_type,
    int category) {
    switch (category) {
        case 0: /* has_compat */
        case 1: /* other_letter_digits */
        case 2: /* spaces */
        case 3: /* symbols */
        case 4: /* punctuation */
            if (class_type == PRECIS_STRING_CLASS_IDENTIFIER) {
                return PRECIS_PROPERTY_SPEC_CLASS_DIS;
            } else {
                return PRECIS_PROPERTY_SPEC_CLASS_PVAL;
            }
        default:
            return PRECIS_PROPERTY_PVALID;
    }
}

precis_derived_property_value_t precis_get_derived_property_value(
    uint32_t cp,
    precis_string_class_type_t class_type) {
    /* Check exceptions first */
    precis_derived_property_value_t exception_val = precis_get_exception_val(cp);
    if (exception_val != PRECIS_PROPERTY_PVALID) {
        /* TODO: Need better way to check if exception was found */
        /* For now, assume if it's not PVALID, it was found */
        return exception_val;
    }

    /* Check backward compatible */
    precis_derived_property_value_t bc_val = precis_get_backward_compatible_val(cp);
    if (bc_val != PRECIS_PROPERTY_PVALID) {
        return bc_val;
    }

    /* Check unassigned */
    if (precis_is_unassigned(cp)) {
        return PRECIS_PROPERTY_UNASSIGNED;
    }

    /* Check ASCII7 */
    if (precis_is_ascii7(cp)) {
        return PRECIS_PROPERTY_PVALID;
    }

    /* Check JoinControl */
    if (precis_is_join_control(cp)) {
        return PRECIS_PROPERTY_CONTEXTJ;
    }

    /* Check OldHangulJamo */
    if (precis_is_old_hangul_jamo(cp)) {
        return PRECIS_PROPERTY_DISALLOWED;
    }

    /* Check PrecisIgnorableProperties */
    if (precis_is_precis_ignorable_property(cp)) {
        return PRECIS_PROPERTY_DISALLOWED;
    }

    /* Check Controls */
    if (precis_is_control(cp)) {
        return PRECIS_PROPERTY_DISALLOWED;
    }

    /* Check HasCompat */
    if (precis_has_compat(cp)) {
        return get_specific_value(class_type, 0);
    }

    /* Check LetterDigits */
    if (precis_is_letter_digit(cp)) {
        return PRECIS_PROPERTY_PVALID;
    }

    /* Check OtherLetterDigits */
    if (precis_is_other_letter_digit(cp)) {
        return get_specific_value(class_type, 1);
    }

    /* Check Spaces */
    if (precis_is_space(cp)) {
        return get_specific_value(class_type, 2);
    }

    /* Check Symbols */
    if (precis_is_symbol(cp)) {
        return get_specific_value(class_type, 3);
    }

    /* Check Punctuation */
    if (precis_is_punctuation(cp)) {
        return get_specific_value(class_type, 4);
    }

    /* Default: DISALLOWED */
    return PRECIS_PROPERTY_DISALLOWED;
}

/* Check if context rule allows the codepoint */
static int allowed_by_context_rule(const char *label, size_t len,
                                    precis_derived_property_value_t val,
                                    uint32_t cp, size_t offset,
                                    precis_error_t *error) {
    precis_context_rule_fn rule = precis_get_context_rule(cp);
    if (!rule) {
        precis_error_set_unexpected(error, PRECIS_UNEXPECTED_MISSING_CONTEXT_RULE);
        return -1;
    }

    bool allowed = false;
    int result = rule(label, len, offset, &allowed);

    if (result == PRECIS_CONTEXT_RULE_UNDEFINED) {
        precis_error_set_unexpected(error, PRECIS_UNEXPECTED_UNDEFINED);
        return -1;
    }

    if (result == PRECIS_CONTEXT_RULE_NOT_APPLICABLE) {
        precis_error_set_unexpected(error, PRECIS_UNEXPECTED_CONTEXT_RULE_NOT_APPLICABLE);
        return -1;
    }

    if (!allowed) {
        precis_error_set_bad_codepoint(error, cp, offset, val);
        return -1;
    }

    return 0;
}

int precis_string_class_allows(precis_string_class_type_t class_type,
                                const char *label, size_t len,
                                precis_error_t *error) {
    if (!label || !error) {
        return -1;
    }

    /* Simple UTF-8 iteration - assumes single-byte for now */
    /* Full implementation would need proper UTF-8 decoding */
    for (size_t offset = 0; offset < len; offset++) {
        uint32_t cp = (uint32_t)(unsigned char)label[offset];
        precis_derived_property_value_t val = precis_get_derived_property_value(cp, class_type);

        switch (val) {
            case PRECIS_PROPERTY_PVALID:
            case PRECIS_PROPERTY_SPEC_CLASS_PVAL:
                /* Allowed */
                break;

            case PRECIS_PROPERTY_SPEC_CLASS_DIS:
            case PRECIS_PROPERTY_DISALLOWED:
            case PRECIS_PROPERTY_UNASSIGNED:
                precis_error_set_bad_codepoint(error, cp, offset, val);
                return -1;

            case PRECIS_PROPERTY_CONTEXTJ:
            case PRECIS_PROPERTY_CONTEXTO:
                if (allowed_by_context_rule(label, len, val, cp, offset, error) != 0) {
                    return -1;
                }
                break;
        }
    }

    return 0;
}
