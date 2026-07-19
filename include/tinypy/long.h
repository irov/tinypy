#ifndef TINYPY_LONG_H
#define TINYPY_LONG_H

#include "tinypy/types.h"

/* Long values use canonical sign-and-magnitude base-2^15 digits internally.
 * Digits are least-significant first, matching the digit payload of CPython
 * 2.7 marshal long objects. sign must be -1, 0 or 1. Canonical zero is the
 * sole combination sign == 0 and digit_count == 0. */
tinypy_value_t *tinypy_long_from_i64(tinypy_vm_t *vm, int64_t value);
tinypy_value_t *tinypy_long_from_base15_digits(tinypy_vm_t *vm, int sign, const uint16_t *digits, size_t digit_count);

/* The value must fit in int64_t. Range overflow is a C contract violation. */
int64_t tinypy_long_as_i64(const tinypy_value_t *value);

/* The returned base-2^15 digit view is borrowed. */
const uint16_t *tinypy_long_base15_view(const tinypy_value_t *value, int *out_sign, size_t *out_digit_count);

#endif
