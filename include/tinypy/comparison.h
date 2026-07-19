#ifndef TINYPY_COMPARISON_H
#define TINYPY_COMPARISON_H

#include "tinypy/types.h"

typedef enum tinypy_compare_operation_e {
    TINYPY_COMPARE_LESS = 0,
    TINYPY_COMPARE_LESS_EQUAL = 1,
    TINYPY_COMPARE_EQUAL = 2,
    TINYPY_COMPARE_NOT_EQUAL = 3,
    TINYPY_COMPARE_GREATER = 4,
    TINYPY_COMPARE_GREATER_EQUAL = 5,
    TINYPY_COMPARE_IN = 6,
    TINYPY_COMPARE_NOT_IN = 7,
    TINYPY_COMPARE_IS = 8,
    TINYPY_COMPARE_IS_NOT = 9,
    TINYPY_COMPARE_EXCEPTION_MATCH = 10
} tinypy_compare_operation_e;

int32_t tinypy_contains(tinypy_value_t *container, tinypy_value_t *item, tinypy_error_t **out_error);
int32_t tinypy_compare_bool(tinypy_value_t *left, tinypy_value_t *right, tinypy_compare_operation_e operation, tinypy_error_t **out_error);
int32_t tinypy_truth(tinypy_value_t *value, tinypy_error_t **out_error);

#endif
