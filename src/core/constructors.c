#include "internal.h"

#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct tinypy_constructor_text_view_t {
    const uint8_t *bytes;
    size_t size;
    uint8_t *owned;
    size_t capacity;
} tinypy_constructor_text_view_t;
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_constructor_no_keywords(tinypy_vm_t *vm, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    if (kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "constructor does not accept keyword arguments", out_error);
        return TINYPY_FALSE;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_constructor_argument_count(tinypy_vm_t *vm, tinypy_value_t *args, size_t minimum, size_t maximum, tinypy_error_t **out_error) {
    size_t count = TINYPY_TUPLE_SIZE(args);

    if (count < minimum || count > maximum) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "constructor received the wrong number of arguments", out_error);
        return TINYPY_FALSE;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_constructor_object_has_excess_arguments(tinypy_value_t *args, tinypy_value_t *kwargs) {
    tinypy_bool_t return_value_1 = TINYPY_TUPLE_SIZE(args) > 1U || (kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_constructor_has_mutable_builtin_layout(tinypy_value_type_e kind) {
    return kind == TINYPY_VALUE_LIST || kind == TINYPY_VALUE_DICT || kind == TINYPY_VALUE_SET || kind == TINYPY_VALUE_BYTEARRAY ? TINYPY_TRUE : TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_constructor_has_immutable_builtin_layout(tinypy_value_type_e kind) {
    return kind == TINYPY_VALUE_INTEGER || kind == TINYPY_VALUE_LONG || kind == TINYPY_VALUE_FLOAT || kind == TINYPY_VALUE_COMPLEX || kind == TINYPY_VALUE_STRING || kind == TINYPY_VALUE_UNICODE ? TINYPY_TRUE : TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_constructor_tail_arguments(tinypy_vm_t *vm, tinypy_value_t *args) {
    size_t size = TINYPY_TUPLE_SIZE(args);

    if (size <= 1U) {
        tinypy_value_t *return_value_1 = tinypy_tuple_from_items(vm, NULL, 0U);
        return return_value_1;
    }
    tinypy_value_t *return_value_2 = tinypy_tuple_from_items(vm, tinypy_internal_tuple_items(args) + 1U, size - 1U);
    return return_value_2;
}
//////////////////////////////////////////////////////////////////////////
#if defined(TINYPY_CYCLE_DIAGNOSTICS)
static void __tinypy_constructor_rebuild_container_diagnostics(tinypy_value_t *value) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);

    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_LIST) {
        __tinypy_internal_cycle_diagnostics_list_clear(vm, value);
        __tinypy_internal_cycle_diagnostics_list_extend(vm, value, 0U, TINYPY_LIST_OBJECT(value)->items, TINYPY_LIST_SIZE(value));
    }
    else if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_DICT) {
        tinypy_dict_entry_t *entry;
        tinypy_dict_entry_t *end;

        __tinypy_internal_cycle_diagnostics_dict_clear(vm, value);
        entry = TINYPY_DICT_ITERATOR_BEGIN(value);
        end = TINYPY_DICT_ITERATOR_END(value);
        for (; entry != end; ++entry) {
            if (TINYPY_DICT_ENTRY_IS_ACTIVE(entry)) {
                __tinypy_internal_cycle_diagnostics_dict_set(vm, value, entry->key, entry->value, TINYPY_TRUE);
            }
        }
    }
}
#endif
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_constructor_call_conversion(tinypy_value_t *value, const char *name, size_t name_size, tinypy_bool_t *out_handled, tinypy_error_t **out_error) {
    tinypy_value_t *method;
    tinypy_value_t *args;
    tinypy_value_t *result;

    *out_handled = TINYPY_FALSE;
    if (tinypy_internal_object_has_special(value, name, name_size) == 0) {
        return NULL;
    }
    *out_handled = TINYPY_TRUE;
    method = tinypy_internal_object_get_special(value, name, name_size, out_error);
    if (method == NULL) {
        return NULL;
    }
    args = tinypy_tuple_from_items(TINYPY_VALUE_VM(value), NULL, 0U);
    result = tinypy_call(method, args, NULL, out_error);
    TINYPY_DECREF(args);
    TINYPY_DECREF(method);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_constructor_unicode_from_default_encoding(tinypy_vm_t *vm, tinypy_value_t *text, tinypy_error_t **out_error) {
    const uint8_t *bytes;
    size_t size;
    size_t index;

    if (TINYPY_VALUE_KIND(text) == TINYPY_VALUE_UNICODE) {
        TINYPY_INCREF(text);
        return text;
    }
    if (TINYPY_VALUE_KIND(text) != TINYPY_VALUE_STRING) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "coercing to Unicode requires a string", out_error);
        return NULL;
    }
    bytes = (const uint8_t *)tinypy_string_view(text, &size);
    for (index = 0U; index < size; ++index) {
        if (bytes[index] >= 0x80U) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_UNICODE_DECODE, "ascii decode error", out_error);
            return NULL;
        }
    }
    tinypy_value_t *return_value = tinypy_unicode_from_utf8(vm, (const char *)bytes, size);
    return return_value;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_object_unicode(tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_bool_t handled;
    tinypy_value_t *text;
    tinypy_value_t *result;

    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_UNICODE) {
        TINYPY_INCREF(value);
        return value;
    }
    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_STRING) {
        tinypy_value_t *return_value = __tinypy_constructor_unicode_from_default_encoding(vm, value, out_error);
        return return_value;
    }
    text = __tinypy_constructor_call_conversion(value, "__unicode__", 11U, &handled, out_error);
    if (handled == 0) {
        text = __tinypy_constructor_call_conversion(value, "__str__", 7U, &handled, out_error);
        if (handled == 0) {
            text = tinypy_object_str(value, out_error);
        }
    }
    if (text == NULL) {
        return NULL;
    }
    result = __tinypy_constructor_unicode_from_default_encoding(vm, text, out_error);
    TINYPY_DECREF(text);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_constructor_ascii_space(uint8_t character) {
    return character == (uint8_t)' ' || character == (uint8_t)'\t' || character == (uint8_t)'\n' || character == (uint8_t)'\r' || character == (uint8_t)'\v' || character == (uint8_t)'\f';
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_constructor_text_view_initialize(tinypy_vm_t *vm, tinypy_value_t *text, tinypy_constructor_text_view_t *view) {
    const uint8_t *bytes = TINYPY_TEXT_BYTES(text);
    size_t size = TINYPY_TEXT_BYTE_SIZE(text);
    size_t input = 0U;
    size_t output = 0U;

    view->bytes = bytes;
    view->size = size;
    view->owned = NULL;
    view->capacity = 0U;
    if (TINYPY_VALUE_KIND(text) != TINYPY_VALUE_UNICODE || size == 0U) {
        return;
    }
    view->owned = (uint8_t *)tinypy_internal_vm_allocate(vm, size);
    view->capacity = size;
    while (input < size) {
        uint32_t code_point;
        uint8_t digit;
        size_t width = tinypy_internal_utf8_decode(bytes + input, size - input, &code_point);

        if (code_point < UINT32_C(0x80)) {
            view->owned[output++] = (uint8_t)code_point;
        }
        else if (tinypy_internal_unicode_decimal_digit(code_point, &digit) != 0) {
            view->owned[output++] = (uint8_t)('0' + digit);
        }
        else if (tinypy_internal_unicode_is_space(code_point) != 0) {
            view->owned[output++] = (uint8_t)' ';
        }
        else {
            view->owned[output++] = UINT8_C(0xff);
        }
        input += width;
    }
    view->bytes = view->owned;
    view->size = output;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_constructor_text_view_destroy(tinypy_vm_t *vm, tinypy_constructor_text_view_t *view) {
    if (view->owned != NULL) {
        tinypy_internal_vm_deallocate(vm, view->owned, view->capacity);
    }
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_constructor_digit(uint8_t character) {
    if (character >= (uint8_t)'0' && character <= (uint8_t)'9') {
        return (int32_t)(character - (uint8_t)'0');
    }
    if (character >= (uint8_t)'a' && character <= (uint8_t)'z') {
        return (int32_t)(character - (uint8_t)'a') + INT32_C(10);
    }
    if (character >= (uint8_t)'A' && character <= (uint8_t)'Z') {
        return (int32_t)(character - (uint8_t)'A') + INT32_C(10);
    }
    return INT32_C(-1);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_constructor_base_value(tinypy_vm_t *vm, tinypy_value_t *value, int32_t *out_base, tinypy_error_t **out_error) {
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
        return TINYPY_FALSE;
    }
    if (base != 0 && (base < 2 || base > 36)) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "integer base must be zero or between 2 and 36", out_error);
        return TINYPY_FALSE;
    }
    *out_base = (int32_t)base;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_constructor_long_multiply_add(uint16_t *digits, size_t *count, uint32_t multiplier, uint32_t addition) {
    uint64_t carry = addition;
    size_t index;

    for (index = 0U; index < *count; ++index) {
        uint64_t current = (uint64_t)digits[index] * multiplier + carry;

        digits[index] = (uint16_t)(current & UINT64_C(0x7fff));
        carry = current >> 15U;
    }
    while (carry != 0U) {
        digits[(*count)++] = (uint16_t)(carry & UINT64_C(0x7fff));
        carry >>= 15U;
    }
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_constructor_integer_from_digits(tinypy_vm_t *vm, int32_t sign, const uint16_t *digits, size_t digit_count, int32_t force_long) {
    uint64_t magnitude = UINT64_C(0);
    uint64_t limit = sign < 0 ? (uint64_t)INT64_MAX + UINT64_C(1) : (uint64_t)INT64_MAX;
    size_t index = digit_count;
    tinypy_bool_t fits = TINYPY_TRUE;

    if (force_long != 0) {
        tinypy_value_t *return_value_1 = tinypy_long_from_base15_digits(vm, digit_count == 0U ? 0 : sign, digits, digit_count);
        return return_value_1;
    }
    while (index != 0U) {
        uint64_t digit = digits[--index];

        if (magnitude > (limit - digit) / UINT64_C(32768)) {
            fits = TINYPY_FALSE;
            break;
        }
        magnitude = magnitude * UINT64_C(32768) + digit;
    }
    if (fits != 0) {
        int64_t value;

        if (sign < 0) {
            value = magnitude == (uint64_t)INT64_MAX + UINT64_C(1) ? INT64_MIN : -(int64_t)magnitude;
        }
        else {
            value = (int64_t)magnitude;
        }
        tinypy_value_t *return_value_2 = tinypy_integer_from_i64(vm, value);
        return return_value_2;
    }
    tinypy_value_t *return_value_3 = tinypy_long_from_base15_digits(vm, sign, digits, digit_count);
    return return_value_3;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_constructor_integer_bytes(tinypy_vm_t *vm, const uint8_t *bytes, size_t size, int32_t base, int32_t force_long, tinypy_error_t **out_error) {
    size_t begin = 0U;
    size_t end = size;
    size_t index;
    int32_t sign = INT32_C(1);
    int32_t actual_base = base;
    size_t digit_count = 0U;
    uint16_t *digits;
    size_t capacity;
    size_t output_count = 0U;
    uint32_t chunk_base;
    size_t chunk_digits;
    uint32_t chunk = 0U;
    size_t chunk_length = 0U;

    while (begin < end && __tinypy_constructor_ascii_space(bytes[begin]) != 0) {
        begin += 1U;
    }
    while (end > begin && __tinypy_constructor_ascii_space(bytes[end - 1U]) != 0) {
        end -= 1U;
    }
    if (begin < end && (bytes[begin] == (uint8_t)'+' || bytes[begin] == (uint8_t)'-')) {
        if (bytes[begin] == (uint8_t)'-') {
            sign = INT32_C(-1);
        }
        begin += 1U;
        while (begin < end && __tinypy_constructor_ascii_space(bytes[begin]) != 0) {
            begin += 1U;
        }
    }
    if (actual_base == 0) {
        if (end - begin >= 2U && bytes[begin] == (uint8_t)'0' && (bytes[begin + 1U] == (uint8_t)'x' || bytes[begin + 1U] == (uint8_t)'X')) {
            actual_base = 16;
            begin += 2U;
        }
        else if (end - begin >= 2U && bytes[begin] == (uint8_t)'0' && (bytes[begin + 1U] == (uint8_t)'b' || bytes[begin + 1U] == (uint8_t)'B')) {
            actual_base = 2;
            begin += 2U;
        }
        else if (end - begin >= 2U && bytes[begin] == (uint8_t)'0' && (bytes[begin + 1U] == (uint8_t)'o' || bytes[begin + 1U] == (uint8_t)'O')) {
            actual_base = 8;
            begin += 2U;
        }
        else {
            actual_base = begin < end && bytes[begin] == (uint8_t)'0' ? 8 : 10;
        }
    }
    else if (end - begin >= 2U && bytes[begin] == (uint8_t)'0') {
        uint8_t prefix = bytes[begin + 1U];

        if ((actual_base == 16 && (prefix == (uint8_t)'x' || prefix == (uint8_t)'X')) || (actual_base == 8 && (prefix == (uint8_t)'o' || prefix == (uint8_t)'O')) || (actual_base == 2 && (prefix == (uint8_t)'b' || prefix == (uint8_t)'B'))) {
            begin += 2U;
        }
    }
    if (force_long != 0 && actual_base <= 21 && begin < end && (bytes[end - 1U] == (uint8_t)'L' || bytes[end - 1U] == (uint8_t)'l')) {
        end -= 1U;
    }
    for (index = begin; index < end; ++index) {
        int32_t digit = __tinypy_constructor_digit(bytes[index]);

        if (digit < 0 || digit >= actual_base) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "invalid literal for integer conversion", out_error);
            return NULL;
        }
        digit_count += 1U;
    }
    if (digit_count == 0U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "invalid literal for integer conversion", out_error);
        return NULL;
    }
    capacity = digit_count / 2U + 2U;
    if (capacity > SIZE_MAX / sizeof(*digits)) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "integer literal is too large", out_error);
        return NULL;
    }
    digits = (uint16_t *)tinypy_internal_vm_allocate_checked(vm, capacity * sizeof(*digits), out_error);
    if (digits == NULL) {
        return NULL;
    }
    chunk_base = (uint32_t)actual_base;
    chunk_digits = 1U;
    while (chunk_base <= UINT32_MAX / (uint32_t)actual_base) {
        chunk_base *= (uint32_t)actual_base;
        chunk_digits += 1U;
    }
    for (index = begin; index < end; ++index) {
        uint32_t digit = (uint32_t)__tinypy_constructor_digit(bytes[index]);

        chunk = chunk * (uint32_t)actual_base + digit;
        chunk_length += 1U;
        if (chunk_length == chunk_digits) {
            __tinypy_constructor_long_multiply_add(digits, &output_count, chunk_base, chunk);
            chunk = 0U;
            chunk_length = 0U;
        }
    }
    if (chunk_length != 0U) {
        uint32_t multiplier = 1U;

        for (index = 0U; index < chunk_length; ++index) {
            multiplier *= (uint32_t)actual_base;
        }
        __tinypy_constructor_long_multiply_add(digits, &output_count, multiplier, chunk);
    }
    tinypy_value_t *result = __tinypy_constructor_integer_from_digits(vm, sign, digits, output_count, force_long);
    tinypy_internal_vm_deallocate(vm, digits, capacity * sizeof(*digits));
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_constructor_integer_text(tinypy_vm_t *vm, tinypy_value_t *text, int32_t base, int32_t force_long, tinypy_error_t **out_error) {
    tinypy_constructor_text_view_t view;
    tinypy_value_t *result;

    __tinypy_constructor_text_view_initialize(vm, text, &view);
    result = __tinypy_constructor_integer_bytes(vm, view.bytes, view.size, base, force_long, out_error);
    __tinypy_constructor_text_view_destroy(vm, &view);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_constructor_number_as_double(tinypy_vm_t *vm, tinypy_value_t *value, double *out_value, tinypy_bool_t allow_complex, tinypy_error_t **out_error) {
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);

    if (kind == TINYPY_VALUE_BOOL || kind == TINYPY_VALUE_INTEGER) {
        *out_value = (double)TINYPY_INTEGER_VALUE(value);
    }
    else if (kind == TINYPY_VALUE_LONG) {
        if (tinypy_internal_long_as_double(value, out_value, out_error) == 0) {
            return TINYPY_FALSE;
        }
    }
    else if (kind == TINYPY_VALUE_FLOAT) {
        *out_value = TINYPY_FLOAT_OBJECT(value)->value;
    }
    else if (kind == TINYPY_VALUE_COMPLEX && allow_complex != 0 && TINYPY_COMPLEX_OBJECT(value)->imaginary == 0.0) {
        *out_value = TINYPY_COMPLEX_OBJECT(value)->real;
    }
    else {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "numeric conversion requires a number", out_error);
        return TINYPY_FALSE;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_long_from_double(tinypy_vm_t *vm, double value) {
    uint16_t digits[(DBL_MAX_EXP + 14) / 15];
    double magnitude = trunc(fabs(value));
    size_t count = 0U;

    while (magnitude >= 1.0) {
        double quotient = floor(magnitude / 32768.0);
        double remainder = magnitude - quotient * 32768.0;

        digits[count++] = (uint16_t)remainder;
        magnitude = quotient;
    }
    tinypy_value_t *return_value = tinypy_long_from_base15_digits(vm, value < 0.0 ? -1 : (count != 0U ? 1 : 0), digits, count);
    return return_value;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_constructor_text_equal_ascii(const uint8_t *bytes, size_t size, const char *ascii) {
    size_t index;

    for (index = 0U; index < size; ++index) {
        uint8_t left = bytes[index];
        uint8_t right = (uint8_t)ascii[index];

        if (right == 0U) {
            return TINYPY_FALSE;
        }
        if (left >= (uint8_t)'A' && left <= (uint8_t)'Z') {
            left = (uint8_t)(left + ((uint8_t)'a' - (uint8_t)'A'));
        }
        if (left != right) {
            return TINYPY_FALSE;
        }
    }
    return ascii[size] == '\0' ? TINYPY_TRUE : TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_constructor_decimal_double(tinypy_vm_t *vm, const uint8_t *bytes, size_t size, double *out_value) {
    char local[128];

    if (size < sizeof(local)) {
        char *end;
        double value;

        (void)memcpy(local, bytes, size);
        local[size] = '\0';
        value = strtod(local, &end);
        if (end == local + size) {
            *out_value = value;
            return TINYPY_TRUE;
        }
    }
    tinypy_bool_t return_value_1 = tinypy_internal_decimal_double(vm, (const char *)bytes, size, out_value);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_constructor_float_bytes(tinypy_vm_t *vm, const uint8_t *bytes, size_t size, double *out_value, tinypy_bool_t report_error, tinypy_error_t **out_error) {
    size_t begin = 0U;
    size_t end = size;
    size_t index;
    int32_t sign = INT32_C(1);
    size_t digits = 0U;

    while (begin < end && __tinypy_constructor_ascii_space(bytes[begin]) != 0) {
        begin += 1U;
    }
    while (end > begin && __tinypy_constructor_ascii_space(bytes[end - 1U]) != 0) {
        end -= 1U;
    }
    if (begin < end && (bytes[begin] == (uint8_t)'+' || bytes[begin] == (uint8_t)'-')) {
        if (bytes[begin] == (uint8_t)'-') {
            sign = INT32_C(-1);
        }
        begin += 1U;
    }
    if (__tinypy_constructor_text_equal_ascii(bytes + begin, end - begin, "nan") != 0) {
        *out_value = sign < 0 ? -NAN : NAN;
        return TINYPY_TRUE;
    }
    if (__tinypy_constructor_text_equal_ascii(bytes + begin, end - begin, "inf") != 0 || __tinypy_constructor_text_equal_ascii(bytes + begin, end - begin, "infinity") != 0) {
        *out_value = sign < 0 ? -INFINITY : INFINITY;
        return TINYPY_TRUE;
    }
    index = begin;
    while (index < end && bytes[index] >= (uint8_t)'0' && bytes[index] <= (uint8_t)'9') {
        digits += 1U;
        index += 1U;
    }
    if (index < end && bytes[index] == (uint8_t)'.') {
        index += 1U;
        while (index < end && bytes[index] >= (uint8_t)'0' && bytes[index] <= (uint8_t)'9') {
            digits += 1U;
            index += 1U;
        }
    }
    if (index < end && (bytes[index] == (uint8_t)'e' || bytes[index] == (uint8_t)'E')) {
        size_t exponent_digits = 0U;

        index += 1U;
        if (index < end && (bytes[index] == (uint8_t)'+' || bytes[index] == (uint8_t)'-')) {
            index += 1U;
        }
        while (index < end && bytes[index] >= (uint8_t)'0' && bytes[index] <= (uint8_t)'9') {
            exponent_digits += 1U;
            index += 1U;
        }
        if (exponent_digits == 0U) {
            digits = 0U;
        }
    }
    if (digits == 0U || index != end) {
        if (report_error != 0) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "invalid literal for float conversion", out_error);
        }
        return TINYPY_FALSE;
    }
    double magnitude;

    if (__tinypy_constructor_decimal_double(vm, bytes + begin, end - begin, &magnitude) == 0) {
        if (report_error != 0) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "invalid literal for float conversion", out_error);
        }
        return TINYPY_FALSE;
    }
    *out_value = sign < 0 ? -magnitude : magnitude;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_constructor_float_text(tinypy_vm_t *vm, tinypy_value_t *text, double *out_value, tinypy_error_t **out_error) {
    tinypy_constructor_text_view_t view;
    tinypy_bool_t result;

    __tinypy_constructor_text_view_initialize(vm, text, &view);
    result = __tinypy_constructor_float_bytes(vm, view.bytes, view.size, out_value, TINYPY_TRUE, out_error);
    __tinypy_constructor_text_view_destroy(vm, &view);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_constructor_complex_text(tinypy_vm_t *vm, tinypy_value_t *text, double *out_real, double *out_imaginary, tinypy_error_t **out_error) {
    tinypy_constructor_text_view_t view;
    const uint8_t *bytes;
    size_t begin = 0U;
    size_t end;
    size_t split = SIZE_MAX;
    size_t index;
    tinypy_bool_t imaginary = TINYPY_FALSE;
    tinypy_bool_t valid = TINYPY_FALSE;

    __tinypy_constructor_text_view_initialize(vm, text, &view);
    bytes = view.bytes;
    end = view.size;
    while (begin < end && __tinypy_constructor_ascii_space(bytes[begin]) != 0) {
        begin += 1U;
    }
    while (end > begin && __tinypy_constructor_ascii_space(bytes[end - 1U]) != 0) {
        end -= 1U;
    }
    if (end - begin >= 2U && bytes[begin] == (uint8_t)'(' && bytes[end - 1U] == (uint8_t)')') {
        begin += 1U;
        end -= 1U;
        while (begin < end && __tinypy_constructor_ascii_space(bytes[begin]) != 0) {
            begin += 1U;
        }
        while (end > begin && __tinypy_constructor_ascii_space(bytes[end - 1U]) != 0) {
            end -= 1U;
        }
    }
    for (index = begin; index < end; ++index) {
        if (__tinypy_constructor_ascii_space(bytes[index]) != 0) {
            goto done;
        }
    }
    if (begin < end && (bytes[end - 1U] == (uint8_t)'j' || bytes[end - 1U] == (uint8_t)'J')) {
        imaginary = TINYPY_TRUE;
        end -= 1U;
    }
    if (imaginary == 0) {
        valid = __tinypy_constructor_float_bytes(vm, bytes + begin, end - begin, out_real, TINYPY_FALSE, out_error);
        *out_imaginary = 0.0;
        goto done;
    }
    for (index = begin + 1U; index < end; ++index) {
        if ((bytes[index] == (uint8_t)'+' || bytes[index] == (uint8_t)'-') && bytes[index - 1U] != (uint8_t)'e' && bytes[index - 1U] != (uint8_t)'E') {
            split = index;
        }
    }
    if (split != SIZE_MAX) {
        if (__tinypy_constructor_float_bytes(vm, bytes + begin, split - begin, out_real, TINYPY_FALSE, out_error) == 0) {
            goto done;
        }
        begin = split;
    }
    else {
        *out_real = 0.0;
    }
    if (end - begin == 0U || (end - begin == 1U && bytes[begin] == (uint8_t)'+')) {
        *out_imaginary = 1.0;
        valid = TINYPY_TRUE;
    }
    else if (end - begin == 1U && bytes[begin] == (uint8_t)'-') {
        *out_imaginary = -1.0;
        valid = TINYPY_TRUE;
    }
    else {
        valid = __tinypy_constructor_float_bytes(vm, bytes + begin, end - begin, out_imaginary, TINYPY_FALSE, out_error);
    }

done:
    __tinypy_constructor_text_view_destroy(vm, &view);
    if (valid == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "complex() arg is a malformed string", out_error);
    }
    return valid;
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
    if (tinypy_internal_list_reserve_checked(vm, result, tinypy_internal_iterable_size_hint(iterable), out_error) == 0) {
        TINYPY_DECREF(iterator);
        TINYPY_DECREF(result);
        return NULL;
    }
    for (;;) {
        tinypy_value_t *item = tinypy_next(iterator, &iteration_error);

        if (item == NULL) {
            break;
        }
        if (tinypy_internal_list_append_checked(result, item, out_error) == 0) {
            TINYPY_DECREF(item);
            TINYPY_DECREF(iterator);
            TINYPY_DECREF(result);
            return NULL;
        }
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
    }
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
    base_types = base_count != 0U ? (tinypy_type_t **)tinypy_internal_vm_allocate(vm, base_count * sizeof(*base_types)) : NULL;
    for (index = 0U; index < base_count; ++index) {
        tinypy_value_t *base = TINYPY_TUPLE_GET(bases, index);

        if (TINYPY_VALUE_KIND(base) != TINYPY_VALUE_TYPE) {
            tinypy_internal_vm_deallocate(vm, base_types, base_count * sizeof(*base_types));
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "type base is not a type", out_error);
            return NULL;
        }
        base_types[index] = (tinypy_type_t *)base;
    }
    name_bytes = (const char *)tinypy_string_view(name, &name_size);
    created = tinypy_type_new(vm, name_bytes, name_size, (const tinypy_type_t *const *)base_types, base_count, NULL, namespace_dict, out_error);
    if (base_types != NULL) {
        tinypy_internal_vm_deallocate(vm, base_types, base_count * sizeof(*base_types));
    }
    tinypy_value_t *return_value_1 = created != NULL ? tinypy_type_as_value(created) : NULL;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_object_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    if (__tinypy_constructor_no_keywords(type->vm, kwargs, out_error) == 0 || __tinypy_constructor_argument_count(type->vm, args, 0U, 0U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *return_value_1 = tinypy_internal_object_allocate(type->vm, type, type->basic_size);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_bool_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = type->vm;
    int32_t truth;

    if (__tinypy_constructor_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_constructor_argument_count(vm, args, 0U, 1U, out_error) == 0) {
        return NULL;
    }
    if (TINYPY_TUPLE_SIZE(args) == 0U) {
        tinypy_value_t *return_value_1 = tinypy_bool_from_i32(vm, INT32_C(0));
        return return_value_1;
    }
    tinypy_value_t *item = TINYPY_TUPLE_GET(args, 0U);
    truth = tinypy_truth(item, out_error);
    tinypy_value_t *return_value_2 = truth < 0 ? NULL : tinypy_bool_from_i32(vm, truth);
    return return_value_2;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_constructor_integer_common(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, int32_t force_long, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = type->vm;
    size_t count = TINYPY_TUPLE_SIZE(args);
    int32_t base = 10;
    tinypy_value_type_e kind;
    tinypy_bool_t handled;
    const char *method_name;
    size_t method_size;
    tinypy_value_t *conversion_result;

    if (kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U) {
        tinypy_value_t *key = tinypy_string_from_bytes(vm, "base", 4U);
        tinypy_value_t *base_value = TINYPY_DICT_SIZE(kwargs) == 1U ? tinypy_dict_get_optional(kwargs, key) : NULL;

        tinypy_bool_t condition = base_value == NULL;
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
        tinypy_value_t *return_value_1 = force_long != 0 ? tinypy_long_from_i64(vm, INT64_C(0)) : tinypy_integer_from_i64(vm, INT64_C(0));
        return return_value_1;
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
        tinypy_value_t *return_value_2 = __tinypy_constructor_integer_text(vm, value, base, force_long, out_error);
        return return_value_2;
    }
    if (kind == TINYPY_VALUE_STRING || kind == TINYPY_VALUE_UNICODE) {
        tinypy_value_t *return_value_3 = __tinypy_constructor_integer_text(vm, value, 10, force_long, out_error);
        return return_value_3;
    }
    if (kind == TINYPY_VALUE_BOOL || kind == TINYPY_VALUE_INTEGER) {
        tinypy_value_t *return_value_4 = force_long != 0 ? tinypy_long_from_i64(vm, TINYPY_INTEGER_VALUE(value)) : tinypy_integer_from_i64(vm, TINYPY_INTEGER_VALUE(value));
        return return_value_4;
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

                tinypy_value_t *return_value_5 = tinypy_integer_from_i64(vm, converted);
                return return_value_5;
            }
        }
        TINYPY_INCREF(value);
        return value;
    }
    if (kind == TINYPY_VALUE_FLOAT) {
        double number = TINYPY_FLOAT_OBJECT(value)->value;

        if (isnan(number)) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "cannot convert float NaN to integer", out_error);
            return NULL;
        }
        if (isinf(number)) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "cannot convert float infinity to integer", out_error);
            return NULL;
        }
        if (number >= -0x1p63 && number < 0x1p63) {
            tinypy_value_t *return_value_6 = force_long != 0 ? tinypy_long_from_i64(vm, (int64_t)number) : tinypy_integer_from_i64(vm, (int64_t)number);
            return return_value_6;
        }
        tinypy_value_t *return_value_7 = tinypy_internal_long_from_double(vm, number);
        return return_value_7;
    }
    method_name = force_long != 0 ? "__long__" : "__int__";
    method_size = force_long != 0 ? 8U : 7U;
    conversion_result = __tinypy_constructor_call_conversion(value, method_name, method_size, &handled, out_error);
    if (handled == 0 && force_long != 0) {
        conversion_result = __tinypy_constructor_call_conversion(value, "__int__", 7U, &handled, out_error);
    }
    if (handled != 0) {
        tinypy_value_type_e converted_kind;

        if (conversion_result == NULL) {
            return NULL;
        }
        converted_kind = TINYPY_VALUE_KIND(conversion_result);
        if (converted_kind != TINYPY_VALUE_BOOL && converted_kind != TINYPY_VALUE_INTEGER && converted_kind != TINYPY_VALUE_LONG) {
            TINYPY_DECREF(conversion_result);
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "integer conversion method returned a non-integer", out_error);
            return NULL;
        }
        if (force_long != 0 && converted_kind != TINYPY_VALUE_LONG) {
            tinypy_value_t *long_result = tinypy_long_from_i64(vm, TINYPY_INTEGER_VALUE(conversion_result));

            TINYPY_DECREF(conversion_result);
            return long_result;
        }
        return conversion_result;
    }
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "value cannot be converted to integer", out_error);
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_integer_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_value_t *value = __tinypy_constructor_integer_common(type, args, kwargs, INT32_C(0), out_error);
    tinypy_value_t *return_value_1 = tinypy_internal_immutable_subclass_copy(type, value, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_long_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_value_t *value = __tinypy_constructor_integer_common(type, args, kwargs, INT32_C(1), out_error);
    tinypy_value_t *return_value_1 = tinypy_internal_immutable_subclass_copy(type, value, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_float_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = type->vm;
    double number;

    if (__tinypy_constructor_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_constructor_argument_count(vm, args, 0U, 1U, out_error) == 0) {
        return NULL;
    }
    if (TINYPY_TUPLE_SIZE(args) == 0U) {
        tinypy_value_t *value = tinypy_float_from_double(vm, 0.0);
        tinypy_value_t *return_value_1 = tinypy_internal_immutable_subclass_copy(type, value, out_error);
        return return_value_1;
    }
    tinypy_value_t *value = TINYPY_TUPLE_GET(args, 0U);
    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_STRING || TINYPY_VALUE_KIND(value) == TINYPY_VALUE_UNICODE) {
        if (__tinypy_constructor_float_text(vm, value, &number, out_error) == 0) {
            return NULL;
        }
    }
    else if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_BOOL || TINYPY_VALUE_KIND(value) == TINYPY_VALUE_INTEGER || TINYPY_VALUE_KIND(value) == TINYPY_VALUE_LONG || TINYPY_VALUE_KIND(value) == TINYPY_VALUE_FLOAT) {
        if (__tinypy_constructor_number_as_double(vm, value, &number, INT32_C(0), out_error) == 0) {
            return NULL;
        }
    }
    else {
        tinypy_bool_t handled;
        tinypy_value_t *converted = __tinypy_constructor_call_conversion(value, "__float__", 9U, &handled, out_error);

        if (handled == 0) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "value cannot be converted to float", out_error);
            return NULL;
        }
        if (converted == NULL) {
            return NULL;
        }
        if (TINYPY_VALUE_KIND(converted) != TINYPY_VALUE_FLOAT) {
            TINYPY_DECREF(converted);
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "__float__ returned a non-float", out_error);
            return NULL;
        }
        number = TINYPY_FLOAT_OBJECT(converted)->value;
        TINYPY_DECREF(converted);
    }
    tinypy_value_t *value_result = tinypy_float_from_double(vm, number);
    tinypy_value_t *return_value_2 = tinypy_internal_immutable_subclass_copy(type, value_result, out_error);
    return return_value_2;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_complex_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = type->vm;
    double real = 0.0;
    double imaginary = 0.0;
    tinypy_bool_t first_is_complex = TINYPY_FALSE;

    if (__tinypy_constructor_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_constructor_argument_count(vm, args, 0U, 2U, out_error) == 0) {
        return NULL;
    }
    if (TINYPY_TUPLE_SIZE(args) >= 1U) {
        tinypy_value_t *first = TINYPY_TUPLE_GET(args, 0U);

        if (TINYPY_VALUE_KIND(first) == TINYPY_VALUE_STRING || TINYPY_VALUE_KIND(first) == TINYPY_VALUE_UNICODE) {
            if (TINYPY_TUPLE_SIZE(args) == 2U) {
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "complex() cannot take a second argument if the first is a string", out_error);
                return NULL;
            }
            if (__tinypy_constructor_complex_text(vm, first, &real, &imaginary, out_error) == 0) {
                return NULL;
            }
        }
        else if (TINYPY_VALUE_KIND(first) == TINYPY_VALUE_COMPLEX) {
            real = TINYPY_COMPLEX_OBJECT(first)->real;
            imaginary = TINYPY_COMPLEX_OBJECT(first)->imaginary;
            first_is_complex = TINYPY_TRUE;
        }
        else if (TINYPY_VALUE_KIND(first) == TINYPY_VALUE_BOOL || TINYPY_VALUE_KIND(first) == TINYPY_VALUE_INTEGER || TINYPY_VALUE_KIND(first) == TINYPY_VALUE_LONG || TINYPY_VALUE_KIND(first) == TINYPY_VALUE_FLOAT) {
            if (__tinypy_constructor_number_as_double(vm, first, &real, INT32_C(0), out_error) == 0) {
                return NULL;
            }
        }
        else {
            tinypy_bool_t handled;
            tinypy_bool_t used_complex_method;
            tinypy_value_t *converted = __tinypy_constructor_call_conversion(first, "__complex__", 11U, &handled, out_error);

            used_complex_method = handled;
            if (handled == 0) {
                converted = __tinypy_constructor_call_conversion(first, "__float__", 9U, &handled, out_error);
            }
            if (handled == 0) {
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "value cannot be converted to complex", out_error);
                return NULL;
            }
            if (converted == NULL) {
                return NULL;
            }
            if (TINYPY_VALUE_KIND(converted) == TINYPY_VALUE_COMPLEX) {
                real = TINYPY_COMPLEX_OBJECT(converted)->real;
                imaginary = TINYPY_COMPLEX_OBJECT(converted)->imaginary;
                first_is_complex = TINYPY_TRUE;
            }
            else if ((used_complex_method != 0 || TINYPY_VALUE_KIND(converted) == TINYPY_VALUE_FLOAT) &&
                     (TINYPY_VALUE_KIND(converted) == TINYPY_VALUE_BOOL || TINYPY_VALUE_KIND(converted) == TINYPY_VALUE_INTEGER || TINYPY_VALUE_KIND(converted) == TINYPY_VALUE_LONG || TINYPY_VALUE_KIND(converted) == TINYPY_VALUE_FLOAT)) {
                if (__tinypy_constructor_number_as_double(vm, converted, &real, INT32_C(0), out_error) == 0) {
                    TINYPY_DECREF(converted);
                    return NULL;
                }
            }
            else {
                TINYPY_DECREF(converted);
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "complex conversion method returned an invalid value", out_error);
                return NULL;
            }
            TINYPY_DECREF(converted);
        }
    }
    if (TINYPY_TUPLE_SIZE(args) == 2U) {
        double second_real;
        tinypy_value_t *second = TINYPY_TUPLE_GET(args, 1U);

        if (TINYPY_VALUE_KIND(second) == TINYPY_VALUE_COMPLEX) {
            second_real = TINYPY_COMPLEX_OBJECT(second)->real;
            real -= TINYPY_COMPLEX_OBJECT(second)->imaginary;
        }
        else if (TINYPY_VALUE_KIND(second) == TINYPY_VALUE_BOOL || TINYPY_VALUE_KIND(second) == TINYPY_VALUE_INTEGER || TINYPY_VALUE_KIND(second) == TINYPY_VALUE_LONG || TINYPY_VALUE_KIND(second) == TINYPY_VALUE_FLOAT) {
            if (__tinypy_constructor_number_as_double(vm, second, &second_real, INT32_C(0), out_error) == 0) {
                return NULL;
            }
        }
        else {
            tinypy_bool_t handled;
            tinypy_value_t *converted = __tinypy_constructor_call_conversion(second, "__float__", 9U, &handled, out_error);

            if (handled == 0) {
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "complex second argument cannot be converted to float", out_error);
                return NULL;
            }
            if (converted == NULL) {
                return NULL;
            }
            if (TINYPY_VALUE_KIND(converted) != TINYPY_VALUE_FLOAT) {
                TINYPY_DECREF(converted);
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "__float__ returned a non-float", out_error);
                return NULL;
            }
            second_real = TINYPY_FLOAT_OBJECT(converted)->value;
            TINYPY_DECREF(converted);
        }
        if (first_is_complex != 0) {
            imaginary += second_real;
        }
        else {
            imaginary = second_real;
        }
    }
    tinypy_value_t *value_result = tinypy_complex_from_doubles(vm, real, imaginary);
    tinypy_value_t *return_value_1 = tinypy_internal_immutable_subclass_copy(type, value_result, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_unicode_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = type->vm;

    if (__tinypy_constructor_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_constructor_argument_count(vm, args, 0U, 3U, out_error) == 0) {
        return NULL;
    }
    if (TINYPY_TUPLE_SIZE(args) == 0U) {
        tinypy_value_t *value = tinypy_unicode_from_utf8(vm, NULL, 0U);
        tinypy_value_t *return_value_1 = tinypy_internal_immutable_subclass_copy(type, value, out_error);
        return return_value_1;
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
        tinypy_value_t *return_value_1 = tinypy_internal_immutable_subclass_copy(type, result, out_error);
        return return_value_1;
    }
    tinypy_value_t *converted = tinypy_internal_object_unicode(value, out_error);
    tinypy_value_t *return_value_2 = tinypy_internal_immutable_subclass_copy(type, converted, out_error);
    return return_value_2;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_list_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = type->vm;

    if (__tinypy_constructor_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_constructor_argument_count(vm, args, 0U, 1U, out_error) == 0) {
        return NULL;
    }
    if (TINYPY_TUPLE_SIZE(args) == 0U) {
        tinypy_value_t *return_value_1 = tinypy_list_from_items(vm, NULL, 0U);
        return return_value_1;
    }
    tinypy_value_t *value = TINYPY_TUPLE_GET(args, 0U);
    if (value->type == &vm->types[TINYPY_VALUE_LIST]) {
        size_t list_size = TINYPY_LIST_SIZE(value);
        tinypy_value_t *return_value_2 = tinypy_list_from_items(vm, TINYPY_LIST_OBJECT(value)->items, list_size);
        return return_value_2;
    }
    if (value->type == &vm->types[TINYPY_VALUE_TUPLE]) {
        tinypy_value_t *const *tuple_items = tinypy_internal_tuple_items(value);
        size_t tuple_size = TINYPY_TUPLE_SIZE(value);
        tinypy_value_t *return_value_3 = tinypy_list_from_items(vm, tuple_items, tuple_size);
        return return_value_3;
    }
    tinypy_value_t *return_value_4 = __tinypy_constructor_sequence_to_list(vm, value, out_error);
    return return_value_4;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_tuple_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = type->vm;
    tinypy_value_t *result;

    if (__tinypy_constructor_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_constructor_argument_count(vm, args, 0U, 1U, out_error) == 0) {
        return NULL;
    }
    if (TINYPY_TUPLE_SIZE(args) == 0U) {
        tinypy_value_t *return_value_1 = type == &vm->types[TINYPY_VALUE_TUPLE] ? tinypy_tuple_from_items(vm, NULL, 0U) : tinypy_internal_tuple_subclass_from_items(type, NULL, 0U);
        return return_value_1;
    }
    tinypy_value_t *value = TINYPY_TUPLE_GET(args, 0U);
    if (type == &vm->types[TINYPY_VALUE_TUPLE] && value->type == &vm->types[TINYPY_VALUE_TUPLE]) {
        TINYPY_INCREF(value);
        return value;
    }
    if (value->type == &vm->types[TINYPY_VALUE_TUPLE]) {
        tinypy_value_t *selected_value_2;
        if (type == &vm->types[TINYPY_VALUE_TUPLE]) {
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
    if (value->type == &vm->types[TINYPY_VALUE_LIST]) {
        tinypy_value_t *selected_value_3;
        if (type == &vm->types[TINYPY_VALUE_TUPLE]) {
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
    if (type == &vm->types[TINYPY_VALUE_TUPLE]) {
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
static tinypy_bool_t __tinypy_constructor_dict_update(tinypy_value_t *result, tinypy_value_t *source, tinypy_error_t **out_error) {
    tinypy_bool_t return_value_1 = tinypy_internal_dict_update_from(result, source, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_dict_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = type->vm;

    if (__tinypy_constructor_argument_count(vm, args, 0U, 1U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *result = tinypy_dict_new(vm);
    tinypy_bool_t condition_2 = TINYPY_TUPLE_SIZE(args) == 1U;
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
    if (tinypy_type_is_subtype((tinypy_type_t *)metaclass_value, &vm->types[TINYPY_VALUE_TYPE]) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "type.__new__ requires a subtype of type", out_error);
        return NULL;
    }
    base_count = TINYPY_TUPLE_SIZE(bases);
    if (base_count != 0U) {
        base_types = (tinypy_type_t **)tinypy_internal_vm_allocate(vm, base_count * sizeof(*base_types));
    }
    for (index = 0U; index < base_count; ++index) {
        tinypy_value_t *base = TINYPY_TUPLE_GET(bases, index);

        if (TINYPY_VALUE_KIND(base) != TINYPY_VALUE_TYPE) {
            tinypy_internal_vm_deallocate(vm, base_types, base_count * sizeof(*base_types));
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "type.__new__ base is not a type", out_error);
            return NULL;
        }
        base_types[index] = (tinypy_type_t *)base;
    }
    name_bytes = (const char *)tinypy_string_view(name, &name_size);
    created = tinypy_type_new(vm, name_bytes, name_size, (const tinypy_type_t *const *)base_types, base_count, (tinypy_type_t *)metaclass_value, namespace_dict, out_error);
    if (base_types != NULL) {
        tinypy_internal_vm_deallocate(vm, base_types, base_count * sizeof(*base_types));
    }
    tinypy_value_t *return_value_1 = created != NULL ? tinypy_type_as_value(created) : NULL;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_constructor_type_init_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_constructor_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_constructor_argument_count(vm, args, 4U, 4U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *return_value_1 = tinypy_none_get(vm);
    return return_value_1;
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
    if ((class_type->flags & TINYPY_TYPE_FLAG_HEAP) == 0U || (class_type->flags & TINYPY_TYPE_FLAG_TYPE_SUBCLASS) != 0U || tinypy_type_is_subtype(class_type, &vm->types[TINYPY_VALUE_INSTANCE]) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "object.__new__ cannot create this type", out_error);
        return NULL;
    }
    tinypy_value_type_e layout_kind = class_type->layout_kind;
    if (__tinypy_constructor_has_immutable_builtin_layout(layout_kind) != 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "object.__new__ cannot create immutable builtin instances", out_error);
        return NULL;
    }
    if (layout_kind == TINYPY_VALUE_SET || layout_kind == TINYPY_VALUE_FROZENSET) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "object.__new__ cannot create set instances", out_error);
        return NULL;
    }
    if (__tinypy_constructor_has_mutable_builtin_layout(layout_kind) == 0 && __tinypy_constructor_object_has_excess_arguments(args, kwargs) != 0) {
        tinypy_value_t *type_init = tinypy_internal_type_lookup_key(vm, class_type, vm->special_init_key);
        tinypy_value_t *object_init = tinypy_internal_type_lookup_key(vm, &vm->types[TINYPY_VALUE_INSTANCE], vm->special_init_key);

        /* CPython 2.7 accepts these arguments when __init__ is overridden.
         * When both __new__ and __init__ are overridden it also emits a
         * DeprecationWarning, which tinypy does not expose. */
        if (type_init == object_init) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "object() takes no parameters", out_error);
            return NULL;
        }
    }
    tinypy_value_t *return_value_1;
    if (layout_kind == TINYPY_VALUE_NATIVE_INSTANCE) {
        return_value_1 = tinypy_native_instance_new(class_type);
    }
    else {
        return_value_1 = tinypy_instance_new(class_type);
        if (layout_kind == TINYPY_VALUE_DICT) {
            tinypy_internal_dict_initialize_empty(return_value_1);
        }
        else if (layout_kind == TINYPY_VALUE_SET) {
            tinypy_internal_set_initialize_empty(return_value_1);
        }
    }
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_constructor_object_init_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (TINYPY_TUPLE_SIZE(args) == 0U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "object.__init__ requires an instance", out_error);
        return NULL;
    }
    tinypy_value_t *self = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_type_e layout_kind = self->type->layout_kind;
    if (__tinypy_constructor_has_mutable_builtin_layout(layout_kind) != 0) {
        tinypy_value_t *constructor_args = __tinypy_constructor_tail_arguments(vm, args);
        tinypy_type_t *builtin_type = &vm->types[layout_kind];
        tinypy_value_t *initialized = builtin_type->create(builtin_type, constructor_args, kwargs, out_error);

        TINYPY_DECREF(constructor_args);
        if (initialized == NULL) {
            return NULL;
        }
        switch (layout_kind) {
        case TINYPY_VALUE_LIST:
            tinypy_internal_list_swap_contents(self, initialized);
            break;
        case TINYPY_VALUE_DICT:
            tinypy_internal_dict_swap_contents(self, initialized);
            break;
        case TINYPY_VALUE_SET:
            tinypy_internal_set_swap_contents(self, initialized);
            break;
        case TINYPY_VALUE_BYTEARRAY:
            if (tinypy_internal_bytearray_resize_allowed(self, TINYPY_SIZED_SIZE(initialized), out_error) == 0) {
                TINYPY_DECREF(initialized);
                return NULL;
            }
            tinypy_internal_bytearray_swap_contents(self, initialized);
            break;
        default:
            break;
        }
#if defined(TINYPY_CYCLE_DIAGNOSTICS)
        __tinypy_constructor_rebuild_container_diagnostics(self);
        __tinypy_constructor_rebuild_container_diagnostics(initialized);
#endif
        TINYPY_DECREF(initialized);
        tinypy_value_t *return_value = tinypy_none_get(vm);
        return return_value;
    }
    if (__tinypy_constructor_object_has_excess_arguments(args, kwargs) != 0) {
        tinypy_type_t *type = self->type;
        tinypy_value_t *type_new = tinypy_type_get_attr(type, "__new__", 7U);
        tinypy_value_t *object_new = tinypy_type_get_attr(&vm->types[TINYPY_VALUE_INSTANCE], "__new__", 7U);

        /* Symmetrically, CPython 2.7 accepts these arguments when __new__ is
         * overridden, with the same unexposed warning when both are custom. */
        if (type_new == object_new) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "object.__init__() takes no parameters", out_error);
            return NULL;
        }
    }
    tinypy_value_t *return_value_1 = tinypy_none_get(vm);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_constructor_object_getattribute_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_constructor_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_constructor_argument_count(vm, args, 2U, 2U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *name = TINYPY_TUPLE_GET(args, 1U);
    if (TINYPY_VALUE_KIND(name) != TINYPY_VALUE_STRING && TINYPY_VALUE_KIND(name) != TINYPY_VALUE_UNICODE) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "attribute name must be a string", out_error);
        return NULL;
    }
    tinypy_value_t *return_value_1 = tinypy_internal_object_get_base_attr_key(TINYPY_TUPLE_GET(args, 0U), name, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_constructor_object_setattr_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_constructor_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_constructor_argument_count(vm, args, 3U, 3U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *name = TINYPY_TUPLE_GET(args, 1U);
    if (TINYPY_VALUE_KIND(name) != TINYPY_VALUE_STRING && TINYPY_VALUE_KIND(name) != TINYPY_VALUE_UNICODE) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "attribute name must be a string", out_error);
        return NULL;
    }
    if (tinypy_internal_object_set_attr_key(TINYPY_TUPLE_GET(args, 0U), name, TINYPY_TUPLE_GET(args, 2U), out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *return_value_1 = tinypy_none_get(vm);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_constructor_object_delattr_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_constructor_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_constructor_argument_count(vm, args, 2U, 2U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *name = TINYPY_TUPLE_GET(args, 1U);
    if (TINYPY_VALUE_KIND(name) != TINYPY_VALUE_STRING && TINYPY_VALUE_KIND(name) != TINYPY_VALUE_UNICODE) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "attribute name must be a string", out_error);
        return NULL;
    }
    if (tinypy_internal_object_delete_attr_key(TINYPY_TUPLE_GET(args, 0U), name, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *return_value_1 = tinypy_none_get(vm);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_constructor_object_hash_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_constructor_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_constructor_argument_count(vm, args, 1U, 1U, out_error) == 0) {
        return NULL;
    }
    tinypy_hash_t hash = (tinypy_hash_t)((uintptr_t)TINYPY_TUPLE_GET(args, 0U) >> 4U);
    if (hash == (tinypy_hash_t)-1) {
        hash = (tinypy_hash_t)-2;
    }
    tinypy_value_t *return_value_1 = tinypy_integer_from_i64(vm, (int64_t)hash);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_constructor_object_format_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_constructor_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_constructor_argument_count(vm, args, 2U, 2U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *spec = TINYPY_TUPLE_GET(args, 1U);
    if ((TINYPY_VALUE_KIND(spec) != TINYPY_VALUE_STRING && TINYPY_VALUE_KIND(spec) != TINYPY_VALUE_UNICODE) || (TINYPY_TEXT_BYTE_SIZE(spec) != 0U && (TINYPY_TEXT_BYTE_SIZE(spec) != 1U || TINYPY_TEXT_BYTES(spec)[0] != (uint8_t)'s'))) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "invalid format specification for object", out_error);
        return NULL;
    }
    tinypy_value_t *return_value_1 = TINYPY_VALUE_KIND(spec) == TINYPY_VALUE_UNICODE
                                         ? tinypy_internal_object_unicode(TINYPY_TUPLE_GET(args, 0U), out_error)
                                         : tinypy_object_str(TINYPY_TUPLE_GET(args, 0U), out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_constructor_object_subclasshook_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_constructor_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_constructor_argument_count(vm, args, 2U, 2U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *return_value_1 = &vm->not_implemented_object.base;
    TINYPY_INCREF(return_value_1);
    return return_value_1;
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
    }
    constructor_arguments = tinypy_internal_tuple_prepend_checked(vm, &self->type->base.base, new_arguments, out_error);
    if (constructor_arguments == NULL) {
        goto cleanup;
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

        if (dict_slot != NULL) {
            if (*dict_slot != NULL) {
                state = *dict_slot;
                TINYPY_INCREF(state);
            }
            else {
                state = tinypy_dict_new(vm);
            }
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
    }
    tinypy_value_t *reduce_items[] = {newobj, constructor_arguments, state, list_items, dict_items};

    result = tinypy_tuple_from_items(vm, reduce_items, 5U);

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
    tinypy_value_t *object_reduce = tinypy_type_get_attr(&vm->types[TINYPY_VALUE_INSTANCE], "__reduce__", 10U);

    if (class_reduce != NULL && class_reduce != object_reduce) {
        tinypy_value_t *reduce = tinypy_object_get_attr(self, "__reduce__", 10U, out_error);

        if (reduce == NULL) {
            return NULL;
        }
        tinypy_value_t *result = __tinypy_constructor_call_with_items(vm, reduce, NULL, 0U, out_error);
        TINYPY_DECREF(reduce);
        return result;
    }
    tinypy_value_t *return_value_1 = __tinypy_constructor_object_reduce_method(function, args, kwargs, user_data, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_copy_reg_newobj(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_constructor_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_constructor_argument_count(vm, args, 1U, SIZE_MAX, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *type_value = TINYPY_TUPLE_GET(args, 0U);
    if (TINYPY_VALUE_KIND(type_value) != TINYPY_VALUE_TYPE) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "__newobj__ requires a type", out_error);
        return NULL;
    }
    tinypy_value_t *constructor = tinypy_object_get_attr(type_value, "__new__", 7U, out_error);
    if (constructor == NULL) {
        return NULL;
    }
    tinypy_value_t *result = tinypy_call(constructor, args, NULL, out_error);
    TINYPY_DECREF(constructor);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_copy_reg_reconstructor(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *instance = NULL;

    (void)user_data;
    if (__tinypy_constructor_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_constructor_argument_count(vm, args, 3U, 3U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *type_value = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *base_value = TINYPY_TUPLE_GET(args, 1U);
    tinypy_value_t *state = TINYPY_TUPLE_GET(args, 2U);
    if (TINYPY_VALUE_KIND(type_value) != TINYPY_VALUE_TYPE || TINYPY_VALUE_KIND(base_value) != TINYPY_VALUE_TYPE) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "_reconstructor requires type arguments", out_error);
        return NULL;
    }
    tinypy_type_t *base = (tinypy_type_t *)base_value;
    tinypy_value_t *constructor = tinypy_internal_object_get_attr_key(base_value, vm->special_new_key, out_error);
    if (constructor == NULL) {
        return NULL;
    }
    if (base == &vm->types[TINYPY_VALUE_INSTANCE]) {
        tinypy_value_t *items[] = {type_value};

        instance = __tinypy_constructor_call_with_items(vm, constructor, items, 1U, out_error);
    }
    else {
        tinypy_value_t *items[] = {type_value, state};

        instance = __tinypy_constructor_call_with_items(vm, constructor, items, 2U, out_error);
    }
    TINYPY_DECREF(constructor);
    if (instance == NULL || base == &vm->types[TINYPY_VALUE_INSTANCE]) {
        return instance;
    }
    tinypy_value_t *initializer = tinypy_internal_type_lookup_key(vm, base, vm->special_init_key);
    tinypy_value_t *object_initializer = tinypy_internal_type_lookup_key(vm, &vm->types[TINYPY_VALUE_INSTANCE], vm->special_init_key);
    if (initializer != NULL && initializer != object_initializer) {
        tinypy_value_t *items[] = {instance, state};
        tinypy_value_t *initialized = __tinypy_constructor_call_with_items(vm, initializer, items, 2U, out_error);

        if (initialized == NULL) {
            TINYPY_DECREF(instance);
            return NULL;
        }
        TINYPY_DECREF(initialized);
    }
    return instance;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_copy_reg_reduce_ex(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_type_t *base = NULL;
    tinypy_value_t *base_state;
    tinypy_value_t *instance_state = NULL;
    tinypy_value_t *reconstructor = NULL;
    tinypy_value_t *constructor_args = NULL;
    tinypy_value_t *result = NULL;
    size_t index;

    (void)user_data;
    if (__tinypy_constructor_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_constructor_argument_count(vm, args, 2U, 2U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *self = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *protocol = TINYPY_TUPLE_GET(args, 1U);
    if ((TINYPY_VALUE_KIND(protocol) != TINYPY_VALUE_BOOL && TINYPY_VALUE_KIND(protocol) != TINYPY_VALUE_INTEGER) || TINYPY_INTEGER_VALUE(protocol) >= 2) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "_reduce_ex requires a protocol below 2", out_error);
        return NULL;
    }
    for (index = 0U; index < tinypy_type_mro_size(self->type); ++index) {
        tinypy_type_t *candidate = (tinypy_type_t *)tinypy_type_mro_at(self->type, index);

        if ((candidate->flags & TINYPY_TYPE_FLAG_HEAP) == 0U) {
            base = candidate;
            break;
        }
    }
    if (base == NULL) {
        base = &vm->types[TINYPY_VALUE_INSTANCE];
    }
    if (base == self->type && base != &vm->types[TINYPY_VALUE_INSTANCE]) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "can't pickle builtin objects", out_error);
        return NULL;
    }
    if (base == &vm->types[TINYPY_VALUE_INSTANCE]) {
        base_state = tinypy_none_get(vm);
    }
    else {
        tinypy_value_t *base_args = tinypy_tuple_from_items(vm, &self, 1U);

        base_state = tinypy_call(&base->base.base, base_args, NULL, out_error);
        TINYPY_DECREF(base_args);
        if (base_state == NULL) {
            return NULL;
        }
    }
    tinypy_value_t *constructor_items[] = {&self->type->base.base, &base->base.base, base_state};
    constructor_args = tinypy_tuple_from_items(vm, constructor_items, 3U);
    TINYPY_DECREF(base_state);

    if (tinypy_object_has_attr(self, "__getstate__", 12U) != 0) {
        tinypy_value_t *getstate = tinypy_object_get_attr(self, "__getstate__", 12U, out_error);

        if (getstate == NULL) {
            goto cleanup;
        }
        instance_state = __tinypy_constructor_call_with_items(vm, getstate, NULL, 0U, out_error);
        TINYPY_DECREF(getstate);
        if (instance_state == NULL) {
            goto cleanup;
        }
    }
    else {
        tinypy_value_t **dict_slot;

        if (self->type->slot_count != 0U) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "a class that defines __slots__ without defining __getstate__ cannot be pickled", out_error);
            goto cleanup;
        }
        dict_slot = tinypy_internal_object_dict_slot(self);
        if (dict_slot != NULL && *dict_slot != NULL) {
            instance_state = *dict_slot;
            TINYPY_INCREF(instance_state);
        }
        else {
            instance_state = tinypy_none_get(vm);
        }
    }
    reconstructor = __tinypy_constructor_copy_reg_function(vm, "_reconstructor", 14U, out_error);
    if (reconstructor == NULL) {
        goto cleanup;
    }
    int32_t has_state = tinypy_truth(instance_state, out_error);
    if (has_state < 0) {
        goto cleanup;
    }
    tinypy_value_t *result_items[] = {reconstructor, constructor_args, instance_state};
    result = tinypy_tuple_from_items(vm, result_items, has_state != 0 ? 3U : 2U);

cleanup:
    if (reconstructor != NULL) {
        TINYPY_DECREF(reconstructor);
    }
    if (instance_state != NULL) {
        TINYPY_DECREF(instance_state);
    }
    if (constructor_args != NULL) {
        TINYPY_DECREF(constructor_args);
    }
    return result;
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
    if (TINYPY_VALUE_KIND(type_value) != TINYPY_VALUE_TYPE || tinypy_type_is_subtype((tinypy_type_t *)type_value, &vm->types[TINYPY_VALUE_TUPLE]) == 0) {
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
static tinypy_value_t *__tinypy_constructor_set_new_common(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_bool_t frozen, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_type_e kind = frozen != 0 ? TINYPY_VALUE_FROZENSET : TINYPY_VALUE_SET;

    if (__tinypy_constructor_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_constructor_argument_count(vm, args, 1U, 2U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *type_value = TINYPY_TUPLE_GET(args, 0U);
    if (TINYPY_VALUE_KIND(type_value) != TINYPY_VALUE_TYPE || tinypy_type_is_subtype((tinypy_type_t *)type_value, &vm->types[kind]) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, frozen != 0 ? "frozenset.__new__ requires a frozenset subtype" : "set.__new__ requires a set subtype", out_error);
        return NULL;
    }
    tinypy_value_t *constructor_args;
    if (frozen == 0 || TINYPY_TUPLE_SIZE(args) == 1U) {
        constructor_args = tinypy_tuple_from_items(vm, NULL, 0U);
    }
    else {
        tinypy_value_t *const *items = tinypy_internal_tuple_items(args);
        constructor_args = tinypy_tuple_from_items(vm, &items[1], 1U);
    }
    tinypy_value_t *result = frozen != 0
                                 ? tinypy_internal_frozenset_create((tinypy_type_t *)type_value, constructor_args, NULL, out_error)
                                 : tinypy_internal_set_create((tinypy_type_t *)type_value, constructor_args, NULL, out_error);
    TINYPY_DECREF(constructor_args);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_constructor_set_new_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    (void)user_data;
    tinypy_value_t *return_value_1 = __tinypy_constructor_set_new_common(function, args, kwargs, TINYPY_FALSE, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_constructor_frozenset_new_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    (void)user_data;
    tinypy_value_t *return_value_1 = __tinypy_constructor_set_new_common(function, args, kwargs, TINYPY_TRUE, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_constructor_type_call_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_constructor_argument_count(vm, args, 1U, SIZE_MAX, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *class_value = TINYPY_TUPLE_GET(args, 0U);
    if (TINYPY_VALUE_KIND(class_value) != TINYPY_VALUE_TYPE) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "type.__call__ requires a type", out_error);
        return NULL;
    }
    tinypy_value_t *call_args = __tinypy_constructor_tail_arguments(vm, args);
    tinypy_value_t *result = tinypy_call(class_value, call_args, kwargs, out_error);

    TINYPY_DECREF(call_args);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_constructor_type_instancecheck_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_constructor_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_constructor_argument_count(vm, args, 2U, 2U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *class_value = TINYPY_TUPLE_GET(args, 0U);
    if (TINYPY_VALUE_KIND(class_value) != TINYPY_VALUE_TYPE) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "type.__instancecheck__ requires a type", out_error);
        return NULL;
    }
    tinypy_value_t *instance = TINYPY_TUPLE_GET(args, 1U);
    tinypy_value_t *result = tinypy_bool_from_i32(vm, tinypy_type_is_subtype(instance->type, (tinypy_type_t *)class_value));
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_constructor_type_subclasscheck_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_constructor_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_constructor_argument_count(vm, args, 2U, 2U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *class_value = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *subclass_value = TINYPY_TUPLE_GET(args, 1U);
    if (TINYPY_VALUE_KIND(class_value) != TINYPY_VALUE_TYPE || TINYPY_VALUE_KIND(subclass_value) != TINYPY_VALUE_TYPE) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "type.__subclasscheck__ requires two types", out_error);
        return NULL;
    }
    tinypy_value_t *result = tinypy_bool_from_i32(vm, tinypy_type_is_subtype((tinypy_type_t *)subclass_value, (tinypy_type_t *)class_value));
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_constructor_type_compare_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    if (__tinypy_constructor_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_constructor_argument_count(vm, args, 2U, 2U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *left = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *right = TINYPY_TUPLE_GET(args, 1U);
    if (TINYPY_VALUE_KIND(left) != TINYPY_VALUE_TYPE) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "type comparison requires a type", out_error);
        return NULL;
    }
    if (TINYPY_VALUE_KIND(right) != TINYPY_VALUE_TYPE) {
        tinypy_value_t *result = tinypy_not_implemented_get(vm);

        return result;
    }
    tinypy_value_t *result = tinypy_internal_compare_builtin_value(left, right, (tinypy_compare_operation_e)(intptr_t)user_data, out_error);

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
    tinypy_value_t *return_value_1 = tinypy_list_from_items(vm, tuple_items, tuple_size);
    return return_value_1;
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
    tinypy_value_t *return_value_1 = tinypy_internal_type_subclasses((tinypy_type_t *)class_value, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_constructor_add_method(tinypy_type_t *type, const char *name, size_t name_size, tinypy_native_function_callback_t callback, int32_t static_method) {
    tinypy_value_t *function = tinypy_native_function_new(type->vm, name, name_size, callback, NULL, NULL);
    tinypy_value_t *attribute = static_method != 0 ? tinypy_static_method_new(function) : function;

    tinypy_type_set_attr(type, name, name_size, attribute);
    if (attribute != function) {
        TINYPY_DECREF(attribute);
    }
    TINYPY_DECREF(function);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_constructor_add_type_comparison(tinypy_vm_t *vm, const char *name, size_t name_size, tinypy_compare_operation_e operation) {
    tinypy_value_t *function = tinypy_native_function_new(vm, name, name_size, __tinypy_constructor_type_compare_method, (void *)(intptr_t)operation, NULL);

    tinypy_type_set_attr(&vm->types[TINYPY_VALUE_TYPE], name, name_size, function);
    TINYPY_DECREF(function);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_constructor_add_class_method(tinypy_type_t *type, const char *name, size_t name_size, tinypy_native_function_callback_t callback) {
    tinypy_value_t *function = tinypy_native_function_new(type->vm, name, name_size, callback, NULL, NULL);
    tinypy_value_t *attribute = tinypy_class_method_new(function);

    tinypy_type_set_attr(type, name, name_size, attribute);
    TINYPY_DECREF(attribute);
    TINYPY_DECREF(function);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_constructor_immutable_new_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_type_e kind = (tinypy_value_type_e)(intptr_t)user_data;

    if (TINYPY_TUPLE_SIZE(args) == 0U || TINYPY_VALUE_KIND(TINYPY_TUPLE_GET(args, 0U)) != TINYPY_VALUE_TYPE) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "immutable __new__ requires a type", out_error);
        return NULL;
    }
    tinypy_type_t *type = (tinypy_type_t *)TINYPY_TUPLE_GET(args, 0U);
    if (type->layout_kind != kind || tinypy_type_is_subtype(type, &vm->types[kind]) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "immutable __new__ received an incompatible type", out_error);
        return NULL;
    }
    tinypy_value_t *constructor_args = __tinypy_constructor_tail_arguments(vm, args);
    tinypy_value_t *result = vm->types[kind].create(type, constructor_args, kwargs, out_error);
    TINYPY_DECREF(constructor_args);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_constructor_add_immutable_new(tinypy_vm_t *vm, tinypy_value_type_e kind) {
    tinypy_type_t *type = &vm->types[kind];
    tinypy_value_t *function = tinypy_native_function_new(vm, "__new__", 7U, __tinypy_constructor_immutable_new_method, (void *)(intptr_t)kind, NULL);
    tinypy_value_t *attribute = tinypy_static_method_new(function);

    tinypy_type_set_attr(type, "__new__", 7U, attribute);
    TINYPY_DECREF(attribute);
    TINYPY_DECREF(function);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_constructor_builtin_new_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_type_t *base_type = (tinypy_type_t *)user_data;

    if (TINYPY_TUPLE_SIZE(args) == 0U || TINYPY_VALUE_KIND(TINYPY_TUPLE_GET(args, 0U)) != TINYPY_VALUE_TYPE) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "builtin __new__ requires a type", out_error);
        return NULL;
    }
    tinypy_type_t *type = (tinypy_type_t *)TINYPY_TUPLE_GET(args, 0U);
    if (tinypy_type_is_subtype(type, base_type) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "builtin __new__ received an incompatible type", out_error);
        return NULL;
    }
    tinypy_value_type_e kind = base_type->layout_kind;
    if (kind == TINYPY_VALUE_LIST || kind == TINYPY_VALUE_DICT || kind == TINYPY_VALUE_BYTEARRAY) {
        tinypy_value_t *result = tinypy_internal_object_allocate(vm, type, type->basic_size);

        if (kind == TINYPY_VALUE_DICT) {
            tinypy_internal_dict_initialize_empty(result);
        }
        return result;
    }
    if (kind == TINYPY_VALUE_PROPERTY || kind == TINYPY_VALUE_CLASS_METHOD || kind == TINYPY_VALUE_STATIC_METHOD) {
        tinypy_value_t *result = tinypy_internal_object_allocate(vm, type, type->basic_size);

        return result;
    }
    tinypy_value_t *constructor_args = __tinypy_constructor_tail_arguments(vm, args);
    tinypy_value_t *result = base_type->create(type, constructor_args, kwargs, out_error);

    TINYPY_DECREF(constructor_args);
    return result;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_constructor_add_builtin_new(tinypy_type_t *type) {
    tinypy_value_t *function = tinypy_native_function_new(type->vm, "__new__", 7U, __tinypy_constructor_builtin_new_method, type, NULL);
    tinypy_value_t *descriptor = tinypy_static_method_new(function);

    tinypy_type_set_attr(type, "__new__", 7U, descriptor);
    TINYPY_DECREF(descriptor);
    TINYPY_DECREF(function);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_constructor_builtin_reduce_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_type_t *declaring_type = (tinypy_type_t *)user_data;
    tinypy_value_t *constructor_args = NULL;
    tinypy_value_t *state = NULL;
    tinypy_value_t *owned[4] = {NULL, NULL, NULL, NULL};
    size_t owned_count = 0U;
    size_t result_count = 2U;

    if (__tinypy_constructor_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_constructor_argument_count(vm, args, 1U, 1U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *self = TINYPY_TUPLE_GET(args, 0U);
    if (tinypy_type_is_subtype(self->type, declaring_type) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "__reduce__ received an incompatible object", out_error);
        return NULL;
    }
    switch (declaring_type->layout_kind) {
    case TINYPY_VALUE_SLICE: {
        tinypy_slice_object_t *slice = TINYPY_SLICE_OBJECT(self);
        tinypy_value_t *items[] = {slice->start, slice->stop, slice->step};

        constructor_args = tinypy_tuple_from_items(vm, items, 3U);
        break;
    }
    case TINYPY_VALUE_XRANGE: {
        tinypy_xrange_object_t *range = TINYPY_XRANGE_OBJECT(self);

        owned[0] = tinypy_integer_from_i64(vm, range->start);
        owned[1] = tinypy_integer_from_i64(vm, tinypy_internal_xrange_stop_value(range));
        owned[2] = tinypy_integer_from_i64(vm, range->step);
        owned_count = 3U;
        constructor_args = tinypy_tuple_from_items(vm, owned, owned_count);
        break;
    }
    case TINYPY_VALUE_SET:
    case TINYPY_VALUE_FROZENSET: {
        tinypy_value_t *list = tinypy_list_from_items(vm, NULL, 0U);
        tinypy_value_t *iterator = tinypy_internal_set_iter(self, out_error);
        tinypy_error_t *iteration_error = NULL;

        if (iterator == NULL || tinypy_internal_list_reserve_checked(vm, list, TINYPY_DICT_SIZE(TINYPY_SET_OBJECT(self)->dict), out_error) == 0) {
            if (iterator != NULL) {
                TINYPY_DECREF(iterator);
            }
            TINYPY_DECREF(list);
            return NULL;
        }
        for (;;) {
            tinypy_value_t *item = tinypy_next(iterator, &iteration_error);

            if (item == NULL) {
                break;
            }
            if (tinypy_internal_list_append_checked(list, item, out_error) == 0) {
                TINYPY_DECREF(item);
                TINYPY_DECREF(iterator);
                TINYPY_DECREF(list);
                return NULL;
            }
            TINYPY_DECREF(item);
        }
        TINYPY_DECREF(iterator);
        if (iteration_error != NULL) {
            TINYPY_DECREF(list);
            if (out_error != NULL) {
                *out_error = iteration_error;
            }
            else {
                tinypy_error_release(iteration_error);
            }
            return NULL;
        }
        constructor_args = tinypy_tuple_from_items(vm, &list, 1U);
        TINYPY_DECREF(list);
        state = tinypy_none_get(vm);
        result_count = 3U;
        break;
    }
    case TINYPY_VALUE_BYTEARRAY: {
        tinypy_value_t *bytes = tinypy_string_from_bytes(vm, TINYPY_BYTEARRAY_OBJECT(self)->bytes, TINYPY_SIZED_SIZE(self));
        tinypy_value_t *decode = tinypy_object_get_attr(bytes, "decode", 6U, out_error);
        tinypy_value_t *encoding = tinypy_string_from_bytes(vm, "latin-1", 7U);
        tinypy_value_t *unicode = NULL;

        if (decode != NULL) {
            tinypy_value_t *decode_args = tinypy_tuple_from_items(vm, &encoding, 1U);

            unicode = tinypy_call(decode, decode_args, NULL, out_error);
            TINYPY_DECREF(decode_args);
            TINYPY_DECREF(decode);
        }
        TINYPY_DECREF(bytes);
        if (unicode == NULL) {
            TINYPY_DECREF(encoding);
            return NULL;
        }
        tinypy_value_t *items[] = {unicode, encoding};
        constructor_args = tinypy_tuple_from_items(vm, items, 2U);
        TINYPY_DECREF(unicode);
        TINYPY_DECREF(encoding);
        state = tinypy_none_get(vm);
        result_count = 3U;
        break;
    }
    default:
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "builtin type has no native reduction", out_error);
        return NULL;
    }
    tinypy_value_t *result_items[] = {&self->type->base.base, constructor_args, state};
    tinypy_value_t *result = tinypy_tuple_from_items(vm, result_items, result_count);

    if (state != NULL) {
        TINYPY_DECREF(state);
    }
    TINYPY_DECREF(constructor_args);
    while (owned_count != 0U) {
        owned_count -= 1U;
        TINYPY_DECREF(owned[owned_count]);
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_constructor_add_builtin_reduce(tinypy_type_t *type) {
    tinypy_value_t *function = tinypy_native_function_new(type->vm, "__reduce__", 10U, __tinypy_constructor_builtin_reduce_method, type, NULL);

    tinypy_type_set_attr(type, "__reduce__", 10U, function);
    TINYPY_DECREF(function);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_copy_reg_add_function(tinypy_vm_t *vm, tinypy_value_t *module, const char *name, size_t name_size, tinypy_native_function_callback_t callback) {
    tinypy_value_t *function = tinypy_native_function_new(vm, name, name_size, callback, NULL, NULL);

    tinypy_module_add_value(module, name, name_size, function);
    TINYPY_DECREF(function);
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_initialize_copy_reg_module(tinypy_vm_t *vm) {
    tinypy_value_t *module = tinypy_module_new(vm, "copy_reg", 8U);
    tinypy_value_t *name = tinypy_string_from_bytes(vm, "copy_reg", 8U);

    tinypy_module_add_value(module, "__name__", 8U, name);
    TINYPY_DECREF(name);
    __tinypy_copy_reg_add_function(vm, module, "__newobj__", 10U, __tinypy_copy_reg_newobj);
    __tinypy_copy_reg_add_function(vm, module, "_reconstructor", 14U, __tinypy_copy_reg_reconstructor);
    __tinypy_copy_reg_add_function(vm, module, "_reduce_ex", 10U, __tinypy_copy_reg_reduce_ex);
    tinypy_internal_register_module(vm, "copy_reg", 8U, module);
    TINYPY_DECREF(module);
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_initialize_constructor_types(tinypy_vm_t *vm) {
    __tinypy_constructor_add_method(&vm->types[TINYPY_VALUE_TYPE], "__new__", 7U, __tinypy_constructor_type_new_method, INT32_C(1));
    __tinypy_constructor_add_method(&vm->types[TINYPY_VALUE_TYPE], "__init__", 8U, __tinypy_constructor_type_init_method, INT32_C(0));
    __tinypy_constructor_add_method(&vm->types[TINYPY_VALUE_TYPE], "__call__", 8U, __tinypy_constructor_type_call_method, INT32_C(0));
    __tinypy_constructor_add_method(&vm->types[TINYPY_VALUE_TYPE], "__instancecheck__", 17U, __tinypy_constructor_type_instancecheck_method, INT32_C(0));
    __tinypy_constructor_add_method(&vm->types[TINYPY_VALUE_TYPE], "__subclasscheck__", 17U, __tinypy_constructor_type_subclasscheck_method, INT32_C(0));
    __tinypy_constructor_add_type_comparison(vm, "__lt__", 6U, TINYPY_COMPARE_LESS);
    __tinypy_constructor_add_type_comparison(vm, "__le__", 6U, TINYPY_COMPARE_LESS_EQUAL);
    __tinypy_constructor_add_type_comparison(vm, "__eq__", 6U, TINYPY_COMPARE_EQUAL);
    __tinypy_constructor_add_type_comparison(vm, "__ne__", 6U, TINYPY_COMPARE_NOT_EQUAL);
    __tinypy_constructor_add_type_comparison(vm, "__gt__", 6U, TINYPY_COMPARE_GREATER);
    __tinypy_constructor_add_type_comparison(vm, "__ge__", 6U, TINYPY_COMPARE_GREATER_EQUAL);
    __tinypy_constructor_add_method(&vm->types[TINYPY_VALUE_TYPE], "mro", 3U, __tinypy_constructor_type_mro_method, INT32_C(0));
    __tinypy_constructor_add_method(&vm->types[TINYPY_VALUE_TYPE], "__subclasses__", 14U, __tinypy_constructor_type_subclasses_method, INT32_C(0));
    __tinypy_constructor_add_method(&vm->types[TINYPY_VALUE_INSTANCE], "__new__", 7U, __tinypy_constructor_object_new_method, INT32_C(1));
    __tinypy_constructor_add_method(&vm->types[TINYPY_VALUE_INSTANCE], "__init__", 8U, __tinypy_constructor_object_init_method, INT32_C(0));
    __tinypy_constructor_add_method(&vm->types[TINYPY_VALUE_INSTANCE], "__getattribute__", 16U, __tinypy_constructor_object_getattribute_method, INT32_C(0));
    __tinypy_constructor_add_method(&vm->types[TINYPY_VALUE_INSTANCE], "__setattr__", 11U, __tinypy_constructor_object_setattr_method, INT32_C(0));
    __tinypy_constructor_add_method(&vm->types[TINYPY_VALUE_INSTANCE], "__delattr__", 11U, __tinypy_constructor_object_delattr_method, INT32_C(0));
    __tinypy_constructor_add_method(&vm->types[TINYPY_VALUE_INSTANCE], "__hash__", 8U, __tinypy_constructor_object_hash_method, INT32_C(0));
    __tinypy_constructor_add_method(&vm->types[TINYPY_VALUE_INSTANCE], "__format__", 10U, __tinypy_constructor_object_format_method, INT32_C(0));
    __tinypy_constructor_add_class_method(&vm->types[TINYPY_VALUE_INSTANCE], "__subclasshook__", 16U, __tinypy_constructor_object_subclasshook_method);
    __tinypy_constructor_add_method(&vm->types[TINYPY_VALUE_INSTANCE], "__reduce__", 10U, __tinypy_constructor_object_reduce_method, INT32_C(0));
    __tinypy_constructor_add_method(&vm->types[TINYPY_VALUE_INSTANCE], "__reduce_ex__", 13U, __tinypy_constructor_object_reduce_ex_method, INT32_C(0));
    __tinypy_constructor_add_method(&vm->types[TINYPY_VALUE_LIST], "__init__", 8U, __tinypy_constructor_object_init_method, INT32_C(0));
    __tinypy_constructor_add_method(&vm->types[TINYPY_VALUE_DICT], "__init__", 8U, __tinypy_constructor_object_init_method, INT32_C(0));
    __tinypy_constructor_add_method(&vm->types[TINYPY_VALUE_SET], "__init__", 8U, __tinypy_constructor_object_init_method, INT32_C(0));
    __tinypy_constructor_add_method(&vm->types[TINYPY_VALUE_BYTEARRAY], "__init__", 8U, __tinypy_constructor_object_init_method, INT32_C(0));
    __tinypy_constructor_add_method(&vm->types[TINYPY_VALUE_TUPLE], "__new__", 7U, __tinypy_constructor_tuple_new_method, INT32_C(1));
    __tinypy_constructor_add_method(&vm->types[TINYPY_VALUE_SET], "__new__", 7U, __tinypy_constructor_set_new_method, INT32_C(1));
    __tinypy_constructor_add_method(&vm->types[TINYPY_VALUE_FROZENSET], "__new__", 7U, __tinypy_constructor_frozenset_new_method, INT32_C(1));
    tinypy_internal_constructor_add_builtin_new(&vm->types[TINYPY_VALUE_LIST]);
    tinypy_internal_constructor_add_builtin_new(&vm->types[TINYPY_VALUE_DICT]);
    tinypy_internal_constructor_add_builtin_new(&vm->types[TINYPY_VALUE_BYTEARRAY]);
    tinypy_internal_constructor_add_builtin_new(&vm->types[TINYPY_VALUE_PROPERTY]);
    tinypy_internal_constructor_add_builtin_new(&vm->types[TINYPY_VALUE_CLASS_METHOD]);
    tinypy_internal_constructor_add_builtin_new(&vm->types[TINYPY_VALUE_STATIC_METHOD]);
    tinypy_internal_constructor_add_builtin_new(&vm->types[TINYPY_VALUE_SLICE]);
    tinypy_internal_constructor_add_builtin_new(&vm->types[TINYPY_VALUE_XRANGE]);
    tinypy_internal_constructor_add_builtin_new(&vm->types[TINYPY_VALUE_BUFFER]);
    __tinypy_constructor_add_builtin_reduce(&vm->types[TINYPY_VALUE_SLICE]);
    __tinypy_constructor_add_builtin_reduce(&vm->types[TINYPY_VALUE_XRANGE]);
    __tinypy_constructor_add_builtin_reduce(&vm->types[TINYPY_VALUE_SET]);
    __tinypy_constructor_add_builtin_reduce(&vm->types[TINYPY_VALUE_FROZENSET]);
    __tinypy_constructor_add_builtin_reduce(&vm->types[TINYPY_VALUE_BYTEARRAY]);
    __tinypy_constructor_add_immutable_new(vm, TINYPY_VALUE_BOOL);
    __tinypy_constructor_add_immutable_new(vm, TINYPY_VALUE_INTEGER);
    __tinypy_constructor_add_immutable_new(vm, TINYPY_VALUE_LONG);
    __tinypy_constructor_add_immutable_new(vm, TINYPY_VALUE_FLOAT);
    __tinypy_constructor_add_immutable_new(vm, TINYPY_VALUE_COMPLEX);
    __tinypy_constructor_add_immutable_new(vm, TINYPY_VALUE_STRING);
    __tinypy_constructor_add_immutable_new(vm, TINYPY_VALUE_UNICODE);
    __tinypy_constructor_add_immutable_new(vm, TINYPY_VALUE_ENUMERATE);
    __tinypy_constructor_add_immutable_new(vm, TINYPY_VALUE_REVERSED);
}
