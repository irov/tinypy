#ifndef TINYPY_NUMERIC_H
#define TINYPY_NUMERIC_H

#include "tinypy/types.h"

/* Double object representations are copied bit-for-bit. This preserves
 * signed zero and NaN payloads on the host IEEE-754 double ABI. */
tinypy_value_t *tinypy_float_from_double(tinypy_vm_t *vm, double value);
double tinypy_float_as_double(const tinypy_value_t *value);
tinypy_value_t *tinypy_complex_from_doubles(tinypy_vm_t *vm, double real_value, double imaginary_value);
void tinypy_complex_as_doubles(const tinypy_value_t *value, double *out_real_value, double *out_imaginary_value);

#endif
