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
/* Output already produced from byte strings must decode as ASCII before the
   result can become unicode, as Python 2.7 raises UnicodeDecodeError there. */
static tinypy_bool_t __tinypy_string_builder_ascii_compatible(tinypy_vm_t *vm, const tinypy_string_builder_t *builder, tinypy_error_t **out_error) {
    size_t index;

    for (index = 0U; index < builder->size; ++index) {
        if (builder->bytes[index] >= 0x80U) {
            tinypy_value_t *pending = tinypy_string_from_bytes(vm, builder->bytes, builder->size);

            (void)tinypy_internal_raise_ascii_decode_error(vm, pending, index, index + 1U, out_error);
            TINYPY_DECREF(pending);
            return TINYPY_FALSE;
        }
    }
    return TINYPY_TRUE;
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
            result = unicode != 0 ? tinypy_unicode_from_utf8(builder->vm, "", 0U) : TINYPY_RET_EMPTY_STRING(builder->vm);
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
static tinypy_bool_t __tinypy_string_integer(tinypy_vm_t *vm, tinypy_value_t *value, int64_t *out_value, tinypy_error_t **out_error) {
    (void)vm;
    tinypy_bool_t return_value_1 = tinypy_internal_integer_as_ssize(value, out_value, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_percent_integer_argument(tinypy_vm_t *vm, tinypy_value_t *value, int64_t *out_value, tinypy_error_t **out_error) {
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);

    if (kind != TINYPY_VALUE_BOOL && kind != TINYPY_VALUE_INTEGER) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "* wants int", out_error);
        return TINYPY_FALSE;
    }
    tinypy_bool_t result = tinypy_internal_index_as_i64(value, out_value, TINYPY_FALSE, out_error);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_percent_append_integer(tinypy_vm_t *vm, tinypy_string_builder_t *builder, tinypy_value_t *value, uint8_t conversion, int32_t alternate, int32_t plus, int32_t space, int64_t precision, tinypy_bool_t new_format, size_t *out_prefix_size, tinypy_error_t **out_error);
static tinypy_bool_t __tinypy_percent_float_operand(tinypy_vm_t *vm, tinypy_value_t *value, tinypy_bool_t type_error, double *out_number, tinypy_error_t **out_error);
static tinypy_bool_t __tinypy_percent_append_double(tinypy_vm_t *vm, tinypy_string_builder_t *builder, double number, uint8_t conversion, int32_t alternate, int32_t plus, int32_t space, int64_t precision, size_t *out_prefix_size, tinypy_error_t **out_error);
static void __tinypy_string_format_group_digits(tinypy_string_builder_t *field, size_t prefix_size);
static tinypy_value_t *__tinypy_string_from_span(tinypy_vm_t *vm, const tinypy_value_t *source, size_t begin, size_t end);
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

/* Counts the digits before the decimal point of a formatted number and
   reports whether it already carries a point or an exponent. */
static size_t __tinypy_string_format_integer_digits(const tinypy_string_builder_t *field, size_t prefix_size, tinypy_bool_t *out_has_point, tinypy_bool_t *out_has_exponent) {
    size_t index;
    size_t digits = 0U;

    *out_has_point = TINYPY_FALSE;
    *out_has_exponent = TINYPY_FALSE;
    for (index = prefix_size; index < field->size; ++index) {
        uint8_t byte = field->bytes[index];

        if (byte == (uint8_t)'.') {
            *out_has_point = TINYPY_TRUE;
        }
        else if (byte == (uint8_t)'e' || byte == (uint8_t)'E') {
            *out_has_exponent = TINYPY_TRUE;
        }
        else if (*out_has_point == 0 && byte >= (uint8_t)'0' && byte <= (uint8_t)'9') {
            digits += 1U;
        }
    }
    if (*out_has_point != 0 && digits == 1U && field->bytes[prefix_size] == (uint8_t)'0') {
        digits = 0U;
    }
    return digits;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_string_format_add_dot_zero(tinypy_string_builder_t *field, size_t prefix_size) {
    tinypy_bool_t has_point;
    tinypy_bool_t has_exponent;

    if (prefix_size >= field->size || field->bytes[prefix_size] < (uint8_t)'0' || field->bytes[prefix_size] > (uint8_t)'9') {
        return;
    }
    (void)__tinypy_string_format_integer_digits(field, prefix_size, &has_point, &has_exponent);
    if (has_point == 0 && has_exponent == 0) {
        __tinypy_string_builder_append(field, ".0", 2U);
    }
}
//////////////////////////////////////////////////////////////////////////
/* Drops the trailing zeros of an exponent form's mantissa, as %g does. */
static void __tinypy_string_format_strip_mantissa_zeros(tinypy_string_builder_t *field, size_t prefix_size) {
    size_t exponent = prefix_size;
    size_t end;

    while (exponent < field->size && field->bytes[exponent] != (uint8_t)'e' && field->bytes[exponent] != (uint8_t)'E') {
        exponent += 1U;
    }
    end = exponent;
    if (memchr(field->bytes + prefix_size, '.', exponent - prefix_size) == NULL) {
        return;
    }
    while (end > prefix_size && field->bytes[end - 1U] == (uint8_t)'0') {
        end -= 1U;
    }
    if (end > prefix_size && field->bytes[end - 1U] == (uint8_t)'.') {
        end -= 1U;
    }
    if (end != exponent) {
        (void)memmove(field->bytes + end, field->bytes + exponent, field->size - exponent);
        field->size -= exponent - end;
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
            tinypy_bool_t return_value_1 = tinypy_internal_raise_ascii_decode_error(vm, text, index, index + 1U, out_error);
            return return_value_1;
        }
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_string_format_is_alignment(uint8_t character) {
    tinypy_bool_t result = character == (uint8_t)'<' || character == (uint8_t)'>' || character == (uint8_t)'^' || character == (uint8_t)'=' ? TINYPY_TRUE : TINYPY_FALSE;

    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_string_format_has_code(const char *codes, uint32_t type) {
    tinypy_bool_t result = type != 0U && type < 0x80U && strchr(codes, (int)type) != NULL ? TINYPY_TRUE : TINYPY_FALSE;

    return result;
}
//////////////////////////////////////////////////////////////////////////
/* Reads a width or precision the way the format spec parser does: the value
   must stay within Py_ssize_t. */
static tinypy_bool_t __tinypy_string_format_spec_integer(tinypy_vm_t *vm, const uint8_t *spec, size_t spec_size, size_t *in_out_offset, int64_t *out_value, tinypy_error_t **out_error) {
    size_t offset = *in_out_offset;
    int64_t value = 0;

    while (offset < spec_size && spec[offset] >= (uint8_t)'0' && spec[offset] <= (uint8_t)'9') {
        int64_t digit = (int64_t)(spec[offset] - (uint8_t)'0');

        if (value > ((int64_t)PTRDIFF_MAX - digit) / 10) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "Too many decimal digits in format string", out_error);
            return TINYPY_FALSE;
        }
        value = value * 10 + digit;
        offset += 1U;
    }
    *in_out_offset = offset;
    *out_value = value;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
/* Reports a presentation type the value does not support or, without a
   value, one that rejects ','; the unicode formatter spells a type outside
   printable ASCII as \x escape. */
static void __tinypy_string_format_code_error(tinypy_vm_t *vm, uint32_t type, tinypy_bool_t unicode_formatter, const tinypy_value_t *value, tinypy_error_t **out_error) {
    char code[16];
    size_t code_size = 1U;

    code[0] = (char)type;
    if (unicode_formatter != 0 && (type <= UINT32_C(32) || type >= UINT32_C(128))) {
        code_size = (size_t)snprintf(code, sizeof(code), "\\x%" PRIx32, type);
    }
    if (value == NULL) {
        const tinypy_message_part_t parts[] = {
            TINYPY_MESSAGE_PART_LITERAL("Cannot specify ',' with '"),
            {code, code_size},
            TINYPY_MESSAGE_PART_LITERAL("'.")
        };

        tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_VALUE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
        return;
    }
    const tinypy_message_part_t parts[] = {
        TINYPY_MESSAGE_PART_LITERAL("Unknown format code '"),
        {code, code_size},
        TINYPY_MESSAGE_PART_LITERAL("' for object of type '"),
        TINYPY_MESSAGE_PART_TYPE_NAME(value),
        TINYPY_MESSAGE_PART_LITERAL("'")
    };

    tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_VALUE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_string_format_value(tinypy_vm_t *vm, tinypy_value_t *value, int32_t conversion, const uint8_t *spec, size_t spec_size, tinypy_bool_t spec_unicode, tinypy_bool_t allow_special, tinypy_value_t *special_spec, tinypy_bool_t *out_unicode, tinypy_error_t **out_error) {
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
    int64_t precision = -1;
    uint32_t type = 0U;
    tinypy_bool_t numeric = TINYPY_FALSE;
    size_t prefix_size = 0U;
    tinypy_string_builder_t field;
    tinypy_string_builder_t output;
    size_t field_width;
    tinypy_bool_t float_default_type = TINYPY_FALSE;
    tinypy_value_type_e value_kind = TINYPY_VALUE_KIND(value);
    tinypy_bool_t integer_kind = value_kind == TINYPY_VALUE_BOOL || value_kind == TINYPY_VALUE_INTEGER || value_kind == TINYPY_VALUE_LONG ? TINYPY_TRUE : TINYPY_FALSE;
    tinypy_bool_t text_kind = value_kind == TINYPY_VALUE_STRING || value_kind == TINYPY_VALUE_UNICODE || conversion != 0 ? TINYPY_TRUE : TINYPY_FALSE;
    tinypy_bool_t unicode_formatter = value_kind == TINYPY_VALUE_UNICODE && conversion == 0 ? TINYPY_TRUE : TINYPY_FALSE;
    tinypy_bool_t direct_builtin = (value->type->flags & TINYPY_TYPE_FLAG_HEAP) == 0U && (integer_kind != 0 || value_kind == TINYPY_VALUE_FLOAT || value_kind == TINYPY_VALUE_COMPLEX || value_kind == TINYPY_VALUE_STRING || value_kind == TINYPY_VALUE_UNICODE);

    *out_unicode = spec_unicode;
    (void)memset(&field, 0, sizeof(field));
    (void)memset(&output, 0, sizeof(output));
    field.vm = vm;
    output.vm = vm;
    if (allow_special != 0 && conversion == 0 && direct_builtin == 0 && tinypy_internal_object_has_special_key(value, vm->internal_special_format_key) != 0) {
        tinypy_value_t *method = tinypy_internal_object_get_special_key(value, vm->internal_special_format_key, out_error);
        tinypy_value_t *format_spec;
        tinypy_value_t *arguments;
        tinypy_value_t *result;

        if (method == NULL) {
            return NULL;
        }
        if (special_spec != NULL) {
            format_spec = TINYPY_RET(special_spec);
        }
        else {
            format_spec = spec_unicode != 0 ? tinypy_unicode_from_utf8(vm, (const char *)spec, spec_size) : tinypy_string_from_bytes(vm, spec, spec_size);
        }
        arguments = tinypy_tuple_from_items(vm, &format_spec, 1U);
        result = tinypy_call(method, arguments, NULL, out_error);
        TINYPY_DECREF(arguments);
        TINYPY_DECREF(format_spec);
        TINYPY_DECREF(method);
        if (result == NULL) {
            return NULL;
        }
        if (TINYPY_VALUE_KIND(result) != TINYPY_VALUE_STRING && TINYPY_VALUE_KIND(result) != TINYPY_VALUE_UNICODE) {
            tinypy_message_part_t parts[] = {
                TINYPY_MESSAGE_PART_TYPE_NAME(value),
                TINYPY_MESSAGE_PART_LITERAL(".__format__ must return string or unicode, not "),
                TINYPY_MESSAGE_PART_TYPE_NAME(result)
            };

            tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, 3U, out_error);
            TINYPY_DECREF(result);
            return NULL;
        }
        if (spec_unicode != 0 && TINYPY_VALUE_KIND(result) == TINYPY_VALUE_STRING) {
            tinypy_value_t *promoted = tinypy_internal_object_unicode(result, out_error);

            TINYPY_DECREF(result);
            result = promoted;
            if (result == NULL) {
                return NULL;
            }
        }
        *out_unicode = TINYPY_VALUE_KIND(result) == TINYPY_VALUE_UNICODE ? TINYPY_TRUE : TINYPY_FALSE;
        return result;
    }
    if (text_kind == 0 && integer_kind == 0 && value_kind != TINYPY_VALUE_FLOAT && value_kind != TINYPY_VALUE_COMPLEX) {
        /* Any other object formats as its str() or unicode() first. */
        tinypy_value_t *text = spec_unicode != 0 ? tinypy_internal_object_unicode(value, out_error) : tinypy_object_str(value, out_error);

        if (text == NULL) {
            return NULL;
        }
        tinypy_value_t *result = __tinypy_internal_string_format_value(vm, text, 0, spec, spec_size, spec_unicode, TINYPY_TRUE, special_spec, out_unicode, out_error);

        TINYPY_DECREF(text);
        return result;
    }
    if (spec_unicode != 0 && (conversion == 'r' || (conversion == 0 && value_kind != TINYPY_VALUE_UNICODE))) {
        for (size_t index = 0U; index < spec_size; ++index) {
            if (spec[index] >= 0x80U) {
                tinypy_value_t *format_spec = tinypy_unicode_from_utf8(vm, (const char *)spec, spec_size);
                tinypy_value_t *encoded = tinypy_internal_text_codec(vm, format_spec, NULL, NULL, TINYPY_FALSE, TINYPY_TRUE, NULL, out_error);

                TINYPY_DECREF(format_spec);
                if (encoded != NULL) {
                    TINYPY_DECREF(encoded);
                }
                return NULL;
            }
        }
    }
    if (value_kind == TINYPY_VALUE_UNICODE && spec_unicode == 0) {
        for (size_t index = 0U; index < spec_size; ++index) {
            if (spec[index] >= 0x80U) {
                tinypy_value_t *format_spec = tinypy_string_from_bytes(vm, (const char *)spec, spec_size);
                (void)tinypy_internal_raise_ascii_decode_error(vm, format_spec, index, index + 1U, out_error);
                TINYPY_DECREF(format_spec);
                return NULL;
            }
        }
    }
    size_t first_size = 1U;
    if (unicode_formatter != 0 && spec_size != 0U) {
        uint32_t first_code_point;

        first_size = tinypy_internal_utf8_decode(spec, spec_size, &first_code_point);
    }
    if (first_size < spec_size && __tinypy_string_format_is_alignment(spec[first_size]) != 0) {
        (void)memcpy(fill, spec, first_size);
        fill_size = first_size;
        align = spec[first_size];
        explicit_fill = TINYPY_TRUE;
        offset = first_size + 1U;
    }
    else if (offset < spec_size && __tinypy_string_format_is_alignment(spec[offset]) != 0) {
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
    if (explicit_fill == 0 && offset < spec_size && spec[offset] == (uint8_t)'0') {
        fill[0] = (uint8_t)'0';
        if (align == 0U) {
            align = (uint8_t)'=';
        }
        offset += 1U;
    }
    int64_t width_value;
    if (__tinypy_string_format_spec_integer(vm, spec, spec_size, &offset, &width_value, out_error) == 0) {
        return NULL;
    }
    size_t width = (size_t)width_value;
    if (offset < spec_size && spec[offset] == (uint8_t)',') {
        grouping = TINYPY_TRUE;
        offset += 1U;
    }
    if (offset < spec_size && spec[offset] == (uint8_t)'.') {
        size_t precision_begin = ++offset;

        if (__tinypy_string_format_spec_integer(vm, spec, spec_size, &offset, &precision, out_error) == 0) {
            return NULL;
        }
        if (offset == precision_begin) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "Format specifier missing precision", out_error);
            return NULL;
        }
    }
    if (offset < spec_size) {
        size_t type_size = 1U;

        type = spec[offset];
        if (unicode_formatter != 0 && type >= 0x80U) {
            type_size = tinypy_internal_utf8_decode(spec + offset, spec_size - offset, &type);
        }
        if (spec_size - offset != type_size) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "Invalid conversion specification", out_error);
            return NULL;
        }
    }
    uint32_t effective_type = type != 0U || conversion != 0 ? type : (integer_kind != 0 ? (uint32_t)'d' : (text_kind != 0 ? (uint32_t)'s' : 0U));
    if (grouping != 0 && effective_type != 0U && __tinypy_string_format_has_code("defgEG%F", effective_type) == 0) {
        __tinypy_string_format_code_error(vm, effective_type, unicode_formatter, NULL, out_error);
        return NULL;
    }
    if (conversion == 0 && type != 0U) {
        const char *codes = text_kind != 0 ? "s" : (value_kind == TINYPY_VALUE_FLOAT ? "eEfFgGn%" : (value_kind == TINYPY_VALUE_COMPLEX ? "eEfFgGn" : "bcdoxXneEfFgG%"));

        if (__tinypy_string_format_has_code(codes, type) == 0) {
            __tinypy_string_format_code_error(vm, type, unicode_formatter, value, out_error);
            return NULL;
        }
    }
    if (conversion == 0 && text_kind != 0) {
        const char *message = sign_specified != 0 ? "Sign not allowed in string format specifier"
                              : (alternate != 0 ? "Alternate form (#) not allowed in string format specifier"
                              : (align == (uint8_t)'=' ? "'=' alignment not allowed in string format specifier" : NULL));

        if (message != NULL) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, message, out_error);
            return NULL;
        }
    }
    if (conversion == 0 && integer_kind != 0 && __tinypy_string_format_has_code("bcdoxXn", effective_type) != 0) {
        if (precision >= 0) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "Precision not allowed in integer format specifier", out_error);
            return NULL;
        }
        if (type == (uint32_t)'c' && sign_specified != 0) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "Sign not allowed with integer format specifier 'c'", out_error);
            return NULL;
        }
    }
    if (conversion == 0 && type == 0U) {
        if (value_kind == TINYPY_VALUE_INTEGER || value_kind == TINYPY_VALUE_LONG || (value_kind == TINYPY_VALUE_BOOL && spec_size != 0U)) {
            type = (uint32_t)'d';
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
        if (type != 0U && type != (uint32_t)'s') {
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
    else if (type == (uint32_t)'c') {
        int64_t character;

        numeric = TINYPY_TRUE;
        if (__tinypy_string_integer(vm, value, &character, out_error) == 0) {
            return NULL;
        }
        if (character < 0 || character > INT64_C(0xff)) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "%c arg not in range(0x100)", out_error);
            return NULL;
        }
        /* __format__ renders the character as a byte string, so a unicode
           format spec upgrades the result through the default ASCII codec. */
        if (spec_unicode != 0) {
            if (character > INT64_C(0x7f)) {
                uint8_t byte = (uint8_t)character;
                tinypy_value_t *rendered = tinypy_string_from_bytes(vm, &byte, 1U);

                (void)tinypy_internal_raise_ascii_decode_error(vm, rendered, 0U, 1U, out_error);
                TINYPY_DECREF(rendered);
                return NULL;
            }
            *out_unicode = TINYPY_TRUE;
        }
        __tinypy_string_builder_character(&field, (uint8_t)character);
    }
    else if (conversion == 0 && value_kind == TINYPY_VALUE_COMPLEX) {
        double real = TINYPY_COMPLEX_OBJECT(value)->real;
        double imaginary = TINYPY_COMPLEX_OBJECT(value)->imaginary;
        tinypy_bool_t default_type = type == 0U ? TINYPY_TRUE : TINYPY_FALSE;
        tinypy_bool_t pure_imaginary = real == 0.0 && signbit(real) == 0 ? TINYPY_TRUE : TINYPY_FALSE;
        uint8_t component_type = type == 0U || type == (uint32_t)'n' ? (uint8_t)'g' : (uint8_t)type;
        int64_t component_precision = precision >= 0 ? precision : (default_type != 0 ? 12 : 6);
        const char *message = precision > INT_MAX ? "precision too big"
                              : (alternate != 0 ? "Alternate form (#) not allowed in complex format specifier"
                              : (fill_size == 1U && fill[0] == (uint8_t)'0' ? "Zero padding is not allowed in complex format specifier"
                              : (align == (uint8_t)'=' ? "'=' alignment flag is not allowed in complex format specifier" : NULL)));

        numeric = TINYPY_TRUE;
        if (message != NULL) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, message, out_error);
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
    else if (integer_kind != 0 && __tinypy_string_format_has_code("bdoxXn", type) != 0) {
        numeric = TINYPY_TRUE;
        if (__tinypy_percent_append_integer(vm, &field, value, type == (uint32_t)'n' ? (uint8_t)'d' : (uint8_t)type, alternate, plus, space, precision, TINYPY_TRUE, &prefix_size, out_error) == 0) {
            __tinypy_string_builder_discard(&field);
            return NULL;
        }
    }
    else if (float_default_type != 0 || (text_kind == 0 && __tinypy_string_format_has_code("eEfFgGn%", type) != 0)) {
        uint8_t float_type = float_default_type != 0 || type == (uint32_t)'n' ? (uint8_t)'g' : (uint8_t)type;
        int64_t float_precision = float_default_type != 0 ? (precision < 0 ? 12 : (precision == 0 ? 1 : precision)) : precision;
        double number;

        numeric = TINYPY_TRUE;
        if (__tinypy_percent_float_operand(vm, value, TINYPY_FALSE, &number, out_error) == 0) {
            return NULL;
        }
        if (precision > INT_MAX || alternate != 0) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, alternate != 0 && precision <= INT_MAX ? "Alternate form (#) not allowed in float format specifier" : "precision too big", out_error);
            return NULL;
        }
        size_t field_start = field.size;

        if (__tinypy_percent_append_double(vm, &field, number, float_type, alternate, plus, space, float_precision, &prefix_size, out_error) == 0) {
            __tinypy_string_builder_discard(&field);
            return NULL;
        }
        if (float_default_type != 0) {
            tinypy_bool_t has_point;
            tinypy_bool_t has_exponent;
            size_t integer_digits = __tinypy_string_format_integer_digits(&field, prefix_size, &has_point, &has_exponent);

            /* With ADD_DOT_0 the 'g' form switches to the exponent as soon as
               the integer part uses every digit of the precision. */
            if (has_exponent == 0 && (int64_t)integer_digits == float_precision) {
                field.size = field_start;
                if (__tinypy_percent_append_double(vm, &field, number, (uint8_t)'e', alternate, plus, space, float_precision > 0 ? float_precision - 1 : 0, &prefix_size, out_error) == 0) {
                    __tinypy_string_builder_discard(&field);
                    return NULL;
                }
                if (alternate == 0) {
                    __tinypy_string_format_strip_mantissa_zeros(&field, prefix_size);
                }
            }
            __tinypy_string_format_add_dot_zero(&field, prefix_size);
        }
    }
    else {
        tinypy_value_t *text;
        tinypy_bool_t default_numeric = type == 0U && TINYPY_VALUE_KIND(value) == TINYPY_VALUE_FLOAT;

        /* A string, or a number formatted with an empty spec as its str(). */
        if (text_kind != 0) {
            text = TINYPY_RET(value);
        }
        else if (spec_unicode != 0) {
            text = tinypy_internal_object_unicode(value, out_error);
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
    tinypy_value_t *return_value_1 = __tinypy_internal_string_format_value(vm, value, conversion, spec, spec_size, spec_unicode, TINYPY_TRUE, NULL, out_unicode, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_string_format_builtin_value(tinypy_vm_t *vm, tinypy_value_t *value, int32_t conversion, const uint8_t *spec, size_t spec_size, tinypy_bool_t spec_unicode, tinypy_bool_t *out_unicode, tinypy_error_t **out_error) {
    tinypy_value_t *return_value_1 = __tinypy_internal_string_format_value(vm, value, conversion, spec, spec_size, spec_unicode, TINYPY_FALSE, NULL, out_unicode, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_string_format_object(tinypy_vm_t *vm, tinypy_value_t *value, tinypy_value_t *format_spec, tinypy_error_t **out_error) {
    const uint8_t *spec = format_spec != NULL ? TINYPY_TEXT_BYTES(format_spec) : (const uint8_t *)"";
    size_t size = format_spec != NULL ? TINYPY_TEXT_BYTE_SIZE(format_spec) : 0U;
    tinypy_bool_t unicode = format_spec != NULL && TINYPY_VALUE_KIND(format_spec) == TINYPY_VALUE_UNICODE;
    tinypy_bool_t result_unicode;
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);
    tinypy_bool_t numeric = kind == TINYPY_VALUE_BOOL || kind == TINYPY_VALUE_INTEGER || kind == TINYPY_VALUE_LONG || kind == TINYPY_VALUE_FLOAT || kind == TINYPY_VALUE_COMPLEX;
    tinypy_value_t *converted = NULL;

    if (unicode != TINYPY_FALSE && numeric != TINYPY_FALSE && (value->type->flags & TINYPY_TYPE_FLAG_HEAP) == 0U) {
        converted = tinypy_object_str(format_spec, out_error);
        if (converted == NULL) {
            return NULL;
        }
        spec = TINYPY_TEXT_BYTES(converted);
        size = TINYPY_TEXT_BYTE_SIZE(converted);
    }
    tinypy_value_t *result = __tinypy_internal_string_format_value(vm, value, 0, spec, size, unicode, TINYPY_TRUE, format_spec, &result_unicode, out_error);

    if (converted != NULL) {
        TINYPY_DECREF(converted);
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_string_format_index(tinypy_vm_t *vm, const uint8_t *bytes, size_t size, tinypy_bool_t unicode, int64_t *out_index, tinypy_bool_t *out_numeric, tinypy_error_t **out_error) {
    size_t offset = 0U;
    int64_t integer = 0;

    *out_numeric = TINYPY_FALSE;
    if (size == 0U) {
        return TINYPY_TRUE;
    }
    while (offset < size) {
        uint8_t digit;
        size_t width = 1U;

        if (unicode != 0) {
            uint32_t code_point;

            width = tinypy_internal_utf8_decode(bytes + offset, size - offset, &code_point);
            if (tinypy_internal_unicode_decimal_digit(code_point, &digit) == 0) {
                return TINYPY_TRUE;
            }
        }
        else if (bytes[offset] >= (uint8_t)'0' && bytes[offset] <= (uint8_t)'9') {
            digit = (uint8_t)(bytes[offset] - (uint8_t)'0');
        }
        else {
            return TINYPY_TRUE;
        }
        if (integer > (INT64_MAX - digit) / 10) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "Too many decimal digits in format string", out_error);
            return TINYPY_FALSE;
        }
        integer = integer * 10 + digit;
        offset += width;
    }
    *out_index = integer;
    *out_numeric = TINYPY_TRUE;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_string_format_lookup(tinypy_vm_t *vm, tinypy_value_t *args, tinypy_value_t *kwargs, const uint8_t *field, size_t field_size, tinypy_bool_t unicode, size_t *auto_index, int32_t *numbering_mode, tinypy_error_t **out_error) {
    tinypy_value_t *value = NULL;
    size_t head_size = 0U;
    size_t path_offset;
    int64_t position = 0;
    tinypy_bool_t numeric;

    while (head_size < field_size && field[head_size] != (uint8_t)'.' && field[head_size] != (uint8_t)'[') {
        head_size += 1U;
    }
    if (__tinypy_string_format_index(vm, field, head_size, unicode, &position, &numeric, out_error) == 0) {
        return NULL;
    }
    if (head_size == 0U) {
        if (*numbering_mode == 2) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "cannot switch from manual field specification to automatic field numbering", out_error);
            return NULL;
        }
        *numbering_mode = 1;
        position = (int64_t)*auto_index;
        *auto_index += 1U;
        numeric = TINYPY_TRUE;
    }
    else if (numeric != 0) {
        if (*numbering_mode == 1) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "cannot switch from automatic field numbering to manual field specification", out_error);
            return NULL;
        }
        *numbering_mode = 2;
    }
    if (numeric != 0) {
        if (TINYPY_TUPLE_SIZE(args) <= 1U || (uint64_t)position >= TINYPY_TUPLE_SIZE(args) - 1U) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_INDEX, "tuple index out of range", out_error);
            return NULL;
        }
        value = TINYPY_RET(TINYPY_TUPLE_GET(args, (size_t)position + 1U));
    }
    else {
        tinypy_value_t *key = unicode != 0 ? tinypy_unicode_from_utf8(vm, (const char *)field, head_size) : tinypy_string_from_bytes(vm, field, head_size);

        if (kwargs != NULL) {
            tinypy_internal_exception_state_t state;

            tinypy_internal_exception_preserve_begin(vm, &state);
            value = tinypy_dict_get_optional(kwargs, key);
            tinypy_internal_exception_preserve_end(vm, &state);
        }
        if (value == NULL) {
            tinypy_internal_exception_raise_key_error(vm, key, out_error);
            TINYPY_DECREF(key);
            return NULL;
        }
        TINYPY_INCREF(value);
        TINYPY_DECREF(key);
    }
    path_offset = head_size;
    while (path_offset < field_size) {
        tinypy_value_t *next;
        tinypy_value_t *key;
        size_t begin;
        size_t end;
        tinypy_bool_t attribute = field[path_offset] == (uint8_t)'.';

        if (attribute != 0) {
            begin = ++path_offset;
            while (path_offset < field_size && field[path_offset] != (uint8_t)'.' && field[path_offset] != (uint8_t)'[') {
                path_offset += 1U;
            }
            end = path_offset;
        }
        else if (field[path_offset] == (uint8_t)'[') {
            begin = ++path_offset;
            while (path_offset < field_size && field[path_offset] != (uint8_t)']') {
                path_offset += 1U;
            }
            if (path_offset == field_size) {
                TINYPY_DECREF(value);
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "Missing ']' in format string", out_error);
                return NULL;
            }
            end = path_offset++;
        }
        else {
            TINYPY_DECREF(value);
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "Only '.' or '[' may follow ']' in format field specifier", out_error);
            return NULL;
        }
        if (begin == end) {
            TINYPY_DECREF(value);
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "Empty attribute in format string", out_error);
            return NULL;
        }
        numeric = TINYPY_FALSE;
        if (attribute == 0 && __tinypy_string_format_index(vm, field + begin, end - begin, unicode, &position, &numeric, out_error) == 0) {
            TINYPY_DECREF(value);
            return NULL;
        }
        if (numeric != 0) {
            /* Mapping-only objects receive a long key; sequence indexing
               builds an int before invoking a Python __getitem__. */
            tinypy_bool_t sequence = TINYPY_VALUE_KIND(value) != TINYPY_VALUE_DICT && ((value->type->sequence_slots != NULL && value->type->sequence_slots->get_item != NULL) || tinypy_internal_object_has_special_key(value, vm->internal_special_getitem_key) != 0);

            key = sequence != 0 ? tinypy_integer_from_i64(vm, position) : tinypy_long_from_i64(vm, position);
        }
        else {
            key = unicode != 0 ? tinypy_unicode_from_utf8(vm, (const char *)field + begin, end - begin) : tinypy_string_from_bytes(vm, field + begin, end - begin);
        }
        next = attribute != 0 ? tinypy_internal_object_get_attr_key(value, key, out_error) : tinypy_get_item(value, key, out_error);
        TINYPY_DECREF(key);
        TINYPY_DECREF(value);
        if (next == NULL) {
            return NULL;
        }
        value = next;
    }
    return value;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_string_format_parse_field(tinypy_vm_t *vm, const uint8_t *bytes, size_t begin, size_t end, tinypy_bool_t unicode, size_t *out_field_end, size_t *out_spec_begin, int32_t *out_conversion, tinypy_error_t **out_error) {
    size_t field_end = begin;

    while (field_end < end && bytes[field_end] != (uint8_t)'!' && bytes[field_end] != (uint8_t)':') {
        if (bytes[field_end] == (uint8_t)'{') {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "unexpected '{' in field name", out_error);
            return TINYPY_FALSE;
        }
        field_end += 1U;
    }
    *out_field_end = field_end;
    *out_spec_begin = end;
    *out_conversion = 0;
    if (field_end < end && bytes[field_end] == (uint8_t)'!') {
        size_t conversion_begin = field_end + 1U;
        size_t width = 1U;
        uint32_t conversion;

        if (conversion_begin >= end) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "end of format while looking for conversion specifier", out_error);
            return TINYPY_FALSE;
        }
        conversion = bytes[conversion_begin];
        if (unicode != 0) {
            width = tinypy_internal_utf8_decode(bytes + conversion_begin, end - conversion_begin, &conversion);
        }
        *out_conversion = (int32_t)conversion;
        *out_spec_begin = conversion_begin + width;
        if (*out_spec_begin < end && bytes[*out_spec_begin] == (uint8_t)':') {
            *out_spec_begin += 1U;
        }
        else if (*out_spec_begin != end) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "expected ':' after format specifier", out_error);
            return TINYPY_FALSE;
        }
    }
    else if (field_end < end) {
        *out_spec_begin = field_end + 1U;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_string_format_convert(tinypy_vm_t *vm, tinypy_value_t *value, int32_t conversion, tinypy_bool_t unicode, tinypy_error_t **out_error) {
    if (conversion == 0) {
        return TINYPY_RET(value);
    }
    if (conversion == 'r') {
        tinypy_value_t *result = tinypy_object_repr(value, out_error);

        return result;
    }
    if (conversion == 's') {
        tinypy_value_t *result = unicode != 0 ? tinypy_internal_object_unicode(value, out_error) : tinypy_object_str(value, out_error);

        return result;
    }
    char message[64];

    if (conversion > 32 && conversion < 127) {
        (void)snprintf(message, sizeof(message), "Unknown conversion specifier %c", (int)conversion);
    }
    else {
        (void)snprintf(message, sizeof(message), "Unknown conversion specifier \\x%" PRIx32, (uint32_t)conversion);
    }
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, message, out_error);
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_string_format_render(tinypy_vm_t *vm, tinypy_value_t *args, tinypy_value_t *kwargs, const uint8_t *bytes, size_t size, tinypy_bool_t unicode, size_t *automatic_index, int32_t *numbering_mode, int32_t recursion_depth, tinypy_error_t **out_error) {
    size_t offset = 0U;
    tinypy_string_builder_t builder;

    if (recursion_depth <= 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "Max string recursion exceeded", out_error);
        return NULL;
    }
    (void)memset(&builder, 0, sizeof(builder));
    builder.vm = vm;
    while (offset < size) {
        if ((bytes[offset] == (uint8_t)'{' || bytes[offset] == (uint8_t)'}') && offset + 1U < size && bytes[offset + 1U] == bytes[offset]) {
            __tinypy_string_builder_character(&builder, bytes[offset]);
            offset += 2U;
            continue;
        }
        if (bytes[offset] == (uint8_t)'{') {
            size_t end = offset + 1U;
            size_t depth = 1U;
            size_t field_end;
            size_t spec_begin;
            int32_t conversion;
            tinypy_value_t *value;
            tinypy_value_t *converted;
            tinypy_value_t *formatted;
            tinypy_value_t *text;
            tinypy_value_t *expanded_spec = NULL;
            tinypy_bool_t field_unicode;

            if (end == size) {
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "Single '{' encountered in format string", out_error);
                goto failure;
            }
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
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "unmatched '{' in format", out_error);
                goto failure;
            }
            if (__tinypy_string_format_parse_field(vm, bytes, offset + 1U, end, unicode, &field_end, &spec_begin, &conversion, out_error) == 0) {
                goto failure;
            }
            value = __tinypy_string_format_lookup(vm, args, kwargs, bytes + offset + 1U, field_end - offset - 1U, unicode, automatic_index, numbering_mode, out_error);
            if (value == NULL) {
                goto failure;
            }
            converted = __tinypy_string_format_convert(vm, value, conversion, unicode, out_error);
            TINYPY_DECREF(value);
            if (converted == NULL) {
                goto failure;
            }
            if (memchr(bytes + spec_begin, '{', end - spec_begin) != NULL) {
                expanded_spec = __tinypy_string_format_render(vm, args, kwargs, bytes + spec_begin, end - spec_begin, unicode, automatic_index, numbering_mode, recursion_depth - 1, out_error);
                if (expanded_spec == NULL) {
                    TINYPY_DECREF(converted);
                    goto failure;
                }
            }
            formatted = tinypy_internal_string_format_value(vm, converted, 0, expanded_spec != NULL ? TINYPY_TEXT_BYTES(expanded_spec) : bytes + spec_begin, expanded_spec != NULL ? TINYPY_TEXT_BYTE_SIZE(expanded_spec) : end - spec_begin, unicode, &field_unicode, out_error);
            TINYPY_DECREF(converted);
            if (expanded_spec != NULL) {
                TINYPY_DECREF(expanded_spec);
            }
            if (formatted == NULL) {
                goto failure;
            }
            text = unicode != 0 ? tinypy_internal_object_unicode(formatted, out_error) : tinypy_object_str(formatted, out_error);
            TINYPY_DECREF(formatted);
            if (text == NULL) {
                goto failure;
            }
            __tinypy_string_builder_append(&builder, TINYPY_TEXT_BYTES(text), TINYPY_TEXT_BYTE_SIZE(text));
            TINYPY_DECREF(text);
            offset = end + 1U;
            continue;
        }
        if (bytes[offset] == (uint8_t)'}') {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "Single '}' encountered in format string", out_error);
            goto failure;
        }
        __tinypy_string_builder_character(&builder, bytes[offset++]);
    }
    tinypy_value_t *result = __tinypy_string_builder_finish(&builder, unicode, out_error);

    return result;

