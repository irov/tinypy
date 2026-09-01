#include "internal.h"

#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct tinypy_string_builder_t {
    tinypy_vm_t *vm;
    uint8_t *bytes;
    size_t size;
    size_t capacity;
    tinypy_value_t *exact_result;
    tinypy_bool_t failed;
    tinypy_bool_t memory_failed;
} tinypy_string_builder_t;

//////////////////////////////////////////////////////////////////////////
static void __tinypy_string_builder_reserve(tinypy_string_builder_t *builder, size_t extra) {
    size_t required;
    size_t capacity;

    if (builder->failed != 0 || extra > SIZE_MAX - builder->size) {
        builder->failed = TINYPY_TRUE;
        return;
    }
    required = builder->size + extra;
    if (required <= builder->capacity) {
        return;
    }
    if (builder->exact_result != NULL) {
        builder->failed = TINYPY_TRUE;
        return;
    }
    capacity = builder->capacity != 0U ? builder->capacity : 64U;
    while (capacity < required) {
        if (capacity > SIZE_MAX / 2U) {
            capacity = required;
            break;
        }
        capacity *= 2U;
    }
    uint8_t *resized;
    if (builder->bytes == NULL) {
        resized = (uint8_t *)tinypy_internal_vm_allocate_checked(builder->vm, capacity, NULL);
    }
    else {
        resized = (uint8_t *)tinypy_internal_vm_reallocate_checked(builder->vm, builder->bytes, builder->capacity, capacity, NULL);
    }
    if (resized == NULL) {
        builder->failed = TINYPY_TRUE;
        builder->memory_failed = TINYPY_TRUE;
        return;
    }
    builder->bytes = resized;
    builder->capacity = capacity;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_string_builder_allocate_exact(tinypy_string_builder_t *builder, tinypy_vm_t *vm, tinypy_bool_t unicode, size_t byte_size, size_t code_point_count, tinypy_error_t **out_error) {
    builder->vm = vm;
    builder->capacity = byte_size;
    builder->exact_result = tinypy_internal_text_allocate_uninitialized_checked(vm, unicode != 0 ? TINYPY_VALUE_UNICODE : TINYPY_VALUE_STRING, byte_size, code_point_count, &builder->bytes, out_error);
    if (builder->exact_result == NULL) {
        builder->bytes = NULL;
        builder->capacity = 0U;
        builder->failed = TINYPY_TRUE;
        builder->memory_failed = TINYPY_TRUE;
        return TINYPY_FALSE;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_string_builder_append(tinypy_string_builder_t *builder, const void *bytes, size_t size) {
    if (size == 0U) {
        return;
    }
    __tinypy_string_builder_reserve(builder, size);
    if (builder->failed != 0) {
        return;
    }
    (void)memcpy(builder->bytes + builder->size, bytes, size);
    builder->size += size;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_string_builder_repeat(tinypy_string_builder_t *builder, const uint8_t *bytes, size_t size, size_t count) {
    if (size == 0U || count == 0U) {
        return;
    }
    if (size == 1U) {
        __tinypy_string_builder_reserve(builder, count);
        if (builder->failed != 0) {
            return;
        }
        (void)memset(builder->bytes + builder->size, bytes[0], count);
        builder->size += count;
        return;
    }
    if (count > SIZE_MAX / size) {
        builder->failed = TINYPY_TRUE;
        return;
    }
    __tinypy_string_builder_reserve(builder, size * count);
    if (builder->failed != 0) {
        return;
    }
    size_t total = size * count;
    size_t begin = builder->size;
    size_t copied = size;

    (void)memcpy(builder->bytes + begin, bytes, size);
    while (copied < total) {
        size_t chunk = copied < total - copied ? copied : total - copied;

        (void)memcpy(builder->bytes + begin + copied, builder->bytes + begin, chunk);
        copied += chunk;
    }
    builder->size += total;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_string_builder_character(tinypy_string_builder_t *builder, uint8_t character) {
    __tinypy_string_builder_append(builder, &character, 1U);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_string_builder_code_point(tinypy_string_builder_t *builder, uint32_t code_point) {
    uint8_t bytes[4];
    size_t size = tinypy_internal_utf8_encode(code_point, bytes);

    __tinypy_string_builder_append(builder, bytes, size);
}
//////////////////////////////////////////////////////////////////////////
static size_t __tinypy_string_builder_code_point_count(const tinypy_string_builder_t *builder) {
    size_t offset = 0U;
    size_t count = 0U;

    while (offset < builder->size) {
        uint8_t first = builder->bytes[offset];

        offset += first < 0x80U ? 1U : (first < 0xe0U ? 2U : (first < 0xf0U ? 3U : 4U));
        count += 1U;
    }
    return count;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_string_builder_finish(tinypy_string_builder_t *builder, tinypy_bool_t unicode, tinypy_error_t **out_error) {
    tinypy_value_t *result = NULL;

    if (builder->failed == 0 && builder->exact_result != NULL && builder->size == builder->capacity) {
        result = builder->exact_result;
        builder->exact_result = NULL;
    }
    else if (builder->failed == 0 && builder->exact_result == NULL) {
        if (builder->size == 0U) {
            result = unicode != 0 ? tinypy_unicode_from_utf8(builder->vm, "", 0U) : tinypy_string_from_bytes(builder->vm, NULL, 0U);
        }
        else {
            uint8_t *output;
            size_t code_point_count = unicode != 0 ? __tinypy_string_builder_code_point_count(builder) : 0U;

            result = tinypy_internal_text_allocate_uninitialized_checked(builder->vm, unicode != 0 ? TINYPY_VALUE_UNICODE : TINYPY_VALUE_STRING, builder->size, code_point_count, &output, out_error);
            if (result != NULL) {
                (void)memcpy(output, builder->bytes, builder->size);
            }
        }
    }
    else if (builder->memory_failed != 0) {
        tinypy_internal_make_vm_error(builder->vm, TINYPY_ERROR_MEMORY, "memory allocation failed", out_error);
    }
    else {
        tinypy_internal_make_vm_error(builder->vm, TINYPY_ERROR_OVERFLOW, "resulting string is too large", out_error);
    }

    if (builder->exact_result != NULL) {
        TINYPY_DECREF(builder->exact_result);
    }
    else if (builder->bytes != NULL && result != NULL && builder->bytes != TINYPY_TEXT_BYTES(result)) {
        tinypy_internal_vm_deallocate(builder->vm, builder->bytes, builder->capacity);
    }
    else if (builder->bytes != NULL && result == NULL) {
        tinypy_internal_vm_deallocate(builder->vm, builder->bytes, builder->capacity);
    }
    builder->bytes = NULL;
    builder->size = 0U;
    builder->capacity = 0U;
    builder->exact_result = NULL;
    builder->failed = TINYPY_FALSE;
    builder->memory_failed = TINYPY_FALSE;
    return result;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_string_builder_discard(tinypy_string_builder_t *builder) {
    if (builder->exact_result != NULL) {
        TINYPY_DECREF(builder->exact_result);
    }
    else if (builder->bytes != NULL) {
        tinypy_internal_vm_deallocate(builder->vm, builder->bytes, builder->capacity);
    }
    builder->bytes = NULL;
    builder->size = 0U;
    builder->capacity = 0U;
    builder->exact_result = NULL;
    builder->failed = TINYPY_FALSE;
    builder->memory_failed = TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_string_method_arguments(tinypy_vm_t *vm, tinypy_value_t *args, tinypy_value_t *kwargs, size_t minimum, size_t maximum, int32_t keywords, tinypy_error_t **out_error) {
    size_t count = TINYPY_TUPLE_SIZE(args);

    if (count < minimum || count > maximum) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "string method received the wrong number of arguments", out_error);
        return TINYPY_FALSE;
    }
    if (keywords == 0 && kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "string method does not accept keyword arguments", out_error);
        return TINYPY_FALSE;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_string_integer(tinypy_vm_t *vm, tinypy_value_t *value, int64_t *out_value, tinypy_error_t **out_error) {
    (void)vm;
    tinypy_bool_t return_value_1 = tinypy_internal_index_as_i64(value, out_value, TINYPY_FALSE, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_percent_append_integer(tinypy_vm_t *vm, tinypy_string_builder_t *builder, tinypy_value_t *value, uint8_t conversion, int32_t alternate, int32_t plus, int32_t space, int64_t precision, tinypy_bool_t new_format, size_t *out_prefix_size, tinypy_error_t **out_error);
static tinypy_bool_t __tinypy_percent_append_float(tinypy_vm_t *vm, tinypy_string_builder_t *builder, tinypy_value_t *value, uint8_t conversion, int32_t alternate, int32_t plus, int32_t space, int64_t precision, tinypy_bool_t long_overflow_type_error, size_t *out_prefix_size, tinypy_error_t **out_error);
static tinypy_bool_t __tinypy_percent_append_double(tinypy_vm_t *vm, tinypy_string_builder_t *builder, double number, uint8_t conversion, int32_t alternate, int32_t plus, int32_t space, int64_t precision, size_t *out_prefix_size, tinypy_error_t **out_error);
static void __tinypy_string_format_group_digits(tinypy_string_builder_t *field, size_t prefix_size);
static size_t __tinypy_string_utf8_width(uint8_t first);
static size_t __tinypy_string_character_count(const tinypy_value_t *value);
static size_t __tinypy_string_byte_offset(const tinypy_value_t *value, size_t character_index);
static void __tinypy_utf8_append(tinypy_string_builder_t *builder, uint32_t code_point);

static tinypy_bool_t __tinypy_string_format_complex_component(tinypy_vm_t *vm, tinypy_string_builder_t *field, double number, uint8_t conversion, int32_t plus, int32_t space, int64_t precision, tinypy_bool_t grouping, tinypy_error_t **out_error) {
    size_t prefix_size = 0U;

    if (__tinypy_percent_append_double(vm, field, number, conversion, 0, plus, space, precision, &prefix_size, out_error) == 0) {
        return TINYPY_FALSE;
    }
    if (grouping != 0) {
        __tinypy_string_format_group_digits(field, prefix_size);
    }
    return field->failed == 0 ? TINYPY_TRUE : TINYPY_FALSE;
}

static void __tinypy_string_format_add_dot_zero(tinypy_string_builder_t *field, size_t prefix_size, int64_t precision) {
    size_t index;
    size_t exponent = SIZE_MAX;

    if (prefix_size >= field->size || field->bytes[prefix_size] < (uint8_t)'0' || field->bytes[prefix_size] > (uint8_t)'9') {
        return;
    }
    for (index = prefix_size; index < field->size; ++index) {
        if (field->bytes[index] == (uint8_t)'.') {
            return;
        }
        if (field->bytes[index] == (uint8_t)'e' || field->bytes[index] == (uint8_t)'E') {
            exponent = index;
            break;
        }
    }
    if (exponent != SIZE_MAX) {
        return;
    }
    if (precision >= 0 && precision <= 1) {
        __tinypy_string_builder_append(field, "e+00", 4U);
    }
    else {
        __tinypy_string_builder_append(field, ".0", 2U);
    }
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_string_format_group_digits(tinypy_string_builder_t *field, size_t prefix_size) {
    size_t integer_end = prefix_size;
    size_t digit_count;
    size_t separator_count;
    size_t source;
    size_t destination;
    size_t group = 0U;

    while (integer_end < field->size && field->bytes[integer_end] >= (uint8_t)'0' && field->bytes[integer_end] <= (uint8_t)'9') {
        integer_end += 1U;
    }
    digit_count = integer_end - prefix_size;
    if (digit_count <= 3U) {
        return;
    }
    separator_count = (digit_count - 1U) / 3U;
    __tinypy_string_builder_reserve(field, separator_count);
    if (field->failed != 0) {
        return;
    }
    (void)memmove(field->bytes + integer_end + separator_count, field->bytes + integer_end, field->size - integer_end);
    source = integer_end;
    destination = integer_end + separator_count;
    while (source > prefix_size) {
        field->bytes[--destination] = field->bytes[--source];
        group += 1U;
        if (group == 3U && source > prefix_size) {
            field->bytes[--destination] = (uint8_t)',';
            group = 0U;
        }
    }
    field->size += separator_count;
}

//////////////////////////////////////////////////////////////////////////
static void __tinypy_string_format_group_zero_pad(tinypy_string_builder_t *field, size_t prefix_size, size_t width) {
    size_t integer_end = prefix_size;
    size_t digit_count;
    size_t suffix_size;
    size_t padded_digits;
    size_t padding;
    size_t fixed_size;
    size_t target;

    while (integer_end < field->size && field->bytes[integer_end] >= (uint8_t)'0' && field->bytes[integer_end] <= (uint8_t)'9') {
        integer_end += 1U;
    }
    digit_count = integer_end - prefix_size;
    if (digit_count == 0U) {
        return;
    }
    suffix_size = field->size - integer_end;
    fixed_size = prefix_size + suffix_size;
    if (width <= fixed_size) {
        return;
    }
    target = width - fixed_size;
    padded_digits = digit_count;
    size_t existing_separators = (padded_digits - 1U) / 3U;
    if (existing_separators < target && padded_digits < target - existing_separators) {
        size_t lower = padded_digits + 1U;
        size_t upper = target;

        while (lower < upper) {
            size_t middle = lower + (upper - lower) / 2U;
            size_t separators = (middle - 1U) / 3U;

            if (separators >= target || middle >= target - separators) {
                upper = middle;
            }
            else {
                lower = middle + 1U;
            }
        }
        padded_digits = lower;
    }
    padding = padded_digits - digit_count;
    if (padding == 0U) {
        return;
    }
    __tinypy_string_builder_reserve(field, padding);
    if (field->failed != 0) {
        return;
    }
    (void)memmove(field->bytes + prefix_size + padding, field->bytes + prefix_size, field->size - prefix_size);
    (void)memset(field->bytes + prefix_size, '0', padding);
    field->size += padding;
}

//////////////////////////////////////////////////////////////////////////
static void __tinypy_string_format_padding(tinypy_string_builder_t *output, const tinypy_string_builder_t *field, size_t field_width, size_t prefix_size, size_t width, const uint8_t *fill, size_t fill_size, uint8_t align) {
    size_t padding = width > field_width ? width - field_width : 0U;
    size_t left = 0U;
    size_t right = 0U;

    if (field->failed != 0) {
        output->failed = TINYPY_TRUE;
        return;
    }

    if (align == (uint8_t)'<') {
        right = padding;
    }
    else if (align == (uint8_t)'^') {
        left = padding / 2U;
        right = padding - left;
    }
    else if (align == (uint8_t)'=') {
        __tinypy_string_builder_append(output, field->bytes, prefix_size);
        __tinypy_string_builder_repeat(output, fill, fill_size, padding);
        __tinypy_string_builder_append(output, field->bytes + prefix_size, field->size - prefix_size);
        return;
    }
    else {
        left = padding;
    }
    __tinypy_string_builder_repeat(output, fill, fill_size, left);
    __tinypy_string_builder_append(output, field->bytes, field->size);
    __tinypy_string_builder_repeat(output, fill, fill_size, right);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_string_format_require_ascii(tinypy_vm_t *vm, const tinypy_value_t *text, tinypy_error_t **out_error) {
    const uint8_t *bytes;
    size_t size;
    size_t index;

    if (TINYPY_VALUE_KIND(text) != TINYPY_VALUE_STRING) {
        return TINYPY_TRUE;
    }
    bytes = TINYPY_TEXT_BYTES(text);
    size = TINYPY_TEXT_BYTE_SIZE(text);
    for (index = 0U; index < size; ++index) {
        if (bytes[index] >= 0x80U) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_UNICODE_DECODE, "ascii decode error during string formatting", out_error);
            return TINYPY_FALSE;
        }
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_string_format_value(tinypy_vm_t *vm, tinypy_value_t *value, int32_t conversion, const uint8_t *spec, size_t spec_size, tinypy_bool_t spec_unicode, tinypy_bool_t allow_special, tinypy_bool_t *out_unicode, tinypy_error_t **out_error) {
    size_t offset = 0U;
    uint8_t fill[4] = {(uint8_t)' ', 0U, 0U, 0U};
    size_t fill_size = 1U;
    uint8_t align = 0U;
    tinypy_bool_t explicit_fill = TINYPY_FALSE;
    int32_t plus = 0;
    int32_t space = 0;
    tinypy_bool_t sign_specified = TINYPY_FALSE;
    int32_t alternate = 0;
    tinypy_bool_t grouping = TINYPY_FALSE;
    tinypy_bool_t zero_padding = TINYPY_FALSE;
    size_t width = 0U;
    int64_t precision = -1;
    uint8_t type = 0U;
    tinypy_bool_t numeric = TINYPY_FALSE;
    size_t prefix_size = 0U;
    tinypy_string_builder_t field;
    tinypy_string_builder_t output;
    size_t field_width;
    tinypy_bool_t float_default_type = TINYPY_FALSE;
    tinypy_value_type_e value_kind = TINYPY_VALUE_KIND(value);
    tinypy_bool_t direct_builtin = (value->type->flags & TINYPY_TYPE_FLAG_HEAP) == 0U && (value_kind == TINYPY_VALUE_BOOL || value_kind == TINYPY_VALUE_INTEGER || value_kind == TINYPY_VALUE_LONG || value_kind == TINYPY_VALUE_FLOAT || value_kind == TINYPY_VALUE_COMPLEX || value_kind == TINYPY_VALUE_STRING || value_kind == TINYPY_VALUE_UNICODE);

    *out_unicode = spec_unicode;
    (void)memset(&field, 0, sizeof(field));
    (void)memset(&output, 0, sizeof(output));
    field.vm = vm;
    output.vm = vm;
    if (allow_special != 0 && conversion == 0 && direct_builtin == 0 && tinypy_internal_object_has_special(value, "__format__", 10U) != 0) {
        tinypy_value_t *method = tinypy_internal_object_get_special(value, "__format__", 10U, out_error);
        tinypy_value_t *format_spec;
        tinypy_value_t *arguments;
        tinypy_value_t *result;

        if (method == NULL) {
            return NULL;
        }
        format_spec = spec_unicode != 0 ? tinypy_unicode_from_utf8(vm, (const char *)spec, spec_size) : tinypy_string_from_bytes(vm, spec, spec_size);
        arguments = tinypy_tuple_from_items(vm, &format_spec, 1U);
        result = tinypy_call(method, arguments, NULL, out_error);
        TINYPY_DECREF(arguments);
        TINYPY_DECREF(format_spec);
        TINYPY_DECREF(method);
        if (result == NULL) {
            return NULL;
        }
        if (TINYPY_VALUE_KIND(result) != TINYPY_VALUE_STRING && TINYPY_VALUE_KIND(result) != TINYPY_VALUE_UNICODE) {
            TINYPY_DECREF(result);
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "__format__ returned a non-string", out_error);
            return NULL;
        }
        if (spec_unicode != 0 && TINYPY_VALUE_KIND(result) == TINYPY_VALUE_STRING) {
            if (__tinypy_string_format_require_ascii(vm, result, out_error) == 0) {
                TINYPY_DECREF(result);
                return NULL;
            }
            tinypy_value_t *promoted = tinypy_unicode_from_utf8(vm, (const char *)TINYPY_TEXT_BYTES(result), TINYPY_TEXT_BYTE_SIZE(result));

            TINYPY_DECREF(result);
            result = promoted;
        }
        *out_unicode = TINYPY_VALUE_KIND(result) == TINYPY_VALUE_UNICODE ? TINYPY_TRUE : TINYPY_FALSE;
        return result;
    }
    size_t first_size = 1U;
    if (spec_unicode != 0 && spec_size != 0U) {
        uint32_t first_code_point;

        first_size = tinypy_internal_utf8_decode(spec, spec_size, &first_code_point);
    }
    if (first_size < spec_size && (spec[first_size] == (uint8_t)'<' || spec[first_size] == (uint8_t)'>' || spec[first_size] == (uint8_t)'^' || spec[first_size] == (uint8_t)'=')) {
        (void)memcpy(fill, spec, first_size);
        fill_size = first_size;
        align = spec[first_size];
        explicit_fill = TINYPY_TRUE;
        offset = first_size + 1U;
    }
    else if (offset < spec_size && (spec[offset] == (uint8_t)'<' || spec[offset] == (uint8_t)'>' || spec[offset] == (uint8_t)'^' || spec[offset] == (uint8_t)'=')) {
        align = spec[offset++];
    }
    if (offset < spec_size && (spec[offset] == (uint8_t)'+' || spec[offset] == (uint8_t)'-' || spec[offset] == (uint8_t)' ')) {
        sign_specified = TINYPY_TRUE;
        plus = spec[offset] == (uint8_t)'+';
        space = spec[offset] == (uint8_t)' ';
        offset += 1U;
    }
    if (offset < spec_size && spec[offset] == (uint8_t)'#') {
        alternate = 1;
        offset += 1U;
    }
    if (offset < spec_size && spec[offset] == (uint8_t)'0') {
        zero_padding = TINYPY_TRUE;
        if (explicit_fill == 0) {
            fill[0] = (uint8_t)'0';
            fill_size = 1U;
        }
        if (align == 0U) {
            align = (uint8_t)'=';
        }
        offset += 1U;
    }
    while (offset < spec_size && spec[offset] >= (uint8_t)'0' && spec[offset] <= (uint8_t)'9') {
        if (width > (SIZE_MAX - 9U) / 10U) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "format width is too large", out_error);
            return NULL;
        }
        width = width * 10U + (size_t)(spec[offset++] - (uint8_t)'0');
    }
    if (offset < spec_size && spec[offset] == (uint8_t)',') {
        grouping = TINYPY_TRUE;
        offset += 1U;
    }
    if (offset < spec_size && spec[offset] == (uint8_t)'.') {
        precision = 0;
        offset += 1U;
        if (offset == spec_size || spec[offset] < (uint8_t)'0' || spec[offset] > (uint8_t)'9') {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "format precision is missing", out_error);
            return NULL;
        }
        while (offset < spec_size && spec[offset] >= (uint8_t)'0' && spec[offset] <= (uint8_t)'9') {
            if (precision > (INT64_MAX - 9) / 10) {
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "format precision is too large", out_error);
                return NULL;
            }
            precision = precision * 10 + (int64_t)(spec[offset++] - (uint8_t)'0');
        }
    }
    if (offset < spec_size) {
        type = spec[offset++];
    }
    if (offset != spec_size) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "invalid format specifier", out_error);
        return NULL;
    }
    if (grouping != 0 && type == (uint8_t)'n') {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "cannot specify ',' with 'n'", out_error);
        return NULL;
    }
    if (conversion == 0 && type == 0U) {
        if (value_kind == TINYPY_VALUE_INTEGER || value_kind == TINYPY_VALUE_LONG || (value_kind == TINYPY_VALUE_BOOL && spec_size != 0U)) {
            type = (uint8_t)'d';
        }
        else if (value_kind == TINYPY_VALUE_FLOAT && spec_size != 0U) {
            float_default_type = TINYPY_TRUE;
        }
    }
    if (conversion == 'r' || conversion == 's') {
        tinypy_value_t *text;

        if (conversion == 's' && spec_unicode != 0) {
            text = tinypy_internal_object_unicode(value, out_error);
        }
        else {
            text = conversion == 'r' ? tinypy_object_repr(value, out_error) : tinypy_object_str(value, out_error);
        }

        if (text == NULL) {
            return NULL;
        }
        if (type != 0U && type != (uint8_t)'s') {
            TINYPY_DECREF(text);
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "string format received an incompatible type", out_error);
            return NULL;
        }
        if (TINYPY_VALUE_KIND(text) == TINYPY_VALUE_UNICODE || (conversion == 's' && TINYPY_VALUE_KIND(value) == TINYPY_VALUE_UNICODE)) {
            *out_unicode = TINYPY_TRUE;
        }
        if (*out_unicode != 0 && __tinypy_string_format_require_ascii(vm, text, out_error) == 0) {
            TINYPY_DECREF(text);
            return NULL;
        }
        __tinypy_string_builder_append(&field, TINYPY_TEXT_BYTES(text), TINYPY_TEXT_BYTE_SIZE(text));
        TINYPY_DECREF(text);
    }
    else if (type == (uint8_t)'c') {
        int64_t character;

        numeric = TINYPY_TRUE;
        if ((value_kind != TINYPY_VALUE_BOOL && value_kind != TINYPY_VALUE_INTEGER && value_kind != TINYPY_VALUE_LONG) || sign_specified != 0 || precision >= 0 || grouping != 0) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "invalid character format", out_error);
            return NULL;
        }
        if (tinypy_internal_index_as_i64(value, &character, TINYPY_FALSE, out_error) == 0) {
            return NULL;
        }
        if (character < 0 || character > INT64_C(0xff)) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "character argument is out of range", out_error);
            return NULL;
        }
        if (spec_unicode != 0) {
            if (character > INT64_C(0x7f)) {
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_UNICODE_DECODE, "ascii decode error during string formatting", out_error);
                return NULL;
            }
            __tinypy_string_builder_character(&field, (uint8_t)character);
            *out_unicode = TINYPY_TRUE;
        }
        else {
            __tinypy_string_builder_character(&field, (uint8_t)character);
        }
    }
    else if (conversion == 0 && value_kind == TINYPY_VALUE_COMPLEX) {
        double real = TINYPY_COMPLEX_OBJECT(value)->real;
        double imaginary = TINYPY_COMPLEX_OBJECT(value)->imaginary;
        tinypy_bool_t default_type = type == 0U ? TINYPY_TRUE : TINYPY_FALSE;
        tinypy_bool_t pure_imaginary = real == 0.0 && signbit(real) == 0 ? TINYPY_TRUE : TINYPY_FALSE;
        uint8_t component_type = type == 0U || type == (uint8_t)'n' ? (uint8_t)'g' : type;
        int64_t component_precision = precision >= 0 ? precision : (default_type != 0 ? 12 : 6);

        numeric = TINYPY_TRUE;
        if (type != 0U && type != (uint8_t)'e' && type != (uint8_t)'E' && type != (uint8_t)'f' && type != (uint8_t)'F' && type != (uint8_t)'g' && type != (uint8_t)'G' && type != (uint8_t)'n') {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "unknown format code for complex", out_error);
            return NULL;
        }
        if (alternate != 0) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "alternate form is not allowed in complex format", out_error);
            return NULL;
        }
        if ((zero_padding != 0 && explicit_fill == 0) || (fill_size == 1U && fill[0] == (uint8_t)'0')) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "zero padding is not allowed in complex format", out_error);
            return NULL;
        }
        if (align == (uint8_t)'=') {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "'=' alignment is not allowed in complex format", out_error);
            return NULL;
        }
        if (default_type != 0 && pure_imaginary != 0) {
            if (__tinypy_string_format_complex_component(vm, &field, imaginary, component_type, plus, space, component_precision, grouping, out_error) == 0) {
                __tinypy_string_builder_discard(&field);
                return NULL;
            }
            __tinypy_string_builder_character(&field, (uint8_t)'j');
        }
        else {
            if (default_type != 0) {
                __tinypy_string_builder_character(&field, (uint8_t)'(');
            }
            if (__tinypy_string_format_complex_component(vm, &field, real, component_type, plus, space, component_precision, grouping, out_error) == 0 || __tinypy_string_format_complex_component(vm, &field, imaginary, component_type, 1, 0, component_precision, grouping, out_error) == 0) {
                __tinypy_string_builder_discard(&field);
                return NULL;
            }
            __tinypy_string_builder_character(&field, (uint8_t)'j');
            if (default_type != 0) {
                __tinypy_string_builder_character(&field, (uint8_t)')');
            }
        }
        grouping = TINYPY_FALSE;
    }
    else if (type == (uint8_t)'b' || type == (uint8_t)'d' || type == (uint8_t)'o' || type == (uint8_t)'x' || type == (uint8_t)'X' || (type == (uint8_t)'n' && (value_kind == TINYPY_VALUE_BOOL || value_kind == TINYPY_VALUE_INTEGER || value_kind == TINYPY_VALUE_LONG))) {
        numeric = TINYPY_TRUE;
        if (value_kind != TINYPY_VALUE_BOOL && value_kind != TINYPY_VALUE_INTEGER && value_kind != TINYPY_VALUE_LONG) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "integer format requires an integer", out_error);
            return NULL;
        }
        if (precision >= 0) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "precision is not allowed in integer format", out_error);
            return NULL;
        }
        if (grouping != 0 && type != (uint8_t)'d' && type != (uint8_t)'n') {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "digit grouping is not allowed for this integer format", out_error);
            return NULL;
        }
        if (__tinypy_percent_append_integer(vm, &field, value, type == (uint8_t)'n' ? (uint8_t)'d' : type, alternate, plus, space, precision, TINYPY_TRUE, &prefix_size, out_error) == 0) {
            __tinypy_string_builder_discard(&field);
            return NULL;
        }
    }
    else if (float_default_type != 0 || type == (uint8_t)'e' || type == (uint8_t)'E' || type == (uint8_t)'f' || type == (uint8_t)'F' || type == (uint8_t)'g' || type == (uint8_t)'G' || type == (uint8_t)'n' || type == (uint8_t)'%') {
        uint8_t float_type = float_default_type != 0 || type == (uint8_t)'n' ? (uint8_t)'g' : type;
        int64_t float_precision = float_default_type != 0 && precision < 0 ? 12 : precision;

        numeric = TINYPY_TRUE;
        if (value_kind != TINYPY_VALUE_BOOL && value_kind != TINYPY_VALUE_INTEGER && value_kind != TINYPY_VALUE_LONG && value_kind != TINYPY_VALUE_FLOAT) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "float format requires a number", out_error);
            return NULL;
        }
        if (alternate != 0) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "alternate form is not allowed in float format", out_error);
            return NULL;
        }
        if (__tinypy_percent_append_float(vm, &field, value, float_type, alternate, plus, space, float_precision, TINYPY_FALSE, &prefix_size, out_error) == 0) {
            __tinypy_string_builder_discard(&field);
            return NULL;
        }
        if (float_default_type != 0) {
            __tinypy_string_format_add_dot_zero(&field, prefix_size, precision);
        }
    }
    else {
        tinypy_value_t *text;
        tinypy_bool_t default_numeric = type == 0U && TINYPY_VALUE_KIND(value) == TINYPY_VALUE_FLOAT;

        if (sign_specified != 0 || alternate != 0 || grouping != 0 || align == (uint8_t)'=') {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "invalid format specifier for string", out_error);
            return NULL;
        }
        if (type != 0U && type != (uint8_t)'s') {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "unknown format code", out_error);
            return NULL;
        }
        if (type == (uint8_t)'s' && (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_BOOL || TINYPY_VALUE_KIND(value) == TINYPY_VALUE_INTEGER || TINYPY_VALUE_KIND(value) == TINYPY_VALUE_LONG || TINYPY_VALUE_KIND(value) == TINYPY_VALUE_FLOAT || TINYPY_VALUE_KIND(value) == TINYPY_VALUE_COMPLEX)) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "string format requires a string", out_error);
            return NULL;
        }
        if (spec_unicode != 0) {
            text = tinypy_internal_object_unicode(value, out_error);
        }
        else if (value_kind == TINYPY_VALUE_UNICODE) {
            text = value;
            TINYPY_INCREF(text);
        }
        else {
            text = tinypy_object_str(value, out_error);
        }
        if (text == NULL) {
            return NULL;
        }
        if (TINYPY_VALUE_KIND(text) == TINYPY_VALUE_UNICODE || TINYPY_VALUE_KIND(value) == TINYPY_VALUE_UNICODE) {
            *out_unicode = TINYPY_TRUE;
        }
        if (*out_unicode != 0 && __tinypy_string_format_require_ascii(vm, text, out_error) == 0) {
            TINYPY_DECREF(text);
            return NULL;
        }
        if (default_numeric != 0) {
            const uint8_t *text_bytes = TINYPY_TEXT_BYTES(text);
            size_t text_size = TINYPY_TEXT_BYTE_SIZE(text);

            numeric = TINYPY_TRUE;
            if (text_size != 0U && text_bytes[0] == (uint8_t)'-') {
                prefix_size = 1U;
            }
            else if (plus != 0 || space != 0) {
                __tinypy_string_builder_character(&field, plus != 0 ? (uint8_t)'+' : (uint8_t)' ');
                prefix_size = 1U;
            }
        }
        __tinypy_string_builder_append(&field, TINYPY_TEXT_BYTES(text), TINYPY_TEXT_BYTE_SIZE(text));
        TINYPY_DECREF(text);
    }
    if (grouping != 0 && numeric != 0 && align == (uint8_t)'=' && fill_size == 1U && fill[0] == (uint8_t)'0') {
        __tinypy_string_format_group_zero_pad(&field, prefix_size, width);
    }
    if (grouping != 0 && numeric != 0) {
        __tinypy_string_format_group_digits(&field, prefix_size);
    }
    field_width = field.size;
    if (numeric == 0) {
        if (*out_unicode != 0) {
            size_t byte_offset = 0U;
            size_t character_count = 0U;

            while (byte_offset < field.size && (precision < 0 || (uint64_t)character_count < (uint64_t)precision)) {
                byte_offset += __tinypy_string_utf8_width(field.bytes[byte_offset]);
                character_count += 1U;
            }
            if (byte_offset < field.size) {
                field.size = byte_offset;
            }
            field_width = character_count;
        }
        else if (precision >= 0 && (uint64_t)precision < (uint64_t)field.size) {
            field.size = (size_t)precision;
            field_width = field.size;
        }
    }
    if (align == 0U) {
        align = numeric != 0 ? (uint8_t)'>' : (uint8_t)'<';
    }
    __tinypy_string_format_padding(&output, &field, field_width, prefix_size, width, fill, fill_size, align);
    __tinypy_string_builder_discard(&field);
    tinypy_value_t *return_value_1 = __tinypy_string_builder_finish(&output, *out_unicode, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_string_format_value(tinypy_vm_t *vm, tinypy_value_t *value, int32_t conversion, const uint8_t *spec, size_t spec_size, tinypy_bool_t spec_unicode, tinypy_bool_t *out_unicode, tinypy_error_t **out_error) {
    tinypy_value_t *return_value_1 = __tinypy_internal_string_format_value(vm, value, conversion, spec, spec_size, spec_unicode, TINYPY_TRUE, out_unicode, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_string_format_builtin_value(tinypy_vm_t *vm, tinypy_value_t *value, int32_t conversion, const uint8_t *spec, size_t spec_size, tinypy_bool_t spec_unicode, tinypy_bool_t *out_unicode, tinypy_error_t **out_error) {
    tinypy_value_t *return_value_1 = __tinypy_internal_string_format_value(vm, value, conversion, spec, spec_size, spec_unicode, TINYPY_FALSE, out_unicode, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_string_format_lookup(tinypy_vm_t *vm, tinypy_value_t *args, tinypy_value_t *kwargs, const uint8_t *field, size_t field_size, size_t *auto_index, int32_t *numbering_mode, tinypy_error_t **out_error) {
    tinypy_value_t *value = NULL;
    size_t head_size = 0U;
    size_t path_offset;

    while (head_size < field_size && field[head_size] != (uint8_t)'.' && field[head_size] != (uint8_t)'[') {
        head_size += 1U;
    }
    if (head_size == 0U) {
        if (*numbering_mode == 2) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "cannot switch from manual field specification to automatic field numbering", out_error);
            return NULL;
        }
        *numbering_mode = 1;
        if (*auto_index + 1U >= TINYPY_TUPLE_SIZE(args)) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_INDEX, "format positional argument is missing", out_error);
            return NULL;
        }
        value = TINYPY_TUPLE_GET(args, *auto_index + 1U);
        ++(*auto_index);
        TINYPY_INCREF(value);
    }
    else {
        size_t index;
        int32_t numeric = INT32_C(1);

        for (index = 0U; index < head_size; ++index) {
            if (field[index] < (uint8_t)'0' || field[index] > (uint8_t)'9') {
                numeric = INT32_C(0);
            }
        }
        if (numeric != 0) {
            size_t position = 0U;

            if (*numbering_mode == 1) {
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "cannot switch from automatic field numbering to manual field specification", out_error);
                return NULL;
            }
            *numbering_mode = 2;
            for (index = 0U; index < head_size; ++index) {
                size_t digit = (size_t)(field[index] - (uint8_t)'0');

                if (position > (SIZE_MAX - digit) / 10U) {
                    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "too many decimal digits in format string", out_error);
                    return NULL;
                }
                position = position * 10U + digit;
            }
#if SIZE_MAX > INT64_MAX
            if (position > (size_t)INT64_MAX) {
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "too many decimal digits in format string", out_error);
                return NULL;
            }
#endif
            if (TINYPY_TUPLE_SIZE(args) <= 1U || position >= TINYPY_TUPLE_SIZE(args) - 1U) {
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_INDEX, "format positional argument is missing", out_error);
                return NULL;
            }
            value = TINYPY_TUPLE_GET(args, position + 1U);
            TINYPY_INCREF(value);
        }
        else if (kwargs != NULL) {
            tinypy_value_t *key = tinypy_string_from_bytes(vm, field, head_size);

            value = tinypy_dict_get_optional(kwargs, key);
            if (value != NULL) {
                TINYPY_INCREF(value);
            }
            TINYPY_DECREF(key);
        }
    }
    if (value == NULL) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_KEY, "format named argument is missing", out_error);
        return NULL;
    }
    path_offset = head_size;
    while (path_offset < field_size) {
        tinypy_value_t *next;

        if (field[path_offset] == (uint8_t)'.') {
            size_t name_begin = ++path_offset;

            while (path_offset < field_size && field[path_offset] != (uint8_t)'.' && field[path_offset] != (uint8_t)'[') {
                path_offset += 1U;
            }
            if (name_begin == path_offset) {
                TINYPY_DECREF(value);
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "empty attribute in format field", out_error);
                return NULL;
            }
            next = tinypy_object_get_attr(value, (const char *)field + name_begin, path_offset - name_begin, out_error);
        }
        else {
            size_t key_begin = ++path_offset;
            size_t key_end;
            tinypy_value_t *key;
            int32_t numeric = INT32_C(1);
            size_t index;

            while (path_offset < field_size && field[path_offset] != (uint8_t)']') {
                path_offset += 1U;
            }
            if (path_offset == field_size) {
                TINYPY_DECREF(value);
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "missing ']' in format field", out_error);
                return NULL;
            }
            key_end = path_offset++;
            for (index = key_begin; index < key_end; ++index) {
                if (field[index] < (uint8_t)'0' || field[index] > (uint8_t)'9') {
                    numeric = INT32_C(0);
                }
            }
            if (numeric != 0 && key_begin != key_end) {
                int64_t integer = 0;

                for (index = key_begin; index < key_end; ++index) {
                    int64_t digit = (int64_t)(field[index] - (uint8_t)'0');

                    if (integer > (INT64_MAX - digit) / 10) {
                        TINYPY_DECREF(value);
                        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "too many decimal digits in format string", out_error);
                        return NULL;
                    }
                    integer = integer * 10 + digit;
                }
                key = tinypy_integer_from_i64(vm, integer);
            }
            else {
                key = tinypy_string_from_bytes(vm, field + key_begin, key_end - key_begin);
            }
            next = tinypy_get_item(value, key, out_error);
            TINYPY_DECREF(key);
        }
        TINYPY_DECREF(value);
        if (next == NULL) {
            return NULL;
        }
        value = next;
    }
    return value;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_string_format_expand_spec(tinypy_vm_t *vm, tinypy_value_t *args, tinypy_value_t *kwargs, const uint8_t *spec, size_t spec_size, size_t *auto_index, int32_t *numbering_mode, tinypy_bool_t spec_unicode, tinypy_error_t **out_error) {
    size_t offset = 0U;
    tinypy_bool_t unicode = spec_unicode;
    tinypy_string_builder_t builder;

    (void)memset(&builder, 0, sizeof(builder));
    builder.vm = vm;
    while (offset < spec_size) {
        if (spec[offset] == (uint8_t)'{' && offset + 1U < spec_size && spec[offset + 1U] == (uint8_t)'{') {
            __tinypy_string_builder_character(&builder, (uint8_t)'{');
            offset += 2U;
            continue;
        }
        if (spec[offset] == (uint8_t)'}' && offset + 1U < spec_size && spec[offset + 1U] == (uint8_t)'}') {
            __tinypy_string_builder_character(&builder, (uint8_t)'}');
            offset += 2U;
            continue;
        }
        if (spec[offset] == (uint8_t)'{') {
            size_t end = offset + 1U;
            size_t field_end;
            size_t nested_spec_begin;
            int32_t conversion = 0;
            tinypy_value_t *value;
            tinypy_value_t *text;
            tinypy_bool_t field_unicode;

            while (end < spec_size && spec[end] != (uint8_t)'}') {
                if (spec[end] == (uint8_t)'{') {
                    __tinypy_string_builder_discard(&builder);
                    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "format specifications may not contain nested replacement fields", out_error);
                    return NULL;
                }
                end += 1U;
            }
            if (end == spec_size) {
                __tinypy_string_builder_discard(&builder);
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "unmatched '{' in format specification", out_error);
                return NULL;
            }
            field_end = offset + 1U;
            while (field_end < end && spec[field_end] != (uint8_t)'!' && spec[field_end] != (uint8_t)':') {
                field_end += 1U;
            }
            nested_spec_begin = end;
            if (field_end < end && spec[field_end] == (uint8_t)'!') {
                if (field_end + 1U >= end || (spec[field_end + 1U] != (uint8_t)'r' && spec[field_end + 1U] != (uint8_t)'s')) {
                    __tinypy_string_builder_discard(&builder);
                    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "invalid format conversion", out_error);
                    return NULL;
                }
                conversion = spec[field_end + 1U];
                nested_spec_begin = field_end + 2U;
                if (nested_spec_begin < end && spec[nested_spec_begin] == (uint8_t)':') {
                    nested_spec_begin += 1U;
                }
                else if (nested_spec_begin != end) {
                    __tinypy_string_builder_discard(&builder);
                    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "invalid format conversion", out_error);
                    return NULL;
                }
            }
            else if (field_end < end) {
                nested_spec_begin = field_end + 1U;
            }
            value = __tinypy_string_format_lookup(vm, args, kwargs, spec + offset + 1U, field_end - offset - 1U, auto_index, numbering_mode, out_error);
            if (value == NULL) {
                __tinypy_string_builder_discard(&builder);
                return NULL;
            }
            text = tinypy_internal_string_format_value(vm, value, conversion, spec + nested_spec_begin, end - nested_spec_begin, spec_unicode, &field_unicode, out_error);
            TINYPY_DECREF(value);
            if (text == NULL) {
                __tinypy_string_builder_discard(&builder);
                return NULL;
            }
            __tinypy_string_builder_append(&builder, TINYPY_TEXT_BYTES(text), TINYPY_TEXT_BYTE_SIZE(text));
            if (field_unicode != 0) {
                unicode = TINYPY_TRUE;
            }
            TINYPY_DECREF(text);
            offset = end + 1U;
            continue;
        }
        if (spec[offset] == (uint8_t)'}') {
            __tinypy_string_builder_discard(&builder);
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "single '}' in format specification", out_error);
            return NULL;
        }
        __tinypy_string_builder_character(&builder, spec[offset++]);
    }
    tinypy_value_t *return_value_1 = __tinypy_string_builder_finish(&builder, unicode, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_string_format_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *format = TINYPY_TUPLE_GET(args, 0U);
    const uint8_t *bytes = TINYPY_TEXT_BYTES(format);
    size_t size = TINYPY_TEXT_BYTE_SIZE(format);
    size_t offset = 0U;
    size_t automatic_index = 0U;
    int32_t numbering_mode = 0;
    tinypy_bool_t unicode = TINYPY_VALUE_KIND(format) == TINYPY_VALUE_UNICODE ? TINYPY_TRUE : TINYPY_FALSE;
    tinypy_string_builder_t builder;

    (void)user_data;
    if (__tinypy_string_method_arguments(vm, args, kwargs, 1U, SIZE_MAX, INT32_C(1), out_error) == 0) {
        return NULL;
    }
    (void)memset(&builder, 0, sizeof(builder));
    builder.vm = vm;
    while (offset < size) {
        if (bytes[offset] == (uint8_t)'{' && offset + 1U < size && bytes[offset + 1U] == (uint8_t)'{') {
            __tinypy_string_builder_character(&builder, (uint8_t)'{');
            offset += 2U;
            continue;
        }
        if (bytes[offset] == (uint8_t)'}' && offset + 1U < size && bytes[offset + 1U] == (uint8_t)'}') {
            __tinypy_string_builder_character(&builder, (uint8_t)'}');
            offset += 2U;
            continue;
        }
        if (bytes[offset] == (uint8_t)'{') {
            size_t end = offset + 1U;
            size_t depth = 1U;
            size_t field_end;
            size_t spec_begin;
            int32_t conversion = 0;
            tinypy_value_t *value;
            tinypy_value_t *text;
            tinypy_value_t *expanded_spec = NULL;
            tinypy_bool_t field_unicode;

            while (end < size && depth != 0U) {
                if (bytes[end] == (uint8_t)'{') {
                    depth += 1U;
                }
                else if (bytes[end] == (uint8_t)'}') {
                    depth -= 1U;
                    if (depth == 0U) {
                        break;
                    }
                }
                end += 1U;
            }
            if (depth != 0U) {
                __tinypy_string_builder_discard(&builder);
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "unmatched '{' in format string", out_error);
                return NULL;
            }
            field_end = offset + 1U;
            while (field_end < end && bytes[field_end] != (uint8_t)'!' && bytes[field_end] != (uint8_t)':') {
                field_end += 1U;
            }
            spec_begin = end;
            if (field_end < end && bytes[field_end] == (uint8_t)'!') {
                if (field_end + 1U >= end || (bytes[field_end + 1U] != (uint8_t)'r' && bytes[field_end + 1U] != (uint8_t)'s')) {
                    __tinypy_string_builder_discard(&builder);
                    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "invalid format conversion", out_error);
                    return NULL;
                }
                conversion = bytes[field_end + 1U];
                spec_begin = field_end + 2U;
                if (spec_begin < end && bytes[spec_begin] == (uint8_t)':') {
                    spec_begin += 1U;
                }
                else if (spec_begin == end) {
                    spec_begin = end;
                }
                else {
                    __tinypy_string_builder_discard(&builder);
                    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "invalid format conversion", out_error);
                    return NULL;
                }
            }
            else if (field_end < end && bytes[field_end] == (uint8_t)':') {
                spec_begin = field_end + 1U;
            }
            value = __tinypy_string_format_lookup(vm, args, kwargs, bytes + offset + 1U, field_end - offset - 1U, &automatic_index, &numbering_mode, out_error);
            if (value == NULL) {
                __tinypy_string_builder_discard(&builder);
                return NULL;
            }
            if (memchr(bytes + spec_begin, '{', end - spec_begin) != NULL || memchr(bytes + spec_begin, '}', end - spec_begin) != NULL) {
                expanded_spec = __tinypy_string_format_expand_spec(vm, args, kwargs, bytes + spec_begin, end - spec_begin, &automatic_index, &numbering_mode, TINYPY_VALUE_KIND(format) == TINYPY_VALUE_UNICODE ? TINYPY_TRUE : TINYPY_FALSE, out_error);
                if (expanded_spec == NULL) {
                    TINYPY_DECREF(value);
                    __tinypy_string_builder_discard(&builder);
                    return NULL;
                }
            }
            text = tinypy_internal_string_format_value(vm, value, conversion, expanded_spec != NULL ? TINYPY_TEXT_BYTES(expanded_spec) : bytes + spec_begin, expanded_spec != NULL ? TINYPY_TEXT_BYTE_SIZE(expanded_spec) : end - spec_begin, expanded_spec != NULL ? (TINYPY_VALUE_KIND(expanded_spec) == TINYPY_VALUE_UNICODE ? TINYPY_TRUE : TINYPY_FALSE) : (TINYPY_VALUE_KIND(format) == TINYPY_VALUE_UNICODE ? TINYPY_TRUE : TINYPY_FALSE), &field_unicode, out_error);
            TINYPY_DECREF(value);
            if (expanded_spec != NULL) {
                TINYPY_DECREF(expanded_spec);
            }
            if (text == NULL) {
                __tinypy_string_builder_discard(&builder);
                return NULL;
            }
            const uint8_t *bytes_2 = TINYPY_TEXT_BYTES(text);
            size_t byte_size = TINYPY_TEXT_BYTE_SIZE(text);
            if (unicode != 0 && tinypy_internal_text_ascii_compatible(vm, text, out_error) == 0) {
                TINYPY_DECREF(text);
                __tinypy_string_builder_discard(&builder);
                return NULL;
            }
            if (unicode == 0 && field_unicode != 0) {
                size_t byte_index;

                for (byte_index = 0U; byte_index < byte_size; ++byte_index) {
                    if (bytes_2[byte_index] >= 0x80U) {
                        TINYPY_DECREF(text);
                        __tinypy_string_builder_discard(&builder);
                        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_UNICODE_ENCODE, "ascii encode error during string formatting", out_error);
                        return NULL;
                    }
                }
            }
            __tinypy_string_builder_append(&builder, bytes_2, byte_size);
            TINYPY_DECREF(text);
            offset = end + 1U;
            continue;
        }
        if (bytes[offset] == (uint8_t)'}') {
            __tinypy_string_builder_discard(&builder);
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "single '}' in format string", out_error);
            return NULL;
        }
        __tinypy_string_builder_character(&builder, bytes[offset++]);
    }
    tinypy_value_t *return_value_1 = __tinypy_string_builder_finish(&builder, unicode, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_string_align_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *text = TINYPY_TUPLE_GET(args, 0U);
    int64_t width;
    const uint8_t *fill = (const uint8_t *)" ";
    size_t fill_size = 1U;
    size_t size = __tinypy_string_character_count(text);
    size_t byte_size = TINYPY_TEXT_BYTE_SIZE(text);
    size_t padding;
    size_t left;
    size_t right;
    tinypy_string_builder_t builder;
    intptr_t mode = (intptr_t)user_data;

    if (__tinypy_string_method_arguments(vm, args, kwargs, 2U, 3U, INT32_C(0), out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *item = TINYPY_TUPLE_GET(args, 1U);
    if (__tinypy_string_integer(vm, item, &width, out_error) == 0) {
        return NULL;
    }
    if (TINYPY_TUPLE_SIZE(args) == 3U) {
        tinypy_value_t *fill_value = TINYPY_TUPLE_GET(args, 2U);

        if ((TINYPY_VALUE_KIND(fill_value) != TINYPY_VALUE_STRING && TINYPY_VALUE_KIND(fill_value) != TINYPY_VALUE_UNICODE) || __tinypy_string_character_count(fill_value) != 1U) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "fill character must be exactly one character", out_error);
            return NULL;
        }
        if (TINYPY_VALUE_KIND(text) == TINYPY_VALUE_STRING && TINYPY_VALUE_KIND(fill_value) == TINYPY_VALUE_UNICODE) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "byte string fill character must be a byte string", out_error);
            return NULL;
        }
        if (TINYPY_VALUE_KIND(text) == TINYPY_VALUE_UNICODE && tinypy_internal_text_ascii_compatible(vm, fill_value, out_error) == 0) {
            return NULL;
        }
        fill = TINYPY_TEXT_BYTES(fill_value);
        fill_size = TINYPY_TEXT_BYTE_SIZE(fill_value);
    }
    if (width <= 0 || (uint64_t)width <= size) {
        TINYPY_INCREF(text);
        return text;
    }
    padding = (size_t)width - size;
    if (mode < 0) {
        left = 0U;
    }
    else if (mode > 0) {
        left = padding;
    }
    else {
        left = padding / 2U + (padding & (size_t)width & 1U);
    }
    right = padding - left;
    if (fill_size != 0U && padding > (SIZE_MAX - byte_size) / fill_size) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "resulting string is too large", out_error);
        return NULL;
    }
    size_t result_size = byte_size + padding * fill_size;
    (void)memset(&builder, 0, sizeof(builder));
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(text);
    if (__tinypy_string_builder_allocate_exact(&builder, vm, kind == TINYPY_VALUE_UNICODE, result_size, kind == TINYPY_VALUE_UNICODE ? size + padding : 0U, out_error) == 0) {
        return NULL;
    }
    __tinypy_string_builder_repeat(&builder, fill, fill_size, left);
    const uint8_t *bytes = TINYPY_TEXT_BYTES(text);
    __tinypy_string_builder_append(&builder, bytes, byte_size);
    __tinypy_string_builder_repeat(&builder, fill, fill_size, right);
    tinypy_value_t *return_value_1 = __tinypy_string_builder_finish(&builder, kind == TINYPY_VALUE_UNICODE, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_string_join_sequence(tinypy_vm_t *vm, tinypy_value_t *separator, tinypy_value_t *sequence, tinypy_bool_t *out_handled, tinypy_error_t **out_error) {
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(sequence);
    size_t count;
    size_t total = 0U;
    size_t character_total = 0U;
    size_t index;
    tinypy_bool_t unicode;
    tinypy_string_builder_t builder;

    *out_handled = TINYPY_FALSE;
    if (kind != TINYPY_VALUE_LIST && kind != TINYPY_VALUE_TUPLE) {
        return NULL;
    }
    *out_handled = TINYPY_TRUE;
    count = kind == TINYPY_VALUE_LIST ? TINYPY_LIST_SIZE(sequence) : TINYPY_TUPLE_SIZE(sequence);
    unicode = TINYPY_VALUE_KIND(separator) == TINYPY_VALUE_UNICODE ? TINYPY_TRUE : TINYPY_FALSE;
    for (index = 0U; index < count; ++index) {
        tinypy_value_t *item = kind == TINYPY_VALUE_LIST ? TINYPY_LIST_GET(sequence, index) : TINYPY_TUPLE_GET(sequence, index);
        size_t item_size;

        if (TINYPY_VALUE_KIND(item) != TINYPY_VALUE_STRING && TINYPY_VALUE_KIND(item) != TINYPY_VALUE_UNICODE) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "join sequence item is not a string", out_error);
            return NULL;
        }
        if (TINYPY_VALUE_KIND(item) == TINYPY_VALUE_UNICODE) {
            unicode = TINYPY_TRUE;
        }
        item_size = TINYPY_TEXT_BYTE_SIZE(item);
        if (item_size > SIZE_MAX - total) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "joined string is too large", out_error);
            return NULL;
        }
        total += item_size;
        character_total += __tinypy_string_character_count(item);
    }
    if (count > 1U) {
        size_t separator_size = TINYPY_TEXT_BYTE_SIZE(separator);

        if (separator_size != 0U && count - 1U > (SIZE_MAX - total) / separator_size) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "joined string is too large", out_error);
            return NULL;
        }
        total += separator_size * (count - 1U);
        character_total += __tinypy_string_character_count(separator) * (count - 1U);
    }
    if (total >= (size_t)PTRDIFF_MAX) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "joined string is too large", out_error);
        return NULL;
    }
    if (unicode != 0) {
        if (tinypy_internal_text_ascii_compatible(vm, separator, out_error) == 0) {
            return NULL;
        }
        for (index = 0U; index < count; ++index) {
            tinypy_value_t *item = kind == TINYPY_VALUE_LIST ? TINYPY_LIST_GET(sequence, index) : TINYPY_TUPLE_GET(sequence, index);

            if (tinypy_internal_text_ascii_compatible(vm, item, out_error) == 0) {
                return NULL;
            }
        }
    }
    (void)memset(&builder, 0, sizeof(builder));
    if (__tinypy_string_builder_allocate_exact(&builder, vm, unicode, total, unicode != 0 ? character_total : 0U, out_error) == 0) {
        return NULL;
    }
    for (index = 0U; index < count; ++index) {
        tinypy_value_t *item = kind == TINYPY_VALUE_LIST ? TINYPY_LIST_GET(sequence, index) : TINYPY_TUPLE_GET(sequence, index);

        if (index != 0U) {
            __tinypy_string_builder_append(&builder, TINYPY_TEXT_BYTES(separator), TINYPY_TEXT_BYTE_SIZE(separator));
        }
        __tinypy_string_builder_append(&builder, TINYPY_TEXT_BYTES(item), TINYPY_TEXT_BYTE_SIZE(item));
    }
    tinypy_value_t *return_value = __tinypy_string_builder_finish(&builder, unicode, out_error);
    return return_value;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_string_join_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *separator = TINYPY_TUPLE_GET(args, 0U);
    tinypy_error_t *iteration_error = NULL;
    tinypy_string_builder_t builder;
    size_t count = 0U;
    tinypy_bool_t unicode = TINYPY_VALUE_KIND(separator) == TINYPY_VALUE_UNICODE;

    (void)user_data;
    if (__tinypy_string_method_arguments(vm, args, kwargs, 2U, 2U, INT32_C(0), out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *item_2 = TINYPY_TUPLE_GET(args, 1U);
    tinypy_bool_t handled;
    tinypy_value_t *sequence_result = __tinypy_string_join_sequence(vm, separator, item_2, &handled, out_error);

    if (handled != 0) {
        return sequence_result;
    }
    tinypy_value_t *iterator = tinypy_iter(item_2, out_error);
    if (iterator == NULL) {
        return NULL;
    }
    (void)memset(&builder, 0, sizeof(builder));
    builder.vm = vm;
    for (;;) {
        tinypy_value_t *item = tinypy_next(iterator, &iteration_error);

        if (item == NULL) {
            break;
        }
        if (TINYPY_VALUE_KIND(item) != TINYPY_VALUE_STRING && TINYPY_VALUE_KIND(item) != TINYPY_VALUE_UNICODE) {
            TINYPY_DECREF(item);
            TINYPY_DECREF(iterator);
            __tinypy_string_builder_discard(&builder);
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "join sequence item is not a string", out_error);
            return NULL;
        }
        if (unicode != 0 && tinypy_internal_text_ascii_compatible(vm, item, out_error) == 0) {
            TINYPY_DECREF(item);
            TINYPY_DECREF(iterator);
            __tinypy_string_builder_discard(&builder);
            return NULL;
        }
        if (unicode == 0 && TINYPY_VALUE_KIND(item) == TINYPY_VALUE_UNICODE) {
            size_t byte_index;

            if (tinypy_internal_text_ascii_compatible(vm, separator, out_error) == 0) {
                TINYPY_DECREF(item);
                TINYPY_DECREF(iterator);
                __tinypy_string_builder_discard(&builder);
                return NULL;
            }
            for (byte_index = 0U; byte_index < builder.size; ++byte_index) {
                if (builder.bytes[byte_index] >= 0x80U) {
                    TINYPY_DECREF(item);
                    TINYPY_DECREF(iterator);
                    __tinypy_string_builder_discard(&builder);
                    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_UNICODE_DECODE, "ascii decode error", out_error);
                    return NULL;
                }
            }
            unicode = TINYPY_TRUE;
        }
        if (count != 0U) {
            const uint8_t *bytes_2 = TINYPY_TEXT_BYTES(separator);
            size_t byte_size_2 = TINYPY_TEXT_BYTE_SIZE(separator);
            __tinypy_string_builder_append(&builder, bytes_2, byte_size_2);
        }
        const uint8_t *bytes = TINYPY_TEXT_BYTES(item);
        size_t byte_size = TINYPY_TEXT_BYTE_SIZE(item);
        __tinypy_string_builder_append(&builder, bytes, byte_size);
        if (TINYPY_VALUE_KIND(item) == TINYPY_VALUE_UNICODE) {
            unicode = INT32_C(1);
        }
        count += 1U;
        TINYPY_DECREF(item);
    }
    TINYPY_DECREF(iterator);
    if (iteration_error != NULL) {
        __tinypy_string_builder_discard(&builder);
        if (out_error != NULL) {
            *out_error = iteration_error;
        }
        else {
            tinypy_error_release(iteration_error);
        }
        return NULL;
    }
    tinypy_value_t *return_value_1 = __tinypy_string_builder_finish(&builder, unicode, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_string_is_text(const tinypy_value_t *value) {
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);

    return kind == TINYPY_VALUE_STRING || kind == TINYPY_VALUE_UNICODE;
}
//////////////////////////////////////////////////////////////////////////
static size_t __tinypy_string_character_count(const tinypy_value_t *value) {
    size_t return_value_1 = TINYPY_VALUE_KIND(value) == TINYPY_VALUE_UNICODE ? TINYPY_SIZED_SIZE(value) : TINYPY_TEXT_BYTE_SIZE(value);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static size_t __tinypy_string_utf8_width(uint8_t first) {
    if (first < 0x80U) {
        return 1U;
    }
    if (first < 0xe0U) {
        return 2U;
    }
    if (first < 0xf0U) {
        return 3U;
    }
    return 4U;
}
//////////////////////////////////////////////////////////////////////////
static size_t __tinypy_string_next_code_point(const tinypy_value_t *value, size_t offset, uint32_t *out_code_point) {
    const uint8_t *bytes = TINYPY_TEXT_BYTES(value);

    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_STRING) {
        *out_code_point = bytes[offset];
        return 1U;
    }
    size_t return_value = tinypy_internal_utf8_decode(bytes + offset, TINYPY_TEXT_BYTE_SIZE(value) - offset, out_code_point);
    return return_value;
}
//////////////////////////////////////////////////////////////////////////
static size_t __tinypy_string_previous_code_point(const tinypy_value_t *value, size_t offset, uint32_t *out_code_point) {
    const uint8_t *bytes = TINYPY_TEXT_BYTES(value);
    size_t begin = offset - 1U;

    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_STRING) {
        *out_code_point = bytes[begin];
        return 1U;
    }
    while (begin != 0U && (bytes[begin] & 0xc0U) == 0x80U) {
        begin -= 1U;
    }
    size_t return_value = tinypy_internal_utf8_decode(bytes + begin, offset - begin, out_code_point);
    return return_value;
}
//////////////////////////////////////////////////////////////////////////
static size_t __tinypy_string_byte_offset(const tinypy_value_t *value, size_t character_index) {
    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_STRING) {
        return character_index;
    }
    size_t return_value = tinypy_internal_unicode_byte_offset((tinypy_value_t *)value, character_index);
    return return_value;
}
//////////////////////////////////////////////////////////////////////////
static size_t __tinypy_string_character_index(const tinypy_value_t *value, size_t byte_offset) {
    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_STRING) {
        return byte_offset;
    }
    size_t return_value = tinypy_internal_unicode_character_index((tinypy_value_t *)value, byte_offset);
    return return_value;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_string_from_span(tinypy_vm_t *vm, const tinypy_value_t *source, size_t begin, size_t end) {
    if (begin == 0U && end == TINYPY_TEXT_BYTE_SIZE(source)) {
        TINYPY_INCREF((tinypy_value_t *)source);
        return (tinypy_value_t *)source;
    }
    if (TINYPY_VALUE_KIND(source) == TINYPY_VALUE_UNICODE) {
        const uint8_t *bytes_2 = TINYPY_TEXT_BYTES(source);
        tinypy_value_t *return_value_1 = tinypy_unicode_from_utf8(vm, (const char *)bytes_2 + begin, end - begin);
        return return_value_1;
    }
    const uint8_t *bytes = TINYPY_TEXT_BYTES(source);
    tinypy_value_t *return_value_2 = tinypy_string_from_bytes(vm, bytes + begin, end - begin);
    return return_value_2;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_string_from_span_as(tinypy_vm_t *vm, const tinypy_value_t *source, size_t begin, size_t end, tinypy_bool_t unicode) {
    if ((unicode != 0) == (TINYPY_VALUE_KIND(source) == TINYPY_VALUE_UNICODE)) {
        tinypy_value_t *return_value_1 = __tinypy_string_from_span(vm, source, begin, end);
        return return_value_1;
    }
    if (unicode != 0) {
        const uint8_t *bytes = TINYPY_TEXT_BYTES(source);
        tinypy_value_t *return_value_2 = tinypy_unicode_from_utf8(vm, (const char *)bytes + begin, end - begin);
        return return_value_2;
    }
    tinypy_value_t *return_value_3 = __tinypy_string_from_span(vm, source, begin, end);
    return return_value_3;
}
//////////////////////////////////////////////////////////////////////////
static int64_t __tinypy_string_normalized_bound(tinypy_vm_t *vm, tinypy_value_t *value, size_t length, int64_t fallback, tinypy_bool_t clamp_upper, tinypy_error_t **out_error) {
    int64_t bound;

    if (value == NULL || TINYPY_VALUE_KIND(value) == TINYPY_VALUE_NONE) {
        return fallback;
    }
    if (__tinypy_string_integer(vm, value, &bound, out_error) == 0) {
        return INT64_MIN;
    }
    if (bound < 0) {
        if (bound < -(int64_t)length) {
            return 0;
        }
        bound += (int64_t)length;
    }
    if (clamp_upper != 0 && (uint64_t)bound > (uint64_t)length) {
        return (int64_t)length;
    }
    return bound;
}
//////////////////////////////////////////////////////////////////////////
static int64_t __tinypy_string_optional_bound(tinypy_vm_t *vm, tinypy_value_t *args, size_t index, size_t length, int64_t fallback, tinypy_bool_t clamp_upper, tinypy_error_t **out_error) {
    size_t argument_count = TINYPY_TUPLE_SIZE(args);
    tinypy_value_t *value = argument_count > index ? TINYPY_TUPLE_GET(args, index) : NULL;
    int64_t return_value_1 = __tinypy_string_normalized_bound(vm, value, length, fallback, clamp_upper, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_string_require_text(tinypy_vm_t *vm, tinypy_value_t *value, const char *message, tinypy_error_t **out_error) {
    if (__tinypy_string_is_text(value) != 0) {
        return TINYPY_TRUE;
    }
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, message, out_error);
    return TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_string_require_compatible(tinypy_vm_t *vm, const tinypy_value_t *left, const tinypy_value_t *right, tinypy_error_t **out_error) {
    if (TINYPY_VALUE_KIND(left) == TINYPY_VALUE_UNICODE && TINYPY_VALUE_KIND(right) == TINYPY_VALUE_STRING) {
        tinypy_bool_t return_value_1 = tinypy_internal_text_ascii_compatible(vm, right, out_error);
        return return_value_1;
    }
    if (TINYPY_VALUE_KIND(right) == TINYPY_VALUE_UNICODE && TINYPY_VALUE_KIND(left) == TINYPY_VALUE_STRING) {
        tinypy_bool_t return_value_2 = tinypy_internal_text_ascii_compatible(vm, left, out_error);
        return return_value_2;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
typedef struct tinypy_string_search_plan_t {
    const uint8_t *needle;
    size_t needle_size;
    tinypy_bool_t reverse;
    size_t shifts[256];
} tinypy_string_search_plan_t;
//////////////////////////////////////////////////////////////////////////
static void __tinypy_string_search_plan_initialize(tinypy_string_search_plan_t *plan, const uint8_t *needle, size_t needle_size, tinypy_bool_t reverse) {
    size_t index;

    plan->needle = needle;
    plan->needle_size = needle_size;
    plan->reverse = reverse;
    if (needle_size <= 3U) {
        return;
    }
    for (index = 0U; index < 256U; ++index) {
        plan->shifts[index] = needle_size;
    }
    if (reverse == 0) {
        size_t last = needle_size - 1U;

        for (index = 0U; index < last; ++index) {
            plan->shifts[needle[index]] = last - index;
        }
    }
    else {
        index = needle_size;
        while (index > 1U) {
            index -= 1U;
            plan->shifts[needle[index]] = index;
        }
    }
}
//////////////////////////////////////////////////////////////////////////
static ptrdiff_t __tinypy_string_search_plan_find(const tinypy_string_search_plan_t *plan, const uint8_t *haystack, size_t haystack_size) {
    const uint8_t *needle = plan->needle;
    size_t needle_size = plan->needle_size;
    tinypy_bool_t reverse = plan->reverse;
    size_t offset;

    if (needle_size == 0U) {
        return reverse != 0 ? (ptrdiff_t)haystack_size : 0;
    }
    if (needle_size > haystack_size) {
        return -1;
    }
    if (needle_size == 1U) {
        if (reverse == 0) {
            const uint8_t *found = (const uint8_t *)memchr(haystack, needle[0], haystack_size);

            return found != NULL ? (ptrdiff_t)(found - haystack) : -1;
        }
        offset = haystack_size;
        while (offset != 0U) {
            offset -= 1U;
            if (haystack[offset] == needle[0]) {
                return (ptrdiff_t)offset;
            }
        }
        return -1;
    }
    if (needle_size <= 3U || haystack_size < 64U) {
        if (reverse == 0) {
            size_t end = haystack_size - needle_size;

            for (offset = 0U; offset <= end; ++offset) {
                if (haystack[offset] == needle[0] && memcmp(haystack + offset + 1U, needle + 1U, needle_size - 1U) == 0) {
                    return (ptrdiff_t)offset;
                }
            }
        }
        else {
            offset = haystack_size - needle_size + 1U;
            while (offset != 0U) {
                offset -= 1U;
                if (haystack[offset] == needle[0] && memcmp(haystack + offset + 1U, needle + 1U, needle_size - 1U) == 0) {
                    return (ptrdiff_t)offset;
                }
            }
        }
        return -1;
    }
    if (reverse == 0) {
        size_t last = needle_size - 1U;
        offset = 0U;
        while (offset <= haystack_size - needle_size) {
            uint8_t tail = haystack[offset + last];

            if (tail == needle[last] && memcmp(haystack + offset, needle, last) == 0) {
                return (ptrdiff_t)offset;
            }
            offset += plan->shifts[tail];
        }
    }
    else {
        offset = haystack_size - needle_size;
        for (;;) {
            uint8_t head = haystack[offset];

            if (head == needle[0] && memcmp(haystack + offset + 1U, needle + 1U, needle_size - 1U) == 0) {
                return (ptrdiff_t)offset;
            }
            if (offset < plan->shifts[head]) {
                break;
            }
            offset -= plan->shifts[head];
        }
    }
    return -1;
}
//////////////////////////////////////////////////////////////////////////
ptrdiff_t tinypy_internal_find_bytes(const uint8_t *haystack, size_t haystack_size, const uint8_t *needle, size_t needle_size, tinypy_bool_t reverse) {
    tinypy_string_search_plan_t plan;

    __tinypy_string_search_plan_initialize(&plan, needle, needle_size, reverse);
    ptrdiff_t return_value_1 = __tinypy_string_search_plan_find(&plan, haystack, haystack_size);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_string_search_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    size_t length;
    int64_t start;
    int64_t end;
    size_t byte_start;
    size_t byte_end;
    ptrdiff_t found;
    intptr_t mode = (intptr_t)user_data;

    if (__tinypy_string_method_arguments(vm, args, kwargs, 2U, 4U, INT32_C(0), out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *text = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *needle = TINYPY_TUPLE_GET(args, 1U);
    if (__tinypy_string_require_text(vm, needle, "substring must be a string", out_error) == 0) {
        return NULL;
    }
    if (__tinypy_string_require_compatible(vm, text, needle, out_error) == 0) {
        return NULL;
    }
    length = __tinypy_string_character_count(text);
    start = __tinypy_string_optional_bound(vm, args, 2U, length, 0, TINYPY_FALSE, out_error);
    if (start == INT64_MIN && out_error != NULL && *out_error != NULL) {
        return NULL;
    }
    end = __tinypy_string_optional_bound(vm, args, 3U, length, (int64_t)length, TINYPY_TRUE, out_error);
    if (end == INT64_MIN && out_error != NULL && *out_error != NULL) {
        return NULL;
    }
    if ((uint64_t)start > (uint64_t)length || start > end) {
        found = -1;
    }
    else {
        byte_start = __tinypy_string_byte_offset(text, (size_t)start);
        byte_end = __tinypy_string_byte_offset(text, (size_t)end);
        const uint8_t *bytes = TINYPY_TEXT_BYTES(text);
        const uint8_t *bytes_2 = TINYPY_TEXT_BYTES(needle);
        size_t byte_size = TINYPY_TEXT_BYTE_SIZE(needle);
        found = tinypy_internal_find_bytes(bytes + byte_start, byte_end - byte_start, bytes_2, byte_size, (tinypy_bool_t)(mode & 1));
        if (found >= 0) {
            found = (ptrdiff_t)__tinypy_string_character_index(text, byte_start + (size_t)found);
        }
    }
    if (found < 0 && mode >= 2) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "substring not found", out_error);
        return NULL;
    }
    tinypy_value_t *return_value_1 = tinypy_integer_from_i64(vm, (int64_t)found);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_string_matches_at(const tinypy_value_t *text, size_t begin, size_t end, const tinypy_value_t *candidate, tinypy_bool_t suffix) {
    const uint8_t *bytes = TINYPY_TEXT_BYTES(text);
    const uint8_t *candidate_bytes = TINYPY_TEXT_BYTES(candidate);
    size_t candidate_size = TINYPY_TEXT_BYTE_SIZE(candidate);

    if (candidate_size > end - begin) {
        return TINYPY_FALSE;
    }
    if (suffix != 0) {
        begin = end - candidate_size;
    }
    tinypy_bool_t return_value_1 = memcmp(bytes + begin, candidate_bytes, candidate_size) == 0 ? TINYPY_TRUE : TINYPY_FALSE;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_string_prefix_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    size_t length;
    int64_t start;
    int64_t end;
    size_t begin;
    size_t finish;
    tinypy_bool_t invalid_bounds;
    tinypy_bool_t suffix = user_data != NULL ? TINYPY_TRUE : TINYPY_FALSE;

    if (__tinypy_string_method_arguments(vm, args, kwargs, 2U, 4U, INT32_C(0), out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *text = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *candidate = TINYPY_TUPLE_GET(args, 1U);
    length = __tinypy_string_character_count(text);
    start = __tinypy_string_optional_bound(vm, args, 2U, length, 0, TINYPY_FALSE, out_error);
    if (start == INT64_MIN && out_error != NULL && *out_error != NULL) {
        return NULL;
    }
    end = __tinypy_string_optional_bound(vm, args, 3U, length, (int64_t)length, TINYPY_TRUE, out_error);
    if (end == INT64_MIN && out_error != NULL && *out_error != NULL) {
        return NULL;
    }
    invalid_bounds = (uint64_t)start > (uint64_t)length || start > end ? TINYPY_TRUE : TINYPY_FALSE;
    begin = invalid_bounds == 0 ? __tinypy_string_byte_offset(text, (size_t)start) : 0U;
    finish = invalid_bounds == 0 ? __tinypy_string_byte_offset(text, (size_t)end) : 0U;
    if (TINYPY_VALUE_KIND(candidate) == TINYPY_VALUE_TUPLE) {
        tinypy_value_t *const *iterator = TINYPY_TUPLE_ITERATOR_BEGIN(candidate);
        tinypy_value_t *const *iterator_end = TINYPY_TUPLE_ITERATOR_END(candidate);

        for (; iterator != iterator_end; ++iterator) {
            tinypy_value_t *item = *iterator;

            if (__tinypy_string_require_text(vm, item, "prefix tuple contains a non-string", out_error) == 0) {
                return NULL;
            }
            if (__tinypy_string_require_compatible(vm, text, item, out_error) == 0) {
                return NULL;
            }
            if ((TINYPY_TEXT_BYTE_SIZE(item) == 0U && (TINYPY_VALUE_KIND(text) == TINYPY_VALUE_UNICODE || TINYPY_VALUE_KIND(item) == TINYPY_VALUE_UNICODE)) || (invalid_bounds == 0 && __tinypy_string_matches_at(text, begin, finish, item, suffix) != 0)) {
                tinypy_value_t *return_value_2 = tinypy_bool_from_i32(vm, INT32_C(1));
                return return_value_2;
            }
        }
        tinypy_value_t *return_value_3 = tinypy_bool_from_i32(vm, INT32_C(0));
        return return_value_3;
    }
    if (__tinypy_string_require_text(vm, candidate, "prefix must be a string or tuple", out_error) == 0) {
        return NULL;
    }
    if (__tinypy_string_require_compatible(vm, text, candidate, out_error) == 0) {
        return NULL;
    }
    if (TINYPY_TEXT_BYTE_SIZE(candidate) == 0U && (TINYPY_VALUE_KIND(text) == TINYPY_VALUE_UNICODE || TINYPY_VALUE_KIND(candidate) == TINYPY_VALUE_UNICODE)) {
        tinypy_value_t *return_value_4 = tinypy_bool_from_i32(vm, INT32_C(1));
        return return_value_4;
    }
    if (invalid_bounds != 0) {
        tinypy_value_t *return_value_5 = tinypy_bool_from_i32(vm, INT32_C(0));
        return return_value_5;
    }
    tinypy_bool_t string_matches_at = __tinypy_string_matches_at(text, begin, finish, candidate, suffix);
    tinypy_value_t *return_value_6 = tinypy_bool_from_i32(vm, string_matches_at);
    return return_value_6;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_string_count_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    size_t length;
    int64_t start;
    int64_t end;
    size_t begin;
    size_t finish;
    size_t needle_size;
    size_t offset;
    size_t count = 0U;

    (void)user_data;
    if (__tinypy_string_method_arguments(vm, args, kwargs, 2U, 4U, INT32_C(0), out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *text = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *needle = TINYPY_TUPLE_GET(args, 1U);
    if (__tinypy_string_require_text(vm, needle, "substring must be a string", out_error) == 0) {
        return NULL;
    }
    if (__tinypy_string_require_compatible(vm, text, needle, out_error) == 0) {
        return NULL;
    }
    length = __tinypy_string_character_count(text);
    start = __tinypy_string_optional_bound(vm, args, 2U, length, 0, TINYPY_FALSE, out_error);
    if (start == INT64_MIN && out_error != NULL && *out_error != NULL) {
        return NULL;
    }
    end = __tinypy_string_optional_bound(vm, args, 3U, length, (int64_t)length, TINYPY_TRUE, out_error);
    if (end == INT64_MIN && out_error != NULL && *out_error != NULL) {
        return NULL;
    }
    if ((uint64_t)start > (uint64_t)length || start > end) {
        tinypy_value_t *return_value_1 = tinypy_integer_from_i64(vm, 0);
        return return_value_1;
    }
    begin = __tinypy_string_byte_offset(text, (size_t)start);
    finish = __tinypy_string_byte_offset(text, (size_t)end);
    needle_size = TINYPY_TEXT_BYTE_SIZE(needle);
    if (needle_size == 0U) {
        tinypy_value_t *return_value_2 = tinypy_integer_from_i64(vm, (int64_t)((size_t)(end - start) + 1U));
        return return_value_2;
    }
    offset = begin;
    while (offset + needle_size <= finish) {
        const uint8_t *bytes = TINYPY_TEXT_BYTES(text);
        const uint8_t *bytes_2 = TINYPY_TEXT_BYTES(needle);
        ptrdiff_t found = tinypy_internal_find_bytes(bytes + offset, finish - offset, bytes_2, needle_size, INT32_C(0));

        if (found < 0) {
            break;
        }
        count += 1U;
        offset += (size_t)found + needle_size;
    }
    tinypy_value_t *return_value_3 = tinypy_integer_from_i64(vm, (int64_t)count);
    return return_value_3;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_string_ascii_space(uint8_t character) {
    return character == (uint8_t)' ' || character == (uint8_t)'\t' || character == (uint8_t)'\n' || character == (uint8_t)'\r' || character == (uint8_t)'\v' || character == (uint8_t)'\f';
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_string_strip_contains(tinypy_value_t *characters, tinypy_bool_t unicode, uint32_t character) {
    size_t offset;

    if (characters == NULL || TINYPY_VALUE_KIND(characters) == TINYPY_VALUE_NONE) {
        tinypy_bool_t return_value_1 = unicode != 0 ? tinypy_internal_unicode_is_space(character) : __tinypy_string_ascii_space((uint8_t)character);
        return return_value_1;
    }
    offset = 0U;
    while (offset < TINYPY_TEXT_BYTE_SIZE(characters)) {
        uint32_t candidate;
        size_t width = __tinypy_string_next_code_point(characters, offset, &candidate);

        if (candidate == character) {
            return TINYPY_TRUE;
        }
        offset += width;
    }
    return TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_string_strip_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *characters = NULL;
    size_t begin = 0U;
    size_t end;
    intptr_t mode = (intptr_t)user_data;
    tinypy_bool_t unicode;
    tinypy_bool_t use_byte_table;
    uint8_t byte_table[256];

    if (__tinypy_string_method_arguments(vm, args, kwargs, 1U, 2U, INT32_C(0), out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *text = TINYPY_TUPLE_GET(args, 0U);
    if (TINYPY_TUPLE_SIZE(args) == 2U) {
        characters = TINYPY_TUPLE_GET(args, 1U);
        if (TINYPY_VALUE_KIND(characters) != TINYPY_VALUE_NONE && __tinypy_string_require_text(vm, characters, "strip characters must be a string", out_error) == 0) {
            return NULL;
        }
    }
    end = TINYPY_TEXT_BYTE_SIZE(text);
    unicode = TINYPY_VALUE_KIND(text) == TINYPY_VALUE_UNICODE || (characters != NULL && TINYPY_VALUE_KIND(characters) == TINYPY_VALUE_UNICODE);
    if (characters != NULL && TINYPY_VALUE_KIND(characters) != TINYPY_VALUE_NONE && __tinypy_string_require_compatible(vm, text, characters, out_error) == 0) {
        return NULL;
    }
    use_byte_table = unicode == 0 ? TINYPY_TRUE : TINYPY_FALSE;
    if (use_byte_table != 0) {
        (void)memset(byte_table, 0, sizeof(byte_table));
        if (characters == NULL || TINYPY_VALUE_KIND(characters) == TINYPY_VALUE_NONE) {
            byte_table[(uint8_t)' '] = 1U;
            byte_table[(uint8_t)'\t'] = 1U;
            byte_table[(uint8_t)'\n'] = 1U;
            byte_table[(uint8_t)'\r'] = 1U;
            byte_table[(uint8_t)'\v'] = 1U;
            byte_table[(uint8_t)'\f'] = 1U;
        }
        else {
            const uint8_t *character_bytes = TINYPY_TEXT_BYTES(characters);
            size_t character_size = TINYPY_TEXT_BYTE_SIZE(characters);
            size_t index;

            for (index = 0U; index < character_size; ++index) {
                byte_table[character_bytes[index]] = 1U;
            }
        }
    }
    while (mode <= 0 && begin < end) {
        uint32_t code_point;
        size_t scalar_size = __tinypy_string_next_code_point(text, begin, &code_point);
        tinypy_bool_t stripped = use_byte_table != 0 ? byte_table[(uint8_t)code_point] != 0U : __tinypy_string_strip_contains(characters, unicode, code_point);

        if (stripped == 0) {
            break;
        }
        begin += scalar_size;
    }
    while (mode >= 0 && end > begin) {
        uint32_t code_point;
        size_t scalar_size = __tinypy_string_previous_code_point(text, end, &code_point);
        tinypy_bool_t stripped = use_byte_table != 0 ? byte_table[(uint8_t)code_point] != 0U : __tinypy_string_strip_contains(characters, unicode, code_point);

        if (stripped == 0) {
            break;
        }
        end -= scalar_size;
    }
    tinypy_value_t *return_value_1 = __tinypy_string_from_span_as(vm, text, begin, end, unicode);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_string_replace_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *new_value;
    int64_t maximum = -1;
    const uint8_t *bytes;
    size_t size;
    size_t old_size;
    size_t new_size;
    size_t offset = 0U;
    size_t replaced = 0U;
    tinypy_bool_t unicode;
    tinypy_string_builder_t builder;
    tinypy_string_search_plan_t search_plan;

    (void)user_data;
    if (__tinypy_string_method_arguments(vm, args, kwargs, 3U, 4U, INT32_C(0), out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *text = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *old_value = TINYPY_TUPLE_GET(args, 1U);
    new_value = TINYPY_TUPLE_GET(args, 2U);
    if (__tinypy_string_require_text(vm, old_value, "replace argument must be a string", out_error) == 0 || __tinypy_string_require_text(vm, new_value, "replace argument must be a string", out_error) == 0) {
        return NULL;
    }
    tinypy_bool_t condition_4 = TINYPY_TUPLE_SIZE(args) == 4U;
    if (condition_4 != 0) {
        tinypy_value_t *item = TINYPY_TUPLE_GET(args, 3U);
        condition_4 = __tinypy_string_integer(vm, item, &maximum, out_error) == 0;
    }
    if (condition_4) {
        return NULL;
    }
    bytes = TINYPY_TEXT_BYTES(text);
    size = TINYPY_TEXT_BYTE_SIZE(text);
    old_size = TINYPY_TEXT_BYTE_SIZE(old_value);
    new_size = TINYPY_TEXT_BYTE_SIZE(new_value);
    __tinypy_string_search_plan_initialize(&search_plan, TINYPY_TEXT_BYTES(old_value), old_size, TINYPY_FALSE);
    unicode = TINYPY_VALUE_KIND(text) == TINYPY_VALUE_UNICODE || TINYPY_VALUE_KIND(old_value) == TINYPY_VALUE_UNICODE || TINYPY_VALUE_KIND(new_value) == TINYPY_VALUE_UNICODE;
    if (unicode != 0 && (tinypy_internal_text_ascii_compatible(vm, text, out_error) == 0 || tinypy_internal_text_ascii_compatible(vm, old_value, out_error) == 0 || tinypy_internal_text_ascii_compatible(vm, new_value, out_error) == 0)) {
        return NULL;
    }
    (void)memset(&builder, 0, sizeof(builder));
    builder.vm = vm;
    if (maximum == 0) {
        tinypy_value_t *return_value_1 = __tinypy_string_from_span_as(vm, text, 0U, size, unicode);
        return return_value_1;
    }
    if (old_size == 0U) {
        if (size == 0U && maximum >= 0) {
            tinypy_value_t *return_value_2 = __tinypy_string_from_span_as(vm, text, 0U, 0U, unicode);
            return return_value_2;
        }
        size_t positions = __tinypy_string_character_count(text) + 1U;
        size_t position;

        replaced = maximum < 0 || (uint64_t)maximum > positions ? positions : (size_t)maximum;
        if (new_size != 0U && replaced > (SIZE_MAX - size) / new_size) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "resulting string is too large", out_error);
            return NULL;
        }
        if (__tinypy_string_builder_allocate_exact(&builder, vm, unicode, size + replaced * new_size, unicode != 0 ? __tinypy_string_character_count(text) + replaced * __tinypy_string_character_count(new_value) : 0U, out_error) == 0) {
            return NULL;
        }
        replaced = 0U;

        for (position = 0U; position < positions; ++position) {
            size_t next = offset;

            if (position < positions - 1U) {
                next += TINYPY_VALUE_KIND(text) == TINYPY_VALUE_UNICODE ? __tinypy_string_utf8_width(bytes[offset]) : 1U;
            }

            if (maximum < 0 || (int64_t)replaced < maximum) {
                const uint8_t *bytes_2 = TINYPY_TEXT_BYTES(new_value);
                size_t byte_size = TINYPY_TEXT_BYTE_SIZE(new_value);
                __tinypy_string_builder_append(&builder, bytes_2, byte_size);
                replaced += 1U;
            }
            if (position < positions - 1U) {
                __tinypy_string_builder_append(&builder, bytes + offset, next - offset);
            }
            offset = next;
        }
        tinypy_value_t *return_value_3 = __tinypy_string_builder_finish(&builder, unicode, out_error);
        return return_value_3;
    }
    while (offset < size && (maximum < 0 || (int64_t)replaced < maximum)) {
        ptrdiff_t found = __tinypy_string_search_plan_find(&search_plan, bytes + offset, size - offset);

        if (found < 0) {
            break;
        }
        offset += (size_t)found + old_size;
        replaced += 1U;
    }
    if (replaced == 0U) {
        __tinypy_string_builder_discard(&builder);
        tinypy_value_t *return_value_4 = __tinypy_string_from_span_as(vm, text, 0U, size, unicode);
        return return_value_4;
    }
    size_t result_size;
    if (new_size >= old_size) {
        size_t growth = new_size - old_size;

        if (growth != 0U && replaced > (SIZE_MAX - size) / growth) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "resulting string is too large", out_error);
            return NULL;
        }
        result_size = size + replaced * growth;
    }
    else {
        result_size = size - replaced * (old_size - new_size);
    }
    size_t result_characters = 0U;
    if (unicode != 0) {
        size_t text_characters = __tinypy_string_character_count(text);
        size_t old_characters = __tinypy_string_character_count(old_value);
        size_t new_characters = __tinypy_string_character_count(new_value);

        if (new_characters >= old_characters) {
            result_characters = text_characters + replaced * (new_characters - old_characters);
        }
        else {
            result_characters = text_characters - replaced * (old_characters - new_characters);
        }
    }
    if (__tinypy_string_builder_allocate_exact(&builder, vm, unicode, result_size, result_characters, out_error) == 0) {
        return NULL;
    }
    offset = 0U;
    size_t remaining = replaced;
    while (remaining != 0U) {
        ptrdiff_t found = __tinypy_string_search_plan_find(&search_plan, bytes + offset, size - offset);

        __tinypy_string_builder_append(&builder, bytes + offset, (size_t)found);
        __tinypy_string_builder_append(&builder, TINYPY_TEXT_BYTES(new_value), new_size);
        offset += (size_t)found + old_size;
        remaining -= 1U;
    }
    __tinypy_string_builder_append(&builder, bytes + offset, size - offset);
    tinypy_value_t *return_value_5 = __tinypy_string_builder_finish(&builder, unicode, out_error);
    return return_value_5;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_string_list_append_span(tinypy_vm_t *vm, tinypy_value_t *list, tinypy_value_t *text, size_t begin, size_t end, tinypy_error_t **out_error) {
    tinypy_value_t *item = __tinypy_string_from_span(vm, text, begin, end);
    tinypy_bool_t result = tinypy_internal_list_append_checked(list, item, out_error);

    TINYPY_DECREF(item);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_string_list_append_span_as(tinypy_vm_t *vm, tinypy_value_t *list, tinypy_value_t *text, size_t begin, size_t end, tinypy_bool_t unicode, tinypy_error_t **out_error) {
    tinypy_value_t *item = __tinypy_string_from_span_as(vm, text, begin, end, unicode);
    tinypy_bool_t result = tinypy_internal_list_append_checked(list, item, out_error);

    TINYPY_DECREF(item);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_string_list_reverse(tinypy_value_t *list) {
    size_t left = 0U;
    size_t right = TINYPY_LIST_SIZE(list);

    while (left < right && left < --right) {
        tinypy_value_t *value = TINYPY_LIST_OBJECT(list)->items[left];

        TINYPY_LIST_OBJECT(list)->items[left] = TINYPY_LIST_OBJECT(list)->items[right];
        TINYPY_LIST_OBJECT(list)->items[right] = value;
        left += 1U;
    }
#if defined(TINYPY_CYCLE_DIAGNOSTICS)
    __tinypy_internal_cycle_diagnostics_list_reindex(TINYPY_VALUE_VM(list), list);
#endif
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_string_split_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *separator = NULL;
    int64_t maximum = -1;
    tinypy_bool_t reverse = user_data != NULL;
    const uint8_t *bytes;
    size_t size;
    size_t separator_size = 0U;
    size_t splits = 0U;
    tinypy_bool_t unicode;
    tinypy_string_search_plan_t search_plan;

    if (__tinypy_string_method_arguments(vm, args, kwargs, 1U, 3U, INT32_C(0), out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *text = TINYPY_TUPLE_GET(args, 0U);
    unicode = TINYPY_VALUE_KIND(text) == TINYPY_VALUE_UNICODE;
    tinypy_bool_t condition_5 = TINYPY_TUPLE_SIZE(args) >= 2U;
    if (condition_5 != 0) {
        tinypy_value_t *item = TINYPY_TUPLE_GET(args, 1U);
        condition_5 = TINYPY_VALUE_KIND(item) != TINYPY_VALUE_NONE;
    }
    if (condition_5) {
        separator = TINYPY_TUPLE_GET(args, 1U);
        if (__tinypy_string_require_text(vm, separator, "separator must be a string", out_error) == 0) {
            return NULL;
        }
        if (__tinypy_string_require_compatible(vm, text, separator, out_error) == 0) {
            return NULL;
        }
        if (TINYPY_VALUE_KIND(separator) == TINYPY_VALUE_UNICODE) {
            unicode = TINYPY_TRUE;
        }
        separator_size = TINYPY_TEXT_BYTE_SIZE(separator);
        if (separator_size == 0U) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "empty separator", out_error);
            return NULL;
        }
    }
    tinypy_bool_t condition_6 = TINYPY_TUPLE_SIZE(args) == 3U;
    if (condition_6 != 0) {
        tinypy_value_t *item = TINYPY_TUPLE_GET(args, 2U);
        condition_6 = __tinypy_string_integer(vm, item, &maximum, out_error) == 0;
    }
    if (condition_6) {
        return NULL;
    }
    tinypy_value_t *result = tinypy_list_from_items(vm, NULL, 0U);
    bytes = TINYPY_TEXT_BYTES(text);
    size = TINYPY_TEXT_BYTE_SIZE(text);
    if (separator != NULL) {
        __tinypy_string_search_plan_initialize(&search_plan, TINYPY_TEXT_BYTES(separator), separator_size, reverse);
    }
    if (separator == NULL) {
        if (reverse == 0) {
            size_t begin = 0U;

            while (begin < size) {
                uint32_t code_point;
                size_t width = __tinypy_string_next_code_point(text, begin, &code_point);
                tinypy_bool_t space = TINYPY_VALUE_KIND(text) == TINYPY_VALUE_UNICODE ? tinypy_internal_unicode_is_space(code_point) : __tinypy_string_ascii_space((uint8_t)code_point);

                if (space == 0) {
                    break;
                }
                begin += width;
            }
            while (begin < size) {
                size_t end;

                if (maximum >= 0 && (int64_t)splits >= maximum) {
                    if (__tinypy_string_list_append_span_as(vm, result, text, begin, size, unicode, out_error) == 0) {
                        TINYPY_DECREF(result);
                        return NULL;
                    }
                    return result;
                }
                end = begin;
                while (end < size) {
                    uint32_t code_point;
                    size_t width = __tinypy_string_next_code_point(text, end, &code_point);
                    tinypy_bool_t space = TINYPY_VALUE_KIND(text) == TINYPY_VALUE_UNICODE ? tinypy_internal_unicode_is_space(code_point) : __tinypy_string_ascii_space((uint8_t)code_point);

                    if (space != 0) {
                        break;
                    }
                    end += width;
                }
                if (__tinypy_string_list_append_span_as(vm, result, text, begin, end, unicode, out_error) == 0) {
                    TINYPY_DECREF(result);
                    return NULL;
                }
                splits += 1U;
                begin = end;
                while (begin < size) {
                    uint32_t code_point;
                    size_t width = __tinypy_string_next_code_point(text, begin, &code_point);
                    tinypy_bool_t space = TINYPY_VALUE_KIND(text) == TINYPY_VALUE_UNICODE ? tinypy_internal_unicode_is_space(code_point) : __tinypy_string_ascii_space((uint8_t)code_point);

                    if (space == 0) {
                        break;
                    }
                    begin += width;
                }
            }
        }
        else {
            size_t end = size;

            while (end != 0U) {
                uint32_t code_point;
                size_t width = __tinypy_string_previous_code_point(text, end, &code_point);
                tinypy_bool_t space = TINYPY_VALUE_KIND(text) == TINYPY_VALUE_UNICODE ? tinypy_internal_unicode_is_space(code_point) : __tinypy_string_ascii_space((uint8_t)code_point);

                if (space == 0) {
                    break;
                }
                end -= width;
            }
            while (end != 0U) {
                size_t begin;

                if (maximum >= 0 && (int64_t)splits >= maximum) {
                    if (__tinypy_string_list_append_span_as(vm, result, text, 0U, end, unicode, out_error) == 0) {
                        TINYPY_DECREF(result);
                        return NULL;
                    }
                    break;
                }
                begin = end;
                while (begin != 0U) {
                    uint32_t code_point;
                    size_t width = __tinypy_string_previous_code_point(text, begin, &code_point);
                    tinypy_bool_t space = TINYPY_VALUE_KIND(text) == TINYPY_VALUE_UNICODE ? tinypy_internal_unicode_is_space(code_point) : __tinypy_string_ascii_space((uint8_t)code_point);

                    if (space != 0) {
                        break;
                    }
                    begin -= width;
                }
                if (__tinypy_string_list_append_span_as(vm, result, text, begin, end, unicode, out_error) == 0) {
                    TINYPY_DECREF(result);
                    return NULL;
                }
                splits += 1U;
                end = begin;
                while (end != 0U) {
                    uint32_t code_point;
                    size_t width = __tinypy_string_previous_code_point(text, end, &code_point);
                    tinypy_bool_t space = TINYPY_VALUE_KIND(text) == TINYPY_VALUE_UNICODE ? tinypy_internal_unicode_is_space(code_point) : __tinypy_string_ascii_space((uint8_t)code_point);

                    if (space == 0) {
                        break;
                    }
                    end -= width;
                }
            }
            __tinypy_string_list_reverse(result);
        }
        return result;
    }
    if (reverse == 0) {
        size_t begin = 0U;

        while (begin <= size) {
            ptrdiff_t found;

            if (maximum >= 0 && (int64_t)splits >= maximum) {
                if (__tinypy_string_list_append_span_as(vm, result, text, begin, size, unicode, out_error) == 0) {
                    TINYPY_DECREF(result);
                    return NULL;
                }
                break;
            }
            found = __tinypy_string_search_plan_find(&search_plan, bytes + begin, size - begin);
            if (found < 0) {
                if (__tinypy_string_list_append_span_as(vm, result, text, begin, size, unicode, out_error) == 0) {
                    TINYPY_DECREF(result);
                    return NULL;
                }
                break;
            }
            if (__tinypy_string_list_append_span_as(vm, result, text, begin, begin + (size_t)found, unicode, out_error) == 0) {
                TINYPY_DECREF(result);
                return NULL;
            }
            begin += (size_t)found + separator_size;
            splits += 1U;
        }
    }
    else {
        size_t end = size;

        for (;;) {
            ptrdiff_t found;

            if (maximum >= 0 && (int64_t)splits >= maximum) {
                if (__tinypy_string_list_append_span_as(vm, result, text, 0U, end, unicode, out_error) == 0) {
                    TINYPY_DECREF(result);
                    return NULL;
                }
                break;
            }
            found = __tinypy_string_search_plan_find(&search_plan, bytes, end);
            if (found < 0) {
                if (__tinypy_string_list_append_span_as(vm, result, text, 0U, end, unicode, out_error) == 0) {
                    TINYPY_DECREF(result);
                    return NULL;
                }
                break;
            }
            if (__tinypy_string_list_append_span_as(vm, result, text, (size_t)found + separator_size, end, unicode, out_error) == 0) {
                TINYPY_DECREF(result);
                return NULL;
            }
            end = (size_t)found;
            splits += 1U;
        }
        __tinypy_string_list_reverse(result);
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_string_translate_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *delete_characters = NULL;
    const uint8_t *source;
    const uint8_t *translation;
    const uint8_t *deleted = NULL;
    size_t source_size;
    size_t translation_size;
    size_t deleted_size = 0U;
    uint8_t *output;
    size_t input_index;
    size_t output_size = 0U;
    uint8_t deleted_flags[32] = {0U};

    (void)user_data;
    if (__tinypy_string_method_arguments(vm, args, kwargs, 2U, 3U, INT32_C(0), out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *text = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *table = TINYPY_TUPLE_GET(args, 1U);
    if (TINYPY_VALUE_KIND(text) != TINYPY_VALUE_STRING || (TINYPY_VALUE_KIND(table) != TINYPY_VALUE_STRING && TINYPY_VALUE_KIND(table) != TINYPY_VALUE_NONE)) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "translate requires byte strings", out_error);
        return NULL;
    }
    translation = NULL;
    if (TINYPY_VALUE_KIND(table) == TINYPY_VALUE_STRING) {
        translation = TINYPY_TEXT_BYTES(table);
        translation_size = TINYPY_TEXT_BYTE_SIZE(table);
        if (translation_size != 256U) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "translation table must be 256 characters long", out_error);
            return NULL;
        }
    }
    if (TINYPY_TUPLE_SIZE(args) == 3U) {
        delete_characters = TINYPY_TUPLE_GET(args, 2U);
        if (TINYPY_VALUE_KIND(delete_characters) != TINYPY_VALUE_STRING) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "delete characters must be a string", out_error);
            return NULL;
        }
        deleted = TINYPY_TEXT_BYTES(delete_characters);
        deleted_size = TINYPY_TEXT_BYTE_SIZE(delete_characters);
        for (input_index = 0U; input_index < deleted_size; ++input_index) {
            deleted_flags[deleted[input_index] >> 3U] |= (uint8_t)(1U << (deleted[input_index] & 7U));
        }
    }
    source = TINYPY_TEXT_BYTES(text);
    source_size = TINYPY_TEXT_BYTE_SIZE(text);
    if (source_size == 0U) {
        tinypy_value_t *return_value_1 = tinypy_string_from_bytes(vm, NULL, 0U);
        return return_value_1;
    }
    for (input_index = 0U; input_index < source_size; ++input_index) {
        uint8_t character = source[input_index];
        if ((deleted_flags[character >> 3U] & (uint8_t)(1U << (character & 7U))) == 0U) {
            output_size += 1U;
        }
    }
    if (output_size == 0U) {
        tinypy_value_t *return_value_1 = tinypy_string_from_bytes(vm, NULL, 0U);
        return return_value_1;
    }
    tinypy_value_t *result = tinypy_internal_text_allocate_uninitialized_checked(vm, TINYPY_VALUE_STRING, output_size, output_size, &output, out_error);

    if (result == NULL) {
        return NULL;
    }
    output_size = 0U;
    for (input_index = 0U; input_index < source_size; ++input_index) {
        uint8_t character = source[input_index];
        if ((deleted_flags[character >> 3U] & (uint8_t)(1U << (character & 7U))) == 0U) {
            output[output_size++] = translation != NULL ? translation[character] : character;
        }
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_unicode_translate_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *text;
    tinypy_value_t *table;
    tinypy_string_builder_t builder;
    size_t offset = 0U;

    (void)user_data;
    if (__tinypy_string_method_arguments(vm, args, kwargs, 2U, 2U, INT32_C(0), out_error) == 0) {
        return NULL;
    }
    text = TINYPY_TUPLE_GET(args, 0U);
    table = TINYPY_TUPLE_GET(args, 1U);
    (void)memset(&builder, 0, sizeof(builder));
    builder.vm = vm;
    while (offset < TINYPY_TEXT_BYTE_SIZE(text)) {
        uint32_t code_point;
        size_t width = __tinypy_string_next_code_point(text, offset, &code_point);
        tinypy_value_t *key = tinypy_integer_from_i64(vm, (int64_t)code_point);
        tinypy_error_t *lookup_error = NULL;
        tinypy_value_t *replacement = tinypy_get_item(table, key, &lookup_error);

        TINYPY_DECREF(key);
        if (replacement == NULL) {
            if (lookup_error != NULL && tinypy_error_kind(lookup_error) == TINYPY_ERROR_KEY) {
                tinypy_error_release(lookup_error);
                __tinypy_string_builder_append(&builder, TINYPY_TEXT_BYTES(text) + offset, width);
                offset += width;
                continue;
            }
            __tinypy_string_builder_discard(&builder);
            if (out_error != NULL) {
                *out_error = lookup_error;
            }
            else if (lookup_error != NULL) {
                tinypy_error_release(lookup_error);
            }
            return NULL;
        }
        if (TINYPY_VALUE_KIND(replacement) == TINYPY_VALUE_NONE) {
            TINYPY_DECREF(replacement);
            offset += width;
            continue;
        }
        if (TINYPY_VALUE_KIND(replacement) == TINYPY_VALUE_UNICODE) {
            __tinypy_string_builder_append(&builder, TINYPY_TEXT_BYTES(replacement), TINYPY_TEXT_BYTE_SIZE(replacement));
        }
        else {
            int64_t mapped;

            if (tinypy_internal_index_as_i64(replacement, &mapped, TINYPY_FALSE, out_error) == 0 || mapped < 0 || mapped > INT64_C(0x10ffff)) {
                TINYPY_DECREF(replacement);
                __tinypy_string_builder_discard(&builder);
                if (out_error == NULL || *out_error == NULL) {
                    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "character mapping must return integer, None or unicode", out_error);
                }
                return NULL;
            }
            __tinypy_string_builder_code_point(&builder, (uint32_t)mapped);
        }
        TINYPY_DECREF(replacement);
        offset += width;
    }
    tinypy_value_t *return_value_1 = __tinypy_string_builder_finish(&builder, TINYPY_TRUE, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_string_case_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_string_builder_t builder;
    size_t size;
    size_t offset;
    intptr_t mode = (intptr_t)user_data;
    int32_t word_start = INT32_C(1);

    if (__tinypy_string_method_arguments(vm, args, kwargs, 1U, 1U, INT32_C(0), out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *text = TINYPY_TUPLE_GET(args, 0U);
    size = TINYPY_TEXT_BYTE_SIZE(text);
    (void)memset(&builder, 0, sizeof(builder));
    builder.vm = vm;
    offset = 0U;
    while (offset < size) {
        uint32_t character;
        size_t scalar_size = __tinypy_string_next_code_point(text, offset, &character);
        tinypy_bool_t cased;
        uint32_t mapped = character;

        if (TINYPY_VALUE_KIND(text) == TINYPY_VALUE_STRING) {
            tinypy_bool_t lower = character >= (uint32_t)'a' && character <= (uint32_t)'z';
            tinypy_bool_t upper = character >= (uint32_t)'A' && character <= (uint32_t)'Z';

            cased = lower != 0 || upper != 0;
            if (mode == 0 || (mode == 2 && upper != 0) || ((mode == 3 || mode == 4) && word_start == 0)) {
                mapped = upper != 0 ? character + (uint32_t)('a' - 'A') : character;
            }
            else if (mode == 1 || (mode == 2 && lower != 0) || ((mode == 3 || mode == 4) && word_start != 0)) {
                mapped = lower != 0 ? character - (uint32_t)('a' - 'A') : character;
            }
            __tinypy_string_builder_character(&builder, (uint8_t)mapped);
        }
        else {
            cased = tinypy_internal_unicode_is_cased(character);
            if (mode == 0) {
                mapped = tinypy_internal_unicode_lower(character);
            }
            else if (mode == 1) {
                mapped = tinypy_internal_unicode_upper(character);
            }
            else if (mode == 2) {
                if (tinypy_internal_unicode_is_lower(character) != 0) {
                    mapped = tinypy_internal_unicode_upper(character);
                }
                else if (tinypy_internal_unicode_is_upper(character) != 0) {
                    mapped = tinypy_internal_unicode_lower(character);
                }
            }
            else if (word_start != 0) {
                mapped = mode == 3 ? tinypy_internal_unicode_upper(character) : tinypy_internal_unicode_title(character);
            }
            else {
                mapped = tinypy_internal_unicode_lower(character);
            }
            __tinypy_string_builder_code_point(&builder, mapped);
        }
        if (mode == 3) {
            word_start = INT32_C(0);
        }
        else if (mode == 4) {
            word_start = cased == 0;
        }
        offset += scalar_size;
    }
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(text);
    tinypy_value_t *return_value_1 = __tinypy_string_builder_finish(&builder, kind == TINYPY_VALUE_UNICODE, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_string_predicate_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    size_t size;
    size_t offset;
    intptr_t mode = (intptr_t)user_data;
    int32_t result = INT32_C(1);
    int32_t cased = INT32_C(0);
    int32_t word_start = INT32_C(1);

    if (__tinypy_string_method_arguments(vm, args, kwargs, 1U, 1U, INT32_C(0), out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *text = TINYPY_TUPLE_GET(args, 0U);
    size = TINYPY_TEXT_BYTE_SIZE(text);
    if (size == 0U) {
        tinypy_value_t *return_value_1 = tinypy_bool_from_i32(vm, INT32_C(0));
        return return_value_1;
    }
    offset = 0U;
    while (offset < size && result != 0) {
        uint32_t character;
        size_t scalar_size = __tinypy_string_next_code_point(text, offset, &character);
        int32_t lower;
        int32_t upper;
        int32_t title;
        int32_t digit;
        int32_t alpha;
        int32_t alnum;
        int32_t space;
        int32_t decimal = INT32_C(0);
        int32_t numeric = INT32_C(0);

        if (TINYPY_VALUE_KIND(text) == TINYPY_VALUE_UNICODE) {
            lower = tinypy_internal_unicode_is_lower(character);
            upper = tinypy_internal_unicode_is_upper(character);
            title = tinypy_internal_unicode_is_title(character);
            digit = tinypy_internal_unicode_is_digit(character);
            alpha = tinypy_internal_unicode_is_alpha(character);
            alnum = tinypy_internal_unicode_is_alnum(character);
            space = tinypy_internal_unicode_is_space(character);
            decimal = tinypy_internal_unicode_is_decimal(character);
            numeric = tinypy_internal_unicode_is_numeric(character);
        }
        else {
            lower = character >= (uint32_t)'a' && character <= (uint32_t)'z';
            upper = character >= (uint32_t)'A' && character <= (uint32_t)'Z';
            title = INT32_C(0);
            digit = character >= (uint32_t)'0' && character <= (uint32_t)'9';
            alpha = lower != 0 || upper != 0;
            alnum = alpha != 0 || digit != 0;
            space = __tinypy_string_ascii_space((uint8_t)character);
        }

        if (mode == 0) {
            result = alpha;
        }
        else if (mode == 1) {
            result = digit;
        }
        else if (mode == 2) {
            result = alnum;
        }
        else if (mode == 3) {
            result = space;
        }
        else if (mode == 4) {
            if (upper != 0 || title != 0) {
                result = INT32_C(0);
            }
            if (lower != 0 || upper != 0 || title != 0) {
                cased = INT32_C(1);
            }
        }
        else if (mode == 5) {
            if (lower != 0 || title != 0) {
                result = INT32_C(0);
            }
            if (lower != 0 || upper != 0 || title != 0) {
                cased = INT32_C(1);
            }
        }
        else if (mode == 6) {
            if (lower != 0 || upper != 0 || title != 0) {
                if ((word_start != 0 && upper == 0 && title == 0) || (word_start == 0 && lower == 0)) {
                    result = INT32_C(0);
                }
                word_start = INT32_C(0);
                cased = INT32_C(1);
            }
            else {
                word_start = INT32_C(1);
            }
        }
        else if (mode == 7) {
            result = decimal;
        }
        else {
            result = numeric;
        }
        offset += scalar_size;
    }
    if ((mode == 4 || mode == 5 || mode == 6) && cased == 0) {
        result = INT32_C(0);
    }
    tinypy_value_t *return_value_2 = tinypy_bool_from_i32(vm, result);
    return return_value_2;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_string_zfill_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    int64_t width;
    size_t size;
    size_t byte_size;
    size_t padding;
    tinypy_string_builder_t builder;

    (void)user_data;
    if (__tinypy_string_method_arguments(vm, args, kwargs, 2U, 2U, INT32_C(0), out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *item = TINYPY_TUPLE_GET(args, 1U);
    if (__tinypy_string_integer(vm, item, &width, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *text = TINYPY_TUPLE_GET(args, 0U);
    size = __tinypy_string_character_count(text);
    byte_size = TINYPY_TEXT_BYTE_SIZE(text);
    if (width <= 0 || (uint64_t)width <= size) {
        tinypy_value_t *return_value_1 = __tinypy_string_from_span(vm, text, 0U, byte_size);
        return return_value_1;
    }
    padding = (size_t)width - size;
    (void)memset(&builder, 0, sizeof(builder));
    builder.vm = vm;
    if (byte_size != 0U && (TINYPY_TEXT_BYTES(text)[0] == (uint8_t)'+' || TINYPY_TEXT_BYTES(text)[0] == (uint8_t)'-')) {
        const uint8_t *bytes = TINYPY_TEXT_BYTES(text);
        __tinypy_string_builder_character(&builder, bytes[0]);
        __tinypy_string_builder_repeat(&builder, (const uint8_t *)"0", 1U, padding);
        const uint8_t *bytes_2 = TINYPY_TEXT_BYTES(text);
        __tinypy_string_builder_append(&builder, bytes_2 + 1U, byte_size - 1U);
    }
    else {
        __tinypy_string_builder_repeat(&builder, (const uint8_t *)"0", 1U, padding);
        const uint8_t *bytes = TINYPY_TEXT_BYTES(text);
        __tinypy_string_builder_append(&builder, bytes, byte_size);
    }
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(text);
    tinypy_value_t *return_value_2 = __tinypy_string_builder_finish(&builder, kind == TINYPY_VALUE_UNICODE, out_error);
    return return_value_2;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_string_splitlines_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    size_t size;
    size_t begin = 0U;
    size_t offset = 0U;
    int32_t keep_ends = INT32_C(0);

    (void)user_data;
    if (__tinypy_string_method_arguments(vm, args, kwargs, 1U, 2U, INT32_C(0), out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *text = TINYPY_TUPLE_GET(args, 0U);
    if (TINYPY_TUPLE_SIZE(args) == 2U) {
        tinypy_value_t *item = TINYPY_TUPLE_GET(args, 1U);
        keep_ends = tinypy_truth(item, out_error);
        if (keep_ends < 0) {
            return NULL;
        }
    }
    tinypy_value_t *result = tinypy_list_from_items(vm, NULL, 0U);
    size = TINYPY_TEXT_BYTE_SIZE(text);
    if (TINYPY_VALUE_KIND(text) == TINYPY_VALUE_STRING) {
        const uint8_t *bytes = TINYPY_STRING_OBJECT(text)->bytes;

        while (offset < size) {
            size_t content_end;
            size_t line_end;

            while (offset < size && bytes[offset] != (uint8_t)'\r' && bytes[offset] != (uint8_t)'\n') {
                offset += 1U;
            }
            if (offset == size) {
                break;
            }
            content_end = offset;
            if (bytes[offset++] == (uint8_t)'\r' && offset < size && bytes[offset] == (uint8_t)'\n') {
                offset += 1U;
            }
            line_end = keep_ends != 0 ? offset : content_end;
            if (__tinypy_string_list_append_span(vm, result, text, begin, line_end, out_error) == 0) {
                TINYPY_DECREF(result);
                return NULL;
            }
            begin = offset;
        }
        if (begin < size) {
            if (__tinypy_string_list_append_span(vm, result, text, begin, size, out_error) == 0) {
                TINYPY_DECREF(result);
                return NULL;
            }
        }
        return result;
    }
    while (offset < size) {
        size_t content_end;
        size_t line_end;
        uint32_t code_point = 0U;
        size_t width = 0U;

        while (offset < size) {
            width = __tinypy_string_next_code_point(text, offset, &code_point);
            if (tinypy_internal_unicode_is_linebreak(code_point) != 0) {
                break;
            }
            offset += width;
        }
        if (offset == size) {
            break;
        }
        content_end = offset;
        offset += width;
        if (code_point == (uint32_t)'\r' && offset < size) {
            uint32_t next_code_point;
            size_t next_width = __tinypy_string_next_code_point(text, offset, &next_code_point);

            if (next_code_point == (uint32_t)'\n') {
                offset += next_width;
            }
        }
        line_end = keep_ends != 0 ? offset : content_end;
        if (__tinypy_string_list_append_span(vm, result, text, begin, line_end, out_error) == 0) {
            TINYPY_DECREF(result);
            return NULL;
        }
        begin = offset;
    }
    if (begin < size) {
        if (__tinypy_string_list_append_span(vm, result, text, begin, size, out_error) == 0) {
            TINYPY_DECREF(result);
            return NULL;
        }
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_string_expandtabs_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    int64_t tab_size = 8;
    size_t column = 0U;
    size_t offset;
    tinypy_bool_t unicode;
    tinypy_string_builder_t builder;

    (void)user_data;
    if (__tinypy_string_method_arguments(vm, args, kwargs, 1U, 2U, INT32_C(0), out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *text = TINYPY_TUPLE_GET(args, 0U);
    tinypy_bool_t condition_8 = TINYPY_TUPLE_SIZE(args) == 2U;
    if (condition_8 != 0) {
        tinypy_value_t *item = TINYPY_TUPLE_GET(args, 1U);
        condition_8 = __tinypy_string_integer(vm, item, &tab_size, out_error) == 0;
    }
    if (condition_8) {
        return NULL;
    }
    unicode = TINYPY_VALUE_KIND(text) == TINYPY_VALUE_UNICODE ? TINYPY_TRUE : TINYPY_FALSE;
    (void)memset(&builder, 0, sizeof(builder));
    builder.vm = vm;
    for (offset = 0U; offset < TINYPY_TEXT_BYTE_SIZE(text); ++offset) {
        uint8_t character = TINYPY_TEXT_BYTES(text)[offset];

        if (character == (uint8_t)'\t') {
            size_t spaces = tab_size > 0 ? (size_t)tab_size - column % (size_t)tab_size : 0U;

            while (spaces-- != 0U) {
                __tinypy_string_builder_character(&builder, (uint8_t)' ');
                column += 1U;
            }
        }
        else {
            __tinypy_string_builder_character(&builder, character);
            if (character == (uint8_t)'\n' || character == (uint8_t)'\r') {
                column = 0U;
            }
            else if (unicode == 0 || (character & 0xc0U) != 0x80U) {
                column += 1U;
            }
        }
    }
    tinypy_value_t *return_value_1 = __tinypy_string_builder_finish(&builder, unicode, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_string_partition_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *items[3];
    tinypy_value_t *result;
    ptrdiff_t found;
    size_t size;
    size_t separator_size;
    tinypy_bool_t reverse = user_data != NULL;
    tinypy_bool_t unicode;

    if (__tinypy_string_method_arguments(vm, args, kwargs, 2U, 2U, INT32_C(0), out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *text = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *separator = TINYPY_TUPLE_GET(args, 1U);
    if (__tinypy_string_require_text(vm, separator, "separator must be a string", out_error) == 0) {
        return NULL;
    }
    if (__tinypy_string_require_compatible(vm, text, separator, out_error) == 0) {
        return NULL;
    }
    unicode = TINYPY_VALUE_KIND(text) == TINYPY_VALUE_UNICODE || TINYPY_VALUE_KIND(separator) == TINYPY_VALUE_UNICODE;
    separator_size = TINYPY_TEXT_BYTE_SIZE(separator);
    if (separator_size == 0U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "empty separator", out_error);
        return NULL;
    }
    size = TINYPY_TEXT_BYTE_SIZE(text);
    const uint8_t *bytes = TINYPY_TEXT_BYTES(text);
    const uint8_t *bytes_2 = TINYPY_TEXT_BYTES(separator);
    found = tinypy_internal_find_bytes(bytes, size, bytes_2, separator_size, reverse);
    if (found < 0) {
        if (reverse == 0) {
            items[0] = __tinypy_string_from_span_as(vm, text, 0U, size, unicode);
            items[1] = __tinypy_string_from_span_as(vm, text, 0U, 0U, unicode);
            items[2] = __tinypy_string_from_span_as(vm, text, 0U, 0U, unicode);
        }
        else {
            items[0] = __tinypy_string_from_span_as(vm, text, 0U, 0U, unicode);
            items[1] = __tinypy_string_from_span_as(vm, text, 0U, 0U, unicode);
            items[2] = __tinypy_string_from_span_as(vm, text, 0U, size, unicode);
        }
    }
    else {
        items[0] = __tinypy_string_from_span_as(vm, text, 0U, (size_t)found, unicode);
        items[1] = __tinypy_string_from_span_as(vm, separator, 0U, separator_size, unicode);
        items[2] = __tinypy_string_from_span_as(vm, text, (size_t)found + separator_size, size, unicode);
    }
    result = tinypy_tuple_from_items(vm, items, 3U);
    TINYPY_DECREF(items[2]);
    TINYPY_DECREF(items[1]);
    TINYPY_DECREF(items[0]);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_codec_name_equal(const tinypy_value_t *name, const char *canonical) {
    const uint8_t *bytes = TINYPY_TEXT_BYTES(name);
    size_t size = TINYPY_TEXT_BYTE_SIZE(name);
    size_t offset = 0U;
    size_t canonical_offset = 0U;

    while (offset < size || canonical[canonical_offset] != '\0') {
        uint8_t character;

        while (offset < size && (bytes[offset] == (uint8_t)'-' || bytes[offset] == (uint8_t)'_' || bytes[offset] == (uint8_t)' ' || bytes[offset] == (uint8_t)'.')) {
            offset += 1U;
        }
        while (canonical[canonical_offset] == '-' || canonical[canonical_offset] == '_' || canonical[canonical_offset] == ' ' || canonical[canonical_offset] == '.') {
            canonical_offset += 1U;
        }
        if (offset == size || canonical[canonical_offset] == '\0') {
            return offset == size && canonical[canonical_offset] == '\0';
        }
        character = bytes[offset++];
        if (character >= (uint8_t)'A' && character <= (uint8_t)'Z') {
            character = (uint8_t)(character + ('a' - 'A'));
        }
        if (character != (uint8_t)canonical[canonical_offset++]) {
            return TINYPY_FALSE;
        }
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_codec_error_name_equal(const tinypy_value_t *name, const char *expected, size_t expected_size) {
    if (TINYPY_TEXT_BYTE_SIZE(name) != expected_size) {
        return TINYPY_FALSE;
    }
    int32_t equal = expected_size == 0U ? 0 : memcmp(TINYPY_TEXT_BYTES(name), expected, expected_size);
    tinypy_bool_t return_value_1 = equal == 0 ? TINYPY_TRUE : TINYPY_FALSE;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_codec_error_mode(tinypy_vm_t *vm, tinypy_value_t *value, tinypy_error_t **out_error) {
    if (value == NULL || __tinypy_codec_error_name_equal(value, "strict", 6U) != 0) {
        return 0;
    }
    if (__tinypy_codec_error_name_equal(value, "ignore", 6U) != 0) {
        return 1;
    }
    if (__tinypy_codec_error_name_equal(value, "replace", 7U) != 0) {
        return 2;
    }
    if (__tinypy_codec_error_name_equal(value, "xmlcharrefreplace", 17U) != 0) {
        return 3;
    }
    if (__tinypy_codec_error_name_equal(value, "backslashreplace", 16U) != 0) {
        return 4;
    }
    tinypy_value_t *handler = tinypy_internal_codecs_lookup_error(vm, value, out_error);

    if (handler == NULL) {
        return -1;
    }
    TINYPY_DECREF(handler);
    return 5;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_codec_unicode_exception(tinypy_vm_t *vm, tinypy_value_t *text, tinypy_value_t *encoding, int32_t codec, tinypy_bool_t decode, size_t start, size_t end, tinypy_error_t **out_error) {
    const char *canonical = codec == 0 ? "ascii" : (codec == 1 ? "utf-8" : "latin-1");
    size_t canonical_size = codec == 0 ? 5U : (codec == 1 ? 5U : 7U);
    const char *reason_text = decode != 0 ? (codec == 0 ? "ordinal not in range(128)" : "invalid start byte") : (codec == 0 ? "ordinal not in range(128)" : "ordinal not in range(256)");
    size_t reason_size = decode != 0 ? (codec == 0 ? 25U : 18U) : (codec == 0 ? 25U : 25U);
    tinypy_value_t *encoding_value = encoding != NULL ? tinypy_string_from_bytes(vm, TINYPY_TEXT_BYTES(encoding), TINYPY_TEXT_BYTE_SIZE(encoding)) : tinypy_string_from_bytes(vm, canonical, canonical_size);
    tinypy_value_t *start_value = tinypy_integer_from_i64(vm, (int64_t)start);
    tinypy_value_t *end_value = tinypy_integer_from_i64(vm, (int64_t)end);
    tinypy_value_t *reason = tinypy_string_from_bytes(vm, reason_text, reason_size);
    tinypy_value_t *items[5] = {encoding_value, text, start_value, end_value, reason};
    tinypy_value_t *args = tinypy_tuple_from_items(vm, items, 5U);
    tinypy_value_t *exception = tinypy_exception_new(vm->exception_types[decode != 0 ? TINYPY_EXCEPTION_UNICODE_DECODE_ERROR : TINYPY_EXCEPTION_UNICODE_ENCODE_ERROR], args, out_error);

    TINYPY_DECREF(args);
    TINYPY_DECREF(reason);
    TINYPY_DECREF(end_value);
    TINYPY_DECREF(start_value);
    TINYPY_DECREF(encoding_value);
    return exception;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_codec_raise_unicode_error(tinypy_vm_t *vm, tinypy_value_t *text, tinypy_value_t *encoding, int32_t codec, tinypy_bool_t decode, size_t start, size_t end, tinypy_error_t **out_error) {
    tinypy_value_t *exception = __tinypy_codec_unicode_exception(vm, text, encoding, codec, decode, start, end, out_error);

    if (exception == NULL) {
        return TINYPY_FALSE;
    }
    (void)tinypy_exception_raise(exception, NULL, out_error);
    TINYPY_DECREF(exception);
    return TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_codec_append_encoded_replacement(tinypy_string_builder_t *builder, tinypy_value_t *replacement, int32_t codec, tinypy_error_t **out_error) {
    const uint8_t *bytes = TINYPY_TEXT_BYTES(replacement);
    size_t size = TINYPY_TEXT_BYTE_SIZE(replacement);
    size_t offset = 0U;

    while (offset != size) {
        uint32_t code_point;
        size_t width = tinypy_internal_utf8_decode(bytes + offset, size - offset, &code_point);

        if (width == 0U || (codec == 0 && code_point > UINT32_C(0x7f)) || (codec == 2 && code_point > UINT32_C(0xff))) {
            tinypy_internal_make_vm_error(builder->vm, TINYPY_ERROR_UNICODE_ENCODE, "error handler replacement is not encodable", out_error);
            return TINYPY_FALSE;
        }
        if (codec == 1) {
            __tinypy_string_builder_append(builder, bytes + offset, width);
        }
        else {
            __tinypy_string_builder_character(builder, (uint8_t)code_point);
        }
        offset += width;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_codec_call_error_handler(tinypy_vm_t *vm, tinypy_string_builder_t *builder, tinypy_value_t *text, tinypy_value_t *encoding, tinypy_value_t *errors, int32_t codec, tinypy_bool_t decode, size_t start, size_t end, size_t *out_next, tinypy_error_t **out_error) {
    tinypy_value_t *exception = __tinypy_codec_unicode_exception(vm, text, encoding, codec, decode, start, end, out_error);
    tinypy_value_t *handler;
    tinypy_value_t *handler_args;
    tinypy_value_t *result;
    tinypy_value_t *replacement;
    tinypy_value_t *position;
    int64_t next;
    size_t input_length = decode != 0 ? TINYPY_TEXT_BYTE_SIZE(text) : TINYPY_SIZED_SIZE(text);

    if (exception == NULL) {
        return TINYPY_FALSE;
    }
    handler = tinypy_internal_codecs_lookup_error(vm, errors, out_error);
    if (handler == NULL) {
        TINYPY_DECREF(exception);
        return TINYPY_FALSE;
    }
    handler_args = tinypy_tuple_from_items(vm, &exception, 1U);
    result = tinypy_call(handler, handler_args, NULL, out_error);
    TINYPY_DECREF(handler_args);
    TINYPY_DECREF(handler);
    TINYPY_DECREF(exception);
    if (result == NULL) {
        return TINYPY_FALSE;
    }
    if (TINYPY_VALUE_KIND(result) != TINYPY_VALUE_TUPLE || TINYPY_TUPLE_SIZE(result) != 2U) {
        TINYPY_DECREF(result);
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "encoding error handler must return (unicode, int) tuple", out_error);
        return TINYPY_FALSE;
    }
    replacement = TINYPY_TUPLE_GET(result, 0U);
    position = TINYPY_TUPLE_GET(result, 1U);
    if (TINYPY_VALUE_KIND(replacement) != TINYPY_VALUE_UNICODE ||
        (TINYPY_VALUE_KIND(position) != TINYPY_VALUE_BOOL && TINYPY_VALUE_KIND(position) != TINYPY_VALUE_INTEGER && TINYPY_VALUE_KIND(position) != TINYPY_VALUE_LONG) ||
        tinypy_internal_index_as_i64(position, &next, TINYPY_FALSE, out_error) == 0) {
        TINYPY_DECREF(result);
        if (out_error == NULL || *out_error == NULL) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "encoding error handler must return (unicode, int) tuple", out_error);
        }
        return TINYPY_FALSE;
    }
    if (next < 0) {
        next += (int64_t)input_length;
    }
    if (next < 0 || (uint64_t)next > (uint64_t)input_length) {
        TINYPY_DECREF(result);
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_INDEX, "position from error handler is out of bounds", out_error);
        return TINYPY_FALSE;
    }
    if (decode != 0) {
        __tinypy_string_builder_append(builder, TINYPY_TEXT_BYTES(replacement), TINYPY_TEXT_BYTE_SIZE(replacement));
        *out_next = (size_t)next;
    }
    else {
        if (__tinypy_codec_append_encoded_replacement(builder, replacement, codec, out_error) == 0) {
            TINYPY_DECREF(result);
            return TINYPY_FALSE;
        }
        *out_next = tinypy_internal_unicode_byte_offset(text, (size_t)next);
    }
    TINYPY_DECREF(result);
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_codec_append_escape(tinypy_string_builder_t *builder, uint32_t code_point, tinypy_bool_t xml) {
    char buffer[32];
    int size;

    if (xml != 0) {
        size = snprintf(buffer, sizeof(buffer), "&#%" PRIu32 ";", code_point);
    }
    else if (code_point <= UINT32_C(0xff)) {
        size = snprintf(buffer, sizeof(buffer), "\\x%02" PRIx32, code_point);
    }
    else if (code_point <= UINT32_C(0xffff)) {
        size = snprintf(buffer, sizeof(buffer), "\\u%04" PRIx32, code_point);
    }
    else {
        size = snprintf(buffer, sizeof(buffer), "\\U%08" PRIx32, code_point);
    }
    __tinypy_string_builder_append(builder, buffer, (size_t)size);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_utf8_append(tinypy_string_builder_t *builder, uint32_t code_point) {
    __tinypy_string_builder_code_point(builder, code_point);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_string_codec_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *encoding = NULL;
    tinypy_value_t *errors = NULL;
    tinypy_bool_t decode = user_data != NULL;
    int32_t codec = 0;
    int32_t error_mode;
    tinypy_string_builder_t builder;
    const uint8_t *bytes;
    size_t size;
    size_t offset = 0U;

    if (__tinypy_string_method_arguments(vm, args, kwargs, 1U, 3U, INT32_C(0), out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *text = TINYPY_TUPLE_GET(args, 0U);
    if (TINYPY_TUPLE_SIZE(args) >= 2U) {
        encoding = TINYPY_TUPLE_GET(args, 1U);
        if (__tinypy_string_require_text(vm, encoding, "encoding name must be a string", out_error) == 0) {
            return NULL;
        }
    }
    if (TINYPY_TUPLE_SIZE(args) == 3U) {
        errors = TINYPY_TUPLE_GET(args, 2U);
        if (__tinypy_string_require_text(vm, errors, "error handler name must be a string", out_error) == 0) {
            return NULL;
        }
    }
    if (encoding == NULL || __tinypy_codec_name_equal(encoding, "ascii") != 0 || __tinypy_codec_name_equal(encoding, "646") != 0 || __tinypy_codec_name_equal(encoding, "usascii") != 0 || __tinypy_codec_name_equal(encoding, "iso646us") != 0 || __tinypy_codec_name_equal(encoding, "ansix341968") != 0) {
        codec = 0;
    }
    else if (__tinypy_codec_name_equal(encoding, "utf8") != 0 || __tinypy_codec_name_equal(encoding, "utf-8") != 0 || __tinypy_codec_name_equal(encoding, "u8") != 0 || __tinypy_codec_name_equal(encoding, "utf") != 0) {
        codec = 1;
    }
    else if (__tinypy_codec_name_equal(encoding, "latin1") != 0 || __tinypy_codec_name_equal(encoding, "iso88591") != 0 || __tinypy_codec_name_equal(encoding, "cp819") != 0 || __tinypy_codec_name_equal(encoding, "l1") != 0) {
        codec = 2;
    }
    else {
        tinypy_value_t *return_value_1 = tinypy_internal_codecs_transform_registered(vm, text, encoding, errors, decode, out_error);

        if (return_value_1 != NULL && __tinypy_string_is_text(return_value_1) == 0) {
            TINYPY_DECREF(return_value_1);
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, decode != 0 ? "decoder did not return a string/unicode object" : "encoder did not return a string/unicode object", out_error);
            return NULL;
        }
        return return_value_1;
    }
    error_mode = errors == NULL ? 0 : -2;
    (void)memset(&builder, 0, sizeof(builder));
    builder.vm = vm;
    bytes = TINYPY_TEXT_BYTES(text);
    size = TINYPY_TEXT_BYTE_SIZE(text);
    if (decode != 0) {
        if (TINYPY_VALUE_KIND(text) == TINYPY_VALUE_UNICODE) {
            while (offset < size) {
                if (bytes[offset] >= 0x80U) {
                    __tinypy_string_builder_discard(&builder);
                    size_t character = tinypy_internal_unicode_character_index(text, offset);

                    (void)__tinypy_codec_raise_unicode_error(vm, text, NULL, 0, TINYPY_FALSE, character, character + 1U, out_error);
                    return NULL;
                }
                offset += 1U;
            }
            offset = 0U;
        }
        if (codec == 2) {
            while (offset < size) {
                __tinypy_utf8_append(&builder, bytes[offset++]);
            }
        }
        else if (codec == 0) {
            while (offset < size) {
                if (bytes[offset] < 0x80U) {
                    __tinypy_string_builder_character(&builder, bytes[offset]);
                }
                else {
                    size_t error_end = offset + 1U;

                    if (error_mode < 0) {
                        error_mode = __tinypy_codec_error_mode(vm, errors, out_error);
                        if (error_mode < 0) {
                            __tinypy_string_builder_discard(&builder);
                            return NULL;
                        }
                    }
                    if (error_mode == 2) {
                        __tinypy_utf8_append(&builder, UINT32_C(0xfffd));
                    }
                    else if (error_mode == 5) {
                        if (__tinypy_codec_call_error_handler(vm, &builder, text, encoding, errors, codec, TINYPY_TRUE, offset, error_end, &offset, out_error) == 0) {
                            __tinypy_string_builder_discard(&builder);
                            return NULL;
                        }
                        continue;
                    }
                    else if (error_mode == 3 || error_mode == 4) {
                        __tinypy_string_builder_discard(&builder);
                        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "error handler does not support UnicodeDecodeError", out_error);
                        return NULL;
                    }
                    else if (error_mode == 0) {
                        __tinypy_string_builder_discard(&builder);
                        (void)__tinypy_codec_raise_unicode_error(vm, text, encoding, codec, TINYPY_TRUE, offset, error_end, out_error);
                        return NULL;
                    }
                }
                offset += 1U;
            }
        }
        else {
            while (offset < size) {
                uint32_t code_point;
                size_t width = tinypy_internal_utf8_decode(bytes + offset, size - offset, &code_point);

                if (width != 0U) {
                    __tinypy_string_builder_append(&builder, bytes + offset, width);
                }
                else {
                    size_t invalid_size = tinypy_internal_utf8_invalid_span(bytes + offset, size - offset);
                    size_t error_end = offset + invalid_size;

                    if (error_mode < 0) {
                        error_mode = __tinypy_codec_error_mode(vm, errors, out_error);
                        if (error_mode < 0) {
                            __tinypy_string_builder_discard(&builder);
                            return NULL;
                        }
                    }
                    if (error_mode == 2) {
                        __tinypy_utf8_append(&builder, UINT32_C(0xfffd));
                    }
                    else if (error_mode == 5) {
                        if (__tinypy_codec_call_error_handler(vm, &builder, text, encoding, errors, codec, TINYPY_TRUE, offset, error_end, &offset, out_error) == 0) {
                            __tinypy_string_builder_discard(&builder);
                            return NULL;
                        }
                        continue;
                    }
                    else if (error_mode == 3 || error_mode == 4) {
                        __tinypy_string_builder_discard(&builder);
                        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "error handler does not support UnicodeDecodeError", out_error);
                        return NULL;
                    }
                    else if (error_mode == 0) {
                        __tinypy_string_builder_discard(&builder);
                        (void)__tinypy_codec_raise_unicode_error(vm, text, encoding, codec, TINYPY_TRUE, offset, error_end, out_error);
                        return NULL;
                    }
                }
                offset += width != 0U ? width : tinypy_internal_utf8_invalid_span(bytes + offset, size - offset);
            }
        }
        tinypy_value_t *return_value_2 = __tinypy_string_builder_finish(&builder, INT32_C(1), out_error);
        return return_value_2;
    }
    if (TINYPY_VALUE_KIND(text) == TINYPY_VALUE_STRING) {
        while (offset < size) {
            if (bytes[offset] >= 0x80U) {
                __tinypy_string_builder_discard(&builder);
                (void)__tinypy_codec_raise_unicode_error(vm, text, NULL, 0, TINYPY_TRUE, offset, offset + 1U, out_error);
                return NULL;
            }
            offset += 1U;
        }
        offset = 0U;
    }
    while (offset < size) {
        uint32_t code_point;
        size_t width = tinypy_internal_utf8_decode(bytes + offset, size - offset, &code_point);

        if (width == 0U) {
            __tinypy_string_builder_discard(&builder);
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_UNICODE_ENCODE, "invalid internal unicode value", out_error);
            return NULL;
        }
        if (codec == 1) {
            if (code_point >= UINT32_C(0xd800) && code_point <= UINT32_C(0xdbff)) {
                uint32_t next_code_point;
                size_t next_width = tinypy_internal_utf8_decode(bytes + offset + width, size - offset - width, &next_code_point);

                if (next_width != 0U && next_code_point >= UINT32_C(0xdc00) && next_code_point <= UINT32_C(0xdfff)) {
                    uint32_t combined = UINT32_C(0x10000) + ((code_point - UINT32_C(0xd800)) << 10U) + (next_code_point - UINT32_C(0xdc00));

                    __tinypy_utf8_append(&builder, combined);
                    offset += width + next_width;
                    continue;
                }
            }
            __tinypy_string_builder_append(&builder, bytes + offset, width);
        }
        else if ((codec == 0 && code_point <= 0x7fU) || (codec == 2 && code_point <= 0xffU)) {
            __tinypy_string_builder_character(&builder, (uint8_t)code_point);
        }
        else {
            size_t character = tinypy_internal_unicode_character_index(text, offset);

            if (error_mode < 0) {
                error_mode = __tinypy_codec_error_mode(vm, errors, out_error);
                if (error_mode < 0) {
                    __tinypy_string_builder_discard(&builder);
                    return NULL;
                }
            }
            if (error_mode == 2) {
                __tinypy_string_builder_character(&builder, (uint8_t)'?');
            }
            else if (error_mode == 3 || error_mode == 4) {
                __tinypy_codec_append_escape(&builder, code_point, error_mode == 3 ? TINYPY_TRUE : TINYPY_FALSE);
            }
            else if (error_mode == 5) {
                if (__tinypy_codec_call_error_handler(vm, &builder, text, encoding, errors, codec, TINYPY_FALSE, character, character + 1U, &offset, out_error) == 0) {
                    __tinypy_string_builder_discard(&builder);
                    return NULL;
                }
                continue;
            }
            else if (error_mode == 0) {
                __tinypy_string_builder_discard(&builder);
                (void)__tinypy_codec_raise_unicode_error(vm, text, encoding, codec, TINYPY_FALSE, character, character + 1U, out_error);
                return NULL;
            }
        }
        offset += width;
    }
    tinypy_value_t *return_value_4 = __tinypy_string_builder_finish(&builder, INT32_C(0), out_error);
    return return_value_4;
}

typedef struct tinypy_percent_arguments_t {
    tinypy_value_t *value;
    size_t index;
    size_t consumed;
} tinypy_percent_arguments_t;

//////////////////////////////////////////////////////////////////////////
static void __tinypy_percent_unsigned(tinypy_string_builder_t *builder, uint64_t value, uint32_t base, tinypy_bool_t uppercase, size_t minimum_digits) {
    uint8_t reverse[64];
    size_t count = 0U;

    do {
        uint32_t digit = (uint32_t)(value % base);

        reverse[count++] = digit < 10U ? (uint8_t)('0' + digit) : (uint8_t)((uppercase != 0 ? 'A' : 'a') + digit - 10U);
        value /= base;
    } while (value != 0U);
    if (count < minimum_digits) {
        __tinypy_string_builder_repeat(builder, (const uint8_t *)"0", 1U, minimum_digits - count);
    }
    while (count != 0U) {
        __tinypy_string_builder_character(builder, reverse[--count]);
    }
}
//////////////////////////////////////////////////////////////////////////
static size_t __tinypy_percent_u32_decimal_digits(uint32_t value) {
    size_t digits = 1U;

    while (value >= UINT32_C(10)) {
        value /= UINT32_C(10);
        digits += 1U;
    }
    return digits;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_percent_long_power_of_two(tinypy_string_builder_t *builder, const tinypy_value_t *value, uint32_t base, tinypy_bool_t uppercase, size_t minimum_digits) {
    const uint16_t *digits = TINYPY_LONG_OBJECT(value)->digits;
    size_t digit_count = TINYPY_LONG_DIGIT_COUNT(value);
    uint32_t bits_per_digit = base == 2U ? 1U : (base == 8U ? 3U : 4U);
    uint32_t mask = base - 1U;
    uint16_t high = digits[digit_count - 1U];
    size_t bit_length = (digit_count - 1U) * 15U;
    size_t output_digits;
    size_t index;

    while (high != 0U) {
        high >>= 1U;
        bit_length += 1U;
    }
    output_digits = (bit_length + bits_per_digit - 1U) / bits_per_digit;
    if (output_digits < minimum_digits) {
        __tinypy_string_builder_repeat(builder, (const uint8_t *)"0", 1U, minimum_digits - output_digits);
    }
    for (index = output_digits; index != 0U; --index) {
        size_t bit_index = (index - 1U) * bits_per_digit;
        size_t word_index = bit_index / 15U;
        uint32_t bit_shift = (uint32_t)(bit_index % 15U);
        uint32_t digit = (uint32_t)digits[word_index] >> bit_shift;

        if (bit_shift + bits_per_digit > 15U && word_index + 1U < digit_count) {
            digit |= (uint32_t)digits[word_index + 1U] << (15U - bit_shift);
        }
        digit &= mask;
        __tinypy_string_builder_character(builder, digit < 10U ? (uint8_t)('0' + digit) : (uint8_t)((uppercase != 0 ? 'A' : 'a') + digit - 10U));
    }
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_percent_long_decimal(tinypy_string_builder_t *builder, const tinypy_value_t *value, size_t minimum_digits) {
    size_t digit_count = TINYPY_LONG_DIGIT_COUNT(value);
    size_t word_count = (digit_count + 1U) / 2U;
    size_t chunk_capacity = (digit_count / 29U) * 15U + ((digit_count % 29U) * 15U + 28U) / 29U;
    size_t allocation_size;
    uint32_t *scratch;
    uint32_t *work;
    uint32_t *chunks;
    size_t chunk_count = 0U;
    size_t active;
    size_t output_digits;
    size_t index;

    if (chunk_capacity > SIZE_MAX - word_count || word_count + chunk_capacity > SIZE_MAX / sizeof(uint32_t)) {
        builder->failed = TINYPY_TRUE;
        return;
    }
    allocation_size = (word_count + chunk_capacity) * sizeof(uint32_t);
    scratch = (uint32_t *)tinypy_internal_vm_allocate_checked(builder->vm, allocation_size, NULL);
    if (scratch == NULL) {
        builder->failed = TINYPY_TRUE;
        builder->memory_failed = TINYPY_TRUE;
        return;
    }
    work = scratch;
    chunks = scratch + word_count;
    for (index = 0U; index < word_count; ++index) {
        size_t digit_index = index * 2U;

        work[index] = (uint32_t)TINYPY_LONG_OBJECT(value)->digits[digit_index];
        if (digit_index + 1U < digit_count) {
            work[index] |= (uint32_t)TINYPY_LONG_OBJECT(value)->digits[digit_index + 1U] << 15U;
        }
    }
    active = word_count;
    while (active != 0U) {
        uint64_t remainder = 0U;

        index = active;

        while (index != 0U) {
            uint64_t current;

            index -= 1U;
            current = (remainder << 30U) | work[index];
            work[index] = (uint32_t)(current / UINT64_C(1000000000));
            remainder = current % UINT64_C(1000000000);
        }
        chunks[chunk_count++] = (uint32_t)remainder;
        while (active != 0U && work[active - 1U] == 0U) {
            active -= 1U;
        }
    }
    output_digits = __tinypy_percent_u32_decimal_digits(chunks[chunk_count - 1U]) + (chunk_count - 1U) * 9U;
    __tinypy_string_builder_reserve(builder, output_digits < minimum_digits ? minimum_digits : output_digits);
    if (output_digits < minimum_digits) {
        __tinypy_string_builder_repeat(builder, (const uint8_t *)"0", 1U, minimum_digits - output_digits);
    }
    __tinypy_percent_unsigned(builder, chunks[chunk_count - 1U], 10U, TINYPY_FALSE, 1U);
    for (index = chunk_count - 1U; index != 0U; --index) {
        __tinypy_percent_unsigned(builder, chunks[index - 1U], 10U, TINYPY_FALSE, 9U);
    }
    tinypy_internal_vm_deallocate(builder->vm, scratch, allocation_size);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_percent_long_digits(tinypy_string_builder_t *builder, const tinypy_value_t *value, uint32_t base, tinypy_bool_t uppercase, size_t minimum_digits) {
    if (TINYPY_LONG_DIGIT_COUNT(value) == 0U) {
        __tinypy_percent_unsigned(builder, UINT64_C(0), base, uppercase, minimum_digits);
        return;
    }
    if (base == 10U) {
        __tinypy_percent_long_decimal(builder, value, minimum_digits);
        return;
    }
    __tinypy_percent_long_power_of_two(builder, value, base, uppercase, minimum_digits);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_percent_append_integer(tinypy_vm_t *vm, tinypy_string_builder_t *builder, tinypy_value_t *value, uint8_t conversion, int32_t alternate, int32_t plus, int32_t space, int64_t precision, tinypy_bool_t new_format, size_t *out_prefix_size, tinypy_error_t **out_error) {
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);
    tinypy_value_t *owned_value = NULL;
    int32_t negative = INT32_C(0);
    uint32_t base = conversion == (uint8_t)'b' ? 2U : (conversion == (uint8_t)'o' ? 8U : ((conversion == (uint8_t)'x' || conversion == (uint8_t)'X') ? 16U : 10U));
    tinypy_bool_t uppercase = conversion == (uint8_t)'X';
    size_t minimum_digits = precision >= 0 ? (size_t)precision : 1U;
    tinypy_bool_t zero_value;
    tinypy_bool_t suppress_zero_digit;
    int64_t float_integer = INT64_C(0);

    if (kind != TINYPY_VALUE_BOOL && kind != TINYPY_VALUE_INTEGER && kind != TINYPY_VALUE_LONG && kind != TINYPY_VALUE_FLOAT) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "integer format requires a number", out_error);
        return TINYPY_FALSE;
    }
    if (kind == TINYPY_VALUE_FLOAT) {
        double number = TINYPY_FLOAT_OBJECT(value)->value;

        if (isfinite(number) == 0) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "integer format requires a finite number", out_error);
            return TINYPY_FALSE;
        }
        if (number < -0x1p63 || number >= 0x1p63) {
            owned_value = tinypy_internal_long_from_double(vm, number);
            value = owned_value;
            kind = TINYPY_VALUE_LONG;
        }
        else {
            float_integer = (int64_t)number;
        }
    }
    if (new_format == 0 && kind != TINYPY_VALUE_LONG && precision > 116) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "formatted integer is too long", out_error);
        return TINYPY_FALSE;
    }
    if (kind == TINYPY_VALUE_LONG) {
        negative = TINYPY_LONG_SIGN(value) < 0;
    }
    else if (kind == TINYPY_VALUE_FLOAT) {
        negative = float_integer < 0;
    }
    else {
        negative = TINYPY_INTEGER_VALUE(value) < 0;
    }
    zero_value = kind == TINYPY_VALUE_LONG ? (TINYPY_LONG_DIGIT_COUNT(value) == 0U ? TINYPY_TRUE : TINYPY_FALSE) : (kind == TINYPY_VALUE_FLOAT ? (float_integer == 0 ? TINYPY_TRUE : TINYPY_FALSE) : (TINYPY_INTEGER_VALUE(value) == 0 ? TINYPY_TRUE : TINYPY_FALSE));
    suppress_zero_digit = precision == 0 && zero_value != 0 && !(new_format == 0 && alternate != 0 && base == 8U);
    if (negative != 0) {
        __tinypy_string_builder_character(builder, (uint8_t)'-');
    }
    else if (plus != 0) {
        __tinypy_string_builder_character(builder, (uint8_t)'+');
    }
    else if (space != 0) {
        __tinypy_string_builder_character(builder, (uint8_t)' ');
    }
    if (alternate != 0 && base == 8U) {
        if (new_format != 0) {
            __tinypy_string_builder_append(builder, "0o", 2U);
        }
        else if (zero_value == 0) {
            __tinypy_string_builder_character(builder, (uint8_t)'0');
            if (minimum_digits != 0U) {
                minimum_digits -= 1U;
            }
        }
    }
    else if (alternate != 0 && base == 2U) {
        __tinypy_string_builder_append(builder, "0b", 2U);
    }
    else if (alternate != 0 && base == 16U) {
        __tinypy_string_builder_append(builder, uppercase != 0 ? "0X" : "0x", 2U);
    }
    *out_prefix_size = builder->size;
    if (suppress_zero_digit != 0) {
        if (owned_value != NULL) {
            TINYPY_DECREF(owned_value);
        }
        return TINYPY_TRUE;
    }
    if (kind == TINYPY_VALUE_LONG) {
        __tinypy_percent_long_digits(builder, value, base, uppercase, minimum_digits);
    }
    else {
        uint64_t magnitude;
        int64_t integer;

        if (kind == TINYPY_VALUE_FLOAT) {
            integer = float_integer;
        }
        else {
            integer = TINYPY_INTEGER_VALUE(value);
        }
        magnitude = integer < 0 ? (uint64_t)(-(integer + INT64_C(1))) + UINT64_C(1) : (uint64_t)integer;
        __tinypy_percent_unsigned(builder, magnitude, base, uppercase, minimum_digits);
    }
    if (owned_value != NULL) {
        TINYPY_DECREF(owned_value);
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static size_t __tinypy_percent_normalize_decimal(uint8_t *bytes, size_t size) {
    size_t decimal_begin = 0U;
    size_t exponent;
    size_t decimal_end;

    while (decimal_begin < size && bytes[decimal_begin] >= (uint8_t)'0' && bytes[decimal_begin] <= (uint8_t)'9') {
        decimal_begin += 1U;
    }
    if (decimal_begin == 0U || decimal_begin == size || bytes[decimal_begin] == (uint8_t)'e' || bytes[decimal_begin] == (uint8_t)'E') {
        return size;
    }
    exponent = decimal_begin;
    while (exponent < size && bytes[exponent] != (uint8_t)'e' && bytes[exponent] != (uint8_t)'E') {
        exponent += 1U;
    }
    decimal_end = decimal_begin;
    while (decimal_end < exponent && (bytes[decimal_end] < (uint8_t)'0' || bytes[decimal_end] > (uint8_t)'9')) {
        decimal_end += 1U;
    }
    if (decimal_end == decimal_begin) {
        return size;
    }
    if (decimal_end > decimal_begin + 1U) {
        (void)memmove(bytes + decimal_begin + 1U, bytes + decimal_end, size - decimal_end);
        size -= decimal_end - decimal_begin - 1U;
    }
    bytes[decimal_begin] = (uint8_t)'.';
    return size;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_percent_float_text(tinypy_vm_t *vm, tinypy_string_builder_t *builder, double magnitude, uint8_t conversion, tinypy_bool_t alternate, size_t precision, tinypy_error_t **out_error) {
    uint8_t local[128];
    char format[7];
    size_t format_size = 0U;
    int required;
    int written;

    if (precision > (size_t)INT_MAX) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "float format precision is too large", out_error);
        return TINYPY_FALSE;
    }
    format[format_size++] = '%';
    if (alternate != 0) {
        format[format_size++] = '#';
    }
    format[format_size++] = '.';
    format[format_size++] = '*';
    format[format_size++] = (char)conversion;
    format[format_size] = '\0';
    required = snprintf((char *)local, sizeof(local), format, (int)precision, magnitude);
    if (required < 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "float formatting failed", out_error);
        return TINYPY_FALSE;
    }
    if ((size_t)required < sizeof(local)) {
        size_t normalized_size = __tinypy_percent_normalize_decimal(local, (size_t)required);

        __tinypy_string_builder_append(builder, local, normalized_size);
        return TINYPY_TRUE;
    }
    size_t begin = builder->size;

    __tinypy_string_builder_reserve(builder, (size_t)required + 1U);
    if (builder->failed != 0) {
        tinypy_internal_make_vm_error(vm, builder->memory_failed != 0 ? TINYPY_ERROR_MEMORY : TINYPY_ERROR_OVERFLOW, builder->memory_failed != 0 ? "memory allocation failed" : "resulting string is too large", out_error);
        return TINYPY_FALSE;
    }
    written = snprintf((char *)builder->bytes + begin, builder->capacity - begin, format, (int)precision, magnitude);
    if (written != required) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "float formatting failed", out_error);
        return TINYPY_FALSE;
    }
    builder->size = begin + __tinypy_percent_normalize_decimal(builder->bytes + begin, (size_t)written);
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_percent_append_double(tinypy_vm_t *vm, tinypy_string_builder_t *builder, double number, uint8_t conversion, int32_t alternate, int32_t plus, int32_t space, int64_t precision_value, size_t *out_prefix_size, tinypy_error_t **out_error) {
    size_t precision = precision_value >= 0 ? (size_t)precision_value : 6U;
    tinypy_bool_t uppercase = conversion == (uint8_t)'E' || conversion == (uint8_t)'F' || conversion == (uint8_t)'G';
    uint8_t lower = (uint8_t)(conversion >= (uint8_t)'A' && conversion <= (uint8_t)'Z' ? conversion + ('a' - 'A') : conversion);
    tinypy_bool_t percentage = lower == (uint8_t)'%';

    if (percentage != 0) {
        number *= 100.0;
        lower = (uint8_t)'f';
    }
    if (isnan(number) == 0 && signbit(number) != 0) {
        __tinypy_string_builder_character(builder, (uint8_t)'-');
    }
    else if (plus != 0) {
        __tinypy_string_builder_character(builder, (uint8_t)'+');
    }
    else if (space != 0) {
        __tinypy_string_builder_character(builder, (uint8_t)' ');
    }
    *out_prefix_size = builder->size;
    if (precision > 100000U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "float format precision is too large", out_error);
        return TINYPY_FALSE;
    }
    if (__tinypy_percent_float_text(vm, builder, fabs(number), uppercase != 0 ? (uint8_t)(lower - ('a' - 'A')) : lower, alternate != 0 ? TINYPY_TRUE : TINYPY_FALSE, precision, out_error) == 0) {
        return TINYPY_FALSE;
    }
    if (percentage != 0) {
        __tinypy_string_builder_character(builder, (uint8_t)'%');
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_percent_append_float(tinypy_vm_t *vm, tinypy_string_builder_t *builder, tinypy_value_t *value, uint8_t conversion, int32_t alternate, int32_t plus, int32_t space, int64_t precision_value, tinypy_bool_t long_overflow_type_error, size_t *out_prefix_size, tinypy_error_t **out_error) {
    double number;

    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_FLOAT) {
        number = TINYPY_FLOAT_OBJECT(value)->value;
    }
    else if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_BOOL || TINYPY_VALUE_KIND(value) == TINYPY_VALUE_INTEGER) {
        number = (double)TINYPY_INTEGER_VALUE(value);
    }
    else if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_LONG) {
        if (tinypy_internal_long_as_double(value, &number, out_error) == 0) {
            if (long_overflow_type_error != 0) {
                if (out_error != NULL && *out_error != NULL) {
                    tinypy_error_release(*out_error);
                    *out_error = NULL;
                }
                tinypy_internal_exception_clear_raised(vm);
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "float format requires a number", out_error);
            }
            return TINYPY_FALSE;
        }
    }
    else {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "float format requires a number", out_error);
        return TINYPY_FALSE;
    }
    tinypy_bool_t return_value_1 = __tinypy_percent_append_double(vm, builder, number, conversion, alternate, plus, space, precision_value, out_prefix_size, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_percent_next_argument(tinypy_vm_t *vm, tinypy_percent_arguments_t *arguments, tinypy_error_t **out_error) {
    tinypy_value_t *value;

    if (TINYPY_VALUE_KIND(arguments->value) == TINYPY_VALUE_TUPLE) {
        if (arguments->index >= TINYPY_TUPLE_SIZE(arguments->value)) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "not enough arguments for format string", out_error);
            return NULL;
        }
        value = TINYPY_TUPLE_GET(arguments->value, arguments->index++);
    }
    else {
        if (arguments->consumed != 0U) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "not enough arguments for format string", out_error);
            return NULL;
        }
        value = arguments->value;
        arguments->consumed = 1U;
    }
    TINYPY_INCREF(value);
    return value;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_percent_mapping_argument(tinypy_vm_t *vm, tinypy_value_t *format, tinypy_value_t *mapping, const uint8_t *key_bytes, size_t key_size, tinypy_error_t **out_error) {
    tinypy_value_t *key = TINYPY_VALUE_KIND(format) == TINYPY_VALUE_UNICODE ? tinypy_unicode_from_utf8(vm, (const char *)key_bytes, key_size) : tinypy_string_from_bytes(vm, key_bytes, key_size);
    tinypy_value_t *value = tinypy_get_item(mapping, key, out_error);

    TINYPY_DECREF(key);
    return value;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_percent_append_padded(tinypy_string_builder_t *output, tinypy_string_builder_t *field, size_t prefix_size, int64_t width, int32_t left, int32_t zero, tinypy_bool_t unicode) {
    size_t field_width = field->size;
    size_t padding;

    if (unicode != 0) {
        size_t offset = 0U;

        field_width = 0U;
        while (offset < field->size) {
            offset += __tinypy_string_utf8_width(field->bytes[offset]);
            field_width += 1U;
        }
    }
    padding = width > 0 && (uint64_t)width > field_width ? (size_t)width - field_width : 0U;

    if (field->failed != 0) {
        output->failed = TINYPY_TRUE;
        return;
    }

    if (left == 0 && zero == 0) {
        __tinypy_string_builder_repeat(output, (const uint8_t *)" ", 1U, padding);
    }
    if (left == 0 && zero != 0 && padding != 0U) {
        __tinypy_string_builder_append(output, field->bytes, prefix_size);
        __tinypy_string_builder_repeat(output, (const uint8_t *)"0", 1U, padding);
        __tinypy_string_builder_append(output, field->bytes + prefix_size, field->size - prefix_size);
    }
    else {
        __tinypy_string_builder_append(output, field->bytes, field->size);
    }
    if (left != 0) {
        __tinypy_string_builder_repeat(output, (const uint8_t *)" ", 1U, padding);
    }
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_string_percent(tinypy_value_t *format, tinypy_value_t *argument_value, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(format);
    const uint8_t *bytes = TINYPY_TEXT_BYTES(format);
    size_t size = TINYPY_TEXT_BYTE_SIZE(format);
    size_t offset = 0U;
    tinypy_bool_t unicode = TINYPY_VALUE_KIND(format) == TINYPY_VALUE_UNICODE;
    tinypy_string_builder_t output;
    tinypy_percent_arguments_t arguments;

    (void)memset(&output, 0, sizeof(output));
    output.vm = vm;
    arguments.value = argument_value;
    arguments.index = 0U;
    arguments.consumed = 0U;
    while (offset < size) {
        size_t key_begin = 0U;
        size_t key_size = 0U;
        int32_t alternate = INT32_C(0);
        int32_t zero = INT32_C(0);
        int32_t left = INT32_C(0);
        int32_t space = INT32_C(0);
        int32_t plus = INT32_C(0);
        int64_t width = 0;
        int64_t precision = -1;
        uint8_t conversion;
        tinypy_value_t *value;
        tinypy_string_builder_t field;
        size_t prefix_size = 0U;

        if (bytes[offset] != (uint8_t)'%') {
            __tinypy_string_builder_character(&output, bytes[offset++]);
            continue;
        }
        offset += 1U;
        if (offset < size && bytes[offset] == (uint8_t)'%') {
            __tinypy_string_builder_character(&output, (uint8_t)'%');
            offset += 1U;
            continue;
        }
        if (offset >= size) {
            __tinypy_string_builder_discard(&output);
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "incomplete format", out_error);
            return NULL;
        }
        if (bytes[offset] == (uint8_t)'(') {
            size_t depth = 1U;

            key_begin = ++offset;
            while (offset < size && depth != 0U) {
                if (bytes[offset] == (uint8_t)'(') {
                    depth += 1U;
                }
                else if (bytes[offset] == (uint8_t)')') {
                    depth -= 1U;
                }
                if (depth != 0U) {
                    offset += 1U;
                }
            }
            if (depth != 0U) {
                __tinypy_string_builder_discard(&output);
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "incomplete format key", out_error);
                return NULL;
            }
            key_size = offset - key_begin;
            offset += 1U;
        }
        for (;;) {
            if (offset < size && bytes[offset] == (uint8_t)'#') {
                alternate = INT32_C(1);
            }
            else if (offset < size && bytes[offset] == (uint8_t)'0') {
                zero = INT32_C(1);
            }
            else if (offset < size && bytes[offset] == (uint8_t)'-') {
                left = INT32_C(1);
            }
            else if (offset < size && bytes[offset] == (uint8_t)' ') {
                space = INT32_C(1);
            }
            else if (offset < size && bytes[offset] == (uint8_t)'+') {
                plus = INT32_C(1);
            }
            else {
                break;
            }
            offset += 1U;
        }
        if (offset < size && bytes[offset] == (uint8_t)'*') {
            tinypy_value_t *width_value = __tinypy_percent_next_argument(vm, &arguments, out_error);

            if (width_value == NULL || __tinypy_string_integer(vm, width_value, &width, out_error) == 0) {
                if (width_value != NULL) {
                    TINYPY_DECREF(width_value);
                }
                __tinypy_string_builder_discard(&output);
                return NULL;
            }
            TINYPY_DECREF(width_value);
            if (width < 0) {
                if (width == INT64_MIN) {
                    __tinypy_string_builder_discard(&output);
                    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "width too big", out_error);
                    return NULL;
                }
                left = INT32_C(1);
                width = -width;
            }
            offset += 1U;
        }
        else {
            while (offset < size && bytes[offset] >= (uint8_t)'0' && bytes[offset] <= (uint8_t)'9') {
                if (width > (INT64_MAX - 9) / 10) {
                    __tinypy_string_builder_discard(&output);
                    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "width too big", out_error);
                    return NULL;
                }
                width = width * 10 + (int64_t)(bytes[offset++] - (uint8_t)'0');
            }
        }
        if (offset < size && bytes[offset] == (uint8_t)'.') {
            offset += 1U;
            precision = 0;
            if (offset < size && bytes[offset] == (uint8_t)'*') {
                tinypy_value_t *precision_value = __tinypy_percent_next_argument(vm, &arguments, out_error);

                if (precision_value == NULL || __tinypy_string_integer(vm, precision_value, &precision, out_error) == 0) {
                    if (precision_value != NULL) {
                        TINYPY_DECREF(precision_value);
                    }
                    __tinypy_string_builder_discard(&output);
                    return NULL;
                }
                TINYPY_DECREF(precision_value);
                if (precision < 0) {
                    precision = 0;
                }
                else if (precision > INT_MAX) {
                    __tinypy_string_builder_discard(&output);
                    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "precision is too large", out_error);
                    return NULL;
                }
                offset += 1U;
            }
            else {
                while (offset < size && bytes[offset] >= (uint8_t)'0' && bytes[offset] <= (uint8_t)'9') {
                    if (precision > (INT_MAX - 9) / 10) {
                        __tinypy_string_builder_discard(&output);
                        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "prec too big", out_error);
                        return NULL;
                    }
                    precision = precision * 10 + (int64_t)(bytes[offset++] - (uint8_t)'0');
                }
            }
        }
        while (offset < size && (bytes[offset] == (uint8_t)'h' || bytes[offset] == (uint8_t)'l' || bytes[offset] == (uint8_t)'L')) {
            offset += 1U;
        }
        if (offset >= size) {
            __tinypy_string_builder_discard(&output);
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "incomplete format", out_error);
            return NULL;
        }
        conversion = bytes[offset++];
        value = key_size != 0U ? __tinypy_percent_mapping_argument(vm, format, argument_value, bytes + key_begin, key_size, out_error) : __tinypy_percent_next_argument(vm, &arguments, out_error);
        if (value == NULL) {
            __tinypy_string_builder_discard(&output);
            return NULL;
        }
        (void)memset(&field, 0, sizeof(field));
        field.vm = vm;
        if (conversion == (uint8_t)'s' || conversion == (uint8_t)'r') {
            tinypy_value_t *text;
            size_t text_size;

            zero = INT32_C(0);

            if (conversion == (uint8_t)'s' && TINYPY_VALUE_KIND(value) == TINYPY_VALUE_UNICODE) {
                TINYPY_INCREF(value);
                text = value;
            }
            else {
                text = conversion == (uint8_t)'r' ? tinypy_object_repr(value, out_error) : tinypy_object_str(value, out_error);
            }

            if (text == NULL) {
                TINYPY_DECREF(value);
                __tinypy_string_builder_discard(&output);
                return NULL;
            }
            text_size = TINYPY_TEXT_BYTE_SIZE(text);
            if (precision >= 0) {
                size_t character_count = __tinypy_string_character_count(text);

                if ((uint64_t)precision < character_count) {
                    text_size = __tinypy_string_byte_offset(text, (size_t)precision);
                }
            }
            const uint8_t *bytes_2 = TINYPY_TEXT_BYTES(text);
            if (unicode != 0 && tinypy_internal_text_ascii_compatible(vm, text, out_error) == 0) {
                TINYPY_DECREF(text);
                TINYPY_DECREF(value);
                __tinypy_string_builder_discard(&field);
                __tinypy_string_builder_discard(&output);
                return NULL;
            }
            if (unicode == 0 && TINYPY_VALUE_KIND(text) == TINYPY_VALUE_UNICODE) {
                if (tinypy_internal_text_ascii_compatible(vm, format, out_error) == 0) {
                    TINYPY_DECREF(text);
                    TINYPY_DECREF(value);
                    __tinypy_string_builder_discard(&field);
                    __tinypy_string_builder_discard(&output);
                    return NULL;
                }
                unicode = TINYPY_TRUE;
            }
            __tinypy_string_builder_append(&field, bytes_2, text_size);
            if (TINYPY_VALUE_KIND(text) == TINYPY_VALUE_UNICODE) {
                unicode = INT32_C(1);
            }
            TINYPY_DECREF(text);
        }
        else if (conversion == (uint8_t)'c') {
            zero = INT32_C(0);
            if (__tinypy_string_is_text(value) != 0 && __tinypy_string_character_count(value) == 1U) {
                const uint8_t *bytes_2 = TINYPY_TEXT_BYTES(value);
                size_t byte_size = TINYPY_TEXT_BYTE_SIZE(value);

                if (unicode != 0 && tinypy_internal_text_ascii_compatible(vm, value, out_error) == 0) {
                    TINYPY_DECREF(value);
                    __tinypy_string_builder_discard(&field);
                    __tinypy_string_builder_discard(&output);
                    return NULL;
                }
                if (unicode == 0 && TINYPY_VALUE_KIND(value) == TINYPY_VALUE_UNICODE) {
                    if (tinypy_internal_text_ascii_compatible(vm, format, out_error) == 0) {
                        TINYPY_DECREF(value);
                        __tinypy_string_builder_discard(&field);
                        __tinypy_string_builder_discard(&output);
                        return NULL;
                    }
                    unicode = TINYPY_TRUE;
                }
                __tinypy_string_builder_append(&field, bytes_2, byte_size);
                if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_UNICODE) {
                    unicode = INT32_C(1);
                }
            }
            else {
                int64_t character;
                int64_t maximum = unicode != 0 ? INT64_C(0x10ffff) : INT64_C(0xff);

                if (__tinypy_string_integer(vm, value, &character, out_error) == 0 || character < 0 || character > maximum) {
                    if (out_error == NULL || *out_error == NULL) {
                        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "%c argument is out of range", out_error);
                    }
                    TINYPY_DECREF(value);
                    __tinypy_string_builder_discard(&field);
                    __tinypy_string_builder_discard(&output);
                    return NULL;
                }
                if (unicode != 0) {
                    __tinypy_string_builder_code_point(&field, (uint32_t)character);
                }
                else {
                    __tinypy_string_builder_character(&field, (uint8_t)character);
                }
            }
        }
        else if (conversion == (uint8_t)'d' || conversion == (uint8_t)'i' || conversion == (uint8_t)'u' || conversion == (uint8_t)'o' || conversion == (uint8_t)'x' || conversion == (uint8_t)'X') {
            if (__tinypy_percent_append_integer(vm, &field, value, conversion, alternate, plus, space, precision, TINYPY_FALSE, &prefix_size, out_error) == 0) {
                TINYPY_DECREF(value);
                __tinypy_string_builder_discard(&field);
                __tinypy_string_builder_discard(&output);
                return NULL;
            }
        }
        else if (conversion == (uint8_t)'e' || conversion == (uint8_t)'E' || conversion == (uint8_t)'f' || conversion == (uint8_t)'F' || conversion == (uint8_t)'g' || conversion == (uint8_t)'G') {
            if (__tinypy_percent_append_float(vm, &field, value, conversion, alternate, plus, space, precision, unicode == 0 ? TINYPY_TRUE : TINYPY_FALSE, &prefix_size, out_error) == 0) {
                TINYPY_DECREF(value);
                __tinypy_string_builder_discard(&field);
                __tinypy_string_builder_discard(&output);
                return NULL;
            }
        }
        else {
            TINYPY_DECREF(value);
            __tinypy_string_builder_discard(&field);
            __tinypy_string_builder_discard(&output);
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "unsupported format character", out_error);
            return NULL;
        }
        TINYPY_DECREF(value);
        if (left != 0) {
            zero = INT32_C(0);
        }
        __tinypy_percent_append_padded(&output, &field, prefix_size, width, left, zero, unicode);
        __tinypy_string_builder_discard(&field);
    }
    if (TINYPY_VALUE_KIND(argument_value) == TINYPY_VALUE_TUPLE) {
        if (arguments.index != TINYPY_TUPLE_SIZE(argument_value)) {
            __tinypy_string_builder_discard(&output);
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "not all arguments converted during string formatting", out_error);
            return NULL;
        }
    }
    else if (arguments.consumed == 0U && TINYPY_VALUE_KIND(argument_value) != TINYPY_VALUE_DICT) {
        __tinypy_string_builder_discard(&output);
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "not all arguments converted during string formatting", out_error);
        return NULL;
    }
    tinypy_value_t *return_value_1 = __tinypy_string_builder_finish(&output, unicode, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_string_formatter_decimal(tinypy_vm_t *vm, tinypy_value_t *text, size_t begin, size_t end, tinypy_error_t **out_error) {
    tinypy_value_t *digits = __tinypy_string_from_span(vm, text, begin, end);
    tinypy_value_t *arguments = tinypy_tuple_from_items(vm, &digits, 1U);
    tinypy_value_t *result = tinypy_internal_long_create(&vm->types[TINYPY_VALUE_LONG], arguments, NULL, out_error);

    TINYPY_DECREF(arguments);
    TINYPY_DECREF(digits);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_string_formatter_is_decimal(const uint8_t *bytes, size_t begin, size_t end) {
    size_t offset;

    if (begin == end) {
        return TINYPY_FALSE;
    }
    for (offset = begin; offset < end; ++offset) {
        if (bytes[offset] < (uint8_t)'0' || bytes[offset] > (uint8_t)'9') {
            return TINYPY_FALSE;
        }
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_string_formatter_record(tinypy_vm_t *vm, tinypy_value_t *literal, tinypy_value_t *field, tinypy_value_t *spec, tinypy_value_t *conversion) {
    tinypy_value_t *items[4] = {
        literal,
        field != NULL ? field : &vm->none_object.base,
        spec != NULL ? spec : &vm->none_object.base,
        conversion != NULL ? conversion : &vm->none_object.base
    };
    tinypy_value_t *result = tinypy_tuple_from_items(vm, items, 4U);

    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_string_formatter_parser_next(tinypy_iterator_object_t *iterator, tinypy_error_t **out_error) {
    tinypy_value_t *text = iterator->iterable;
    const uint8_t *bytes;
    size_t size;
    size_t offset = iterator->index;

    if (text == NULL) {
        return NULL;
    }
    tinypy_vm_t *vm = TINYPY_VALUE_VM(text);
    bytes = TINYPY_TEXT_BYTES(text);
    size = TINYPY_TEXT_BYTE_SIZE(text);
    if (offset >= size) {
        tinypy_internal_iterator_clear(iterator);
        return NULL;
    }
    size_t literal_begin = offset;
    size_t brace = offset;

    while (brace < size && bytes[brace] != (uint8_t)'{' && bytes[brace] != (uint8_t)'}') {
        brace += 1U;
    }
    if (brace == size) {
        tinypy_value_t *literal = __tinypy_string_from_span(vm, text, literal_begin, size);
        tinypy_value_t *record = __tinypy_string_formatter_record(vm, literal, NULL, NULL, NULL);

        TINYPY_DECREF(literal);
        iterator->index = size;
        return record;
    }
    if (brace + 1U < size && bytes[brace + 1U] == bytes[brace]) {
        tinypy_value_t *literal = __tinypy_string_from_span(vm, text, literal_begin, brace + 1U);
        tinypy_value_t *record = __tinypy_string_formatter_record(vm, literal, NULL, NULL, NULL);

        TINYPY_DECREF(literal);
        iterator->index = brace + 2U;
        return record;
    }
    if (bytes[brace] == (uint8_t)'}') {
        tinypy_internal_iterator_clear(iterator);
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "single '}' encountered in format string", out_error);
        return NULL;
    }
    size_t end = brace + 1U;
    size_t depth = 1U;

    while (end < size && depth != 0U) {
        if (bytes[end] == (uint8_t)'{') {
            depth += 1U;
        }
        else if (bytes[end] == (uint8_t)'}') {
            depth -= 1U;
            if (depth == 0U) {
                break;
            }
        }
        end += 1U;
    }
    if (depth != 0U) {
        tinypy_internal_iterator_clear(iterator);
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "unmatched '{' in format string", out_error);
        return NULL;
    }
    size_t field_end = brace + 1U;

    while (field_end < end && bytes[field_end] != (uint8_t)'!' && bytes[field_end] != (uint8_t)':') {
        field_end += 1U;
    }
    size_t spec_begin = end;
    tinypy_value_t *conversion = NULL;

    if (field_end < end && bytes[field_end] == (uint8_t)'!') {
        size_t conversion_begin = field_end + 1U;

        if (conversion_begin >= end) {
            tinypy_internal_iterator_clear(iterator);
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "end of format while looking for conversion specifier", out_error);
            return NULL;
        }
        conversion = __tinypy_string_from_span(vm, text, conversion_begin, conversion_begin + 1U);
        spec_begin = conversion_begin + 1U;
        if (spec_begin < end && bytes[spec_begin] == (uint8_t)':') {
            spec_begin += 1U;
        }
        else if (spec_begin != end) {
            TINYPY_DECREF(conversion);
            tinypy_internal_iterator_clear(iterator);
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "expected ':' after format specifier", out_error);
            return NULL;
        }
    }
    else if (field_end < end) {
        spec_begin = field_end + 1U;
    }
    tinypy_value_t *literal = __tinypy_string_from_span(vm, text, literal_begin, brace);
    tinypy_value_t *field = __tinypy_string_from_span(vm, text, brace + 1U, field_end);
    tinypy_value_t *spec = __tinypy_string_from_span(vm, text, spec_begin, end);
    tinypy_value_t *record = __tinypy_string_formatter_record(vm, literal, field, spec, conversion);

    TINYPY_DECREF(spec);
    TINYPY_DECREF(field);
    TINYPY_DECREF(literal);
    if (conversion != NULL) {
        TINYPY_DECREF(conversion);
    }
    iterator->index = end + 1U;
    return record;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_string_formatter_parser_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_string_method_arguments(vm, args, kwargs, 1U, 1U, INT32_C(0), out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *text = TINYPY_TUPLE_GET(args, 0U);
    if (__tinypy_string_require_text(vm, text, "formatter parser requires a string", out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *result = tinypy_internal_formatter_iterator_new(text, INT32_C(6));

    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_string_formatter_path_component(tinypy_vm_t *vm, tinypy_bool_t attribute, tinypy_value_t *value) {
    tinypy_value_t *flag = tinypy_bool_from_i32(vm, attribute);
    tinypy_value_t *items[2] = {flag, value};
    tinypy_value_t *component = tinypy_tuple_from_items(vm, items, 2U);

    TINYPY_DECREF(flag);
    return component;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_string_formatter_field_next(tinypy_iterator_object_t *iterator, tinypy_error_t **out_error) {
    tinypy_value_t *text = iterator->iterable;
    const uint8_t *bytes;
    size_t size;
    size_t offset = iterator->index;

    if (text == NULL) {
        return NULL;
    }
    tinypy_vm_t *vm = TINYPY_VALUE_VM(text);
    bytes = TINYPY_TEXT_BYTES(text);
    size = TINYPY_TEXT_BYTE_SIZE(text);
    if (offset >= size) {
        tinypy_internal_iterator_clear(iterator);
        return NULL;
    }
    tinypy_bool_t attribute;
    size_t begin;
    size_t end;
    tinypy_value_t *component_value;

    if (bytes[offset] == (uint8_t)'.') {
        attribute = TINYPY_TRUE;
        begin = ++offset;
        while (offset < size && bytes[offset] != (uint8_t)'.' && bytes[offset] != (uint8_t)'[') {
            offset += 1U;
        }
        end = offset;
        if (begin == end) {
            goto empty_attribute;
        }
        component_value = __tinypy_string_from_span(vm, text, begin, end);
    }
    else if (bytes[offset] == (uint8_t)'[') {
        attribute = TINYPY_FALSE;
        begin = ++offset;
        while (offset < size && bytes[offset] != (uint8_t)']') {
            offset += 1U;
        }
        if (offset == size) {
            tinypy_internal_iterator_clear(iterator);
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "missing ']' in format string", out_error);
            return NULL;
        }
        end = offset++;
        if (begin == end) {
            goto empty_attribute;
        }
        if (__tinypy_string_formatter_is_decimal(bytes, begin, end) != 0) {
            component_value = __tinypy_string_formatter_decimal(vm, text, begin, end, out_error);
            if (component_value == NULL) {
                tinypy_internal_iterator_clear(iterator);
                return NULL;
            }
        }
        else {
            component_value = __tinypy_string_from_span(vm, text, begin, end);
        }
        if (offset < size && bytes[offset] != (uint8_t)'.' && bytes[offset] != (uint8_t)'[') {
            TINYPY_DECREF(component_value);
            tinypy_internal_iterator_clear(iterator);
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "only '.' or '[' may follow ']' in format field specifier", out_error);
            return NULL;
        }
    }
    else {
        tinypy_internal_iterator_clear(iterator);
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "invalid format field", out_error);
        return NULL;
    }
    tinypy_value_t *component = __tinypy_string_formatter_path_component(vm, attribute, component_value);

    TINYPY_DECREF(component_value);
    iterator->index = offset;
    return component;

empty_attribute:
    tinypy_internal_iterator_clear(iterator);
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "empty attribute in format string", out_error);
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_string_formatter_field_name_split_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *head;
    const uint8_t *bytes;
    size_t size;
    size_t offset = 0U;

    (void)user_data;
    if (__tinypy_string_method_arguments(vm, args, kwargs, 1U, 1U, INT32_C(0), out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *text = TINYPY_TUPLE_GET(args, 0U);
    if (__tinypy_string_require_text(vm, text, "formatter field splitter requires a string", out_error) == 0) {
        return NULL;
    }
    bytes = TINYPY_TEXT_BYTES(text);
    size = TINYPY_TEXT_BYTE_SIZE(text);
    while (offset < size && bytes[offset] != (uint8_t)'.' && bytes[offset] != (uint8_t)'[') {
        offset += 1U;
    }
    if (__tinypy_string_formatter_is_decimal(bytes, 0U, offset) != 0) {
        head = __tinypy_string_formatter_decimal(vm, text, 0U, offset, out_error);
        if (head == NULL) {
            return NULL;
        }
    }
    else {
        head = __tinypy_string_from_span(vm, text, 0U, offset);
    }
    tinypy_value_t *path_iterator = tinypy_internal_formatter_iterator_new(text, INT32_C(7));
    tinypy_value_t *items[2] = {head, path_iterator};
    tinypy_value_t *result;

    TINYPY_ITERATOR_OBJECT(path_iterator)->index = offset;
    result = tinypy_tuple_from_items(vm, items, 2U);
    TINYPY_DECREF(path_iterator);
    TINYPY_DECREF(head);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_string_add_method(tinypy_type_t *type, const char *name, size_t name_size, tinypy_native_function_callback_t callback, void *user_data) {
    tinypy_value_t *function = tinypy_native_function_new(type->vm, name, name_size, callback, user_data, NULL);

    tinypy_type_set_attr(type, name, name_size, function);
    TINYPY_DECREF(function);
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_initialize_string_types(tinypy_vm_t *vm) {
    tinypy_type_t *types[2] = {&vm->types[TINYPY_VALUE_STRING], &vm->types[TINYPY_VALUE_UNICODE]};
    size_t index;

    for (index = 0U; index < 2U; ++index) {
        __tinypy_string_add_method(types[index], "_formatter_parser", 17U, __tinypy_string_formatter_parser_method, NULL);
        __tinypy_string_add_method(types[index], "_formatter_field_name_split", 27U, __tinypy_string_formatter_field_name_split_method, NULL);
        __tinypy_string_add_method(types[index], "format", 6U, __tinypy_string_format_method, NULL);
        __tinypy_string_add_method(types[index], "center", 6U, __tinypy_string_align_method, NULL);
        __tinypy_string_add_method(types[index], "ljust", 5U, __tinypy_string_align_method, (void *)(intptr_t)-1);
        __tinypy_string_add_method(types[index], "rjust", 5U, __tinypy_string_align_method, (void *)(intptr_t)1);
        __tinypy_string_add_method(types[index], "join", 4U, __tinypy_string_join_method, NULL);
        __tinypy_string_add_method(types[index], "find", 4U, __tinypy_string_search_method, (void *)(intptr_t)0);
        __tinypy_string_add_method(types[index], "rfind", 5U, __tinypy_string_search_method, (void *)(intptr_t)1);
        __tinypy_string_add_method(types[index], "index", 5U, __tinypy_string_search_method, (void *)(intptr_t)2);
        __tinypy_string_add_method(types[index], "rindex", 6U, __tinypy_string_search_method, (void *)(intptr_t)3);
        __tinypy_string_add_method(types[index], "startswith", 10U, __tinypy_string_prefix_method, NULL);
        __tinypy_string_add_method(types[index], "endswith", 8U, __tinypy_string_prefix_method, (void *)(intptr_t)1);
        __tinypy_string_add_method(types[index], "count", 5U, __tinypy_string_count_method, NULL);
        __tinypy_string_add_method(types[index], "strip", 5U, __tinypy_string_strip_method, (void *)(intptr_t)0);
        __tinypy_string_add_method(types[index], "lstrip", 6U, __tinypy_string_strip_method, (void *)(intptr_t)-1);
        __tinypy_string_add_method(types[index], "rstrip", 6U, __tinypy_string_strip_method, (void *)(intptr_t)1);
        __tinypy_string_add_method(types[index], "replace", 7U, __tinypy_string_replace_method, NULL);
        __tinypy_string_add_method(types[index], "split", 5U, __tinypy_string_split_method, NULL);
        __tinypy_string_add_method(types[index], "rsplit", 6U, __tinypy_string_split_method, (void *)(intptr_t)1);
        __tinypy_string_add_method(types[index], "lower", 5U, __tinypy_string_case_method, (void *)(intptr_t)0);
        __tinypy_string_add_method(types[index], "upper", 5U, __tinypy_string_case_method, (void *)(intptr_t)1);
        __tinypy_string_add_method(types[index], "swapcase", 8U, __tinypy_string_case_method, (void *)(intptr_t)2);
        __tinypy_string_add_method(types[index], "capitalize", 10U, __tinypy_string_case_method, (void *)(intptr_t)3);
        __tinypy_string_add_method(types[index], "title", 5U, __tinypy_string_case_method, (void *)(intptr_t)4);
        __tinypy_string_add_method(types[index], "isalpha", 7U, __tinypy_string_predicate_method, (void *)(intptr_t)0);
        __tinypy_string_add_method(types[index], "isdigit", 7U, __tinypy_string_predicate_method, (void *)(intptr_t)1);
        __tinypy_string_add_method(types[index], "isalnum", 7U, __tinypy_string_predicate_method, (void *)(intptr_t)2);
        __tinypy_string_add_method(types[index], "isspace", 7U, __tinypy_string_predicate_method, (void *)(intptr_t)3);
        __tinypy_string_add_method(types[index], "islower", 7U, __tinypy_string_predicate_method, (void *)(intptr_t)4);
        __tinypy_string_add_method(types[index], "isupper", 7U, __tinypy_string_predicate_method, (void *)(intptr_t)5);
        __tinypy_string_add_method(types[index], "istitle", 7U, __tinypy_string_predicate_method, (void *)(intptr_t)6);
        __tinypy_string_add_method(types[index], "zfill", 5U, __tinypy_string_zfill_method, NULL);
        __tinypy_string_add_method(types[index], "splitlines", 10U, __tinypy_string_splitlines_method, NULL);
        __tinypy_string_add_method(types[index], "expandtabs", 10U, __tinypy_string_expandtabs_method, NULL);
        __tinypy_string_add_method(types[index], "partition", 9U, __tinypy_string_partition_method, NULL);
        __tinypy_string_add_method(types[index], "rpartition", 10U, __tinypy_string_partition_method, (void *)(intptr_t)1);
        __tinypy_string_add_method(types[index], "encode", 6U, __tinypy_string_codec_method, NULL);
        __tinypy_string_add_method(types[index], "decode", 6U, __tinypy_string_codec_method, (void *)(intptr_t)1);
        if (types[index] == &vm->types[TINYPY_VALUE_STRING]) {
            __tinypy_string_add_method(types[index], "translate", 9U, __tinypy_string_translate_method, NULL);
        }
        else {
            __tinypy_string_add_method(types[index], "translate", 9U, __tinypy_unicode_translate_method, NULL);
            __tinypy_string_add_method(types[index], "isdecimal", 9U, __tinypy_string_predicate_method, (void *)(intptr_t)7);
            __tinypy_string_add_method(types[index], "isnumeric", 9U, __tinypy_string_predicate_method, (void *)(intptr_t)8);
        }
    }
}
