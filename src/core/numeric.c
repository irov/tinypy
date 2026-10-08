#include "tinypy/numeric.h"

#include "internal.h"

#include <errno.h>
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_float_from_double(tinypy_vm_t *vm, double value) {
    if (value == 0.0 && signbit(value) == 0) {
        tinypy_value_t *result = TINYPY_RET(&vm->float_zero_object.base);
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
static tinypy_value_t *__tinypy_numeric_bit_length_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    size_t bits = 0U;

    (void)user_data;
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 0U, 0U, TINYPY_ARITY_STYLE_PARSED, out_error) == 0) {
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
#if SIZE_MAX > INT64_MAX
    if (bits > (size_t)INT64_MAX) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "bit length is too large", out_error);
        return NULL;
    }
#endif
    tinypy_value_t *return_value_1 = tinypy_integer_from_i64(vm, (int64_t)bits);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_numeric_field(tinypy_value_t *value, int32_t field, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);
    if (kind == TINYPY_VALUE_COMPLEX) {
        double component = field == 0 ? TINYPY_COMPLEX_OBJECT(value)->real : TINYPY_COMPLEX_OBJECT(value)->imaginary;

        tinypy_value_t *return_value_1 = tinypy_float_from_double(vm, component);
        return return_value_1;
    }
    if (kind == TINYPY_VALUE_FLOAT) {
        if (field == 0) {
            TINYPY_INCREF(value);
            tinypy_value_t *result = tinypy_internal_immutable_subclass_copy(&vm->types[kind], value, out_error);
            return result;
        }
        tinypy_value_t *return_value_1 = tinypy_float_from_double(vm, 0.0);
        return return_value_1;
    }
    if (kind == TINYPY_VALUE_LONG) {
        if (field == 0 || field == 2) {
            TINYPY_INCREF(value);
            tinypy_value_t *result = tinypy_internal_immutable_subclass_copy(&vm->types[kind], value, out_error);
            return result;
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
        tinypy_value_t *result = tinypy_internal_immutable_subclass_copy(&vm->types[kind], value, out_error);
        return result;
    }
    tinypy_value_t *return_value_1 = tinypy_integer_from_i64(vm, field == 3 ? INT64_C(1) : INT64_C(0));
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_numeric_conjugate_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 0U, 0U, TINYPY_ARITY_STYLE_PARSED, out_error) == 0) {
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
    tinypy_value_t *result = tinypy_internal_immutable_subclass_copy(&vm->types[TINYPY_VALUE_KIND(value)], value, out_error);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_numeric_getnewargs_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *items[2];
    size_t item_count = 1U;
    tinypy_value_type_e kind;

    (void)user_data;
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 0U, 0U, TINYPY_ARITY_STYLE_PARSED, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *value = TINYPY_TUPLE_GET(args, 0U);
    kind = TINYPY_VALUE_KIND(value);
    if (kind == TINYPY_VALUE_BOOL || kind == TINYPY_VALUE_INTEGER) {
        items[0] = tinypy_integer_from_i64(vm, TINYPY_INTEGER_VALUE(value));
    }
    else if (kind == TINYPY_VALUE_LONG) {
        items[0] = tinypy_internal_long_from_base15_digits_checked(vm, TINYPY_LONG_SIGN(value), TINYPY_LONG_OBJECT(value)->digits, TINYPY_LONG_DIGIT_COUNT(value), out_error);
    }
    else if (kind == TINYPY_VALUE_FLOAT || kind == TINYPY_VALUE_COMPLEX) {
        item_count = kind == TINYPY_VALUE_COMPLEX ? 2U : 1U;
        for (size_t index = 0U; index < item_count; ++index) {
            items[index] = tinypy_internal_object_allocate_checked(vm, &vm->types[TINYPY_VALUE_FLOAT], sizeof(tinypy_float_object_t), out_error);
            if (items[index] == NULL) {
                if (index != 0U) {
                    TINYPY_DECREF(items[0]);
                }
                return NULL;
            }
            TINYPY_FLOAT_OBJECT(items[index])->value = kind == TINYPY_VALUE_FLOAT ? TINYPY_FLOAT_OBJECT(value)->value : (index == 0U ? TINYPY_COMPLEX_OBJECT(value)->real : TINYPY_COMPLEX_OBJECT(value)->imaginary);
        }
    }
    else {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "__getnewargs__ requires a numeric object", out_error);
        return NULL;
    }
    if (items[0] == NULL) {
        return NULL;
    }
    tinypy_value_t *result = tinypy_tuple_from_items(vm, items, item_count);
    if (item_count == 2U) {
        TINYPY_DECREF(items[1]);
    }
    TINYPY_DECREF(items[0]);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_numeric_cmp_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_type_e expected = (tinypy_value_type_e)(intptr_t)user_data;
    int32_t equal;
    int32_t less;

    if (tinypy_internal_native_method_arguments(function, args, kwargs, 1U, 1U, TINYPY_ARITY_STYLE_WRAPPER, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *left = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *right = TINYPY_TUPLE_GET(args, 1U);
    tinypy_bool_t left_valid = expected == TINYPY_VALUE_INTEGER ? (TINYPY_VALUE_KIND(left) == TINYPY_VALUE_BOOL || TINYPY_VALUE_KIND(left) == TINYPY_VALUE_INTEGER) : TINYPY_VALUE_KIND(left) == expected;
    tinypy_bool_t right_valid = expected == TINYPY_VALUE_INTEGER ? (TINYPY_VALUE_KIND(right) == TINYPY_VALUE_BOOL || TINYPY_VALUE_KIND(right) == TINYPY_VALUE_INTEGER) : TINYPY_VALUE_KIND(right) == expected;

    if (left_valid == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "numeric comparison descriptor received an incompatible object", out_error);
        return NULL;
    }
    if (right_valid == 0) {
        tinypy_type_t *type = &vm->types[expected];
        const tinypy_message_part_t parts[] = {
            {type->name, type->name_size},
            TINYPY_MESSAGE_PART_LITERAL(".__cmp__(x,y) requires y to be a '"),
            {type->name, type->name_size},
            TINYPY_MESSAGE_PART_LITERAL("', not a '"),
            TINYPY_MESSAGE_PART_TYPE_NAME(right),
            TINYPY_MESSAGE_PART_LITERAL("'")
        };

        tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, 6U, out_error);
        return NULL;
    }
    tinypy_value_t *comparison = tinypy_internal_compare_builtin_value(left, right, TINYPY_COMPARE_EQUAL, out_error);
    if (comparison == NULL) {
        return NULL;
    }
    equal = tinypy_bool_as_i32(comparison);
    TINYPY_DECREF(comparison);
    if (equal != 0) {
        tinypy_value_t *result = tinypy_integer_from_i64(vm, INT64_C(0));

        return result;
    }
    comparison = tinypy_internal_compare_builtin_value(left, right, TINYPY_COMPARE_LESS, out_error);
    if (comparison == NULL) {
        return NULL;
    }
    less = tinypy_bool_as_i32(comparison);
    TINYPY_DECREF(comparison);
    tinypy_value_t *result = tinypy_integer_from_i64(vm, less != 0 ? INT64_C(-1) : INT64_C(1));

    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_numeric_coerce_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_type_e target = (tinypy_value_type_e)(intptr_t)user_data;
    tinypy_value_t *converted = NULL;
    tinypy_bool_t compatible = TINYPY_FALSE;

    if (tinypy_internal_native_method_arguments(function, args, kwargs, 1U, 1U, TINYPY_ARITY_STYLE_WRAPPER, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *self = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *other = TINYPY_TUPLE_GET(args, 1U);
    tinypy_value_type_e self_kind = TINYPY_VALUE_KIND(self);
    tinypy_value_type_e other_kind = TINYPY_VALUE_KIND(other);
    tinypy_bool_t self_valid = target == TINYPY_VALUE_INTEGER ? (self_kind == TINYPY_VALUE_BOOL || self_kind == TINYPY_VALUE_INTEGER) : self_kind == target;

    if (self_valid == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "numeric coercion descriptor received an incompatible object", out_error);
        return NULL;
    }
    if (target == TINYPY_VALUE_INTEGER) {
        compatible = other_kind == TINYPY_VALUE_BOOL || other_kind == TINYPY_VALUE_INTEGER;
    }
    else if (target == TINYPY_VALUE_LONG) {
        compatible = other_kind == TINYPY_VALUE_BOOL || other_kind == TINYPY_VALUE_INTEGER || other_kind == TINYPY_VALUE_LONG;
    }
    else if (target == TINYPY_VALUE_FLOAT) {
        compatible = other_kind == TINYPY_VALUE_BOOL || other_kind == TINYPY_VALUE_INTEGER || other_kind == TINYPY_VALUE_LONG || other_kind == TINYPY_VALUE_FLOAT;
    }
    else if (target == TINYPY_VALUE_COMPLEX) {
        compatible = other_kind == TINYPY_VALUE_BOOL || other_kind == TINYPY_VALUE_INTEGER || other_kind == TINYPY_VALUE_LONG || other_kind == TINYPY_VALUE_FLOAT || other_kind == TINYPY_VALUE_COMPLEX;
    }
    if (compatible == 0) {
        tinypy_value_t *result = TINYPY_RET_NOT_IMPLEMENTED(vm);
        return result;
    }
    if (other_kind == target || (target == TINYPY_VALUE_INTEGER && (other_kind == TINYPY_VALUE_BOOL || other_kind == TINYPY_VALUE_INTEGER))) {
        converted = TINYPY_RET(other);
    }
    else {
        if (target == TINYPY_VALUE_LONG) {
            converted = tinypy_long_from_i64(vm, TINYPY_INTEGER_VALUE(other));
        }
        else {
            double number;

            if (other_kind == TINYPY_VALUE_LONG) {
                if (tinypy_long_as_double(other, &number, out_error) == 0) {
                    return NULL;
                }
            }
            else if (other_kind == TINYPY_VALUE_FLOAT) {
                number = TINYPY_FLOAT_OBJECT(other)->value;
            }
            else {
                number = (double)TINYPY_INTEGER_VALUE(other);
            }
            converted = target == TINYPY_VALUE_FLOAT ? tinypy_float_from_double(vm, number) : tinypy_complex_from_doubles(vm, number, 0.0);
        }
        if (converted == NULL) {
            return NULL;
        }
    }
    tinypy_value_t *items[2] = {self, converted};
    tinypy_value_t *result = tinypy_tuple_from_items(vm, items, 2U);
    TINYPY_DECREF(converted);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_float_is_integer_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 0U, 0U, TINYPY_ARITY_STYLE_PARSED, out_error) == 0) {
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
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 0U, 0U, TINYPY_ARITY_STYLE_PARSED, out_error) == 0) {
        return NULL;
    }
    double value = TINYPY_FLOAT_OBJECT(TINYPY_TUPLE_GET(args, 0U))->value;
    if (isnan(value)) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "Cannot pass NaN to float.as_integer_ratio.", out_error);
        return NULL;
    }
    if (isinf(value)) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "Cannot pass infinity to float.as_integer_ratio.", out_error);
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
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 0U, 0U, TINYPY_ARITY_STYLE_PARSED, out_error) == 0) {
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
static int32_t __tinypy_float_hex_digit(uint8_t byte) {
    if (byte >= '0' && byte <= '9') {
        return byte - '0';
    }
    if (byte >= 'a' && byte <= 'f') {
        return byte - 'a' + 10;
    }
    if (byte >= 'A' && byte <= 'F') {
        return byte - 'A' + 10;
    }
    return -1;
}
//////////////////////////////////////////////////////////////////////////
/* Keep the representable bits, a rounding bit and a sticky bit. No decimal
   conversion or process locale participates in hexadecimal conversion. */
static tinypy_bool_t __tinypy_float_parse_hex(tinypy_vm_t *vm, const uint8_t *bytes, size_t size, double *out_number, tinypy_error_t **out_error) {
    size_t begin = 0U;
    size_t end = size;
    size_t cursor;
    size_t coefficient_begin;
    size_t coefficient_end;
    size_t fraction_digits = 0U;
    size_t significant_digits = 0U;
    size_t leading_zeros = 0U;
    size_t digit_count = 0U;
    int32_t first_digit = 0;
    int32_t leading_bits = 0;
    tinypy_bool_t negative = TINYPY_FALSE;
    tinypy_bool_t dot = TINYPY_FALSE;
    int64_t exponent = 0;
    int64_t top;
    int64_t least;
    uint64_t significand = 0U;
    size_t kept = 0U;
    size_t precision;
    tinypy_bool_t guard = TINYPY_FALSE;
    tinypy_bool_t sticky = TINYPY_FALSE;
    tinypy_bool_t started = TINYPY_FALSE;

    while (begin < end && __tinypy_float_hex_space(bytes[begin]) != 0) {
        begin += 1U;
    }
    while (end > begin && __tinypy_float_hex_space(bytes[end - 1U]) != 0) {
        end -= 1U;
    }
    if (begin == end) {
        goto invalid;
    }
    if (bytes[begin] == '+' || bytes[begin] == '-') {
        negative = bytes[begin] == '-' ? TINYPY_TRUE : TINYPY_FALSE;
        begin += 1U;
    }
    if (end - begin == 3U || end - begin == 8U) {
        const char *special = end - begin == 8U ? "infinity" : (bytes[begin] == 'n' || bytes[begin] == 'N' ? "nan" : "inf");
        tinypy_bool_t match = TINYPY_TRUE;

        for (size_t index = 0U; index < end - begin; ++index) {
            uint8_t byte = bytes[begin + index];

            if (byte >= 'A' && byte <= 'Z') {
                byte = (uint8_t)(byte + ('a' - 'A'));
            }
            if (byte != (uint8_t)special[index]) {
                match = TINYPY_FALSE;
                break;
            }
        }
        if (match != 0) {
            *out_number = copysign(special[0] == 'n' ? NAN : INFINITY, negative != 0 ? -1.0 : 1.0);
            return TINYPY_TRUE;
        }
    }
    if (end - begin >= 2U && bytes[begin] == '0' && (bytes[begin + 1U] == 'x' || bytes[begin + 1U] == 'X')) {
        begin += 2U;
    }
    coefficient_begin = begin;
    cursor = begin;
    while (cursor < end) {
        int32_t digit = __tinypy_float_hex_digit(bytes[cursor]);

        if (digit >= 0) {
            digit_count += 1U;
            if (dot != 0) {
                fraction_digits += 1U;
            }
            if (digit != 0 || significant_digits != 0U) {
                if (significant_digits == 0U) {
                    first_digit = digit;
                }
                significant_digits += 1U;
            }
            else {
                leading_zeros += 1U;
            }
        }
        else if (bytes[cursor] == '.' && dot == 0) {
            dot = TINYPY_TRUE;
        }
        else {
            break;
        }
        cursor += 1U;
    }
    coefficient_end = cursor;
    if (digit_count == 0U) {
        goto invalid;
    }
    if (cursor < end && (bytes[cursor] == 'p' || bytes[cursor] == 'P')) {
        tinypy_bool_t exponent_negative = TINYPY_FALSE;
        size_t exponent_begin;

        cursor += 1U;
        if (cursor < end && (bytes[cursor] == '+' || bytes[cursor] == '-')) {
            exponent_negative = bytes[cursor] == '-' ? TINYPY_TRUE : TINYPY_FALSE;
            cursor += 1U;
        }
        exponent_begin = cursor;
        while (cursor < end && bytes[cursor] >= '0' && bytes[cursor] <= '9') {
            int32_t digit = bytes[cursor] - '0';

            if (exponent <= (INT64_MAX / 4 - digit) / 10) {
                exponent = exponent * 10 + digit;
            }
            else {
                exponent = INT64_MAX / 4;
            }
            cursor += 1U;
        }
        if (cursor == exponent_begin) {
            goto invalid;
        }
        if (exponent_negative != 0) {
            exponent = -exponent;
        }
    }
    if (cursor != end) {
        goto invalid;
    }
    if (digit_count > (size_t)(INT64_MAX / 8)) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "hexadecimal string too long to convert", out_error);
        return TINYPY_FALSE;
    }
    if (significant_digits == 0U) {
        *out_number = negative != 0 ? -0.0 : 0.0;
        return TINYPY_TRUE;
    }
    for (int32_t digit = first_digit; digit != 0; digit >>= 1) {
        leading_bits += 1;
    }
    exponent -= (int64_t)fraction_digits * 4;
    top = exponent + ((int64_t)significant_digits - 1) * 4 + leading_bits;
    if (top > DBL_MAX_EXP) {
        goto overflow;
    }
    if (top < DBL_MIN_EXP - DBL_MANT_DIG) {
        *out_number = negative != 0 ? -0.0 : 0.0;
        return TINYPY_TRUE;
    }
    least = (top > DBL_MIN_EXP ? top : DBL_MIN_EXP) - DBL_MANT_DIG;
    precision = (size_t)(top - least);
    for (cursor = coefficient_begin; cursor < coefficient_end; ++cursor) {
        int32_t digit = __tinypy_float_hex_digit(bytes[cursor]);

        if (digit < 0) {
            continue;
        }
        for (int32_t shift = 3; shift >= 0; --shift) {
            uint64_t bit = (uint64_t)((digit >> shift) & 1);

            if (started == 0 && bit == 0U) {
                continue;
            }
            started = TINYPY_TRUE;
            if (kept < precision) {
                significand = (significand << 1U) | bit;
            }
            else if (kept == precision) {
                guard = bit != 0U ? TINYPY_TRUE : TINYPY_FALSE;
            }
            else if (bit != 0U) {
                sticky = TINYPY_TRUE;
            }
            kept += 1U;
        }
    }
    if (kept < precision) {
        significand <<= precision - kept;
    }
    /* float_fromhex takes the bit above a rounding bit at the top of the
       leading digit from the character before that digit: a stripped zero
       keeps the tie even, anything else rounds it up. */
    tinypy_bool_t odd = (significand & UINT64_C(1)) != 0U || (precision == 0U && leading_bits == 4 && leading_zeros == 0U) ? TINYPY_TRUE : TINYPY_FALSE;
    if (guard != 0 && (sticky != 0 || odd != 0)) {
        significand += 1U;
    }
    *out_number = ldexp((double)significand, (int)least);
    if (isinf(*out_number)) {
        goto overflow;
    }
    *out_number = copysign(*out_number, negative != 0 ? -1.0 : 1.0);
    return TINYPY_TRUE;

