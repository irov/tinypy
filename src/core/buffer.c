#include "tinypy/buffer.h"

#include "internal.h"

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
        size_t code_points;

        const uint8_t *return_value_1 = (const uint8_t *)tinypy_unicode_utf8_view(owner, out_size, &code_points);
        return return_value_1;
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
tinypy_value_t *tinypy_buffer_from_object(tinypy_value_t *object, size_t offset, size_t size) {
    size_t owner_size;

    tinypy_vm_t *vm = TINYPY_VALUE_VM(object);
    (void)__tinypy_buffer_owner_view(object, &owner_size);
    tinypy_buffer_object_t *buffer = (tinypy_buffer_object_t *)tinypy_internal_value_allocate(vm, TINYPY_VALUE_BUFFER, sizeof(*buffer));
    buffer->owner = object;
    buffer->offset = offset;
    buffer->size = size;
    TINYPY_INCREF(object);
    return &buffer->base;
}
//////////////////////////////////////////////////////////////////////////
const void *tinypy_buffer_view(const tinypy_value_t *value, size_t *out_size) {
    const uint8_t *bytes;
    size_t owner_size;

    const tinypy_buffer_object_t *buffer = TINYPY_BUFFER_OBJECT((tinypy_value_t *)value);
    bytes = __tinypy_buffer_owner_view(buffer->owner, &owner_size);
    if (buffer->offset >= owner_size) {
        *out_size = 0U;
        return bytes != NULL ? bytes + owner_size : NULL;
    }
    owner_size -= buffer->offset;
    *out_size = buffer->size == TINYPY_BUFFER_TO_END || buffer->size > owner_size ? owner_size : buffer->size;
    return bytes != NULL ? bytes + buffer->offset : NULL;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_buffer_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data) {
    visit(TINYPY_BUFFER_OBJECT(value)->owner, user_data);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_buffer_integer_as_i64(tinypy_value_t *value, int64_t *out_value) {
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);

    if (kind == TINYPY_VALUE_BOOL || kind == TINYPY_VALUE_INTEGER) {
        *out_value = TINYPY_INTEGER_VALUE(value);
        return TINYPY_TRUE;
    }
    if (kind == TINYPY_VALUE_LONG) {
        const uint16_t *digits = TINYPY_LONG_OBJECT(value)->digits;
        size_t count = TINYPY_LONG_DIGIT_COUNT(value);
        uint64_t magnitude = UINT64_C(0);
        uint64_t limit = TINYPY_LONG_SIGN(value) < 0 ? (uint64_t)INT64_MAX + UINT64_C(1) : (uint64_t)INT64_MAX;
        size_t index;

        if (count > 5U) {
            return TINYPY_FALSE;
        }
        for (index = count; index != 0U; index -= 1U) {
            magnitude = (magnitude << 15U) | digits[index - 1U];
        }
        if (magnitude > limit) {
            return TINYPY_FALSE;
        }
        *out_value = TINYPY_LONG_SIGN(value) < 0
                         ? (magnitude == (uint64_t)INT64_MAX + UINT64_C(1) ? INT64_MIN : -(int64_t)magnitude)
                         : (int64_t)magnitude;
        return TINYPY_TRUE;
    }
    return TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_buffer_constructor_integer(tinypy_vm_t *vm, tinypy_value_t *value, int64_t *out_value, tinypy_error_t **out_error) {
    if (__tinypy_buffer_integer_as_i64(value, out_value) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "buffer offset and size must be integers", out_error);
        return TINYPY_FALSE;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_buffer_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = type->vm;
    size_t argument_count = TINYPY_TUPLE_SIZE(args);
    size_t owner_size;
    int64_t offset = INT64_C(0);
    int64_t requested_size = INT64_C(-1);

    if ((kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U) || argument_count < 1U || argument_count > 3U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "buffer constructor expects object, optional offset and size", out_error);
        return NULL;
    }
    tinypy_value_t *owner = TINYPY_TUPLE_GET(args, 0U);
    if (__tinypy_buffer_supported(owner) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "object does not support the buffer interface", out_error);
        return NULL;
    }
    tinypy_bool_t condition = argument_count >= 2U;
    if (condition != 0) {
        tinypy_value_t *item = TINYPY_TUPLE_GET(args, 1U);
        condition = __tinypy_buffer_constructor_integer(vm, item, &offset, out_error) == 0;
    }
    if (condition) {
        return NULL;
    }
    tinypy_bool_t condition_2 = argument_count >= 3U;
    if (condition_2 != 0) {
        tinypy_value_t *item = TINYPY_TUPLE_GET(args, 2U);
        condition_2 = __tinypy_buffer_constructor_integer(vm, item, &requested_size, out_error) == 0;
    }
    if (condition_2) {
        return NULL;
    }
    (void)__tinypy_buffer_owner_view(owner, &owner_size);
    if (offset < 0 || (uint64_t)offset > (uint64_t)owner_size) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "buffer offset is outside the source", out_error);
        return NULL;
    }
    if (requested_size < -1 || (requested_size >= 0 && (uint64_t)requested_size > (uint64_t)(owner_size - (size_t)offset))) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "buffer size is outside the source", out_error);
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
static tinypy_bool_t __tinypy_buffer_normalize_index(tinypy_vm_t *vm, tinypy_value_t *key, size_t size, size_t *out_index, tinypy_error_t **out_error) {
    int64_t index;

    if (__tinypy_buffer_integer_as_i64(key, &index) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "buffer index must be an integer", out_error);
        return TINYPY_FALSE;
    }
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
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_INDEX, "buffer index is out of range", out_error);
    return TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_buffer_slice_bound(tinypy_value_t *value, int64_t fallback, int64_t *out_value) {
    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_NONE) {
        *out_value = fallback;
        return TINYPY_TRUE;
    }
    tinypy_bool_t return_value_1 = __tinypy_buffer_integer_as_i64(value, out_value);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_buffer_slice(tinypy_value_t *value, tinypy_value_t *slice, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    size_t size;
    const uint8_t *bytes = (const uint8_t *)tinypy_buffer_view(value, &size);
    int64_t step;
    int64_t start;
    int64_t stop;
    size_t length = 0U;
    uint8_t *selected;
    int64_t source;
    size_t index;

    tinypy_value_t *slice_step = tinypy_slice_step(slice);
    if (__tinypy_buffer_slice_bound(slice_step, INT64_C(1), &step) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "slice indices must be integers", out_error);
        return NULL;
    }
    if (step == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "slice step cannot be zero", out_error);
        return NULL;
    }
    tinypy_value_t *slice_start = tinypy_slice_start(slice);
    if (__tinypy_buffer_slice_bound(slice_start, step < 0 ? (int64_t)size - 1 : 0, &start) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "slice indices must be integers", out_error);
        return NULL;
    }
    tinypy_value_t *slice_stop = tinypy_slice_stop(slice);
    if (__tinypy_buffer_slice_bound(slice_stop, step < 0 ? -1 : (int64_t)size, &stop) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "slice indices must be integers", out_error);
        return NULL;
    }
    if (start < 0) {
        start += (int64_t)size;
    }
    tinypy_bool_t condition_5 = stop < 0;
    if (condition_5 != 0) {
        tinypy_bool_t condition_6 = step < 0;
        if (condition_6 != 0) {
            tinypy_value_t *slice_stop_2 = tinypy_slice_stop(slice);
            condition_6 = TINYPY_VALUE_KIND(slice_stop_2) == TINYPY_VALUE_NONE;
        }
        condition_5 = !(condition_6);
    }
    if (condition_5) {
        stop += (int64_t)size;
    }
    if (step > 0) {
        if (start < 0) {
            start = 0;
        }
        if (stop < 0) {
            stop = 0;
        }
        if (start > (int64_t)size) {
            start = (int64_t)size;
        }
        if (stop > (int64_t)size) {
            stop = (int64_t)size;
        }
        if (start < stop) {
            length = (size_t)(1 + (stop - start - 1) / step);
        }
    }
    else {
        if (start < -1) {
            start = -1;
        }
        if (stop < -1) {
            stop = -1;
        }
        if (start >= (int64_t)size) {
            start = (int64_t)size - 1;
        }
        if (stop >= (int64_t)size) {
            stop = (int64_t)size - 1;
        }
        if (stop < start) {
            length = (size_t)(1 + (start - stop - 1) / -step);
        }
    }
    if (length == 0U) {
        tinypy_value_t *return_value_1 = tinypy_string_from_bytes(vm, NULL, 0U);
        return return_value_1;
    }
    if (step == 1) {
        tinypy_value_t *return_value_2 = tinypy_string_from_bytes(vm, bytes + (size_t)start, length);
        return return_value_2;
    }
    selected = (uint8_t *)tinypy_internal_vm_allocate(vm, length);
    source = start;
    for (index = 0U; index < length; ++index) {
        selected[index] = bytes[(size_t)source];
        source += step;
    }
    tinypy_value_t *result = tinypy_string_from_bytes(vm, selected, length);
    tinypy_internal_vm_deallocate(vm, selected, length);
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
    bytes = (const uint8_t *)tinypy_buffer_view(value, &size);
    if (__tinypy_buffer_normalize_index(vm, key, size, &index, out_error) == 0) {
        return NULL;
    }
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
tinypy_value_t *tinypy_internal_buffer_repr(tinypy_value_t *value, tinypy_error_t **out_error) {
    (void)value;
    TINYPY_CLEAR_ERROR(out_error);
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_value_t *return_value_1 = tinypy_string_from_bytes(vm, "<read-only buffer>", 18U);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_buffer_method_arguments(tinypy_vm_t *vm, tinypy_value_t *args, tinypy_value_t *kwargs, size_t count, tinypy_error_t **out_error) {
    if ((kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U) || TINYPY_TUPLE_SIZE(args) != count) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "buffer method received invalid arguments", out_error);
        return TINYPY_FALSE;
    }
    if (TINYPY_VALUE_KIND(TINYPY_TUPLE_GET(args, 0U)) != TINYPY_VALUE_BUFFER) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "buffer method requires a buffer object", out_error);
        return TINYPY_FALSE;
    }
    return TINYPY_TRUE;
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
        tinypy_value_t *result = &vm->not_implemented_object.base;
        TINYPY_INCREF(result);
        return result;
    }
    left_bytes = (const uint8_t *)tinypy_buffer_view(left, &left_size);
    right_bytes = __tinypy_buffer_owner_view(right, &right_size);
    if (right_size > SIZE_MAX - left_size || left_size + right_size >= (size_t)PTRDIFF_MAX) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "concatenated buffer is too large", out_error);
        return NULL;
    }
    total_size = left_size + right_size;
    tinypy_value_t *result = tinypy_internal_text_allocate_uninitialized(vm, TINYPY_VALUE_STRING, total_size, total_size, &output);
    if (result == NULL) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "concatenated buffer is too large", out_error);
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

    if (tinypy_internal_index_as_i64(count_value, &count, TINYPY_FALSE, out_error) == 0) {
        if (out_error == NULL || *out_error == NULL) {
            tinypy_value_t *result = &vm->not_implemented_object.base;
            TINYPY_INCREF(result);
            return result;
        }
        return NULL;
    }
    bytes = (const uint8_t *)tinypy_buffer_view(buffer, &unit_size);
    if (count <= 0 || unit_size == 0U) {
        tinypy_value_t *result = tinypy_string_from_bytes(vm, NULL, 0U);

        return result;
    }
    if ((uint64_t)count > (uint64_t)(SIZE_MAX / unit_size) || unit_size * (size_t)count >= (size_t)PTRDIFF_MAX) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "repeated buffer is too large", out_error);
        return NULL;
    }
    total_size = unit_size * (size_t)count;
    tinypy_value_t *result = tinypy_internal_text_allocate_uninitialized(vm, TINYPY_VALUE_STRING, total_size, total_size, &output);
    if (result == NULL) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "repeated buffer is too large", out_error);
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
    if (__tinypy_buffer_method_arguments(vm, args, kwargs, 1U, out_error) == 0) {
        return NULL;
    }
    ptrdiff_t length = tinypy_internal_buffer_length(TINYPY_TUPLE_GET(args, 0U), out_error);
    tinypy_value_t *result = tinypy_integer_from_i64(vm, (int64_t)length);

    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_buffer_getitem_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_buffer_method_arguments(vm, args, kwargs, 2U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *result = tinypy_internal_buffer_get_item(TINYPY_TUPLE_GET(args, 0U), TINYPY_TUPLE_GET(args, 1U), out_error);

    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_buffer_getslice_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_buffer_method_arguments(vm, args, kwargs, 3U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *slice = tinypy_slice_new(vm, TINYPY_TUPLE_GET(args, 1U), TINYPY_TUPLE_GET(args, 2U), NULL);
    tinypy_value_t *result = tinypy_internal_buffer_get_item(TINYPY_TUPLE_GET(args, 0U), slice, out_error);
    TINYPY_DECREF(slice);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_buffer_add_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_buffer_method_arguments(vm, args, kwargs, 2U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *result = __tinypy_buffer_concat(TINYPY_TUPLE_GET(args, 0U), TINYPY_TUPLE_GET(args, 1U), out_error);

    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_buffer_multiply_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_buffer_method_arguments(vm, args, kwargs, 2U, out_error) == 0) {
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
    if (__tinypy_buffer_method_arguments(vm, args, kwargs, 2U, out_error) == 0) {
        return NULL;
    }
    left = TINYPY_TUPLE_GET(args, 0U);
    right = TINYPY_TUPLE_GET(args, 1U);
    if (TINYPY_VALUE_KIND(right) != TINYPY_VALUE_BUFFER) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "buffer comparison requires another buffer", out_error);
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
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_buffer_method_arguments(vm, args, kwargs, 1U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *result = tinypy_internal_buffer_string(TINYPY_TUPLE_GET(args, 0U), out_error);

    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_buffer_repr_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_buffer_method_arguments(vm, args, kwargs, 1U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *result = tinypy_internal_buffer_repr(TINYPY_TUPLE_GET(args, 0U), out_error);

    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_buffer_readonly_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)args;
    (void)kwargs;
    (void)user_data;
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "buffer is read-only", out_error);
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_buffer_register_method(tinypy_vm_t *vm, const char *name, size_t name_size, tinypy_native_function_callback_t callback) {
    tinypy_value_t *function = tinypy_native_function_new(vm, name, name_size, callback, NULL, NULL);

    tinypy_type_set_attr(&vm->types[TINYPY_VALUE_BUFFER], name, name_size, function);
    TINYPY_DECREF(function);
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_initialize_buffer_type(tinypy_vm_t *vm) {
    __tinypy_buffer_register_method(vm, "__len__", 7U, __tinypy_buffer_len_method);
    __tinypy_buffer_register_method(vm, "__getitem__", 11U, __tinypy_buffer_getitem_method);
    __tinypy_buffer_register_method(vm, "__getslice__", 12U, __tinypy_buffer_getslice_method);
    __tinypy_buffer_register_method(vm, "__add__", 7U, __tinypy_buffer_add_method);
    __tinypy_buffer_register_method(vm, "__mul__", 7U, __tinypy_buffer_multiply_method);
    __tinypy_buffer_register_method(vm, "__rmul__", 8U, __tinypy_buffer_multiply_method);
    __tinypy_buffer_register_method(vm, "__cmp__", 7U, __tinypy_buffer_cmp_method);
    __tinypy_buffer_register_method(vm, "__str__", 7U, __tinypy_buffer_string_method);
    __tinypy_buffer_register_method(vm, "__repr__", 8U, __tinypy_buffer_repr_method);
    __tinypy_buffer_register_method(vm, "__setitem__", 11U, __tinypy_buffer_readonly_method);
    __tinypy_buffer_register_method(vm, "__setslice__", 12U, __tinypy_buffer_readonly_method);
    __tinypy_buffer_register_method(vm, "__delitem__", 11U, __tinypy_buffer_readonly_method);
    __tinypy_buffer_register_method(vm, "__delslice__", 12U, __tinypy_buffer_readonly_method);
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
    tinypy_value_t **dict_slot = tinypy_internal_object_dict_slot(result);

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
    *dict_slot = tinypy_dict_new(type->vm);
    tinypy_dict_set(*dict_slot, type->vm->builtins_key, owner);
    if (readonly == 0) {
        __tinypy_memoryview_export(result, 1);
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_memoryview_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = type->vm;
    size_t size;
    tinypy_bool_t readonly;

    if ((kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U) || TINYPY_TUPLE_SIZE(args) != 1U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "memoryview() requires exactly one argument", out_error);
        return NULL;
    }
    tinypy_value_t *owner = TINYPY_TUPLE_GET(args, 0U);
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
    tinypy_value_t *message = tinypy_string_from_bytes(vm, "", 0U);
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

    if (tinypy_internal_index_as_i64(key, &index, TINYPY_FALSE, out_error) == 0) {
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
    if (tinypy_internal_bytes_view(value, &replacement, &replacement_size) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "memoryview assignment requires a byte string", out_error);
        return TINYPY_FALSE;
    }
    size_t owner_offset = 0U;
    tinypy_value_t *owner = __tinypy_memoryview_writable_owner(instance, &owner_offset);
    if (owner == NULL) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "cannot modify read-only memory", out_error);
        return TINYPY_FALSE;
    }
    uint8_t *owner_bytes = TINYPY_BYTEARRAY_OBJECT(owner)->bytes + owner_offset;
    if (TINYPY_VALUE_KIND(key) == TINYPY_VALUE_SLICE) {
        tinypy_internal_slice_indices_t slice;

        if (tinypy_internal_slice_indices(key, size, &slice, out_error) == 0) {
            return TINYPY_FALSE;
        }
        if (slice.step != 1) {
            __tinypy_memoryview_not_implemented(vm, out_error);
            return TINYPY_FALSE;
        }
        if (replacement_size != slice.length) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "cannot modify size of memoryview object", out_error);
            return TINYPY_FALSE;
        }
        if (replacement_size != 0U) {
            (void)memmove(owner_bytes + (size_t)slice.start, replacement, replacement_size);
        }
        return TINYPY_TRUE;
    }
    size_t index;
    if (__tinypy_memoryview_index(instance, key, size, &index, out_error) == 0) {
        return TINYPY_FALSE;
    }
    if (replacement_size != 1U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "memoryview assignment requires a single byte", out_error);
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
        tinypy_value_t *result = tinypy_not_implemented_get(vm);

        return result;
    }
    left = tinypy_internal_memoryview_view(instance, &left_size);
    if (tinypy_internal_bytes_view(other, &right, &right_size) == 0) {
        tinypy_value_t *result = tinypy_not_implemented_get(vm);

        return result;
    }
    tinypy_bool_t equal = left_size == right_size && (left_size == 0U || memcmp(left, right, left_size) == 0) ? TINYPY_TRUE : TINYPY_FALSE;
    tinypy_value_t *result = tinypy_bool_from_i32(vm, operation == TINYPY_COMPARE_EQUAL ? equal : !equal);

    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_memoryview_method_arguments(tinypy_vm_t *vm, tinypy_value_t *args, tinypy_value_t *kwargs, size_t count, tinypy_error_t **out_error) {
    if ((kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U) || TINYPY_TUPLE_SIZE(args) != count) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "memoryview method received invalid arguments", out_error);
        return TINYPY_FALSE;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_memoryview_len_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_memoryview_method_arguments(vm, args, kwargs, 1U, out_error) == 0) {
        return NULL;
    }
    size_t size;
    (void)tinypy_internal_memoryview_view(TINYPY_TUPLE_GET(args, 0U), &size);
    tinypy_value_t *result = tinypy_integer_from_i64(vm, (int64_t)size);

    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_memoryview_get_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_memoryview_method_arguments(vm, args, kwargs, 2U, out_error) == 0) {
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
    if (__tinypy_memoryview_method_arguments(vm, args, kwargs, 3U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *self = TINYPY_TUPLE_GET(args, 0U);
    if (__tinypy_memoryview_set(self, __tinypy_memoryview_payload(self), TINYPY_TUPLE_GET(args, 1U), TINYPY_TUPLE_GET(args, 2U), NULL, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *result = tinypy_none_get(vm);

    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_memoryview_delete_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_memoryview_method_arguments(vm, args, kwargs, 2U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *self = TINYPY_TUPLE_GET(args, 0U);
    if (__tinypy_memoryview_set(self, __tinypy_memoryview_payload(self), TINYPY_TUPLE_GET(args, 1U), NULL, NULL, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *result = tinypy_none_get(vm);

    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_memoryview_tobytes_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_memoryview_method_arguments(vm, args, kwargs, 1U, out_error) == 0) {
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
    if (__tinypy_memoryview_method_arguments(vm, args, kwargs, 1U, out_error) == 0) {
        return NULL;
    }
    size_t size;
    const uint8_t *bytes = tinypy_internal_memoryview_view(TINYPY_TUPLE_GET(args, 0U), &size);
    tinypy_value_t *result = tinypy_list_from_items(vm, NULL, 0U);
    size_t index;
    for (index = 0U; index < size; ++index) {
        tinypy_value_t *item = tinypy_integer_from_i64(vm, bytes[index]);

        tinypy_list_append(result, item);
        TINYPY_DECREF(item);
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_memoryview_property(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    intptr_t field = (intptr_t)user_data;

    if (__tinypy_memoryview_method_arguments(vm, args, kwargs, 1U, out_error) == 0) {
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
        tinypy_value_t *result = tinypy_none_get(vm);

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
static void __tinypy_memoryview_register_method(tinypy_vm_t *vm, const char *name, size_t name_size, tinypy_native_function_callback_t callback) {
    tinypy_value_t *function = tinypy_native_function_new(vm, name, name_size, callback, NULL, NULL);

    tinypy_type_set_attr(vm->memoryview_type, name, name_size, function);
    TINYPY_DECREF(function);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_memoryview_register_property(tinypy_vm_t *vm, const char *name, size_t name_size, intptr_t field) {
    tinypy_value_t *getter = tinypy_native_function_new(vm, name, name_size, __tinypy_memoryview_property, (void *)field, NULL);
    tinypy_value_t *descriptor = tinypy_property_new(vm, getter, NULL, NULL, NULL);

    tinypy_type_set_attr(vm->memoryview_type, name, name_size, descriptor);
    TINYPY_DECREF(descriptor);
    TINYPY_DECREF(getter);
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_initialize_memoryview_type(tinypy_vm_t *vm) {
    tinypy_native_type_spec_t spec;

    tinypy_native_type_spec_init(&spec);
    spec.payload_size = sizeof(tinypy_internal_memoryview_payload_t);
    spec.finalize = __tinypy_memoryview_finalize;
    spec.repr = __tinypy_memoryview_repr;
    spec.hash = __tinypy_memoryview_hash;
    spec.compare = __tinypy_memoryview_compare;
    spec.mapping_get = __tinypy_memoryview_get;
    spec.mapping_set = __tinypy_memoryview_set;
    spec.mapping_length = __tinypy_memoryview_length;
    vm->memoryview_type = tinypy_native_type_new(vm, "memoryview", 10U, NULL, 0U, NULL, &spec, NULL);
    vm->memoryview_type->create = __tinypy_memoryview_create;
    vm->memoryview_type->has_instance_dict = INT32_C(0);
    __tinypy_memoryview_register_method(vm, "__len__", 7U, __tinypy_memoryview_len_method);
    __tinypy_memoryview_register_method(vm, "__getitem__", 11U, __tinypy_memoryview_get_method);
    __tinypy_memoryview_register_method(vm, "__setitem__", 11U, __tinypy_memoryview_set_method);
    __tinypy_memoryview_register_method(vm, "__delitem__", 11U, __tinypy_memoryview_delete_method);
    __tinypy_memoryview_register_method(vm, "tobytes", 7U, __tinypy_memoryview_tobytes_method);
    __tinypy_memoryview_register_method(vm, "tolist", 6U, __tinypy_memoryview_tolist_method);
    __tinypy_memoryview_register_property(vm, "format", 6U, 0);
    __tinypy_memoryview_register_property(vm, "itemsize", 8U, 1);
    __tinypy_memoryview_register_property(vm, "ndim", 4U, 2);
    __tinypy_memoryview_register_property(vm, "readonly", 8U, 3);
    __tinypy_memoryview_register_property(vm, "shape", 5U, 4);
    __tinypy_memoryview_register_property(vm, "strides", 7U, 5);
    __tinypy_memoryview_register_property(vm, "suboffsets", 10U, 6);
    vm->memoryview_type->flags = (vm->memoryview_type->flags | TINYPY_TYPE_FLAG_IMMUTABLE) & ~TINYPY_TYPE_FLAG_BASE_TYPE;
}