failure:
    __tinypy_string_builder_discard(&builder);
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_string_format_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    size_t automatic_index = 0U;
    int32_t numbering_mode = 0;

    (void)user_data;
    tinypy_value_t *format = TINYPY_TUPLE_GET(args, 0U);
    tinypy_bool_t unicode = TINYPY_VALUE_KIND(format) == TINYPY_VALUE_UNICODE;
    tinypy_value_t *result = __tinypy_string_format_render(vm, args, kwargs, TINYPY_TEXT_BYTES(format), TINYPY_TEXT_BYTE_SIZE(format), unicode, &automatic_index, &numbering_mode, INT32_C(2), out_error);

    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_string_align_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *text = TINYPY_TUPLE_GET(args, 0U);
    int64_t width;
    uint8_t fill[4] = {(uint8_t)' ', 0U, 0U, 0U};
    size_t fill_size = 1U;
    size_t size = __tinypy_string_character_count(text);
    size_t byte_size = TINYPY_TEXT_BYTE_SIZE(text);
    size_t padding;
    size_t left;
    size_t right;
    tinypy_string_builder_t builder;
    intptr_t mode = (intptr_t)user_data;

    if (tinypy_internal_native_method_arguments(function, args, kwargs, 1U, 2U, TINYPY_ARITY_STYLE_PARSED, out_error) == TINYPY_FALSE) {
        return NULL;
    }
    tinypy_value_t *item = TINYPY_TUPLE_GET(args, 1U);
    if (__tinypy_string_integer(vm, item, &width, out_error) == 0) {
        return NULL;
    }
    if (TINYPY_TUPLE_SIZE(args) == 3U) {
        tinypy_value_t *fill_value = TINYPY_TUPLE_GET(args, 2U);

        if (TINYPY_VALUE_KIND(text) == TINYPY_VALUE_STRING) {
            if (TINYPY_VALUE_KIND(fill_value) != TINYPY_VALUE_STRING || TINYPY_TEXT_BYTE_SIZE(fill_value) != 1U) {
                tinypy_message_part_t parts[] = {
                    TINYPY_MESSAGE_PART_TEXT(tinypy_native_function_name(function)),
                    TINYPY_MESSAGE_PART_LITERAL("() argument 2 must be char, not "),
                    {TINYPY_VALUE_KIND(fill_value) == TINYPY_VALUE_NONE ? "None" : fill_value->type->name, TINYPY_VALUE_KIND(fill_value) == TINYPY_VALUE_NONE ? 4U : fill_value->type->name_size},
                };

                tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
                return NULL;
            }
            fill[0] = TINYPY_TEXT_BYTES(fill_value)[0];
        }
        else {
            tinypy_error_t *conversion_error = NULL;
            tinypy_value_t *converted = tinypy_internal_string_argument_text(vm, fill_value, TINYPY_TRUE, &conversion_error);

            if (converted != NULL && TINYPY_VALUE_KIND(converted) == TINYPY_VALUE_STRING) {
                tinypy_value_t *decoded = tinypy_internal_text_codec(vm, converted, NULL, NULL, TINYPY_TRUE, TINYPY_TRUE, NULL, &conversion_error);

                TINYPY_DECREF(converted);
                converted = decoded;
            }
            if (converted == NULL) {
                if (conversion_error != NULL) {
                    tinypy_error_release(conversion_error);
                }
                tinypy_vm_clear_error(vm);
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "The fill character cannot be converted to Unicode", out_error);
                return NULL;
            }
            if (__tinypy_string_character_count(converted) != 1U) {
                TINYPY_DECREF(converted);
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "The fill character must be exactly one character long", out_error);
                return NULL;
            }
            fill_size = TINYPY_TEXT_BYTE_SIZE(converted);
            (void)memcpy(fill, TINYPY_TEXT_BYTES(converted), fill_size);
            TINYPY_DECREF(converted);
        }
    }
    if (width <= 0 || (uint64_t)width <= size) {
        /* Centered subtypes clamp the two signed margins independently.
         * A margin of -1 at an odd width leaves one leading fill character. */
        if (mode != 0 || text->type == &vm->types[TINYPY_VALUE_KIND(text)] || width != (int64_t)size - INT64_C(1) || (width & INT64_C(1)) == 0) {
            tinypy_value_t *unchanged = __tinypy_string_from_span(vm, text, 0U, TINYPY_TEXT_BYTE_SIZE(text));
            return unchanged;
        }
        padding = 1U;
        left = 1U;
    }
    else {
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
static void __tinypy_string_join_item_error(tinypy_vm_t *vm, tinypy_value_t *item, size_t index, tinypy_bool_t unicode, tinypy_error_t **out_error) {
    char prefix[96];
    int prefix_size = snprintf(prefix, sizeof(prefix), "sequence item %zu: expected %s, ", index, unicode != 0 ? "string or Unicode" : "string");
    tinypy_message_part_t parts[] = {
        {prefix, (size_t)prefix_size},
        {item->type->name, item->type->name_size < 80U ? item->type->name_size : 80U},
        TINYPY_MESSAGE_PART_LITERAL(" found"),
    };

    tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
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
    if (sequence->type != &vm->types[TINYPY_VALUE_LIST] && sequence->type != &vm->types[TINYPY_VALUE_TUPLE]) {
        return NULL;
    }
    *out_handled = TINYPY_TRUE;
    count = kind == TINYPY_VALUE_LIST ? TINYPY_LIST_SIZE(sequence) : TINYPY_TUPLE_SIZE(sequence);
    unicode = TINYPY_VALUE_KIND(separator) == TINYPY_VALUE_UNICODE ? TINYPY_TRUE : TINYPY_FALSE;
    if (count == 1U) {
        tinypy_value_t *only = kind == TINYPY_VALUE_LIST ? TINYPY_LIST_GET(sequence, 0U) : TINYPY_TUPLE_GET(sequence, 0U);

        if (only->type == &vm->types[TINYPY_VALUE_UNICODE] || (unicode == 0 && only->type == &vm->types[TINYPY_VALUE_STRING])) {
            return TINYPY_RET(only);
        }
    }
    if (unicode == 0) {
        for (index = 0U; index < count; ++index) {
            tinypy_value_t *item = kind == TINYPY_VALUE_LIST ? TINYPY_LIST_GET(sequence, index) : TINYPY_TUPLE_GET(sequence, index);

            if (TINYPY_VALUE_KIND(item) == TINYPY_VALUE_UNICODE) {
                unicode = TINYPY_TRUE;
                break;
            }
            if (TINYPY_VALUE_KIND(item) != TINYPY_VALUE_STRING) {
                __tinypy_string_join_item_error(vm, item, index, TINYPY_FALSE, out_error);
                return NULL;
            }
        }
    }
    if (unicode != 0 && count > 1U && tinypy_internal_text_ascii_compatible(vm, separator, out_error) == 0) {
        return NULL;
    }
    for (index = 0U; index < count; ++index) {
        tinypy_value_t *item = kind == TINYPY_VALUE_LIST ? TINYPY_LIST_GET(sequence, index) : TINYPY_TUPLE_GET(sequence, index);
        size_t item_size;

        if (TINYPY_VALUE_KIND(item) != TINYPY_VALUE_STRING && TINYPY_VALUE_KIND(item) != TINYPY_VALUE_UNICODE) {
            __tinypy_string_join_item_error(vm, item, index, unicode, out_error);
            return NULL;
        }
        if (unicode != 0 && tinypy_internal_text_ascii_compatible(vm, item, out_error) == 0) {
            return NULL;
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

    (void)user_data;
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 1U, 1U, TINYPY_ARITY_STYLE_SINGLE, out_error) == TINYPY_FALSE) {
        return NULL;
    }
    tinypy_value_t *item_2 = TINYPY_TUPLE_GET(args, 1U);
    tinypy_bool_t handled;
    tinypy_value_t *sequence_result = __tinypy_string_join_sequence(vm, separator, item_2, &handled, out_error);

    if (handled != 0) {
        return sequence_result;
    }
    tinypy_error_t *iterator_error = NULL;
    tinypy_value_t *iterator = tinypy_iter(item_2, &iterator_error);
    if (iterator == NULL) {
        if (iterator_error != NULL && tinypy_error_kind(iterator_error) == TINYPY_ERROR_TYPE) {
            tinypy_error_release(iterator_error);
            tinypy_vm_clear_error(vm);
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "can only join an iterable", out_error);
        }
        else if (out_error != NULL) {
            *out_error = iterator_error;
        }
        else if (iterator_error != NULL) {
            tinypy_error_release(iterator_error);
        }
        return NULL;
    }
    tinypy_value_t *sequence = tinypy_list_from_items(vm, NULL, 0U);
    tinypy_bool_t collected = tinypy_internal_list_extend_iterable(sequence, iterator, "error return without exception set", out_error);

    TINYPY_DECREF(iterator);
    if (collected == 0) {
        TINYPY_DECREF(sequence);
        return NULL;
    }
    sequence_result = __tinypy_string_join_sequence(vm, separator, sequence, &handled, out_error);
    TINYPY_DECREF(sequence);
    return sequence_result;
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
    if (return_value == 0U) {
        /* Unicode objects hold canonical UTF-8; a damaged byte still has to
           advance the scan instead of stalling every loop built on it. */
        *out_code_point = UINT32_C(0xfffd);
        return_value = 1U;
    }
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
    if (return_value == 0U) {
        *out_code_point = UINT32_C(0xfffd);
        return_value = offset - begin;
    }
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
    /* Methods of a str or unicode subclass return the exact base type. */
    if (begin == 0U && end == TINYPY_TEXT_BYTE_SIZE(source) && source->type == &vm->types[TINYPY_VALUE_KIND(source)]) {
        return TINYPY_RET((tinypy_value_t *)source);
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
/* _PyEval_SliceIndex: a bound is None, an integer or an __index__ object. */
static int64_t __tinypy_string_normalized_bound(tinypy_vm_t *vm, tinypy_value_t *value, size_t length, int64_t fallback, tinypy_bool_t clamp_upper, tinypy_error_t **out_error) {
    int64_t bound;

    if (value == NULL || TINYPY_VALUE_KIND(value) == TINYPY_VALUE_NONE) {
        return fallback;
    }
    if (tinypy_internal_object_has_special_key(value, vm->internal_special_index_key) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "slice indices must be integers or None or have an __index__ method", out_error);
        return INT64_MIN;
    }
    if (tinypy_internal_index_as_i64(value, &bound, TINYPY_TRUE, out_error) == 0) {
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
tinypy_value_t *tinypy_internal_string_argument_text(tinypy_vm_t *vm, tinypy_value_t *value, tinypy_bool_t unicode, tinypy_error_t **out_error) {
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);

    if (kind == TINYPY_VALUE_STRING || kind == TINYPY_VALUE_UNICODE) {
        return TINYPY_RET(value);
    }
    if (kind == TINYPY_VALUE_BUFFER) {
        tinypy_value_t *result = tinypy_internal_buffer_character_string(value, out_error);

        return result;
    }
    if (unicode == 0 && kind == TINYPY_VALUE_BYTEARRAY) {
        const uint8_t *bytes;
        size_t size;

        (void)tinypy_internal_bytes_view(value, &bytes, &size);
        tinypy_value_t *result = tinypy_internal_string_from_bytes_checked(vm, bytes, size, out_error);
        return result;
    }
    if (unicode == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "expected a string or other character buffer object", out_error);
    }
    else if (kind == TINYPY_VALUE_BYTEARRAY) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "decoding bytearray is not supported", out_error);
    }
    else {
        tinypy_message_part_t parts[] = {
            TINYPY_MESSAGE_PART_LITERAL("coercing to Unicode: need string or buffer, "),
            TINYPY_MESSAGE_PART_TYPE_NAME(value),
            TINYPY_MESSAGE_PART_LITERAL(" found")
        };

        tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, 3U, out_error);
    }
    return NULL;
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

    size_t supplied = TINYPY_TUPLE_SIZE(args) - 1U;
    if (TINYPY_VALUE_KIND(TINYPY_TUPLE_GET(args, 0U)) == TINYPY_VALUE_STRING && (supplied < 1U || supplied > 3U) && (kwargs == NULL || TINYPY_DICT_SIZE(kwargs) == 0U)) {
        tinypy_internal_make_arity_error(vm, "find/rfind/index/rindex", sizeof("find/rfind/index/rindex") - 1U, supplied, 1U, 3U, TINYPY_ARITY_STYLE_PARSED, out_error);
        return NULL;
    }
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 1U, 3U, TINYPY_ARITY_STYLE_PARSED, out_error) == TINYPY_FALSE) {
        return NULL;
    }
    tinypy_value_t *text = TINYPY_TUPLE_GET(args, 0U);
    length = __tinypy_string_character_count(text);
    start = __tinypy_string_optional_bound(vm, args, 2U, length, 0, TINYPY_FALSE, out_error);
    if (start == INT64_MIN && out_error != NULL && *out_error != NULL) {
        return NULL;
    }
    end = __tinypy_string_optional_bound(vm, args, 3U, length, (int64_t)length, TINYPY_TRUE, out_error);
    if (end == INT64_MIN && out_error != NULL && *out_error != NULL) {
        return NULL;
    }
    tinypy_value_t *needle = tinypy_internal_string_argument_text(vm, TINYPY_TUPLE_GET(args, 1U), TINYPY_VALUE_KIND(text) == TINYPY_VALUE_UNICODE, out_error);
    if (needle == NULL) {
        return NULL;
    }
    if (__tinypy_string_require_compatible(vm, text, needle, out_error) == 0) {
        TINYPY_DECREF(needle);
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
        TINYPY_DECREF(needle);
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "substring not found", out_error);
        return NULL;
    }
    tinypy_value_t *return_value_1 = tinypy_integer_from_i64(vm, (int64_t)found);
    TINYPY_DECREF(needle);
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

    if (tinypy_internal_native_method_arguments(function, args, kwargs, 1U, 3U, TINYPY_ARITY_STYLE_PARSED, out_error) == TINYPY_FALSE) {
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
            tinypy_value_t *item = tinypy_internal_string_argument_text(vm, *iterator, TINYPY_VALUE_KIND(text) == TINYPY_VALUE_UNICODE, out_error);

            if (item == NULL) {
                return NULL;
            }
            if (__tinypy_string_require_compatible(vm, text, item, out_error) == 0) {
                TINYPY_DECREF(item);
                return NULL;
            }
            if ((TINYPY_TEXT_BYTE_SIZE(item) == 0U && (TINYPY_VALUE_KIND(text) == TINYPY_VALUE_UNICODE || TINYPY_VALUE_KIND(item) == TINYPY_VALUE_UNICODE)) || (invalid_bounds == 0 && __tinypy_string_matches_at(text, begin, finish, item, suffix) != 0)) {
                tinypy_value_t *return_value_2 = TINYPY_RET_TRUE(vm);
                TINYPY_DECREF(item);
                return return_value_2;
            }
            TINYPY_DECREF(item);
        }
        tinypy_value_t *return_value_3 = TINYPY_RET_FALSE(vm);
        return return_value_3;
    }
    tinypy_value_type_e candidate_kind = TINYPY_VALUE_KIND(candidate);
    if (candidate_kind != TINYPY_VALUE_STRING && candidate_kind != TINYPY_VALUE_UNICODE && candidate_kind != TINYPY_VALUE_BUFFER && (candidate_kind != TINYPY_VALUE_BYTEARRAY || TINYPY_VALUE_KIND(text) == TINYPY_VALUE_UNICODE)) {
        tinypy_message_part_t parts[] = {
            {suffix != 0 ? "endswith" : "startswith", suffix != 0 ? 8U : 10U},
            TINYPY_MESSAGE_PART_LITERAL(" first arg must be str, unicode, or tuple, not "),
            TINYPY_MESSAGE_PART_TYPE_NAME(candidate)
        };

        tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, 3U, out_error);
        return NULL;
    }
    candidate = tinypy_internal_string_argument_text(vm, candidate, TINYPY_VALUE_KIND(text) == TINYPY_VALUE_UNICODE, out_error);
    if (candidate == NULL) {
        return NULL;
    }
    if (__tinypy_string_require_compatible(vm, text, candidate, out_error) == 0) {
        TINYPY_DECREF(candidate);
        return NULL;
    }
    if (TINYPY_TEXT_BYTE_SIZE(candidate) == 0U && (TINYPY_VALUE_KIND(text) == TINYPY_VALUE_UNICODE || TINYPY_VALUE_KIND(candidate) == TINYPY_VALUE_UNICODE)) {
        tinypy_value_t *return_value_4 = TINYPY_RET_TRUE(vm);
        TINYPY_DECREF(candidate);
        return return_value_4;
    }
    if (invalid_bounds != 0) {
        tinypy_value_t *return_value_5 = TINYPY_RET_FALSE(vm);
        TINYPY_DECREF(candidate);
        return return_value_5;
    }
    tinypy_bool_t string_matches_at = __tinypy_string_matches_at(text, begin, finish, candidate, suffix);
    tinypy_value_t *return_value_6 = tinypy_bool_from_i32(vm, string_matches_at);
    TINYPY_DECREF(candidate);
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
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 1U, 3U, TINYPY_ARITY_STYLE_PARSED, out_error) == TINYPY_FALSE) {
        return NULL;
    }
    tinypy_value_t *text = TINYPY_TUPLE_GET(args, 0U);
    length = __tinypy_string_character_count(text);
    start = __tinypy_string_optional_bound(vm, args, 2U, length, 0, TINYPY_FALSE, out_error);
    if (start == INT64_MIN && out_error != NULL && *out_error != NULL) {
        return NULL;
    }
    end = __tinypy_string_optional_bound(vm, args, 3U, length, (int64_t)length, TINYPY_TRUE, out_error);
    if (end == INT64_MIN && out_error != NULL && *out_error != NULL) {
        return NULL;
    }
    tinypy_value_t *needle = tinypy_internal_string_argument_text(vm, TINYPY_TUPLE_GET(args, 1U), TINYPY_VALUE_KIND(text) == TINYPY_VALUE_UNICODE, out_error);
    if (needle == NULL) {
        return NULL;
    }
    if (__tinypy_string_require_compatible(vm, text, needle, out_error) == 0) {
        TINYPY_DECREF(needle);
        return NULL;
    }
    if ((uint64_t)start > (uint64_t)length || start > end) {
        tinypy_value_t *return_value_1 = tinypy_integer_from_i64(vm, 0);
        TINYPY_DECREF(needle);
        return return_value_1;
    }
    begin = __tinypy_string_byte_offset(text, (size_t)start);
    finish = __tinypy_string_byte_offset(text, (size_t)end);
    needle_size = TINYPY_TEXT_BYTE_SIZE(needle);
    if (needle_size == 0U) {
        tinypy_value_t *return_value_2 = tinypy_integer_from_i64(vm, (int64_t)((size_t)(end - start) + 1U));
        TINYPY_DECREF(needle);
        return return_value_2;
    }
    tinypy_string_search_plan_t count_plan;

    __tinypy_string_search_plan_initialize(&count_plan, TINYPY_TEXT_BYTES(needle), needle_size, TINYPY_FALSE);
    offset = begin;
    while (offset + needle_size <= finish) {
        const uint8_t *bytes = TINYPY_TEXT_BYTES(text);
        ptrdiff_t found = __tinypy_string_search_plan_find(&count_plan, bytes + offset, finish - offset);

        if (found < 0) {
            break;
        }
        count += 1U;
        offset += (size_t)found + needle_size;
    }
    tinypy_value_t *return_value_3 = tinypy_integer_from_i64(vm, (int64_t)count);
    TINYPY_DECREF(needle);
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

    if (tinypy_internal_native_method_arguments(function, args, kwargs, 0U, 1U, TINYPY_ARITY_STYLE_PARSED, out_error) == TINYPY_FALSE) {
        return NULL;
    }
    tinypy_value_t *text = TINYPY_TUPLE_GET(args, 0U);
    if (TINYPY_TUPLE_SIZE(args) == 2U) {
        characters = TINYPY_TUPLE_GET(args, 1U);
        if (TINYPY_VALUE_KIND(characters) != TINYPY_VALUE_NONE && __tinypy_string_is_text(characters) == 0) {
            tinypy_message_part_t parts[] = {
                {mode == 0 ? "strip" : (mode < 0 ? "lstrip" : "rstrip"), mode == 0 ? 5U : 6U},
                TINYPY_MESSAGE_PART_LITERAL(" arg must be None, "),
                {TINYPY_VALUE_KIND(text) == TINYPY_VALUE_UNICODE ? "unicode or str" : "str or unicode", 14U}
            };

            tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, 3U, out_error);
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
static tinypy_value_t *__tinypy_string_replace_text(tinypy_vm_t *vm, tinypy_value_t *text, tinypy_value_t *old_value, tinypy_value_t *new_value, int64_t maximum, tinypy_error_t **out_error) {
    const uint8_t *bytes;
    size_t size;
    size_t old_size;
    size_t new_size;
    size_t offset = 0U;
    size_t replaced = 0U;
    tinypy_bool_t unicode;
    tinypy_string_builder_t builder;
    tinypy_string_search_plan_t search_plan;

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
static tinypy_value_t *__tinypy_string_replace_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    int64_t maximum = -1;

    (void)user_data;
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 2U, 3U, TINYPY_ARITY_STYLE_PARSED, out_error) == TINYPY_FALSE) {
        return NULL;
    }
    if (TINYPY_TUPLE_SIZE(args) == 4U && __tinypy_string_integer(vm, TINYPY_TUPLE_GET(args, 3U), &maximum, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *text = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *old_argument = TINYPY_TUPLE_GET(args, 1U);
    tinypy_value_t *new_argument = TINYPY_TUPLE_GET(args, 2U);
    tinypy_bool_t unicode = TINYPY_VALUE_KIND(text) == TINYPY_VALUE_UNICODE || TINYPY_VALUE_KIND(old_argument) == TINYPY_VALUE_UNICODE || TINYPY_VALUE_KIND(new_argument) == TINYPY_VALUE_UNICODE;
    tinypy_value_t *old_value = tinypy_internal_string_argument_text(vm, old_argument, unicode, out_error);

    if (old_value == NULL) {
        return NULL;
    }
    tinypy_value_t *new_value = tinypy_internal_string_argument_text(vm, new_argument, unicode, out_error);
    tinypy_value_t *result = new_value != NULL ? __tinypy_string_replace_text(vm, text, old_value, new_value, maximum, out_error) : NULL;

    if (new_value != NULL) {
        TINYPY_DECREF(new_value);
    }
    TINYPY_DECREF(old_value);
    return result;
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
static tinypy_value_t *__tinypy_string_split_text(tinypy_vm_t *vm, tinypy_value_t *text, tinypy_value_t *separator, int64_t maximum, tinypy_bool_t reverse, tinypy_error_t **out_error) {
    const uint8_t *bytes;
    size_t size;
    size_t separator_size = 0U;
    size_t splits = 0U;
    tinypy_bool_t unicode;
    tinypy_string_search_plan_t search_plan;

    unicode = TINYPY_VALUE_KIND(text) == TINYPY_VALUE_UNICODE;
    if (separator != NULL) {
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
static tinypy_value_t *__tinypy_string_split_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    int64_t maximum = -1;
    tinypy_value_t *separator = NULL;

    if (tinypy_internal_native_method_arguments(function, args, kwargs, 0U, 2U, TINYPY_ARITY_STYLE_PARSED, out_error) == TINYPY_FALSE) {
        return NULL;
    }
    if (TINYPY_TUPLE_SIZE(args) == 3U && __tinypy_string_integer(vm, TINYPY_TUPLE_GET(args, 2U), &maximum, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *text = TINYPY_TUPLE_GET(args, 0U);
    if (TINYPY_TUPLE_SIZE(args) >= 2U && TINYPY_VALUE_KIND(TINYPY_TUPLE_GET(args, 1U)) != TINYPY_VALUE_NONE) {
        separator = tinypy_internal_string_argument_text(vm, TINYPY_TUPLE_GET(args, 1U), TINYPY_VALUE_KIND(text) == TINYPY_VALUE_UNICODE, out_error);
        if (separator == NULL) {
            return NULL;
        }
    }
    tinypy_value_t *result = __tinypy_string_split_text(vm, text, separator, maximum, user_data != NULL, out_error);

    if (separator != NULL) {
        TINYPY_DECREF(separator);
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_unicode_translate_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error);
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_string_character_buffer(tinypy_vm_t *vm, tinypy_value_t *value, const uint8_t **out_bytes, size_t *out_size, tinypy_error_t **out_error) {
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);

    if ((kind == TINYPY_VALUE_STRING || kind == TINYPY_VALUE_BUFFER || kind == TINYPY_VALUE_BYTEARRAY) && tinypy_internal_bytes_view(value, out_bytes, out_size) != 0) {
        return TINYPY_TRUE;
    }
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "expected a string or other character buffer object", out_error);
    return TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_string_translate_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *delete_characters = NULL;
    tinypy_value_t *table_buffer = NULL;
    tinypy_value_t *delete_buffer = NULL;
    tinypy_value_t *result = NULL;
    const uint8_t *source;
    const uint8_t *translation;
    const uint8_t *deleted = NULL;
    size_t source_size;
    size_t translation_size;
    size_t deleted_size = 0U;
    uint8_t *output;
    size_t input_index;
    size_t output_size = 0U;
    tinypy_bool_t changed = TINYPY_FALSE;
    uint8_t deleted_flags[32] = {0U};

    (void)user_data;
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 1U, 2U, TINYPY_ARITY_STYLE_UNPACK, out_error) == TINYPY_FALSE) {
        return NULL;
    }
    tinypy_value_t *text = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *table = TINYPY_TUPLE_GET(args, 1U);
    if (TINYPY_VALUE_KIND(text) != TINYPY_VALUE_STRING) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "translate requires byte strings", out_error);
        return NULL;
    }
    translation = NULL;
    if (TINYPY_VALUE_KIND(table) == TINYPY_VALUE_UNICODE) {
        if (TINYPY_TUPLE_SIZE(args) == 3U) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "deletions are implemented differently for unicode", out_error);
            return NULL;
        }
        tinypy_value_t *unicode_text = tinypy_internal_text_codec(vm, text, NULL, NULL, TINYPY_TRUE, TINYPY_TRUE, NULL, out_error);
        if (unicode_text == NULL) {
            return NULL;
        }
        tinypy_value_t *items[] = {unicode_text, table};
        tinypy_value_t *translated_args = tinypy_tuple_from_items(vm, items, 2U);
        result = __tinypy_unicode_translate_method(function, translated_args, NULL, NULL, out_error);

        TINYPY_DECREF(translated_args);
        TINYPY_DECREF(unicode_text);
        return result;
    }
    if (TINYPY_VALUE_KIND(table) != TINYPY_VALUE_NONE) {
        if (TINYPY_VALUE_KIND(table) == TINYPY_VALUE_BUFFER) {
            table_buffer = tinypy_internal_buffer_character_string(table, out_error);
            if (table_buffer == NULL) {
                return NULL;
            }
            table = table_buffer;
        }
        if (__tinypy_string_character_buffer(vm, table, &translation, &translation_size, out_error) == 0) {
            goto cleanup;
        }
        if (translation_size != 256U) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "translation table must be 256 characters long", out_error);
            goto cleanup;
        }
    }
    if (TINYPY_TUPLE_SIZE(args) == 3U) {
        delete_characters = TINYPY_TUPLE_GET(args, 2U);
        if (TINYPY_VALUE_KIND(delete_characters) == TINYPY_VALUE_UNICODE) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "deletions are implemented differently for unicode", out_error);
            goto cleanup;
        }
        if (TINYPY_VALUE_KIND(delete_characters) == TINYPY_VALUE_BUFFER) {
            delete_buffer = tinypy_internal_buffer_character_string(delete_characters, out_error);
            if (delete_buffer == NULL) {
                goto cleanup;
            }
            delete_characters = delete_buffer;
        }
        if (__tinypy_string_character_buffer(vm, delete_characters, &deleted, &deleted_size, out_error) == 0) {
            goto cleanup;
        }
        for (input_index = 0U; input_index < deleted_size; ++input_index) {
            deleted_flags[deleted[input_index] >> 3U] |= (uint8_t)(1U << (deleted[input_index] & 7U));
        }
    }
    source = TINYPY_TEXT_BYTES(text);
    source_size = TINYPY_TEXT_BYTE_SIZE(text);
    if (source_size == 0U) {
        result = TINYPY_RET_EMPTY_STRING(vm);
        goto cleanup;
    }
    for (input_index = 0U; input_index < source_size; ++input_index) {
        uint8_t character = source[input_index];
        if ((deleted_flags[character >> 3U] & (uint8_t)(1U << (character & 7U))) == 0U) {
            output_size += 1U;
            if (translation != NULL && translation[character] != character) {
                changed = TINYPY_TRUE;
            }
        }
        else {
            changed = TINYPY_TRUE;
        }
    }
    if (changed == 0 && text->type == &vm->types[TINYPY_VALUE_STRING]) {
        result = TINYPY_RET(text);
        goto cleanup;
    }
    if (output_size == 0U) {
        result = TINYPY_RET_EMPTY_STRING(vm);
        goto cleanup;
    }
    result = tinypy_internal_text_allocate_uninitialized_checked(vm, TINYPY_VALUE_STRING, output_size, output_size, &output, out_error);

    if (result == NULL) {
        goto cleanup;
    }
    output_size = 0U;
    for (input_index = 0U; input_index < source_size; ++input_index) {
        uint8_t character = source[input_index];
        if ((deleted_flags[character >> 3U] & (uint8_t)(1U << (character & 7U))) == 0U) {
            output[output_size++] = translation != NULL ? translation[character] : character;
        }
    }
