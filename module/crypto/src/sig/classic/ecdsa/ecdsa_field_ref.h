/*
 * Internal header for reference field implementations
 * 
 * These are internal functions used by the architecture dispatcher.
 * Do not include this header outside of sig_ecdsa/ implementation files.
 */

#ifndef ECDSA_FIELD_REF_H
#define ECDSA_FIELD_REF_H

#include "ecdsa_field.h"

// Reference (generic C) implementations - used as fallback by dispatcher
void ecdsa_field_mul_ref(ecdsa_field_t *r, const ecdsa_field_t *a, const ecdsa_field_t *b);
void ecdsa_field_sqr_ref(ecdsa_field_t *r, const ecdsa_field_t *a);

#endif // ECDSA_FIELD_REF_H
