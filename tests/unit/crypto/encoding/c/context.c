/**
 * @file context.c
 * @brief Context rules implementation
 */

#include "context.h"
#include "common.h"
#include "error.h"
#include <string.h>
#include <stdint.h>

/* Helper functions to get characters at positions */
static uint32_t get_char_at(const char *label, size_t label_len, size_t offset) {
    if (offset >= label_len) {
        return 0;
    }
    /* Simple UTF-8 decoding - assumes single byte for now */
    /* Full implementation would need proper UTF-8 decoding */
    return (uint32_t)(unsigned char)label[offset];
}

static uint32_t get_char_before(const char *label, size_t label_len, size_t offset) {
    if (offset == 0 || offset > label_len) {
        return 0;
    }
    return get_char_at(label, label_len, offset - 1);
}

static uint32_t get_char_after(const char *label, size_t label_len, size_t offset) {
    if (offset + 1 >= label_len) {
        return 0;
    }
    return get_char_at(label, label_len, offset + 1);
}

/* ZERO WIDTH NON-JOINER U+200C */
int precis_context_rule_zero_width_nonjoiner(const char *label, size_t label_len,
                                               size_t offset, bool *allowed) {
    if (!label || !allowed) {
        return PRECIS_CONTEXT_RULE_UNDEFINED;
    }

    uint32_t cp = get_char_at(label, label_len, offset);
    if (cp != 0x200c) {
        return PRECIS_CONTEXT_RULE_NOT_APPLICABLE;
    }

    uint32_t prev = get_char_before(label, label_len, offset);
    if (prev == 0) {
        return PRECIS_CONTEXT_RULE_UNDEFINED;
    }

    if (precis_is_virama(prev)) {
        *allowed = true;
        return PRECIS_CONTEXT_RULE_OK;
    }

    /* Check transparent joining type code points before U+200C */
    size_t i = offset;
    uint32_t cp_check = prev;
    while (precis_is_transparent(cp_check) && i > 0) {
        i--;
        cp_check = get_char_before(label, label_len, i);
        if (cp_check == 0) {
            return PRECIS_CONTEXT_RULE_UNDEFINED;
        }
    }

    /* Joining_Type:{L,D} */
    if (!(precis_is_left_joining(cp_check) || precis_is_dual_joining(cp_check))) {
        *allowed = false;
        return PRECIS_CONTEXT_RULE_OK;
    }

    /* Check transparent joining type code points following U+200C */
    i = offset;
    uint32_t next = get_char_after(label, label_len, offset);
    if (next == 0) {
        return PRECIS_CONTEXT_RULE_UNDEFINED;
    }

    cp_check = next;
    while (precis_is_transparent(cp_check)) {
        i++;
        cp_check = get_char_after(label, label_len, i);
        if (cp_check == 0) {
            return PRECIS_CONTEXT_RULE_UNDEFINED;
        }
    }

    /* Joining_Type:{R,D} */
    *allowed = (precis_is_right_joining(cp_check) || precis_is_dual_joining(cp_check));
    return PRECIS_CONTEXT_RULE_OK;
}

/* ZERO WIDTH JOINER U+200D */
int precis_context_rule_zero_width_joiner(const char *label, size_t label_len,
                                            size_t offset, bool *allowed) {
    if (!label || !allowed) {
        return PRECIS_CONTEXT_RULE_UNDEFINED;
    }

    uint32_t cp = get_char_at(label, label_len, offset);
    if (cp != 0x200d) {
        return PRECIS_CONTEXT_RULE_NOT_APPLICABLE;
    }

    uint32_t prev = get_char_before(label, label_len, offset);
    if (prev == 0) {
        return PRECIS_CONTEXT_RULE_UNDEFINED;
    }

    *allowed = precis_is_virama(prev);
    return PRECIS_CONTEXT_RULE_OK;
}

/* MIDDLE DOT U+00B7 */
int precis_context_rule_middle_dot(const char *label, size_t label_len,
                                    size_t offset, bool *allowed) {
    if (!label || !allowed) {
        return PRECIS_CONTEXT_RULE_UNDEFINED;
    }

    uint32_t cp = get_char_at(label, label_len, offset);
    if (cp != 0x00b7) {
        return PRECIS_CONTEXT_RULE_NOT_APPLICABLE;
    }

    uint32_t prev = get_char_before(label, label_len, offset);
    uint32_t next = get_char_after(label, label_len, offset);

    if (prev == 0 || next == 0) {
        return PRECIS_CONTEXT_RULE_UNDEFINED;
    }

    *allowed = (prev == 0x006c && next == 0x006c);
    return PRECIS_CONTEXT_RULE_OK;
}

/* GREEK LOWER NUMERAL SIGN (KERAIA) U+0375 */
int precis_context_rule_greek_lower_numeral_sign_keraia(const char *label, size_t label_len,
                                                         size_t offset, bool *allowed) {
    if (!label || !allowed) {
        return PRECIS_CONTEXT_RULE_UNDEFINED;
    }

    uint32_t cp = get_char_at(label, label_len, offset);
    if (cp != 0x0375) {
        return PRECIS_CONTEXT_RULE_NOT_APPLICABLE;
    }

    uint32_t next = get_char_after(label, label_len, offset);
    if (next == 0) {
        return PRECIS_CONTEXT_RULE_UNDEFINED;
    }

    *allowed = precis_is_greek(next);
    return PRECIS_CONTEXT_RULE_OK;
}

