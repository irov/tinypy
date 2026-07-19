#include "internal.h"

#include <assert.h>
#include <math.h>
#include <string.h>

static int32_t __tinypy_constructor_no_keywords(tinypy_vm_t *vm, tinypy_value_t *kwargs, tinypy_error_t **out_error)
{
    if (kwargs != NULL && tinypy_dict_size(kwargs) != 0U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "constructor does not accept keyword arguments", out_error);
        return INT32_C(0);
    }
    return INT32_C(1);
}

static int32_t __tinypy_constructor_argument_count(tinypy_vm_t *vm, tinypy_value_t *args, size_t minimum, size_t maximum, tinypy_error_t **out_error)
{
    size_t count = tinypy_tuple_size(args);

    if (count < minimum || count > maximum) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "constructor received the wrong number of arguments", out_error);
        return INT32_C(0);
    }
    return INT32_C(1);
}

static int32_t __tinypy_constructor_ascii_space(unsigned char character)
{
    return character == (unsigned char)' ' || character == (unsigned char)'\t' || character == (unsigned char)'\n' || character == (unsigned char)'\r' || character == (unsigned char)'\v' || character == (unsigned char)'\f';
}

static int32_t __tinypy_constructor_digit(unsigned char character)
{
    if (character >= (unsigned char)'0' && character <= (unsigned char)'9') return (int32_t)(character - (unsigned char)'0');
    if (character >= (unsigned char)'a' && character <= (unsigned char)'z') return (int32_t)(character - (unsigned char)'a') + INT32_C(10);
    if (character >= (unsigned char)'A' && character <= (unsigned char)'Z') return (int32_t)(character - (unsigned char)'A') + INT32_C(10);
    return INT32_C(-1);
}

static int32_t __tinypy_constructor_base_value(tinypy_vm_t *vm, tinypy_value_t *value, int32_t *out_base, tinypy_error_t **out_error)
{
    tinypy_value_type_e kind = tinypy_internal_value_kind(value);
    int64_t base;

    if (kind == TINYPY_VALUE_BOOL || kind == TINYPY_VALUE_INTEGER) base = TINYPY_INTEGER_VALUE(value);
    else if (kind == TINYPY_VALUE_LONG && TINYPY_LONG_DIGIT_COUNT(value) <= 1U) base = (int64_t)TINYPY_LONG_SIGN(value) * (int64_t)(TINYPY_LONG_DIGIT_COUNT(value) == 0U ? 0U : TINYPY_LONG_OBJECT(value)->digits[0]);
    else {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "integer base is not an integer", out_error);
        return INT32_C(0);
    }
    if (base != 0 && (base < 2 || base > 36)) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "integer base must be zero or between 2 and 36", out_error);
        return INT32_C(0);
    }
    *out_base = (int32_t)base;
    return INT32_C(1);
}

static tinypy_value_t *__tinypy_constructor_integer_text(tinypy_vm_t *vm, tinypy_value_t *text, int32_t base, int32_t force_long, tinypy_error_t **out_error)
{
    const unsigned char *bytes = tinypy_internal_text_bytes(text);
    size_t size = tinypy_internal_text_byte_size(text);
    size_t begin = 0U;
    size_t end = size;
    size_t index;
    int32_t sign = INT32_C(1);
    int32_t actual_base = base;
    size_t digit_count = 0U;
    tinypy_value_t *result;
    tinypy_value_t *base_value;

    while (begin < end && __tinypy_constructor_ascii_space(bytes[begin]) != 0) begin += 1U;
    while (end > begin && __tinypy_constructor_ascii_space(bytes[end - 1U]) != 0) end -= 1U;
    if (begin < end && (bytes[begin] == (unsigned char)'+' || bytes[begin] == (unsigned char)'-')) {
        if (bytes[begin] == (unsigned char)'-') sign = INT32_C(-1);
        begin += 1U;
    }
    if (actual_base == 0) {
        if (end - begin >= 2U && bytes[begin] == (unsigned char)'0' && (bytes[begin + 1U] == (unsigned char)'x' || bytes[begin + 1U] == (unsigned char)'X')) {
            actual_base = 16;
            begin += 2U;
        } else if (end - begin >= 2U && bytes[begin] == (unsigned char)'0' && (bytes[begin + 1U] == (unsigned char)'b' || bytes[begin + 1U] == (unsigned char)'B')) {
            actual_base = 2;
            begin += 2U;
        } else if (end - begin >= 2U && bytes[begin] == (unsigned char)'0' && (bytes[begin + 1U] == (unsigned char)'o' || bytes[begin + 1U] == (unsigned char)'O')) {
            actual_base = 8;
            begin += 2U;
        } else actual_base = begin < end && bytes[begin] == (unsigned char)'0' ? 8 : 10;
    } else if (end - begin >= 2U && bytes[begin] == (unsigned char)'0') {
        unsigned char prefix = bytes[begin + 1U];

        if ((actual_base == 16 && (prefix == (unsigned char)'x' || prefix == (unsigned char)'X')) || (actual_base == 8 && (prefix == (unsigned char)'o' || prefix == (unsigned char)'O')) || (actual_base == 2 && (prefix == (unsigned char)'b' || prefix == (unsigned char)'B'))) begin += 2U;
    }
    result = force_long != 0 ? tinypy_long_from_i64(vm, INT64_C(0)) : tinypy_integer_from_i64(vm, INT64_C(0));
    base_value = tinypy_integer_from_i64(vm, actual_base);
    for (index = begin; index < end; index += 1U) {
        int32_t digit = __tinypy_constructor_digit(bytes[index]);
        tinypy_value_t *multiplied;
        tinypy_value_t *digit_value;
        tinypy_value_t *added;

        if (digit < 0 || digit >= actual_base) {
            tinypy_release(base_value);
            tinypy_release(result);
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "invalid literal for integer conversion", out_error);
            return NULL;
        }
        digit_count += 1U;
        multiplied = tinypy_multiply(result, base_value, out_error);
        tinypy_release(result);
        if (multiplied == NULL) {
            tinypy_release(base_value);
            return NULL;
        }
        digit_value = force_long != 0 ? tinypy_long_from_i64(vm, digit) : tinypy_integer_from_i64(vm, digit);
        added = tinypy_add(multiplied, digit_value, out_error);
        tinypy_release(digit_value);
        tinypy_release(multiplied);
        if (added == NULL) {
            tinypy_release(base_value);
            return NULL;
        }
        result = added;
    }
    tinypy_release(base_value);
    if (digit_count == 0U) {
        tinypy_release(result);
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "invalid literal for integer conversion", out_error);
        return NULL;
    }
    if (sign < 0) {
        tinypy_value_t *negative = tinypy_negative(result, out_error);

        tinypy_release(result);
        return negative;
    }
    return result;
}

