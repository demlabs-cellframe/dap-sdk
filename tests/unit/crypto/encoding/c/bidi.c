/**
 * @file bidi.c
 * @brief Bidirectional text rules implementation
 */

#include "bidi.h"
#include "common.h"
#include <stdbool.h>
#include <stdint.h>

/* Forward declaration for generated bidi class table */
typedef struct {
    uint32_t cp;
    precis_bidi_class_t class;
} precis_bidi_class_entry_t;

extern const precis_bidi_class_entry_t BIDI_CLASS_TABLE[];
extern const size_t BIDI_CLASS_TABLE_SIZE;

/* Binary search for bidi class */
static precis_bidi_class_t binary_search_bidi_class(uint32_t cp,
                                                     const precis_bidi_class_entry_t *table,
                                                     size_t size) {
    if (!table || size == 0) {
        return PRECIS_BIDI_CLASS_L; /* Default: Left-to-Right */
    }

    size_t left = 0;
    size_t right = size;

    while (left < right) {
        size_t mid = left + (right - left) / 2;
        const precis_bidi_class_entry_t *entry = &table[mid];

        if (cp < entry->cp) {
            right = mid;
        } else if (cp > entry->cp) {
            left = mid + 1;
        } else {
            return entry->class;
        }
    }

    return PRECIS_BIDI_CLASS_L; /* Default: Left-to-Right */
}

precis_bidi_class_t precis_bidi_class(uint32_t cp) {
    return binary_search_bidi_class(cp, BIDI_CLASS_TABLE, BIDI_CLASS_TABLE_SIZE);
}

bool precis_has_rtl(const char *label, size_t len) {
    if (!label) {
        return false;
    }

    for (size_t i = 0; i < len; i++) {
        uint32_t cp = (uint32_t)(unsigned char)label[i];
        precis_bidi_class_t cls = precis_bidi_class(cp);
        if (cls == PRECIS_BIDI_CLASS_R || cls == PRECIS_BIDI_CLASS_AL || cls == PRECIS_BIDI_CLASS_AN) {
            return true;
        }
    }

    return false;
}

static bool is_valid_rtl_label(const char *label, size_t len, size_t start_pos,
                               precis_bidi_class_t first_class) {
    bool nsm = false;
    bool en = false;
    bool an = false;
    precis_bidi_class_t prev = first_class;

    for (size_t i = start_pos; i < len; i++) {
        uint32_t cp = (uint32_t)(unsigned char)label[i];
        precis_bidi_class_t cls = precis_bidi_class(cp);

        /* Rule 2: In an RTL label, only characters with specific Bidi properties are allowed */
        switch (cls) {
            case PRECIS_BIDI_CLASS_R:
            case PRECIS_BIDI_CLASS_AL:
            case PRECIS_BIDI_CLASS_ES:
            case PRECIS_BIDI_CLASS_CS:
            case PRECIS_BIDI_CLASS_ET:
            case PRECIS_BIDI_CLASS_ON:
            case PRECIS_BIDI_CLASS_BN:
                break;

            case PRECIS_BIDI_CLASS_AN:
                if (en) {
                    /* Rule 4: if EN is present, no AN may be present */
                    return false;
                }
                an = true;
                break;

            case PRECIS_BIDI_CLASS_EN:
                if (an) {
                    /* Rule 4: if AN is present, no EN may be present */
                    return false;
                }
                en = true;
                break;

            case PRECIS_BIDI_CLASS_NSM:
                /* Rule 3: NSM must follow R, AL, EN, or AN */
                if (!(prev == PRECIS_BIDI_CLASS_R || prev == PRECIS_BIDI_CLASS_AL ||
                      prev == PRECIS_BIDI_CLASS_EN || prev == PRECIS_BIDI_CLASS_AN)) {
                    return false;
                }
                nsm = true;
                continue;

            default:
                /* Character not allowed in RTL label */
                return false;
        }

        if (nsm) {
            /* After NSM, only NSM is allowed */
            return false;
        } else {
            prev = cls;
        }
    }

    /* Rule 3: End must be R, AL, EN, or AN (possibly followed by NSM) */
    return nsm || (prev == PRECIS_BIDI_CLASS_R || prev == PRECIS_BIDI_CLASS_AL ||
                   prev == PRECIS_BIDI_CLASS_EN || prev == PRECIS_BIDI_CLASS_AN);
}

static bool is_valid_ltr_label(const char *label, size_t len, size_t start_pos,
                                precis_bidi_class_t first_class) {
    bool nsm = false;
    precis_bidi_class_t prev = first_class;

    for (size_t i = start_pos; i < len; i++) {
        uint32_t cp = (uint32_t)(unsigned char)label[i];
        precis_bidi_class_t cls = precis_bidi_class(cp);

        /* Rule 5: In an LTR label, only characters with specific Bidi properties are allowed */
        switch (cls) {
            case PRECIS_BIDI_CLASS_L:
            case PRECIS_BIDI_CLASS_EN:
            case PRECIS_BIDI_CLASS_ES:
            case PRECIS_BIDI_CLASS_CS:
            case PRECIS_BIDI_CLASS_ET:
            case PRECIS_BIDI_CLASS_ON:
            case PRECIS_BIDI_CLASS_BN:
                if (nsm) {
                    /* Rule 6: After NSM, only NSM is allowed */
                    return false;
                }
                prev = cls;
                break;

            case PRECIS_BIDI_CLASS_NSM:
                /* Rule 6: NSM must follow L or EN */
                if (!(prev == PRECIS_BIDI_CLASS_L || prev == PRECIS_BIDI_CLASS_EN)) {
                    return false;
                }
                nsm = true;
                break;

            default:
                /* Character not allowed in LTR label */
                return false;
        }
    }

    /* Rule 6: End must be L or EN (possibly followed by NSM) */
    return nsm || (prev == PRECIS_BIDI_CLASS_L || prev == PRECIS_BIDI_CLASS_EN);
}

bool precis_satisfy_bidi_rule(const char *label, size_t len) {
    if (!label || len == 0) {
        return true; /* Empty label is valid */
    }

    uint32_t first_cp = (uint32_t)(unsigned char)label[0];
    precis_bidi_class_t first = precis_bidi_class(first_cp);

    /* Rule 1: First character must be L, R, or AL */
    if (first == PRECIS_BIDI_CLASS_R || first == PRECIS_BIDI_CLASS_AL) {
        /* RTL label */
        return is_valid_rtl_label(label, len, 1, first);
    } else if (first == PRECIS_BIDI_CLASS_L) {
        /* LTR label */
        return is_valid_ltr_label(label, len, 1, first);
    } else {
        /* First character not in {L, R, AL} */
        return false;
    }
}
