#include "internal.h"

#include <math.h>
#include <string.h>

typedef struct tinypy_string_builder_t {
    tinypy_vm_t *vm;
    unsigned char *bytes;
    size_t size;
    size_t capacity;
} tinypy_string_builder_t;

//////////////////////////////////////////////////////////////////////////
static void __tinypy_string_builder_reserve(tinypy_string_builder_t *builder, size_t extra) {
    size_t required;
    size_t capacity;

    assert(builder->size <= SIZE_MAX - extra);
    required = builder->size + extra;
    if (required <= builder->capacity) {
        return;
    }
    capacity = builder->capacity != 0U ? builder->capacity : 64U;
    while (capacity < required) {
        assert(capacity <= SIZE_MAX / 2U);
        capacity *= 2U;
    }
    if (builder->bytes == NULL) {
        builder->bytes = (unsigned char *)tinypy_internal_vm_allocate(builder->vm, capacity, (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    }
    else {
        builder->bytes = (unsigned char *)tinypy_internal_vm_reallocate(builder->vm, builder->bytes, builder->capacity, capacity, (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    }
    builder->capacity = capacity;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_string_builder_append(tinypy_string_builder_t *builder, const void *bytes, size_t size) {
    if (size == 0U) {
        return;
    }
    __tinypy_string_builder_reserve(builder, size);
    (void)memcpy(builder->bytes + builder->size, bytes, size);
    builder->size += size;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_string_builder_character(tinypy_string_builder_t *builder, unsigned char character) {
    __tinypy_string_builder_append(builder, &character, 1U);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_string_builder_finish(tinypy_string_builder_t *builder, int32_t unicode) {
    tinypy_value_t *result = unicode != 0 ? tinypy_unicode_from_utf8(builder->vm, (const char *)builder->bytes, builder->size) : tinypy_string_from_bytes(builder->vm, builder->bytes, builder->size);

    if (builder->bytes != NULL) {
        tinypy_internal_vm_deallocate(builder->vm, builder->bytes, builder->capacity, (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    }
    builder->bytes = NULL;
    builder->size = 0U;
    builder->capacity = 0U;
    return result;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_string_builder_discard(tinypy_string_builder_t *builder) {
    if (builder->bytes != NULL) {
        tinypy_internal_vm_deallocate(builder->vm, builder->bytes, builder->capacity, (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    }
    builder->bytes = NULL;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_string_method_arguments(tinypy_vm_t *vm, tinypy_value_t *args, tinypy_value_t *kwargs, size_t minimum, size_t maximum, int32_t keywords, tinypy_error_t **out_error) {
    size_t count = TINYPY_TUPLE_SIZE(args);

    if (count < minimum || count > maximum) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "string method received the wrong number of arguments", out_error);
        return INT32_C(0);
    }
    if (keywords == 0 && kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "string method does not accept keyword arguments", out_error);
        return INT32_C(0);
    }
    return INT32_C(1);
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_string_integer(tinypy_vm_t *vm, tinypy_value_t *value, int64_t *out_value, tinypy_error_t **out_error) {
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);

    if (kind == TINYPY_VALUE_BOOL || kind == TINYPY_VALUE_INTEGER) {
        *out_value = TINYPY_INTEGER_VALUE(value);
        return INT32_C(1);
    }
    if (kind == TINYPY_VALUE_LONG && TINYPY_LONG_DIGIT_COUNT(value) <= 5U) {
        uint64_t magnitude = 0U;
        size_t index = TINYPY_LONG_DIGIT_COUNT(value);

        while (index != 0U) {
            index -= 1U;
            if (magnitude > (UINT64_MAX >> 15U)) {
                break;
            }
            magnitude = (magnitude << 15U) | TINYPY_LONG_OBJECT(value)->digits[index];
        }
        if (TINYPY_LONG_SIGN(value) >= 0 && magnitude <= (uint64_t)INT64_MAX) {
            *out_value = (int64_t)magnitude;
            return INT32_C(1);
        }
        if (TINYPY_LONG_SIGN(value) < 0 && magnitude <= (uint64_t)INT64_MAX + UINT64_C(1)) {
            *out_value = magnitude == (uint64_t)INT64_MAX + UINT64_C(1) ? INT64_MIN : -(int64_t)magnitude;
            return INT32_C(1);
        }
    }
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "integer argument required", out_error);
    return INT32_C(0);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_string_format_hex(tinypy_vm_t *vm, tinypy_value_t *value, const unsigned char *spec, size_t spec_size, tinypy_error_t **out_error) {
    int64_t number;
    uint64_t magnitude;
    unsigned char reversed[32];
    unsigned char output[64];
    size_t digit_count = 0U;
    size_t width = 0U;
    size_t spec_index = 0U;
    size_t output_size = 0U;
    int32_t uppercase;

    if (__tinypy_string_integer(vm, value, &number, out_error) == 0) {
        return NULL;
    }
    uppercase = spec[spec_size - 1U] == (unsigned char)'X';
    while (spec_index + 1U < spec_size && spec[spec_index] >= (unsigned char)'0' && spec[spec_index] <= (unsigned char)'9') {
        width = width * 10U + (size_t)(spec[spec_index] - (unsigned char)'0');
        ++spec_index;
    }
    magnitude = number < 0 ? (uint64_t)(-(number + INT64_C(1))) + UINT64_C(1) : (uint64_t)number;
    do {
        unsigned char digit = (unsigned char)(magnitude & UINT64_C(15));

        reversed[digit_count++] = digit < 10U ? (unsigned char)('0' + digit) : (unsigned char)((uppercase != 0 ? 'A' : 'a') + digit - 10U);
        magnitude >>= 4U;
    } while (magnitude != 0U);
    if (number < 0) {
        output[output_size++] = (unsigned char)'-';
    }
    while (output_size + digit_count < width) {
        output[output_size++] = spec_size > 1U && spec[0] == (unsigned char)'0' ? (unsigned char)'0' : (unsigned char)' ';
    }
    while (digit_count != 0U) {
        output[output_size++] = reversed[--digit_count];
    }
    return tinypy_string_from_bytes(vm, output, output_size);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_string_format_value(tinypy_vm_t *vm, tinypy_value_t *value, int32_t conversion, const unsigned char *spec, size_t spec_size, tinypy_error_t **out_error) {
    if (spec_size != 0U && (spec[spec_size - 1U] == (unsigned char)'x' || spec[spec_size - 1U] == (unsigned char)'X')) {
        return __tinypy_string_format_hex(vm, value, spec, spec_size, out_error);
    }
    if (conversion == 'r') {
        return tinypy_object_repr(value, out_error);
    }
    return tinypy_object_str(value, out_error);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_string_format_lookup(tinypy_vm_t *vm, tinypy_value_t *args, tinypy_value_t *kwargs, const unsigned char *field, size_t field_size, size_t *auto_index, tinypy_error_t **out_error) {
    tinypy_value_t *value = NULL;
    size_t head_size = 0U;
    size_t path_offset;

    while (head_size < field_size && field[head_size] != (unsigned char)'.' && field[head_size] != (unsigned char)'[') {
        head_size += 1U;
    }
    if (head_size == 0U) {
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
            if (field[index] < (unsigned char)'0' || field[index] > (unsigned char)'9') {
                numeric = INT32_C(0);
            }
        }
        if (numeric != 0) {
            size_t position = 0U;

            for (index = 0U; index < head_size; ++index) {
                position = position * 10U + (size_t)(field[index] - (unsigned char)'0');
            }
            if (position + 1U >= TINYPY_TUPLE_SIZE(args)) {
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

        if (field[path_offset] == (unsigned char)'.') {
            size_t name_begin = ++path_offset;

            while (path_offset < field_size && field[path_offset] != (unsigned char)'.' && field[path_offset] != (unsigned char)'[') {
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

            while (path_offset < field_size && field[path_offset] != (unsigned char)']') {
                path_offset += 1U;
            }
            if (path_offset == field_size) {
                TINYPY_DECREF(value);
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "missing ']' in format field", out_error);
                return NULL;
            }
            key_end = path_offset++;
            for (index = key_begin; index < key_end; ++index) {
                if (field[index] < (unsigned char)'0' || field[index] > (unsigned char)'9') {
                    numeric = INT32_C(0);
                }
            }
            if (numeric != 0 && key_begin != key_end) {
                int64_t integer = 0;

                for (index = key_begin; index < key_end; ++index) {
                    integer = integer * 10 + (int64_t)(field[index] - (unsigned char)'0');
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
static tinypy_value_t *__tinypy_string_format_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *format = TINYPY_TUPLE_GET(args, 0U);
    const unsigned char *bytes = TINYPY_TEXT_BYTES(format);
    size_t size = TINYPY_TEXT_BYTE_SIZE(format);
    size_t offset = 0U;
    size_t automatic_index = 0U;
    tinypy_string_builder_t builder;

    (void)user_data;
    if (__tinypy_string_method_arguments(vm, args, kwargs, 1U, SIZE_MAX, INT32_C(1), out_error) == 0) {
        return NULL;
    }
    (void)memset(&builder, 0, sizeof(builder));
    builder.vm = vm;
    while (offset < size) {
        if (bytes[offset] == (unsigned char)'{' && offset + 1U < size && bytes[offset + 1U] == (unsigned char)'{') {
            __tinypy_string_builder_character(&builder, (unsigned char)'{');
            offset += 2U;
            continue;
        }
        if (bytes[offset] == (unsigned char)'}' && offset + 1U < size && bytes[offset + 1U] == (unsigned char)'}') {
            __tinypy_string_builder_character(&builder, (unsigned char)'}');
            offset += 2U;
            continue;
        }
        if (bytes[offset] == (unsigned char)'{') {
            size_t end = offset + 1U;
            size_t field_end;
            size_t spec_begin;
            int32_t conversion = 0;
            tinypy_value_t *value;
            tinypy_value_t *text;

            while (end < size && bytes[end] != (unsigned char)'}') {
                end += 1U;
            }
            if (end == size) {
                __tinypy_string_builder_discard(&builder);
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "unmatched '{' in format string", out_error);
                return NULL;
            }
            field_end = offset + 1U;
            while (field_end < end && bytes[field_end] != (unsigned char)'!' && bytes[field_end] != (unsigned char)':') {
                field_end += 1U;
            }
            spec_begin = end;
            if (field_end < end && bytes[field_end] == (unsigned char)'!') {
                if (field_end + 1U >= end || (bytes[field_end + 1U] != (unsigned char)'r' && bytes[field_end + 1U] != (unsigned char)'s')) {
                    __tinypy_string_builder_discard(&builder);
                    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "invalid format conversion", out_error);
                    return NULL;
                }
                conversion = bytes[field_end + 1U];
                spec_begin = field_end + 2U;
                if (spec_begin < end && bytes[spec_begin] == (unsigned char)':') {
                    spec_begin += 1U;
                }
                else {
                    spec_begin = end;
                }
            }
            else if (field_end < end && bytes[field_end] == (unsigned char)':') {
                spec_begin = field_end + 1U;
            }
            value = __tinypy_string_format_lookup(vm, args, kwargs, bytes + offset + 1U, field_end - offset - 1U, &automatic_index, out_error);
            if (value == NULL) {
                __tinypy_string_builder_discard(&builder);
                return NULL;
            }
            text = __tinypy_string_format_value(vm, value, conversion, bytes + spec_begin, end - spec_begin, out_error);
            TINYPY_DECREF(value);
            if (text == NULL) {
                __tinypy_string_builder_discard(&builder);
                return NULL;
            }
            const unsigned char *bytes_2 = TINYPY_TEXT_BYTES(text);
            size_t byte_size = TINYPY_TEXT_BYTE_SIZE(text);
            __tinypy_string_builder_append(&builder, bytes_2, byte_size);
            TINYPY_DECREF(text);
            offset = end + 1U;
            continue;
        }
        if (bytes[offset] == (unsigned char)'}') {
            __tinypy_string_builder_discard(&builder);
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "single '}' in format string", out_error);
            return NULL;
        }
        __tinypy_string_builder_character(&builder, bytes[offset++]);
    }
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(format);
    return __tinypy_string_builder_finish(&builder, kind == TINYPY_VALUE_UNICODE);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_string_align_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *text = TINYPY_TUPLE_GET(args, 0U);
    int64_t width;
    unsigned char fill = (unsigned char)' ';
    size_t size = TINYPY_TEXT_BYTE_SIZE(text);
    size_t padding;
    size_t left;
    size_t right;
    tinypy_string_builder_t builder;
    intptr_t mode = (intptr_t)user_data;

    int condition = __tinypy_string_method_arguments(vm, args, kwargs, 2U, 3U, INT32_C(0), out_error) == 0;
    if (condition == 0) {
        tinypy_value_t *item = TINYPY_TUPLE_GET(args, 1U);
        condition = __tinypy_string_integer(vm, item, &width, out_error) == 0;
    }
    if (condition) {
        return NULL;
    }
    if (TINYPY_TUPLE_SIZE(args) == 3U) {
        tinypy_value_t *fill_value = TINYPY_TUPLE_GET(args, 2U);

        if ((TINYPY_VALUE_KIND(fill_value) != TINYPY_VALUE_STRING && TINYPY_VALUE_KIND(fill_value) != TINYPY_VALUE_UNICODE) || TINYPY_TEXT_BYTE_SIZE(fill_value) != 1U) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "fill character must be exactly one character", out_error);
            return NULL;
        }
        fill = TINYPY_TEXT_BYTES(fill_value)[0];
    }
    if (width <= 0 || (uint64_t)width <= size) {
        tinypy_value_t *selected_value;
        if (TINYPY_VALUE_KIND(text) == TINYPY_VALUE_UNICODE) {
            const unsigned char *bytes_2 = TINYPY_TEXT_BYTES(text);
            selected_value = tinypy_unicode_from_utf8(vm, (const char *)bytes_2, size);
        }
        else {
            const unsigned char *bytes_2 = TINYPY_TEXT_BYTES(text);
            selected_value = tinypy_string_from_bytes(vm, bytes_2, size);
        }
        return selected_value;
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
    (void)memset(&builder, 0, sizeof(builder));
    builder.vm = vm;
    while (left-- != 0U) {
        __tinypy_string_builder_character(&builder, fill);
    }
    const unsigned char *bytes = TINYPY_TEXT_BYTES(text);
    __tinypy_string_builder_append(&builder, bytes, size);
    while (right-- != 0U) {
        __tinypy_string_builder_character(&builder, fill);
    }
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(text);
    return __tinypy_string_builder_finish(&builder, kind == TINYPY_VALUE_UNICODE);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_string_join_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *separator = TINYPY_TUPLE_GET(args, 0U);
    tinypy_error_t *iteration_error = NULL;
    tinypy_string_builder_t builder;
    size_t count = 0U;
    int32_t unicode = TINYPY_VALUE_KIND(separator) == TINYPY_VALUE_UNICODE;

    (void)user_data;
    if (__tinypy_string_method_arguments(vm, args, kwargs, 2U, 2U, INT32_C(0), out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *item_2 = TINYPY_TUPLE_GET(args, 1U);
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
        if (count != 0U) {
            const unsigned char *bytes_2 = TINYPY_TEXT_BYTES(separator);
            size_t byte_size_2 = TINYPY_TEXT_BYTE_SIZE(separator);
            __tinypy_string_builder_append(&builder, bytes_2, byte_size_2);
        }
        const unsigned char *bytes = TINYPY_TEXT_BYTES(item);
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
    return __tinypy_string_builder_finish(&builder, unicode);
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_string_is_text(const tinypy_value_t *value) {
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);

    return kind == TINYPY_VALUE_STRING || kind == TINYPY_VALUE_UNICODE;
}
//////////////////////////////////////////////////////////////////////////
static size_t __tinypy_string_character_count(const tinypy_value_t *value) {
    return TINYPY_VALUE_KIND(value) == TINYPY_VALUE_UNICODE ? TINYPY_SIZED_SIZE(value) : TINYPY_TEXT_BYTE_SIZE(value);
}
//////////////////////////////////////////////////////////////////////////
static size_t __tinypy_string_utf8_width(unsigned char first) {
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
static size_t __tinypy_string_byte_offset(const tinypy_value_t *value, size_t character_index) {
    const unsigned char *bytes;
    size_t byte_size;
    size_t offset = 0U;
    size_t index = 0U;

    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_STRING) {
        return character_index;
    }
    bytes = TINYPY_TEXT_BYTES(value);
    byte_size = TINYPY_TEXT_BYTE_SIZE(value);
    while (offset < byte_size && index < character_index) {
        offset += __tinypy_string_utf8_width(bytes[offset]);
        index += 1U;
    }
    return offset;
}
//////////////////////////////////////////////////////////////////////////
static size_t __tinypy_string_character_index(const tinypy_value_t *value, size_t byte_offset) {
    const unsigned char *bytes;
    size_t offset = 0U;
    size_t index = 0U;

    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_STRING) {
        return byte_offset;
    }
    bytes = TINYPY_TEXT_BYTES(value);
    while (offset < byte_offset) {
        offset += __tinypy_string_utf8_width(bytes[offset]);
        index += 1U;
    }
    return index;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_string_from_span(tinypy_vm_t *vm, const tinypy_value_t *source, size_t begin, size_t end) {
    assert(begin <= end);
    assert(end <= TINYPY_TEXT_BYTE_SIZE(source));
    if (TINYPY_VALUE_KIND(source) == TINYPY_VALUE_UNICODE) {
        const unsigned char *bytes_2 = TINYPY_TEXT_BYTES(source);
        return tinypy_unicode_from_utf8(vm, (const char *)bytes_2 + begin, end - begin);
    }
    const unsigned char *bytes = TINYPY_TEXT_BYTES(source);
    return tinypy_string_from_bytes(vm, bytes + begin, end - begin);
}
//////////////////////////////////////////////////////////////////////////
static int64_t __tinypy_string_normalized_bound(tinypy_vm_t *vm, tinypy_value_t *value, size_t length, int64_t fallback, tinypy_error_t **out_error) {
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
    if ((uint64_t)bound > (uint64_t)length) {
        return (int64_t)length;
    }
    return bound;
}
//////////////////////////////////////////////////////////////////////////
static int64_t __tinypy_string_optional_bound(tinypy_vm_t *vm, tinypy_value_t *args, size_t index, size_t length, int64_t fallback, tinypy_error_t **out_error) {
    size_t argument_count = TINYPY_TUPLE_SIZE(args);
    tinypy_value_t *value = argument_count > index ? TINYPY_TUPLE_GET(args, index) : NULL;
    return __tinypy_string_normalized_bound(vm, value, length, fallback, out_error);
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_string_require_text(tinypy_vm_t *vm, tinypy_value_t *value, const char *message, tinypy_error_t **out_error) {
    if (__tinypy_string_is_text(value) != 0) {
        return INT32_C(1);
    }
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, message, out_error);
    return INT32_C(0);
}
//////////////////////////////////////////////////////////////////////////
static ptrdiff_t __tinypy_string_find_bytes(const unsigned char *haystack, size_t haystack_size, const unsigned char *needle, size_t needle_size, int32_t reverse) {
    size_t offset;

    if (needle_size == 0U) {
        return reverse != 0 ? (ptrdiff_t)haystack_size : 0;
    }
    if (needle_size > haystack_size) {
        return -1;
    }
    if (reverse == 0) {
        for (offset = 0U; offset <= haystack_size - needle_size; ++offset) {
            if (memcmp(haystack + offset, needle, needle_size) == 0) {
                return (ptrdiff_t)offset;
            }
        }
    }
    else {
        offset = haystack_size - needle_size + 1U;
        while (offset != 0U) {
            offset -= 1U;
            if (memcmp(haystack + offset, needle, needle_size) == 0) {
                return (ptrdiff_t)offset;
            }
        }
    }
    return -1;
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
    length = __tinypy_string_character_count(text);
    start = __tinypy_string_optional_bound(vm, args, 2U, length, 0, out_error);
    if (start == INT64_MIN && out_error != NULL && *out_error != NULL) {
        return NULL;
    }
    end = __tinypy_string_optional_bound(vm, args, 3U, length, (int64_t)length, out_error);
    if (end == INT64_MIN && out_error != NULL && *out_error != NULL) {
        return NULL;
    }
    if (start > end) {
        found = -1;
    }
    else {
        byte_start = __tinypy_string_byte_offset(text, (size_t)start);
        byte_end = __tinypy_string_byte_offset(text, (size_t)end);
        const unsigned char *bytes = TINYPY_TEXT_BYTES(text);
        const unsigned char *bytes_2 = TINYPY_TEXT_BYTES(needle);
        size_t byte_size = TINYPY_TEXT_BYTE_SIZE(needle);
        found = __tinypy_string_find_bytes(bytes + byte_start, byte_end - byte_start, bytes_2, byte_size, (int32_t)(mode & 1));
        if (found >= 0) {
            found = (ptrdiff_t)__tinypy_string_character_index(text, byte_start + (size_t)found);
        }
    }
    if (found < 0 && mode >= 2) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "substring not found", out_error);
        return NULL;
    }
    return tinypy_integer_from_i64(vm, (int64_t)found);
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_string_matches_at(const tinypy_value_t *text, size_t begin, size_t end, const tinypy_value_t *candidate, int32_t suffix) {
    const unsigned char *bytes = TINYPY_TEXT_BYTES(text);
    const unsigned char *candidate_bytes = TINYPY_TEXT_BYTES(candidate);
    size_t candidate_size = TINYPY_TEXT_BYTE_SIZE(candidate);

    if (candidate_size > end - begin) {
        return INT32_C(0);
    }
    if (suffix != 0) {
        begin = end - candidate_size;
    }
    return memcmp(bytes + begin, candidate_bytes, candidate_size) == 0 ? INT32_C(1) : INT32_C(0);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_string_prefix_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    size_t length;
    int64_t start;
    int64_t end;
    size_t begin;
    size_t finish;
    int32_t suffix = user_data != NULL;

    if (__tinypy_string_method_arguments(vm, args, kwargs, 2U, 4U, INT32_C(0), out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *text = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *candidate = TINYPY_TUPLE_GET(args, 1U);
    length = __tinypy_string_character_count(text);
    start = __tinypy_string_optional_bound(vm, args, 2U, length, 0, out_error);
    if (start == INT64_MIN && out_error != NULL && *out_error != NULL) {
        return NULL;
    }
    end = __tinypy_string_optional_bound(vm, args, 3U, length, (int64_t)length, out_error);
    if (end == INT64_MIN && out_error != NULL && *out_error != NULL) {
        return NULL;
    }
    if (start > end) {
        return tinypy_bool_from_i32(vm, INT32_C(0));
    }
    begin = __tinypy_string_byte_offset(text, (size_t)start);
    finish = __tinypy_string_byte_offset(text, (size_t)end);
    if (TINYPY_VALUE_KIND(candidate) == TINYPY_VALUE_TUPLE) {
        tinypy_value_t *const *iterator = TINYPY_TUPLE_ITERATOR_BEGIN(candidate);
        tinypy_value_t *const *iterator_end = TINYPY_TUPLE_ITERATOR_END(candidate);

        for (; iterator != iterator_end; ++iterator) {
            tinypy_value_t *item = *iterator;

            if (__tinypy_string_require_text(vm, item, "prefix tuple contains a non-string", out_error) == 0) {
                return NULL;
            }
            if (__tinypy_string_matches_at(text, begin, finish, item, suffix) != 0) {
                return tinypy_bool_from_i32(vm, INT32_C(1));
            }
        }
        return tinypy_bool_from_i32(vm, INT32_C(0));
    }
    if (__tinypy_string_require_text(vm, candidate, "prefix must be a string or tuple", out_error) == 0) {
        return NULL;
    }
    int32_t string_matches_at = __tinypy_string_matches_at(text, begin, finish, candidate, suffix);
    return tinypy_bool_from_i32(vm, string_matches_at);
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
    length = __tinypy_string_character_count(text);
    start = __tinypy_string_optional_bound(vm, args, 2U, length, 0, out_error);
    if (start == INT64_MIN && out_error != NULL && *out_error != NULL) {
        return NULL;
    }
    end = __tinypy_string_optional_bound(vm, args, 3U, length, (int64_t)length, out_error);
    if (end == INT64_MIN && out_error != NULL && *out_error != NULL) {
        return NULL;
    }
    if (start > end) {
        return tinypy_integer_from_i64(vm, 0);
    }
    begin = __tinypy_string_byte_offset(text, (size_t)start);
    finish = __tinypy_string_byte_offset(text, (size_t)end);
    needle_size = TINYPY_TEXT_BYTE_SIZE(needle);
    if (needle_size == 0U) {
        return tinypy_integer_from_i64(vm, (int64_t)((size_t)(end - start) + 1U));
    }
    offset = begin;
    while (offset + needle_size <= finish) {
        const unsigned char *bytes = TINYPY_TEXT_BYTES(text);
        const unsigned char *bytes_2 = TINYPY_TEXT_BYTES(needle);
        ptrdiff_t found = __tinypy_string_find_bytes(bytes + offset, finish - offset, bytes_2, needle_size, INT32_C(0));

        if (found < 0) {
            break;
        }
        count += 1U;
        offset += (size_t)found + needle_size;
    }
    return tinypy_integer_from_i64(vm, (int64_t)count);
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_string_ascii_space(unsigned char character) {
    return character == (unsigned char)' ' || character == (unsigned char)'\t' || character == (unsigned char)'\n' || character == (unsigned char)'\r' || character == (unsigned char)'\v' || character == (unsigned char)'\f';
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_string_strip_contains(tinypy_value_t *characters, unsigned char character) {
    size_t index;

    if (characters == NULL || TINYPY_VALUE_KIND(characters) == TINYPY_VALUE_NONE) {
        return __tinypy_string_ascii_space(character);
    }
    for (index = 0U; index < TINYPY_TEXT_BYTE_SIZE(characters); ++index) {
        if (TINYPY_TEXT_BYTES(characters)[index] == character) {
            return INT32_C(1);
        }
    }
    return INT32_C(0);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_string_strip_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *characters = NULL;
    size_t begin = 0U;
    size_t end;
    intptr_t mode = (intptr_t)user_data;

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
    if (mode <= 0) {
        const unsigned char *bytes = TINYPY_TEXT_BYTES(text);

        while (begin < end && __tinypy_string_strip_contains(characters, bytes[begin]) != 0) {
            begin += 1U;
        }
    }
    if (mode >= 0) {
        const unsigned char *bytes = TINYPY_TEXT_BYTES(text);

        while (end > begin && __tinypy_string_strip_contains(characters, bytes[end - 1U]) != 0) {
            end -= 1U;
        }
    }
    return __tinypy_string_from_span(vm, text, begin, end);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_string_replace_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *new_value;
    int64_t maximum = -1;
    const unsigned char *bytes;
    size_t size;
    size_t old_size;
    size_t offset = 0U;
    size_t replaced = 0U;
    int32_t unicode;
    tinypy_string_builder_t builder;

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
    int condition_4 = TINYPY_TUPLE_SIZE(args) == 4U;
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
    unicode = TINYPY_VALUE_KIND(text) == TINYPY_VALUE_UNICODE || TINYPY_VALUE_KIND(new_value) == TINYPY_VALUE_UNICODE;
    (void)memset(&builder, 0, sizeof(builder));
    builder.vm = vm;
    if (maximum == 0) {
        return __tinypy_string_from_span(vm, text, 0U, size);
    }
    if (old_size == 0U) {
        size_t positions = __tinypy_string_character_count(text) + 1U;
        size_t position;

        for (position = 0U; position < positions; ++position) {
            size_t next = position < positions - 1U ? __tinypy_string_byte_offset(text, position + 1U) : size;

            if (maximum < 0 || (int64_t)replaced < maximum) {
                const unsigned char *bytes_2 = TINYPY_TEXT_BYTES(new_value);
                size_t byte_size = TINYPY_TEXT_BYTE_SIZE(new_value);
                __tinypy_string_builder_append(&builder, bytes_2, byte_size);
                replaced += 1U;
            }
            if (position < positions - 1U) {
                __tinypy_string_builder_append(&builder, bytes + offset, next - offset);
            }
            offset = next;
        }
        return __tinypy_string_builder_finish(&builder, unicode);
    }
    while (offset < size && (maximum < 0 || (int64_t)replaced < maximum)) {
        const unsigned char *bytes_2 = TINYPY_TEXT_BYTES(old_value);
        ptrdiff_t found = __tinypy_string_find_bytes(bytes + offset, size - offset, bytes_2, old_size, INT32_C(0));

        if (found < 0) {
            break;
        }
        __tinypy_string_builder_append(&builder, bytes + offset, (size_t)found);
        const unsigned char *bytes_3 = TINYPY_TEXT_BYTES(new_value);
        size_t byte_size = TINYPY_TEXT_BYTE_SIZE(new_value);
        __tinypy_string_builder_append(&builder, bytes_3, byte_size);
        offset += (size_t)found + old_size;
        replaced += 1U;
    }
    __tinypy_string_builder_append(&builder, bytes + offset, size - offset);
    return __tinypy_string_builder_finish(&builder, unicode);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_string_list_append_span(tinypy_vm_t *vm, tinypy_value_t *list, tinypy_value_t *text, size_t begin, size_t end) {
    tinypy_value_t *item = __tinypy_string_from_span(vm, text, begin, end);

    tinypy_list_append(list, item);
    TINYPY_DECREF(item);
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
    tinypy_internal_cycle_diagnostics_list_reindex(TINYPY_VALUE_VM(list), list);
#endif
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_string_split_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *separator = NULL;
    int64_t maximum = -1;
    int32_t reverse = user_data != NULL;
    const unsigned char *bytes;
    size_t size;
    size_t separator_size = 0U;
    size_t splits = 0U;

    if (__tinypy_string_method_arguments(vm, args, kwargs, 1U, 3U, INT32_C(0), out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *text = TINYPY_TUPLE_GET(args, 0U);
    int condition_5 = TINYPY_TUPLE_SIZE(args) >= 2U;
    if (condition_5 != 0) {
        tinypy_value_t *item = TINYPY_TUPLE_GET(args, 1U);
        condition_5 = TINYPY_VALUE_KIND(item) != TINYPY_VALUE_NONE;
    }
    if (condition_5) {
        separator = TINYPY_TUPLE_GET(args, 1U);
        if (__tinypy_string_require_text(vm, separator, "separator must be a string", out_error) == 0) {
            return NULL;
        }
        separator_size = TINYPY_TEXT_BYTE_SIZE(separator);
        if (separator_size == 0U) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "empty separator", out_error);
            return NULL;
        }
    }
    int condition_6 = TINYPY_TUPLE_SIZE(args) == 3U;
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
    if (separator == NULL) {
        if (reverse == 0) {
            size_t begin = 0U;

            while (begin < size && __tinypy_string_ascii_space(bytes[begin]) != 0) {
                begin += 1U;
            }
            while (begin < size) {
                size_t end;

                if (maximum >= 0 && (int64_t)splits >= maximum) {
                    __tinypy_string_list_append_span(vm, result, text, begin, size);
                    return result;
                }
                end = begin;
                while (end < size && __tinypy_string_ascii_space(bytes[end]) == 0) {
                    end += 1U;
                }
                __tinypy_string_list_append_span(vm, result, text, begin, end);
                splits += 1U;
                begin = end;
                while (begin < size && __tinypy_string_ascii_space(bytes[begin]) != 0) {
                    begin += 1U;
                }
            }
        }
        else {
            size_t end = size;

            while (end != 0U && __tinypy_string_ascii_space(bytes[end - 1U]) != 0) {
                end -= 1U;
            }
            while (end != 0U) {
                size_t begin;

                if (maximum >= 0 && (int64_t)splits >= maximum) {
                    __tinypy_string_list_append_span(vm, result, text, 0U, end);
                    break;
                }
                begin = end;
                while (begin != 0U && __tinypy_string_ascii_space(bytes[begin - 1U]) == 0) {
                    begin -= 1U;
                }
                __tinypy_string_list_append_span(vm, result, text, begin, end);
                splits += 1U;
                end = begin;
                while (end != 0U && __tinypy_string_ascii_space(bytes[end - 1U]) != 0) {
                    end -= 1U;
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
                __tinypy_string_list_append_span(vm, result, text, begin, size);
                break;
            }
            const unsigned char *bytes_2 = TINYPY_TEXT_BYTES(separator);
            found = __tinypy_string_find_bytes(bytes + begin, size - begin, bytes_2, separator_size, INT32_C(0));
            if (found < 0) {
                __tinypy_string_list_append_span(vm, result, text, begin, size);
                break;
            }
            __tinypy_string_list_append_span(vm, result, text, begin, begin + (size_t)found);
            begin += (size_t)found + separator_size;
            splits += 1U;
        }
    }
    else {
        size_t end = size;

        for (;;) {
            ptrdiff_t found;

            if (maximum >= 0 && (int64_t)splits >= maximum) {
                __tinypy_string_list_append_span(vm, result, text, 0U, end);
                break;
            }
            const unsigned char *bytes_2 = TINYPY_TEXT_BYTES(separator);
            found = __tinypy_string_find_bytes(bytes, end, bytes_2, separator_size, INT32_C(1));
            if (found < 0) {
                __tinypy_string_list_append_span(vm, result, text, 0U, end);
                break;
            }
            __tinypy_string_list_append_span(vm, result, text, (size_t)found + separator_size, end);
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
    const unsigned char *source;
    const unsigned char *translation;
    const unsigned char *deleted = NULL;
    size_t source_size;
    size_t translation_size;
    size_t deleted_size = 0U;
    unsigned char *output;
    size_t input_index;
    size_t output_size = 0U;
    tinypy_value_t *result;

    (void)user_data;
    if (__tinypy_string_method_arguments(vm, args, kwargs, 2U, 3U, INT32_C(0), out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *text = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *table = TINYPY_TUPLE_GET(args, 1U);
    if (TINYPY_VALUE_KIND(text) != TINYPY_VALUE_STRING || TINYPY_VALUE_KIND(table) != TINYPY_VALUE_STRING) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "translate requires byte strings", out_error);
        return NULL;
    }
    translation = TINYPY_TEXT_BYTES(table);
    translation_size = TINYPY_TEXT_BYTE_SIZE(table);
    if (translation_size != 256U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "translation table must be 256 characters long", out_error);
        return NULL;
    }
    if (TINYPY_TUPLE_SIZE(args) == 3U) {
        delete_characters = TINYPY_TUPLE_GET(args, 2U);
        if (TINYPY_VALUE_KIND(delete_characters) != TINYPY_VALUE_STRING) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "delete characters must be a string", out_error);
            return NULL;
        }
        deleted = TINYPY_TEXT_BYTES(delete_characters);
        deleted_size = TINYPY_TEXT_BYTE_SIZE(delete_characters);
    }
    source = TINYPY_TEXT_BYTES(text);
    source_size = TINYPY_TEXT_BYTE_SIZE(text);
    if (source_size == 0U) {
        return tinypy_string_from_bytes(vm, NULL, 0U);
    }
    output = (unsigned char *)tinypy_internal_vm_allocate(vm, source_size, (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    for (input_index = 0U; input_index < source_size; ++input_index) {
        unsigned char character = source[input_index];
        size_t deleted_index;
        int32_t remove = INT32_C(0);

        for (deleted_index = 0U; deleted_index < deleted_size; ++deleted_index) {
            if (deleted[deleted_index] == character) {
                remove = INT32_C(1);
                break;
            }
        }
        if (remove == 0) {
            output[output_size++] = translation[character];
        }
    }
    result = tinypy_string_from_bytes(vm, output, output_size);
    tinypy_internal_vm_deallocate(vm, output, source_size, (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_string_case_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_string_builder_t builder;
    const unsigned char *bytes;
    size_t size;
    size_t index;
    intptr_t mode = (intptr_t)user_data;
    int32_t word_start = INT32_C(1);

    if (__tinypy_string_method_arguments(vm, args, kwargs, 1U, 1U, INT32_C(0), out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *text = TINYPY_TUPLE_GET(args, 0U);
    bytes = TINYPY_TEXT_BYTES(text);
    size = TINYPY_TEXT_BYTE_SIZE(text);
    (void)memset(&builder, 0, sizeof(builder));
    builder.vm = vm;
    for (index = 0U; index < size; ++index) {
        unsigned char character = bytes[index];
        int32_t alpha = (character >= (unsigned char)'A' && character <= (unsigned char)'Z') || (character >= (unsigned char)'a' && character <= (unsigned char)'z');

        if (mode == 0 && character >= (unsigned char)'A' && character <= (unsigned char)'Z') {
            character = (unsigned char)(character + ('a' - 'A'));
        }
        else if (mode == 1 && character >= (unsigned char)'a' && character <= (unsigned char)'z') {
            character = (unsigned char)(character - ('a' - 'A'));
        }
        else if (mode == 2) {
            if (character >= (unsigned char)'A' && character <= (unsigned char)'Z') {
                character = (unsigned char)(character + ('a' - 'A'));
            }
            else if (character >= (unsigned char)'a' && character <= (unsigned char)'z') {
                character = (unsigned char)(character - ('a' - 'A'));
            }
        }
        else if (mode == 3 || mode == 4) {
            if (word_start != 0 && character >= (unsigned char)'a' && character <= (unsigned char)'z') {
                character = (unsigned char)(character - ('a' - 'A'));
            }
            else if (word_start == 0 && character >= (unsigned char)'A' && character <= (unsigned char)'Z') {
                character = (unsigned char)(character + ('a' - 'A'));
            }
            if (mode == 3) {
                word_start = INT32_C(0);
            }
            else {
                word_start = alpha == 0;
            }
        }
        __tinypy_string_builder_character(&builder, character);
    }
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(text);
    return __tinypy_string_builder_finish(&builder, kind == TINYPY_VALUE_UNICODE);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_string_predicate_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    const unsigned char *bytes;
    size_t size;
    size_t index;
    intptr_t mode = (intptr_t)user_data;
    int32_t result = INT32_C(1);
    int32_t cased = INT32_C(0);
    int32_t word_start = INT32_C(1);

    if (__tinypy_string_method_arguments(vm, args, kwargs, 1U, 1U, INT32_C(0), out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *text = TINYPY_TUPLE_GET(args, 0U);
    bytes = TINYPY_TEXT_BYTES(text);
    size = TINYPY_TEXT_BYTE_SIZE(text);
    if (size == 0U) {
        return tinypy_bool_from_i32(vm, INT32_C(0));
    }
    for (index = 0U; index < size && result != 0; ++index) {
        unsigned char character = bytes[index];
        int32_t lower = character >= (unsigned char)'a' && character <= (unsigned char)'z';
        int32_t upper = character >= (unsigned char)'A' && character <= (unsigned char)'Z';
        int32_t digit = character >= (unsigned char)'0' && character <= (unsigned char)'9';
        int32_t alpha = lower != 0 || upper != 0;

        if (mode == 0) {
            result = alpha;
        }
        else if (mode == 1) {
            result = digit;
        }
        else if (mode == 2) {
            result = alpha != 0 || digit != 0;
        }
        else if (mode == 3) {
            result = __tinypy_string_ascii_space(character);
        }
        else if (mode == 4) {
            if (upper != 0) {
                result = INT32_C(0);
            }
            if (alpha != 0) {
                cased = INT32_C(1);
            }
        }
        else if (mode == 5) {
            if (lower != 0) {
                result = INT32_C(0);
            }
            if (alpha != 0) {
                cased = INT32_C(1);
            }
        }
        else {
            if (alpha != 0) {
                if ((word_start != 0 && upper == 0) || (word_start == 0 && lower == 0)) {
                    result = INT32_C(0);
                }
                word_start = INT32_C(0);
                cased = INT32_C(1);
            }
            else if (digit == 0) {
                word_start = INT32_C(1);
            }
        }
    }
    if ((mode == 4 || mode == 5 || mode == 6) && cased == 0) {
        result = INT32_C(0);
    }
    return tinypy_bool_from_i32(vm, result);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_string_zfill_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    int64_t width;
    size_t size;
    size_t padding;
    size_t index;
    tinypy_string_builder_t builder;

    (void)user_data;
    int condition_7 = __tinypy_string_method_arguments(vm, args, kwargs, 2U, 2U, INT32_C(0), out_error) == 0;
    if (condition_7 == 0) {
        tinypy_value_t *item = TINYPY_TUPLE_GET(args, 1U);
        condition_7 = __tinypy_string_integer(vm, item, &width, out_error) == 0;
    }
    if (condition_7) {
        return NULL;
    }
    tinypy_value_t *text = TINYPY_TUPLE_GET(args, 0U);
    size = TINYPY_TEXT_BYTE_SIZE(text);
    if (width <= 0 || (uint64_t)width <= size) {
        return __tinypy_string_from_span(vm, text, 0U, size);
    }
    padding = (size_t)width - size;
    (void)memset(&builder, 0, sizeof(builder));
    builder.vm = vm;
    if (size != 0U && (TINYPY_TEXT_BYTES(text)[0] == (unsigned char)'+' || TINYPY_TEXT_BYTES(text)[0] == (unsigned char)'-')) {
        const unsigned char *bytes = TINYPY_TEXT_BYTES(text);
        __tinypy_string_builder_character(&builder, bytes[0]);
        for (index = 0U; index < padding; ++index) {
            __tinypy_string_builder_character(&builder, (unsigned char)'0');
        }
        const unsigned char *bytes_2 = TINYPY_TEXT_BYTES(text);
        __tinypy_string_builder_append(&builder, bytes_2 + 1U, size - 1U);
    }
    else {
        for (index = 0U; index < padding; ++index) {
            __tinypy_string_builder_character(&builder, (unsigned char)'0');
        }
        const unsigned char *bytes = TINYPY_TEXT_BYTES(text);
        __tinypy_string_builder_append(&builder, bytes, size);
    }
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(text);
    return __tinypy_string_builder_finish(&builder, kind == TINYPY_VALUE_UNICODE);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_string_splitlines_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    const unsigned char *bytes;
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
    bytes = TINYPY_TEXT_BYTES(text);
    size = TINYPY_TEXT_BYTE_SIZE(text);
    while (offset < size) {
        size_t content_end;
        size_t line_end;

        while (offset < size && bytes[offset] != (unsigned char)'\n' && bytes[offset] != (unsigned char)'\r') {
            offset += 1U;
        }
        if (offset == size) {
            break;
        }
        content_end = offset;
        offset += 1U;
        if (bytes[content_end] == (unsigned char)'\r' && offset < size && bytes[offset] == (unsigned char)'\n') {
            offset += 1U;
        }
        line_end = keep_ends != 0 ? offset : content_end;
        __tinypy_string_list_append_span(vm, result, text, begin, line_end);
        begin = offset;
    }
    if (begin < size) {
        __tinypy_string_list_append_span(vm, result, text, begin, size);
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_string_expandtabs_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    int64_t tab_size = 8;
    size_t column = 0U;
    size_t offset;
    tinypy_string_builder_t builder;

    (void)user_data;
    if (__tinypy_string_method_arguments(vm, args, kwargs, 1U, 2U, INT32_C(0), out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *text = TINYPY_TUPLE_GET(args, 0U);
    int condition_8 = TINYPY_TUPLE_SIZE(args) == 2U;
    if (condition_8 != 0) {
        tinypy_value_t *item = TINYPY_TUPLE_GET(args, 1U);
        condition_8 = __tinypy_string_integer(vm, item, &tab_size, out_error) == 0;
    }
    if (condition_8) {
        return NULL;
    }
    (void)memset(&builder, 0, sizeof(builder));
    builder.vm = vm;
    for (offset = 0U; offset < TINYPY_TEXT_BYTE_SIZE(text); ++offset) {
        unsigned char character = TINYPY_TEXT_BYTES(text)[offset];

        if (character == (unsigned char)'\t') {
            size_t spaces = tab_size > 0 ? (size_t)tab_size - column % (size_t)tab_size : 0U;

            while (spaces-- != 0U) {
                __tinypy_string_builder_character(&builder, (unsigned char)' ');
                column += 1U;
            }
        }
        else {
            __tinypy_string_builder_character(&builder, character);
            if (character == (unsigned char)'\n' || character == (unsigned char)'\r') {
                column = 0U;
            }
            else if ((character & 0xc0U) != 0x80U) {
                column += 1U;
            }
        }
    }
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(text);
    return __tinypy_string_builder_finish(&builder, kind == TINYPY_VALUE_UNICODE);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_string_partition_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *items[3];
    tinypy_value_t *result;
    ptrdiff_t found;
    size_t size;
    size_t separator_size;
    int32_t reverse = user_data != NULL;

    if (__tinypy_string_method_arguments(vm, args, kwargs, 2U, 2U, INT32_C(0), out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *text = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *separator = TINYPY_TUPLE_GET(args, 1U);
    if (__tinypy_string_require_text(vm, separator, "separator must be a string", out_error) == 0) {
        return NULL;
    }
    separator_size = TINYPY_TEXT_BYTE_SIZE(separator);
    if (separator_size == 0U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "empty separator", out_error);
        return NULL;
    }
    size = TINYPY_TEXT_BYTE_SIZE(text);
    const unsigned char *bytes = TINYPY_TEXT_BYTES(text);
    const unsigned char *bytes_2 = TINYPY_TEXT_BYTES(separator);
    found = __tinypy_string_find_bytes(bytes, size, bytes_2, separator_size, reverse);
    if (found < 0) {
        if (reverse == 0) {
            items[0] = __tinypy_string_from_span(vm, text, 0U, size);
            items[1] = __tinypy_string_from_span(vm, text, 0U, 0U);
            items[2] = __tinypy_string_from_span(vm, text, 0U, 0U);
        }
        else {
            items[0] = __tinypy_string_from_span(vm, text, 0U, 0U);
            items[1] = __tinypy_string_from_span(vm, text, 0U, 0U);
            items[2] = __tinypy_string_from_span(vm, text, 0U, size);
        }
    }
    else {
        items[0] = __tinypy_string_from_span(vm, text, 0U, (size_t)found);
        items[1] = __tinypy_string_from_span(vm, separator, 0U, separator_size);
        items[2] = __tinypy_string_from_span(vm, text, (size_t)found + separator_size, size);
    }
    result = tinypy_tuple_from_items(vm, items, 3U);
    TINYPY_DECREF(items[2]);
    TINYPY_DECREF(items[1]);
    TINYPY_DECREF(items[0]);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_codec_name_equal(const tinypy_value_t *name, const char *canonical) {
    const unsigned char *bytes = TINYPY_TEXT_BYTES(name);
    size_t size = TINYPY_TEXT_BYTE_SIZE(name);
    size_t offset = 0U;
    size_t canonical_offset = 0U;

    while (offset < size || canonical[canonical_offset] != '\0') {
        unsigned char character;

        while (offset < size && (bytes[offset] == (unsigned char)'-' || bytes[offset] == (unsigned char)'_' || bytes[offset] == (unsigned char)' ' || bytes[offset] == (unsigned char)'.')) {
            offset += 1U;
        }
        while (canonical[canonical_offset] == '-' || canonical[canonical_offset] == '_' || canonical[canonical_offset] == ' ' || canonical[canonical_offset] == '.') {
            canonical_offset += 1U;
        }
        if (offset == size || canonical[canonical_offset] == '\0') {
            return offset == size && canonical[canonical_offset] == '\0';
        }
        character = bytes[offset++];
        if (character >= (unsigned char)'A' && character <= (unsigned char)'Z') {
            character = (unsigned char)(character + ('a' - 'A'));
        }
        if (character != (unsigned char)canonical[canonical_offset++]) {
            return INT32_C(0);
        }
    }
    return INT32_C(1);
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_codec_error_mode(tinypy_vm_t *vm, tinypy_value_t *value, tinypy_error_t **out_error) {
    if (value == NULL || __tinypy_codec_name_equal(value, "strict") != 0) {
        return 0;
    }
    if (__tinypy_codec_name_equal(value, "ignore") != 0) {
        return 1;
    }
    if (__tinypy_codec_name_equal(value, "replace") != 0) {
        return 2;
    }
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "unknown codec error handler", out_error);
    return -1;
}
//////////////////////////////////////////////////////////////////////////
static size_t __tinypy_utf8_decode(const unsigned char *bytes, size_t size, uint32_t *out_code_point) {
    unsigned char first;
    size_t width;
    uint32_t code_point;
    size_t index;

    if (size == 0U) {
        return 0U;
    }
    first = bytes[0];
    if (first < 0x80U) {
        *out_code_point = first;
        return 1U;
    }
    width = __tinypy_string_utf8_width(first);
    if (width > size || (width == 2U && first < 0xc2U) || (width == 4U && first > 0xf4U)) {
        return 0U;
    }
    code_point = first & (uint32_t)(0x7fU >> width);
    for (index = 1U; index < width; ++index) {
        if ((bytes[index] & 0xc0U) != 0x80U) {
            return 0U;
        }
        code_point = (code_point << 6U) | (bytes[index] & 0x3fU);
    }
    if ((width == 3U && code_point < 0x800U) || (width == 4U && code_point < 0x10000U) || (code_point >= 0xd800U && code_point <= 0xdfffU) || code_point > 0x10ffffU) {
        return 0U;
    }
    *out_code_point = code_point;
    return width;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_utf8_append(tinypy_string_builder_t *builder, uint32_t code_point) {
    unsigned char bytes[4];
    size_t size;

    if (code_point <= 0x7fU) {
        bytes[0] = (unsigned char)code_point;
        size = 1U;
    }
    else if (code_point <= 0x7ffU) {
        bytes[0] = (unsigned char)(0xc0U | (code_point >> 6U));
        bytes[1] = (unsigned char)(0x80U | (code_point & 0x3fU));
        size = 2U;
    }
    else if (code_point <= 0xffffU) {
        bytes[0] = (unsigned char)(0xe0U | (code_point >> 12U));
        bytes[1] = (unsigned char)(0x80U | ((code_point >> 6U) & 0x3fU));
        bytes[2] = (unsigned char)(0x80U | (code_point & 0x3fU));
        size = 3U;
    }
    else {
        bytes[0] = (unsigned char)(0xf0U | (code_point >> 18U));
        bytes[1] = (unsigned char)(0x80U | ((code_point >> 12U) & 0x3fU));
        bytes[2] = (unsigned char)(0x80U | ((code_point >> 6U) & 0x3fU));
        bytes[3] = (unsigned char)(0x80U | (code_point & 0x3fU));
        size = 4U;
    }
    __tinypy_string_builder_append(builder, bytes, size);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_string_codec_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *encoding = NULL;
    tinypy_value_t *errors = NULL;
    int32_t decode = user_data != NULL;
    int32_t codec = 0;
    int32_t error_mode;
    tinypy_string_builder_t builder;
    const unsigned char *bytes;
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
    if (encoding == NULL || __tinypy_codec_name_equal(encoding, "ascii") != 0) {
        codec = 0;
    }
    else if (__tinypy_codec_name_equal(encoding, "utf8") != 0 || __tinypy_codec_name_equal(encoding, "utf-8") != 0) {
        codec = 1;
    }
    else if (__tinypy_codec_name_equal(encoding, "latin1") != 0 || __tinypy_codec_name_equal(encoding, "iso88591") != 0) {
        codec = 2;
    }
    else {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "unknown encoding", out_error);
        return NULL;
    }
    error_mode = __tinypy_codec_error_mode(vm, errors, out_error);
    if (error_mode < 0) {
        return NULL;
    }
    (void)memset(&builder, 0, sizeof(builder));
    builder.vm = vm;
    bytes = TINYPY_TEXT_BYTES(text);
    size = TINYPY_TEXT_BYTE_SIZE(text);
    if (decode != 0) {
        if (TINYPY_VALUE_KIND(text) == TINYPY_VALUE_UNICODE) {
            return __tinypy_string_from_span(vm, text, 0U, size);
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
                else if (error_mode == 2) {
                    __tinypy_utf8_append(&builder, UINT32_C(0xfffd));
                }
                else if (error_mode == 0) {
                    __tinypy_string_builder_discard(&builder);
                    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "ascii decode error", out_error);
                    return NULL;
                }
                offset += 1U;
            }
        }
        else {
            while (offset < size) {
                uint32_t code_point;
                size_t width = __tinypy_utf8_decode(bytes + offset, size - offset, &code_point);

                if (width != 0U) {
                    __tinypy_string_builder_append(&builder, bytes + offset, width);
                }
                else if (error_mode == 2) {
                    __tinypy_utf8_append(&builder, UINT32_C(0xfffd));
                }
                else if (error_mode == 0) {
                    __tinypy_string_builder_discard(&builder);
                    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "utf-8 decode error", out_error);
                    return NULL;
                }
                offset += width != 0U ? width : 1U;
            }
        }
        return __tinypy_string_builder_finish(&builder, INT32_C(1));
    }
    if (TINYPY_VALUE_KIND(text) == TINYPY_VALUE_STRING) {
        return __tinypy_string_from_span(vm, text, 0U, size);
    }
    while (offset < size) {
        uint32_t code_point;
        size_t width = __tinypy_utf8_decode(bytes + offset, size - offset, &code_point);

        assert(width != 0U);
        if (codec == 1) {
            __tinypy_string_builder_append(&builder, bytes + offset, width);
        }
        else if ((codec == 0 && code_point <= 0x7fU) || (codec == 2 && code_point <= 0xffU)) {
            __tinypy_string_builder_character(&builder, (unsigned char)code_point);
        }
        else if (error_mode == 2) {
            __tinypy_string_builder_character(&builder, (unsigned char)'?');
        }
        else if (error_mode == 0) {
            __tinypy_string_builder_discard(&builder);
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "text encode error", out_error);
            return NULL;
        }
        offset += width;
    }
    return __tinypy_string_builder_finish(&builder, INT32_C(0));
}

typedef struct tinypy_percent_arguments_t {
    tinypy_value_t *value;
    size_t index;
    size_t consumed;
} tinypy_percent_arguments_t;

//////////////////////////////////////////////////////////////////////////
static void __tinypy_percent_unsigned(tinypy_string_builder_t *builder, uint64_t value, uint32_t base, int32_t uppercase, size_t minimum_digits) {
    unsigned char reverse[96];
    size_t count = 0U;

    do {
        uint32_t digit = (uint32_t)(value % base);

        reverse[count++] = digit < 10U ? (unsigned char)('0' + digit) : (unsigned char)((uppercase != 0 ? 'A' : 'a') + digit - 10U);
        value /= base;
    } while (value != 0U);
    while (count < minimum_digits) {
        reverse[count++] = (unsigned char)'0';
    }
    while (count != 0U) {
        __tinypy_string_builder_character(builder, reverse[--count]);
    }
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_percent_long_digits(tinypy_string_builder_t *builder, const tinypy_value_t *value, uint32_t base, int32_t uppercase, size_t minimum_digits) {
    size_t digit_count = TINYPY_LONG_DIGIT_COUNT(value);
    size_t allocation_size;
    uint16_t *work;
    unsigned char *reverse;
    size_t reverse_capacity;
    size_t reverse_count = 0U;
    size_t active;

    if (digit_count == 0U) {
        __tinypy_percent_unsigned(builder, UINT64_C(0), base, uppercase, minimum_digits);
        return;
    }
    assert(digit_count <= SIZE_MAX / sizeof(*work));
    allocation_size = digit_count * sizeof(*work);
    work = (uint16_t *)tinypy_internal_vm_allocate(builder->vm, allocation_size, (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    (void)memcpy(work, TINYPY_LONG_OBJECT(value)->digits, allocation_size);
    assert(digit_count <= SIZE_MAX / 15U);
    reverse_capacity = digit_count * 15U;
    reverse = (unsigned char *)tinypy_internal_vm_allocate(builder->vm, reverse_capacity, (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    active = digit_count;
    while (active != 0U) {
        uint32_t remainder = 0U;
        size_t index = active;

        while (index != 0U) {
            uint32_t current;

            index -= 1U;
            current = (remainder << 15U) | work[index];
            work[index] = (uint16_t)(current / base);
            remainder = current % base;
        }
        reverse[reverse_count++] = remainder < 10U ? (unsigned char)('0' + remainder) : (unsigned char)((uppercase != 0 ? 'A' : 'a') + remainder - 10U);
        while (active != 0U && work[active - 1U] == 0U) {
            active -= 1U;
        }
    }
    while (reverse_count < minimum_digits) {
        reverse[reverse_count++] = (unsigned char)'0';
    }
    while (reverse_count != 0U) {
        __tinypy_string_builder_character(builder, reverse[--reverse_count]);
    }
    tinypy_internal_vm_deallocate(builder->vm, reverse, reverse_capacity, (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    tinypy_internal_vm_deallocate(builder->vm, work, allocation_size, (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_percent_append_integer(tinypy_vm_t *vm, tinypy_string_builder_t *builder, tinypy_value_t *value, unsigned char conversion, int32_t alternate, int32_t plus, int32_t space, int64_t precision, size_t *out_prefix_size, tinypy_error_t **out_error) {
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);
    int32_t negative = INT32_C(0);
    uint32_t base = conversion == (unsigned char)'o' ? 8U : ((conversion == (unsigned char)'x' || conversion == (unsigned char)'X') ? 16U : 10U);
    int32_t uppercase = conversion == (unsigned char)'X';
    size_t minimum_digits = precision >= 0 ? (size_t)precision : 1U;

    if (kind != TINYPY_VALUE_BOOL && kind != TINYPY_VALUE_INTEGER && kind != TINYPY_VALUE_LONG && kind != TINYPY_VALUE_FLOAT) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "integer format requires a number", out_error);
        return INT32_C(0);
    }
    if (kind == TINYPY_VALUE_LONG) {
        negative = TINYPY_LONG_SIGN(value) < 0;
    }
    else if (kind == TINYPY_VALUE_FLOAT) {
        negative = TINYPY_FLOAT_OBJECT(value)->value < 0.0;
    }
    else {
        negative = TINYPY_INTEGER_VALUE(value) < 0;
    }
    if (negative != 0) {
        __tinypy_string_builder_character(builder, (unsigned char)'-');
    }
    else if (plus != 0) {
        __tinypy_string_builder_character(builder, (unsigned char)'+');
    }
    else if (space != 0) {
        __tinypy_string_builder_character(builder, (unsigned char)' ');
    }
    if (alternate != 0 && base == 8U) {
        __tinypy_string_builder_character(builder, (unsigned char)'0');
    }
    else if (alternate != 0 && base == 16U) {
        __tinypy_string_builder_append(builder, uppercase != 0 ? "0X" : "0x", 2U);
    }
    *out_prefix_size = builder->size;
    if (kind == TINYPY_VALUE_LONG) {
        __tinypy_percent_long_digits(builder, value, base, uppercase, minimum_digits);
    }
    else {
        uint64_t magnitude;
        int64_t integer;

        if (kind == TINYPY_VALUE_FLOAT) {
            double number = TINYPY_FLOAT_OBJECT(value)->value;

            if (isfinite(number) == 0 || fabs(number) > (double)INT64_MAX) {
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "float is too large for integer format", out_error);
                return INT32_C(0);
            }
            integer = (int64_t)number;
        }
        else {
            integer = TINYPY_INTEGER_VALUE(value);
        }
        magnitude = integer < 0 ? (uint64_t)(-(integer + INT64_C(1))) + UINT64_C(1) : (uint64_t)integer;
        if (precision == 0 && magnitude == 0U && alternate == 0) {
            return INT32_C(1);
        }
        __tinypy_percent_unsigned(builder, magnitude, base, uppercase, minimum_digits);
    }
    return INT32_C(1);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_percent_fixed(tinypy_string_builder_t *builder, double magnitude, size_t precision) {
    uint64_t integer;
    double fraction;
    size_t index;

    assert(magnitude <= (double)UINT64_MAX);
    integer = (uint64_t)floor(magnitude);
    fraction = magnitude - (double)integer;
    if (precision <= 18U) {
        double scale = pow(10.0, (double)precision);
        uint64_t fractional = (uint64_t)floor(fraction * scale + 0.5);

        if ((double)fractional >= scale) {
            integer += 1U;
            fractional = 0U;
        }
        __tinypy_percent_unsigned(builder, integer, 10U, INT32_C(0), 1U);
        if (precision != 0U) {
            __tinypy_string_builder_character(builder, (unsigned char)'.');
            __tinypy_percent_unsigned(builder, fractional, 10U, INT32_C(0), precision);
        }
        return;
    }
    __tinypy_percent_unsigned(builder, integer, 10U, INT32_C(0), 1U);
    __tinypy_string_builder_character(builder, (unsigned char)'.');
    for (index = 0U; index < precision; ++index) {
        unsigned char digit;

        fraction *= 10.0;
        digit = (unsigned char)floor(fraction);
        if (digit > 9U) {
            digit = 9U;
        }
        __tinypy_string_builder_character(builder, (unsigned char)('0' + digit));
        fraction -= digit;
    }
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_percent_append_float(tinypy_vm_t *vm, tinypy_string_builder_t *builder, tinypy_value_t *value, unsigned char conversion, int32_t alternate, int32_t plus, int32_t space, int64_t precision_value, size_t *out_prefix_size, tinypy_error_t **out_error) {
    double number;
    double magnitude;
    size_t precision = precision_value >= 0 ? (size_t)precision_value : 6U;
    int32_t uppercase = conversion == (unsigned char)'E' || conversion == (unsigned char)'F' || conversion == (unsigned char)'G';
    unsigned char lower = (unsigned char)(conversion >= (unsigned char)'A' && conversion <= (unsigned char)'Z' ? conversion + ('a' - 'A') : conversion);

    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_FLOAT) {
        number = TINYPY_FLOAT_OBJECT(value)->value;
    }
    else if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_BOOL || TINYPY_VALUE_KIND(value) == TINYPY_VALUE_INTEGER) {
        number = (double)TINYPY_INTEGER_VALUE(value);
    }
    else if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_LONG) {
        size_t index = TINYPY_LONG_DIGIT_COUNT(value);

        number = 0.0;
        while (index != 0U) {
            number = number * 32768.0 + (double)TINYPY_LONG_OBJECT(value)->digits[--index];
        }
        if (TINYPY_LONG_SIGN(value) < 0) {
            number = -number;
        }
    }
    else {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "float format requires a number", out_error);
        return INT32_C(0);
    }
    if (signbit(number) != 0) {
        __tinypy_string_builder_character(builder, (unsigned char)'-');
    }
    else if (plus != 0) {
        __tinypy_string_builder_character(builder, (unsigned char)'+');
    }
    else if (space != 0) {
        __tinypy_string_builder_character(builder, (unsigned char)' ');
    }
    *out_prefix_size = builder->size;
    magnitude = fabs(number);
    if (isnan(number) != 0) {
        __tinypy_string_builder_append(builder, uppercase != 0 ? "NAN" : "nan", 3U);
        return INT32_C(1);
    }
    if (isinf(number) != 0) {
        __tinypy_string_builder_append(builder, uppercase != 0 ? "INF" : "inf", 3U);
        return INT32_C(1);
    }
    if (precision > 100000U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "float format precision is too large", out_error);
        return INT32_C(0);
    }
    if (lower == (unsigned char)'f' && magnitude <= (double)UINT64_MAX) {
        __tinypy_percent_fixed(builder, magnitude, precision);
        if (alternate != 0 && precision == 0U) {
            __tinypy_string_builder_character(builder, (unsigned char)'.');
        }
        return INT32_C(1);
    } {
        int selected_value_2;
        if (magnitude == 0.0) {
            selected_value_2 = 0;
        }
        else {
            double logarithm = log10(magnitude);
            selected_value_2 = (int)floor(logarithm);
        }
        int exponent = selected_value_2;
        double normalized = magnitude == 0.0 ? 0.0 : magnitude / pow(10.0, (double)exponent);
        int32_t scientific = lower == (unsigned char)'e';
        size_t effective_precision = precision;

        if (lower == (unsigned char)'g') {
            if (effective_precision == 0U) {
                effective_precision = 1U;
            }
            scientific = exponent < -4 || exponent >= (int)effective_precision;
            effective_precision -= 1U;
        }
        if (scientific != 0) {
            size_t before = builder->size;

            __tinypy_percent_fixed(builder, normalized, effective_precision);
            if (lower == (unsigned char)'g' && alternate == 0) {
                while (builder->size > before && builder->bytes[builder->size - 1U] == (unsigned char)'0') {
                    builder->size -= 1U;
                }
                if (builder->size > before && builder->bytes[builder->size - 1U] == (unsigned char)'.') {
                    builder->size -= 1U;
                }
            }
            __tinypy_string_builder_character(builder, uppercase != 0 ? (unsigned char)'E' : (unsigned char)'e');
            __tinypy_string_builder_character(builder, exponent < 0 ? (unsigned char)'-' : (unsigned char)'+');
            __tinypy_percent_unsigned(builder, (uint64_t)(exponent < 0 ? -exponent : exponent), 10U, INT32_C(0), 2U);
            return INT32_C(1);
        }
        if (lower == (unsigned char)'g') {
            size_t fractional_precision = exponent >= 0 ? (effective_precision > (size_t)exponent ? effective_precision - (size_t)exponent : 0U) : effective_precision + (size_t)(-exponent);
            size_t before = builder->size;

            if (magnitude > (double)UINT64_MAX) {
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "float is too large for fixed format", out_error);
                return INT32_C(0);
            }
            __tinypy_percent_fixed(builder, magnitude, fractional_precision);
            if (alternate == 0) {
                while (builder->size > before && builder->bytes[builder->size - 1U] == (unsigned char)'0') {
                    builder->size -= 1U;
                }
                if (builder->size > before && builder->bytes[builder->size - 1U] == (unsigned char)'.') {
                    builder->size -= 1U;
                }
            }
            return INT32_C(1);
        }
    }
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "float is too large for fixed format", out_error);
    return INT32_C(0);
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
static tinypy_value_t *__tinypy_percent_mapping_argument(tinypy_vm_t *vm, tinypy_value_t *format, tinypy_value_t *mapping, const unsigned char *key_bytes, size_t key_size, tinypy_error_t **out_error) {
    tinypy_value_t *key = TINYPY_VALUE_KIND(format) == TINYPY_VALUE_UNICODE ? tinypy_unicode_from_utf8(vm, (const char *)key_bytes, key_size) : tinypy_string_from_bytes(vm, key_bytes, key_size);
    tinypy_value_t *value = tinypy_get_item(mapping, key, out_error);

    TINYPY_DECREF(key);
    return value;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_percent_append_padded(tinypy_string_builder_t *output, tinypy_string_builder_t *field, size_t prefix_size, int64_t width, int32_t left, int32_t zero) {
    size_t padding = width > 0 && (uint64_t)width > field->size ? (size_t)width - field->size : 0U;
    size_t index;

    if (left == 0 && zero == 0) {
        for (index = 0U; index < padding; ++index) {
            __tinypy_string_builder_character(output, (unsigned char)' ');
        }
    }
    if (left == 0 && zero != 0 && padding != 0U) {
        __tinypy_string_builder_append(output, field->bytes, prefix_size);
        for (index = 0U; index < padding; ++index) {
            __tinypy_string_builder_character(output, (unsigned char)'0');
        }
        __tinypy_string_builder_append(output, field->bytes + prefix_size, field->size - prefix_size);
    }
    else {
        __tinypy_string_builder_append(output, field->bytes, field->size);
    }
    if (left != 0) {
        for (index = 0U; index < padding; ++index) {
            __tinypy_string_builder_character(output, (unsigned char)' ');
        }
    }
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_string_percent(tinypy_value_t *format, tinypy_value_t *argument_value, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(format);
    const unsigned char *bytes = TINYPY_TEXT_BYTES(format);
    size_t size = TINYPY_TEXT_BYTE_SIZE(format);
    size_t offset = 0U;
    int32_t unicode = TINYPY_VALUE_KIND(format) == TINYPY_VALUE_UNICODE;
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
        unsigned char conversion;
        tinypy_value_t *value;
        tinypy_string_builder_t field;
        size_t prefix_size = 0U;

        if (bytes[offset] != (unsigned char)'%') {
            __tinypy_string_builder_character(&output, bytes[offset++]);
            continue;
        }
        offset += 1U;
        if (offset < size && bytes[offset] == (unsigned char)'%') {
            __tinypy_string_builder_character(&output, (unsigned char)'%');
            offset += 1U;
            continue;
        }
        if (offset >= size) {
            __tinypy_string_builder_discard(&output);
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "incomplete format", out_error);
            return NULL;
        }
        if (bytes[offset] == (unsigned char)'(') {
            size_t depth = 1U;

            key_begin = ++offset;
            while (offset < size && depth != 0U) {
                if (bytes[offset] == (unsigned char)'(') {
                    depth += 1U;
                }
                else if (bytes[offset] == (unsigned char)')') {
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
            if (offset < size && bytes[offset] == (unsigned char)'#') {
                alternate = INT32_C(1);
            }
            else if (offset < size && bytes[offset] == (unsigned char)'0') {
                zero = INT32_C(1);
            }
            else if (offset < size && bytes[offset] == (unsigned char)'-') {
                left = INT32_C(1);
            }
            else if (offset < size && bytes[offset] == (unsigned char)' ') {
                space = INT32_C(1);
            }
            else if (offset < size && bytes[offset] == (unsigned char)'+') {
                plus = INT32_C(1);
            }
            else {
                break;
            }
            offset += 1U;
        }
        if (offset < size && bytes[offset] == (unsigned char)'*') {
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
                left = INT32_C(1);
                width = -width;
            }
            offset += 1U;
        }
        else {
            while (offset < size && bytes[offset] >= (unsigned char)'0' && bytes[offset] <= (unsigned char)'9') {
                width = width * 10 + (int64_t)(bytes[offset++] - (unsigned char)'0');
            }
        }
        if (offset < size && bytes[offset] == (unsigned char)'.') {
            offset += 1U;
            precision = 0;
            if (offset < size && bytes[offset] == (unsigned char)'*') {
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
                    precision = -1;
                }
                offset += 1U;
            }
            else {
                while (offset < size && bytes[offset] >= (unsigned char)'0' && bytes[offset] <= (unsigned char)'9') {
                    precision = precision * 10 + (int64_t)(bytes[offset++] - (unsigned char)'0');
                }
            }
        }
        while (offset < size && (bytes[offset] == (unsigned char)'h' || bytes[offset] == (unsigned char)'l' || bytes[offset] == (unsigned char)'L')) {
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
        if (conversion == (unsigned char)'s' || conversion == (unsigned char)'r') {
            tinypy_value_t *text = conversion == (unsigned char)'r' ? tinypy_object_repr(value, out_error) : tinypy_object_str(value, out_error);
            size_t text_size;

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
            const unsigned char *bytes_2 = TINYPY_TEXT_BYTES(text);
            __tinypy_string_builder_append(&field, bytes_2, text_size);
            if (TINYPY_VALUE_KIND(text) == TINYPY_VALUE_UNICODE) {
                unicode = INT32_C(1);
            }
            TINYPY_DECREF(text);
        }
        else if (conversion == (unsigned char)'c') {
            if (__tinypy_string_is_text(value) != 0 && __tinypy_string_character_count(value) == 1U) {
                const unsigned char *bytes_2 = TINYPY_TEXT_BYTES(value);
                size_t byte_size = TINYPY_TEXT_BYTE_SIZE(value);
                __tinypy_string_builder_append(&field, bytes_2, byte_size);
                if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_UNICODE) {
                    unicode = INT32_C(1);
                }
            }
            else {
                int64_t character;

                if (__tinypy_string_integer(vm, value, &character, out_error) == 0 || character < 0 || character > 255) {
                    if (out_error == NULL || *out_error == NULL) {
                        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "%c argument is out of range", out_error);
                    }
                    TINYPY_DECREF(value);
                    __tinypy_string_builder_discard(&field);
                    __tinypy_string_builder_discard(&output);
                    return NULL;
                }
                __tinypy_string_builder_character(&field, (unsigned char)character);
            }
        }
        else if (conversion == (unsigned char)'d' || conversion == (unsigned char)'i' || conversion == (unsigned char)'u' || conversion == (unsigned char)'o' || conversion == (unsigned char)'x' || conversion == (unsigned char)'X') {
            if (__tinypy_percent_append_integer(vm, &field, value, conversion, alternate, plus, space, precision, &prefix_size, out_error) == 0) {
                TINYPY_DECREF(value);
                __tinypy_string_builder_discard(&field);
                __tinypy_string_builder_discard(&output);
                return NULL;
            }
            if (precision >= 0) {
                zero = INT32_C(0);
            }
        }
        else if (conversion == (unsigned char)'e' || conversion == (unsigned char)'E' || conversion == (unsigned char)'f' || conversion == (unsigned char)'F' || conversion == (unsigned char)'g' || conversion == (unsigned char)'G') {
            if (__tinypy_percent_append_float(vm, &field, value, conversion, alternate, plus, space, precision, &prefix_size, out_error) == 0) {
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
        __tinypy_percent_append_padded(&output, &field, prefix_size, width, left, zero);
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
    return __tinypy_string_builder_finish(&output, unicode);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_string_add_method(tinypy_type_t *type, const char *name, size_t name_size, tinypy_native_function_callback_t callback, void *user_data) {
    tinypy_value_t *function = tinypy_native_function_new(type->vm, name, name_size, callback, user_data, NULL);
    tinypy_value_t *key = tinypy_string_from_bytes(type->vm, name, name_size);

    tinypy_dict_set(type->dict, key, function);
    TINYPY_DECREF(key);
    TINYPY_DECREF(function);
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_initialize_string_types(tinypy_vm_t *vm) {
    tinypy_type_t *types[2] = {&vm->string_type, &vm->unicode_type};
    size_t index;

    for (index = 0U; index < 2U; ++index) {
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
        if (types[index] == &vm->string_type) {
            __tinypy_string_add_method(types[index], "translate", 9U, __tinypy_string_translate_method, NULL);
        }
    }
}
