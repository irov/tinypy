#include "internal.h"

#include <math.h>
#include <string.h>

typedef enum tinypy_struct_byte_order_e {
    TINYPY_STRUCT_NATIVE_ENDIAN,
    TINYPY_STRUCT_LITTLE_ENDIAN,
    TINYPY_STRUCT_BIG_ENDIAN
} tinypy_struct_byte_order_e;

typedef struct tinypy_struct_format_t {
    tinypy_struct_byte_order_e byte_order;
    tinypy_bool_t native_format;
    size_t item_count;
    size_t byte_size;
} tinypy_struct_format_t;

typedef struct tinypy_struct_buffer_t {
    const uint8_t *bytes;
    size_t size;
    tinypy_value_t *encoded;
    tinypy_value_t *owner;
    tinypy_value_t *export_owner;
} tinypy_struct_buffer_t;

typedef struct tinypy_struct_payload_t {
    tinypy_value_t *format_value;
    tinypy_struct_format_t format;
    tinypy_bool_t initialized;
} tinypy_struct_payload_t;

#define TINYPY_STRUCT_CACHE_SIZE 100U

typedef struct tinypy_struct_state_t {
    tinypy_vm_t *vm;
    size_t reference_count;
    tinypy_value_t *cache;
} tinypy_struct_state_t;

