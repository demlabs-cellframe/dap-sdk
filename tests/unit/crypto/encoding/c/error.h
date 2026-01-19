/**
 * @file error.h
 * @brief Error handling for PRECIS library
 */

#ifndef PRECIS_ERROR_H
#define PRECIS_ERROR_H

#include "precis.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Internal error functions */
void precis_error_init(precis_error_t *error, precis_error_type_t type);
void precis_error_set_bad_codepoint(precis_error_t *error, uint32_t cp, size_t position,
                                     precis_derived_property_value_t property);
void precis_error_set_unexpected(precis_error_t *error, precis_unexpected_error_t unexpected);

#ifdef __cplusplus
}
#endif

#endif /* PRECIS_ERROR_H */
