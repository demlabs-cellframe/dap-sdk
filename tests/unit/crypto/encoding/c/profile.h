/**
 * @file profile.h
 * @brief Profile interface for PRECIS
 */

#ifndef PRECIS_PROFILE_H
#define PRECIS_PROFILE_H

#include "precis.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Profile rule functions */
typedef int (*precis_width_mapping_fn)(const char *input, size_t input_len,
                                       char **output, size_t *output_len,
                                       precis_error_t *error);
typedef int (*precis_additional_mapping_fn)(const char *input, size_t input_len,
                                            char **output, size_t *output_len,
                                            precis_error_t *error);
typedef int (*precis_case_mapping_fn)(const char *input, size_t input_len,
                                      char **output, size_t *output_len,
                                      precis_error_t *error);
typedef int (*precis_normalization_fn)(const char *input, size_t input_len,
                                       char **output, size_t *output_len,
                                       precis_error_t *error);
typedef int (*precis_directionality_fn)(const char *input, size_t input_len,
                                         char **output, size_t *output_len,
                                         precis_error_t *error);

/* Stabilize function - apply rules until stable */
int precis_stabilize(const char *input, size_t input_len,
                     int (*apply_rules)(const char *in, size_t in_len,
                                       char **out, size_t *out_len,
                                       precis_error_t *err),
                     char **output, size_t *output_len,
                     precis_error_t *error);

#ifdef __cplusplus
}
#endif

#endif /* PRECIS_PROFILE_H */
