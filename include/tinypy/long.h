#ifndef TINYPY_LONG_H
#define TINYPY_LONG_H

#include "tinypy/types.h"

/* Long values use canonical sign-and-magnitude base-2^15 digits internally.
 * Digits are least-significant first, matching the digit payload of CPython
 * 2.7 marshal long objects. sign must be -1, 0 or 1. Canonical zero is the
 * sole combination sign == 0 and digit_count == 0. The digit constructor
 * returns NULL when digit_count cannot be represented by the allocation. */
tinypy_value_t *tinypy_long_from_i64(tinypy_vm_t *vm, int64_t value);
tinypy_value_t *tinypy_long_from_base15_digits(tinypy_vm_t *vm, int32_t sign, const uint16_t *digits, size_t digit_count);

/* The value must fit in int64_t. Range overflow is a C contract violation. */
int64_t tinypy_long_as_i64(const tinypy_value_t *value);

/* Converts a long value to double, rounding to nearest with ties to even.
 * value must be a live TINYPY_VALUE_LONG and out_value must be non-NULL.
 * Returns TINYPY_TRUE and writes out_value on success. If the magnitude is
 * too large, including overflow caused by rounding, returns TINYPY_FALSE,
 * leaves out_value unchanged and raises OverflowError in the value's VM.
 * Optional out_error receives an owned diagnostic on failure or NULL on
 * success. A successful conversion does not clear a pending VM exception. */
tinypy_bool_t tinypy_long_as_double(const tinypy_value_t *value, double *out_value, tinypy_error_t **out_error);

/* The returned base-2^15 digit view is borrowed. */
const uint16_t *tinypy_long_base15_view(const tinypy_value_t *value, int32_t *out_sign, size_t *out_digit_count);

#endif