static double __tinypy_constructor_long_as_double(const tinypy_value_t *value)
{
    double result = 0.0;
    size_t index = TINYPY_LONG_DIGIT_COUNT(value);

    while (index != 0U) {
        index -= 1U;
        result = result * 32768.0 + (double)TINYPY_LONG_OBJECT(value)->digits[index];
    }
    return TINYPY_LONG_SIGN(value) < 0 ? -result : result;
}

static int32_t __tinypy_constructor_number_as_double(tinypy_vm_t *vm, tinypy_value_t *value, double *out_value, int32_t allow_complex, tinypy_error_t **out_error)
{
    tinypy_value_type_e kind = tinypy_internal_value_kind(value);

    if (kind == TINYPY_VALUE_BOOL || kind == TINYPY_VALUE_INTEGER) *out_value = (double)TINYPY_INTEGER_VALUE(value);
    else if (kind == TINYPY_VALUE_LONG) *out_value = __tinypy_constructor_long_as_double(value);
    else if (kind == TINYPY_VALUE_FLOAT) *out_value = TINYPY_FLOAT_OBJECT(value)->value;
    else if (kind == TINYPY_VALUE_COMPLEX && allow_complex != 0 && TINYPY_COMPLEX_OBJECT(value)->imaginary == 0.0) *out_value = TINYPY_COMPLEX_OBJECT(value)->real;
    else {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "numeric conversion requires a number", out_error);
        return INT32_C(0);
    }
    return INT32_C(1);
}

static int32_t __tinypy_constructor_text_equal_ascii(const unsigned char *bytes, size_t size, const char *ascii)
{
    size_t index;

    for (index = 0U; index < size; index += 1U) {
        unsigned char left = bytes[index];
        unsigned char right = (unsigned char)ascii[index];

        if (right == 0U) return INT32_C(0);
        if (left >= (unsigned char)'A' && left <= (unsigned char)'Z') left = (unsigned char)(left + ((unsigned char)'a' - (unsigned char)'A'));
        if (left != right) return INT32_C(0);
    }
    return ascii[size] == '\0' ? INT32_C(1) : INT32_C(0);
}

