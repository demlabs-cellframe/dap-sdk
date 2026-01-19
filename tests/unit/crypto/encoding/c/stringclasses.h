/**
 * @file stringclasses.h
 * @brief String classes for PRECIS
 */

#ifndef PRECIS_STRINGCLASSES_H
#define PRECIS_STRINGCLASSES_H

#include "precis.h"
#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Get derived property value for a string class */
precis_derived_property_value_t precis_get_derived_property_value(
    uint32_t cp,
    precis_string_class_type_t class_type);

/* Check if a label is allowed by a string class */
int precis_string_class_allows(precis_string_class_type_t class_type,
                                const char *label, size_t len,
                                precis_error_t *error);

#ifdef __cplusplus
}
#endif

#endif /* PRECIS_STRINGCLASSES_H */
