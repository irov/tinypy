#include "tinypy/numeric.h"

#include "internal.h"

#include <math.h>

tinypy_value_t *tinypy_float_from_double(tinypy_vm_t *vm, double value)
{
    tinypy_value_t *result;

    assert(tinypy_internal_vm_valid(vm));
    if (value == 0.0 && signbit(value) == 0) {
        result = &vm->float_zero_object.base;
        tinypy_retain(result);
        return result;
    }
    result = tinypy_internal_value_allocate(
        vm,
        TINYPY_VALUE_FLOAT,
        sizeof(tinypy_float_object_t));
    TINYPY_FLOAT_OBJECT(result)->value = value;
    return result;
}

double tinypy_float_as_double(const tinypy_value_t *value)
{
    assert(value != NULL);
    assert(tinypy_internal_vm_valid(tinypy_internal_value_vm(value)));
    assert(tinypy_internal_value_kind(value) == TINYPY_VALUE_FLOAT);

    return TINYPY_FLOAT_OBJECT(value)->value;
}

tinypy_value_t *tinypy_complex_from_doubles(
    tinypy_vm_t *vm,
    double real_value,
    double imaginary_value)
{
    tinypy_value_t *result;

    assert(tinypy_internal_vm_valid(vm));
    result = tinypy_internal_value_allocate(
        vm,
        TINYPY_VALUE_COMPLEX,
        sizeof(tinypy_complex_object_t));
    TINYPY_COMPLEX_OBJECT(result)->real = real_value;
    TINYPY_COMPLEX_OBJECT(result)->imaginary = imaginary_value;
    return result;
}

void tinypy_complex_as_doubles(
    const tinypy_value_t *value,
    double *out_real_value,
    double *out_imaginary_value)
{
    assert(value != NULL);
    assert(tinypy_internal_vm_valid(tinypy_internal_value_vm(value)));
    assert(out_real_value != NULL);
    assert(out_imaginary_value != NULL);
    assert(tinypy_internal_value_kind(value) == TINYPY_VALUE_COMPLEX);

    *out_real_value = TINYPY_COMPLEX_OBJECT(value)->real;
    *out_imaginary_value = TINYPY_COMPLEX_OBJECT(value)->imaginary;
}