static int32_t __tinypy_constructor_float_text(tinypy_vm_t *vm, tinypy_value_t *text, double *out_value, tinypy_error_t **out_error)
{
    const unsigned char *bytes = tinypy_internal_text_bytes(text);
    size_t size = tinypy_internal_text_byte_size(text);
    size_t begin = 0U;
    size_t end = size;
    size_t index;
    int32_t sign = INT32_C(1);
    double mantissa = 0.0;
    int32_t fractional_digits = 0;
    int32_t exponent = 0;
    int32_t exponent_sign = INT32_C(1);
    size_t digits = 0U;

    while (begin < end && __tinypy_constructor_ascii_space(bytes[begin]) != 0) begin += 1U;
    while (end > begin && __tinypy_constructor_ascii_space(bytes[end - 1U]) != 0) end -= 1U;
    if (begin < end && (bytes[begin] == (unsigned char)'+' || bytes[begin] == (unsigned char)'-')) {
        if (bytes[begin] == (unsigned char)'-') sign = INT32_C(-1);
        begin += 1U;
    }
    if (__tinypy_constructor_text_equal_ascii(bytes + begin, end - begin, "nan") != 0) {
        *out_value = sign < 0 ? -NAN : NAN;
        return INT32_C(1);
    }
    if (__tinypy_constructor_text_equal_ascii(bytes + begin, end - begin, "inf") != 0 || __tinypy_constructor_text_equal_ascii(bytes + begin, end - begin, "infinity") != 0) {
        *out_value = sign < 0 ? -INFINITY : INFINITY;
        return INT32_C(1);
    }
    index = begin;
    while (index < end && bytes[index] >= (unsigned char)'0' && bytes[index] <= (unsigned char)'9') {
        mantissa = mantissa * 10.0 + (double)(bytes[index] - (unsigned char)'0');
        digits += 1U;
        index += 1U;
    }
    if (index < end && bytes[index] == (unsigned char)'.') {
        index += 1U;
        while (index < end && bytes[index] >= (unsigned char)'0' && bytes[index] <= (unsigned char)'9') {
            mantissa = mantissa * 10.0 + (double)(bytes[index] - (unsigned char)'0');
            fractional_digits += 1;
            digits += 1U;
            index += 1U;
        }
    }
    if (index < end && (bytes[index] == (unsigned char)'e' || bytes[index] == (unsigned char)'E')) {
        size_t exponent_digits = 0U;

        index += 1U;
        if (index < end && (bytes[index] == (unsigned char)'+' || bytes[index] == (unsigned char)'-')) {
            if (bytes[index] == (unsigned char)'-') exponent_sign = INT32_C(-1);
            index += 1U;
        }
        while (index < end && bytes[index] >= (unsigned char)'0' && bytes[index] <= (unsigned char)'9') {
            if (exponent < 100000) exponent = exponent * 10 + (int32_t)(bytes[index] - (unsigned char)'0');
            exponent_digits += 1U;
            index += 1U;
        }
        if (exponent_digits == 0U) digits = 0U;
    }
    if (digits == 0U || index != end) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "invalid literal for float conversion", out_error);
        return INT32_C(0);
    }
    *out_value = (double)sign * mantissa * pow(10.0, (double)(exponent_sign * exponent - fractional_digits));
    return INT32_C(1);
}

static tinypy_value_t *__tinypy_constructor_sequence_to_list(tinypy_vm_t *vm, tinypy_value_t *iterable, tinypy_error_t **out_error)
{
    tinypy_value_t *result = tinypy_list_from_items(vm, NULL, 0U);
    tinypy_value_t *iterator = tinypy_iter(iterable, out_error);
    tinypy_error_t *iteration_error = NULL;

    if (iterator == NULL) {
        tinypy_release(result);
        return NULL;
    }
    for (;;) {
        tinypy_value_t *item = tinypy_next(iterator, &iteration_error);

        if (item == NULL) break;
        tinypy_list_append(result, item);
        tinypy_release(item);
    }
    tinypy_release(iterator);
    if (iteration_error != NULL) {
        tinypy_release(result);
        if (out_error != NULL) *out_error = iteration_error;
        else tinypy_error_release(iteration_error);
        return NULL;
    }
    return result;
}