cleanup:
    if (table_buffer != NULL) {
        TINYPY_DECREF(table_buffer);
    }
    if (delete_buffer != NULL) {
        TINYPY_DECREF(delete_buffer);
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_unicode_translate_lookup(tinypy_vm_t *vm, tinypy_value_t *table, uint32_t code_point, tinypy_value_t **out_replacement, tinypy_error_t **out_error) {
    tinypy_value_t *key = tinypy_integer_from_i64(vm, (int64_t)code_point);
    tinypy_error_t *lookup_error = NULL;
    tinypy_value_t *replacement = tinypy_get_item(table, key, &lookup_error);

    TINYPY_DECREF(key);
    *out_replacement = NULL;
    if (replacement == NULL) {
        if (lookup_error != NULL && (tinypy_error_kind(lookup_error) == TINYPY_ERROR_KEY || tinypy_error_kind(lookup_error) == TINYPY_ERROR_INDEX || tinypy_error_kind(lookup_error) == TINYPY_ERROR_LOOKUP)) {
            tinypy_error_release(lookup_error);
            tinypy_vm_clear_error(vm);
            return TINYPY_TRUE;
        }
        if (out_error != NULL) {
            *out_error = lookup_error;
        }
        else if (lookup_error != NULL) {
            tinypy_error_release(lookup_error);
        }
        return TINYPY_FALSE;
    }
    tinypy_value_type_e replacement_kind = TINYPY_VALUE_KIND(replacement);

    if (replacement_kind != TINYPY_VALUE_NONE && replacement_kind != TINYPY_VALUE_UNICODE) {
        if (replacement_kind != TINYPY_VALUE_BOOL && replacement_kind != TINYPY_VALUE_INTEGER) {
            TINYPY_DECREF(replacement);
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "character mapping must return integer, None or unicode", out_error);
            return TINYPY_FALSE;
        }
        int64_t mapped = TINYPY_INTEGER_VALUE(replacement);

        if (mapped < 0 || mapped > INT64_C(0x10ffff)) {
            TINYPY_DECREF(replacement);
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "character mapping must be in range(0x%lx)", out_error);
            return TINYPY_FALSE;
        }
    }
    *out_replacement = replacement;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_unicode_translate_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *text;
    tinypy_value_t *table;
    tinypy_string_builder_t builder;
    size_t offset = 0U;

    (void)user_data;
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 1U, 1U, TINYPY_ARITY_STYLE_SINGLE, out_error) == TINYPY_FALSE) {
        return NULL;
    }
    text = TINYPY_TUPLE_GET(args, 0U);
    table = TINYPY_TUPLE_GET(args, 1U);
    if (TINYPY_TEXT_BYTE_SIZE(text) != 0U && TINYPY_VALUE_KIND(table) != TINYPY_VALUE_OLD_INSTANCE && tinypy_internal_object_has_special_key(table, vm->internal_special_getitem_key) == 0 && (table->type->mapping_slots == NULL || table->type->mapping_slots->get_item == NULL) && (table->type->sequence_slots == NULL || table->type->sequence_slots->get_item == NULL)) {
        tinypy_message_part_t parts[] = {
            TINYPY_MESSAGE_PART_LITERAL("'"),
            TINYPY_MESSAGE_PART_TYPE_NAME(table),
            TINYPY_MESSAGE_PART_LITERAL("' object has no attribute '__getitem__'"),
        };

        tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
        return NULL;
    }
    (void)memset(&builder, 0, sizeof(builder));
    builder.vm = vm;
    while (offset < TINYPY_TEXT_BYTE_SIZE(text)) {
        uint32_t code_point;
        size_t width = __tinypy_string_next_code_point(text, offset, &code_point);
        tinypy_value_t *replacement;

        if (__tinypy_unicode_translate_lookup(vm, table, code_point, &replacement, out_error) == TINYPY_FALSE) {
            __tinypy_string_builder_discard(&builder);
            return NULL;
        }
        if (replacement == NULL) {
            __tinypy_string_builder_append(&builder, TINYPY_TEXT_BYTES(text) + offset, width);
            offset += width;
            continue;
        }
        if (TINYPY_VALUE_KIND(replacement) == TINYPY_VALUE_NONE) {
            TINYPY_DECREF(replacement);
            offset += width;
            while (offset < TINYPY_TEXT_BYTE_SIZE(text)) {
                width = __tinypy_string_next_code_point(text, offset, &code_point);
                if (__tinypy_unicode_translate_lookup(vm, table, code_point, &replacement, out_error) == TINYPY_FALSE) {
                    __tinypy_string_builder_discard(&builder);
                    return NULL;
                }
                if (replacement == NULL) {
                    break;
                }
                tinypy_bool_t deleted = TINYPY_VALUE_KIND(replacement) == TINYPY_VALUE_NONE ? TINYPY_TRUE : TINYPY_FALSE;

                TINYPY_DECREF(replacement);
                if (deleted == TINYPY_FALSE) {
                    break;
                }
                offset += width;
            }
            continue;
        }
        if (TINYPY_VALUE_KIND(replacement) == TINYPY_VALUE_UNICODE) {
            __tinypy_string_builder_append(&builder, TINYPY_TEXT_BYTES(replacement), TINYPY_TEXT_BYTE_SIZE(replacement));
        }
        else {
            int64_t mapped = TINYPY_INTEGER_VALUE(replacement);

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
    tinypy_bool_t changed = TINYPY_FALSE;

    if (tinypy_internal_native_method_arguments(function, args, kwargs, 0U, 0U, TINYPY_ARITY_STYLE_PARSED, out_error) == TINYPY_FALSE) {
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
            tinypy_bool_t rebuild = mapped != character;
            if (mode == 2) {
                rebuild = tinypy_internal_unicode_is_lower(character) != 0 || tinypy_internal_unicode_is_upper(character) != 0;
            }
            else if (mode == 3) {
                rebuild = offset == 0U ? tinypy_internal_unicode_is_upper(character) == 0 : tinypy_internal_unicode_is_lower(character) == 0;
            }
            else if (mode == 4 && TINYPY_SIZED_SIZE(text) != 1U) {
                rebuild = TINYPY_TRUE;
            }
            if (rebuild != 0 && changed == 0) {
                __tinypy_string_builder_append(&builder, TINYPY_TEXT_BYTES(text), offset);
                changed = TINYPY_TRUE;
            }
            if (changed != 0) {
                __tinypy_string_builder_code_point(&builder, mapped);
            }
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
    if (kind == TINYPY_VALUE_UNICODE && changed == 0) {
        tinypy_value_t *unchanged = __tinypy_string_from_span_as(vm, text, 0U, size, TINYPY_TRUE);
        return unchanged;
    }
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

    if (tinypy_internal_native_method_arguments(function, args, kwargs, 0U, 0U, TINYPY_ARITY_STYLE_PARSED, out_error) == TINYPY_FALSE) {
        return NULL;
    }
    tinypy_value_t *text = TINYPY_TUPLE_GET(args, 0U);
    size = TINYPY_TEXT_BYTE_SIZE(text);
    if (size == 0U) {
        tinypy_value_t *return_value_1 = TINYPY_RET_FALSE(vm);
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
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 1U, 1U, TINYPY_ARITY_STYLE_PARSED, out_error) == TINYPY_FALSE) {
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
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 0U, 1U, TINYPY_ARITY_STYLE_PARSED, out_error) == TINYPY_FALSE) {
        return NULL;
    }
    tinypy_value_t *text = TINYPY_TUPLE_GET(args, 0U);
    if (TINYPY_TUPLE_SIZE(args) == 2U) {
        tinypy_value_t *item = TINYPY_TUPLE_GET(args, 1U);
        int64_t parsed_keep_ends;

        if (__tinypy_string_integer(vm, item, &parsed_keep_ends, out_error) == 0) {
            return NULL;
        }
        if (parsed_keep_ends < INT32_MIN || parsed_keep_ends > INT32_MAX) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "Python int too large to convert to C int", out_error);
            return NULL;
        }
        keep_ends = parsed_keep_ends != 0 ? INT32_C(1) : INT32_C(0);
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

    (void)user_data;
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 0U, 1U, TINYPY_ARITY_STYLE_PARSED, out_error) == TINYPY_FALSE) {
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
    if (tab_size > INT32_MAX || tab_size < INT32_MIN) {
        /* The tab size is a C int in Python 2.7, so larger values fail early. */
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "Python int too large to convert to C int", out_error);
        return NULL;
    }
    unicode = TINYPY_VALUE_KIND(text) == TINYPY_VALUE_UNICODE ? TINYPY_TRUE : TINYPY_FALSE;
    /* Two passes: the output size is known before any byte is written, so a
       huge tab size fails with OverflowError instead of looping. */
    const uint8_t *source = TINYPY_TEXT_BYTES(text);
    size_t source_size = TINYPY_TEXT_BYTE_SIZE(text);
    if (source_size == 0U) {
        tinypy_value_t *empty = __tinypy_string_from_span_as(vm, text, 0U, 0U, unicode);
        return empty;
    }
    size_t total = 0U;
    size_t added_spaces = 0U;
    size_t tabs = 0U;
    for (offset = 0U; offset < source_size; ++offset) {
        uint8_t character = source[offset];

        if (character == (uint8_t)'\t') {
            size_t spaces = tab_size > 0 ? (size_t)tab_size - column % (size_t)tab_size : 0U;

            if (spaces > (size_t)PTRDIFF_MAX - total) {
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "new string is too long", out_error);
                return NULL;
            }
            total += spaces;
            added_spaces += spaces;
            tabs += 1U;
            column += spaces;
        }
        else {
            total += 1U;
            if (character == (uint8_t)'\n' || character == (uint8_t)'\r') {
                column = 0U;
            }
            else if (unicode == 0 || (character & 0xc0U) != 0x80U) {
                column += 1U;
            }
        }
    }
    uint8_t *destination;
    tinypy_value_t *result = tinypy_internal_text_allocate_uninitialized_checked(vm, unicode != 0 ? TINYPY_VALUE_UNICODE : TINYPY_VALUE_STRING, total, TINYPY_SIZED_SIZE(text) - tabs + added_spaces, &destination, out_error);
    if (result == NULL) {
        return NULL;
    }
    column = 0U;
    size_t written = 0U;
    for (offset = 0U; offset < source_size; ++offset) {
        uint8_t character = source[offset];

        if (character == (uint8_t)'\t') {
            size_t spaces = tab_size > 0 ? (size_t)tab_size - column % (size_t)tab_size : 0U;

            if (spaces != 0U) {
                (void)memset(destination + written, ' ', spaces);
            }
            written += spaces;
            column += spaces;
        }
        else {
            destination[written] = character;
            written += 1U;
            if (character == (uint8_t)'\n' || character == (uint8_t)'\r') {
                column = 0U;
            }
            else if (unicode == 0 || (character & 0xc0U) != 0x80U) {
                column += 1U;
            }
        }
    }
    return result;
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

    if (tinypy_internal_native_method_arguments(function, args, kwargs, 1U, 1U, TINYPY_ARITY_STYLE_SINGLE, out_error) == TINYPY_FALSE) {
        return NULL;
    }
    tinypy_value_t *text = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *separator_argument = TINYPY_TUPLE_GET(args, 1U);
    tinypy_value_t *separator = tinypy_internal_string_argument_text(vm, separator_argument, TINYPY_VALUE_KIND(text) == TINYPY_VALUE_UNICODE, out_error);
    if (separator == NULL) {
        return NULL;
    }
    if (__tinypy_string_require_compatible(vm, text, separator, out_error) == 0) {
        TINYPY_DECREF(separator);
        return NULL;
    }
    unicode = TINYPY_VALUE_KIND(text) == TINYPY_VALUE_UNICODE || TINYPY_VALUE_KIND(separator) == TINYPY_VALUE_UNICODE;
    separator_size = TINYPY_TEXT_BYTE_SIZE(separator);
    if (separator_size == 0U) {
        TINYPY_DECREF(separator);
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "empty separator", out_error);
        return NULL;
    }
    size = TINYPY_TEXT_BYTE_SIZE(text);
    const uint8_t *bytes = TINYPY_TEXT_BYTES(text);
    const uint8_t *bytes_2 = TINYPY_TEXT_BYTES(separator);
    found = tinypy_internal_find_bytes(bytes, size, bytes_2, separator_size, reverse);
    if (found < 0) {
        if (reverse == 0) {
            items[0] = unicode == 0 ? TINYPY_RET(text) : __tinypy_string_from_span_as(vm, text, 0U, size, TINYPY_TRUE);
            items[1] = __tinypy_string_from_span_as(vm, text, 0U, 0U, unicode);
            items[2] = __tinypy_string_from_span_as(vm, text, 0U, 0U, unicode);
        }
        else {
            items[0] = __tinypy_string_from_span_as(vm, text, 0U, 0U, unicode);
            items[1] = __tinypy_string_from_span_as(vm, text, 0U, 0U, unicode);
            items[2] = unicode == 0 ? TINYPY_RET(text) : __tinypy_string_from_span_as(vm, text, 0U, size, TINYPY_TRUE);
        }
    }
    else {
        items[0] = __tinypy_string_from_span_as(vm, text, 0U, (size_t)found, unicode);
        if (unicode == 0) {
            items[1] = TINYPY_RET(separator_argument);
        }
        else {
            items[1] = __tinypy_string_from_span_as(vm, separator, 0U, separator_size, TINYPY_TRUE);
        }
        items[2] = __tinypy_string_from_span_as(vm, text, (size_t)found + separator_size, size, unicode);
    }
    result = tinypy_tuple_from_items(vm, items, 3U);
    TINYPY_DECREF(items[2]);
    TINYPY_DECREF(items[1]);
    TINYPY_DECREF(items[0]);
    TINYPY_DECREF(separator);
    return result;
}
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_codec_error_mode(tinypy_vm_t *vm, tinypy_value_t *value) {
    if (value == NULL || TINYPY_NAME_EQ(value, vm->internal_codec_strict_name) != 0) {
        return 0;
    }
    if (TINYPY_NAME_EQ(value, vm->internal_ignore_key) != 0) {
        return 1;
    }
    if (TINYPY_NAME_EQ(value, vm->internal_replace_key) != 0) {
        return 2;
    }
    if (TINYPY_NAME_EQ(value, vm->internal_xmlcharrefreplace_key) != 0) {
        return 3;
    }
    if (TINYPY_NAME_EQ(value, vm->internal_backslashreplace_key) != 0) {
        return 4;
    }
    return 5;
}
//////////////////////////////////////////////////////////////////////////
static const char *__tinypy_codec_utf8_decode_reason(const uint8_t *bytes, size_t size) {
    uint8_t first = size != 0U ? bytes[0] : 0U;
    size_t expected;
    size_t index;

    if (first < 0xc2U || first > 0xf4U) {
        return "invalid start byte";
    }
    expected = first < 0xe0U ? 2U : (first < 0xf0U ? 3U : 4U);
    if (size < expected) {
        return "unexpected end of data";
    }
    for (index = 1U; index < expected; ++index) {
        if ((bytes[index] & 0xc0U) != 0x80U) {
            break;
        }
    }
    return "invalid continuation byte";
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_codec_unicode_reason(tinypy_vm_t *vm, tinypy_value_t *text, int32_t codec, tinypy_bool_t decode, size_t start) {
    const char *reason_text = decode != 0 ? (codec == 0 ? "ordinal not in range(128)" : __tinypy_codec_utf8_decode_reason(TINYPY_TEXT_BYTES(text) + start, TINYPY_TEXT_BYTE_SIZE(text) - start)) : (codec == 0 ? "ordinal not in range(128)" : "ordinal not in range(256)");
    tinypy_value_t *result = tinypy_string_from_bytes(vm, reason_text, strlen(reason_text));
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_codec_unicode_exception(tinypy_vm_t *vm, tinypy_value_t *text, tinypy_value_t *encoding, int32_t codec, tinypy_bool_t decode, size_t start, size_t end, tinypy_error_t **out_error) {
    tinypy_value_t *canonical = codec == 0 ? vm->internal_codec_ascii_name : (codec == 1 ? vm->internal_codec_utf8_name : vm->internal_codec_latin1_name);
    tinypy_value_t *encoding_value = TINYPY_RET(canonical);

    (void)encoding;
    tinypy_value_t *start_value = tinypy_integer_from_i64(vm, (int64_t)start);
    tinypy_value_t *end_value = tinypy_integer_from_i64(vm, (int64_t)end);
    tinypy_value_t *reason = __tinypy_codec_unicode_reason(vm, text, codec, decode, start);
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
static void __tinypy_codec_update_unicode_exception(tinypy_vm_t *vm, tinypy_value_t *exception, tinypy_value_t *text, int32_t codec, tinypy_bool_t decode, size_t start, size_t end) {
    tinypy_internal_unicode_error_payload_t *payload = (tinypy_internal_unicode_error_payload_t *)tinypy_native_instance_payload(exception);
    tinypy_value_t *previous_reason = payload->reason;

    payload->start = (int64_t)start;
    payload->end = (int64_t)end;
    payload->reason = __tinypy_codec_unicode_reason(vm, text, codec, decode, start);
    if (previous_reason != NULL) {
        TINYPY_DECREF(previous_reason);
    }
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
tinypy_bool_t tinypy_internal_raise_ascii_decode_error(tinypy_vm_t *vm, const tinypy_value_t *text, size_t start, size_t end, tinypy_error_t **out_error) {
    tinypy_bool_t return_value_1 = __tinypy_codec_raise_unicode_error(vm, (tinypy_value_t *)text, NULL, INT32_C(0), TINYPY_TRUE, start, end, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_codec_append_encoded_replacement(tinypy_string_builder_t *builder, tinypy_value_t *replacement, int32_t codec) {
    const uint8_t *bytes = TINYPY_TEXT_BYTES(replacement);
    size_t size = TINYPY_TEXT_BYTE_SIZE(replacement);
    size_t offset = 0U;

    while (offset != size) {
        uint32_t code_point;
        size_t width = tinypy_internal_utf8_decode(bytes + offset, size - offset, &code_point);

        if (width == 0U || (codec == 0 && code_point > UINT32_C(0x7f)) || (codec == 2 && code_point > UINT32_C(0xff))) {
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
typedef struct tinypy_codec_error_state_t {
    tinypy_value_t *handler;
    tinypy_value_t *exception;
} tinypy_codec_error_state_t;
//////////////////////////////////////////////////////////////////////////
static void __tinypy_codec_error_state_finalize(tinypy_codec_error_state_t *state) {
    if (state->handler != NULL) {
        TINYPY_DECREF(state->handler);
    }
    if (state->exception != NULL) {
        TINYPY_DECREF(state->exception);
    }
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_codec_call_error_handler(tinypy_vm_t *vm, tinypy_string_builder_t *builder, tinypy_codec_error_state_t *state, tinypy_value_t *text, tinypy_value_t *encoding, tinypy_value_t *errors, int32_t codec, tinypy_bool_t decode, size_t start, size_t end, size_t *out_next, tinypy_error_t **out_error) {
    tinypy_value_t *handler_args;
    tinypy_value_t *result;
    tinypy_value_t *replacement;
    tinypy_value_t *position;
    int64_t next;
    size_t input_length = decode != 0 ? TINYPY_TEXT_BYTE_SIZE(text) : TINYPY_SIZED_SIZE(text);

    if (state->handler == NULL) {
        state->handler = tinypy_internal_codecs_lookup_error(vm, errors, out_error);
        if (state->handler == NULL) {
            return TINYPY_FALSE;
        }
    }
    if (state->exception == NULL) {
        state->exception = __tinypy_codec_unicode_exception(vm, text, encoding, codec, decode, start, end, out_error);
        if (state->exception == NULL) {
            return TINYPY_FALSE;
        }
    }
    else {
        __tinypy_codec_update_unicode_exception(vm, state->exception, text, codec, decode, start, end);
    }
    handler_args = tinypy_tuple_from_items(vm, &state->exception, 1U);
    result = tinypy_call(state->handler, handler_args, NULL, out_error);
    TINYPY_DECREF(handler_args);
    if (result == NULL) {
        return TINYPY_FALSE;
    }
    if (TINYPY_VALUE_KIND(result) != TINYPY_VALUE_TUPLE || TINYPY_TUPLE_SIZE(result) != 2U) {
        TINYPY_DECREF(result);
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, decode != 0 ? "decoding error handler must return (unicode, int) tuple" : "encoding error handler must return (unicode, int) tuple", out_error);
        return TINYPY_FALSE;
    }
    replacement = TINYPY_TUPLE_GET(result, 0U);
    position = TINYPY_TUPLE_GET(result, 1U);
    if (TINYPY_VALUE_KIND(replacement) != TINYPY_VALUE_UNICODE) {
        TINYPY_DECREF(result);
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, decode != 0 ? "decoding error handler must return (unicode, int) tuple" : "encoding error handler must return (unicode, int) tuple", out_error);
        return TINYPY_FALSE;
    }
    if (TINYPY_VALUE_KIND(position) == TINYPY_VALUE_FLOAT) {
        TINYPY_DECREF(result);
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "integer argument expected, got float", out_error);
        return TINYPY_FALSE;
    }
    if (tinypy_internal_integer_as_ssize(position, &next, out_error) == 0) {
        TINYPY_DECREF(result);
        return TINYPY_FALSE;
    }
    if (next < 0) {
        next += (int64_t)input_length;
    }
    if (next < 0 || (uint64_t)next > (uint64_t)input_length) {
        TINYPY_DECREF(result);
        char message[96];
        (void)snprintf(message, sizeof(message), "position %" PRId64 " from error handler out of bounds", next);
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_INDEX, message, out_error);
        return TINYPY_FALSE;
    }
    if (decode != 0) {
        __tinypy_string_builder_append(builder, TINYPY_TEXT_BYTES(replacement), TINYPY_TEXT_BYTE_SIZE(replacement));
        *out_next = (size_t)next;
    }
    else {
        TINYPY_INCREF(replacement);
        TINYPY_DECREF(result);
        if (__tinypy_codec_append_encoded_replacement(builder, replacement, codec) == 0) {
            __tinypy_codec_update_unicode_exception(vm, state->exception, text, codec, TINYPY_FALSE, start, start + 1U);
            (void)tinypy_exception_raise(state->exception, NULL, out_error);
            TINYPY_DECREF(replacement);
            return TINYPY_FALSE;
        }
        *out_next = tinypy_internal_unicode_byte_offset(text, (size_t)next);
        TINYPY_DECREF(replacement);
        return TINYPY_TRUE;
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
    const char *name = decode != 0 ? "decode" : "encode";
    tinypy_value_t *names[2] = {vm->internal_encoding_key, vm->internal_errors_key};
    tinypy_value_t *values[2];
    tinypy_value_t *parameters;
    tinypy_value_t *result;

    tinypy_value_t *text = TINYPY_TUPLE_GET(args, 0U);
    parameters = tinypy_tuple_from_items(vm, TINYPY_TUPLE_ITERATOR_BEGIN(args) + 1U, TINYPY_TUPLE_SIZE(args) - 1U);
    tinypy_bool_t parsed = tinypy_internal_constructor_optional_arguments(vm, name, 6U, parameters, kwargs, names, 2U, UINT32_C(3), values, out_error);

    TINYPY_DECREF(parameters);
    if (parsed == 0) {
        return NULL;
    }
    if (values[0] != NULL) {
        encoding = tinypy_internal_string_from_bytes_checked(vm, TINYPY_TEXT_BYTES(values[0]), TINYPY_TEXT_BYTE_SIZE(values[0]), out_error);
        if (encoding == NULL) {
            return NULL;
        }
    }
    if (values[1] != NULL) {
        errors = tinypy_internal_string_from_bytes_checked(vm, TINYPY_TEXT_BYTES(values[1]), TINYPY_TEXT_BYTE_SIZE(values[1]), out_error);
        if (errors == NULL) {
            if (encoding != NULL) {
                TINYPY_DECREF(encoding);
            }
            return NULL;
        }
    }
    result = tinypy_internal_text_codec(vm, text, encoding, errors, decode, TINYPY_TRUE, NULL, out_error);
    if (errors != NULL) {
        TINYPY_DECREF(errors);
    }
    if (encoding != NULL) {
        TINYPY_DECREF(encoding);
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_text_codec(tinypy_vm_t *vm, tinypy_value_t *text, tinypy_value_t *encoding, tinypy_value_t *errors, tinypy_bool_t decode, tinypy_bool_t final, size_t *out_consumed, tinypy_error_t **out_error) {
    int32_t codec = 0;
    int32_t error_mode;
    tinypy_string_builder_t builder;
    const uint8_t *bytes;
    size_t size;
    size_t offset = 0U;
    tinypy_codec_error_state_t error_state = {NULL, NULL};

    if ((encoding != NULL && tinypy_internal_codecs_validate_name(vm, encoding, out_error) == 0) || (errors != NULL && tinypy_internal_codecs_validate_name(vm, errors, out_error) == 0)) {
        return NULL;
    }
    if (out_consumed != NULL) {
        *out_consumed = TINYPY_VALUE_KIND(text) == TINYPY_VALUE_UNICODE ? TINYPY_SIZED_SIZE(text) : TINYPY_TEXT_BYTE_SIZE(text);
    }
    tinypy_internal_builtin_codec_e builtin = encoding != NULL ? tinypy_internal_codecs_builtin(vm, encoding) : TINYPY_INTERNAL_BUILTIN_CODEC_ASCII;
    if (builtin <= TINYPY_INTERNAL_BUILTIN_CODEC_LATIN1) {
        codec = (int32_t)builtin;
    }
    else {
        tinypy_value_t *return_value_1 = tinypy_internal_codecs_transform_registered(vm, text, encoding, errors, decode, out_error);

        if (return_value_1 != NULL && __tinypy_string_is_text(return_value_1) == 0) {
            const tinypy_message_part_t parts[] = {
                {decode != 0 ? "decoder" : "encoder", 7U},
                TINYPY_MESSAGE_PART_LITERAL(" did not return a string/unicode object (type="),
                TINYPY_MESSAGE_PART_TYPE_NAME(return_value_1),
                TINYPY_MESSAGE_PART_LITERAL(")")
            };

            tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
            TINYPY_DECREF(return_value_1);
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
                    size_t character = tinypy_internal_unicode_character_index(text, offset);

                    (void)__tinypy_codec_raise_unicode_error(vm, text, NULL, 0, TINYPY_FALSE, character, character + 1U, out_error);
                    goto codec_error;
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
                        error_mode = __tinypy_codec_error_mode(vm, errors);
                    }
                    if (error_mode == 2) {
                        __tinypy_utf8_append(&builder, UINT32_C(0xfffd));
                    }
                    else if (error_mode == 5) {
                        if (__tinypy_codec_call_error_handler(vm, &builder, &error_state, text, encoding, errors, codec, TINYPY_TRUE, offset, error_end, &offset, out_error) == 0) {
                            goto codec_error;
                        }
                        continue;
                    }
                    else if (error_mode == 3 || error_mode == 4) {
                        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "don't know how to handle UnicodeDecodeError in error callback", out_error);
                        goto codec_error;
                    }
                    else if (error_mode == 0) {
                        (void)__tinypy_codec_raise_unicode_error(vm, text, encoding, codec, TINYPY_TRUE, offset, error_end, out_error);
                        goto codec_error;
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
                    size_t remaining = size - offset;
                    uint8_t first = bytes[offset];
                    size_t expected = first < 0xe0U ? 2U : (first < 0xf0U ? 3U : 4U);

                    if (final == 0 && first >= 0xc2U && first <= 0xf4U && remaining < expected && invalid_size == remaining) {
                        if (out_consumed != NULL) {
                            *out_consumed = offset;
                        }
                        break;
                    }

                    if (error_mode < 0) {
                        error_mode = __tinypy_codec_error_mode(vm, errors);
                    }
                    if (error_mode == 2) {
                        __tinypy_utf8_append(&builder, UINT32_C(0xfffd));
                    }
                    else if (error_mode == 5) {
                        if (__tinypy_codec_call_error_handler(vm, &builder, &error_state, text, encoding, errors, codec, TINYPY_TRUE, offset, error_end, &offset, out_error) == 0) {
                            goto codec_error;
                        }
                        continue;
                    }
                    else if (error_mode == 3 || error_mode == 4) {
                        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "don't know how to handle UnicodeDecodeError in error callback", out_error);
                        goto codec_error;
                    }
                    else if (error_mode == 0) {
                        (void)__tinypy_codec_raise_unicode_error(vm, text, encoding, codec, TINYPY_TRUE, offset, error_end, out_error);
                        goto codec_error;
                    }
                }
                offset += width != 0U ? width : tinypy_internal_utf8_invalid_span(bytes + offset, size - offset);
            }
        }
        tinypy_value_t *return_value_2 = __tinypy_string_builder_finish(&builder, INT32_C(1), out_error);
        __tinypy_codec_error_state_finalize(&error_state);
        return return_value_2;
    }
    if (TINYPY_VALUE_KIND(text) == TINYPY_VALUE_STRING) {
        while (offset < size) {
            if (bytes[offset] >= 0x80U) {
                (void)__tinypy_codec_raise_unicode_error(vm, text, NULL, 0, TINYPY_TRUE, offset, offset + 1U, out_error);
                goto codec_error;
            }
            offset += 1U;
        }
        offset = 0U;
    }
    while (offset < size) {
        uint32_t code_point;
        size_t width = tinypy_internal_utf8_decode(bytes + offset, size - offset, &code_point);

        if (width == 0U) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_UNICODE_ENCODE, "invalid internal unicode value", out_error);
            goto codec_error;
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
            size_t error_end = character + 1U;

            if (error_mode < 0) {
                error_mode = __tinypy_codec_error_mode(vm, errors);
            }
            size_t scan = offset + width;
            while ((error_mode == 0 || error_mode == 5) && scan < size) {
                uint32_t next_scalar;
                size_t next_width = tinypy_internal_utf8_decode(bytes + scan, size - scan, &next_scalar);
                if (next_width == 0U || (codec == 0 && next_scalar <= UINT32_C(0x7f)) || (codec == 2 && next_scalar <= UINT32_C(0xff))) {
                    break;
                }
                error_end += 1U;
                scan += next_width;
            }

            if (error_mode == 2) {
                __tinypy_string_builder_character(&builder, (uint8_t)'?');
            }
            else if (error_mode == 3 || error_mode == 4) {
                __tinypy_codec_append_escape(&builder, code_point, error_mode == 3 ? TINYPY_TRUE : TINYPY_FALSE);
            }
            else if (error_mode == 5) {
                if (__tinypy_codec_call_error_handler(vm, &builder, &error_state, text, encoding, errors, codec, TINYPY_FALSE, character, error_end, &offset, out_error) == 0) {
                    goto codec_error;
                }
                continue;
            }
            else if (error_mode == 0) {
                (void)__tinypy_codec_raise_unicode_error(vm, text, encoding, codec, TINYPY_FALSE, character, error_end, out_error);
                goto codec_error;
            }
        }
        offset += width;
    }
    tinypy_value_t *return_value_4 = __tinypy_string_builder_finish(&builder, INT32_C(0), out_error);
    __tinypy_codec_error_state_finalize(&error_state);
    return return_value_4;

codec_error:
    __tinypy_string_builder_discard(&builder);
    __tinypy_codec_error_state_finalize(&error_state);
    return NULL;
}

/* The operand state of PyString_Format: a tuple is consumed item by item, a
   single operand once (index -2 before, -1 after); a %(key) conversion
   makes the mapping's value the single operand. */
typedef struct tinypy_percent_arguments_t {
    tinypy_value_t *value;
    tinypy_value_t *key_value;
    int64_t length;
    int64_t index;
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
    tinypy_bool_t long_value = TINYPY_VALUE_KIND(value) == TINYPY_VALUE_LONG ? TINYPY_TRUE : TINYPY_FALSE;
    uint32_t base = conversion == (uint8_t)'b' ? 2U : (conversion == (uint8_t)'o' ? 8U : ((conversion == (uint8_t)'x' || conversion == (uint8_t)'X') ? 16U : 10U));
    tinypy_bool_t uppercase = conversion == (uint8_t)'X';
    size_t minimum_digits = precision >= 0 ? (size_t)precision : 1U;
    tinypy_bool_t suppress_zero_digit;

    /* formatint renders a plain int into a fixed 120-byte buffer. */
    if (new_format == 0 && long_value == 0 && precision > 116) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "formatted integer is too long (precision too large?)", out_error);
        return TINYPY_FALSE;
    }
    tinypy_bool_t negative = (long_value != 0 ? TINYPY_LONG_SIGN(value) < 0 : TINYPY_INTEGER_VALUE(value) < 0) ? TINYPY_TRUE : TINYPY_FALSE;
    tinypy_bool_t zero_value = (long_value != 0 ? TINYPY_LONG_DIGIT_COUNT(value) == 0U : TINYPY_INTEGER_VALUE(value) == 0) ? TINYPY_TRUE : TINYPY_FALSE;
    suppress_zero_digit = precision == 0 && zero_value != 0 && long_value == 0 && !(new_format == 0 && alternate != 0 && base == 8U);
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
        return TINYPY_TRUE;
    }
    if (long_value != 0) {
        __tinypy_percent_long_digits(builder, value, base, uppercase, minimum_digits);
        return TINYPY_TRUE;
    }
    int64_t integer = TINYPY_INTEGER_VALUE(value);
    uint64_t magnitude = integer < 0 ? (uint64_t)(-(integer + INT64_C(1))) + UINT64_C(1) : (uint64_t)integer;
    __tinypy_percent_unsigned(builder, magnitude, base, uppercase, minimum_digits);
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
    if (__tinypy_percent_float_text(vm, builder, fabs(number), uppercase != 0 ? (uint8_t)(lower - ('a' - 'A')) : lower, alternate != 0 ? TINYPY_TRUE : TINYPY_FALSE, precision, out_error) == 0) {
        return TINYPY_FALSE;
    }
    if (percentage != 0) {
        __tinypy_string_builder_character(builder, (uint8_t)'%');
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_percent_clear_conversion_error(tinypy_vm_t *vm, tinypy_error_t **out_error) {
    if (out_error != NULL && *out_error != NULL) {
        tinypy_error_release(*out_error);
        *out_error = NULL;
    }
    tinypy_vm_clear_error(vm);
}
//////////////////////////////////////////////////////////////////////////
/* PyFloat_AsDouble: a float is read as is, any other operand through
   __float__; %-formatting of a byte string reports any failure as "float
   argument required". */
static tinypy_bool_t __tinypy_percent_float_operand(tinypy_vm_t *vm, tinypy_value_t *value, tinypy_bool_t type_error, double *out_number, tinypy_error_t **out_error) {
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);
    tinypy_bool_t converted = TINYPY_FALSE;
    tinypy_error_t *float_error = NULL;

    if (kind == TINYPY_VALUE_FLOAT) {
        *out_number = TINYPY_FLOAT_OBJECT(value)->value;
        return TINYPY_TRUE;
    }
    tinypy_bool_t builtin_integer = (kind == TINYPY_VALUE_BOOL || kind == TINYPY_VALUE_INTEGER || kind == TINYPY_VALUE_LONG) && tinypy_internal_object_has_special_override_key(value, vm->internal_special_float_key) == 0 ? TINYPY_TRUE : TINYPY_FALSE;
    if (builtin_integer != 0 && kind != TINYPY_VALUE_LONG) {
        *out_number = (double)TINYPY_INTEGER_VALUE(value);
        return TINYPY_TRUE;
    }
    if (builtin_integer != 0) {
        converted = tinypy_long_as_double(value, out_number, &float_error);
    }
    else {
        tinypy_bool_t handled;
        tinypy_value_t *number = tinypy_internal_call_conversion(value, vm->internal_special_float_key, &handled, &float_error);

        if (handled == 0) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "a float is required", &float_error);
        }
        else if (number != NULL && TINYPY_VALUE_KIND(number) != TINYPY_VALUE_FLOAT) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "nb_float should return float object", &float_error);
        }
        else if (number != NULL) {
            *out_number = TINYPY_FLOAT_OBJECT(number)->value;
            converted = TINYPY_TRUE;
        }
        if (number != NULL) {
            TINYPY_DECREF(number);
        }
    }
    if (converted != 0) {
        return TINYPY_TRUE;
    }
    if (type_error != 0) {
        __tinypy_percent_clear_conversion_error(vm, &float_error);
        const tinypy_message_part_t parts[] = {
            TINYPY_MESSAGE_PART_LITERAL("float argument required, not "),
            TINYPY_MESSAGE_PART_TYPE_NAME(value)
        };

        tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
        return TINYPY_FALSE;
    }
    if (out_error != NULL) {
        *out_error = float_error;
    }
    else if (float_error != NULL) {
        tinypy_error_release(float_error);
    }
    return TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_percent_next_argument(tinypy_vm_t *vm, tinypy_percent_arguments_t *arguments, tinypy_error_t **out_error) {
    if (arguments->index >= arguments->length) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "not enough arguments for format string", out_error);
        return NULL;
    }
    arguments->index += 1;
    tinypy_value_t *value = arguments->length < 0 ? arguments->value : TINYPY_TUPLE_GET(arguments->value, (size_t)(arguments->index - 1));
    return TINYPY_RET(value);
}
//////////////////////////////////////////////////////////////////////////
/* A '*' width or precision takes the next operand, which must be an int. */
static tinypy_bool_t __tinypy_percent_star_argument(tinypy_vm_t *vm, tinypy_percent_arguments_t *arguments, int64_t *out_value, tinypy_error_t **out_error) {
    tinypy_value_t *value = __tinypy_percent_next_argument(vm, arguments, out_error);

    if (value == NULL) {
        return TINYPY_FALSE;
    }
    tinypy_bool_t result = __tinypy_percent_integer_argument(vm, value, out_value, out_error);

    TINYPY_DECREF(value);
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* A %(key) conversion looks the key up at once and makes the value the
   single operand of the rest of the conversion. */
static tinypy_bool_t __tinypy_percent_key_argument(tinypy_vm_t *vm, tinypy_value_t *format, tinypy_value_t *mapping, const uint8_t *key_bytes, size_t key_size, tinypy_percent_arguments_t *arguments, tinypy_error_t **out_error) {
    tinypy_value_t *key = TINYPY_VALUE_KIND(format) == TINYPY_VALUE_UNICODE ? tinypy_unicode_from_utf8(vm, (const char *)key_bytes, key_size) : tinypy_string_from_bytes(vm, key_bytes, key_size);
    tinypy_value_t *value = tinypy_get_item(mapping, key, out_error);

    TINYPY_DECREF(key);
    if (value == NULL) {
        return TINYPY_FALSE;
    }
    if (arguments->key_value != NULL) {
        TINYPY_DECREF(arguments->key_value);
    }
    arguments->key_value = value;
    arguments->value = value;
    arguments->length = -1;
    arguments->index = -2;
    return TINYPY_TRUE;
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
/* %d and its kin take an int or long as is and read any other number
   through __int__ and, should that fail, __long__, the way PyString_Format
   tries PyNumber_Int and PyNumber_Long; any failure is a TypeError. */
static tinypy_value_t *__tinypy_percent_integer_operand(tinypy_vm_t *vm, tinypy_value_t *value, uint8_t conversion, tinypy_error_t **out_error) {
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);

    if (kind == TINYPY_VALUE_BOOL || kind == TINYPY_VALUE_INTEGER || kind == TINYPY_VALUE_LONG) {
        return TINYPY_RET(value);
    }
    tinypy_bool_t number = kind == TINYPY_VALUE_OLD_INSTANCE || tinypy_internal_object_has_special_key(value, vm->internal_special_int_key) != 0 || tinypy_internal_object_has_special_key(value, vm->internal_special_float_key) != 0 ? TINYPY_TRUE : TINYPY_FALSE;
    if (number != 0) {
        tinypy_value_t *const names[] = {vm->internal_special_int_key, vm->internal_special_long_key};
        size_t index;

        for (index = 0U; index < sizeof(names) / sizeof(names[0]); ++index) {
            tinypy_bool_t handled;
            tinypy_value_t *integer = tinypy_internal_call_conversion(value, names[index], &handled, out_error);

            if (integer != NULL) {
                tinypy_value_type_e integer_kind = TINYPY_VALUE_KIND(integer);

                if (integer_kind == TINYPY_VALUE_BOOL || integer_kind == TINYPY_VALUE_INTEGER || integer_kind == TINYPY_VALUE_LONG) {
                    return integer;
                }
                TINYPY_DECREF(integer);
            }
            __tinypy_percent_clear_conversion_error(vm, out_error);
        }
    }
    const tinypy_message_part_t parts[] = {
        TINYPY_MESSAGE_PART_LITERAL("%"),
        {(const char *)&conversion, 1U},
        TINYPY_MESSAGE_PART_LITERAL(" format: a number is required, not "),
        TINYPY_MESSAGE_PART_TYPE_NAME(value)
    };

    tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
/* formatchar: a byte string format reads an integer as PyArg_Parse("b")
   does; a unicode format takes any code point and reports every failure to
   read one as "%c requires int or char". */
static tinypy_bool_t __tinypy_percent_append_character(tinypy_vm_t *vm, tinypy_string_builder_t *field, tinypy_value_t *value, tinypy_bool_t unicode, tinypy_error_t **out_error) {
    int64_t character;

    if (__tinypy_string_is_text(value) != 0) {
        if (__tinypy_string_character_count(value) != 1U) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "%c requires int or char", out_error);
            return TINYPY_FALSE;
        }
        __tinypy_string_builder_append(field, TINYPY_TEXT_BYTES(value), TINYPY_TEXT_BYTE_SIZE(value));
        return TINYPY_TRUE;
    }
    if (unicode != 0) {
        if (tinypy_internal_number_as_i64(value, &character, out_error) == 0) {
            __tinypy_percent_clear_conversion_error(vm, out_error);
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "%c requires int or char", out_error);
            return TINYPY_FALSE;
        }
        if (character < 0 || character > INT64_C(0x10ffff)) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "%c arg not in range(0x110000) (wide Python build)", out_error);
            return TINYPY_FALSE;
        }
        __tinypy_string_builder_code_point(field, (uint32_t)character);
        return TINYPY_TRUE;
    }
    if (__tinypy_string_integer(vm, value, &character, out_error) == 0) {
        return TINYPY_FALSE;
    }
    if (character < 0 || character > INT64_C(0xff)) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, character < 0 ? "unsigned byte integer is less than minimum" : "unsigned byte integer is greater than maximum", out_error);
        return TINYPY_FALSE;
    }
    __tinypy_string_builder_character(field, (uint8_t)character);
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
/* A byte string format reads the character as a C char, so a byte above 0x7f
   shows sign-extended; a unicode format shows '?' for a character outside
   printable ASCII and counts the index in characters from where it took
   over the formatting. */