//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_struct_arguments(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, size_t minimum, size_t maximum, tinypy_arity_style_e style, tinypy_error_t **out_error) {
    if (TINYPY_NATIVE_FUNCTION_OBJECT(function)->owner != NULL || TINYPY_NATIVE_FUNCTION_OBJECT(function)->self != NULL) {
        tinypy_bool_t accepted = tinypy_internal_native_method_arguments(function, args, kwargs, minimum, maximum, style, out_error);

        return accepted;
    }
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *name = tinypy_native_function_name(function);
    size_t count = TINYPY_TUPLE_SIZE(args);

    if (kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U) {
        tinypy_message_part_t parts[] = {
            TINYPY_MESSAGE_PART_TEXT(name),
            TINYPY_MESSAGE_PART_LITERAL("() takes no keyword arguments")
        };

        tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, 2U, out_error);
        return TINYPY_FALSE;
    }
    if (count < minimum || count > maximum) {
        tinypy_internal_make_arity_error(vm, (const char *)TINYPY_TEXT_BYTES(name), TINYPY_TEXT_BYTE_SIZE(name), count, minimum, maximum, style, out_error);
        return TINYPY_FALSE;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_struct_format_value(tinypy_vm_t *vm, tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);

    if (kind == TINYPY_VALUE_STRING) {
        return TINYPY_RET(value);
    }
    if (kind == TINYPY_VALUE_UNICODE) {
        tinypy_value_t *result = tinypy_internal_text_codec(vm, value, vm->internal_codec_ascii_name, NULL, TINYPY_FALSE, TINYPY_TRUE, NULL, out_error);

        return result;
    }
    tinypy_message_part_t parts[] = {
        TINYPY_MESSAGE_PART_LITERAL("Struct() argument 1 must be string, not "),
        TINYPY_MESSAGE_PART_TYPE_NAME(value)
    };

    tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, 2U, out_error);
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_struct_parse_format_uncached(tinypy_vm_t *vm, tinypy_value_t *value, tinypy_struct_format_t *out_format, tinypy_error_t **out_error) {
    const uint8_t *bytes = TINYPY_TEXT_BYTES(value);
    size_t size = TINYPY_TEXT_BYTE_SIZE(value);
    const uint8_t *terminator = (const uint8_t *)memchr(bytes, 0, size);
    size_t index = 0U;
    size_t count = 0U;
    size_t size_limit = SIZE_MAX;
    int32_t have_count = INT32_C(0);

    if (terminator != NULL) {
        size = (size_t)(terminator - bytes);
    }
    out_format->byte_order = TINYPY_STRUCT_NATIVE_ENDIAN;
    out_format->native_format = TINYPY_TRUE;
    out_format->item_count = 0U;
    out_format->byte_size = 0U;
#if SIZE_MAX > INT64_MAX
    size_limit = (size_t)INT64_MAX;
#endif
    if (index < size) {
        uint8_t prefix = bytes[index];

        if (prefix == (uint8_t)'<' || prefix == (uint8_t)'>' || prefix == (uint8_t)'!' || prefix == (uint8_t)'=' || prefix == (uint8_t)'@') {
            out_format->native_format = prefix == (uint8_t)'@' ? TINYPY_TRUE : TINYPY_FALSE;
            if (prefix == (uint8_t)'<') {
                out_format->byte_order = TINYPY_STRUCT_LITTLE_ENDIAN;
            }
            else if (prefix == (uint8_t)'>' || prefix == (uint8_t)'!') {
                out_format->byte_order = TINYPY_STRUCT_BIG_ENDIAN;
            }
            index += 1U;
        }
    }
    while (index < size) {
        uint8_t character = bytes[index++];

        if (character == (uint8_t)' ' || character == (uint8_t)'\t' || character == (uint8_t)'\r' || character == (uint8_t)'\n' || character == (uint8_t)'\v' || character == (uint8_t)'\f') {
            if (have_count != 0) {
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "bad char in struct format", out_error);
                return TINYPY_FALSE;
            }
            continue;
        }
        if (character >= (uint8_t)'0' && character <= (uint8_t)'9') {
            size_t digit = (size_t)(character - (uint8_t)'0');

            if (count > (size_limit - digit) / 10U) {
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "total struct size too long", out_error);
                return TINYPY_FALSE;
            }
            count = count * 10U + digit;
            have_count = INT32_C(1);
            continue;
        }
        if (character != (uint8_t)'d') {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "bad char in struct format", out_error);
            return TINYPY_FALSE;
        }
        if (have_count == 0) {
            count = 1U;
        }
        if (count > size_limit / 8U || out_format->byte_size > size_limit - count * 8U || out_format->item_count > size_limit - count) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "total struct size too long", out_error);
            return TINYPY_FALSE;
        }
        out_format->byte_size += count * 8U;
        out_format->item_count += count;
        count = 0U;
        have_count = INT32_C(0);
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_struct_cache_clear(tinypy_struct_state_t *state) {
    tinypy_value_t *cache = state->cache;

    state->cache = NULL;
    if (cache != NULL) {
        TINYPY_DECREF(cache);
    }
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_struct_state_finalize(void *user_data) {
    tinypy_struct_state_t *state = (tinypy_struct_state_t *)user_data;

    state->reference_count -= 1U;
    if (state->reference_count == 0U) {
        __tinypy_struct_cache_clear(state);
        tinypy_internal_vm_deallocate(state->vm, state, sizeof(*state));
    }
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_struct_parse_format(tinypy_struct_state_t *state, tinypy_value_t *value, tinypy_struct_format_t *out_format, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = state->vm;
    if (state->cache == NULL) {
        state->cache = tinypy_dict_new(vm);
    }
    tinypy_value_t *cache = TINYPY_RET(state->cache);
    tinypy_value_t *cached = tinypy_internal_dict_get_optional_suppressed(vm, cache, value);
    if (cached != NULL) {
        TINYPY_INCREF(cached);
        out_format->byte_order = (tinypy_struct_byte_order_e)TINYPY_INTEGER_VALUE(TINYPY_TUPLE_GET(cached, 1U));
        out_format->native_format = (tinypy_bool_t)TINYPY_INTEGER_VALUE(TINYPY_TUPLE_GET(cached, 2U));
        out_format->item_count = (size_t)TINYPY_INTEGER_VALUE(TINYPY_TUPLE_GET(cached, 3U));
        out_format->byte_size = (size_t)TINYPY_INTEGER_VALUE(TINYPY_TUPLE_GET(cached, 4U));
        tinypy_value_t *result = TINYPY_RET(TINYPY_TUPLE_GET(cached, 0U));

        TINYPY_DECREF(cached);
        TINYPY_DECREF(cache);
        return result;
    }
    tinypy_value_t *normalized = __tinypy_struct_format_value(vm, value, out_error);
    if (normalized == NULL) {
        TINYPY_DECREF(cache);
        return NULL;
    }
    if (__tinypy_struct_parse_format_uncached(vm, normalized, out_format, out_error) == TINYPY_FALSE) {
        TINYPY_DECREF(normalized);
        TINYPY_DECREF(cache);
        return NULL;
    }
    tinypy_value_t *items[] = {
        normalized,
        tinypy_integer_from_i64(vm, (int64_t)out_format->byte_order),
        tinypy_integer_from_i64(vm, (int64_t)out_format->native_format),
        tinypy_integer_from_i64(vm, (int64_t)out_format->item_count),
        tinypy_integer_from_i64(vm, (int64_t)out_format->byte_size)
    };
    tinypy_value_t *entry = tinypy_tuple_from_items(vm, items, sizeof(items) / sizeof(items[0]));

    for (size_t index = 1U; index < sizeof(items) / sizeof(items[0]); ++index) {
        TINYPY_DECREF(items[index]);
    }
    if (TINYPY_DICT_SIZE(cache) >= TINYPY_STRUCT_CACHE_SIZE) {
        tinypy_dict_clear(cache);
    }
    tinypy_internal_exception_state_t saved;
    tinypy_error_t *cache_error = NULL;
    tinypy_internal_exception_preserve_begin(vm, &saved);
    (void)tinypy_internal_dict_set_checked(vm, cache, value, entry, &cache_error);
    if (cache_error != NULL) {
        tinypy_error_release(cache_error);
    }
    tinypy_internal_exception_preserve_end(vm, &saved);
    TINYPY_DECREF(entry);
    TINYPY_DECREF(cache);
    return normalized;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_struct_call_format(tinypy_value_t *function, tinypy_struct_state_t *state, tinypy_value_t *args, tinypy_struct_format_t *out_format, tinypy_error_t **out_error) {
    tinypy_value_t *first = TINYPY_TUPLE_GET(args, 0U);

    if (TINYPY_NATIVE_FUNCTION_OBJECT(function)->owner != NULL || TINYPY_NATIVE_FUNCTION_OBJECT(function)->self != NULL) {
        tinypy_struct_payload_t *payload = (tinypy_struct_payload_t *)tinypy_native_instance_payload(first);

        if (payload->initialized == TINYPY_FALSE) {
            tinypy_internal_make_vm_error(state->vm, TINYPY_ERROR_VALUE, "uninitialized Struct", out_error);
            return NULL;
        }
        *out_format = payload->format;
        return TINYPY_RET(payload->format_value);
    }
    tinypy_value_t *result = __tinypy_struct_parse_format(state, first, out_format, out_error);

    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_struct_native_little_endian(void) {
    uint16_t probe = UINT16_C(1);

    return *((const uint8_t *)&probe) == (uint8_t)1 ? TINYPY_TRUE : TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
static uint64_t __tinypy_struct_read_u64(const uint8_t *bytes, tinypy_struct_byte_order_e byte_order) {
    int32_t little = byte_order == TINYPY_STRUCT_LITTLE_ENDIAN || (byte_order == TINYPY_STRUCT_NATIVE_ENDIAN && __tinypy_struct_native_little_endian() != 0);
    uint64_t bits = UINT64_C(0);
    size_t index;

    for (index = 0U; index < 8U; ++index) {
        size_t source_index = little != 0 ? 7U - index : index;

        bits = (bits << 8U) | bytes[source_index];
    }
    return bits;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_struct_write_u64(uint8_t *bytes, uint64_t bits, tinypy_struct_byte_order_e byte_order) {
    int32_t little = byte_order == TINYPY_STRUCT_LITTLE_ENDIAN || (byte_order == TINYPY_STRUCT_NATIVE_ENDIAN && __tinypy_struct_native_little_endian() != 0);
    size_t index;

    for (index = 0U; index < 8U; ++index) {
        size_t destination_index = little != 0 ? index : 7U - index;

        bytes[destination_index] = (uint8_t)(bits & UINT64_C(0xff));
        bits >>= 8U;
    }
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_struct_as_double(tinypy_vm_t *vm, tinypy_value_t *value, double *out_value, tinypy_error_t **out_error) {
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);

    if (kind == TINYPY_VALUE_FLOAT) {
        *out_value = tinypy_float_as_double(value);
        return TINYPY_TRUE;
    }
    if ((kind == TINYPY_VALUE_BOOL || kind == TINYPY_VALUE_INTEGER) && tinypy_internal_object_has_special_override_key(value, vm->internal_special_float_key) == 0) {
        *out_value = (double)TINYPY_INTEGER_VALUE(value);
        return TINYPY_TRUE;
    }
    if (kind == TINYPY_VALUE_LONG && tinypy_internal_object_has_special_override_key(value, vm->internal_special_float_key) == 0) {
        tinypy_error_t *conversion_error = NULL;

        if (tinypy_long_as_double(value, out_value, &conversion_error) != 0) {
            return TINYPY_TRUE;
        }
        if (conversion_error != NULL) {
            tinypy_error_release(conversion_error);
        }
        tinypy_vm_clear_error(vm);
    }
    else {
        tinypy_bool_t handled;
        tinypy_error_t *conversion_error = NULL;
        tinypy_value_t *converted = tinypy_internal_call_conversion(value, vm->internal_special_float_key, &handled, &conversion_error);

        if (converted != NULL) {
            if (TINYPY_VALUE_KIND(converted) == TINYPY_VALUE_FLOAT) {
                *out_value = tinypy_float_as_double(converted);
                TINYPY_DECREF(converted);
                return TINYPY_TRUE;
            }
            TINYPY_DECREF(converted);
        }
        if (conversion_error != NULL) {
            tinypy_error_release(conversion_error);
        }
        tinypy_vm_clear_error(vm);
    }
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "required argument is not a float", out_error);
    return TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_struct_calcsize(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_struct_state_t *state = (tinypy_struct_state_t *)user_data;
    tinypy_struct_format_t format;

    if (__tinypy_struct_arguments(function, args, kwargs, 1U, 1U, TINYPY_ARITY_STYLE_SINGLE, out_error) == TINYPY_FALSE) {
        return NULL;
    }
    tinypy_value_t *item = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *format_value = __tinypy_struct_parse_format(state, item, &format, out_error);
    if (format_value == NULL) {
        return NULL;
    }
    tinypy_value_t *result = tinypy_integer_from_i64(vm, (int64_t)format.byte_size);
    TINYPY_DECREF(format_value);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_struct_encode_double(tinypy_vm_t *vm, const tinypy_struct_format_t *format, tinypy_value_t *item, uint8_t *bytes, tinypy_error_t **out_error) {
    double value;
    uint64_t bits;

    if (__tinypy_struct_as_double(vm, item, &value, out_error) == 0) {
        return TINYPY_FALSE;
    }
    if (format->native_format == 0 && vm->double_format_unknown != 0) {
        if (isfinite(value) == 0) {
            tinypy_internal_exception_raise_system_error(vm, "frexp() result out of range", out_error);
            return TINYPY_FALSE;
        }
        if (value == 0.0) {
            bits = UINT64_C(0);
        }
        else {
            (void)memcpy(&bits, &value, sizeof(bits));
        }
    }
    else {
        (void)memcpy(&bits, &value, sizeof(bits));
    }
    __tinypy_struct_write_u64(bytes, bits, format->byte_order);
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_struct_buffer_acquire(tinypy_vm_t *vm, tinypy_value_t *value, tinypy_bool_t writable, tinypy_struct_buffer_t *buffer, tinypy_error_t **out_error) {
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);
    tinypy_value_t *bytes_value = value;

    (void)memset(buffer, 0, sizeof(*buffer));
    if (writable != 0 && kind != TINYPY_VALUE_BYTEARRAY && (tinypy_internal_memoryview_check(value) == 0 || tinypy_internal_memoryview_is_readonly(value) != 0)) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "argument must be read-write buffer", out_error);
        return TINYPY_FALSE;
    }
    if (writable == 0 && kind == TINYPY_VALUE_NONE) {
        return TINYPY_TRUE;
    }
    if (kind == TINYPY_VALUE_UNICODE) {
        buffer->owner = TINYPY_RET(value);
        buffer->encoded = tinypy_internal_text_codec(vm, value, NULL, NULL, TINYPY_FALSE, TINYPY_TRUE, NULL, out_error);
        if (buffer->encoded == NULL) {
            return TINYPY_FALSE;
        }
        bytes_value = buffer->encoded;
    }
    if (tinypy_internal_bytes_view(bytes_value, &buffer->bytes, &buffer->size) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "argument must be string or buffer", out_error);
        return TINYPY_FALSE;
    }
    if (kind != TINYPY_VALUE_UNICODE) {
        buffer->owner = TINYPY_RET(value);
    }
    if (kind == TINYPY_VALUE_BYTEARRAY) {
        buffer->export_owner = value;
        TINYPY_BYTEARRAY_OBJECT(value)->exports += 1U;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_struct_buffer_release(tinypy_struct_buffer_t *buffer) {
    if (buffer->export_owner != NULL) {
        TINYPY_BYTEARRAY_OBJECT(buffer->export_owner)->exports -= 1U;
    }
    if (buffer->encoded != NULL) {
        TINYPY_DECREF(buffer->encoded);
    }
    if (buffer->owner != NULL) {
        TINYPY_DECREF(buffer->owner);
    }
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_struct_unpack_values(tinypy_vm_t *vm, const tinypy_struct_format_t *format, const uint8_t *bytes, tinypy_error_t **out_error) {
    size_t index;

    if (format->item_count == 0U) {
        tinypy_value_t *result = TINYPY_RET_EMPTY_TUPLE(vm);
        return result;
    }
    size_t allocation_size = format->item_count * sizeof(tinypy_value_t *);
    tinypy_value_t **items = (tinypy_value_t **)tinypy_internal_vm_allocate_checked(vm, allocation_size, out_error);
    if (items == NULL) {
        return NULL;
    }
    for (index = 0U; index < format->item_count; ++index) {
        uint64_t bits = __tinypy_struct_read_u64(bytes + index * 8U, format->byte_order);
        double value;

        (void)memcpy(&value, &bits, sizeof(value));
        if (format->native_format == 0 && vm->double_format_unknown != 0 && isfinite(value) == 0) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "can't unpack IEEE 754 special value on non-IEEE platform", out_error);
            for (size_t previous = 0U; previous < index; ++previous) {
                TINYPY_DECREF(items[previous]);
            }
            tinypy_internal_vm_deallocate(vm, items, allocation_size);
            return NULL;
        }
        items[index] = tinypy_float_from_double(vm, value);
    }
    tinypy_value_t *result = tinypy_tuple_from_items(vm, items, format->item_count);
    for (index = 0U; index < format->item_count; ++index) {
        TINYPY_DECREF(items[index]);
    }
    tinypy_internal_vm_deallocate(vm, items, allocation_size);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_struct_unpack(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_struct_state_t *state = (tinypy_struct_state_t *)user_data;
    tinypy_struct_format_t format;
    tinypy_struct_buffer_t buffer;
    tinypy_bool_t method = TINYPY_NATIVE_FUNCTION_OBJECT(function)->owner != NULL || TINYPY_NATIVE_FUNCTION_OBJECT(function)->self != NULL ? TINYPY_TRUE : TINYPY_FALSE;

    if (__tinypy_struct_arguments(function, args, kwargs, method != TINYPY_FALSE ? 1U : 2U, method != TINYPY_FALSE ? 1U : 2U, method != TINYPY_FALSE ? TINYPY_ARITY_STYLE_SINGLE : TINYPY_ARITY_STYLE_UNPACK, out_error) == TINYPY_FALSE) {
        return NULL;
    }
    tinypy_value_t *format_value = __tinypy_struct_call_format(function, state, args, &format, out_error);
    if (format_value == NULL) {
        return NULL;
    }
    tinypy_value_t *item = TINYPY_TUPLE_GET(args, 1U);
    tinypy_error_t *buffer_error = NULL;
    tinypy_bool_t acquired = __tinypy_struct_buffer_acquire(vm, item, TINYPY_FALSE, &buffer, &buffer_error);
    if (acquired == TINYPY_FALSE || TINYPY_VALUE_KIND(item) == TINYPY_VALUE_NONE || buffer.size != format.byte_size) {
        if (buffer_error != NULL) {
            tinypy_error_release(buffer_error);
            tinypy_vm_clear_error(vm);
        }
        __tinypy_struct_buffer_release(&buffer);
        TINYPY_DECREF(format_value);
        char length_buffer[TINYPY_MESSAGE_SIZE_BUFFER];
        size_t length_size = tinypy_internal_format_size(length_buffer, format.byte_size);
        tinypy_message_part_t parts[] = {
            TINYPY_MESSAGE_PART_LITERAL("unpack requires a string argument of length "),
            {length_buffer, length_size}
        };

        tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_VALUE, parts, 2U, out_error);
        return NULL;
    }
    tinypy_value_t *result = __tinypy_struct_unpack_values(vm, &format, buffer.bytes, out_error);
    __tinypy_struct_buffer_release(&buffer);
    TINYPY_DECREF(format_value);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_struct_item_count_error(tinypy_vm_t *vm, const char *operation, size_t operation_size, size_t expected, size_t supplied, tinypy_error_t **out_error) {
    char expected_buffer[TINYPY_MESSAGE_SIZE_BUFFER];
    char supplied_buffer[TINYPY_MESSAGE_SIZE_BUFFER];
    size_t expected_size = tinypy_internal_format_size(expected_buffer, expected);
    size_t supplied_size = tinypy_internal_format_size(supplied_buffer, supplied);
    tinypy_message_part_t parts[] = {
        {operation, operation_size},
        TINYPY_MESSAGE_PART_LITERAL(" expected "),
        {expected_buffer, expected_size},
        TINYPY_MESSAGE_PART_LITERAL(" items for packing (got "),
        {supplied_buffer, supplied_size},
        TINYPY_MESSAGE_PART_LITERAL(")")
    };

    tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_VALUE, parts, 6U, out_error);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_struct_pack(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_struct_state_t *state = (tinypy_struct_state_t *)user_data;
    tinypy_struct_format_t format;
    uint8_t *bytes;
    size_t index;

    if (__tinypy_struct_arguments(function, args, kwargs, 0U, SIZE_MAX, TINYPY_ARITY_STYLE_PARSED, out_error) == TINYPY_FALSE) {
        return NULL;
    }
    if (TINYPY_TUPLE_SIZE(args) == 0U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "missing format argument", out_error);
        return NULL;
    }
    tinypy_value_t *format_value = __tinypy_struct_call_format(function, state, args, &format, out_error);
    if (format_value == NULL) {
        return NULL;
    }
    if (TINYPY_TUPLE_SIZE(args) != format.item_count + 1U) {
        __tinypy_struct_item_count_error(vm, "pack", 4U, format.item_count, TINYPY_TUPLE_SIZE(args) - 1U, out_error);
        TINYPY_DECREF(format_value);
        return NULL;
    }
    if (format.byte_size == 0U) {
        tinypy_value_t *return_value_1 = TINYPY_RET_EMPTY_STRING(vm);
        TINYPY_DECREF(format_value);
        return return_value_1;
    }
    tinypy_value_t *result = tinypy_internal_text_allocate_uninitialized_checked(vm, TINYPY_VALUE_STRING, format.byte_size, format.byte_size, &bytes, out_error);
    if (result == NULL) {
        TINYPY_DECREF(format_value);
        return NULL;
    }
    for (index = 0U; index < format.item_count; ++index) {
        tinypy_value_t *item = TINYPY_TUPLE_GET(args, index + 1U);
        if (__tinypy_struct_encode_double(vm, &format, item, bytes + index * 8U, out_error) == 0) {
            TINYPY_DECREF(result);
            TINYPY_DECREF(format_value);
            return NULL;
        }
    }
    TINYPY_DECREF(format_value);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_struct_offset(tinypy_vm_t *vm, int64_t signed_offset, size_t buffer_size, size_t required_size, const char *operation, size_t operation_size, size_t *out_offset, tinypy_error_t **out_error) {
    size_t offset;

    if (signed_offset < 0) {
        uint64_t distance = (uint64_t)(-(signed_offset + 1)) + UINT64_C(1);

        if (distance > buffer_size) {
            goto too_small;
        }
        offset = buffer_size - (size_t)distance;
    }
    else {
        if ((uint64_t)signed_offset > buffer_size) {
            goto too_small;
        }
        offset = (size_t)signed_offset;
    }
    if (required_size > buffer_size - offset) {
        goto too_small;
    }
    *out_offset = offset;
    return TINYPY_TRUE;

too_small: {
        char message[96];
        size_t position = 0U;
        static const char prefix[] = " requires a buffer of at least ";
        static const char suffix[] = " bytes";
        char digits[32];
        size_t digit_count = 0U;
        size_t remaining_size = required_size;

        (void)memcpy(message + position, operation, operation_size);
        position += operation_size;
        (void)memcpy(message + position, prefix, sizeof(prefix) - 1U);
        position += sizeof(prefix) - 1U;
        do {
            digits[digit_count++] = (char)('0' + remaining_size % 10U);
            remaining_size /= 10U;
        } while (remaining_size != 0U);
        while (digit_count != 0U) {
            message[position++] = digits[--digit_count];
        }
        (void)memcpy(message + position, suffix, sizeof(suffix));
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, message, out_error);
        return TINYPY_FALSE;
    }
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_struct_pack_offset(tinypy_value_t *value, int64_t *out_value, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);
    tinypy_bool_t result;

    if (kind == TINYPY_VALUE_LONG) {
        result = tinypy_internal_index_as_i64(value, out_value, TINYPY_FALSE, out_error);
        return result;
    }
    if (kind == TINYPY_VALUE_FLOAT) {
        tinypy_bool_t handled = TINYPY_FALSE;
        tinypy_value_t *converted = tinypy_internal_call_conversion(value, vm->internal_special_int_key, &handled, out_error);

        if (handled == 0 || converted == NULL) {
            return TINYPY_FALSE;
        }
        kind = TINYPY_VALUE_KIND(converted);
        if (kind != TINYPY_VALUE_INTEGER && kind != TINYPY_VALUE_LONG && kind != TINYPY_VALUE_BOOL) {
            TINYPY_DECREF(converted);
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "__int__ method should return an integer", out_error);
            return TINYPY_FALSE;
        }
        result = tinypy_internal_index_as_i64(converted, out_value, TINYPY_FALSE, out_error);
        TINYPY_DECREF(converted);
        return result;
    }
    result = tinypy_internal_integer_as_ssize(value, out_value, out_error);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_struct_pack_into(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_struct_state_t *state = (tinypy_struct_state_t *)user_data;
    tinypy_struct_format_t format;
    tinypy_struct_buffer_t buffer_view;
    size_t offset;
    int64_t signed_offset;
    tinypy_value_t *result = NULL;
    size_t index;

    if (__tinypy_struct_arguments(function, args, kwargs, 0U, SIZE_MAX, TINYPY_ARITY_STYLE_PARSED, out_error) == TINYPY_FALSE) {
        return NULL;
    }
    if (TINYPY_TUPLE_SIZE(args) == 0U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "missing format argument", out_error);
        return NULL;
    }
    tinypy_value_t *format_value = __tinypy_struct_call_format(function, state, args, &format, out_error);
    if (format_value == NULL) {
        return NULL;
    }
    if (TINYPY_TUPLE_SIZE(args) < 3U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, TINYPY_TUPLE_SIZE(args) == 1U ? "pack_into expected buffer argument" : "pack_into expected offset argument", out_error);
        TINYPY_DECREF(format_value);
        return NULL;
    }
    if (TINYPY_TUPLE_SIZE(args) != format.item_count + 3U) {
        __tinypy_struct_item_count_error(vm, "pack_into", 9U, format.item_count, TINYPY_TUPLE_SIZE(args) - 3U, out_error);
        TINYPY_DECREF(format_value);
        return NULL;
    }
    tinypy_value_t *buffer = TINYPY_TUPLE_GET(args, 1U);
    if (__tinypy_struct_buffer_acquire(vm, buffer, TINYPY_TRUE, &buffer_view, out_error) == 0) {
        TINYPY_DECREF(format_value);
        return NULL;
    }
    if (__tinypy_struct_pack_offset(TINYPY_TUPLE_GET(args, 2U), &signed_offset, out_error) == 0) {
        goto cleanup;
    }
    if (__tinypy_struct_offset(vm, signed_offset, buffer_view.size, format.byte_size, "pack_into", 9U, &offset, out_error) == 0) {
        goto cleanup;
    }
    if (format.byte_size != 0U) {
        (void)memset((uint8_t *)buffer_view.bytes + offset, 0, format.byte_size);
    }
    for (index = 0U; index < format.item_count; ++index) {
        tinypy_value_t *item = TINYPY_TUPLE_GET(args, index + 3U);
        if (__tinypy_struct_encode_double(vm, &format, item, (uint8_t *)buffer_view.bytes + offset + index * 8U, out_error) == 0) {
            goto cleanup;
        }
    }
    result = TINYPY_RET_NONE(vm);

cleanup:
    __tinypy_struct_buffer_release(&buffer_view);
    TINYPY_DECREF(format_value);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_struct_duplicate_keyword(tinypy_vm_t *vm, tinypy_value_t *name, size_t position, tinypy_error_t **out_error) {
    char position_buffer[TINYPY_MESSAGE_SIZE_BUFFER];
    size_t position_size = tinypy_internal_format_size(position_buffer, position);
    tinypy_message_part_t parts[] = {
        TINYPY_MESSAGE_PART_LITERAL("Argument given by name ('"),
        TINYPY_MESSAGE_PART_TEXT(name),
        TINYPY_MESSAGE_PART_LITERAL("') and position ("),
        {position_buffer, position_size},
        TINYPY_MESSAGE_PART_LITERAL(")")
    };

    tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, 5U, out_error);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_struct_known_keyword(tinypy_value_t *key, tinypy_value_t *name) {
    const uint8_t *bytes = TINYPY_TEXT_BYTES(key);
    size_t size = TINYPY_TEXT_BYTE_SIZE(key);
    const uint8_t *terminator = (const uint8_t *)memchr(bytes, 0, size);

    if (terminator != NULL) {
        size = (size_t)(terminator - bytes);
    }
    tinypy_bool_t known = size == TINYPY_TEXT_BYTE_SIZE(name) && memcmp(bytes, TINYPY_TEXT_BYTES(name), size) == 0 ? TINYPY_TRUE : TINYPY_FALSE;

    return known;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_struct_unpack_from(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_struct_state_t *state = (tinypy_struct_state_t *)user_data;
    tinypy_struct_format_t format;
    tinypy_struct_buffer_t buffer_view;
    tinypy_value_t *buffer = TINYPY_TUPLE_SIZE(args) >= 2U ? TINYPY_TUPLE_GET(args, 1U) : NULL;
    tinypy_value_t *offset_value = TINYPY_TUPLE_SIZE(args) >= 3U ? TINYPY_TUPLE_GET(args, 2U) : NULL;
    tinypy_value_t *result = NULL;
    size_t offset = 0U;
    int64_t signed_offset = INT64_C(0);

    if (TINYPY_TUPLE_SIZE(args) == 0U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "missing format argument", out_error);
        return NULL;
    }
    tinypy_value_t *format_value = __tinypy_struct_call_format(function, state, args, &format, out_error);
    if (format_value == NULL) {
        return NULL;
    }
    size_t count = TINYPY_TUPLE_SIZE(args) - 1U;
    size_t keyword_count = kwargs != NULL ? TINYPY_DICT_SIZE(kwargs) : 0U;
    size_t recognized = 0U;
    if (count > 2U || keyword_count > 2U - count) {
        tinypy_internal_make_arity_error(vm, "unpack_from", 11U, count + keyword_count, 0U, 2U, TINYPY_ARITY_STYLE_PARSED, out_error);
        TINYPY_DECREF(format_value);
        return NULL;
    }
    tinypy_value_t *keyword = keyword_count != 0U ? tinypy_internal_constructor_keyword_optional(kwargs, vm->internal_buffer_key) : NULL;
    if (keyword != NULL) {
        recognized += 1U;
        if (buffer != NULL) {
            __tinypy_struct_duplicate_keyword(vm, vm->internal_buffer_key, 1U, out_error);
            TINYPY_DECREF(format_value);
            return NULL;
        }
        buffer = keyword;
    }
    if (buffer == NULL) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "Required argument 'buffer' (pos 1) not found", out_error);
        TINYPY_DECREF(format_value);
        return NULL;
    }
    tinypy_error_t *buffer_error = NULL;
    if (__tinypy_struct_buffer_acquire(vm, buffer, TINYPY_FALSE, &buffer_view, &buffer_error) == TINYPY_FALSE) {
        if (buffer_error != NULL && tinypy_error_kind(buffer_error) == TINYPY_ERROR_TYPE) {
            tinypy_error_release(buffer_error);
            tinypy_vm_clear_error(vm);
            tinypy_message_part_t parts[] = {
                TINYPY_MESSAGE_PART_LITERAL("unpack_from() argument 1 must be string or buffer, not "),
                TINYPY_MESSAGE_PART_TYPE_NAME(buffer)
            };

            tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, 2U, out_error);
        }
        else if (out_error != NULL) {
            *out_error = buffer_error;
        }
        else if (buffer_error != NULL) {
            tinypy_error_release(buffer_error);
        }
        __tinypy_struct_buffer_release(&buffer_view);
        TINYPY_DECREF(format_value);
        return NULL;
    }
    if (recognized < keyword_count) {
        keyword = tinypy_internal_constructor_keyword_optional(kwargs, vm->internal_offset_key);
        if (keyword != NULL) {
            recognized += 1U;
            if (offset_value != NULL) {
                __tinypy_struct_duplicate_keyword(vm, vm->internal_offset_key, 2U, out_error);
                goto cleanup;
            }
            offset_value = keyword;
        }
    }
    if (offset_value != NULL && tinypy_internal_integer_as_ssize(offset_value, &signed_offset, out_error) == 0) {
        goto cleanup;
    }
    if (recognized != keyword_count) {
        tinypy_dict_entry_t *iterator = TINYPY_DICT_ITERATOR_BEGIN(kwargs);
        tinypy_dict_entry_t *end = TINYPY_DICT_ITERATOR_END(kwargs);

        for (; iterator != end; ++iterator) {
            if (TINYPY_DICT_ENTRY_IS_ACTIVE(iterator) != 0 && TINYPY_VALUE_KIND(iterator->key) != TINYPY_VALUE_STRING) {
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "keywords must be strings", out_error);
                goto cleanup;
            }
            if (TINYPY_DICT_ENTRY_IS_ACTIVE(iterator) != 0 && __tinypy_struct_known_keyword(iterator->key, vm->internal_buffer_key) == TINYPY_FALSE && __tinypy_struct_known_keyword(iterator->key, vm->internal_offset_key) == TINYPY_FALSE) {
                const uint8_t *bytes = TINYPY_TEXT_BYTES(iterator->key);
                size_t size = TINYPY_TEXT_BYTE_SIZE(iterator->key);
                const uint8_t *terminator = (const uint8_t *)memchr(bytes, 0, size);
                if (terminator != NULL) {
                    size = (size_t)(terminator - bytes);
                }
                tinypy_message_part_t parts[] = {
                    TINYPY_MESSAGE_PART_LITERAL("'"),
                    {(const char *)bytes, size},
                    TINYPY_MESSAGE_PART_LITERAL("' is an invalid keyword argument for this function")
                };

                tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, 3U, out_error);
                goto cleanup;
            }
        }
    }
    if (TINYPY_VALUE_KIND(buffer) == TINYPY_VALUE_NONE) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "unpack_from requires a buffer argument", out_error);
        goto cleanup;
    }
    if (TINYPY_VALUE_KIND(buffer) == TINYPY_VALUE_BUFFER) {
        size_t current_size;

        /* Legacy buffers do not export their bytearray owner. Refresh its
         * address after callbacks while retaining the acquired view bound. */
        (void)tinypy_internal_bytes_view(buffer, &buffer_view.bytes, &current_size);
        if (current_size < buffer_view.size) {
            buffer_view.size = current_size;
        }
    }
    if (__tinypy_struct_offset(vm, signed_offset, buffer_view.size, format.byte_size, "unpack_from", 11U, &offset, out_error) == 0) {
        goto cleanup;
    }
    const uint8_t *selected_bytes = format.byte_size != 0U ? buffer_view.bytes + offset : NULL;
    result = __tinypy_struct_unpack_values(vm, &format, selected_bytes, out_error);

