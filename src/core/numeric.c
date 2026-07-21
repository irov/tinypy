#include "tinypy/numeric.h"

#include "internal.h"

#include <math.h>

//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_float_from_double(tinypy_vm_t *vm, double value) {
    assert(tinypy_internal_vm_valid(vm));
    if (value == 0.0 && signbit(value) == 0) {
        tinypy_value_t *result = &vm->float_zero_object.base;
        TINYPY_INCREF(result);
        return result;
    }
    tinypy_value_t *result = tinypy_internal_value_allocate(
        vm,
        TINYPY_VALUE_FLOAT,
        sizeof(tinypy_float_object_t));
    TINYPY_FLOAT_OBJECT(result)->value = value;
    return result;
}
//////////////////////////////////////////////////////////////////////////
double tinypy_float_as_double(const tinypy_value_t *value) {
    assert(value != NULL);
    assert(tinypy_internal_vm_valid(TINYPY_VALUE_VM(value)));
    assert(TINYPY_VALUE_KIND(value) == TINYPY_VALUE_FLOAT);

    return TINYPY_FLOAT_OBJECT(value)->value;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_complex_from_doubles(tinypy_vm_t *vm, double real_value, double imaginary_value) {
    assert(tinypy_internal_vm_valid(vm));
    tinypy_value_t *result = tinypy_internal_value_allocate(
        vm,
        TINYPY_VALUE_COMPLEX,
        sizeof(tinypy_complex_object_t));
    TINYPY_COMPLEX_OBJECT(result)->real = real_value;
    TINYPY_COMPLEX_OBJECT(result)->imaginary = imaginary_value;
    return result;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_complex_as_doubles(const tinypy_value_t *value, double *out_real_value, double *out_imaginary_value) {
    assert(value != NULL);
    assert(tinypy_internal_vm_valid(TINYPY_VALUE_VM(value)));
    assert(out_real_value != NULL);
    assert(out_imaginary_value != NULL);
    assert(TINYPY_VALUE_KIND(value) == TINYPY_VALUE_COMPLEX);

    *out_real_value = TINYPY_COMPLEX_OBJECT(value)->real;
    *out_imaginary_value = TINYPY_COMPLEX_OBJECT(value)->imaginary;
}