static void __tinypy_percent_unsupported(tinypy_vm_t *vm, tinypy_value_t *format, size_t offset, tinypy_bool_t unicode, size_t unicode_begin, tinypy_error_t **out_error) {
    const uint8_t *bytes = TINYPY_TEXT_BYTES(format);
    uint32_t code_point = (uint32_t)(int32_t)(int8_t)bytes[offset];
    char character = (char)bytes[offset];
    size_t index = offset;
    char tail[64];

    if (TINYPY_VALUE_KIND(format) == TINYPY_VALUE_UNICODE) {
        (void)tinypy_internal_utf8_decode(bytes + offset, TINYPY_TEXT_BYTE_SIZE(format) - offset, &code_point);
        index = tinypy_internal_unicode_character_index(format, offset);
    }
    else if (unicode != 0) {
        code_point = bytes[offset];
        index = offset - unicode_begin;
    }
    if (unicode != 0) {
        character = code_point >= UINT32_C(31) && code_point <= UINT32_C(126) ? (char)code_point : '?';
    }
    int tail_size = snprintf(tail, sizeof(tail), "' (0x%" PRIx32 ") at index %zu", code_point, index);
    const tinypy_message_part_t parts[] = {
        TINYPY_MESSAGE_PART_LITERAL("unsupported format character '"),
        {&character, 1U},
        {tail, (size_t)tail_size}
    };

    tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_VALUE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
}
//////////////////////////////////////////////////////////////////////////
/* A unicode operand turns %-formatting of a byte string into unicode, which
   decodes the format and the output so far with the default ASCII codec; a
   byte string operand of a unicode format is decoded the same way. */
