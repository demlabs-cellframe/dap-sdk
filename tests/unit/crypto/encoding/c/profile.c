/**
 * @file profile.c
 * @brief Profile implementation
 */

#include "profile.h"
#include "error.h"
#include <stdlib.h>
#include <string.h>

int precis_stabilize(const char *input, size_t input_len,
                     int (*apply_rules)(const char *in, size_t in_len,
                                       char **out, size_t *out_len,
                                       precis_error_t *err),
                     char **output, size_t *output_len,
                     precis_error_t *error) {
    if (!input || !apply_rules || !output || !output_len || !error) {
        return -1;
    }

    char *current = (char *)malloc(input_len + 1);
    if (!current) {
        return -1;
    }
    memcpy(current, input, input_len);
    current[input_len] = '\0';
    size_t current_len = input_len;

    for (int i = 0; i <= 2; i++) {
        char *next = NULL;
        size_t next_len = 0;
        precis_error_t iter_error;
        memset(&iter_error, 0, sizeof(iter_error));

        int result = apply_rules(current, current_len, &next, &next_len, &iter_error);
        if (result != 0) {
            free(current);
            *error = iter_error;
            return -1;
        }

        /* Check if strings are equal */
        if (next_len == current_len && memcmp(next, current, current_len) == 0) {
            free(next);
            *output = current;
            *output_len = current_len;
            return 0;
        }

        /* Strings are different, use the new one for next iteration */
        free(current);
        current = next;
        current_len = next_len;
    }

    /* String did not stabilize after 3 iterations */
    free(current);
    precis_error_init(error, PRECIS_ERROR_INVALID);
    return -1;
}