tinypy_value_t *tinypy_internal_type_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = type->vm;
    size_t count = tinypy_tuple_size(args);

    if (__tinypy_constructor_no_keywords(vm, kwargs, out_error) == 0 || (count != 1U && count != 3U)) {
        if (count != 1U && count != 3U && (out_error == NULL || *out_error == NULL)) tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "type requires one or three arguments", out_error);
        return NULL;
    }
    if (count == 1U) {
        tinypy_value_t *result = tinypy_type_as_value(tinypy_tuple_get(args, 0U)->type);

        tinypy_retain(result);
        return result;
    }
    {
        tinypy_value_t *name = tinypy_tuple_get(args, 0U);
        tinypy_value_t *bases = tinypy_tuple_get(args, 1U);
        tinypy_value_t *namespace_dict = tinypy_tuple_get(args, 2U);
        tinypy_type_t **base_types;
        tinypy_type_t *created;
        const char *name_bytes;
        size_t name_size;
        size_t base_count;
        size_t index;

        if (tinypy_internal_value_kind(name) != TINYPY_VALUE_STRING || tinypy_internal_value_kind(bases) != TINYPY_VALUE_TUPLE || tinypy_internal_value_kind(namespace_dict) != TINYPY_VALUE_DICT) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "type(name, bases, dict) received invalid arguments", out_error);
            return NULL;
        }
        base_count = tinypy_tuple_size(bases);
        base_types = base_count != 0U ? (tinypy_type_t **)tinypy_internal_vm_allocate(vm, base_count * sizeof(*base_types), (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY) : NULL;
        for (index = 0U; index < base_count; index += 1U) {
            tinypy_value_t *base = tinypy_tuple_get(bases, index);

            if (tinypy_internal_value_kind(base) != TINYPY_VALUE_TYPE) {
                tinypy_internal_vm_deallocate(vm, base_types, base_count * sizeof(*base_types), (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "type base is not a type", out_error);
                return NULL;
            }
            base_types[index] = (tinypy_type_t *)base;
        }
        name_bytes = (const char *)tinypy_string_view(name, &name_size);
        created = tinypy_type_new(vm, name_bytes, name_size, (const tinypy_type_t *const *)base_types, base_count, NULL, namespace_dict, out_error);
        if (base_types != NULL) tinypy_internal_vm_deallocate(vm, base_types, base_count * sizeof(*base_types), (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
        return created != NULL ? tinypy_type_as_value(created) : NULL;
    }
}

tinypy_value_t *tinypy_internal_object_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error)
{
    if (__tinypy_constructor_no_keywords(type->vm, kwargs, out_error) == 0 || __tinypy_constructor_argument_count(type->vm, args, 0U, 0U, out_error) == 0) return NULL;
    return tinypy_internal_object_allocate(type->vm, type, type->basic_size);
}

tinypy_value_t *tinypy_internal_bool_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = type->vm;
    int32_t truth;

    if (__tinypy_constructor_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_constructor_argument_count(vm, args, 0U, 1U, out_error) == 0) return NULL;
    if (tinypy_tuple_size(args) == 0U) return tinypy_bool_from_i32(vm, INT32_C(0));
    truth = tinypy_truth(tinypy_tuple_get(args, 0U), out_error);
    return truth < 0 ? NULL : tinypy_bool_from_i32(vm, truth);
}

static tinypy_value_t *__tinypy_constructor_integer_common(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, int32_t force_long, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = type->vm;
    size_t count = tinypy_tuple_size(args);
    int32_t base = 10;
    tinypy_value_t *value;
    tinypy_value_type_e kind;

    if (kwargs != NULL && tinypy_dict_size(kwargs) != 0U) {
        tinypy_value_t *key = tinypy_string_from_bytes(vm, "base", 4U);

        if (tinypy_dict_size(kwargs) != 1U || tinypy_dict_contains(kwargs, key) == 0 || __tinypy_constructor_base_value(vm, tinypy_dict_get(kwargs, key), &base, out_error) == 0) {
            tinypy_release(key);
            if (out_error == NULL || *out_error == NULL) tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "integer constructor received invalid keywords", out_error);
            return NULL;
        }
        tinypy_release(key);
    }
    if (count > 2U || (count == 0U && kwargs != NULL && tinypy_dict_size(kwargs) != 0U)) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "integer constructor received invalid arguments", out_error);
        return NULL;
    }
    if (count == 0U) return force_long != 0 ? tinypy_long_from_i64(vm, INT64_C(0)) : tinypy_integer_from_i64(vm, INT64_C(0));
    value = tinypy_tuple_get(args, 0U);
    kind = tinypy_internal_value_kind(value);
    if (count == 2U) {
        if (kwargs != NULL && tinypy_dict_size(kwargs) != 0U) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "integer base was provided twice", out_error);
            return NULL;
        }
        if (__tinypy_constructor_base_value(vm, tinypy_tuple_get(args, 1U), &base, out_error) == 0) return NULL;
    }
    if (count == 2U || (kwargs != NULL && tinypy_dict_size(kwargs) != 0U)) {
        if (kind != TINYPY_VALUE_STRING && kind != TINYPY_VALUE_UNICODE) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "integer base requires a string argument", out_error);
            return NULL;
        }
        return __tinypy_constructor_integer_text(vm, value, base, force_long, out_error);
    }
    if (kind == TINYPY_VALUE_STRING || kind == TINYPY_VALUE_UNICODE) return __tinypy_constructor_integer_text(vm, value, 10, force_long, out_error);
    if (kind == TINYPY_VALUE_BOOL || kind == TINYPY_VALUE_INTEGER) return force_long != 0 ? tinypy_long_from_i64(vm, TINYPY_INTEGER_VALUE(value)) : tinypy_integer_from_i64(vm, TINYPY_INTEGER_VALUE(value));
    if (kind == TINYPY_VALUE_LONG) {
        if (force_long != 0) {
            tinypy_retain(value);
            return value;
        }
        if (TINYPY_LONG_DIGIT_COUNT(value) <= 4U) {
            uint64_t magnitude = 0U;
            size_t index = TINYPY_LONG_DIGIT_COUNT(value);

            while (index != 0U) {
                index -= 1U;
                magnitude = (magnitude << 15U) | TINYPY_LONG_OBJECT(value)->digits[index];
            }
            if ((TINYPY_LONG_SIGN(value) >= 0 && magnitude <= (uint64_t)INT64_MAX) || (TINYPY_LONG_SIGN(value) < 0 && magnitude <= (uint64_t)INT64_MAX + UINT64_C(1))) {
                int64_t converted = TINYPY_LONG_SIGN(value) < 0 ? (magnitude == (uint64_t)INT64_MAX + UINT64_C(1) ? INT64_MIN : -(int64_t)magnitude) : (int64_t)magnitude;

                return tinypy_integer_from_i64(vm, converted);
            }
        }
        tinypy_retain(value);
        return value;
    }
    if (kind == TINYPY_VALUE_FLOAT) {
        double number = TINYPY_FLOAT_OBJECT(value)->value;

        if (!isfinite(number)) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "cannot convert non-finite float to integer", out_error);
            return NULL;
        }
        if (number >= (double)INT64_MIN && number <= (double)INT64_MAX) return force_long != 0 ? tinypy_long_from_i64(vm, (int64_t)number) : tinypy_integer_from_i64(vm, (int64_t)number);
    }
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "value cannot be converted to integer", out_error);
    return NULL;
}

