#include "tinypy/buffer.h"

#include "internal.h"

#include <stdio.h>

//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_buffer_supported(const tinypy_value_t *value) {
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);

    return kind == TINYPY_VALUE_STRING || kind == TINYPY_VALUE_UNICODE || kind == TINYPY_VALUE_BUFFER || kind == TINYPY_VALUE_BYTEARRAY;
}
//////////////////////////////////////////////////////////////////////////
static const uint8_t *__tinypy_buffer_owner_view(const tinypy_value_t *owner, size_t *out_size) {
    const uint8_t * function_result;
    switch (TINYPY_VALUE_KIND(owner)) {
    case TINYPY_VALUE_STRING:
        function_result = (const uint8_t *)tinypy_string_view(owner, out_size);
        return function_result;
    case TINYPY_VALUE_UNICODE: {
        *out_size = TINYPY_SIZED_SIZE(owner) * sizeof(uint32_t);
        const uint8_t *result = *out_size != 0U ? (const uint8_t *)TINYPY_UNICODE_OBJECT(owner)->native_buffer : TINYPY_UNICODE_OBJECT(owner)->utf8;
        return result;
    }
    case TINYPY_VALUE_BUFFER:
        function_result = (const uint8_t *)tinypy_buffer_view(owner, out_size);
        return function_result;
    case TINYPY_VALUE_BYTEARRAY:
        function_result = (const uint8_t *)tinypy_bytearray_view(owner, out_size);
        return function_result;
    default:
        *out_size = 0U;
        return NULL;
    }
}
//////////////////////////////////////////////////////////////////////////
static const uint8_t *__tinypy_buffer_constrained_view(const uint8_t *bytes, size_t owner_size, size_t offset, size_t requested_size, size_t *out_size) {
    if (offset >= owner_size) {
        *out_size = 0U;
        return bytes != NULL ? bytes + owner_size : NULL;
    }
    owner_size -= offset;
    *out_size = requested_size == TINYPY_BUFFER_TO_END || requested_size > owner_size ? owner_size : requested_size;
    return bytes != NULL ? bytes + offset : NULL;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_buffer_from_object(tinypy_value_t *object, size_t offset, size_t size) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(object);

    if (TINYPY_VALUE_KIND(object) == TINYPY_VALUE_BUFFER) {
        tinypy_buffer_object_t *source = TINYPY_BUFFER_OBJECT(object);

        if (source->size != TINYPY_BUFFER_TO_END) {
            size_t available = offset < source->size ? source->size - offset : 0U;

            if (size == TINYPY_BUFFER_TO_END || size > available) {
                size = available;
            }
        }
        offset += source->offset;
        object = source->owner;
    }
    if (TINYPY_VALUE_KIND(object) == TINYPY_VALUE_UNICODE) {
        size_t owner_size;

        (void)tinypy_internal_unicode_native_buffer(object, TINYPY_FALSE, &owner_size, NULL);
    }
    tinypy_buffer_object_t *buffer = (tinypy_buffer_object_t *)tinypy_internal_value_allocate(vm, TINYPY_VALUE_BUFFER, sizeof(*buffer));
    buffer->owner = object;
    buffer->offset = offset;
    buffer->size = size;
    buffer->hash = (tinypy_hash_t)0;
    buffer->hash_computed = TINYPY_FALSE;
    TINYPY_INCREF(object);
    return &buffer->base;
}
//////////////////////////////////////////////////////////////////////////
const void *tinypy_buffer_view(const tinypy_value_t *value, size_t *out_size) {
    const uint8_t *bytes;
    size_t owner_size;

    const tinypy_buffer_object_t *buffer = TINYPY_BUFFER_OBJECT((tinypy_value_t *)value);
    bytes = __tinypy_buffer_owner_view(buffer->owner, &owner_size);
    const uint8_t *result = __tinypy_buffer_constrained_view(bytes, owner_size, buffer->offset, buffer->size, out_size);
    return result;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_buffer_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data) {
    visit(TINYPY_BUFFER_OBJECT(value)->owner, user_data);
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_buffer_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = type->vm;
    size_t argument_count = TINYPY_TUPLE_SIZE(args);
    size_t owner_size;
    int64_t offset = INT64_C(0);
    int64_t requested_size = INT64_C(-1);

    if (kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "buffer() does not take keyword arguments", out_error);
        return NULL;
    }
    if (argument_count < 1U || argument_count > 3U) {
        tinypy_internal_make_arity_error(vm, "buffer", 6U, argument_count, 1U, 3U, TINYPY_ARITY_STYLE_PARSED, out_error);
        return NULL;
    }
    tinypy_value_t *owner = TINYPY_TUPLE_GET(args, 0U);
    tinypy_bool_t condition = argument_count >= 2U;
    if (condition != 0) {
        tinypy_value_t *item = TINYPY_TUPLE_GET(args, 1U);
        condition = tinypy_internal_integer_as_ssize(item, &offset, out_error) == 0;
    }
    if (condition) {
        return NULL;
    }
    tinypy_bool_t condition_2 = argument_count >= 3U;
    if (condition_2 != 0) {
        tinypy_value_t *item = TINYPY_TUPLE_GET(args, 2U);
        condition_2 = tinypy_internal_integer_as_ssize(item, &requested_size, out_error) == 0;
    }
    if (condition_2) {
        return NULL;
    }
    if (__tinypy_buffer_supported(owner) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "buffer object expected", out_error);
        return NULL;
    }
    if (TINYPY_VALUE_KIND(owner) == TINYPY_VALUE_UNICODE && tinypy_internal_unicode_native_buffer(owner, TINYPY_TRUE, &owner_size, out_error) == NULL) {
        return NULL;
    }
    if (offset < 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "offset must be zero or positive", out_error);
        return NULL;
    }
    if (requested_size < -1) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "size must be zero or positive", out_error);
        return NULL;
    }
    tinypy_value_t *return_value_1 = tinypy_buffer_from_object(owner, (size_t)offset, requested_size < 0 ? TINYPY_BUFFER_TO_END : (size_t)requested_size);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
