/**
 * @file error.c
 * @brief Error handling implementation
 */

#include "error.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

const char *precis_error_message(const precis_error_t *error) {
    if (!error) {
        return "null error";
    }

    switch (error->type) {
        case PRECIS_ERROR_NONE:
            return "no error";
        case PRECIS_ERROR_INVALID:
            return "invalid label";
        case PRECIS_ERROR_BAD_CODEPOINT: {
            static char buf[256];
            snprintf(buf, sizeof(buf),
                     "bad codepoint: code point %#06x, position: %zu, property: %d",
                     error->u.codepoint_info.cp,
                     error->u.codepoint_info.position,
                     error->u.codepoint_info.property);
            return buf;
        }
        case PRECIS_ERROR_UNEXPECTED: {
            static char buf[128];
            const char *msg = "unexpected: ";
            switch (error->u.unexpected) {
                case PRECIS_UNEXPECTED_CONTEXT_RULE_NOT_APPLICABLE:
                    msg = "unexpected: context rule not applicable";
                    break;
                case PRECIS_UNEXPECTED_MISSING_CONTEXT_RULE:
                    msg = "unexpected: missing context rule";
                    break;
                case PRECIS_UNEXPECTED_PROFILE_RULE_NOT_APPLICABLE:
                    msg = "unexpected: profile rule not applicable";
                    break;
                case PRECIS_UNEXPECTED_UNDEFINED:
                    msg = "unexpected: undefined";
                    break;
            }
            snprintf(buf, sizeof(buf), "%s", msg);
            return buf;
        }
        default:
            return "unknown error";
    }
}

void precis_error_free(precis_error_t *error) {
    if (error) {
        memset(error, 0, sizeof(precis_error_t));
    }
}

void precis_error_init(precis_error_t *error, precis_error_type_t type) {
    if (error) {
        memset(error, 0, sizeof(precis_error_t));
        error->type = type;
    }
}

void precis_error_set_bad_codepoint(precis_error_t *error, uint32_t cp, size_t position,
                                     precis_derived_property_value_t property) {
    if (error) {
        error->type = PRECIS_ERROR_BAD_CODEPOINT;
        error->u.codepoint_info.cp = cp;
        error->u.codepoint_info.position = position;
        error->u.codepoint_info.property = property;
    }
}

void precis_error_set_unexpected(precis_error_t *error, precis_unexpected_error_t unexpected) {
    if (error) {
        error->type = PRECIS_ERROR_UNEXPECTED;
        error->u.unexpected = unexpected;
    }
}