tinypy_value_t *tinypy_internal_integer_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error)
{
    return __tinypy_constructor_integer_common(type, args, kwargs, INT32_C(0), out_error);
}

tinypy_value_t *tinypy_internal_long_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error)
{
    return __tinypy_constructor_integer_common(type, args, kwargs, INT32_C(1), out_error);
}

tinypy_value_t *tinypy_internal_float_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = type->vm;
    tinypy_value_t *value;
    double number;

    if (__tinypy_constructor_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_constructor_argument_count(vm, args, 0U, 1U, out_error) == 0) return NULL;
    if (tinypy_tuple_size(args) == 0U) return tinypy_float_from_double(vm, 0.0);
    value = tinypy_tuple_get(args, 0U);
    if (tinypy_internal_value_kind(value) == TINYPY_VALUE_STRING || tinypy_internal_value_kind(value) == TINYPY_VALUE_UNICODE) {
        if (__tinypy_constructor_float_text(vm, value, &number, out_error) == 0) return NULL;
    } else if (__tinypy_constructor_number_as_double(vm, value, &number, INT32_C(0), out_error) == 0) return NULL;
    return tinypy_float_from_double(vm, number);
}

tinypy_value_t *tinypy_internal_complex_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = type->vm;
    double real = 0.0;
    double imaginary = 0.0;

    if (__tinypy_constructor_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_constructor_argument_count(vm, args, 0U, 2U, out_error) == 0) return NULL;
    if (tinypy_tuple_size(args) >= 1U) {
        tinypy_value_t *first = tinypy_tuple_get(args, 0U);

        if (tinypy_internal_value_kind(first) == TINYPY_VALUE_COMPLEX) {
            real = TINYPY_COMPLEX_OBJECT(first)->real;
            imaginary = TINYPY_COMPLEX_OBJECT(first)->imaginary;
        } else if (__tinypy_constructor_number_as_double(vm, first, &real, INT32_C(0), out_error) == 0) return NULL;
    }
    if (tinypy_tuple_size(args) == 2U) {
        double second_real;
        tinypy_value_t *second = tinypy_tuple_get(args, 1U);

        if (tinypy_internal_value_kind(second) == TINYPY_VALUE_COMPLEX) {
            second_real = TINYPY_COMPLEX_OBJECT(second)->real;
            real -= TINYPY_COMPLEX_OBJECT(second)->imaginary;
        } else if (__tinypy_constructor_number_as_double(vm, second, &second_real, INT32_C(0), out_error) == 0) return NULL;
        imaginary += second_real;
    }
    return tinypy_complex_from_doubles(vm, real, imaginary);
}

tinypy_value_t *tinypy_internal_unicode_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = type->vm;
    tinypy_value_t *value;

    if (__tinypy_constructor_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_constructor_argument_count(vm, args, 0U, 3U, out_error) == 0) return NULL;
    if (tinypy_tuple_size(args) == 0U) return tinypy_unicode_from_utf8(vm, NULL, 0U);
    value = tinypy_tuple_get(args, 0U);
    if (tinypy_tuple_size(args) >= 2U) {
        tinypy_value_t *method;
        tinypy_value_t *method_arguments;
        tinypy_value_t *result;
        tinypy_value_t *items[2];
        size_t argument_count = tinypy_tuple_size(args) - 1U;

        if (tinypy_internal_value_kind(value) != TINYPY_VALUE_STRING) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "unicode decoding requires a byte string", out_error);
            return NULL;
        }
        items[0] = tinypy_tuple_get(args, 1U);
        if (argument_count == 2U) items[1] = tinypy_tuple_get(args, 2U);
        method = tinypy_object_get_attr(value, "decode", 6U, out_error);
        if (method == NULL) return NULL;
        method_arguments = tinypy_tuple_from_items(vm, items, argument_count);
        result = tinypy_call(method, method_arguments, NULL, out_error);
        tinypy_release(method_arguments);
        tinypy_release(method);
        return result;
    }
    if (tinypy_internal_value_kind(value) == TINYPY_VALUE_UNICODE) {
        tinypy_retain(value);
        return value;
    }
    if (tinypy_internal_value_kind(value) == TINYPY_VALUE_STRING) {
        size_t size;
        const char *bytes = (const char *)tinypy_string_view(value, &size);

        return tinypy_unicode_from_utf8(vm, bytes, size);
    }
    {
        tinypy_value_t *text = tinypy_object_str(value, out_error);
        tinypy_value_t *result;

        if (text == NULL) return NULL;
        result = tinypy_unicode_from_utf8(vm, (const char *)tinypy_internal_text_bytes(text), tinypy_internal_text_byte_size(text));
        tinypy_release(text);
        return result;
    }
}