static tinypy_bool_t __tinypy_percent_promote(tinypy_vm_t *vm, tinypy_value_t *format, const tinypy_string_builder_t *output, tinypy_value_t *operand, tinypy_bool_t *in_out_unicode, tinypy_error_t **out_error) {
    if (*in_out_unicode != 0) {
        tinypy_bool_t compatible = tinypy_internal_text_ascii_compatible(vm, operand, out_error);
        return compatible;
    }
    if (TINYPY_VALUE_KIND(operand) != TINYPY_VALUE_UNICODE) {
        return TINYPY_TRUE;
    }
    if (tinypy_internal_text_ascii_compatible(vm, format, out_error) == 0 || __tinypy_string_builder_ascii_compatible(vm, output, out_error) == 0) {
        return TINYPY_FALSE;
    }
    *in_out_unicode = TINYPY_TRUE;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
/* Any operand with __getitem__ supplies %(key) values except a tuple or a
   string; a classic instance always counts as a mapping. */
static tinypy_bool_t __tinypy_percent_is_mapping(tinypy_value_t *value) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);

    if (kind == TINYPY_VALUE_DICT || kind == TINYPY_VALUE_OLD_INSTANCE) {
        return TINYPY_TRUE;
    }
    if (kind == TINYPY_VALUE_TUPLE || kind == TINYPY_VALUE_STRING || kind == TINYPY_VALUE_UNICODE) {
        return TINYPY_FALSE;
    }
    tinypy_bool_t return_value_1 = tinypy_internal_object_has_special_key(value, vm->internal_special_getitem_key);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_string_percent(tinypy_value_t *format, tinypy_value_t *argument_value, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(format);
    const uint8_t *bytes = TINYPY_TEXT_BYTES(format);
    size_t size = TINYPY_TEXT_BYTE_SIZE(format);
    size_t offset = 0U;
    tinypy_bool_t unicode = TINYPY_VALUE_KIND(format) == TINYPY_VALUE_UNICODE ? TINYPY_TRUE : TINYPY_FALSE;
    size_t unicode_begin = 0U;
    tinypy_bool_t mapping = __tinypy_percent_is_mapping(argument_value);
    tinypy_string_builder_t output;
    tinypy_string_builder_t field;
    tinypy_percent_arguments_t arguments;
    tinypy_value_t *value = NULL;

    (void)memset(&output, 0, sizeof(output));
    (void)memset(&field, 0, sizeof(field));
    output.vm = vm;
    field.vm = vm;
    arguments.value = argument_value;
    arguments.key_value = NULL;
    arguments.length = TINYPY_VALUE_KIND(argument_value) == TINYPY_VALUE_TUPLE ? (int64_t)TINYPY_TUPLE_SIZE(argument_value) : -1;
    arguments.index = TINYPY_VALUE_KIND(argument_value) == TINYPY_VALUE_TUPLE ? 0 : -2;
    while (offset < size) {
        size_t specifier_begin = offset;
        int32_t alternate = INT32_C(0);
        int32_t zero = INT32_C(0);
        int32_t left = INT32_C(0);
        int32_t space = INT32_C(0);
        int32_t plus = INT32_C(0);
        int64_t width = 0;
        int64_t precision = -1;
        size_t prefix_size = 0U;

        if (bytes[offset] != (uint8_t)'%') {
            __tinypy_string_builder_character(&output, bytes[offset++]);
            continue;
        }
        offset += 1U;
        if (offset < size && bytes[offset] == (uint8_t)'(') {
            size_t depth = 1U;
            size_t key_begin = ++offset;

            if (mapping == 0) {
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "format requires a mapping", out_error);
                goto failure;
            }
            while (offset < size && depth != 0U) {
                if (bytes[offset] == (uint8_t)'(') {
                    depth += 1U;
                }
                else if (bytes[offset] == (uint8_t)')') {
                    depth -= 1U;
                }
                offset += 1U;
            }
            if (depth != 0U) {
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "incomplete format key", out_error);
                goto failure;
            }
            if (__tinypy_percent_key_argument(vm, format, argument_value, bytes + key_begin, offset - key_begin - 1U, &arguments, out_error) == 0) {
                goto failure;
            }
        }
        for (; offset < size; ++offset) {
            if (bytes[offset] == (uint8_t)'#') {
                alternate = INT32_C(1);
            }
            else if (bytes[offset] == (uint8_t)'0') {
                zero = INT32_C(1);
            }
            else if (bytes[offset] == (uint8_t)'-') {
                left = INT32_C(1);
            }
            else if (bytes[offset] == (uint8_t)' ') {
                space = INT32_C(1);
            }
            else if (bytes[offset] == (uint8_t)'+') {
                plus = INT32_C(1);
            }
            else {
                break;
            }
        }
        if (offset < size && bytes[offset] == (uint8_t)'*') {
            if (__tinypy_percent_star_argument(vm, &arguments, &width, out_error) == 0) {
                goto failure;
            }
            if (width < 0) {
                if (width == INT64_MIN) {
                    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "width too big", out_error);
                    goto failure;
                }
                left = INT32_C(1);
                width = -width;
            }
            offset += 1U;
        }
        else {
            while (offset < size && bytes[offset] >= (uint8_t)'0' && bytes[offset] <= (uint8_t)'9') {
                if (width > (INT64_MAX - 9) / 10) {
                    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "width too big", out_error);
                    goto failure;
                }
                width = width * 10 + (int64_t)(bytes[offset++] - (uint8_t)'0');
            }
        }
        if (offset < size && bytes[offset] == (uint8_t)'.') {
            offset += 1U;
            precision = 0;
            if (offset < size && bytes[offset] == (uint8_t)'*') {
                if (__tinypy_percent_star_argument(vm, &arguments, &precision, out_error) == 0) {
                    goto failure;
                }
                if (precision < INT_MIN || precision > INT_MAX) {
                    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "Python int too large to convert to C int", out_error);
                    goto failure;
                }
                if (precision < 0) {
                    precision = 0;
                }
                offset += 1U;
            }
            else {
                while (offset < size && bytes[offset] >= (uint8_t)'0' && bytes[offset] <= (uint8_t)'9') {
                    if (precision > (INT_MAX - 9) / 10) {
                        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "prec too big", out_error);
                        goto failure;
                    }
                    precision = precision * 10 + (int64_t)(bytes[offset++] - (uint8_t)'0');
                }
            }
        }
        if (offset < size && (bytes[offset] == (uint8_t)'h' || bytes[offset] == (uint8_t)'l' || bytes[offset] == (uint8_t)'L')) {
            offset += 1U;
        }
        if (offset >= size) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "incomplete format", out_error);
            goto failure;
        }
        uint8_t conversion = bytes[offset++];
        if (conversion == (uint8_t)'%') {
            /* A literal percent takes no operand but keeps its width. */
            __tinypy_string_builder_character(&field, (uint8_t)'%');
            __tinypy_percent_append_padded(&output, &field, 0U, width, left, INT32_C(0), unicode);
            __tinypy_string_builder_discard(&field);
            continue;
        }
        value = __tinypy_percent_next_argument(vm, &arguments, out_error);
        if (value == NULL) {
            goto failure;
        }
        tinypy_bool_t was_unicode = unicode;
        if (conversion == (uint8_t)'s' || conversion == (uint8_t)'r') {
            tinypy_value_t *text;

            zero = INT32_C(0);
            if (conversion == (uint8_t)'s' && TINYPY_VALUE_KIND(value) == TINYPY_VALUE_UNICODE) {
                text = TINYPY_RET(value);
            }
            else if (conversion == (uint8_t)'s' && unicode != 0) {
                /* %s in a unicode format goes through __unicode__. */
                text = tinypy_internal_object_unicode(value, out_error);
            }
            else if (conversion == (uint8_t)'s' && tinypy_internal_object_has_special_key(value, vm->internal_special_str_key) != 0) {
                tinypy_bool_t handled;

                text = tinypy_internal_call_conversion(value, vm->internal_special_str_key, &handled, out_error);
                if (text != NULL && __tinypy_string_is_text(text) == 0) {
                    TINYPY_DECREF(text);
                    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "__str__ returned non-string", out_error);
                    text = NULL;
                }
            }
            else {
                text = conversion == (uint8_t)'r' ? tinypy_object_repr(value, out_error) : tinypy_object_str(value, out_error);
            }
            if (text == NULL) {
                goto failure;
            }
            size_t text_size = TINYPY_TEXT_BYTE_SIZE(text);
            if (precision >= 0) {
                size_t character_count = __tinypy_string_character_count(text);

                if ((uint64_t)precision < character_count) {
                    text_size = __tinypy_string_byte_offset(text, (size_t)precision);
                }
            }
            if (__tinypy_percent_promote(vm, format, &output, text, &unicode, out_error) == 0) {
                TINYPY_DECREF(text);
                goto failure;
            }
            __tinypy_string_builder_append(&field, TINYPY_TEXT_BYTES(text), text_size);
            TINYPY_DECREF(text);
        }
        else if (conversion == (uint8_t)'c') {
            zero = INT32_C(0);
            if (__tinypy_string_is_text(value) != 0 && __tinypy_string_character_count(value) == 1U && __tinypy_percent_promote(vm, format, &output, value, &unicode, out_error) == 0) {
                goto failure;
            }
            if (__tinypy_percent_append_character(vm, &field, value, unicode, out_error) == 0) {
                goto failure;
            }
        }
        else if (conversion == (uint8_t)'d' || conversion == (uint8_t)'i' || conversion == (uint8_t)'u' || conversion == (uint8_t)'o' || conversion == (uint8_t)'x' || conversion == (uint8_t)'X') {
            tinypy_value_t *integer = __tinypy_percent_integer_operand(vm, value, conversion == (uint8_t)'i' ? (uint8_t)'d' : conversion, out_error);

            if (integer == NULL) {
                goto failure;
            }
            tinypy_bool_t appended = __tinypy_percent_append_integer(vm, &field, integer, conversion, alternate, plus, space, precision, TINYPY_FALSE, &prefix_size, out_error);

            TINYPY_DECREF(integer);
            if (appended == 0) {
                goto failure;
            }
        }
        else if (conversion == (uint8_t)'e' || conversion == (uint8_t)'E' || conversion == (uint8_t)'f' || conversion == (uint8_t)'F' || conversion == (uint8_t)'g' || conversion == (uint8_t)'G') {
            double number;

            if (__tinypy_percent_float_operand(vm, value, unicode == 0 ? TINYPY_TRUE : TINYPY_FALSE, &number, out_error) == 0) {
                goto failure;
            }
            if (__tinypy_percent_append_double(vm, &field, number, conversion, alternate, plus, space, precision, &prefix_size, out_error) == 0) {
                goto failure;
            }
        }
        else {
            __tinypy_percent_unsupported(vm, format, offset - 1U, unicode, unicode_begin, out_error);
            goto failure;
        }
        if (unicode != was_unicode) {
            unicode_begin = specifier_begin;
        }
        TINYPY_DECREF(value);
        value = NULL;
        if (left != 0) {
            zero = INT32_C(0);
        }
        __tinypy_percent_append_padded(&output, &field, prefix_size, width, left, zero, unicode);
        __tinypy_string_builder_discard(&field);
    }
    if (arguments.key_value != NULL) {
        TINYPY_DECREF(arguments.key_value);
    }
    /* A mapping operand is never required to be consumed positionally. */
    if (mapping == 0 && arguments.index < arguments.length) {
        __tinypy_string_builder_discard(&output);
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "not all arguments converted during string formatting", out_error);
        return NULL;
    }
    tinypy_value_t *return_value_1 = __tinypy_string_builder_finish(&output, unicode, out_error);
    return return_value_1;
