/**
 * @file example.c
 * @brief Example usage of PRECIS library
 */

#include "precis.h"
#include <stdio.h>
#include <string.h>

int main(void) {
    precis_string_t output;
    precis_error_t error;
    bool result;

    memset(&output, 0, sizeof(output));
    memset(&error, 0, sizeof(error));

    const char *input = "Guybrush";
    size_t input_len = strlen(input);

    printf("Testing UsernameCaseMapped profile:\n");
    printf("Input: %s\n", input);

    /* Test prepare */
    if (precis_username_case_mapped_prepare(input, input_len, &output, &error) == 0) {
        printf("Prepare: Success (%.*s)\n", (int)output.len, output.data);
        precis_string_free(&output);
    } else {
        printf("Prepare: Error - %s\n", precis_error_message(&error));
    }

    /* Test enforce */
    memset(&output, 0, sizeof(output));
    memset(&error, 0, sizeof(error));
    if (precis_username_case_mapped_enforce(input, input_len, &output, &error) == 0) {
        printf("Enforce: Success (%.*s)\n", (int)output.len, output.data);
        precis_string_free(&output);
    } else {
        printf("Enforce: Error - %s\n", precis_error_message(&error));
    }

    /* Test compare */
    memset(&error, 0, sizeof(error));
    const char *s1 = "Guybrush";
    const char *s2 = "guybrush";
    if (precis_username_case_mapped_compare(s1, strlen(s1), s2, strlen(s2),
                                            &result, &error) == 0) {
        printf("Compare '%s' and '%s': %s\n", s1, s2, result ? "Equal" : "Not equal");
    } else {
        printf("Compare: Error - %s\n", precis_error_message(&error));
    }

    return 0;
}