tinypy_value_t *tinypy_internal_list_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = type->vm;
    tinypy_value_t *value;

    if (__tinypy_constructor_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_constructor_argument_count(vm, args, 0U, 1U, out_error) == 0) return NULL;
    if (tinypy_tuple_size(args) == 0U) return tinypy_list_from_items(vm, NULL, 0U);
    value = tinypy_tuple_get(args, 0U);
    if (tinypy_internal_value_kind(value) == TINYPY_VALUE_LIST) return tinypy_list_from_items(vm, TINYPY_LIST_OBJECT(value)->items, tinypy_list_size(value));
    if (tinypy_internal_value_kind(value) == TINYPY_VALUE_TUPLE) return tinypy_list_from_items(vm, tinypy_internal_tuple_items(value), tinypy_tuple_size(value));
    return __tinypy_constructor_sequence_to_list(vm, value, out_error);
}

tinypy_value_t *tinypy_internal_tuple_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = type->vm;
    tinypy_value_t *value;
    tinypy_value_t *list;
    tinypy_value_t *result;

    if (__tinypy_constructor_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_constructor_argument_count(vm, args, 0U, 1U, out_error) == 0) return NULL;
    if (tinypy_tuple_size(args) == 0U) return type == &vm->tuple_type ? tinypy_tuple_from_items(vm, NULL, 0U) : tinypy_internal_tuple_subclass_from_items(type, NULL, 0U);
    value = tinypy_tuple_get(args, 0U);
    if (type == &vm->tuple_type && value->type == &vm->tuple_type) {
        tinypy_retain(value);
        return value;
    }
    if (tinypy_internal_value_kind(value) == TINYPY_VALUE_TUPLE) return type == &vm->tuple_type ? tinypy_tuple_from_items(vm, tinypy_internal_tuple_items(value), tinypy_tuple_size(value)) : tinypy_internal_tuple_subclass_from_items(type, tinypy_internal_tuple_items(value), tinypy_tuple_size(value));
    if (tinypy_internal_value_kind(value) == TINYPY_VALUE_LIST) return type == &vm->tuple_type ? tinypy_tuple_from_items(vm, TINYPY_LIST_OBJECT(value)->items, tinypy_list_size(value)) : tinypy_internal_tuple_subclass_from_items(type, TINYPY_LIST_OBJECT(value)->items, tinypy_list_size(value));
    list = __tinypy_constructor_sequence_to_list(vm, value, out_error);
    if (list == NULL) return NULL;
    result = type == &vm->tuple_type ? tinypy_tuple_from_items(vm, TINYPY_LIST_OBJECT(list)->items, tinypy_list_size(list)) : tinypy_internal_tuple_subclass_from_items(type, TINYPY_LIST_OBJECT(list)->items, tinypy_list_size(list));
    tinypy_release(list);
    return result;
}