/* HEBREW PUNCTUATION GERESH and GERSHAYIM U+05F3, U+05F4 */
int precis_context_rule_hebrew_punctuation(const char *label, size_t label_len,
                                             size_t offset, bool *allowed) {
    if (!label || !allowed) {
        return PRECIS_CONTEXT_RULE_UNDEFINED;
    }

    uint32_t cp = get_char_at(label, label_len, offset);
    if (cp != 0x05f3 && cp != 0x05f4) {
        return PRECIS_CONTEXT_RULE_NOT_APPLICABLE;
    }

    uint32_t prev = get_char_before(label, label_len, offset);
    if (prev == 0) {
        return PRECIS_CONTEXT_RULE_UNDEFINED;
    }

    *allowed = precis_is_hebrew(prev);
    return PRECIS_CONTEXT_RULE_OK;
}

/* KATAKANA MIDDLE DOT U+30FB */
int precis_context_rule_katakana_middle_dot(const char *label, size_t label_len,
                                              size_t offset, bool *allowed) {
    if (!label || !allowed) {
        return PRECIS_CONTEXT_RULE_UNDEFINED;
    }

    uint32_t cp = get_char_at(label, label_len, offset);
    if (cp != 0x30fb) {
        return PRECIS_CONTEXT_RULE_NOT_APPLICABLE;
    }

    /* Check if label contains at least one Hiragana, Katakana, or Han character */
    for (size_t i = 0; i < label_len; i++) {
        uint32_t c = get_char_at(label, label_len, i);
        if (precis_is_hiragana(c) || precis_is_katakana(c) || precis_is_han(c)) {
            *allowed = true;
            return PRECIS_CONTEXT_RULE_OK;
        }
    }

    *allowed = false;
    return PRECIS_CONTEXT_RULE_OK;
}

/* ARABIC-INDIC DIGITS U+0660..U+0669 */
int precis_context_rule_arabic_indic_digits(const char *label, size_t label_len,
                                             size_t offset, bool *allowed) {
    if (!label || !allowed) {
        return PRECIS_CONTEXT_RULE_UNDEFINED;
    }

    uint32_t cp = get_char_at(label, label_len, offset);
    if (cp < 0x0660 || cp > 0x0669) {
        return PRECIS_CONTEXT_RULE_NOT_APPLICABLE;
    }

    /* Check if label contains Extended Arabic-Indic Digits (U+06F0..U+06F9) */
    for (size_t i = 0; i < label_len; i++) {
        uint32_t c = get_char_at(label, label_len, i);
        if (c >= 0x06f0 && c <= 0x06f9) {
            *allowed = false;
            return PRECIS_CONTEXT_RULE_OK;
        }
    }

    *allowed = true;
    return PRECIS_CONTEXT_RULE_OK;
}

/* EXTENDED ARABIC-INDIC DIGITS U+06F0..U+06F9 */
int precis_context_rule_extended_arabic_indic_digits(const char *label, size_t label_len,
                                                       size_t offset, bool *allowed) {
    if (!label || !allowed) {
        return PRECIS_CONTEXT_RULE_UNDEFINED;
    }

    uint32_t cp = get_char_at(label, label_len, offset);
    if (cp < 0x06f0 || cp > 0x06f9) {
        return PRECIS_CONTEXT_RULE_NOT_APPLICABLE;
    }

    /* Check if label contains Arabic-Indic Digits (U+0660..U+0669) */
    for (size_t i = 0; i < label_len; i++) {
        uint32_t c = get_char_at(label, label_len, i);
        if (c >= 0x0660 && c <= 0x0669) {
            *allowed = false;
            return PRECIS_CONTEXT_RULE_OK;
        }
    }

    *allowed = true;
    return PRECIS_CONTEXT_RULE_OK;
}

/* Get context rule for a codepoint */
precis_context_rule_fn precis_get_context_rule(uint32_t cp) {
    switch (cp) {
        case 0x00b7:
            return precis_context_rule_middle_dot;
        case 0x200c:
            return precis_context_rule_zero_width_nonjoiner;
        case 0x200d:
            return precis_context_rule_zero_width_joiner;
        case 0x0375:
            return precis_context_rule_greek_lower_numeral_sign_keraia;
        case 0x05f3:
        case 0x05f4:
            return precis_context_rule_hebrew_punctuation;
        case 0x30fb:
            return precis_context_rule_katakana_middle_dot;
        default:
            if (cp >= 0x0660 && cp <= 0x0669) {
                return precis_context_rule_arabic_indic_digits;
            }
            if (cp >= 0x06f0 && cp <= 0x06f9) {
                return precis_context_rule_extended_arabic_indic_digits;
            }
            return NULL;
    }
}
