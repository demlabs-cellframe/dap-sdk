/**
 * @file bidi.h
 * @brief Bidirectional text rules for PRECIS
 */

#ifndef PRECIS_BIDI_H
#define PRECIS_BIDI_H

#include "precis.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PRECIS_BIDI_CLASS_L,
    PRECIS_BIDI_CLASS_R,
    PRECIS_BIDI_CLASS_AL,
    PRECIS_BIDI_CLASS_AN,
    PRECIS_BIDI_CLASS_EN,
    PRECIS_BIDI_CLASS_ES,
    PRECIS_BIDI_CLASS_CS,
    PRECIS_BIDI_CLASS_ET,
    PRECIS_BIDI_CLASS_ON,
    PRECIS_BIDI_CLASS_BN,
    PRECIS_BIDI_CLASS_NSM,
    PRECIS_BIDI_CLASS_WS
} precis_bidi_class_t;

/* Get bidi class for a codepoint */
precis_bidi_class_t precis_bidi_class(uint32_t cp);

/* Check if label has RTL characters */
bool precis_has_rtl(const char *label, size_t len);

/* Check if label satisfies bidi rule */
bool precis_satisfy_bidi_rule(const char *label, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* PRECIS_BIDI_H */