static int32_t __tinypy_constructor_dict_update(tinypy_value_t *result, tinypy_value_t *source, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = tinypy_internal_value_vm(result);

    if (tinypy_internal_value_kind(source) == TINYPY_VALUE_DICT) {
        tinypy_dict_object_t *dict = TINYPY_DICT_OBJECT(source);
        size_t index;

        for (index = 0U; index <= dict->mask; index += 1U) {
            tinypy_dict_entry_t *entry = &dict->table[index];

            if (entry->state == TINYPY_DICT_ENTRY_ACTIVE) tinypy_dict_set(result, entry->key, entry->value);
        }
        return INT32_C(1);
    }
    {
        tinypy_value_t *iterator = tinypy_iter(source, out_error);
        tinypy_error_t *iteration_error = NULL;

        if (iterator == NULL) return INT32_C(0);
        for (;;) {
            tinypy_value_t *item = tinypy_next(iterator, &iteration_error);
            tinypy_value_t *key;
            tinypy_value_t *value;
            tinypy_value_type_e kind;

            if (item == NULL) break;
            kind = tinypy_internal_value_kind(item);
            if ((kind != TINYPY_VALUE_TUPLE && kind != TINYPY_VALUE_LIST) || (kind == TINYPY_VALUE_TUPLE ? tinypy_tuple_size(item) : tinypy_list_size(item)) != 2U) {
                tinypy_release(item);
                tinypy_release(iterator);
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "dictionary update sequence item does not have length two", out_error);
                return INT32_C(0);
            }
            key = kind == TINYPY_VALUE_TUPLE ? tinypy_tuple_get(item, 0U) : tinypy_list_get(item, 0U);
            value = kind == TINYPY_VALUE_TUPLE ? tinypy_tuple_get(item, 1U) : tinypy_list_get(item, 1U);
            if (tinypy_internal_value_kind(key) == TINYPY_VALUE_LIST || tinypy_internal_value_kind(key) == TINYPY_VALUE_DICT || tinypy_internal_value_kind(key) == TINYPY_VALUE_SET) {
                tinypy_release(item);
                tinypy_release(iterator);
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "unhashable dictionary key", out_error);
                return INT32_C(0);
            }
            tinypy_dict_set(result, key, value);
            tinypy_release(item);
        }
        tinypy_release(iterator);
        if (iteration_error != NULL) {
            if (out_error != NULL) *out_error = iteration_error;
            else tinypy_error_release(iteration_error);
            return INT32_C(0);
        }
    }
    return INT32_C(1);
}

tinypy_value_t *tinypy_internal_dict_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = type->vm;
    tinypy_value_t *result;

    if (__tinypy_constructor_argument_count(vm, args, 0U, 1U, out_error) == 0) return NULL;
    result = tinypy_dict_new(vm);
    if (tinypy_tuple_size(args) == 1U && __tinypy_constructor_dict_update(result, tinypy_tuple_get(args, 0U), out_error) == 0) {
        tinypy_release(result);
        return NULL;
    }
    if (kwargs != NULL && __tinypy_constructor_dict_update(result, kwargs, out_error) == 0) {
        tinypy_release(result);
        return NULL;
    }
    return result;
}

static tinypy_value_t *__tinypy_constructor_type_new_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);
    tinypy_value_t *metaclass_value;
    tinypy_value_t *name;
    tinypy_value_t *bases;
    tinypy_value_t *namespace_dict;
    tinypy_type_t **base_types = NULL;
    size_t base_count;
    size_t index;
    const char *name_bytes;
    size_t name_size;
    tinypy_type_t *created;

    (void)user_data;
    if (__tinypy_constructor_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_constructor_argument_count(vm, args, 4U, 4U, out_error) == 0) return NULL;
    metaclass_value = tinypy_tuple_get(args, 0U);
    name = tinypy_tuple_get(args, 1U);
    bases = tinypy_tuple_get(args, 2U);
    namespace_dict = tinypy_tuple_get(args, 3U);
    if (tinypy_internal_value_kind(metaclass_value) != TINYPY_VALUE_TYPE || tinypy_internal_value_kind(name) != TINYPY_VALUE_STRING || tinypy_internal_value_kind(bases) != TINYPY_VALUE_TUPLE || tinypy_internal_value_kind(namespace_dict) != TINYPY_VALUE_DICT) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "type.__new__ received invalid arguments", out_error);
        return NULL;
    }
    if (tinypy_type_is_subtype((tinypy_type_t *)metaclass_value, &vm->type_type) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "type.__new__ requires a subtype of type", out_error);
        return NULL;
    }
    base_count = tinypy_tuple_size(bases);
    if (base_count != 0U) {
        assert(base_count <= SIZE_MAX / sizeof(*base_types));
        base_types = (tinypy_type_t **)tinypy_internal_vm_allocate(vm, base_count * sizeof(*base_types), (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    }
    for (index = 0U; index < base_count; index += 1U) {
        tinypy_value_t *base = tinypy_tuple_get(bases, index);

        if (tinypy_internal_value_kind(base) != TINYPY_VALUE_TYPE) {
            tinypy_internal_vm_deallocate(vm, base_types, base_count * sizeof(*base_types), (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "type.__new__ base is not a type", out_error);
            return NULL;
        }
        base_types[index] = (tinypy_type_t *)base;
    }
    name_bytes = (const char *)tinypy_string_view(name, &name_size);
    created = tinypy_type_new(vm, name_bytes, name_size, (const tinypy_type_t *const *)base_types, base_count, (tinypy_type_t *)metaclass_value, namespace_dict, out_error);
    if (base_types != NULL) tinypy_internal_vm_deallocate(vm, base_types, base_count * sizeof(*base_types), (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    return created != NULL ? tinypy_type_as_value(created) : NULL;
}

static tinypy_value_t *__tinypy_constructor_type_init_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);

    (void)user_data;
    if (__tinypy_constructor_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_constructor_argument_count(vm, args, 4U, 4U, out_error) == 0) return NULL;
    return tinypy_none_get(vm);
}

static tinypy_value_t *__tinypy_constructor_object_new_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);
    tinypy_value_t *class_value;
    tinypy_type_t *class_type;

    (void)user_data;
    if (tinypy_tuple_size(args) == 0U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "object.__new__ requires a type", out_error);
        return NULL;
    }
    class_value = tinypy_tuple_get(args, 0U);
    if (tinypy_internal_value_kind(class_value) != TINYPY_VALUE_TYPE) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "object.__new__ argument is not a type", out_error);
        return NULL;
    }
    class_type = (tinypy_type_t *)class_value;
    if ((class_type->flags & TINYPY_TYPE_FLAG_HEAP) == 0U || (class_type->flags & TINYPY_TYPE_FLAG_TYPE_SUBCLASS) != 0U || tinypy_type_is_subtype(class_type, &vm->object_type) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "object.__new__ cannot create this type", out_error);
        return NULL;
    }
    (void)kwargs;
    return tinypy_instance_new(class_type);
}