ptrdiff_t tinypy_internal_buffer_length(tinypy_value_t *value, tinypy_error_t **out_error) {
    size_t size;

    TINYPY_CLEAR_ERROR(out_error);
    (void)tinypy_buffer_view(value, &size);
    return (ptrdiff_t)size;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_buffer_normalize_index(tinypy_value_t *value, tinypy_value_t *key, size_t *out_index, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    int64_t index;
    size_t size;

    if (tinypy_internal_object_has_special_key(key, vm->internal_special_index_key) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "sequence index must be integer", out_error);
        return TINYPY_FALSE;
    }
    if (tinypy_internal_index_as_i64(key, &index, TINYPY_TRUE, out_error) == 0) {
        return TINYPY_FALSE;
    }
    (void)tinypy_buffer_view(value, &size);
    if (index < 0) {
        uint64_t distance = (uint64_t)(-(index + 1)) + UINT64_C(1);

        if (distance > size) {
            goto out_of_range;
        }
        *out_index = size - (size_t)distance;
        return TINYPY_TRUE;
    }
    if ((uint64_t)index >= (uint64_t)size) {
        goto out_of_range;
    }
    *out_index = (size_t)index;
    return TINYPY_TRUE;
out_of_range:
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_INDEX, "buffer index out of range", out_error);
    return TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_buffer_slice(tinypy_value_t *value, tinypy_value_t *slice, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_internal_slice_indices_t indices;
    size_t size;
    const uint8_t *bytes;
    uint8_t *selected;
    int64_t source;
    size_t index;

    if (tinypy_internal_slice_unpack(slice, &indices, out_error) == 0) {
        return NULL;
    }
    bytes = (const uint8_t *)tinypy_buffer_view(value, &size);
    if (tinypy_internal_slice_adjust_indices(vm, size, &indices, out_error) == 0) {
        return NULL;
    }
    if (indices.length == 0U) {
        tinypy_value_t *result = tinypy_internal_string_from_bytes_checked(vm, NULL, 0U, out_error);

        return result;
    }
    if (indices.step == 1 || indices.length == 1U) {
        tinypy_value_t *result = tinypy_internal_string_from_bytes_checked(vm, bytes + (size_t)indices.start, indices.length, out_error);

        return result;
    }
    tinypy_value_t *result = tinypy_internal_text_allocate_uninitialized_checked(vm, TINYPY_VALUE_STRING, indices.length, indices.length, &selected, out_error);
    if (result == NULL) {
        return NULL;
    }
    source = indices.start;
    for (index = 0U; index < indices.length; ++index) {
        selected[index] = bytes[(size_t)source];
        if (index + 1U < indices.length) {
            source += indices.step;
        }
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_buffer_get_item(tinypy_value_t *value, tinypy_value_t *key, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    const uint8_t *bytes;
    size_t size;
    size_t index;

    TINYPY_CLEAR_ERROR(out_error);
    if (TINYPY_VALUE_KIND(key) == TINYPY_VALUE_SLICE) {
        tinypy_value_t *return_value_1 = __tinypy_buffer_slice(value, key, out_error);
        return return_value_1;
    }
    if (__tinypy_buffer_normalize_index(value, key, &index, out_error) == 0) {
        return NULL;
    }
    bytes = (const uint8_t *)tinypy_buffer_view(value, &size);
    tinypy_value_t *return_value_2 = tinypy_string_from_bytes(vm, bytes + index, 1U);
    return return_value_2;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_buffer_string(tinypy_value_t *value, tinypy_error_t **out_error) {
    const void *bytes;
    size_t size;

    TINYPY_CLEAR_ERROR(out_error);
    bytes = tinypy_buffer_view(value, &size);
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_value_t *return_value_1 = tinypy_string_from_bytes(vm, bytes, size);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_buffer_character_string(tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_buffer_object_t *buffer = TINYPY_BUFFER_OBJECT(value);
    size_t offset = buffer->offset;
    size_t requested_size = buffer->size;
    tinypy_value_t *owner = TINYPY_RET(buffer->owner);
    const uint8_t *bytes;
    size_t size;

    if (TINYPY_VALUE_KIND(owner) == TINYPY_VALUE_UNICODE) {
        tinypy_value_t *encoded = tinypy_internal_text_codec(vm, owner, NULL, NULL, TINYPY_FALSE, TINYPY_TRUE, NULL, out_error);

        TINYPY_DECREF(owner);
        if (encoded == NULL) {
            return NULL;
        }
        owner = encoded;
    }
    bytes = __tinypy_buffer_owner_view(owner, &size);
    bytes = __tinypy_buffer_constrained_view(bytes, size, offset, requested_size, &size);
    tinypy_value_t *result = tinypy_internal_string_from_bytes_checked(vm, bytes, size, out_error);

    TINYPY_DECREF(owner);
    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_buffer_repr(tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_buffer_object_t *buffer = TINYPY_BUFFER_OBJECT(value);
    char text[192U];
    int written;

    TINYPY_CLEAR_ERROR(out_error);
    if (buffer->size == TINYPY_BUFFER_TO_END) {
        written = snprintf(text, sizeof(text), "<read-only buffer for %p, size -1, offset %zu at %p>", (void *)buffer->owner, buffer->offset, (void *)value);
    } else {
        written = snprintf(text, sizeof(text), "<read-only buffer for %p, size %zu, offset %zu at %p>", (void *)buffer->owner, buffer->size, buffer->offset, (void *)value);
    }
    if (written < 0 || (size_t)written >= sizeof(text)) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "buffer representation is too large", out_error);
        return NULL;
    }
    tinypy_value_t *return_value_1 = tinypy_string_from_bytes(vm, text, (size_t)written);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_buffer_concat(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(left);
    const uint8_t *left_bytes;
    const uint8_t *right_bytes;
    size_t left_size;
    size_t right_size;
    size_t total_size;
    uint8_t *output;

    if (__tinypy_buffer_supported(right) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "bad argument type for built-in operation", out_error);
        return NULL;
    }
    left_bytes = (const uint8_t *)tinypy_buffer_view(left, &left_size);
    if (left_size == 0U) {
        return TINYPY_RET(right);
    }
    if (TINYPY_VALUE_KIND(right) == TINYPY_VALUE_UNICODE && tinypy_internal_unicode_native_buffer(right, TINYPY_TRUE, &right_size, out_error) == NULL) {
        return NULL;
    }
    right_bytes = __tinypy_buffer_owner_view(right, &right_size);
    if (right_size > SIZE_MAX - left_size || left_size + right_size >= (size_t)PTRDIFF_MAX) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "concatenated buffer is too large", out_error);
        return NULL;
    }
    total_size = left_size + right_size;
    tinypy_value_t *result = tinypy_internal_text_allocate_uninitialized_checked(vm, TINYPY_VALUE_STRING, total_size, total_size, &output, out_error);
    if (result == NULL) {
        return NULL;
    }
    if (left_size != 0U) {
        (void)memcpy(output, left_bytes, left_size);
    }
    if (right_size != 0U) {
        (void)memcpy(output + left_size, right_bytes, right_size);
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_buffer_repeat(tinypy_value_t *buffer, tinypy_value_t *count_value, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(buffer);
    const uint8_t *bytes;
    size_t unit_size;
    size_t total_size;
    int64_t count;
    uint8_t *output;

    if (tinypy_internal_object_has_special_key(count_value, vm->internal_special_index_key) == 0) {
        const tinypy_message_part_t parts[] = {
            TINYPY_MESSAGE_PART_LITERAL("'"),
            TINYPY_MESSAGE_PART_TYPE_NAME(count_value),
            TINYPY_MESSAGE_PART_LITERAL("' object cannot be interpreted as an index")
        };
        tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
        return NULL;
    }
    if (tinypy_internal_index_as_i64(count_value, &count, TINYPY_FALSE, out_error) == 0) {
        if (out_error == NULL || *out_error == NULL) {
            tinypy_value_t *result = TINYPY_RET_NOT_IMPLEMENTED(vm);
            return result;
        }
        return NULL;
    }
    bytes = (const uint8_t *)tinypy_buffer_view(buffer, &unit_size);
    if (count <= 0 || unit_size == 0U) {
        tinypy_value_t *result = TINYPY_RET_EMPTY_STRING(vm);

        return result;
    }
    if ((uint64_t)count > (uint64_t)(SIZE_MAX / unit_size) || unit_size * (size_t)count >= (size_t)PTRDIFF_MAX) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "repeated buffer is too large", out_error);
        return NULL;
    }
    total_size = unit_size * (size_t)count;
    tinypy_value_t *result = tinypy_internal_text_allocate_uninitialized_checked(vm, TINYPY_VALUE_STRING, total_size, total_size, &output, out_error);
    if (result == NULL) {
        return NULL;
    }
    (void)memcpy(output, bytes, unit_size);
    size_t copied = unit_size;
    while (copied < total_size) {
        size_t chunk = copied < total_size - copied ? copied : total_size - copied;

        (void)memcpy(output + copied, output, chunk);
        copied += chunk;
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_buffer_len_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 0U, 0U, TINYPY_ARITY_STYLE_WRAPPER, out_error) == 0) {
        return NULL;
    }
    ptrdiff_t length = tinypy_internal_buffer_length(TINYPY_TUPLE_GET(args, 0U), out_error);
    tinypy_value_t *result = tinypy_integer_from_i64(vm, (int64_t)length);

    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_buffer_hash_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 0U, 0U, TINYPY_ARITY_STYLE_WRAPPER, out_error) == 0) {
        return NULL;
    }
    tinypy_hash_t hash = tinypy_internal_hash_builtin_value(TINYPY_TUPLE_GET(args, 0U), out_error);
    if (out_error != NULL && *out_error != NULL) {
        return NULL;
    }
    tinypy_value_t *result = tinypy_integer_from_i64(vm, hash);

    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_buffer_getitem_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {

    (void)user_data;
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 1U, 1U, TINYPY_ARITY_STYLE_WRAPPER, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *result = tinypy_internal_buffer_get_item(TINYPY_TUPLE_GET(args, 0U), TINYPY_TUPLE_GET(args, 1U), out_error);

    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_buffer_getslice_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {

    (void)user_data;
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 2U, 2U, TINYPY_ARITY_STYLE_PARSED, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *slice = tinypy_internal_legacy_slice_new(args, out_error);
    if (slice == NULL) {
        return NULL;
    }
    tinypy_value_t *result = tinypy_internal_buffer_get_item(TINYPY_TUPLE_GET(args, 0U), slice, out_error);
    TINYPY_DECREF(slice);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_buffer_add_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {

    (void)user_data;
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 1U, 1U, TINYPY_ARITY_STYLE_WRAPPER, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *result = __tinypy_buffer_concat(TINYPY_TUPLE_GET(args, 0U), TINYPY_TUPLE_GET(args, 1U), out_error);

    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_buffer_multiply_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {

    (void)user_data;
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 1U, 1U, TINYPY_ARITY_STYLE_UNPACK, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *result = __tinypy_buffer_repeat(TINYPY_TUPLE_GET(args, 0U), TINYPY_TUPLE_GET(args, 1U), out_error);

    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_buffer_cmp_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *left;
    tinypy_value_t *right;
    const uint8_t *left_bytes;
    const uint8_t *right_bytes;
    size_t left_size;
    size_t right_size;
    size_t common_size;
    int32_t order = 0;

    (void)user_data;
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 1U, 1U, TINYPY_ARITY_STYLE_WRAPPER, out_error) == 0) {
        return NULL;
    }
    left = TINYPY_TUPLE_GET(args, 0U);
    right = TINYPY_TUPLE_GET(args, 1U);
    if (TINYPY_VALUE_KIND(right) != TINYPY_VALUE_BUFFER) {
        tinypy_message_part_t parts[] = {
            TINYPY_MESSAGE_PART_LITERAL("buffer.__cmp__(x,y) requires y to be a 'buffer', not a '"),
            TINYPY_MESSAGE_PART_TYPE_NAME(right), TINYPY_MESSAGE_PART_LITERAL("'"),
        };

        tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
        return NULL;
    }
    left_bytes = (const uint8_t *)tinypy_buffer_view(left, &left_size);
    right_bytes = (const uint8_t *)tinypy_buffer_view(right, &right_size);
    common_size = left_size < right_size ? left_size : right_size;
    if (common_size != 0U) {
        int comparison = memcmp(left_bytes, right_bytes, common_size);

        order = comparison < 0 ? -1 : (comparison > 0 ? 1 : 0);
    }
    if (order == 0 && left_size != right_size) {
        order = left_size < right_size ? -1 : 1;
    }
    tinypy_value_t *result = tinypy_integer_from_i64(vm, order);

    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_buffer_string_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {

    (void)user_data;
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 0U, 0U, TINYPY_ARITY_STYLE_WRAPPER, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *result = tinypy_internal_buffer_string(TINYPY_TUPLE_GET(args, 0U), out_error);

    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_buffer_repr_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {

    (void)user_data;
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 0U, 0U, TINYPY_ARITY_STYLE_WRAPPER, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *result = tinypy_internal_buffer_repr(TINYPY_TUPLE_GET(args, 0U), out_error);

    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_buffer_readonly_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *name = tinypy_native_function_name(function);
    tinypy_bool_t slice = name == vm->internal_special_setslice_key || name == vm->internal_special_delslice_key;
    size_t count = name == vm->internal_special_setslice_key ? 3U : (slice != TINYPY_FALSE || name == vm->internal_special_setitem_key ? 2U : 1U);
    tinypy_arity_style_e style = slice != TINYPY_FALSE ? TINYPY_ARITY_STYLE_PARSED : (count == 2U ? TINYPY_ARITY_STYLE_UNPACK : TINYPY_ARITY_STYLE_WRAPPER);

    (void)user_data;
    if (tinypy_internal_native_method_arguments(function, args, kwargs, count, count, style, out_error) == TINYPY_FALSE) {
        return NULL;
    }
    if (slice != TINYPY_FALSE) {
        tinypy_value_t *bounds = tinypy_internal_legacy_slice_new(args, out_error);
        if (bounds == NULL) {
            return NULL;
        }
        TINYPY_DECREF(bounds);
    }
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "buffer is read-only", out_error);
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_initialize_buffer_type(tinypy_vm_t *vm) {
    tinypy_internal_type_add_method(&vm->types[TINYPY_VALUE_BUFFER], vm->internal_special_length_key, __tinypy_buffer_len_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(&vm->types[TINYPY_VALUE_BUFFER], vm->internal_special_hash_key, __tinypy_buffer_hash_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(&vm->types[TINYPY_VALUE_BUFFER], vm->internal_special_getitem_key, __tinypy_buffer_getitem_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(&vm->types[TINYPY_VALUE_BUFFER], vm->internal_special_getslice_key, __tinypy_buffer_getslice_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(&vm->types[TINYPY_VALUE_BUFFER], vm->internal_special_add_key, __tinypy_buffer_add_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(&vm->types[TINYPY_VALUE_BUFFER], vm->internal_special_mul_key, __tinypy_buffer_multiply_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(&vm->types[TINYPY_VALUE_BUFFER], vm->internal_special_rmul_key, __tinypy_buffer_multiply_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(&vm->types[TINYPY_VALUE_BUFFER], vm->internal_special_cmp_key, __tinypy_buffer_cmp_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(&vm->types[TINYPY_VALUE_BUFFER], vm->internal_special_str_key, __tinypy_buffer_string_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(&vm->types[TINYPY_VALUE_BUFFER], vm->internal_special_repr_key, __tinypy_buffer_repr_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(&vm->types[TINYPY_VALUE_BUFFER], vm->internal_special_setitem_key, __tinypy_buffer_readonly_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(&vm->types[TINYPY_VALUE_BUFFER], vm->internal_special_setslice_key, __tinypy_buffer_readonly_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(&vm->types[TINYPY_VALUE_BUFFER], vm->internal_special_delitem_key, __tinypy_buffer_readonly_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(&vm->types[TINYPY_VALUE_BUFFER], vm->internal_special_delslice_key, __tinypy_buffer_readonly_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
}
//////////////////////////////////////////////////////////////////////////
typedef struct tinypy_internal_memoryview_payload_t {
    tinypy_value_t *owner;
    size_t offset;
    size_t size;
    tinypy_bool_t readonly;
} tinypy_internal_memoryview_payload_t;
//////////////////////////////////////////////////////////////////////////
static tinypy_internal_memoryview_payload_t *__tinypy_memoryview_payload(tinypy_value_t *value) {
    tinypy_internal_memoryview_payload_t *result = (tinypy_internal_memoryview_payload_t *)tinypy_native_instance_payload(value);

    return result;
}
//////////////////////////////////////////////////////////////////////////
static const tinypy_internal_memoryview_payload_t *__tinypy_memoryview_const_payload(const tinypy_value_t *value) {
    const tinypy_internal_memoryview_payload_t *result = (const tinypy_internal_memoryview_payload_t *)tinypy_native_instance_const_payload(value);

    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_memoryview_check(const tinypy_value_t *value) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);

    tinypy_bool_t result = TINYPY_VALUE_KIND(value) == TINYPY_VALUE_NATIVE_INSTANCE && vm->memoryview_type != NULL && value->type == vm->memoryview_type ? TINYPY_TRUE : TINYPY_FALSE;

    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_memoryview_is_readonly(const tinypy_value_t *value) {
    const tinypy_internal_memoryview_payload_t *payload = __tinypy_memoryview_const_payload(value);
    tinypy_bool_t result = payload->readonly;

    return result;
}
//////////////////////////////////////////////////////////////////////////
const uint8_t *tinypy_internal_memoryview_view(const tinypy_value_t *value, size_t *out_size) {
    const tinypy_internal_memoryview_payload_t *payload = __tinypy_memoryview_const_payload(value);
    const uint8_t *bytes;
    size_t owner_size;

    if (TINYPY_VALUE_KIND(payload->owner) == TINYPY_VALUE_STRING) {
        bytes = (const uint8_t *)tinypy_string_view(payload->owner, &owner_size);
    }
    else if (TINYPY_VALUE_KIND(payload->owner) == TINYPY_VALUE_BYTEARRAY) {
        bytes = (const uint8_t *)tinypy_bytearray_view(payload->owner, &owner_size);
    }
    else {
        bytes = (const uint8_t *)tinypy_buffer_view(payload->owner, &owner_size);
    }
    if (payload->offset >= owner_size) {
        *out_size = 0U;
        return bytes != NULL ? bytes + owner_size : NULL;
    }
    owner_size -= payload->offset;
    *out_size = payload->size < owner_size ? payload->size : owner_size;
    return bytes != NULL ? bytes + payload->offset : NULL;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_memoryview_writable_owner(tinypy_value_t *value, size_t *out_offset) {
    tinypy_internal_memoryview_payload_t *payload = __tinypy_memoryview_payload(value);
    tinypy_value_t *owner = payload->owner;

    *out_offset = payload->offset;
    tinypy_value_t *result = TINYPY_VALUE_KIND(owner) == TINYPY_VALUE_BYTEARRAY ? owner : NULL;

    return result;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_memoryview_export(tinypy_value_t *value, int32_t change) {
    size_t offset = 0U;
    tinypy_value_t *owner = __tinypy_memoryview_writable_owner(value, &offset);

    (void)offset;
    if (owner != NULL) {
        tinypy_bytearray_object_t *bytearray = TINYPY_BYTEARRAY_OBJECT(owner);

        if (change > 0) {
            bytearray->exports += 1U;
        }
        else {
            bytearray->exports -= 1U;
        }
    }
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_memoryview_instance(tinypy_type_t *type, tinypy_value_t *owner, size_t offset, size_t size, tinypy_bool_t readonly) {
    tinypy_value_t *result = tinypy_native_instance_new(type);
    tinypy_internal_memoryview_payload_t *payload = __tinypy_memoryview_payload(result);

    if (tinypy_internal_memoryview_check(owner) != 0) {
        const tinypy_internal_memoryview_payload_t *source = __tinypy_memoryview_const_payload(owner);
        size_t available = offset < source->size ? source->size - offset : 0U;

        owner = source->owner;
        offset = source->offset + (offset < source->size ? offset : source->size);
        if (size > available) {
            size = available;
        }
        readonly = source->readonly != 0 ? TINYPY_TRUE : readonly;
    }
    payload->owner = owner;
    payload->offset = offset;
    payload->size = size;
    payload->readonly = readonly;
    TINYPY_INCREF(owner);
    if (readonly == 0) {
        __tinypy_memoryview_export(result, 1);
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_memoryview_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data) {
    tinypy_internal_memoryview_payload_t *payload = __tinypy_memoryview_payload(value);
    tinypy_value_t *owner = payload->owner;

    tinypy_internal_instance_release_references(value, visit, user_data);
    if (owner == NULL) {
        return;
    }
    if (payload->readonly == 0) {
        __tinypy_memoryview_export(value, -1);
    }
    payload->owner = NULL;
    visit(owner, user_data);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_memoryview_traverse_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data) {
    tinypy_internal_memoryview_payload_t *payload = __tinypy_memoryview_payload(value);

    tinypy_internal_instance_release_references(value, visit, user_data);
    if (payload->owner != NULL) {
        visit(payload->owner, user_data);
    }
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_memoryview_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = type->vm;
    size_t count = TINYPY_TUPLE_SIZE(args);
    size_t supplied = count + (kwargs != NULL ? TINYPY_DICT_SIZE(kwargs) : 0U);
    size_t size;
    tinypy_bool_t readonly;

    if (supplied > 1U) {
        tinypy_internal_make_arity_error(vm, "memoryview", 10U, supplied, 0U, 1U, TINYPY_ARITY_STYLE_PARSED, out_error);
        return NULL;
    }
    tinypy_value_t *owner = count != 0U ? TINYPY_TUPLE_GET(args, 0U) : tinypy_internal_constructor_keyword_optional(kwargs, vm->internal_object_key);
    if (owner == NULL) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "Required argument 'object' (pos 1) not found", out_error);
        return NULL;
    }
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(owner);
    if (kind != TINYPY_VALUE_STRING && kind != TINYPY_VALUE_BYTEARRAY && kind != TINYPY_VALUE_BUFFER && tinypy_internal_memoryview_check(owner) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "cannot make memory view because object does not have the buffer interface", out_error);
        return NULL;
    }
    if (tinypy_internal_memoryview_check(owner) != 0) {
        const tinypy_internal_memoryview_payload_t *source = __tinypy_memoryview_const_payload(owner);

        (void)tinypy_internal_memoryview_view(owner, &size);
        readonly = source->readonly;
    }
    else if (kind == TINYPY_VALUE_BYTEARRAY) {
        (void)tinypy_bytearray_view(owner, &size);
        readonly = TINYPY_FALSE;
    }
    else if (kind == TINYPY_VALUE_BUFFER) {
        (void)tinypy_buffer_view(owner, &size);
        readonly = TINYPY_TRUE;
    }
    else {
        (void)tinypy_string_view(owner, &size);
        readonly = TINYPY_TRUE;
    }
    tinypy_value_t *result = __tinypy_memoryview_instance(type, owner, 0U, size, readonly);

    return result;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_memoryview_finalize(tinypy_value_t *instance, void *payload_value, void *user_data) {
    tinypy_internal_memoryview_payload_t *payload = (tinypy_internal_memoryview_payload_t *)payload_value;
    tinypy_vm_t *vm = TINYPY_VALUE_VM(instance);

    (void)user_data;
    if (payload->owner == NULL) {
        return;
    }
    if (vm->state == TINYPY_VM_STATE_DESTROYING) {
        payload->owner = NULL;
        return;
    }
    if (payload->readonly == 0) {
        __tinypy_memoryview_export(instance, -1);
    }
    TINYPY_DECREF(payload->owner);
    payload->owner = NULL;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_memoryview_repr(tinypy_value_t *instance, void *payload, void *user_data, tinypy_error_t **out_error) {
    static const char prefix[] = "<memory at 0x";
    static const char digits[] = "0123456789abcdef";
    char bytes[sizeof(prefix) - 1U + sizeof(uintptr_t) * 2U + 1U];
    uintptr_t address = (uintptr_t)instance;
    size_t position = sizeof(prefix) - 1U;
    size_t digit_count = 1U;
    uintptr_t remaining = address;
    size_t index;

    (void)payload;
    (void)user_data;
    TINYPY_CLEAR_ERROR(out_error);
    (void)memcpy(bytes, prefix, sizeof(prefix) - 1U);
    while (remaining >= (uintptr_t)16U) {
        digit_count += 1U;
        remaining /= (uintptr_t)16U;
    }
    for (index = digit_count; index != 0U; index -= 1U) {
        bytes[position + index - 1U] = digits[address & (uintptr_t)15U];
        address >>= 4U;
    }
    position += digit_count;
    bytes[position++] = '>';
    tinypy_value_t *result = tinypy_string_from_bytes(TINYPY_VALUE_VM(instance), bytes, position);

    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_hash_t __tinypy_memoryview_hash(tinypy_value_t *instance, void *payload, void *user_data, tinypy_error_t **out_error) {
    (void)payload;
    (void)user_data;
    tinypy_internal_make_vm_error(TINYPY_VALUE_VM(instance), TINYPY_ERROR_TYPE, "memoryview objects are unhashable", out_error);
    return (tinypy_hash_t)0;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_memoryview_not_implemented(tinypy_vm_t *vm, tinypy_error_t **out_error) {
    tinypy_value_t *message = TINYPY_RET_EMPTY_STRING(vm);
    tinypy_value_t *args = tinypy_tuple_from_items(vm, &message, 1U);
    tinypy_value_t *exception = tinypy_internal_exception_instantiate(vm->exception_types[TINYPY_EXCEPTION_NOT_IMPLEMENTED_ERROR], args, NULL, out_error);

    TINYPY_DECREF(args);
    TINYPY_DECREF(message);
    if (exception != NULL) {
        tinypy_internal_exception_set_raised(vm, exception, NULL);
        TINYPY_DECREF(exception);
        tinypy_internal_exception_make_diagnostic(vm, out_error);
    }
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_memoryview_index(tinypy_value_t *instance, tinypy_value_t *key, size_t size, size_t *out_index, tinypy_error_t **out_error) {
    int64_t index;

    tinypy_vm_t *vm = TINYPY_VALUE_VM(instance);
    if (tinypy_internal_object_has_special_key(key, vm->internal_special_index_key) == 0) {
        const tinypy_message_part_t parts[] = {
            TINYPY_MESSAGE_PART_LITERAL("cannot index memory using \""),
            TINYPY_MESSAGE_PART_TYPE_NAME(key),
            TINYPY_MESSAGE_PART_LITERAL("\"")
        };
        tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
        return TINYPY_FALSE;
    }
    if (tinypy_internal_index_as_i64(key, &index, TINYPY_TRUE, out_error) == 0) {
        return TINYPY_FALSE;
    }
    if (index < 0) {
        uint64_t distance = (uint64_t)(-(index + INT64_C(1))) + UINT64_C(1);

        if (distance <= size) {
            *out_index = size - (size_t)distance;
            return TINYPY_TRUE;
        }
    }
    else if ((uint64_t)index < (uint64_t)size) {
        *out_index = (size_t)index;
        return TINYPY_TRUE;
    }
    tinypy_internal_make_vm_error(TINYPY_VALUE_VM(instance), TINYPY_ERROR_INDEX, "index out of bounds", out_error);
    return TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_memoryview_get(tinypy_value_t *instance, void *payload_value, tinypy_value_t *key, void *user_data, tinypy_error_t **out_error) {
    tinypy_internal_memoryview_payload_t *payload = (tinypy_internal_memoryview_payload_t *)payload_value;
    tinypy_vm_t *vm = TINYPY_VALUE_VM(instance);
    size_t size;
    const uint8_t *bytes = tinypy_internal_memoryview_view(instance, &size);

    (void)user_data;
    if (TINYPY_VALUE_KIND(key) == TINYPY_VALUE_SLICE) {
        tinypy_internal_slice_indices_t slice;

        if (tinypy_internal_slice_indices(key, size, &slice, out_error) == 0) {
            return NULL;
        }
        if (slice.step != 1) {
            __tinypy_memoryview_not_implemented(vm, out_error);
            return NULL;
        }
        tinypy_value_t *result = __tinypy_memoryview_instance(instance->type, instance, (size_t)slice.start, slice.length, payload->readonly);

        return result;
    }
    size_t index;
    if (__tinypy_memoryview_index(instance, key, size, &index, out_error) == 0) {
        return NULL;
    }
    bytes = tinypy_internal_memoryview_view(instance, &size);
    tinypy_value_t *result = tinypy_string_from_bytes(vm, bytes + index, 1U);

    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_memoryview_set(tinypy_value_t *instance, void *payload_value, tinypy_value_t *key, tinypy_value_t *value, void *user_data, tinypy_error_t **out_error) {
    tinypy_internal_memoryview_payload_t *payload = (tinypy_internal_memoryview_payload_t *)payload_value;
    tinypy_vm_t *vm = TINYPY_VALUE_VM(instance);
    size_t size;

    (void)user_data;
    (void)tinypy_internal_memoryview_view(instance, &size);
    if (payload->readonly != 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "cannot modify read-only memory", out_error);
        return TINYPY_FALSE;
    }
    if (value == NULL) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "cannot delete memory", out_error);
        return TINYPY_FALSE;
    }
    const uint8_t *replacement;
    size_t replacement_size;
    size_t index = 0U;
    tinypy_internal_slice_indices_t slice;
    tinypy_bool_t is_slice = TINYPY_VALUE_KIND(key) == TINYPY_VALUE_SLICE;
    size_t owner_offset = 0U;
    tinypy_value_t *owner = __tinypy_memoryview_writable_owner(instance, &owner_offset);
    if (owner == NULL) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "cannot modify read-only memory", out_error);
        return TINYPY_FALSE;
    }
    if (is_slice != 0) {
        if (tinypy_internal_slice_indices(key, size, &slice, out_error) == 0) {
            return TINYPY_FALSE;
        }
        if (slice.step != 1) {
            __tinypy_memoryview_not_implemented(vm, out_error);
            return TINYPY_FALSE;
        }
    }
    else if (__tinypy_memoryview_index(instance, key, size, &index, out_error) == 0) {
        return TINYPY_FALSE;
    }
    if (tinypy_internal_bytes_view(value, &replacement, &replacement_size) == 0) {
        const tinypy_message_part_t parts[] = {
            TINYPY_MESSAGE_PART_LITERAL("'"),
            TINYPY_MESSAGE_PART_TYPE_NAME(value),
            TINYPY_MESSAGE_PART_LITERAL("' does not have the buffer interface")
        };
        tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
        return TINYPY_FALSE;
    }
    uint8_t *owner_bytes = TINYPY_BYTEARRAY_OBJECT(owner)->bytes + owner_offset;
    if (is_slice != 0) {
        if (replacement_size != slice.length) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "cannot modify size of memoryview object", out_error);
            return TINYPY_FALSE;
        }
        if (replacement_size != 0U) {
            (void)memmove(owner_bytes + (size_t)slice.start, replacement, replacement_size);
        }
        return TINYPY_TRUE;
    }
    if (replacement_size != 1U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "cannot modify size of memoryview object", out_error);
        return TINYPY_FALSE;
    }
    owner_bytes[index] = replacement[0];
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static ptrdiff_t __tinypy_memoryview_length(tinypy_value_t *instance, void *payload, void *user_data, tinypy_error_t **out_error) {
    size_t size;

    (void)payload;
    (void)user_data;
    TINYPY_CLEAR_ERROR(out_error);
    (void)tinypy_internal_memoryview_view(instance, &size);
    return (ptrdiff_t)size;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_memoryview_compare(tinypy_value_t *instance, void *payload, tinypy_value_t *other, tinypy_compare_operation_e operation, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(instance);
    const uint8_t *left;
    const uint8_t *right;
    size_t left_size;
    size_t right_size;

    (void)payload;
    (void)user_data;
    TINYPY_CLEAR_ERROR(out_error);
    if (operation != TINYPY_COMPARE_EQUAL && operation != TINYPY_COMPARE_NOT_EQUAL) {
        tinypy_value_t *result = TINYPY_RET_NOT_IMPLEMENTED(vm);

        return result;
    }
    left = tinypy_internal_memoryview_view(instance, &left_size);
    if (tinypy_internal_bytes_view(other, &right, &right_size) == 0) {
        tinypy_value_t *result = TINYPY_RET_NOT_IMPLEMENTED(vm);

        return result;
    }
    tinypy_bool_t equal = left_size == right_size && (left_size == 0U || memcmp(left, right, left_size) == 0) ? TINYPY_TRUE : TINYPY_FALSE;
    tinypy_value_t *result = tinypy_bool_from_i32(vm, operation == TINYPY_COMPARE_EQUAL ? equal : !equal);

    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_memoryview_len_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 0U, 0U, TINYPY_ARITY_STYLE_WRAPPER, out_error) == 0) {
        return NULL;
    }
    size_t size;
    (void)tinypy_internal_memoryview_view(TINYPY_TUPLE_GET(args, 0U), &size);
    tinypy_value_t *result = tinypy_integer_from_i64(vm, (int64_t)size);

    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_memoryview_compare_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    if (tinypy_internal_native_method_arguments(function, args, kwargs, 1U, 1U, TINYPY_ARITY_STYLE_WRAPPER, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *self = TINYPY_TUPLE_GET(args, 0U);
    if (tinypy_internal_memoryview_check(self) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "comparison requires a memoryview", out_error);
        return NULL;
    }
    tinypy_value_t *result = __tinypy_memoryview_compare(self, __tinypy_memoryview_payload(self), TINYPY_TUPLE_GET(args, 1U), (tinypy_compare_operation_e)(intptr_t)user_data, NULL, out_error);

    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_memoryview_get_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {

    (void)user_data;
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 1U, 1U, TINYPY_ARITY_STYLE_WRAPPER, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *self = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *result = __tinypy_memoryview_get(self, __tinypy_memoryview_payload(self), TINYPY_TUPLE_GET(args, 1U), NULL, out_error);

    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_memoryview_set_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 2U, 2U, TINYPY_ARITY_STYLE_UNPACK, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *self = TINYPY_TUPLE_GET(args, 0U);
    if (__tinypy_memoryview_set(self, __tinypy_memoryview_payload(self), TINYPY_TUPLE_GET(args, 1U), TINYPY_TUPLE_GET(args, 2U), NULL, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *result = TINYPY_RET_NONE(vm);

    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_memoryview_delete_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 1U, 1U, TINYPY_ARITY_STYLE_WRAPPER, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *self = TINYPY_TUPLE_GET(args, 0U);
    if (__tinypy_memoryview_set(self, __tinypy_memoryview_payload(self), TINYPY_TUPLE_GET(args, 1U), NULL, NULL, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *result = TINYPY_RET_NONE(vm);

    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_memoryview_tobytes_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 0U, 0U, TINYPY_ARITY_STYLE_PARSED, out_error) == 0) {
        return NULL;
    }
    size_t size;
    const uint8_t *bytes = tinypy_internal_memoryview_view(TINYPY_TUPLE_GET(args, 0U), &size);
    tinypy_value_t *result = tinypy_string_from_bytes(vm, bytes, size);

    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_memoryview_tolist_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 0U, 0U, TINYPY_ARITY_STYLE_PARSED, out_error) == 0) {
        return NULL;
    }
    size_t size;
    const uint8_t *bytes = tinypy_internal_memoryview_view(TINYPY_TUPLE_GET(args, 0U), &size);
    tinypy_value_t *result = tinypy_list_from_items(vm, NULL, 0U);
    size_t index;

    if (tinypy_internal_list_reserve_checked(vm, result, size, out_error) == 0) {
        TINYPY_DECREF(result);
        return NULL;
    }
    for (index = 0U; index < size; ++index) {
        tinypy_value_t *item = tinypy_integer_from_i64(vm, bytes[index]);

        if (tinypy_internal_list_append_checked(result, item, out_error) == 0) {
            TINYPY_DECREF(item);
            TINYPY_DECREF(result);
            return NULL;
        }
        TINYPY_DECREF(item);
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_memoryview_repr_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {

    (void)user_data;
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 0U, 0U, TINYPY_ARITY_STYLE_WRAPPER, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *self = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *result = __tinypy_memoryview_repr(self, __tinypy_memoryview_payload(self), NULL, out_error);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_memoryview_property(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    intptr_t field = (intptr_t)user_data;

    if (tinypy_internal_native_method_arguments(function, args, kwargs, 0U, 0U, TINYPY_ARITY_STYLE_PARSED, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *self = TINYPY_TUPLE_GET(args, 0U);
    if (field == 0) {
        tinypy_value_t *result = tinypy_string_from_bytes(vm, "B", 1U);

        return result;
    }
    if (field == 1 || field == 2) {
        tinypy_value_t *result = tinypy_long_from_i64(vm, INT64_C(1));

        return result;
    }
    if (field == 3) {
        tinypy_value_t *result = tinypy_bool_from_i32(vm, __tinypy_memoryview_const_payload(self)->readonly);

        return result;
    }
    if (field == 6) {
        tinypy_value_t *result = TINYPY_RET_NONE(vm);

        return result;
    }
    size_t size;
    (void)tinypy_internal_memoryview_view(self, &size);
    tinypy_value_t *item = tinypy_long_from_i64(vm, field == 4 ? (int64_t)size : INT64_C(1));
    tinypy_value_t *result = tinypy_tuple_from_items(vm, &item, 1U);

    TINYPY_DECREF(item);
    return result;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_initialize_memoryview_type(tinypy_vm_t *vm) {
    tinypy_native_type_spec_t spec;
    tinypy_value_t *const comparison_names[] = {vm->internal_special_lt_key, vm->internal_special_le_key, vm->internal_special_eq_key, vm->internal_special_ne_key, vm->internal_special_gt_key, vm->internal_special_ge_key};
    size_t comparison_index;

    tinypy_native_type_spec_init(&spec);
    spec.payload_size = sizeof(tinypy_internal_memoryview_payload_t);
    spec.finalize = __tinypy_memoryview_finalize;
    spec.repr = __tinypy_memoryview_repr;
    spec.hash = __tinypy_memoryview_hash;
    spec.compare = __tinypy_memoryview_compare;
    spec.mapping_get = __tinypy_memoryview_get;
    spec.mapping_set = __tinypy_memoryview_set;
    spec.mapping_length = __tinypy_memoryview_length;
    spec.has_instance_dict = TINYPY_FALSE;
    spec.has_weakrefs = TINYPY_FALSE;
    vm->memoryview_type = tinypy_native_type_new_key(vm->internal_memoryview_key, NULL, 0U, NULL, &spec, NULL);
    tinypy_value_t *doc = tinypy_string_from_bytes(vm, "memoryview(object)\n\nCreate a new memoryview object which references the given object.", 85U);

    tinypy_type_set_attr_key(vm->memoryview_type, vm->internal_special_doc_key, doc);
    TINYPY_DECREF(doc);
    vm->memoryview_type->create = __tinypy_memoryview_create;
    tinypy_internal_constructor_add_builtin_new(vm->memoryview_type);
    vm->memoryview_type->release_references = __tinypy_memoryview_release_references;
    vm->memoryview_type->traverse_references = __tinypy_memoryview_traverse_references;
    tinypy_internal_type_add_method(vm->memoryview_type, vm->internal_special_length_key, __tinypy_memoryview_len_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(vm->memoryview_type, vm->internal_special_getitem_key, __tinypy_memoryview_get_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(vm->memoryview_type, vm->internal_special_setitem_key, __tinypy_memoryview_set_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(vm->memoryview_type, vm->internal_special_delitem_key, __tinypy_memoryview_delete_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(vm->memoryview_type, vm->internal_special_repr_key, __tinypy_memoryview_repr_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    for (comparison_index = 0U; comparison_index < sizeof(comparison_names) / sizeof(comparison_names[0]); ++comparison_index) {
        tinypy_internal_type_add_method(vm->memoryview_type, comparison_names[comparison_index], __tinypy_memoryview_compare_method, (void *)(intptr_t)comparison_index, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    }
    tinypy_internal_type_add_method(vm->memoryview_type, vm->internal_tobytes_key, __tinypy_memoryview_tobytes_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(vm->memoryview_type, vm->internal_tolist_key, __tinypy_memoryview_tolist_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_property(vm->memoryview_type, vm->internal_format_key, __tinypy_memoryview_property, (void *)0, NULL);
    tinypy_internal_type_add_property(vm->memoryview_type, vm->internal_itemsize_key, __tinypy_memoryview_property, (void *)1, NULL);
    tinypy_internal_type_add_property(vm->memoryview_type, vm->internal_ndim_key, __tinypy_memoryview_property, (void *)2, NULL);
    tinypy_internal_type_add_property(vm->memoryview_type, vm->internal_readonly_key, __tinypy_memoryview_property, (void *)3, NULL);
    tinypy_internal_type_add_property(vm->memoryview_type, vm->internal_shape_key, __tinypy_memoryview_property, (void *)4, NULL);
    tinypy_internal_type_add_property(vm->memoryview_type, vm->internal_strides_key, __tinypy_memoryview_property, (void *)5, NULL);
    tinypy_internal_type_add_property(vm->memoryview_type, vm->internal_suboffsets_key, __tinypy_memoryview_property, (void *)6, NULL);
    vm->memoryview_type->flags = (vm->memoryview_type->flags | TINYPY_TYPE_FLAG_IMMUTABLE) & ~TINYPY_TYPE_FLAG_BASE_TYPE;
}