cleanup:
    __tinypy_struct_buffer_release(&buffer_view);
    TINYPY_DECREF(format_value);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_struct_object_init(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_struct_format_t format;
    tinypy_value_t *self = TINYPY_TUPLE_GET(args, 0U);
    tinypy_struct_payload_t *payload = (tinypy_struct_payload_t *)tinypy_native_instance_payload(self);
    size_t count = TINYPY_TUPLE_SIZE(args) - 1U;
    size_t keyword_count = kwargs != NULL ? TINYPY_DICT_SIZE(kwargs) : 0U;

    (void)user_data;
    if (count > 1U || keyword_count > 1U - count) {
        tinypy_internal_make_arity_error(vm, "Struct", 6U, count + keyword_count, 0U, 1U, TINYPY_ARITY_STYLE_PARSED, out_error);
        return NULL;
    }
    tinypy_value_t *format_value = count != 0U ? TINYPY_TUPLE_GET(args, 1U) : (keyword_count != 0U ? tinypy_internal_constructor_keyword_optional(kwargs, vm->internal_format_key) : NULL);
    if (format_value == NULL) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "Required argument 'format' (pos 1) not found", out_error);
        return NULL;
    }
    tinypy_value_t *normalized = __tinypy_struct_format_value(vm, format_value, out_error);
    if (normalized == NULL) {
        return NULL;
    }
    tinypy_value_t *previous = payload->format_value;
    payload->format_value = normalized;
    if (previous != NULL) {
        TINYPY_DECREF(previous);
    }
    if (__tinypy_struct_parse_format_uncached(vm, payload->format_value, &format, out_error) == TINYPY_FALSE) {
        return NULL;
    }
    payload->format = format;
    payload->initialized = TINYPY_TRUE;
    tinypy_value_t *result = TINYPY_RET_NONE(vm);
    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_struct_get_field(tinypy_value_t *instance, tinypy_value_t *name, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(instance);
    tinypy_struct_payload_t *payload = (tinypy_struct_payload_t *)tinypy_native_instance_payload(instance);

    TINYPY_CLEAR_ERROR(out_error);
    if (TINYPY_NAME_EQ(name, vm->internal_format_key) != TINYPY_FALSE) {
        tinypy_value_t *result = payload->format_value != NULL ? TINYPY_RET(payload->format_value) : TINYPY_RET_NONE(vm);

        return result;
    }
    tinypy_value_t *result = tinypy_integer_from_i64(vm, payload->initialized != TINYPY_FALSE ? (int64_t)payload->format.byte_size : INT64_C(-1));

    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_struct_object_new(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_type_t *base_type = (tinypy_type_t *)user_data;

    (void)kwargs;
    if (TINYPY_TUPLE_SIZE(args) == 0U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "Struct.__new__(): not enough arguments", out_error);
        return NULL;
    }
    tinypy_value_t *type_value = TINYPY_TUPLE_GET(args, 0U);
    if (TINYPY_VALUE_KIND(type_value) != TINYPY_VALUE_TYPE) {
        tinypy_message_part_t parts[] = {
            TINYPY_MESSAGE_PART_LITERAL("Struct.__new__(X): X is not a type object ("),
            TINYPY_MESSAGE_PART_TYPE_NAME(type_value),
            TINYPY_MESSAGE_PART_LITERAL(")")
        };

        tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, 3U, out_error);
        return NULL;
    }
    tinypy_type_t *type = (tinypy_type_t *)type_value;
    if (tinypy_type_is_subtype(type, base_type) == TINYPY_FALSE) {
        tinypy_message_part_t parts[] = {
            TINYPY_MESSAGE_PART_LITERAL("Struct.__new__("),
            {type->name, type->name_size},
            TINYPY_MESSAGE_PART_LITERAL("): "),
            {type->name, type->name_size},
            TINYPY_MESSAGE_PART_LITERAL(" is not a subtype of Struct")
        };

        tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, 5U, out_error);
        return NULL;
    }
    tinypy_value_t *result = tinypy_internal_object_allocate_checked(vm, type, type->basic_size, out_error);

    return result;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_struct_object_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data) {
    tinypy_struct_payload_t *payload = (tinypy_struct_payload_t *)tinypy_native_instance_payload(value);
    tinypy_value_t *format_value = payload->format_value;

    payload->format_value = NULL;
    tinypy_internal_instance_release_references(value, visit, user_data);
    if (format_value != NULL) {
        visit(format_value, user_data);
    }
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_struct_object_traverse_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data) {
    tinypy_struct_payload_t *payload = (tinypy_struct_payload_t *)tinypy_native_instance_payload(value);

    tinypy_internal_instance_release_references(value, visit, user_data);
    if (payload->format_value != NULL) {
        visit(payload->format_value, user_data);
    }
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_struct_sizeof(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    (void)user_data;
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 0U, 0U, TINYPY_ARITY_STYLE_PARSED, out_error) == TINYPY_FALSE) {
        return NULL;
    }
    tinypy_value_t *self = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *result = tinypy_long_from_i64(TINYPY_VALUE_VM(function), (int64_t)self->type->basic_size);

    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_struct_clearcache(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_struct_state_t *state = (tinypy_struct_state_t *)user_data;

    if (__tinypy_struct_arguments(function, args, kwargs, 0U, 0U, TINYPY_ARITY_STYLE_PARSED, out_error) == TINYPY_FALSE) {
        return NULL;
    }
    __tinypy_struct_cache_clear(state);
    tinypy_value_t *return_value_1 = TINYPY_RET_NONE(vm);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_struct_add_function(tinypy_value_t *module, tinypy_struct_state_t *state, tinypy_value_t *name, tinypy_native_function_callback_t callback) {
    state->reference_count += 1U;
    tinypy_internal_module_add_function(module, name, callback, state, __tinypy_struct_state_finalize);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_struct_add_type_method(tinypy_type_t *type, tinypy_struct_state_t *state, tinypy_value_t *name, tinypy_native_function_callback_t callback) {
    state->reference_count += 1U;
    tinypy_internal_type_add_method(type, name, callback, state, __tinypy_struct_state_finalize, TINYPY_NATIVE_DESCRIPTOR_AUTO);
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_initialize_struct_module(tinypy_vm_t *vm) {
    tinypy_struct_state_t *state = (tinypy_struct_state_t *)tinypy_internal_vm_allocate(vm, sizeof(*state));
    tinypy_value_t *module = tinypy_module_new_key(vm->internal_struct_module_name);
    tinypy_value_t *name = TINYPY_RET(vm->internal_struct_module_name);
    tinypy_value_t *doc = tinypy_string_from_bytes(vm, "Functions to convert between Python values and C structs.", 57U);
    tinypy_value_t *version = tinypy_string_from_bytes(vm, "0.2", 3U);

    (void)memset(state, 0, sizeof(*state));
    state->vm = vm;
    tinypy_module_add_value_key(module, vm->internal_special_name_key, name);
    tinypy_module_add_value_key(module, vm->internal_special_doc_key, doc);
    tinypy_module_add_value_key(module, vm->internal_special_version_key, version);
    tinypy_module_add_value_key(module, vm->internal_error_key, &vm->exception_types[TINYPY_EXCEPTION_VALUE_ERROR]->base.base);
    tinypy_module_add_value_key(module, vm->internal_py_struct_float_coerce_key, &vm->true_object.base);
    tinypy_module_add_value_key(module, vm->internal_py_struct_range_checking_key, &vm->true_object.base);
    __tinypy_struct_add_function(module, state, vm->internal_calcsize_key, __tinypy_struct_calcsize);
    __tinypy_struct_add_function(module, state, vm->internal_pack_key, __tinypy_struct_pack);
    __tinypy_struct_add_function(module, state, vm->internal_unpack_key, __tinypy_struct_unpack);
    __tinypy_struct_add_function(module, state, vm->internal_pack_into_key, __tinypy_struct_pack_into);
    __tinypy_struct_add_function(module, state, vm->internal_unpack_from_key, __tinypy_struct_unpack_from);
    __tinypy_struct_add_function(module, state, vm->internal_clearcache_key, __tinypy_struct_clearcache);
    tinypy_native_type_spec_t spec;
    tinypy_native_type_spec_init(&spec);
    spec.payload_size = sizeof(tinypy_struct_payload_t);
    spec.has_instance_dict = TINYPY_FALSE;
    spec.has_weakrefs = TINYPY_TRUE;
    tinypy_type_t *struct_type = tinypy_native_type_new_key(vm->internal_struct_key, NULL, 0U, NULL, &spec, NULL);
    tinypy_value_t *type_doc = tinypy_string_from_bytes(vm, "Compiled struct object", 22U);

    struct_type->flags |= TINYPY_TYPE_FLAG_IMMUTABLE;
    struct_type->release_references = __tinypy_struct_object_release_references;
    struct_type->traverse_references = __tinypy_struct_object_traverse_references;
    tinypy_dict_delete(struct_type->dict, vm->internal_special_weakref_key);
    tinypy_type_set_attr_key(struct_type, vm->internal_special_doc_key, type_doc);
    TINYPY_DECREF(type_doc);
    tinypy_internal_type_add_static_method(struct_type, vm->internal_special_new_key, __tinypy_struct_object_new, struct_type, NULL);
    tinypy_internal_initialize_struct_descriptors(struct_type);
    tinypy_internal_type_add_object_attribute_methods(struct_type);
    __tinypy_struct_add_type_method(struct_type, state, vm->internal_special_init_key, __tinypy_struct_object_init);
    __tinypy_struct_add_type_method(struct_type, state, vm->internal_pack_key, __tinypy_struct_pack);
    __tinypy_struct_add_type_method(struct_type, state, vm->internal_unpack_key, __tinypy_struct_unpack);
    __tinypy_struct_add_type_method(struct_type, state, vm->internal_pack_into_key, __tinypy_struct_pack_into);
    __tinypy_struct_add_type_method(struct_type, state, vm->internal_unpack_from_key, __tinypy_struct_unpack_from);
    __tinypy_struct_add_type_method(struct_type, state, vm->internal_special_sizeof_key, __tinypy_struct_sizeof);
    tinypy_module_add_value_key(module, vm->internal_struct_key, &struct_type->base.base);
    TINYPY_DECREF(&struct_type->base.base);
    TINYPY_DECREF(version);
    TINYPY_DECREF(doc);
    TINYPY_DECREF(name);
    tinypy_internal_register_module(vm, vm->internal_struct_module_name, module);
    TINYPY_DECREF(module);
}