failure:
    if (value != NULL) {
        TINYPY_DECREF(value);
    }
    if (arguments.key_value != NULL) {
        TINYPY_DECREF(arguments.key_value);
    }
    __tinypy_string_builder_discard(&field);
    __tinypy_string_builder_discard(&output);
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_string_formatter_decimal(tinypy_vm_t *vm, tinypy_value_t *text, size_t begin, size_t end, tinypy_error_t **out_error) {
    int64_t integer;
    tinypy_bool_t numeric;

    if (__tinypy_string_format_index(vm, TINYPY_TEXT_BYTES(text) + begin, end - begin, TINYPY_VALUE_KIND(text) == TINYPY_VALUE_UNICODE, &integer, &numeric, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *result = tinypy_long_from_i64(vm, integer);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_string_formatter_is_decimal(const uint8_t *bytes, size_t begin, size_t end, tinypy_bool_t unicode) {
    size_t offset;

    if (begin == end) {
        return TINYPY_FALSE;
    }
    for (offset = begin; offset < end;) {
        if (unicode != 0) {
            uint32_t code_point;
            uint8_t digit;
            size_t width = tinypy_internal_utf8_decode(bytes + offset, end - offset, &code_point);

            if (tinypy_internal_unicode_decimal_digit(code_point, &digit) == 0) {
                return TINYPY_FALSE;
            }
            offset += width;
        }
        else if (bytes[offset] < (uint8_t)'0' || bytes[offset] > (uint8_t)'9') {
            return TINYPY_FALSE;
        }
        else {
            offset += 1U;
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
        tinypy_value_t *literal = TINYPY_VALUE_KIND(text) == TINYPY_VALUE_UNICODE
            ? tinypy_unicode_from_utf8(vm, (const char *)bytes + literal_begin, size - literal_begin)
            : tinypy_string_from_bytes(vm, bytes + literal_begin, size - literal_begin);
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
        iterator->index = brace + 1U;
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "Single '}' encountered in format string", out_error);
        return NULL;
    }
    size_t end = brace + 1U;
    size_t depth = 1U;

    if (end == size) {
        iterator->index = size;
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "Single '{' encountered in format string", out_error);
        return NULL;
    }
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
        iterator->index = size;
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "unmatched '{' in format", out_error);
        return NULL;
    }
    size_t field_end;
    size_t spec_begin;
    int32_t conversion_character;
    tinypy_value_t *conversion = NULL;

    iterator->index = end + 1U;
    if (__tinypy_string_format_parse_field(vm, bytes, brace + 1U, end, TINYPY_VALUE_KIND(text) == TINYPY_VALUE_UNICODE, &field_end, &spec_begin, &conversion_character, out_error) == 0) {
        return NULL;
    }
    if (conversion_character != 0) {
        size_t conversion_begin = field_end + 1U;
        size_t conversion_end = conversion_begin + (TINYPY_VALUE_KIND(text) == TINYPY_VALUE_UNICODE ? __tinypy_string_utf8_width(bytes[conversion_begin]) : 1U);

        conversion = __tinypy_string_from_span(vm, text, conversion_begin, conversion_end);
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
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 0U, 0U, TINYPY_ARITY_STYLE_PARSED, out_error) == TINYPY_FALSE) {
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
            iterator->index = offset;
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "Missing ']' in format string", out_error);
            return NULL;
        }
        end = offset++;
        if (begin == end) {
            goto empty_attribute;
        }
        if (__tinypy_string_formatter_is_decimal(bytes, begin, end, TINYPY_VALUE_KIND(text) == TINYPY_VALUE_UNICODE) != 0) {
            component_value = __tinypy_string_formatter_decimal(vm, text, begin, end, out_error);
            if (component_value == NULL) {
                iterator->index = offset;
                return NULL;
            }
        }
        else {
            component_value = __tinypy_string_from_span(vm, text, begin, end);
        }
    }
    else {
        iterator->index = offset + 1U;
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "Only '.' or '[' may follow ']' in format field specifier", out_error);
        return NULL;
    }
    tinypy_value_t *component = __tinypy_string_formatter_path_component(vm, attribute, component_value);

    TINYPY_DECREF(component_value);
    iterator->index = offset;
    return component;