invalid:
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "invalid hexadecimal floating-point string", out_error);
    return TINYPY_FALSE;
overflow:
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "hexadecimal value too large to represent as a float", out_error);
    return TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_float_fromhex_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    double number;

    (void)user_data;
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 1U, 1U, TINYPY_ARITY_STYLE_SINGLE, out_error) == TINYPY_FALSE) {
        return NULL;
    }
    tinypy_value_t *class_value = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *source = TINYPY_TUPLE_GET(args, 1U);
    tinypy_value_t *encoded = NULL;
    if (TINYPY_VALUE_KIND(class_value) != TINYPY_VALUE_TYPE || (TINYPY_VALUE_KIND(source) != TINYPY_VALUE_STRING && TINYPY_VALUE_KIND(source) != TINYPY_VALUE_UNICODE)) {
        const tinypy_message_part_t parts[] = {
            TINYPY_MESSAGE_PART_LITERAL("expected string or Unicode object, "),
            TINYPY_MESSAGE_PART_TYPE_NAME(source),
            TINYPY_MESSAGE_PART_LITERAL(" found")
        };

        tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, 3U, out_error);
        return NULL;
    }
    if (TINYPY_VALUE_KIND(source) == TINYPY_VALUE_UNICODE) {
        encoded = tinypy_internal_text_codec(vm, source, NULL, NULL, TINYPY_FALSE, TINYPY_TRUE, NULL, out_error);
        if (encoded == NULL) {
            return NULL;
        }
        source = encoded;
    }
    if (__tinypy_float_parse_hex(vm, TINYPY_TEXT_BYTES(source), TINYPY_TEXT_BYTE_SIZE(source), &number, out_error) == 0) {
        if (encoded != NULL) {
            TINYPY_DECREF(encoded);
        }
        return NULL;
    }
    if (encoded != NULL) {
        TINYPY_DECREF(encoded);
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
static tinypy_value_t *__tinypy_numeric_trunc_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 0U, 0U, TINYPY_ARITY_STYLE_PARSED, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *self = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(self);
    if (kind == TINYPY_VALUE_INTEGER || kind == TINYPY_VALUE_LONG) {
        TINYPY_INCREF(self);
        tinypy_value_t *return_value_1 = tinypy_internal_immutable_subclass_copy(&vm->types[kind], self, out_error);
        return return_value_1;
    }
    if (kind == TINYPY_VALUE_BOOL) {
        tinypy_value_t *result = tinypy_integer_from_i64(vm, TINYPY_INTEGER_VALUE(self));
        return result;
    }
    tinypy_value_t *number = tinypy_float_from_double(vm, TINYPY_FLOAT_OBJECT(self)->value);
    tinypy_value_t *constructor_args = tinypy_tuple_from_items(vm, &number, 1U);
    tinypy_value_t *result = tinypy_internal_integer_create(&vm->types[TINYPY_VALUE_INTEGER], constructor_args, NULL, out_error);
    TINYPY_DECREF(constructor_args);
    TINYPY_DECREF(number);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_numeric_integer_base_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    intptr_t base = (intptr_t)user_data;
    tinypy_bool_t result_unicode;

    if (tinypy_internal_native_method_arguments(function, args, kwargs, 0U, 0U, TINYPY_ARITY_STYLE_WRAPPER, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *self = TINYPY_TUPLE_GET(args, 0U);
    const uint8_t *spec = base == 8 ? (const uint8_t *)"#o" : (const uint8_t *)"#x";
    tinypy_value_t *formatted = tinypy_internal_string_format_builtin_value(vm, self, 0, spec, 2U, TINYPY_FALSE, &result_unicode, out_error);
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
    tinypy_value_t *result = tinypy_internal_text_allocate_uninitialized_checked(vm, TINYPY_VALUE_STRING, result_size, result_size, &output, out_error);
    size_t output_index = 0U;

    if (result == NULL) {
        TINYPY_DECREF(formatted);
        return NULL;
    }

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
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 1U, 1U, TINYPY_ARITY_STYLE_SINGLE, out_error) == TINYPY_FALSE) {
        return NULL;
    }
    tinypy_value_t *kind = TINYPY_TUPLE_GET(args, 1U);
    if (TINYPY_VALUE_KIND(kind) != TINYPY_VALUE_STRING) {
        const tinypy_message_part_t parts[] = {
            TINYPY_MESSAGE_PART_LITERAL("__getformat__() argument must be string, not "),
            TINYPY_MESSAGE_PART_TYPE_NAME(kind)
        };

        tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, 2U, out_error);
        return NULL;
    }
    const uint8_t *bytes = TINYPY_TEXT_BYTES(kind);
    size_t size = TINYPY_TEXT_BYTE_SIZE(kind);
    tinypy_bool_t single_precision = size >= 5U && memcmp(bytes, TINYPY_TEXT_BYTES(vm->internal_float_key), 5U) == 0 && (size == 5U || bytes[5U] == 0);
    tinypy_bool_t double_precision = size >= 6U && memcmp(bytes, TINYPY_TEXT_BYTES(vm->internal_double_key), 6U) == 0 && (size == 6U || bytes[6U] == 0);

    if (single_precision == 0 && double_precision == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "__getformat__() argument 1 must be 'double' or 'float'", out_error);
        return NULL;
    }
    if (single_precision != 0 ? vm->float_format_unknown : vm->double_format_unknown) {
        tinypy_value_t *result = TINYPY_RET(vm->internal_unknown_key);
        return result;
    }
    tinypy_value_t *format = *((const uint8_t *)&byteorder_probe) == 1U ? vm->internal_ieee_little_endian_key : vm->internal_ieee_big_endian_key;
    tinypy_value_t *return_value_1 = TINYPY_RET(format);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_float_setformat_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    uint16_t byteorder_probe = UINT16_C(1);

    (void)user_data;
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 2U, 2U, TINYPY_ARITY_STYLE_PARSED, out_error) == TINYPY_FALSE) {
        return NULL;
    }
    tinypy_value_t *kind = TINYPY_TUPLE_GET(args, 1U);
    tinypy_value_t *format_value = TINYPY_TUPLE_GET(args, 2U);
    tinypy_value_t *items[] = {kind, format_value};
    tinypy_value_t *const names[] = {vm->internal_type_key, vm->internal_format_key};
    tinypy_value_t *parsed[2];
    tinypy_value_t *parser_args = tinypy_tuple_from_items(vm, items, 2U);
    tinypy_bool_t valid = tinypy_internal_constructor_optional_arguments(vm, "__setformat__", sizeof("__setformat__") - 1U, parser_args, NULL, names, 2U, UINT32_C(3), parsed, out_error);

    TINYPY_DECREF(parser_args);
    if (valid == TINYPY_FALSE) {
        return NULL;
    }
    if (TINYPY_NAME_EQ(kind, vm->internal_float_key) == TINYPY_FALSE && TINYPY_NAME_EQ(kind, vm->internal_double_key) == TINYPY_FALSE) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "__setformat__() argument 1 must be 'double' or 'float'", out_error);
        return NULL;
    }
    tinypy_value_t *native_format = *((const uint8_t *)&byteorder_probe) == 1U ? vm->internal_ieee_little_endian_key : vm->internal_ieee_big_endian_key;
    tinypy_bool_t unknown = TINYPY_NAME_EQ(format_value, vm->internal_unknown_key) != TINYPY_FALSE;
    tinypy_bool_t native = TINYPY_NAME_EQ(format_value, native_format) != TINYPY_FALSE;
    if (unknown == 0 && native == 0) {
        if (TINYPY_NAME_EQ(format_value, vm->internal_ieee_little_endian_key) != TINYPY_FALSE || TINYPY_NAME_EQ(format_value, vm->internal_ieee_big_endian_key) != TINYPY_FALSE) {
            const tinypy_message_part_t parts[] = {
                TINYPY_MESSAGE_PART_LITERAL("can only set "),
                TINYPY_MESSAGE_PART_TEXT(kind),
                TINYPY_MESSAGE_PART_LITERAL(" format to 'unknown' or the detected platform value")
            };

            tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_VALUE, parts, 3U, out_error);
        }
        else {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "__setformat__() argument 2 must be 'unknown', 'IEEE, little-endian' or 'IEEE, big-endian'", out_error);
        }
        return NULL;
    }
    if (TINYPY_TEXT_BYTE_SIZE(kind) == 5U) {
        vm->float_format_unknown = unknown;
    }
    else {
        vm->double_format_unknown = unknown;
    }
    tinypy_value_t *return_value_1 = TINYPY_RET_NONE(vm);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_initialize_numeric_types(tinypy_vm_t *vm) {
    tinypy_type_t *integer_types[2] = {&vm->types[TINYPY_VALUE_INTEGER], &vm->types[TINYPY_VALUE_LONG]};
    size_t index;

    for (index = 0U; index < 2U; ++index) {
        tinypy_internal_type_add_method((integer_types[index]), vm->internal_bit_length_key, __tinypy_numeric_bit_length_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
        tinypy_internal_type_add_method((integer_types[index]), vm->internal_conjugate_key, __tinypy_numeric_conjugate_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
        tinypy_internal_type_add_method((integer_types[index]), vm->internal_special_trunc_key, __tinypy_numeric_trunc_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
        tinypy_internal_type_add_method((integer_types[index]), vm->internal_special_hex_key, __tinypy_numeric_integer_base_method, (void *)(intptr_t)16, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
        tinypy_internal_type_add_method((integer_types[index]), vm->internal_special_oct_key, __tinypy_numeric_integer_base_method, (void *)(intptr_t)8, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
        tinypy_internal_type_add_method((integer_types[index]), vm->internal_special_getnewargs_key, __tinypy_numeric_getnewargs_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
        tinypy_internal_type_add_method((integer_types[index]), vm->internal_special_cmp_key, __tinypy_numeric_cmp_method, (void *)(intptr_t)(index == 0U ? TINYPY_VALUE_INTEGER : TINYPY_VALUE_LONG), NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
        tinypy_internal_type_add_method((integer_types[index]), vm->internal_special_coerce_key, __tinypy_numeric_coerce_method, (void *)(intptr_t)(index == 0U ? TINYPY_VALUE_INTEGER : TINYPY_VALUE_LONG), NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    }
    tinypy_internal_type_add_method((&vm->types[TINYPY_VALUE_FLOAT]), vm->internal_conjugate_key, __tinypy_numeric_conjugate_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method((&vm->types[TINYPY_VALUE_FLOAT]), vm->internal_is_integer_key, __tinypy_float_is_integer_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method((&vm->types[TINYPY_VALUE_FLOAT]), vm->internal_as_integer_ratio_key, __tinypy_float_as_integer_ratio_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method((&vm->types[TINYPY_VALUE_FLOAT]), vm->internal_hex_key, __tinypy_float_hex_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method((&vm->types[TINYPY_VALUE_FLOAT]), vm->internal_special_trunc_key, __tinypy_numeric_trunc_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method((&vm->types[TINYPY_VALUE_FLOAT]), vm->internal_special_getnewargs_key, __tinypy_numeric_getnewargs_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method((&vm->types[TINYPY_VALUE_FLOAT]), vm->internal_special_coerce_key, __tinypy_numeric_coerce_method, (void *)(intptr_t)TINYPY_VALUE_FLOAT, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_class_method((&vm->types[TINYPY_VALUE_FLOAT]), vm->internal_fromhex_key, __tinypy_float_fromhex_method, NULL, NULL);
    tinypy_internal_type_add_class_method((&vm->types[TINYPY_VALUE_FLOAT]), vm->internal_special_getformat_key, __tinypy_float_getformat_method, NULL, NULL);
    tinypy_internal_type_add_class_method((&vm->types[TINYPY_VALUE_FLOAT]), vm->internal_special_setformat_key, __tinypy_float_setformat_method, NULL, NULL);
    tinypy_internal_type_add_method((&vm->types[TINYPY_VALUE_COMPLEX]), vm->internal_conjugate_key, __tinypy_numeric_conjugate_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method((&vm->types[TINYPY_VALUE_COMPLEX]), vm->internal_special_getnewargs_key, __tinypy_numeric_getnewargs_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method((&vm->types[TINYPY_VALUE_COMPLEX]), vm->internal_special_coerce_key, __tinypy_numeric_coerce_method, (void *)(intptr_t)TINYPY_VALUE_COMPLEX, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_initialize_numeric_descriptors(vm);
}
