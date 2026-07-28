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
    if (size == TINYPY_BUFFER_TO_END) {
        size = owner_size - offset;
    }
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
    *out_size = buffer->size;
    return bytes + buffer->offset;
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
    size_t size = TINYPY_BUFFER_OBJECT(value)->size;

    TINYPY_CLEAR_ERROR(out_error);
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
    const uint8_t *bytes = (const uint8_t *)tinypy_buffer_view(value, &(size_t){0U});
    size_t size = TINYPY_BUFFER_OBJECT(value)->size;
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
    selected = (uint8_t *)tinypy_internal_vm_allocate(vm, length, TINYPY_ALLOC_TAG_TEMPORARY);
    source = start;
    for (index = 0U; index < length; ++index) {
        selected[index] = bytes[(size_t)source];
        source += step;
    }
    tinypy_value_t *result = tinypy_string_from_bytes(vm, selected, length);
    tinypy_internal_vm_deallocate(vm, selected, length, TINYPY_ALLOC_TAG_TEMPORARY);
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