empty_attribute:
    iterator->index = offset;
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "Empty attribute in format string", out_error);
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
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 0U, 0U, TINYPY_ARITY_STYLE_PARSED, out_error) == TINYPY_FALSE) {
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
    if (__tinypy_string_formatter_is_decimal(bytes, 0U, offset, TINYPY_VALUE_KIND(text) == TINYPY_VALUE_UNICODE) != 0) {
        head = __tinypy_string_formatter_decimal(vm, text, 0U, offset, out_error);
        if (head == NULL) {
            return NULL;
        }
    }
    else {
        head = TINYPY_VALUE_KIND(text) == TINYPY_VALUE_UNICODE
            ? tinypy_unicode_from_utf8(vm, (const char *)bytes, offset)
            : tinypy_string_from_bytes(vm, bytes, offset);
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
void tinypy_internal_initialize_string_types(tinypy_vm_t *vm) {
    tinypy_type_t *types[2] = {&vm->types[TINYPY_VALUE_STRING], &vm->types[TINYPY_VALUE_UNICODE]};
    size_t index;

    for (index = 0U; index < 2U; ++index) {
        tinypy_internal_type_add_method((types[index]), vm->internal_formatter_parser_key, __tinypy_string_formatter_parser_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
        tinypy_internal_type_add_method((types[index]), vm->internal_formatter_field_name_split_key, __tinypy_string_formatter_field_name_split_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
        tinypy_internal_type_add_method((types[index]), vm->internal_format_key, __tinypy_string_format_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
        tinypy_internal_type_add_method((types[index]), vm->internal_center_key, __tinypy_string_align_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
        tinypy_internal_type_add_method((types[index]), vm->internal_ljust_key, __tinypy_string_align_method, (void *)(intptr_t)-1, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
        tinypy_internal_type_add_method((types[index]), vm->internal_rjust_key, __tinypy_string_align_method, (void *)(intptr_t)1, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
        tinypy_internal_type_add_method((types[index]), vm->internal_join_key, __tinypy_string_join_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
        tinypy_internal_type_add_method((types[index]), vm->internal_find_key, __tinypy_string_search_method, (void *)(intptr_t)0, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
        tinypy_internal_type_add_method((types[index]), vm->internal_rfind_key, __tinypy_string_search_method, (void *)(intptr_t)1, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
        tinypy_internal_type_add_method((types[index]), vm->internal_index_key, __tinypy_string_search_method, (void *)(intptr_t)2, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
        tinypy_internal_type_add_method((types[index]), vm->internal_rindex_key, __tinypy_string_search_method, (void *)(intptr_t)3, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
        tinypy_internal_type_add_method((types[index]), vm->internal_startswith_key, __tinypy_string_prefix_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
        tinypy_internal_type_add_method((types[index]), vm->internal_endswith_key, __tinypy_string_prefix_method, (void *)(intptr_t)1, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
        tinypy_internal_type_add_method((types[index]), vm->internal_count_key, __tinypy_string_count_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
        tinypy_internal_type_add_method((types[index]), vm->internal_strip_key, __tinypy_string_strip_method, (void *)(intptr_t)0, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
        tinypy_internal_type_add_method((types[index]), vm->internal_lstrip_key, __tinypy_string_strip_method, (void *)(intptr_t)-1, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
        tinypy_internal_type_add_method((types[index]), vm->internal_rstrip_key, __tinypy_string_strip_method, (void *)(intptr_t)1, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
        tinypy_internal_type_add_method((types[index]), vm->internal_replace_key, __tinypy_string_replace_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
        tinypy_internal_type_add_method((types[index]), vm->internal_split_key, __tinypy_string_split_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
        tinypy_internal_type_add_method((types[index]), vm->internal_rsplit_key, __tinypy_string_split_method, (void *)(intptr_t)1, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
        tinypy_internal_type_add_method((types[index]), vm->internal_lower_key, __tinypy_string_case_method, (void *)(intptr_t)0, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
        tinypy_internal_type_add_method((types[index]), vm->internal_upper_key, __tinypy_string_case_method, (void *)(intptr_t)1, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
        tinypy_internal_type_add_method((types[index]), vm->internal_swapcase_key, __tinypy_string_case_method, (void *)(intptr_t)2, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
        tinypy_internal_type_add_method((types[index]), vm->internal_capitalize_key, __tinypy_string_case_method, (void *)(intptr_t)3, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
        tinypy_internal_type_add_method((types[index]), vm->internal_title_key, __tinypy_string_case_method, (void *)(intptr_t)4, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
        tinypy_internal_type_add_method((types[index]), vm->internal_isalpha_key, __tinypy_string_predicate_method, (void *)(intptr_t)0, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
        tinypy_internal_type_add_method((types[index]), vm->internal_isdigit_key, __tinypy_string_predicate_method, (void *)(intptr_t)1, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
        tinypy_internal_type_add_method((types[index]), vm->internal_isalnum_key, __tinypy_string_predicate_method, (void *)(intptr_t)2, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
        tinypy_internal_type_add_method((types[index]), vm->internal_isspace_key, __tinypy_string_predicate_method, (void *)(intptr_t)3, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
        tinypy_internal_type_add_method((types[index]), vm->internal_islower_key, __tinypy_string_predicate_method, (void *)(intptr_t)4, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
        tinypy_internal_type_add_method((types[index]), vm->internal_isupper_key, __tinypy_string_predicate_method, (void *)(intptr_t)5, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
        tinypy_internal_type_add_method((types[index]), vm->internal_istitle_key, __tinypy_string_predicate_method, (void *)(intptr_t)6, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
        tinypy_internal_type_add_method((types[index]), vm->internal_zfill_key, __tinypy_string_zfill_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
        tinypy_internal_type_add_method((types[index]), vm->internal_splitlines_key, __tinypy_string_splitlines_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
        tinypy_internal_type_add_method((types[index]), vm->internal_expandtabs_key, __tinypy_string_expandtabs_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
        tinypy_internal_type_add_method((types[index]), vm->internal_partition_key, __tinypy_string_partition_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
        tinypy_internal_type_add_method((types[index]), vm->internal_rpartition_key, __tinypy_string_partition_method, (void *)(intptr_t)1, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
        tinypy_internal_type_add_method((types[index]), vm->internal_encode_key, __tinypy_string_codec_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
        tinypy_internal_type_add_method((types[index]), vm->internal_decode_key, __tinypy_string_codec_method, (void *)(intptr_t)1, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
        if (types[index] == &vm->types[TINYPY_VALUE_STRING]) {
            tinypy_internal_type_add_method((types[index]), vm->internal_translate_key, __tinypy_string_translate_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
        }
        else {
            tinypy_internal_type_add_method((types[index]), vm->internal_translate_key, __tinypy_unicode_translate_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
            tinypy_internal_type_add_method((types[index]), vm->internal_isdecimal_key, __tinypy_string_predicate_method, (void *)(intptr_t)7, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
            tinypy_internal_type_add_method((types[index]), vm->internal_isnumeric_key, __tinypy_string_predicate_method, (void *)(intptr_t)8, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
        }
    }
}
