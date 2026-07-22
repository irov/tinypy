#include "internal.h"

#include <assert.h>
#include <math.h>
#include <string.h>
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_constructor_no_keywords(tinypy_vm_t *vm, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    if (kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "constructor does not accept keyword arguments", out_error);
        return INT32_C(0);
    }
    return INT32_C(1);
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_constructor_argument_count(tinypy_vm_t *vm, tinypy_value_t *args, size_t minimum, size_t maximum, tinypy_error_t **out_error) {
    size_t count = TINYPY_TUPLE_SIZE(args);

    if (count < minimum || count > maximum) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "constructor received the wrong number of arguments", out_error);
        return INT32_C(0);
    }
    return INT32_C(1);
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_constructor_ascii_space(unsigned char character) {
    return character == (unsigned char)' ' || character == (unsigned char)'\t' || character == (unsigned char)'\n' || character == (unsigned char)'\r' || character == (unsigned char)'\v' || character == (unsigned char)'\f';
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_constructor_digit(unsigned char character) {
    if (character >= (unsigned char)'0' && character <= (unsigned char)'9') {
        return (int32_t)(character - (unsigned char)'0');
    }
    if (character >= (unsigned char)'a' && character <= (unsigned char)'z') {
        return (int32_t)(character - (unsigned char)'a') + INT32_C(10);
    }
    if (character >= (unsigned char)'A' && character <= (unsigned char)'Z') {
        return (int32_t)(character - (unsigned char)'A') + INT32_C(10);
    }
    return INT32_C(-1);
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_constructor_base_value(tinypy_vm_t *vm, tinypy_value_t *value, int32_t *out_base, tinypy_error_t **out_error) {
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);
    int64_t base;

    if (kind == TINYPY_VALUE_BOOL || kind == TINYPY_VALUE_INTEGER) {
        base = TINYPY_INTEGER_VALUE(value);
    }
    else if (kind == TINYPY_VALUE_LONG && TINYPY_LONG_DIGIT_COUNT(value) <= 1U) {
        base = (int64_t)TINYPY_LONG_SIGN(value) * (int64_t)(TINYPY_LONG_DIGIT_COUNT(value) == 0U ? 0U : TINYPY_LONG_OBJECT(value)->digits[0]);
    }
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
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_constructor_integer_text(tinypy_vm_t *vm, tinypy_value_t *text, int32_t base, int32_t force_long, tinypy_error_t **out_error) {
    const unsigned char *bytes = TINYPY_TEXT_BYTES(text);
    size_t size = TINYPY_TEXT_BYTE_SIZE(text);
    size_t begin = 0U;
    size_t end = size;
    size_t index;
    int32_t sign = INT32_C(1);
    int32_t actual_base = base;
    size_t digit_count = 0U;

    while (begin < end && __tinypy_constructor_ascii_space(bytes[begin]) != 0) {
        begin += 1U;
    }
    while (end > begin && __tinypy_constructor_ascii_space(bytes[end - 1U]) != 0) {
        end -= 1U;
    }
    if (begin < end && (bytes[begin] == (unsigned char)'+' || bytes[begin] == (unsigned char)'-')) {
        if (bytes[begin] == (unsigned char)'-') {
            sign = INT32_C(-1);
        }
        begin += 1U;
    }
    if (actual_base == 0) {
        if (end - begin >= 2U && bytes[begin] == (unsigned char)'0' && (bytes[begin + 1U] == (unsigned char)'x' || bytes[begin + 1U] == (unsigned char)'X')) {
            actual_base = 16;
            begin += 2U;
        }
        else if (end - begin >= 2U && bytes[begin] == (unsigned char)'0' && (bytes[begin + 1U] == (unsigned char)'b' || bytes[begin + 1U] == (unsigned char)'B')) {
            actual_base = 2;
            begin += 2U;
        }
        else if (end - begin >= 2U && bytes[begin] == (unsigned char)'0' && (bytes[begin + 1U] == (unsigned char)'o' || bytes[begin + 1U] == (unsigned char)'O')) {
            actual_base = 8;
            begin += 2U;
        }
        else {
            actual_base = begin < end && bytes[begin] == (unsigned char)'0' ? 8 : 10;
        }
    }
    else if (end - begin >= 2U && bytes[begin] == (unsigned char)'0') {
        unsigned char prefix = bytes[begin + 1U];

        if ((actual_base == 16 && (prefix == (unsigned char)'x' || prefix == (unsigned char)'X')) || (actual_base == 8 && (prefix == (unsigned char)'o' || prefix == (unsigned char)'O')) || (actual_base == 2 && (prefix == (unsigned char)'b' || prefix == (unsigned char)'B'))) {
            begin += 2U;
        }
    }
    tinypy_value_t *result = force_long != 0 ? tinypy_long_from_i64(vm, INT64_C(0)) : tinypy_integer_from_i64(vm, INT64_C(0));
    tinypy_value_t *base_value = tinypy_integer_from_i64(vm, actual_base);
    for (index = begin; index < end; ++index) {
        int32_t digit = __tinypy_constructor_digit(bytes[index]);
        tinypy_value_t *multiplied;
        tinypy_value_t *digit_value;
        tinypy_value_t *added;

        if (digit < 0 || digit >= actual_base) {
            TINYPY_DECREF(base_value);
            TINYPY_DECREF(result);
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "invalid literal for integer conversion", out_error);
            return NULL;
        }
        digit_count += 1U;
        multiplied = tinypy_multiply(result, base_value, out_error);
        TINYPY_DECREF(result);
        if (multiplied == NULL) {
            TINYPY_DECREF(base_value);
            return NULL;
        }
        digit_value = force_long != 0 ? tinypy_long_from_i64(vm, digit) : tinypy_integer_from_i64(vm, digit);
        added = tinypy_add(multiplied, digit_value, out_error);
        TINYPY_DECREF(digit_value);
        TINYPY_DECREF(multiplied);
        if (added == NULL) {
            TINYPY_DECREF(base_value);
            return NULL;
        }
        result = added;
    }
    TINYPY_DECREF(base_value);
    if (digit_count == 0U) {
        TINYPY_DECREF(result);
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "invalid literal for integer conversion", out_error);
        return NULL;
    }
    if (sign < 0) {
        tinypy_value_t *negative = tinypy_negative(result, out_error);

        TINYPY_DECREF(result);
        return negative;
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
static double __tinypy_constructor_long_as_double(const tinypy_value_t *value) {
    double result = 0.0;
    size_t index = TINYPY_LONG_DIGIT_COUNT(value);

    while (index != 0U) {
        index -= 1U;
        result = result * 32768.0 + (double)TINYPY_LONG_OBJECT(value)->digits[index];
    }
    return TINYPY_LONG_SIGN(value) < 0 ? -result : result;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_constructor_number_as_double(tinypy_vm_t *vm, tinypy_value_t *value, double *out_value, int32_t allow_complex, tinypy_error_t **out_error) {
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);

    if (kind == TINYPY_VALUE_BOOL || kind == TINYPY_VALUE_INTEGER) {
        *out_value = (double)TINYPY_INTEGER_VALUE(value);
    }
    else if (kind == TINYPY_VALUE_LONG) {
        *out_value = __tinypy_constructor_long_as_double(value);
    }
    else if (kind == TINYPY_VALUE_FLOAT) {
        *out_value = TINYPY_FLOAT_OBJECT(value)->value;
    }
    else if (kind == TINYPY_VALUE_COMPLEX && allow_complex != 0 && TINYPY_COMPLEX_OBJECT(value)->imaginary == 0.0) {
        *out_value = TINYPY_COMPLEX_OBJECT(value)->real;
    }
    else {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "numeric conversion requires a number", out_error);
        return INT32_C(0);
    }
    return INT32_C(1);
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_constructor_text_equal_ascii(const unsigned char *bytes, size_t size, const char *ascii) {
    size_t index;

    for (index = 0U; index < size; ++index) {
        unsigned char left = bytes[index];
        unsigned char right = (unsigned char)ascii[index];

        if (right == 0U) {
            return INT32_C(0);
        }
        if (left >= (unsigned char)'A' && left <= (unsigned char)'Z') {
            left = (unsigned char)(left + ((unsigned char)'a' - (unsigned char)'A'));
        }
        if (left != right) {
            return INT32_C(0);
        }
    }
    return ascii[size] == '\0' ? INT32_C(1) : INT32_C(0);
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_constructor_float_text(tinypy_vm_t *vm, tinypy_value_t *text, double *out_value, tinypy_error_t **out_error) {
    const unsigned char *bytes = TINYPY_TEXT_BYTES(text);
    size_t size = TINYPY_TEXT_BYTE_SIZE(text);
    size_t begin = 0U;
    size_t end = size;
    size_t index;
    int32_t sign = INT32_C(1);
    double mantissa = 0.0;
    int32_t fractional_digits = 0;
    int32_t exponent = 0;
    int32_t exponent_sign = INT32_C(1);
    size_t digits = 0U;

    while (begin < end && __tinypy_constructor_ascii_space(bytes[begin]) != 0) {
        begin += 1U;
    }
    while (end > begin && __tinypy_constructor_ascii_space(bytes[end - 1U]) != 0) {
        end -= 1U;
    }
    if (begin < end && (bytes[begin] == (unsigned char)'+' || bytes[begin] == (unsigned char)'-')) {
        if (bytes[begin] == (unsigned char)'-') {
            sign = INT32_C(-1);
        }
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
            if (bytes[index] == (unsigned char)'-') {
                exponent_sign = INT32_C(-1);
            }
            index += 1U;
        }
        while (index < end && bytes[index] >= (unsigned char)'0' && bytes[index] <= (unsigned char)'9') {
            if (exponent < 100000) {
                exponent = exponent * 10 + (int32_t)(bytes[index] - (unsigned char)'0');
            }
            exponent_digits += 1U;
            index += 1U;
        }
        if (exponent_digits == 0U) {
            digits = 0U;
        }
    }
    if (digits == 0U || index != end) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "invalid literal for float conversion", out_error);
        return INT32_C(0);
    }
    *out_value = (double)sign * mantissa * pow(10.0, (double)(exponent_sign * exponent - fractional_digits));
    return INT32_C(1);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_constructor_sequence_to_list(tinypy_vm_t *vm, tinypy_value_t *iterable, tinypy_error_t **out_error) {
    tinypy_value_t *result = tinypy_list_from_items(vm, NULL, 0U);
    tinypy_value_t *iterator = tinypy_iter(iterable, out_error);
    tinypy_error_t *iteration_error = NULL;

    if (iterator == NULL) {
        TINYPY_DECREF(result);
        return NULL;
    }
    for (;;) {
        tinypy_value_t *item = tinypy_next(iterator, &iteration_error);

        if (item == NULL) {
            break;
        }
        tinypy_list_append(result, item);
        TINYPY_DECREF(item);
    }
    TINYPY_DECREF(iterator);
    if (iteration_error != NULL) {
        TINYPY_DECREF(result);
        if (out_error != NULL) {
            *out_error = iteration_error;
        }
        else {
            tinypy_error_release(iteration_error);
        }
        return NULL;
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_type_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = type->vm;
    size_t count = TINYPY_TUPLE_SIZE(args);

    if (__tinypy_constructor_no_keywords(vm, kwargs, out_error) == 0 || (count != 1U && count != 3U)) {
        if (count != 1U && count != 3U && (out_error == NULL || *out_error == NULL)) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "type requires one or three arguments", out_error);
        }
        return NULL;
    }
    if (count == 1U) {
        tinypy_value_t *item = TINYPY_TUPLE_GET(args, 0U);
        tinypy_value_t *result = tinypy_type_as_value(item->type);

        TINYPY_INCREF(result);
        return result;
    } {
        tinypy_value_t *name = TINYPY_TUPLE_GET(args, 0U);
        tinypy_value_t *bases = TINYPY_TUPLE_GET(args, 1U);
        tinypy_value_t *namespace_dict = TINYPY_TUPLE_GET(args, 2U);
        tinypy_type_t **base_types;
        tinypy_type_t *created;
        const char *name_bytes;
        size_t name_size;
        size_t base_count;
        size_t index;

        if (TINYPY_VALUE_KIND(name) != TINYPY_VALUE_STRING || TINYPY_VALUE_KIND(bases) != TINYPY_VALUE_TUPLE || TINYPY_VALUE_KIND(namespace_dict) != TINYPY_VALUE_DICT) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "type(name, bases, dict) received invalid arguments", out_error);
            return NULL;
        }
        base_count = TINYPY_TUPLE_SIZE(bases);
        base_types = base_count != 0U ? (tinypy_type_t **)tinypy_internal_vm_allocate(vm, base_count * sizeof(*base_types), (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY) : NULL;
        for (index = 0U; index < base_count; ++index) {
            tinypy_value_t *base = TINYPY_TUPLE_GET(bases, index);

            if (TINYPY_VALUE_KIND(base) != TINYPY_VALUE_TYPE) {
                tinypy_internal_vm_deallocate(vm, base_types, base_count * sizeof(*base_types), (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "type base is not a type", out_error);
                return NULL;
            }
            base_types[index] = (tinypy_type_t *)base;
        }
        name_bytes = (const char *)tinypy_string_view(name, &name_size);
        created = tinypy_type_new(vm, name_bytes, name_size, (const tinypy_type_t *const *)base_types, base_count, NULL, namespace_dict, out_error);
        if (base_types != NULL) {
            tinypy_internal_vm_deallocate(vm, base_types, base_count * sizeof(*base_types), (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
        }
        return created != NULL ? tinypy_type_as_value(created) : NULL;
    }
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_object_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    if (__tinypy_constructor_no_keywords(type->vm, kwargs, out_error) == 0 || __tinypy_constructor_argument_count(type->vm, args, 0U, 0U, out_error) == 0) {
        return NULL;
    }
    return tinypy_internal_object_allocate(type->vm, type, type->basic_size);
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_bool_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = type->vm;
    int32_t truth;

    if (__tinypy_constructor_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_constructor_argument_count(vm, args, 0U, 1U, out_error) == 0) {
        return NULL;
    }
    if (TINYPY_TUPLE_SIZE(args) == 0U) {
        return tinypy_bool_from_i32(vm, INT32_C(0));
    }
    tinypy_value_t *item = TINYPY_TUPLE_GET(args, 0U);
    truth = tinypy_truth(item, out_error);
    return truth < 0 ? NULL : tinypy_bool_from_i32(vm, truth);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_constructor_integer_common(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, int32_t force_long, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = type->vm;
    size_t count = TINYPY_TUPLE_SIZE(args);
    int32_t base = 10;
    tinypy_value_type_e kind;

    if (kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U) {
        tinypy_value_t *key = tinypy_string_from_bytes(vm, "base", 4U);
        tinypy_value_t *base_value = TINYPY_DICT_SIZE(kwargs) == 1U ? tinypy_dict_get_optional(kwargs, key) : NULL;

        int condition = base_value == NULL;
        if (condition == 0) {
            condition = __tinypy_constructor_base_value(vm, base_value, &base, out_error) == 0;
        }
        if (condition) {
            TINYPY_DECREF(key);
            if (out_error == NULL || *out_error == NULL) {
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "integer constructor received invalid keywords", out_error);
            }
            return NULL;
        }
        TINYPY_DECREF(key);
    }
    if (count > 2U || (count == 0U && kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U)) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "integer constructor received invalid arguments", out_error);
        return NULL;
    }
    if (count == 0U) {
        return force_long != 0 ? tinypy_long_from_i64(vm, INT64_C(0)) : tinypy_integer_from_i64(vm, INT64_C(0));
    }
    tinypy_value_t *value = TINYPY_TUPLE_GET(args, 0U);
    kind = TINYPY_VALUE_KIND(value);
    if (count == 2U) {
        if (kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "integer base was provided twice", out_error);
            return NULL;
        }
        tinypy_value_t *item = TINYPY_TUPLE_GET(args, 1U);
        if (__tinypy_constructor_base_value(vm, item, &base, out_error) == 0) {
            return NULL;
        }
    }
    if (count == 2U || (kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U)) {
        if (kind != TINYPY_VALUE_STRING && kind != TINYPY_VALUE_UNICODE) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "integer base requires a string argument", out_error);
            return NULL;
        }
        return __tinypy_constructor_integer_text(vm, value, base, force_long, out_error);
    }
    if (kind == TINYPY_VALUE_STRING || kind == TINYPY_VALUE_UNICODE) {
        return __tinypy_constructor_integer_text(vm, value, 10, force_long, out_error);
    }
    if (kind == TINYPY_VALUE_BOOL || kind == TINYPY_VALUE_INTEGER) {
        return force_long != 0 ? tinypy_long_from_i64(vm, TINYPY_INTEGER_VALUE(value)) : tinypy_integer_from_i64(vm, TINYPY_INTEGER_VALUE(value));
    }
    if (kind == TINYPY_VALUE_LONG) {
        if (force_long != 0) {
            TINYPY_INCREF(value);
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
        TINYPY_INCREF(value);
        return value;
    }
    if (kind == TINYPY_VALUE_FLOAT) {
        double number = TINYPY_FLOAT_OBJECT(value)->value;

        if (!isfinite(number)) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "cannot convert non-finite float to integer", out_error);
            return NULL;
        }
        if (number >= (double)INT64_MIN && number <= (double)INT64_MAX) {
            return force_long != 0 ? tinypy_long_from_i64(vm, (int64_t)number) : tinypy_integer_from_i64(vm, (int64_t)number);
        }
    }
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "value cannot be converted to integer", out_error);
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_integer_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    return __tinypy_constructor_integer_common(type, args, kwargs, INT32_C(0), out_error);
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_long_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    return __tinypy_constructor_integer_common(type, args, kwargs, INT32_C(1), out_error);
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_float_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = type->vm;
    double number;

    if (__tinypy_constructor_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_constructor_argument_count(vm, args, 0U, 1U, out_error) == 0) {
        return NULL;
    }
    if (TINYPY_TUPLE_SIZE(args) == 0U) {
        return tinypy_float_from_double(vm, 0.0);
    }
    tinypy_value_t *value = TINYPY_TUPLE_GET(args, 0U);
    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_STRING || TINYPY_VALUE_KIND(value) == TINYPY_VALUE_UNICODE) {
        if (__tinypy_constructor_float_text(vm, value, &number, out_error) == 0) {
            return NULL;
        }
    }
    else if (__tinypy_constructor_number_as_double(vm, value, &number, INT32_C(0), out_error) == 0) {
        return NULL;
    }
    return tinypy_float_from_double(vm, number);
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_complex_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = type->vm;
    double real = 0.0;
    double imaginary = 0.0;

    if (__tinypy_constructor_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_constructor_argument_count(vm, args, 0U, 2U, out_error) == 0) {
        return NULL;
    }
    if (TINYPY_TUPLE_SIZE(args) >= 1U) {
        tinypy_value_t *first = TINYPY_TUPLE_GET(args, 0U);

        if (TINYPY_VALUE_KIND(first) == TINYPY_VALUE_COMPLEX) {
            real = TINYPY_COMPLEX_OBJECT(first)->real;
            imaginary = TINYPY_COMPLEX_OBJECT(first)->imaginary;
        }
        else if (__tinypy_constructor_number_as_double(vm, first, &real, INT32_C(0), out_error) == 0) {
            return NULL;
        }
    }
    if (TINYPY_TUPLE_SIZE(args) == 2U) {
        double second_real;
        tinypy_value_t *second = TINYPY_TUPLE_GET(args, 1U);

        if (TINYPY_VALUE_KIND(second) == TINYPY_VALUE_COMPLEX) {
            second_real = TINYPY_COMPLEX_OBJECT(second)->real;
            real -= TINYPY_COMPLEX_OBJECT(second)->imaginary;
        }
        else if (__tinypy_constructor_number_as_double(vm, second, &second_real, INT32_C(0), out_error) == 0) {
            return NULL;
        }
        imaginary += second_real;
    }
    return tinypy_complex_from_doubles(vm, real, imaginary);
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_unicode_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = type->vm;

    if (__tinypy_constructor_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_constructor_argument_count(vm, args, 0U, 3U, out_error) == 0) {
        return NULL;
    }
    if (TINYPY_TUPLE_SIZE(args) == 0U) {
        return tinypy_unicode_from_utf8(vm, NULL, 0U);
    }
    tinypy_value_t *value = TINYPY_TUPLE_GET(args, 0U);
    if (TINYPY_TUPLE_SIZE(args) >= 2U) {
        tinypy_value_t *method;
        tinypy_value_t *method_arguments;
        tinypy_value_t *result;
        tinypy_value_t *items[2];
        size_t argument_count = TINYPY_TUPLE_SIZE(args) - 1U;

        if (TINYPY_VALUE_KIND(value) != TINYPY_VALUE_STRING) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "unicode decoding requires a byte string", out_error);
            return NULL;
        }
        items[0] = TINYPY_TUPLE_GET(args, 1U);
        if (argument_count == 2U) {
            items[1] = TINYPY_TUPLE_GET(args, 2U);
        }
        method = tinypy_object_get_attr(value, "decode", 6U, out_error);
        if (method == NULL) {
            return NULL;
        }
        method_arguments = tinypy_tuple_from_items(vm, items, argument_count);
        result = tinypy_call(method, method_arguments, NULL, out_error);
        TINYPY_DECREF(method_arguments);
        TINYPY_DECREF(method);
        return result;
    }
    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_UNICODE) {
        TINYPY_INCREF(value);
        return value;
    }
    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_STRING) {
        size_t size;
        const char *bytes = (const char *)tinypy_string_view(value, &size);

        return tinypy_unicode_from_utf8(vm, bytes, size);
    } {
        tinypy_value_t *text = tinypy_object_str(value, out_error);
        tinypy_value_t *result;

        if (text == NULL) {
            return NULL;
        }
        const unsigned char *bytes_2 = TINYPY_TEXT_BYTES(text);
        size_t byte_size = TINYPY_TEXT_BYTE_SIZE(text);
        result = tinypy_unicode_from_utf8(vm, (const char *)bytes_2, byte_size);
        TINYPY_DECREF(text);
        return result;
    }
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_list_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = type->vm;

    if (__tinypy_constructor_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_constructor_argument_count(vm, args, 0U, 1U, out_error) == 0) {
        return NULL;
    }
    if (TINYPY_TUPLE_SIZE(args) == 0U) {
        return tinypy_list_from_items(vm, NULL, 0U);
    }
    tinypy_value_t *value = TINYPY_TUPLE_GET(args, 0U);
    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_LIST) {
        size_t list_size = TINYPY_LIST_SIZE(value);
        return tinypy_list_from_items(vm, TINYPY_LIST_OBJECT(value)->items, list_size);
    }
    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_TUPLE) {
        tinypy_value_t *const *tuple_items = tinypy_internal_tuple_items(value);
        size_t tuple_size = TINYPY_TUPLE_SIZE(value);
        return tinypy_list_from_items(vm, tuple_items, tuple_size);
    }
    return __tinypy_constructor_sequence_to_list(vm, value, out_error);
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_tuple_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = type->vm;
    tinypy_value_t *result;

    if (__tinypy_constructor_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_constructor_argument_count(vm, args, 0U, 1U, out_error) == 0) {
        return NULL;
    }
    if (TINYPY_TUPLE_SIZE(args) == 0U) {
        return type == &vm->tuple_type ? tinypy_tuple_from_items(vm, NULL, 0U) : tinypy_internal_tuple_subclass_from_items(type, NULL, 0U);
    }
    tinypy_value_t *value = TINYPY_TUPLE_GET(args, 0U);
    if (type == &vm->tuple_type && value->type == &vm->tuple_type) {
        TINYPY_INCREF(value);
        return value;
    }
    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_TUPLE) {
        tinypy_value_t *selected_value_2;
        if (type == &vm->tuple_type) {
            tinypy_value_t *const *tuple_items = tinypy_internal_tuple_items(value);
            size_t tuple_size = TINYPY_TUPLE_SIZE(value);
            selected_value_2 = tinypy_tuple_from_items(vm, tuple_items, tuple_size);
        }
        else {
            tinypy_value_t *const *tuple_items = tinypy_internal_tuple_items(value);
            size_t tuple_size = TINYPY_TUPLE_SIZE(value);
            selected_value_2 = tinypy_internal_tuple_subclass_from_items(type, tuple_items, tuple_size);
        }
        return selected_value_2;
    }
    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_LIST) {
        tinypy_value_t *selected_value_3;
        if (type == &vm->tuple_type) {
            size_t list_size = TINYPY_LIST_SIZE(value);
            selected_value_3 = tinypy_tuple_from_items(vm, TINYPY_LIST_OBJECT(value)->items, list_size);
        }
        else {
            size_t list_size = TINYPY_LIST_SIZE(value);
            selected_value_3 = tinypy_internal_tuple_subclass_from_items(type, TINYPY_LIST_OBJECT(value)->items, list_size);
        }
        return selected_value_3;
    }
    tinypy_value_t *list = __tinypy_constructor_sequence_to_list(vm, value, out_error);
    if (list == NULL) {
        return NULL;
    }
    tinypy_value_t *selected_value;
    if (type == &vm->tuple_type) {
        size_t list_size = TINYPY_LIST_SIZE(list);
        selected_value = tinypy_tuple_from_items(vm, TINYPY_LIST_OBJECT(list)->items, list_size);
    }
    else {
        size_t list_size = TINYPY_LIST_SIZE(list);
        selected_value = tinypy_internal_tuple_subclass_from_items(type, TINYPY_LIST_OBJECT(list)->items, list_size);
    }
    result = selected_value;
    TINYPY_DECREF(list);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_constructor_dict_update(tinypy_value_t *result, tinypy_value_t *source, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(result);

    if (TINYPY_VALUE_KIND(source) == TINYPY_VALUE_DICT) {
        tinypy_dict_entry_t *iterator = TINYPY_DICT_ITERATOR_BEGIN(source);
        tinypy_dict_entry_t *iterator_end = TINYPY_DICT_ITERATOR_END(source);

        for (; iterator != iterator_end; ++iterator) {
            if (iterator->state == TINYPY_DICT_ENTRY_ACTIVE) {
                tinypy_dict_set(result, iterator->key, iterator->value);
            }
        }
        return INT32_C(1);
    } {
        tinypy_value_t *iterator = tinypy_iter(source, out_error);
        tinypy_error_t *iteration_error = NULL;

        if (iterator == NULL) {
            return INT32_C(0);
        }
        for (;;) {
            tinypy_value_t *item = tinypy_next(iterator, &iteration_error);
            tinypy_value_t *key;
            tinypy_value_t *value;
            tinypy_value_type_e kind;

            if (item == NULL) {
                break;
            }
            kind = TINYPY_VALUE_KIND(item);
            if ((kind != TINYPY_VALUE_TUPLE && kind != TINYPY_VALUE_LIST) || (kind == TINYPY_VALUE_TUPLE ? TINYPY_TUPLE_SIZE(item) : TINYPY_LIST_SIZE(item)) != 2U) {
                TINYPY_DECREF(item);
                TINYPY_DECREF(iterator);
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "dictionary update sequence item does not have length two", out_error);
                return INT32_C(0);
            }
            key = kind == TINYPY_VALUE_TUPLE ? TINYPY_TUPLE_GET(item, 0U) : TINYPY_LIST_GET(item, 0U);
            value = kind == TINYPY_VALUE_TUPLE ? TINYPY_TUPLE_GET(item, 1U) : TINYPY_LIST_GET(item, 1U);
            if (TINYPY_VALUE_KIND(key) == TINYPY_VALUE_LIST || TINYPY_VALUE_KIND(key) == TINYPY_VALUE_DICT || TINYPY_VALUE_KIND(key) == TINYPY_VALUE_SET) {
                TINYPY_DECREF(item);
                TINYPY_DECREF(iterator);
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "unhashable dictionary key", out_error);
                return INT32_C(0);
            }
            tinypy_dict_set(result, key, value);
            TINYPY_DECREF(item);
        }
        TINYPY_DECREF(iterator);
        if (iteration_error != NULL) {
            if (out_error != NULL) {
                *out_error = iteration_error;
            }
            else {
                tinypy_error_release(iteration_error);
            }
            return INT32_C(0);
        }
    }
    return INT32_C(1);
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_dict_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = type->vm;

    if (__tinypy_constructor_argument_count(vm, args, 0U, 1U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *result = tinypy_dict_new(vm);
    int condition_2 = TINYPY_TUPLE_SIZE(args) == 1U;
    if (condition_2 != 0) {
        tinypy_value_t *item = TINYPY_TUPLE_GET(args, 0U);
        condition_2 = __tinypy_constructor_dict_update(result, item, out_error) == 0;
    }
    if (condition_2) {
        TINYPY_DECREF(result);
        return NULL;
    }
    if (kwargs != NULL && __tinypy_constructor_dict_update(result, kwargs, out_error) == 0) {
        TINYPY_DECREF(result);
        return NULL;
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_constructor_type_new_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *bases;
    tinypy_value_t *namespace_dict;
    tinypy_type_t **base_types = NULL;
    size_t base_count;
    size_t index;
    const char *name_bytes;
    size_t name_size;
    tinypy_type_t *created;

    (void)user_data;
    if (__tinypy_constructor_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_constructor_argument_count(vm, args, 4U, 4U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *metaclass_value = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *name = TINYPY_TUPLE_GET(args, 1U);
    bases = TINYPY_TUPLE_GET(args, 2U);
    namespace_dict = TINYPY_TUPLE_GET(args, 3U);
    if (TINYPY_VALUE_KIND(metaclass_value) != TINYPY_VALUE_TYPE || TINYPY_VALUE_KIND(name) != TINYPY_VALUE_STRING || TINYPY_VALUE_KIND(bases) != TINYPY_VALUE_TUPLE || TINYPY_VALUE_KIND(namespace_dict) != TINYPY_VALUE_DICT) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "type.__new__ received invalid arguments", out_error);
        return NULL;
    }
    if (tinypy_type_is_subtype((tinypy_type_t *)metaclass_value, &vm->type_type) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "type.__new__ requires a subtype of type", out_error);
        return NULL;
    }
    base_count = TINYPY_TUPLE_SIZE(bases);
    if (base_count != 0U) {
        assert(base_count <= SIZE_MAX / sizeof(*base_types));
        base_types = (tinypy_type_t **)tinypy_internal_vm_allocate(vm, base_count * sizeof(*base_types), (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    }
    for (index = 0U; index < base_count; ++index) {
        tinypy_value_t *base = TINYPY_TUPLE_GET(bases, index);

        if (TINYPY_VALUE_KIND(base) != TINYPY_VALUE_TYPE) {
            tinypy_internal_vm_deallocate(vm, base_types, base_count * sizeof(*base_types), (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "type.__new__ base is not a type", out_error);
            return NULL;
        }
        base_types[index] = (tinypy_type_t *)base;
    }
    name_bytes = (const char *)tinypy_string_view(name, &name_size);
    created = tinypy_type_new(vm, name_bytes, name_size, (const tinypy_type_t *const *)base_types, base_count, (tinypy_type_t *)metaclass_value, namespace_dict, out_error);
    if (base_types != NULL) {
        tinypy_internal_vm_deallocate(vm, base_types, base_count * sizeof(*base_types), (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    }
    return created != NULL ? tinypy_type_as_value(created) : NULL;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_constructor_type_init_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_constructor_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_constructor_argument_count(vm, args, 4U, 4U, out_error) == 0) {
        return NULL;
    }
    return tinypy_none_get(vm);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_constructor_object_new_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (TINYPY_TUPLE_SIZE(args) == 0U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "object.__new__ requires a type", out_error);
        return NULL;
    }
    tinypy_value_t *class_value = TINYPY_TUPLE_GET(args, 0U);
    if (TINYPY_VALUE_KIND(class_value) != TINYPY_VALUE_TYPE) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "object.__new__ argument is not a type", out_error);
        return NULL;
    }
    tinypy_type_t *class_type = (tinypy_type_t *)class_value;
    if ((class_type->flags & TINYPY_TYPE_FLAG_HEAP) == 0U || (class_type->flags & TINYPY_TYPE_FLAG_TYPE_SUBCLASS) != 0U || tinypy_type_is_subtype(class_type, &vm->object_type) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "object.__new__ cannot create this type", out_error);
        return NULL;
    }
    (void)kwargs;
    return class_type->layout_kind == TINYPY_VALUE_NATIVE_INSTANCE
               ? tinypy_native_instance_new(class_type)
               : tinypy_instance_new(class_type);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_constructor_object_init_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_constructor_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_constructor_argument_count(vm, args, 1U, 1U, out_error) == 0) {
        return NULL;
    }
    return tinypy_none_get(vm);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_constructor_call_with_items(tinypy_vm_t *vm, tinypy_value_t *callable, tinypy_value_t *const *items, size_t item_count, tinypy_error_t **out_error) {
    tinypy_value_t *call_args = tinypy_tuple_from_items(vm, items, item_count);
    tinypy_value_t *result = tinypy_call(callable, call_args, NULL, out_error);

    TINYPY_DECREF(call_args);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_constructor_copy_reg_function(tinypy_vm_t *vm, const char *name, size_t name_size, tinypy_error_t **out_error) {
    tinypy_value_t *module = tinypy_import_module(vm, "copy_reg", 8U, NULL, NULL, INT32_C(0), out_error);

    if (module == NULL) {
        return NULL;
    }
    tinypy_value_t *function = tinypy_object_get_attr(module, name, name_size, out_error);
    TINYPY_DECREF(module);
    return function;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_constructor_object_common_reduce(tinypy_value_t *self, tinypy_value_t *protocol_value, int64_t protocol, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(self);

    if (protocol < 2) {
        tinypy_value_t *reducer = __tinypy_constructor_copy_reg_function(vm, "_reduce_ex", 10U, out_error);

        if (reducer == NULL) {
            return NULL;
        }
        tinypy_value_t *items[] = {self, protocol_value};
        tinypy_value_t *result = __tinypy_constructor_call_with_items(vm, reducer, items, 2U, out_error);
        TINYPY_DECREF(reducer);
        return result;
    }

    tinypy_value_t *newobj = __tinypy_constructor_copy_reg_function(vm, "__newobj__", 10U, out_error);
    tinypy_value_t *new_arguments = NULL;
    tinypy_value_t *constructor_arguments = NULL;
    tinypy_value_t *state = NULL;
    tinypy_value_t *list_items = NULL;
    tinypy_value_t *dict_items = NULL;
    tinypy_value_t *result = NULL;

    if (newobj == NULL) {
        return NULL;
    }
    if (tinypy_object_has_attr(self, "__getnewargs__", 14U) != 0) {
        tinypy_value_t *getnewargs = tinypy_object_get_attr(self, "__getnewargs__", 14U, out_error);

        if (getnewargs == NULL) {
            goto cleanup;
        }
        new_arguments = __tinypy_constructor_call_with_items(vm, getnewargs, NULL, 0U, out_error);
        TINYPY_DECREF(getnewargs);
        if (new_arguments == NULL) {
            goto cleanup;
        }
        if (TINYPY_VALUE_KIND(new_arguments) != TINYPY_VALUE_TUPLE) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "__getnewargs__ should return a tuple", out_error);
            goto cleanup;
        }
    }
    else {
        new_arguments = tinypy_tuple_from_items(vm, NULL, 0U);
    } {
        size_t argument_count = TINYPY_TUPLE_SIZE(new_arguments);
        size_t output_count;
        tinypy_value_t **items;
        size_t index;

        assert(argument_count < SIZE_MAX);
        output_count = argument_count + 1U;
        assert(output_count <= SIZE_MAX / sizeof(*items));
        items = (tinypy_value_t **)tinypy_internal_vm_allocate(vm, output_count * sizeof(*items), (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
        items[0] = &self->type->base.base;
        for (index = 0U; index < argument_count; ++index) {
            items[index + 1U] = TINYPY_TUPLE_GET(new_arguments, index);
        }
        constructor_arguments = tinypy_tuple_from_items(vm, items, output_count);
        tinypy_internal_vm_deallocate(vm, items, output_count * sizeof(*items), (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    }
    if (tinypy_object_has_attr(self, "__getstate__", 12U) != 0) {
        tinypy_value_t *getstate = tinypy_object_get_attr(self, "__getstate__", 12U, out_error);

        if (getstate == NULL) {
            goto cleanup;
        }
        state = __tinypy_constructor_call_with_items(vm, getstate, NULL, 0U, out_error);
        TINYPY_DECREF(getstate);
        if (state == NULL) {
            goto cleanup;
        }
    }
    else {
        tinypy_value_t **dict_slot = tinypy_internal_object_dict_slot(self);

        if (dict_slot != NULL && *dict_slot != NULL) {
            state = *dict_slot;
            TINYPY_INCREF(state);
        }
        else {
            state = tinypy_none_get(vm);
        }
    }
    if (TINYPY_VALUE_KIND(self) == TINYPY_VALUE_LIST) {
        list_items = tinypy_iter(self, out_error);
        if (list_items == NULL) {
            goto cleanup;
        }
    }
    else {
        list_items = tinypy_none_get(vm);
    }
    if (TINYPY_VALUE_KIND(self) == TINYPY_VALUE_DICT) {
        tinypy_value_t *iteritems = tinypy_object_get_attr(self, "iteritems", 9U, out_error);

        if (iteritems == NULL) {
            goto cleanup;
        }
        dict_items = __tinypy_constructor_call_with_items(vm, iteritems, NULL, 0U, out_error);
        TINYPY_DECREF(iteritems);
        if (dict_items == NULL) {
            goto cleanup;
        }
    }
    else {
        dict_items = tinypy_none_get(vm);
    } {
        tinypy_value_t *items[] = {newobj, constructor_arguments, state, list_items, dict_items};

        result = tinypy_tuple_from_items(vm, items, 5U);
    }

cleanup:
    if (dict_items != NULL) {
        TINYPY_DECREF(dict_items);
    }
    if (list_items != NULL) {
        TINYPY_DECREF(list_items);
    }
    if (state != NULL) {
        TINYPY_DECREF(state);
    }
    if (constructor_arguments != NULL) {
        TINYPY_DECREF(constructor_arguments);
    }
    if (new_arguments != NULL) {
        TINYPY_DECREF(new_arguments);
    }
    TINYPY_DECREF(newobj);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_constructor_object_reduce_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *protocol_value;
    int64_t protocol;

    (void)user_data;
    if (__tinypy_constructor_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_constructor_argument_count(vm, args, 1U, 2U, out_error) == 0) {
        return NULL;
    }
    if (TINYPY_TUPLE_SIZE(args) == 2U) {
        protocol_value = TINYPY_TUPLE_GET(args, 1U);
        if (TINYPY_VALUE_KIND(protocol_value) != TINYPY_VALUE_BOOL && TINYPY_VALUE_KIND(protocol_value) != TINYPY_VALUE_INTEGER) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "pickle protocol must be an integer", out_error);
            return NULL;
        }
        protocol = TINYPY_INTEGER_VALUE(protocol_value);
    }
    else {
        protocol_value = tinypy_integer_from_i64(vm, INT64_C(0));
        protocol = 0;
    }
    tinypy_value_t *result = __tinypy_constructor_object_common_reduce(TINYPY_TUPLE_GET(args, 0U), protocol_value, protocol, out_error);
    if (TINYPY_TUPLE_SIZE(args) == 1U) {
        TINYPY_DECREF(protocol_value);
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_constructor_object_reduce_ex_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_constructor_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_constructor_argument_count(vm, args, 1U, 2U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *self = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *class_reduce = tinypy_type_get_attr(self->type, "__reduce__", 10U);
    tinypy_value_t *object_reduce = tinypy_type_get_attr(&vm->object_type, "__reduce__", 10U);

    if (class_reduce != NULL && class_reduce != object_reduce) {
        tinypy_value_t *reduce = tinypy_object_get_attr(self, "__reduce__", 10U, out_error);

        if (reduce == NULL) {
            return NULL;
        }
        tinypy_value_t *result = __tinypy_constructor_call_with_items(vm, reduce, NULL, 0U, out_error);
        TINYPY_DECREF(reduce);
        return result;
    }
    return __tinypy_constructor_object_reduce_method(function, args, kwargs, user_data, out_error);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_constructor_tuple_new_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *result;

    (void)user_data;
    if (__tinypy_constructor_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_constructor_argument_count(vm, args, 1U, 2U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *type_value = TINYPY_TUPLE_GET(args, 0U);
    if (TINYPY_VALUE_KIND(type_value) != TINYPY_VALUE_TYPE || tinypy_type_is_subtype((tinypy_type_t *)type_value, &vm->tuple_type) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "tuple.__new__ requires a tuple subtype", out_error);
        return NULL;
    }
    tinypy_value_t *selected_value_4;
    if (TINYPY_TUPLE_SIZE(args) == 1U) {
        selected_value_4 = tinypy_tuple_from_items(vm, NULL, 0U);
    }
    else {
        tinypy_value_t *const *tuple_items = tinypy_internal_tuple_items(args);
        selected_value_4 = tinypy_tuple_from_items(vm, &tuple_items[1], 1U);
    }
    tinypy_value_t *constructor_args = selected_value_4;
    result = tinypy_internal_tuple_create((tinypy_type_t *)type_value, constructor_args, NULL, out_error);
    TINYPY_DECREF(constructor_args);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_constructor_type_mro_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_constructor_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_constructor_argument_count(vm, args, 1U, 1U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *class_value = TINYPY_TUPLE_GET(args, 0U);
    if (TINYPY_VALUE_KIND(class_value) != TINYPY_VALUE_TYPE) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "mro() requires a type", out_error);
        return NULL;
    }
    tinypy_type_t *type = (tinypy_type_t *)class_value;
    tinypy_value_t *const *tuple_items = tinypy_internal_tuple_items(type->mro);
    size_t tuple_size = TINYPY_TUPLE_SIZE(type->mro);
    return tinypy_list_from_items(vm, tuple_items, tuple_size);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_constructor_type_subclasses_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_constructor_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_constructor_argument_count(vm, args, 1U, 1U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *class_value = TINYPY_TUPLE_GET(args, 0U);
    if (TINYPY_VALUE_KIND(class_value) != TINYPY_VALUE_TYPE) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "__subclasses__() requires a type", out_error);
        return NULL;
    }
    return tinypy_internal_type_subclasses((tinypy_type_t *)class_value);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_constructor_add_method(tinypy_type_t *type, const char *name, size_t name_size, tinypy_native_function_callback_t callback, int32_t static_method) {
    tinypy_value_t *function = tinypy_native_function_new(type->vm, name, name_size, callback, NULL, NULL);
    tinypy_value_t *attribute = static_method != 0 ? tinypy_static_method_new(function) : function;
    tinypy_value_t *key = tinypy_string_from_bytes(type->vm, name, name_size);

    tinypy_dict_set(type->dict, key, attribute);
    TINYPY_DECREF(key);
    if (attribute != function) {
        TINYPY_DECREF(attribute);
    }
    TINYPY_DECREF(function);
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_initialize_constructor_types(tinypy_vm_t *vm) {
    __tinypy_constructor_add_method(&vm->type_type, "__new__", 7U, __tinypy_constructor_type_new_method, INT32_C(1));
    __tinypy_constructor_add_method(&vm->type_type, "__init__", 8U, __tinypy_constructor_type_init_method, INT32_C(0));
    __tinypy_constructor_add_method(&vm->type_type, "mro", 3U, __tinypy_constructor_type_mro_method, INT32_C(0));
    __tinypy_constructor_add_method(&vm->type_type, "__subclasses__", 14U, __tinypy_constructor_type_subclasses_method, INT32_C(0));
    __tinypy_constructor_add_method(&vm->object_type, "__new__", 7U, __tinypy_constructor_object_new_method, INT32_C(1));
    __tinypy_constructor_add_method(&vm->object_type, "__init__", 8U, __tinypy_constructor_object_init_method, INT32_C(0));
    __tinypy_constructor_add_method(&vm->object_type, "__reduce__", 10U, __tinypy_constructor_object_reduce_method, INT32_C(0));
    __tinypy_constructor_add_method(&vm->object_type, "__reduce_ex__", 13U, __tinypy_constructor_object_reduce_ex_method, INT32_C(0));
    __tinypy_constructor_add_method(&vm->tuple_type, "__new__", 7U, __tinypy_constructor_tuple_new_method, INT32_C(1));
}
