#include "internal.h"

#include <string.h>

typedef enum tinypy_struct_byte_order_e {
    TINYPY_STRUCT_NATIVE_ENDIAN,
    TINYPY_STRUCT_LITTLE_ENDIAN,
    TINYPY_STRUCT_BIG_ENDIAN
} tinypy_struct_byte_order_e;

typedef struct tinypy_struct_format_t {
    tinypy_struct_byte_order_e byte_order;
    size_t item_count;
    size_t byte_size;
} tinypy_struct_format_t;

#define TINYPY_STRUCT_CACHE_SIZE 32U

typedef struct tinypy_struct_cache_entry_t {
    tinypy_value_t *key;
    tinypy_struct_format_t format;
    uint64_t age;
} tinypy_struct_cache_entry_t;

typedef struct tinypy_struct_state_t {
    tinypy_vm_t *vm;
    size_t reference_count;
    uint64_t clock;
    tinypy_struct_cache_entry_t entries[TINYPY_STRUCT_CACHE_SIZE];
} tinypy_struct_state_t;

//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_struct_arguments(tinypy_vm_t *vm, tinypy_value_t *args, tinypy_value_t *kwargs, size_t minimum, tinypy_error_t **out_error) {
    if ((kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U) || TINYPY_TUPLE_SIZE(args) < minimum) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "struct function received invalid arguments", out_error);
        return TINYPY_FALSE;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_struct_format_view(tinypy_vm_t *vm, tinypy_value_t *value, const uint8_t **out_bytes, size_t *out_size, tinypy_error_t **out_error) {
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);

    if (kind != TINYPY_VALUE_STRING && kind != TINYPY_VALUE_UNICODE) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "struct format must be a string", out_error);
        return TINYPY_FALSE;
    }
    *out_bytes = TINYPY_TEXT_BYTES(value);
    *out_size = TINYPY_TEXT_BYTE_SIZE(value);
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_struct_parse_format_uncached(tinypy_vm_t *vm, tinypy_value_t *value, tinypy_struct_format_t *out_format, tinypy_error_t **out_error) {
    const uint8_t *bytes;
    size_t size;
    size_t index = 0U;
    size_t count = 0U;
    size_t size_limit = SIZE_MAX;
    int32_t have_count = INT32_C(0);

    if (__tinypy_struct_format_view(vm, value, &bytes, &size, out_error) == 0) {
        return TINYPY_FALSE;
    }
    out_format->byte_order = TINYPY_STRUCT_NATIVE_ENDIAN;
    out_format->item_count = 0U;
    out_format->byte_size = 0U;
#if SIZE_MAX > INT64_MAX
    size_limit = (size_t)INT64_MAX;
#endif
    if (index < size) {
        uint8_t prefix = bytes[index];

        if (prefix == (uint8_t)'<' || prefix == (uint8_t)'>' || prefix == (uint8_t)'!' || prefix == (uint8_t)'=' || prefix == (uint8_t)'@') {
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
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "unsupported struct format character", out_error);
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
    size_t index;

    for (index = 0U; index < TINYPY_STRUCT_CACHE_SIZE; ++index) {
        tinypy_struct_cache_entry_t *entry = &state->entries[index];

        if (entry->key != NULL) {
            TINYPY_DECREF(entry->key);
            entry->key = NULL;
        }
        entry->age = UINT64_C(0);
    }
    state->clock = UINT64_C(0);
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
static tinypy_bool_t __tinypy_struct_parse_format(tinypy_struct_state_t *state, tinypy_value_t *value, tinypy_struct_format_t *out_format, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = state->vm;
    const uint8_t *bytes;
    size_t size;
    size_t index;
    size_t replacement = 0U;
    uint64_t oldest_age = UINT64_MAX;

    if (__tinypy_struct_format_view(vm, value, &bytes, &size, out_error) == 0) {
        return TINYPY_FALSE;
    }
    for (index = 0U; index < TINYPY_STRUCT_CACHE_SIZE; ++index) {
        tinypy_struct_cache_entry_t *entry = &state->entries[index];

        if (entry->key != NULL && (entry->key == value || (TINYPY_TEXT_BYTE_SIZE(entry->key) == size && memcmp(TINYPY_TEXT_BYTES(entry->key), bytes, size) == 0))) {
            if (state->clock == UINT64_MAX) {
                __tinypy_struct_cache_clear(state);
                break;
            }
            state->clock += UINT64_C(1);
            entry->age = state->clock;
            *out_format = entry->format;
            return TINYPY_TRUE;
        }
        if (entry->key == NULL) {
            replacement = index;
            oldest_age = UINT64_C(0);
        }
        else if (oldest_age != UINT64_C(0) && entry->age < oldest_age) {
            replacement = index;
            oldest_age = entry->age;
        }
    }
    if (__tinypy_struct_parse_format_uncached(vm, value, out_format, out_error) == 0) {
        return TINYPY_FALSE;
    }
    if (state->clock == UINT64_MAX) {
        __tinypy_struct_cache_clear(state);
        replacement = 0U;
    }
    tinypy_struct_cache_entry_t *entry = &state->entries[replacement];
    if (entry->key != NULL) {
        TINYPY_DECREF(entry->key);
    }
    TINYPY_INCREF(value);
    state->clock += UINT64_C(1);
    entry->key = value;
    entry->format = *out_format;
    entry->age = state->clock;
    return TINYPY_TRUE;
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
    if (kind == TINYPY_VALUE_BOOL || kind == TINYPY_VALUE_INTEGER) {
        *out_value = (double)TINYPY_INTEGER_VALUE(value);
        return TINYPY_TRUE;
    }
    if (kind == TINYPY_VALUE_LONG && TINYPY_LONG_DIGIT_COUNT(value) <= 4U) {
        *out_value = (double)tinypy_long_as_i64(value);
        return TINYPY_TRUE;
    }
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "required argument is not a float", out_error);
    return TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_struct_calcsize(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_struct_state_t *state = (tinypy_struct_state_t *)user_data;
    tinypy_struct_format_t format;

    if (__tinypy_struct_arguments(vm, args, kwargs, 1U, out_error) == 0 || TINYPY_TUPLE_SIZE(args) != 1U) {
        return NULL;
    }
    tinypy_value_t *item = TINYPY_TUPLE_GET(args, 0U);
    if (__tinypy_struct_parse_format(state, item, &format, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *return_value_1 = tinypy_integer_from_i64(vm, (int64_t)format.byte_size);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_struct_unpack(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_struct_state_t *state = (tinypy_struct_state_t *)user_data;
    tinypy_struct_format_t format;
    const uint8_t *bytes;
    size_t byte_size;
    size_t index;

    if (__tinypy_struct_arguments(vm, args, kwargs, 2U, out_error) == 0 || TINYPY_TUPLE_SIZE(args) != 2U) {
        return NULL;
    }
    tinypy_value_t *item_2 = TINYPY_TUPLE_GET(args, 0U);
    if (__tinypy_struct_parse_format(state, item_2, &format, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *item = TINYPY_TUPLE_GET(args, 1U);
    if (tinypy_internal_bytes_view(item, &bytes, &byte_size) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "unpack requires a string argument", out_error);
        return NULL;
    }
    if (byte_size != format.byte_size) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "unpack requires a string argument of the exact format size", out_error);
        return NULL;
    }
    if (format.item_count == 0U) {
        tinypy_value_t *return_value_1 = tinypy_tuple_from_items(vm, NULL, 0U);
        return return_value_1;
    }
    tinypy_value_t **items = (tinypy_value_t **)tinypy_internal_vm_allocate(vm, format.item_count * sizeof(*items));
    for (index = 0U; index < format.item_count; ++index) {
        uint64_t bits = __tinypy_struct_read_u64(bytes + index * 8U, format.byte_order);
        double value;

        (void)memcpy(&value, &bits, sizeof(value));
        items[index] = tinypy_float_from_double(vm, value);
    }
    tinypy_value_t *result = tinypy_tuple_from_items(vm, items, format.item_count);
    for (index = 0U; index < format.item_count; ++index) {
        TINYPY_DECREF(items[index]);
    }
    tinypy_internal_vm_deallocate(vm, items, format.item_count * sizeof(*items));
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_struct_pack(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_struct_state_t *state = (tinypy_struct_state_t *)user_data;
    tinypy_struct_format_t format;
    uint8_t *bytes;
    size_t index;

    if (__tinypy_struct_arguments(vm, args, kwargs, 1U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *item_2 = TINYPY_TUPLE_GET(args, 0U);
    if (__tinypy_struct_parse_format(state, item_2, &format, out_error) == 0) {
        return NULL;
    }
    if (TINYPY_TUPLE_SIZE(args) != format.item_count + 1U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "pack expected a different number of items", out_error);
        return NULL;
    }
    if (format.byte_size == 0U) {
        tinypy_value_t *return_value_1 = tinypy_string_from_bytes(vm, NULL, 0U);
        return return_value_1;
    }
    bytes = (uint8_t *)tinypy_internal_vm_allocate(vm, format.byte_size);
    for (index = 0U; index < format.item_count; ++index) {
        double value;
        uint64_t bits;

        tinypy_value_t *item = TINYPY_TUPLE_GET(args, index + 1U);
        if (__tinypy_struct_as_double(vm, item, &value, out_error) == 0) {
            tinypy_internal_vm_deallocate(vm, bytes, format.byte_size);
            return NULL;
        }
        (void)memcpy(&bits, &value, sizeof(bits));
        __tinypy_struct_write_u64(bytes + index * 8U, bits, format.byte_order);
    }
    tinypy_value_t *result = tinypy_string_from_bytes(vm, bytes, format.byte_size);
    tinypy_internal_vm_deallocate(vm, bytes, format.byte_size);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_struct_offset(tinypy_vm_t *vm, tinypy_value_t *value, size_t buffer_size, size_t required_size, const char *operation, size_t operation_size, size_t *out_offset, tinypy_error_t **out_error) {
    int64_t signed_offset;
    size_t offset;

    if (tinypy_internal_index_as_i64(value, &signed_offset, TINYPY_FALSE, out_error) == 0) {
        return TINYPY_FALSE;
    }
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
static tinypy_value_t *__tinypy_struct_pack_into(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_struct_state_t *state = (tinypy_struct_state_t *)user_data;
    tinypy_struct_format_t format;
    const uint8_t *buffer_bytes;
    size_t buffer_size;
    size_t offset;
    tinypy_value_t **pack_items;
    tinypy_value_t *pack_args;
    tinypy_value_t *packed;
    tinypy_value_t *result;
    size_t index;

    if (__tinypy_struct_arguments(vm, args, kwargs, 3U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *format_value = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *buffer = TINYPY_TUPLE_GET(args, 1U);
    if (__tinypy_struct_parse_format(state, format_value, &format, out_error) == 0) {
        return NULL;
    }
    if (TINYPY_TUPLE_SIZE(args) != format.item_count + 3U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "pack expected a different number of items", out_error);
        return NULL;
    }
    if (tinypy_internal_bytes_view(buffer, &buffer_bytes, &buffer_size) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "argument must be read-write buffer", out_error);
        return NULL;
    }
    (void)buffer_bytes;
    if (__tinypy_struct_offset(vm, TINYPY_TUPLE_GET(args, 2U), buffer_size, format.byte_size, "pack_into", 9U, &offset, out_error) == 0) {
        return NULL;
    }
    pack_items = (tinypy_value_t **)tinypy_internal_vm_allocate(vm, (format.item_count + 1U) * sizeof(*pack_items));
    pack_items[0] = format_value;
    for (index = 0U; index != format.item_count; ++index) {
        pack_items[index + 1U] = TINYPY_TUPLE_GET(args, index + 3U);
    }
    pack_args = tinypy_tuple_from_items(vm, pack_items, format.item_count + 1U);
    tinypy_internal_vm_deallocate(vm, pack_items, (format.item_count + 1U) * sizeof(*pack_items));
    packed = __tinypy_struct_pack(function, pack_args, NULL, state, out_error);
    TINYPY_DECREF(pack_args);
    if (packed == NULL) {
        return NULL;
    }
    tinypy_value_t *start = tinypy_integer_from_i64(vm, (int64_t)offset);
    tinypy_value_t *stop = tinypy_integer_from_i64(vm, (int64_t)(offset + format.byte_size));
    tinypy_value_t *slice = tinypy_slice_new(vm, start, stop, NULL);
    tinypy_bool_t assigned = tinypy_set_item(buffer, slice, packed, out_error);

    TINYPY_DECREF(slice);
    TINYPY_DECREF(stop);
    TINYPY_DECREF(start);
    TINYPY_DECREF(packed);
    if (assigned == 0) {
        return NULL;
    }
    result = tinypy_none_get(vm);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_struct_unpack_from(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_struct_state_t *state = (tinypy_struct_state_t *)user_data;
    tinypy_struct_format_t format;
    const uint8_t *buffer_bytes;
    size_t buffer_size;
    size_t offset = 0U;

    if (__tinypy_struct_arguments(vm, args, kwargs, 2U, out_error) == 0 || TINYPY_TUPLE_SIZE(args) > 3U) {
        return NULL;
    }
    tinypy_value_t *format_value = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *buffer = TINYPY_TUPLE_GET(args, 1U);
    if (__tinypy_struct_parse_format(state, format_value, &format, out_error) == 0) {
        return NULL;
    }
    if (tinypy_internal_bytes_view(buffer, &buffer_bytes, &buffer_size) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "unpack_from requires a string argument", out_error);
        return NULL;
    }
    if (TINYPY_TUPLE_SIZE(args) == 3U) {
        if (__tinypy_struct_offset(vm, TINYPY_TUPLE_GET(args, 2U), buffer_size, format.byte_size, "unpack_from", 11U, &offset, out_error) == 0) {
            return NULL;
        }
    }
    else if (format.byte_size > buffer_size) {
        tinypy_value_t *zero = tinypy_integer_from_i64(vm, INT64_C(0));
        tinypy_bool_t valid = __tinypy_struct_offset(vm, zero, buffer_size, format.byte_size, "unpack_from", 11U, &offset, out_error);

        TINYPY_DECREF(zero);
        if (valid == 0) {
            return NULL;
        }
    }
    const uint8_t *selected_bytes = format.byte_size != 0U ? buffer_bytes + offset : NULL;
    tinypy_value_t *selected = tinypy_string_from_bytes(vm, selected_bytes, format.byte_size);
    tinypy_value_t *unpack_items[2] = {format_value, selected};
    tinypy_value_t *unpack_args = tinypy_tuple_from_items(vm, unpack_items, 2U);
    tinypy_value_t *result = __tinypy_struct_unpack(function, unpack_args, NULL, state, out_error);

    TINYPY_DECREF(unpack_args);
    TINYPY_DECREF(selected);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_struct_object_init(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_struct_state_t *state = (tinypy_struct_state_t *)user_data;
    tinypy_struct_format_t format;
    tinypy_value_t *result;

    if (__tinypy_struct_arguments(vm, args, kwargs, 2U, out_error) == 0 || TINYPY_TUPLE_SIZE(args) != 2U) {
        return NULL;
    }
    tinypy_value_t *self = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *format_value = TINYPY_TUPLE_GET(args, 1U);
    if (__tinypy_struct_parse_format(state, format_value, &format, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *size_value = tinypy_integer_from_i64(vm, (int64_t)format.byte_size);

    tinypy_instance_set_attr(self, "_format", 7U, format_value);
    tinypy_instance_set_attr(self, "_size", 5U, size_value);
    TINYPY_DECREF(size_value);
    result = tinypy_none_get(vm);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_struct_object_property(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *function_name = tinypy_native_function_name(function);
    tinypy_bool_t format = TINYPY_TEXT_BYTE_SIZE(function_name) == 6U ? TINYPY_TRUE : TINYPY_FALSE;
    const char *name = format != 0 ? "_format" : "_size";
    size_t name_size = format != 0 ? 7U : 5U;
    tinypy_value_t *result;

    (void)user_data;
    if (__tinypy_struct_arguments(vm, args, kwargs, 1U, out_error) == 0 || TINYPY_TUPLE_SIZE(args) != 1U) {
        return NULL;
    }
    result = tinypy_object_get_attr(TINYPY_TUPLE_GET(args, 0U), name, name_size, out_error);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_struct_object_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_struct_state_t *state = (tinypy_struct_state_t *)user_data;
    tinypy_value_t *function_name = tinypy_native_function_name(function);
    const uint8_t *name = TINYPY_TEXT_BYTES(function_name);
    size_t name_size = TINYPY_TEXT_BYTE_SIZE(function_name);
    tinypy_value_t *format_value;
    tinypy_value_t **items;
    tinypy_value_t *call_args;
    tinypy_value_t *result;
    size_t item_count;
    size_t index;

    if (__tinypy_struct_arguments(vm, args, kwargs, 1U, out_error) == 0) {
        return NULL;
    }
    format_value = tinypy_object_get_attr(TINYPY_TUPLE_GET(args, 0U), "_format", 7U, out_error);
    if (format_value == NULL) {
        return NULL;
    }
    item_count = TINYPY_TUPLE_SIZE(args);
    items = (tinypy_value_t **)tinypy_internal_vm_allocate(vm, item_count * sizeof(*items));
    items[0] = format_value;
    for (index = 1U; index != item_count; ++index) {
        items[index] = TINYPY_TUPLE_GET(args, index);
    }
    call_args = tinypy_tuple_from_items(vm, items, item_count);
    tinypy_internal_vm_deallocate(vm, items, item_count * sizeof(*items));
    TINYPY_DECREF(format_value);
    if (name_size == 4U && memcmp(name, "pack", 4U) == 0) {
        result = __tinypy_struct_pack(function, call_args, NULL, state, out_error);
    }
    else if (name_size == 6U && memcmp(name, "unpack", 6U) == 0) {
        result = __tinypy_struct_unpack(function, call_args, NULL, state, out_error);
    }
    else if (name_size == 9U && memcmp(name, "pack_into", 9U) == 0) {
        result = __tinypy_struct_pack_into(function, call_args, NULL, state, out_error);
    }
    else {
        result = __tinypy_struct_unpack_from(function, call_args, NULL, state, out_error);
    }
    TINYPY_DECREF(call_args);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_struct_clearcache(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_struct_state_t *state = (tinypy_struct_state_t *)user_data;

    if (__tinypy_struct_arguments(vm, args, kwargs, 0U, out_error) == 0 || TINYPY_TUPLE_SIZE(args) != 0U) {
        return NULL;
    }
    __tinypy_struct_cache_clear(state);
    tinypy_value_t *return_value_1 = tinypy_none_get(vm);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_struct_add_function(tinypy_vm_t *vm, tinypy_value_t *module, tinypy_struct_state_t *state, const char *name, size_t name_size, tinypy_native_function_callback_t callback) {
    tinypy_value_t *function;

    state->reference_count += 1U;
    function = tinypy_native_function_new(vm, name, name_size, callback, state, __tinypy_struct_state_finalize);

    tinypy_module_add_value(module, name, name_size, function);
    TINYPY_DECREF(function);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_struct_add_type_method(tinypy_type_t *type, tinypy_struct_state_t *state, const char *name, size_t name_size, tinypy_native_function_callback_t callback) {
    tinypy_value_t *function;

    state->reference_count += 1U;
    function = tinypy_native_function_new(type->vm, name, name_size, callback, state, __tinypy_struct_state_finalize);
    tinypy_type_set_attr(type, name, name_size, function);
    TINYPY_DECREF(function);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_struct_add_type_property(tinypy_type_t *type, tinypy_struct_state_t *state, const char *name, size_t name_size) {
    tinypy_value_t *getter;
    tinypy_value_t *descriptor;

    state->reference_count += 1U;
    getter = tinypy_native_function_new(type->vm, name, name_size, __tinypy_struct_object_property, state, __tinypy_struct_state_finalize);
    descriptor = tinypy_property_new(type->vm, getter, NULL, NULL, NULL);
    tinypy_type_set_attr(type, name, name_size, descriptor);
    TINYPY_DECREF(descriptor);
    TINYPY_DECREF(getter);
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_initialize_struct_module(tinypy_vm_t *vm) {
    tinypy_struct_state_t *state = (tinypy_struct_state_t *)tinypy_internal_vm_allocate(vm, sizeof(*state));
    tinypy_value_t *module = tinypy_module_new(vm, "_struct", 7U);
    tinypy_value_t *name = tinypy_string_from_bytes(vm, "_struct", 7U);
    tinypy_value_t *doc = tinypy_string_from_bytes(vm, "Functions to convert between Python values and C structs.", 57U);
    tinypy_value_t *version = tinypy_string_from_bytes(vm, "0.2", 3U);

    (void)memset(state, 0, sizeof(*state));
    state->vm = vm;
    tinypy_module_add_value(module, "__name__", 8U, name);
    tinypy_module_add_value(module, "__doc__", 7U, doc);
    tinypy_module_add_value(module, "__version__", 11U, version);
    tinypy_module_add_value(module, "error", 5U, &vm->exception_types[TINYPY_EXCEPTION_VALUE_ERROR]->base.base);
    tinypy_module_add_value(module, "_PY_STRUCT_FLOAT_COERCE", 23U, &vm->true_object.base);
    tinypy_module_add_value(module, "_PY_STRUCT_RANGE_CHECKING", 25U, &vm->true_object.base);
    __tinypy_struct_add_function(vm, module, state, "calcsize", 8U, __tinypy_struct_calcsize);
    __tinypy_struct_add_function(vm, module, state, "pack", 4U, __tinypy_struct_pack);
    __tinypy_struct_add_function(vm, module, state, "unpack", 6U, __tinypy_struct_unpack);
    __tinypy_struct_add_function(vm, module, state, "pack_into", 9U, __tinypy_struct_pack_into);
    __tinypy_struct_add_function(vm, module, state, "unpack_from", 11U, __tinypy_struct_unpack_from);
    __tinypy_struct_add_function(vm, module, state, "_clearcache", 11U, __tinypy_struct_clearcache);
    tinypy_type_t *struct_type = tinypy_type_new(vm, "Struct", 6U, NULL, 0U, NULL, NULL, NULL);
    tinypy_value_t *module_name = tinypy_string_from_bytes(vm, "_struct", 7U);

    tinypy_type_set_attr(struct_type, "__module__", 10U, module_name);
    __tinypy_struct_add_type_method(struct_type, state, "__init__", 8U, __tinypy_struct_object_init);
    __tinypy_struct_add_type_method(struct_type, state, "pack", 4U, __tinypy_struct_object_method);
    __tinypy_struct_add_type_method(struct_type, state, "unpack", 6U, __tinypy_struct_object_method);
    __tinypy_struct_add_type_method(struct_type, state, "pack_into", 9U, __tinypy_struct_object_method);
    __tinypy_struct_add_type_method(struct_type, state, "unpack_from", 11U, __tinypy_struct_object_method);
    __tinypy_struct_add_type_property(struct_type, state, "format", 6U);
    __tinypy_struct_add_type_property(struct_type, state, "size", 4U);
    tinypy_module_add_value(module, "Struct", 6U, &struct_type->base.base);
    TINYPY_DECREF(module_name);
    TINYPY_DECREF(&struct_type->base.base);
    TINYPY_DECREF(version);
    TINYPY_DECREF(doc);
    TINYPY_DECREF(name);
    tinypy_internal_register_module(vm, "_struct", 7U, module);
    TINYPY_DECREF(module);
}
