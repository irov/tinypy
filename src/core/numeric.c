#include "tinypy/numeric.h"

#include "internal.h"

#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_float_from_double(tinypy_vm_t *vm, double value) {
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

    double return_value_1 = TINYPY_FLOAT_OBJECT(value)->value;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_complex_from_doubles(tinypy_vm_t *vm, double real_value, double imaginary_value) {
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

    *out_real_value = TINYPY_COMPLEX_OBJECT(value)->real;
    *out_imaginary_value = TINYPY_COMPLEX_OBJECT(value)->imaginary;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_numeric_method_arguments(tinypy_vm_t *vm, tinypy_value_t *args, tinypy_value_t *kwargs, size_t count, tinypy_error_t **out_error) {
    if ((kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U) || TINYPY_TUPLE_SIZE(args) != count) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "numeric method received invalid arguments", out_error);
        return TINYPY_FALSE;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_numeric_bit_length_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    size_t bits = 0U;

    (void)user_data;
    if (__tinypy_numeric_method_arguments(vm, args, kwargs, 1U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *value = TINYPY_TUPLE_GET(args, 0U);
    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_LONG) {
        size_t count = TINYPY_LONG_DIGIT_COUNT(value);
        uint16_t high;

        if (count != 0U) {
            high = TINYPY_LONG_OBJECT(value)->digits[count - 1U];
            bits = (count - 1U) * 15U;
            while (high != 0U) {
                high >>= 1U;
                bits += 1U;
            }
        }
    }
    else {
        int64_t integer = TINYPY_INTEGER_VALUE(value);
        uint64_t magnitude = integer < 0 ? (uint64_t)(-(integer + 1)) + UINT64_C(1) : (uint64_t)integer;

        while (magnitude != 0U) {
            magnitude >>= 1U;
            bits += 1U;
        }
    }
    if (bits > (size_t)INT64_MAX) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "bit length is too large", out_error);
        return NULL;
    }
    tinypy_value_t *return_value_1 = tinypy_integer_from_i64(vm, (int64_t)bits);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_numeric_field_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    intptr_t field = (intptr_t)user_data;

    if (__tinypy_numeric_method_arguments(vm, args, kwargs, 1U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *value = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);
    if (kind == TINYPY_VALUE_COMPLEX) {
        double component = field == 0 ? TINYPY_COMPLEX_OBJECT(value)->real : TINYPY_COMPLEX_OBJECT(value)->imaginary;

        tinypy_value_t *return_value_1 = tinypy_float_from_double(vm, component);
        return return_value_1;
    }
    if (kind == TINYPY_VALUE_FLOAT) {
        if (field == 0) {
            TINYPY_INCREF(value);
            return value;
        }
        tinypy_value_t *return_value_1 = tinypy_float_from_double(vm, 0.0);
        return return_value_1;
    }
    if (kind == TINYPY_VALUE_LONG) {
        if (field == 0 || field == 2) {
            TINYPY_INCREF(value);
            return value;
        }
        tinypy_value_t *return_value_1 = tinypy_long_from_i64(vm, field == 3 ? INT64_C(1) : INT64_C(0));
        return return_value_1;
    }
    if (kind == TINYPY_VALUE_BOOL) {
        tinypy_value_t *return_value_1 = tinypy_integer_from_i64(vm, field == 3 ? INT64_C(1) : (field == 1 ? INT64_C(0) : TINYPY_INTEGER_VALUE(value)));
        return return_value_1;
    }
    if (field == 0 || field == 2) {
        TINYPY_INCREF(value);
        return value;
    }
    tinypy_value_t *return_value_1 = tinypy_integer_from_i64(vm, field == 3 ? INT64_C(1) : INT64_C(0));
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_numeric_conjugate_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_numeric_method_arguments(vm, args, kwargs, 1U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *value = TINYPY_TUPLE_GET(args, 0U);
    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_COMPLEX) {
        tinypy_value_t *return_value_1 = tinypy_complex_from_doubles(vm, TINYPY_COMPLEX_OBJECT(value)->real, -TINYPY_COMPLEX_OBJECT(value)->imaginary);
        return return_value_1;
    }
    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_BOOL) {
        tinypy_value_t *return_value_1 = tinypy_integer_from_i64(vm, TINYPY_INTEGER_VALUE(value));
        return return_value_1;
    }
    TINYPY_INCREF(value);
    return value;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_float_is_integer_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_numeric_method_arguments(vm, args, kwargs, 1U, out_error) == 0) {
        return NULL;
    }
    double value = TINYPY_FLOAT_OBJECT(TINYPY_TUPLE_GET(args, 0U))->value;
    tinypy_value_t *return_value_1 = tinypy_bool_from_i32(vm, isfinite(value) != 0 && trunc(value) == value ? INT32_C(1) : INT32_C(0));
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_float_ratio_shift(tinypy_vm_t *vm, tinypy_value_t *value, int32_t shift, tinypy_error_t **out_error) {
    if (shift == 0) {
        return value;
    }
    tinypy_value_t *shift_value = tinypy_integer_from_i64(vm, shift);
    tinypy_value_t *result = tinypy_left_shift(value, shift_value, out_error);
    TINYPY_DECREF(shift_value);
    TINYPY_DECREF(value);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_float_as_integer_ratio_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    int exponent;
    double fraction;

    (void)user_data;
    if (__tinypy_numeric_method_arguments(vm, args, kwargs, 1U, out_error) == 0) {
        return NULL;
    }
    double value = TINYPY_FLOAT_OBJECT(TINYPY_TUPLE_GET(args, 0U))->value;
    if (isnan(value)) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "cannot convert NaN to integer ratio", out_error);
        return NULL;
    }
    if (isinf(value)) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "cannot convert Infinity to integer ratio", out_error);
        return NULL;
    }
    if (value == 0.0) {
        tinypy_value_t *items[2] = {tinypy_integer_from_i64(vm, INT64_C(0)), tinypy_integer_from_i64(vm, INT64_C(1))};
        tinypy_value_t *result = tinypy_tuple_from_items(vm, items, 2U);
        TINYPY_DECREF(items[1]);
        TINYPY_DECREF(items[0]);
        return result;
    }
    fraction = frexp(value, &exponent);
    while (trunc(fraction) != fraction) {
        fraction *= 2.0;
        exponent -= 1;
    }
    tinypy_value_t *numerator = tinypy_integer_from_i64(vm, (int64_t)fraction);
    tinypy_value_t *denominator = tinypy_integer_from_i64(vm, INT64_C(1));
    if (exponent > 0) {
        numerator = __tinypy_float_ratio_shift(vm, numerator, exponent, out_error);
        if (numerator == NULL) {
            TINYPY_DECREF(denominator);
            return NULL;
        }
    }
    else if (exponent < 0) {
        denominator = __tinypy_float_ratio_shift(vm, denominator, -exponent, out_error);
        if (denominator == NULL) {
            TINYPY_DECREF(numerator);
            return NULL;
        }
    }
    tinypy_value_t *items[2] = {numerator, denominator};
    tinypy_value_t *result = tinypy_tuple_from_items(vm, items, 2U);
    TINYPY_DECREF(denominator);
    TINYPY_DECREF(numerator);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_float_hex_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    static const char digits[] = "0123456789abcdef";
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    uint64_t bits;
    uint64_t fraction;
    uint32_t encoded_exponent;
    int32_t exponent;
    char text[32];
    size_t size = 0U;
    size_t index;

    (void)user_data;
    if (__tinypy_numeric_method_arguments(vm, args, kwargs, 1U, out_error) == 0) {
        return NULL;
    }
    double value = TINYPY_FLOAT_OBJECT(TINYPY_TUPLE_GET(args, 0U))->value;
    if (isnan(value)) {
        tinypy_value_t *return_value_1 = tinypy_string_from_bytes(vm, "nan", 3U);
        return return_value_1;
    }
    if (signbit(value) != 0) {
        text[size++] = '-';
    }
    if (isinf(value)) {
        (void)memcpy(text + size, "inf", 3U);
        tinypy_value_t *return_value_1 = tinypy_string_from_bytes(vm, text, size + 3U);
        return return_value_1;
    }
    (void)memcpy(&bits, &value, sizeof(bits));
    fraction = bits & UINT64_C(0x000fffffffffffff);
    encoded_exponent = (uint32_t)((bits >> 52U) & UINT64_C(0x7ff));
    text[size++] = '0';
    text[size++] = 'x';
    if (encoded_exponent == 0U) {
        text[size++] = '0';
        exponent = -1022;
    }
    else {
        text[size++] = '1';
        exponent = (int32_t)encoded_exponent - 1023;
    }
    text[size++] = '.';
    if (encoded_exponent == 0U && fraction == 0U) {
        text[size++] = '0';
        exponent = 0;
    }
    else {
        for (index = 0U; index < 13U; ++index) {
            size_t shift = 48U - index * 4U;

            text[size++] = digits[(fraction >> shift) & UINT64_C(0xf)];
        }
    }
    text[size++] = 'p';
    text[size++] = exponent < 0 ? '-' : '+';
    if (exponent < 0) {
        exponent = -exponent;
    }
    char reverse[5];
    size_t exponent_size = 0U;
    do {
        reverse[exponent_size++] = (char)('0' + exponent % 10);
        exponent /= 10;
    } while (exponent != 0);
    while (exponent_size != 0U) {
        text[size++] = reverse[--exponent_size];
    }
    tinypy_value_t *return_value_1 = tinypy_string_from_bytes(vm, text, size);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_float_hex_space(uint8_t character) {
    return character == (uint8_t)' ' || character == (uint8_t)'\t' || character == (uint8_t)'\n' || character == (uint8_t)'\r' || character == (uint8_t)'\v' || character == (uint8_t)'\f' ? TINYPY_TRUE : TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_float_fromhex_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    size_t begin = 0U;
    size_t end;
    char *local;
    char *parse_end;
    double number;

    (void)user_data;
    if ((kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U) || TINYPY_TUPLE_SIZE(args) != 2U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "fromhex() requires one string argument", out_error);
        return NULL;
    }
    tinypy_value_t *class_value = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *source = TINYPY_TUPLE_GET(args, 1U);
    if (TINYPY_VALUE_KIND(class_value) != TINYPY_VALUE_TYPE || (TINYPY_VALUE_KIND(source) != TINYPY_VALUE_STRING && TINYPY_VALUE_KIND(source) != TINYPY_VALUE_UNICODE)) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "fromhex() requires one string argument", out_error);
        return NULL;
    }
    const uint8_t *bytes = TINYPY_TEXT_BYTES(source);
    end = TINYPY_TEXT_BYTE_SIZE(source);
    while (begin < end && __tinypy_float_hex_space(bytes[begin]) != 0) {
        begin += 1U;
    }
    while (end > begin && __tinypy_float_hex_space(bytes[end - 1U]) != 0) {
        end -= 1U;
    }
    if (begin == end || end - begin == SIZE_MAX) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "invalid hexadecimal floating-point string", out_error);
        return NULL;
    }
    size_t allocation_size = end - begin + 1U;
    local = (char *)vm->allocator.allocate(vm->allocator.user_data, allocation_size, TINYPY_INTERNAL_ALIGNMENT);
    (void)memcpy(local, bytes + begin, end - begin);
    local[end - begin] = '\0';
    errno = 0;
    number = strtod(local, &parse_end);
    tinypy_bool_t valid = parse_end == local + (end - begin) ? TINYPY_TRUE : TINYPY_FALSE;
    int32_t range_error = errno == ERANGE && isinf(number) ? INT32_C(1) : INT32_C(0);
    vm->allocator.deallocate(vm->allocator.user_data, local, allocation_size, TINYPY_INTERNAL_ALIGNMENT);
    if (valid == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "invalid hexadecimal floating-point string", out_error);
        return NULL;
    }
    if (range_error != 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "hexadecimal value too large to represent as a float", out_error);
        return NULL;
    }
    tinypy_value_t *result = tinypy_float_from_double(vm, number);
    if ((tinypy_type_t *)class_value == &vm->types[TINYPY_VALUE_FLOAT]) {
        return result;
    }
    tinypy_value_t *call_args = tinypy_tuple_from_items(vm, &result, 1U);
    tinypy_value_t *subclass_result = tinypy_call(class_value, call_args, NULL, out_error);
    TINYPY_DECREF(call_args);
    TINYPY_DECREF(result);
    return subclass_result;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_numeric_add_method(tinypy_type_t *type, const char *name, size_t name_size, tinypy_native_function_callback_t callback, void *user_data) {
    tinypy_value_t *function = tinypy_native_function_new(type->vm, name, name_size, callback, user_data, NULL);

    tinypy_type_set_attr(type, name, name_size, function);
    TINYPY_DECREF(function);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_numeric_add_property(tinypy_type_t *type, const char *name, size_t name_size, intptr_t field) {
    tinypy_value_t *function = tinypy_native_function_new(type->vm, name, name_size, __tinypy_numeric_field_method, (void *)field, NULL);
    tinypy_value_t *property = tinypy_property_new(type->vm, function, NULL, NULL, NULL);

    tinypy_type_set_attr(type, name, name_size, property);
    TINYPY_DECREF(property);
    TINYPY_DECREF(function);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_numeric_add_class_method(tinypy_type_t *type, const char *name, size_t name_size, tinypy_native_function_callback_t callback) {
    tinypy_value_t *function = tinypy_native_function_new(type->vm, name, name_size, callback, NULL, NULL);
    tinypy_value_t *descriptor = tinypy_class_method_new(function);

    tinypy_type_set_attr(type, name, name_size, descriptor);
    TINYPY_DECREF(descriptor);
    TINYPY_DECREF(function);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_numeric_trunc_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_numeric_method_arguments(vm, args, kwargs, 1U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *self = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(self);
    if (kind == TINYPY_VALUE_INTEGER || kind == TINYPY_VALUE_LONG) {
        TINYPY_INCREF(self);
        tinypy_value_t *return_value_1 = tinypy_internal_immutable_subclass_copy(&vm->types[kind], self, out_error);
        return return_value_1;
    }
    tinypy_value_t *constructor_args = tinypy_tuple_from_items(vm, &self, 1U);
    tinypy_value_t *result = tinypy_internal_integer_create(&vm->types[TINYPY_VALUE_INTEGER], constructor_args, NULL, out_error);
    TINYPY_DECREF(constructor_args);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_numeric_integer_base_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    intptr_t base = (intptr_t)user_data;
    tinypy_bool_t result_unicode;

    if (__tinypy_numeric_method_arguments(vm, args, kwargs, 1U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *self = TINYPY_TUPLE_GET(args, 0U);
    const uint8_t *spec = base == 8 ? (const uint8_t *)"#o" : (const uint8_t *)"#x";
    tinypy_value_t *formatted = tinypy_internal_string_format_value(vm, self, 0, spec, 2U, TINYPY_FALSE, &result_unicode, out_error);
    if (formatted == NULL) {
        return NULL;
    }
    const uint8_t *bytes = TINYPY_TEXT_BYTES(formatted);
    size_t size = TINYPY_TEXT_BYTE_SIZE(formatted);
    size_t prefix = size >= 3U && bytes[0] == (uint8_t)'-' ? 1U : 0U;
    tinypy_bool_t long_suffix = TINYPY_VALUE_KIND(self) == TINYPY_VALUE_LONG ? TINYPY_TRUE : TINYPY_FALSE;
    tinypy_bool_t octal_zero = base == 8 && size - prefix == 3U && bytes[prefix] == (uint8_t)'0' && bytes[prefix + 1U] == (uint8_t)'o' && bytes[prefix + 2U] == (uint8_t)'0' ? TINYPY_TRUE : TINYPY_FALSE;
    size_t result_size = size + (long_suffix != 0 ? 1U : 0U) - (base == 8 ? 1U : 0U) - (octal_zero != 0 ? 1U : 0U);
    uint8_t *output;
    tinypy_value_t *result = tinypy_internal_text_allocate_uninitialized(vm, TINYPY_VALUE_STRING, result_size, result_size, &output);
    size_t output_index = 0U;

    if (prefix != 0U) {
        output[output_index++] = (uint8_t)'-';
    }
    if (base == 8) {
        output[output_index++] = (uint8_t)'0';
        if (octal_zero == 0) {
            (void)memcpy(output + output_index, bytes + prefix + 2U, size - prefix - 2U);
            output_index += size - prefix - 2U;
        }
    }
    else {
        (void)memcpy(output + output_index, bytes + prefix, size - prefix);
        output_index += size - prefix;
    }
    if (long_suffix != 0) {
        output[output_index] = (uint8_t)'L';
    }
    TINYPY_DECREF(formatted);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_float_getformat_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    uint16_t byteorder_probe = UINT16_C(1);

    (void)user_data;
    if ((kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U) || TINYPY_TUPLE_SIZE(args) != 2U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "float.__getformat__ requires a format kind", out_error);
        return NULL;
    }
    tinypy_value_t *kind = TINYPY_TUPLE_GET(args, 1U);
    if (TINYPY_VALUE_KIND(kind) != TINYPY_VALUE_STRING || (TINYPY_TEXT_BYTE_SIZE(kind) != 5U && TINYPY_TEXT_BYTE_SIZE(kind) != 6U) || (memcmp(TINYPY_TEXT_BYTES(kind), "float", 5U) != 0 && memcmp(TINYPY_TEXT_BYTES(kind), "double", 6U) != 0)) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "unknown float format", out_error);
        return NULL;
    }
    const char *format = *((const uint8_t *)&byteorder_probe) == 1U ? "IEEE, little-endian" : "IEEE, big-endian";
    tinypy_value_t *return_value_1 = tinypy_string_from_bytes(vm, format, strlen(format));
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_float_setformat_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    uint16_t byteorder_probe = UINT16_C(1);

    (void)user_data;
    if ((kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U) || TINYPY_TUPLE_SIZE(args) != 3U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "float.__setformat__ requires kind and format", out_error);
        return NULL;
    }
    tinypy_value_t *kind = TINYPY_TUPLE_GET(args, 1U);
    tinypy_value_t *format_value = TINYPY_TUPLE_GET(args, 2U);
    if (TINYPY_VALUE_KIND(kind) != TINYPY_VALUE_STRING || TINYPY_VALUE_KIND(format_value) != TINYPY_VALUE_STRING || ((TINYPY_TEXT_BYTE_SIZE(kind) != 5U || memcmp(TINYPY_TEXT_BYTES(kind), "float", 5U) != 0) && (TINYPY_TEXT_BYTE_SIZE(kind) != 6U || memcmp(TINYPY_TEXT_BYTES(kind), "double", 6U) != 0))) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "unknown float format", out_error);
        return NULL;
    }
    const char *native_format = *((const uint8_t *)&byteorder_probe) == 1U ? "IEEE, little-endian" : "IEEE, big-endian";
    size_t native_size = strlen(native_format);
    tinypy_bool_t unknown = TINYPY_TEXT_BYTE_SIZE(format_value) == 7U && memcmp(TINYPY_TEXT_BYTES(format_value), "unknown", 7U) == 0;
    tinypy_bool_t native = TINYPY_TEXT_BYTE_SIZE(format_value) == native_size && memcmp(TINYPY_TEXT_BYTES(format_value), native_format, native_size) == 0;
    if (unknown == 0 && native == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "can only set the native float format", out_error);
        return NULL;
    }
    tinypy_value_t *return_value_1 = tinypy_none_get(vm);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_initialize_numeric_types(tinypy_vm_t *vm) {
    tinypy_type_t *integer_types[2] = {&vm->types[TINYPY_VALUE_INTEGER], &vm->types[TINYPY_VALUE_LONG]};
    size_t index;

    for (index = 0U; index < 2U; ++index) {
        __tinypy_numeric_add_method(integer_types[index], "bit_length", 10U, __tinypy_numeric_bit_length_method, NULL);
        __tinypy_numeric_add_method(integer_types[index], "conjugate", 9U, __tinypy_numeric_conjugate_method, NULL);
        __tinypy_numeric_add_method(integer_types[index], "__trunc__", 9U, __tinypy_numeric_trunc_method, NULL);
        __tinypy_numeric_add_method(integer_types[index], "__hex__", 7U, __tinypy_numeric_integer_base_method, (void *)(intptr_t)16);
        __tinypy_numeric_add_method(integer_types[index], "__oct__", 7U, __tinypy_numeric_integer_base_method, (void *)(intptr_t)8);
        __tinypy_numeric_add_property(integer_types[index], "real", 4U, 0);
        __tinypy_numeric_add_property(integer_types[index], "imag", 4U, 1);
        __tinypy_numeric_add_property(integer_types[index], "numerator", 9U, 2);
        __tinypy_numeric_add_property(integer_types[index], "denominator", 11U, 3);
    }
    __tinypy_numeric_add_method(&vm->types[TINYPY_VALUE_FLOAT], "conjugate", 9U, __tinypy_numeric_conjugate_method, NULL);
    __tinypy_numeric_add_method(&vm->types[TINYPY_VALUE_FLOAT], "is_integer", 10U, __tinypy_float_is_integer_method, NULL);
    __tinypy_numeric_add_method(&vm->types[TINYPY_VALUE_FLOAT], "as_integer_ratio", 16U, __tinypy_float_as_integer_ratio_method, NULL);
    __tinypy_numeric_add_method(&vm->types[TINYPY_VALUE_FLOAT], "hex", 3U, __tinypy_float_hex_method, NULL);
    __tinypy_numeric_add_method(&vm->types[TINYPY_VALUE_FLOAT], "__trunc__", 9U, __tinypy_numeric_trunc_method, NULL);
    __tinypy_numeric_add_class_method(&vm->types[TINYPY_VALUE_FLOAT], "fromhex", 7U, __tinypy_float_fromhex_method);
    __tinypy_numeric_add_class_method(&vm->types[TINYPY_VALUE_FLOAT], "__getformat__", 13U, __tinypy_float_getformat_method);
    __tinypy_numeric_add_class_method(&vm->types[TINYPY_VALUE_FLOAT], "__setformat__", 13U, __tinypy_float_setformat_method);
    __tinypy_numeric_add_property(&vm->types[TINYPY_VALUE_FLOAT], "real", 4U, 0);
    __tinypy_numeric_add_property(&vm->types[TINYPY_VALUE_FLOAT], "imag", 4U, 1);
    __tinypy_numeric_add_method(&vm->types[TINYPY_VALUE_COMPLEX], "conjugate", 9U, __tinypy_numeric_conjugate_method, NULL);
    __tinypy_numeric_add_property(&vm->types[TINYPY_VALUE_COMPLEX], "real", 4U, 0);
    __tinypy_numeric_add_property(&vm->types[TINYPY_VALUE_COMPLEX], "imag", 4U, 1);
}
