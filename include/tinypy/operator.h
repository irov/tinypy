#ifndef TINYPY_OPERATOR_H
#define TINYPY_OPERATOR_H

#include "tinypy/types.h"

tinypy_value_t *tinypy_positive(tinypy_value_t *value, tinypy_error_t **out_error);
tinypy_value_t *tinypy_negative(tinypy_value_t *value, tinypy_error_t **out_error);
tinypy_value_t *tinypy_invert(tinypy_value_t *value, tinypy_error_t **out_error);
tinypy_value_t *tinypy_add(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error);
tinypy_value_t *tinypy_subtract(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error);
tinypy_value_t *tinypy_multiply(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error);
tinypy_value_t *tinypy_divide(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error);
tinypy_value_t *tinypy_inplace_add(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error);
tinypy_value_t *tinypy_inplace_subtract(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error);
tinypy_value_t *tinypy_inplace_multiply(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error);
tinypy_value_t *tinypy_inplace_divide(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error);
tinypy_value_t *tinypy_inplace_floor_divide(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error);
tinypy_value_t *tinypy_inplace_true_divide(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error);
tinypy_value_t *tinypy_inplace_remainder(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error);
tinypy_value_t *tinypy_inplace_power(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error);
tinypy_value_t *tinypy_inplace_left_shift(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error);
tinypy_value_t *tinypy_inplace_right_shift(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error);
tinypy_value_t *tinypy_inplace_bit_and(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error);
tinypy_value_t *tinypy_inplace_bit_xor(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error);
tinypy_value_t *tinypy_inplace_bit_or(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error);
tinypy_value_t *tinypy_floor_divide(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error);
tinypy_value_t *tinypy_true_divide(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error);
tinypy_value_t *tinypy_remainder(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error);
tinypy_value_t *tinypy_divmod(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error);
tinypy_value_t *tinypy_power(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error);
tinypy_value_t *tinypy_left_shift(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error);
tinypy_value_t *tinypy_right_shift(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error);
tinypy_value_t *tinypy_bit_and(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error);
tinypy_value_t *tinypy_bit_xor(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error);
tinypy_value_t *tinypy_bit_or(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error);
/* PyNumber_AsSsize_t: the index of an int, a long or an object with
 * __index__. Any other value raises TypeError, and an index that int64_t
 * cannot hold raises overflow_kind, as IndexError for an item index. */
tinypy_bool_t tinypy_number_as_index(tinypy_value_t *value, tinypy_error_kind_e overflow_kind, int64_t *out_index, tinypy_error_t **out_error);

#endif