static tinypy_value_t *__tinypy_constructor_tuple_new_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);
    tinypy_value_t *type_value;
    tinypy_value_t *constructor_args;
    tinypy_value_t *result;

    (void)user_data;
    if (__tinypy_constructor_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_constructor_argument_count(vm, args, 1U, 2U, out_error) == 0) return NULL;
    type_value = tinypy_tuple_get(args, 0U);
    if (tinypy_internal_value_kind(type_value) != TINYPY_VALUE_TYPE || tinypy_type_is_subtype((tinypy_type_t *)type_value, &vm->tuple_type) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "tuple.__new__ requires a tuple subtype", out_error);
        return NULL;
    }
    constructor_args = tinypy_tuple_size(args) == 1U ? tinypy_tuple_from_items(vm, NULL, 0U) : tinypy_tuple_from_items(vm, &tinypy_internal_tuple_items(args)[1], 1U);
    result = tinypy_internal_tuple_create((tinypy_type_t *)type_value, constructor_args, NULL, out_error);
    tinypy_release(constructor_args);
    return result;
}

static tinypy_value_t *__tinypy_constructor_type_mro_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);
    tinypy_value_t *class_value;
    tinypy_type_t *type;

    (void)user_data;
    if (__tinypy_constructor_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_constructor_argument_count(vm, args, 1U, 1U, out_error) == 0) return NULL;
    class_value = tinypy_tuple_get(args, 0U);
    if (tinypy_internal_value_kind(class_value) != TINYPY_VALUE_TYPE) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "mro() requires a type", out_error);
        return NULL;
    }
    type = (tinypy_type_t *)class_value;
    return tinypy_list_from_items(vm, tinypy_internal_tuple_items(type->mro), tinypy_tuple_size(type->mro));
}

static tinypy_value_t *__tinypy_constructor_type_subclasses_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);
    tinypy_value_t *class_value;

    (void)user_data;
    if (__tinypy_constructor_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_constructor_argument_count(vm, args, 1U, 1U, out_error) == 0) return NULL;
    class_value = tinypy_tuple_get(args, 0U);
    if (tinypy_internal_value_kind(class_value) != TINYPY_VALUE_TYPE) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "__subclasses__() requires a type", out_error);
        return NULL;
    }
    return tinypy_internal_type_subclasses((tinypy_type_t *)class_value);
}

static void __tinypy_constructor_add_method(tinypy_type_t *type, const char *name, size_t name_size, tinypy_native_function_callback_t callback, int32_t static_method)
{
    tinypy_value_t *function = tinypy_native_function_new(type->vm, name, name_size, callback, NULL, NULL);
    tinypy_value_t *attribute = static_method != 0 ? tinypy_static_method_new(function) : function;
    tinypy_value_t *key = tinypy_string_from_bytes(type->vm, name, name_size);

    tinypy_dict_set(type->dict, key, attribute);
    tinypy_release(key);
    if (attribute != function) tinypy_release(attribute);
    tinypy_release(function);
}

void tinypy_internal_initialize_constructor_types(tinypy_vm_t *vm)
{
    __tinypy_constructor_add_method(&vm->type_type, "__new__", 7U, __tinypy_constructor_type_new_method, INT32_C(1));
    __tinypy_constructor_add_method(&vm->type_type, "__init__", 8U, __tinypy_constructor_type_init_method, INT32_C(0));
    __tinypy_constructor_add_method(&vm->type_type, "mro", 3U, __tinypy_constructor_type_mro_method, INT32_C(0));
    __tinypy_constructor_add_method(&vm->type_type, "__subclasses__", 14U, __tinypy_constructor_type_subclasses_method, INT32_C(0));
    __tinypy_constructor_add_method(&vm->object_type, "__new__", 7U, __tinypy_constructor_object_new_method, INT32_C(1));
    __tinypy_constructor_add_method(&vm->tuple_type, "__new__", 7U, __tinypy_constructor_tuple_new_method, INT32_C(1));
}
