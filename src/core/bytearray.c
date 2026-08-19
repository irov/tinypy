#include "tinypy/bytearray.h"

#include "internal.h"

#include <string.h>

static void __tinypy_bytearray_reserve(tinypy_value_t *value, size_t minimum) {
    tinypy_bytearray_object_t *bytearray = TINYPY_BYTEARRAY_OBJECT(value);
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    size_t capacity;

    if (minimum <= bytearray->capacity) {
        return;
    }
    capacity = bytearray->capacity == 0U ? 16U : bytearray->capacity;
    while (capacity < minimum) {
        size_t half = capacity >> 1U;

        if (capacity > SIZE_MAX - half - 1U) {
            capacity = minimum;
            break;
        }
        capacity += half + 1U;
    }
    if (bytearray->bytes == NULL) {
        bytearray->bytes = (uint8_t *)tinypy_internal_vm_allocate(vm, capacity);
    }
    else {
        bytearray->bytes = (uint8_t *)tinypy_internal_vm_reallocate(vm, bytearray->bytes, bytearray->capacity, capacity);
    }
    bytearray->capacity = capacity;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_bytes_view(const tinypy_value_t *value, const uint8_t **out_bytes, size_t *out_size) {
    switch (TINYPY_VALUE_KIND(value)) {
    case TINYPY_VALUE_STRING:
        *out_bytes = (const uint8_t *)tinypy_string_view(value, out_size);
        return TINYPY_TRUE;
    case TINYPY_VALUE_BYTEARRAY:
        *out_bytes = (const uint8_t *)tinypy_bytearray_view(value, out_size);
        return TINYPY_TRUE;
    case TINYPY_VALUE_BUFFER:
        *out_bytes = (const uint8_t *)tinypy_buffer_view(value, out_size);
        return TINYPY_TRUE;
    default:
        return TINYPY_FALSE;
    }
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_bytearray_item(tinypy_vm_t *vm, tinypy_value_t *value, uint8_t *out_byte, tinypy_error_t **out_error) {
    int64_t integer;

    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_STRING && TINYPY_SIZED_SIZE(value) == 1U) {
        *out_byte = TINYPY_STRING_OBJECT(value)->bytes[0];
        return TINYPY_TRUE;
    }
    if (tinypy_internal_index_as_i64(value, &integer, TINYPY_FALSE, out_error) == 0) {
        return TINYPY_FALSE;
    }
    if (integer >= 0 && integer <= 255) {
        *out_byte = (uint8_t)integer;
        return TINYPY_TRUE;
    }
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "byte must be in range(0, 256)", out_error);
    return TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_bytearray_collect(tinypy_vm_t *vm, tinypy_value_t *source, uint8_t **out_bytes, size_t *out_size, tinypy_error_t **out_error) {
    const uint8_t *view;
    size_t view_size;
    uint8_t *bytes = NULL;
    size_t size = 0U;
    size_t capacity = 0U;
    tinypy_error_t *iteration_error = NULL;

    *out_bytes = NULL;
    *out_size = 0U;
    if (tinypy_internal_bytes_view(source, &view, &view_size) != 0) {
        if (view_size != 0U) {
            bytes = (uint8_t *)tinypy_internal_vm_allocate(vm, view_size);
            (void)memcpy(bytes, view, view_size);
        }
        *out_bytes = bytes;
        *out_size = view_size;
        return TINYPY_TRUE;
    }
    tinypy_value_t *iterator = tinypy_iter(source, out_error);
    if (iterator == NULL) {
        return TINYPY_FALSE;
    }
    for (;;) {
        tinypy_value_t *item = tinypy_next(iterator, &iteration_error);
        uint8_t byte;

        if (item == NULL) {
            break;
        }
        if (__tinypy_bytearray_item(vm, item, &byte, out_error) == 0) {
            TINYPY_DECREF(item);
            TINYPY_DECREF(iterator);
            if (bytes != NULL) {
                tinypy_internal_vm_deallocate(vm, bytes, capacity);
            }
            return TINYPY_FALSE;
        }
        TINYPY_DECREF(item);
        if (size == capacity) {
            size_t new_capacity;

            if (capacity == 0U) {
                new_capacity = 16U;
            }
            else if (capacity > SIZE_MAX / 2U) {
                if (bytes != NULL) {
                    tinypy_internal_vm_deallocate(vm, bytes, capacity);
                }
                TINYPY_DECREF(iterator);
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "bytearray is too large", out_error);
                return TINYPY_FALSE;
            }
            else {
                new_capacity = capacity * 2U;
            }

            if (bytes == NULL) {
                bytes = (uint8_t *)tinypy_internal_vm_allocate(vm, new_capacity);
            }
            else {
                bytes = (uint8_t *)tinypy_internal_vm_reallocate(vm, bytes, capacity, new_capacity);
            }
            capacity = new_capacity;
        }
        bytes[size] = byte;
        size += 1U;
    }
    TINYPY_DECREF(iterator);
    if (iteration_error != NULL) {
        if (bytes != NULL) {
            tinypy_internal_vm_deallocate(vm, bytes, capacity);
        }
        if (out_error != NULL) {
            *out_error = iteration_error;
        }
        else {
            tinypy_error_release(iteration_error);
        }
        return TINYPY_FALSE;
    }
    if (size != 0U && capacity != size) {
        bytes = (uint8_t *)tinypy_internal_vm_reallocate(vm, bytes, capacity, size);
    }
    *out_bytes = bytes;
    *out_size = size;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_bytearray_index(tinypy_vm_t *vm, tinypy_value_t *key, size_t size, size_t *out_index, tinypy_error_t **out_error) {
    int64_t index;

    if (tinypy_internal_index_as_i64(key, &index, TINYPY_FALSE, out_error) == 0) {
        return TINYPY_FALSE;
    }
    if (index < 0) {
        uint64_t distance = (uint64_t)(-(index + INT64_C(1))) + UINT64_C(1);

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
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_INDEX, "bytearray index out of range", out_error);
    return TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_bytearray_replace_slice(tinypy_value_t *value, const tinypy_internal_slice_indices_t *slice, const uint8_t *replacement, size_t replacement_size) {
    tinypy_bytearray_object_t *bytearray = TINYPY_BYTEARRAY_OBJECT(value);
    size_t old_size = TINYPY_SIZED_SIZE(value);
    size_t index;

    if (slice->step == 1) {
        size_t new_size;
        size_t tail_start = (size_t)slice->start + slice->length;
        size_t tail_size = old_size - tail_start;

        new_size = old_size - slice->length + replacement_size;
        __tinypy_bytearray_reserve(value, new_size);
        if (tail_size != 0U && replacement_size != slice->length) {
            (void)memmove(bytearray->bytes + (size_t)slice->start + replacement_size, bytearray->bytes + tail_start, tail_size);
        }
        if (replacement_size != 0U) {
            (void)memcpy(bytearray->bytes + (size_t)slice->start, replacement, replacement_size);
        }
        TINYPY_SIZED_SIZE(value) = new_size;
        return;
    }
    for (index = 0U; index < replacement_size; ++index) {
        bytearray->bytes[(size_t)(slice->start + (int64_t)index * slice->step)] = replacement[index];
    }
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_bytearray_delete_index(tinypy_value_t *value, size_t index) {
    tinypy_bytearray_object_t *bytearray = TINYPY_BYTEARRAY_OBJECT(value);
    size_t size = TINYPY_SIZED_SIZE(value);

    if (index + 1U < size) {
        (void)memmove(bytearray->bytes + index, bytearray->bytes + index + 1U, size - index - 1U);
    }
    TINYPY_SIZED_SIZE(value) -= 1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_bytearray_allocate(tinypy_vm_t *vm, size_t size) {
    tinypy_bytearray_object_t *bytearray = (tinypy_bytearray_object_t *)tinypy_internal_value_allocate(vm, TINYPY_VALUE_BYTEARRAY, sizeof(*bytearray));
    if (size != 0U) {
        bytearray->bytes = (uint8_t *)tinypy_internal_vm_allocate(vm, size);
        bytearray->capacity = size;
    }
    bytearray->base.size = size;
    return &bytearray->base.base;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_bytearray_adopt(tinypy_vm_t *vm, uint8_t *bytes, size_t size) {
    tinypy_bytearray_object_t *bytearray = (tinypy_bytearray_object_t *)tinypy_internal_value_allocate(vm, TINYPY_VALUE_BYTEARRAY, sizeof(*bytearray));

    bytearray->bytes = bytes;
    bytearray->capacity = size;
    bytearray->base.size = size;
    return &bytearray->base.base;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_bytearray_from_bytes(tinypy_vm_t *vm, const void *bytes, size_t size) {
    tinypy_value_t *result = __tinypy_bytearray_allocate(vm, size);

    if (size != 0U) {
        tinypy_bytearray_object_t *bytearray = TINYPY_BYTEARRAY_OBJECT(result);
        (void)memcpy(bytearray->bytes, bytes, size);
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
size_t tinypy_bytearray_size(const tinypy_value_t *value) {
    size_t return_value_1 = TINYPY_SIZED_SIZE(value);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
const void *tinypy_bytearray_view(const tinypy_value_t *value, size_t *out_size) {
    *out_size = TINYPY_SIZED_SIZE(value);
    const void *return_value_1 = TINYPY_BYTEARRAY_OBJECT((tinypy_value_t *)value)->bytes;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_bytearray_set(tinypy_value_t *value, size_t index, uint8_t byte) {
    TINYPY_BYTEARRAY_OBJECT(value)->bytes[index] = byte;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_bytearray_destroy(tinypy_value_t *value) {
    tinypy_bytearray_object_t *bytearray = TINYPY_BYTEARRAY_OBJECT(value);

    if (bytearray->bytes == NULL) {
        return;
    }
    tinypy_internal_vm_deallocate(TINYPY_VALUE_VM(value), bytearray->bytes, bytearray->capacity);
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_bytearray_swap_contents(tinypy_value_t *left, tinypy_value_t *right) {
    size_t size = TINYPY_SIZED_SIZE(left);
    size_t capacity = TINYPY_BYTEARRAY_OBJECT(left)->capacity;
    uint8_t *bytes = TINYPY_BYTEARRAY_OBJECT(left)->bytes;

    TINYPY_SIZED_SIZE(left) = TINYPY_SIZED_SIZE(right);
    TINYPY_BYTEARRAY_OBJECT(left)->capacity = TINYPY_BYTEARRAY_OBJECT(right)->capacity;
    TINYPY_BYTEARRAY_OBJECT(left)->bytes = TINYPY_BYTEARRAY_OBJECT(right)->bytes;
    TINYPY_SIZED_SIZE(right) = size;
    TINYPY_BYTEARRAY_OBJECT(right)->capacity = capacity;
    TINYPY_BYTEARRAY_OBJECT(right)->bytes = bytes;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_bytearray_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = type->vm;
    uint8_t *bytes;
    size_t size;
    int64_t requested_size;

    if ((kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U) || TINYPY_TUPLE_SIZE(args) > 3U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "bytearray constructor received invalid arguments", out_error);
        return NULL;
    }
    if (TINYPY_TUPLE_SIZE(args) == 0U) {
        tinypy_value_t *return_value_1 = tinypy_bytearray_from_bytes(vm, NULL, 0U);
        return return_value_1;
    }
    tinypy_value_t *source = TINYPY_TUPLE_GET(args, 0U);
    size_t argument_count = TINYPY_TUPLE_SIZE(args);
    if (argument_count >= 2U) {
        if (TINYPY_VALUE_KIND(source) != TINYPY_VALUE_UNICODE) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "encoding without a unicode argument", out_error);
            return NULL;
        }
        tinypy_value_t *encode = tinypy_object_get_attr(source, "encode", 6U, out_error);
        if (encode == NULL) {
            return NULL;
        }
        tinypy_value_t *encoding_args[2];
        encoding_args[0] = TINYPY_TUPLE_GET(args, 1U);
        if (argument_count == 3U) {
            encoding_args[1] = TINYPY_TUPLE_GET(args, 2U);
        }
        tinypy_value_t *encode_args = tinypy_tuple_from_items(vm, encoding_args, argument_count - 1U);
        tinypy_value_t *encoded = tinypy_call(encode, encode_args, NULL, out_error);
        TINYPY_DECREF(encode_args);
        TINYPY_DECREF(encode);
        if (encoded == NULL) {
            return NULL;
        }
        tinypy_value_t *result = tinypy_bytearray_from_bytes(vm, TINYPY_TEXT_BYTES(encoded), TINYPY_TEXT_BYTE_SIZE(encoded));
        TINYPY_DECREF(encoded);
        return result;
    }
    if (TINYPY_VALUE_KIND(source) == TINYPY_VALUE_UNICODE) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "string argument without an encoding", out_error);
        return NULL;
    }
    tinypy_value_type_e source_kind = TINYPY_VALUE_KIND(source);
    if (source_kind == TINYPY_VALUE_BOOL || source_kind == TINYPY_VALUE_INTEGER || source_kind == TINYPY_VALUE_LONG || tinypy_internal_object_has_special(source, "__index__", 9U) != 0) {
        if (tinypy_internal_index_as_i64(source, &requested_size, TINYPY_FALSE, out_error) == 0) {
            return NULL;
        }
        if (requested_size < 0) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "negative count", out_error);
            return NULL;
        }
        tinypy_value_t *result = tinypy_bytearray_from_bytes(vm, NULL, 0U);
        if (requested_size != 0) {
            __tinypy_bytearray_reserve(result, (size_t)requested_size);
            (void)memset(TINYPY_BYTEARRAY_OBJECT(result)->bytes, 0, (size_t)requested_size);
            TINYPY_SIZED_SIZE(result) = (size_t)requested_size;
        }
        return result;
    }
    if (__tinypy_bytearray_collect(vm, source, &bytes, &size, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *return_value_1 = __tinypy_bytearray_adopt(vm, bytes, size);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
ptrdiff_t tinypy_internal_bytearray_length(tinypy_value_t *value, tinypy_error_t **out_error) {
    TINYPY_CLEAR_ERROR(out_error);
    ptrdiff_t return_value_1 = TINYPY_SIZED_SIZE(value);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_bytearray_get_item(tinypy_value_t *value, tinypy_value_t *key, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    size_t size = TINYPY_SIZED_SIZE(value);
    size_t index;

    if (TINYPY_VALUE_KIND(key) == TINYPY_VALUE_SLICE) {
        tinypy_internal_slice_indices_t slice;
        tinypy_value_t *result;
        size_t selected_index;

        if (tinypy_internal_slice_indices(key, size, &slice, out_error) == 0) {
            return NULL;
        }
        if (slice.length == 0U) {
            tinypy_value_t *return_value_1 = tinypy_bytearray_from_bytes(vm, NULL, 0U);
            return return_value_1;
        }
        if (slice.step == 1) {
            tinypy_value_t *return_value_2 = tinypy_bytearray_from_bytes(vm, TINYPY_BYTEARRAY_OBJECT(value)->bytes + (size_t)slice.start, slice.length);
            return return_value_2;
        }
        result = __tinypy_bytearray_allocate(vm, slice.length);
        for (selected_index = 0U; selected_index < slice.length; ++selected_index) {
            TINYPY_BYTEARRAY_OBJECT(result)->bytes[selected_index] = TINYPY_BYTEARRAY_OBJECT(value)->bytes[(size_t)(slice.start + (int64_t)selected_index * slice.step)];
        }
        return result;
    }
    if (__tinypy_bytearray_index(vm, key, size, &index, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *return_value_3 = tinypy_integer_from_i64(vm, (int64_t)TINYPY_BYTEARRAY_OBJECT(value)->bytes[index]);
    return return_value_3;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_bytearray_set_item(tinypy_value_t *value, tinypy_value_t *key, tinypy_value_t *item, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    size_t size = TINYPY_SIZED_SIZE(value);
    size_t index;

    if (TINYPY_VALUE_KIND(key) == TINYPY_VALUE_SLICE) {
        tinypy_internal_slice_indices_t slice;
        uint8_t *replacement = NULL;
        size_t replacement_size = 0U;
        size_t deletion_index;

        if (tinypy_internal_slice_indices(key, size, &slice, out_error) == 0) {
            return TINYPY_FALSE;
        }
        if (item != NULL) {
            if (__tinypy_bytearray_collect(vm, item, &replacement, &replacement_size, out_error) == 0) {
                return TINYPY_FALSE;
            }
            if (slice.step != 1 && replacement_size != slice.length) {
                if (replacement != NULL) {
                    tinypy_internal_vm_deallocate(vm, replacement, replacement_size);
                }
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "extended slice assignment has the wrong size", out_error);
                return TINYPY_FALSE;
            }
            if (slice.step == 1 && replacement_size > SIZE_MAX - (size - slice.length)) {
                if (replacement != NULL) {
                    tinypy_internal_vm_deallocate(vm, replacement, replacement_size);
                }
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "bytearray is too large", out_error);
                return TINYPY_FALSE;
            }
            __tinypy_bytearray_replace_slice(value, &slice, replacement, replacement_size);
            if (replacement != NULL) {
                tinypy_internal_vm_deallocate(vm, replacement, replacement_size);
            }
            return TINYPY_TRUE;
        }
        if (slice.step == 1) {
            __tinypy_bytearray_replace_slice(value, &slice, NULL, 0U);
            return TINYPY_TRUE;
        }
        if (slice.step > 0) {
            for (deletion_index = slice.length; deletion_index != 0U; deletion_index -= 1U) {
                __tinypy_bytearray_delete_index(value, (size_t)(slice.start + (int64_t)(deletion_index - 1U) * slice.step));
            }
        }
        else {
            for (deletion_index = 0U; deletion_index < slice.length; ++deletion_index) {
                __tinypy_bytearray_delete_index(value, (size_t)(slice.start + (int64_t)deletion_index * slice.step));
            }
        }
        return TINYPY_TRUE;
    }
    if (__tinypy_bytearray_index(vm, key, size, &index, out_error) == 0) {
        return TINYPY_FALSE;
    }
    if (item == NULL) {
        __tinypy_bytearray_delete_index(value, index);
        return TINYPY_TRUE;
    }
    tinypy_bool_t return_value_1 = __tinypy_bytearray_item(vm, item, &TINYPY_BYTEARRAY_OBJECT(value)->bytes[index], out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_bytearray_string(tinypy_value_t *value, tinypy_error_t **out_error) {
    TINYPY_CLEAR_ERROR(out_error);
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_value_t *return_value_1 = tinypy_string_from_bytes(vm, TINYPY_BYTEARRAY_OBJECT(value)->bytes, TINYPY_SIZED_SIZE(value));
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_bytearray_repr(tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_value_t *string = tinypy_internal_bytearray_string(value, out_error);
    const uint8_t *quoted_bytes;
    size_t quoted_size;
    uint8_t *bytes;

    if (string == NULL) {
        return NULL;
    }
    tinypy_value_t *quoted = tinypy_object_repr(string, out_error);
    TINYPY_DECREF(string);
    if (quoted == NULL) {
        return NULL;
    }
    quoted_bytes = (const uint8_t *)tinypy_string_view(quoted, &quoted_size);
    bytes = (uint8_t *)tinypy_internal_vm_allocate(vm, quoted_size + 12U);
    (void)memcpy(bytes, "bytearray(b", 11U);
    (void)memcpy(bytes + 11U, quoted_bytes, quoted_size);
    bytes[quoted_size + 11U] = (uint8_t)')';
    tinypy_value_t *result = tinypy_string_from_bytes(vm, bytes, quoted_size + 12U);
    tinypy_internal_vm_deallocate(vm, bytes, quoted_size + 12U);
    TINYPY_DECREF(quoted);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_bytearray_method_arguments(tinypy_vm_t *vm, tinypy_value_t *args, tinypy_value_t *kwargs, size_t minimum, size_t maximum, tinypy_error_t **out_error) {
    size_t count = TINYPY_TUPLE_SIZE(args);

    if ((kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U) || count < minimum || count > maximum) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "bytearray method received invalid arguments", out_error);
        return TINYPY_FALSE;
    }
    tinypy_value_t *item = TINYPY_TUPLE_GET(args, 0U);
    if (TINYPY_VALUE_KIND(item) != TINYPY_VALUE_BYTEARRAY) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "bytearray method requires a bytearray", out_error);
        return TINYPY_FALSE;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_bytearray_add_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    const uint8_t *right_bytes;
    size_t left_size;
    size_t right_size;

    (void)user_data;
    if (__tinypy_bytearray_method_arguments(vm, args, kwargs, 2U, 2U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *left = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *item = TINYPY_TUPLE_GET(args, 1U);
    if (tinypy_internal_bytes_view(item, &right_bytes, &right_size) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "cannot concatenate bytearray with this value", out_error);
        return NULL;
    }
    left_size = TINYPY_SIZED_SIZE(left);
    if (right_size > SIZE_MAX - left_size) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "bytearray is too large", out_error);
        return NULL;
    }
    if (left_size + right_size == 0U) {
        tinypy_value_t *return_value_1 = tinypy_bytearray_from_bytes(vm, NULL, 0U);
        return return_value_1;
    }
    tinypy_value_t *result = __tinypy_bytearray_allocate(vm, left_size + right_size);
    uint8_t *bytes = TINYPY_BYTEARRAY_OBJECT(result)->bytes;
    if (left_size != 0U) {
        (void)memcpy(bytes, TINYPY_BYTEARRAY_OBJECT(left)->bytes, left_size);
    }
    if (right_size != 0U) {
        (void)memcpy(bytes + left_size, right_bytes, right_size);
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_bytearray_multiply_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    int64_t count;
    size_t unit_size;
    size_t total_size;

    (void)user_data;
    if (__tinypy_bytearray_method_arguments(vm, args, kwargs, 2U, 2U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *value = TINYPY_TUPLE_GET(args, 0U);
    if (tinypy_internal_index_as_i64(TINYPY_TUPLE_GET(args, 1U), &count, TINYPY_FALSE, out_error) == 0) {
        return NULL;
    }
    unit_size = TINYPY_SIZED_SIZE(value);
    if (count <= 0 || unit_size == 0U) {
        tinypy_value_t *return_value_1 = __tinypy_bytearray_allocate(vm, 0U);
        return return_value_1;
    }
    if ((uint64_t)count > (uint64_t)(SIZE_MAX / unit_size)) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "repeated bytearray is too large", out_error);
        return NULL;
    }
    total_size = unit_size * (size_t)count;
    tinypy_value_t *result = __tinypy_bytearray_allocate(vm, total_size);
    (void)memcpy(TINYPY_BYTEARRAY_OBJECT(result)->bytes, TINYPY_BYTEARRAY_OBJECT(value)->bytes, unit_size);
    size_t copied = unit_size;
    while (copied < total_size) {
        size_t chunk = copied < total_size - copied ? copied : total_size - copied;

        (void)memcpy(TINYPY_BYTEARRAY_OBJECT(result)->bytes + copied, TINYPY_BYTEARRAY_OBJECT(result)->bytes, chunk);
        copied += chunk;
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_bytearray_inplace_add_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    const uint8_t *right_bytes;
    size_t right_size;

    (void)user_data;
    if (__tinypy_bytearray_method_arguments(vm, args, kwargs, 2U, 2U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *left = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *right = TINYPY_TUPLE_GET(args, 1U);
    if (left == right) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_BUFFER, "Existing exports of data: object cannot be re-sized", out_error);
        return NULL;
    }
    if (tinypy_internal_bytes_view(right, &right_bytes, &right_size) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "cannot concatenate bytearray with this value", out_error);
        return NULL;
    }
    size_t left_size = TINYPY_SIZED_SIZE(left);
    if (right_size > SIZE_MAX - left_size) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "bytearray is too large", out_error);
        return NULL;
    }
    __tinypy_bytearray_reserve(left, left_size + right_size);
    uint8_t *left_bytes = TINYPY_BYTEARRAY_OBJECT(left)->bytes;
    if (right_size != 0U) {
        (void)memcpy(left_bytes + left_size, right_bytes, right_size);
    }
    TINYPY_SIZED_SIZE(left) = left_size + right_size;
    TINYPY_INCREF(left);
    return left;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_bytearray_inplace_multiply_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    int64_t count;

    (void)user_data;
    if (__tinypy_bytearray_method_arguments(vm, args, kwargs, 2U, 2U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *value = TINYPY_TUPLE_GET(args, 0U);
    if (tinypy_internal_index_as_i64(TINYPY_TUPLE_GET(args, 1U), &count, TINYPY_FALSE, out_error) == 0) {
        return NULL;
    }
    size_t unit_size = TINYPY_SIZED_SIZE(value);
    if (count <= 0) {
        TINYPY_SIZED_SIZE(value) = 0U;
    }
    else if (count > 1 && unit_size != 0U) {
        if ((uint64_t)count > (uint64_t)(SIZE_MAX / unit_size)) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "repeated bytearray is too large", out_error);
            return NULL;
        }
        size_t total_size = unit_size * (size_t)count;
        __tinypy_bytearray_reserve(value, total_size);
        uint8_t *bytes = TINYPY_BYTEARRAY_OBJECT(value)->bytes;
        size_t copied = unit_size;
        while (copied < total_size) {
            size_t chunk = copied < total_size - copied ? copied : total_size - copied;

            (void)memcpy(bytes + copied, bytes, chunk);
            copied += chunk;
        }
        TINYPY_SIZED_SIZE(value) = total_size;
    }
    TINYPY_INCREF(value);
    return value;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_bytearray_append_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    uint8_t byte;
    size_t size;

    (void)user_data;
    if (__tinypy_bytearray_method_arguments(vm, args, kwargs, 2U, 2U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *item = TINYPY_TUPLE_GET(args, 1U);
    if (__tinypy_bytearray_item(vm, item, &byte, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *value = TINYPY_TUPLE_GET(args, 0U);
    size = TINYPY_SIZED_SIZE(value);
    if (size == SIZE_MAX) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "bytearray is too large", out_error);
        return NULL;
    }
    __tinypy_bytearray_reserve(value, size + 1U);
    TINYPY_BYTEARRAY_OBJECT(value)->bytes[size] = byte;
    TINYPY_SIZED_SIZE(value) += 1;
    tinypy_value_t *return_value_1 = tinypy_none_get(vm);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_bytearray_extend_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    uint8_t *extension;
    size_t extension_size;
    size_t size;

    (void)user_data;
    if (__tinypy_bytearray_method_arguments(vm, args, kwargs, 2U, 2U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *item = TINYPY_TUPLE_GET(args, 1U);
    if (__tinypy_bytearray_collect(vm, item, &extension, &extension_size, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *value = TINYPY_TUPLE_GET(args, 0U);
    size = TINYPY_SIZED_SIZE(value);
    if (extension_size > SIZE_MAX - size) {
        if (extension != NULL) {
            tinypy_internal_vm_deallocate(vm, extension, extension_size);
        }
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "bytearray is too large", out_error);
        return NULL;
    }
    __tinypy_bytearray_reserve(value, size + extension_size);
    if (extension_size != 0U) {
        (void)memcpy(TINYPY_BYTEARRAY_OBJECT(value)->bytes + size, extension, extension_size);
    }
    TINYPY_SIZED_SIZE(value) = size + extension_size;
    if (extension != NULL) {
        tinypy_internal_vm_deallocate(vm, extension, extension_size);
    }
    tinypy_value_t *return_value_1 = tinypy_none_get(vm);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_bytearray_bound(tinypy_value_t *value, size_t size, int64_t fallback, tinypy_bool_t clamp_high, int64_t *out_bound, tinypy_error_t **out_error) {
    int64_t bound;

    if (value == NULL || TINYPY_VALUE_KIND(value) == TINYPY_VALUE_NONE) {
        *out_bound = fallback;
        return TINYPY_TRUE;
    }
    if (tinypy_internal_index_as_i64(value, &bound, TINYPY_TRUE, out_error) == 0) {
        return TINYPY_FALSE;
    }
    if (bound < 0) {
        bound = bound < -(int64_t)size ? 0 : bound + (int64_t)size;
    }
    if (clamp_high != 0 && (uint64_t)bound > (uint64_t)size) {
        bound = (int64_t)size;
    }
    *out_bound = bound;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_bytearray_find_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    const uint8_t *needle;
    size_t size;
    size_t needle_size;
    int64_t start;
    int64_t stop;
    ptrdiff_t found = -1;
    size_t argument_count;
    tinypy_value_t *stop_value;

    (void)user_data;
    if (__tinypy_bytearray_method_arguments(vm, args, kwargs, 2U, 4U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *value = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *item = TINYPY_TUPLE_GET(args, 1U);
    if (tinypy_internal_bytes_view(item, &needle, &needle_size) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "substring must support the buffer interface", out_error);
        return NULL;
    }
    size = TINYPY_SIZED_SIZE(value);
    argument_count = TINYPY_TUPLE_SIZE(args);
    tinypy_value_t *start_value = argument_count >= 3U ? TINYPY_TUPLE_GET(args, 2U) : NULL;
    if (__tinypy_bytearray_bound(start_value, size, 0, TINYPY_FALSE, &start, out_error) == 0) {
        return NULL;
    }
    stop_value = argument_count >= 4U ? TINYPY_TUPLE_GET(args, 3U) : NULL;
    if (__tinypy_bytearray_bound(stop_value, size, (int64_t)size, TINYPY_TRUE, &stop, out_error) == 0) {
        return NULL;
    }
    if (start <= stop && needle_size <= (size_t)(stop - start)) {
        found = tinypy_internal_find_bytes(TINYPY_BYTEARRAY_OBJECT(value)->bytes + (size_t)start, (size_t)(stop - start), needle, needle_size, TINYPY_FALSE);
        if (found >= 0) {
            found += (ptrdiff_t)start;
        }
    }
    tinypy_value_t *return_value_1 = tinypy_integer_from_i64(vm, (int64_t)found);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
typedef enum tinypy_bytearray_bridge_result_e {
    TINYPY_BYTEARRAY_BRIDGE_DIRECT = 0,
    TINYPY_BYTEARRAY_BRIDGE_VALUE = 1,
    TINYPY_BYTEARRAY_BRIDGE_LIST = 2,
    TINYPY_BYTEARRAY_BRIDGE_TUPLE = 3
} tinypy_bytearray_bridge_result_e;
//////////////////////////////////////////////////////////////////////////
typedef struct tinypy_bytearray_bridge_spec_t {
    const char *name;
    size_t name_size;
    tinypy_bytearray_bridge_result_e result;
    tinypy_bool_t join;
} tinypy_bytearray_bridge_spec_t;
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_bytearray_bridge_argument(tinypy_vm_t *vm, tinypy_value_t *value, tinypy_error_t **out_error) {
    const uint8_t *bytes;
    size_t size;

    if (tinypy_internal_bytes_view(value, &bytes, &size) != 0) {
        tinypy_value_t *return_value_1 = tinypy_string_from_bytes(vm, bytes, size);
        return return_value_1;
    }
    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_TUPLE) {
        size_t count = TINYPY_TUPLE_SIZE(value);
        tinypy_value_t **items = count != 0U ? (tinypy_value_t **)tinypy_internal_vm_allocate(vm, count * sizeof(*items)) : NULL;
        size_t index;

        for (index = 0U; index < count; ++index) {
            items[index] = __tinypy_bytearray_bridge_argument(vm, TINYPY_TUPLE_GET(value, index), out_error);
            if (items[index] == NULL) {
                while (index != 0U) {
                    TINYPY_DECREF(items[--index]);
                }
                if (items != NULL) {
                    tinypy_internal_vm_deallocate(vm, items, count * sizeof(*items));
                }
                return NULL;
            }
        }
        tinypy_value_t *result = tinypy_tuple_from_items(vm, items, count);
        while (count != 0U) {
            TINYPY_DECREF(items[--count]);
        }
        if (items != NULL) {
            tinypy_internal_vm_deallocate(vm, items, TINYPY_TUPLE_SIZE(result) * sizeof(*items));
        }
        return result;
    }
    TINYPY_INCREF(value);
    return value;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_bytearray_bridge_iterable(tinypy_vm_t *vm, tinypy_value_t *iterable, tinypy_error_t **out_error) {
    tinypy_value_t *iterator = tinypy_iter(iterable, out_error);
    tinypy_error_t *iteration_error = NULL;

    if (iterator == NULL) {
        return NULL;
    }
    tinypy_value_t *result = tinypy_list_from_items(vm, NULL, 0U);
    tinypy_internal_list_reserve(vm, result, tinypy_internal_iterable_size_hint(iterable));
    for (;;) {
        tinypy_value_t *item = tinypy_next(iterator, &iteration_error);
        tinypy_value_t *converted;

        if (item == NULL) {
            break;
        }
        converted = __tinypy_bytearray_bridge_argument(vm, item, out_error);
        TINYPY_DECREF(item);
        if (converted == NULL) {
            TINYPY_DECREF(iterator);
            TINYPY_DECREF(result);
            return NULL;
        }
        tinypy_list_append(result, converted);
        TINYPY_DECREF(converted);
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
static tinypy_value_t *__tinypy_bytearray_bridge_value(tinypy_vm_t *vm, tinypy_value_t *value, tinypy_bytearray_bridge_result_e result_kind, tinypy_error_t **out_error) {
    if (result_kind == TINYPY_BYTEARRAY_BRIDGE_DIRECT) {
        return value;
    }
    if (result_kind == TINYPY_BYTEARRAY_BRIDGE_VALUE) {
        const uint8_t *bytes;
        size_t size;

        if (tinypy_internal_bytes_view(value, &bytes, &size) == 0) {
            TINYPY_DECREF(value);
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "bytearray method returned a non-byte string", out_error);
            return NULL;
        }
        tinypy_value_t *result = tinypy_bytearray_from_bytes(vm, bytes, size);
        TINYPY_DECREF(value);
        return result;
    }
    size_t count = result_kind == TINYPY_BYTEARRAY_BRIDGE_LIST ? TINYPY_LIST_SIZE(value) : TINYPY_TUPLE_SIZE(value);
    tinypy_value_t **items = count != 0U ? (tinypy_value_t **)tinypy_internal_vm_allocate(vm, count * sizeof(*items)) : NULL;
    size_t index;

    for (index = 0U; index < count; ++index) {
        tinypy_value_t *item = result_kind == TINYPY_BYTEARRAY_BRIDGE_LIST ? TINYPY_LIST_GET(value, index) : TINYPY_TUPLE_GET(value, index);
        const uint8_t *bytes;
        size_t size;

        if (tinypy_internal_bytes_view(item, &bytes, &size) == 0) {
            while (index != 0U) {
                TINYPY_DECREF(items[--index]);
            }
            if (items != NULL) {
                tinypy_internal_vm_deallocate(vm, items, count * sizeof(*items));
            }
            TINYPY_DECREF(value);
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "bytearray method returned a non-byte string", out_error);
            return NULL;
        }
        items[index] = tinypy_bytearray_from_bytes(vm, bytes, size);
    }
    tinypy_value_t *result = result_kind == TINYPY_BYTEARRAY_BRIDGE_LIST ? tinypy_list_from_items(vm, items, count) : tinypy_tuple_from_items(vm, items, count);
    while (count != 0U) {
        TINYPY_DECREF(items[--count]);
    }
    if (items != NULL) {
        size_t result_count = result_kind == TINYPY_BYTEARRAY_BRIDGE_LIST ? TINYPY_LIST_SIZE(result) : TINYPY_TUPLE_SIZE(result);
        tinypy_internal_vm_deallocate(vm, items, result_count * sizeof(*items));
    }
    TINYPY_DECREF(value);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_bytearray_ascii_transform(tinypy_vm_t *vm, tinypy_value_t *self, const char *name, size_t name_size) {
    size_t size = TINYPY_SIZED_SIZE(self);
    tinypy_value_t *result = tinypy_bytearray_from_bytes(vm, TINYPY_BYTEARRAY_OBJECT(self)->bytes, size);
    uint8_t *bytes = TINYPY_BYTEARRAY_OBJECT(result)->bytes;
    size_t index;

    for (index = 0U; index < size; ++index) {
        uint8_t byte = bytes[index];

        if (name_size == 5U && memcmp(name, "lower", 5U) == 0) {
            if (byte >= (uint8_t)'A' && byte <= (uint8_t)'Z') {
                bytes[index] = (uint8_t)(byte + ((uint8_t)'a' - (uint8_t)'A'));
            }
        }
        else if (name_size == 5U && memcmp(name, "upper", 5U) == 0) {
            if (byte >= (uint8_t)'a' && byte <= (uint8_t)'z') {
                bytes[index] = (uint8_t)(byte - ((uint8_t)'a' - (uint8_t)'A'));
            }
        }
        else if (byte >= (uint8_t)'A' && byte <= (uint8_t)'Z') {
            bytes[index] = (uint8_t)(byte + ((uint8_t)'a' - (uint8_t)'A'));
        }
        else if (byte >= (uint8_t)'a' && byte <= (uint8_t)'z') {
            bytes[index] = (uint8_t)(byte - ((uint8_t)'a' - (uint8_t)'A'));
        }
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_bytearray_bridge_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    const tinypy_bytearray_bridge_spec_t *spec = (const tinypy_bytearray_bridge_spec_t *)user_data;
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    size_t argument_count = TINYPY_TUPLE_SIZE(args);
    tinypy_value_t *converted[3] = {NULL, NULL, NULL};
    size_t index;

    if (__tinypy_bytearray_method_arguments(vm, args, kwargs, 1U, 4U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *self = TINYPY_TUPLE_GET(args, 0U);
    if (argument_count == 1U && ((spec->name_size == 5U && (memcmp(spec->name, "lower", 5U) == 0 || memcmp(spec->name, "upper", 5U) == 0)) || (spec->name_size == 8U && memcmp(spec->name, "swapcase", 8U) == 0))) {
        tinypy_value_t *return_value_1 = __tinypy_bytearray_ascii_transform(vm, self, spec->name, spec->name_size);
        return return_value_1;
    }
    tinypy_value_t *string = tinypy_string_from_bytes(vm, TINYPY_BYTEARRAY_OBJECT(self)->bytes, TINYPY_SIZED_SIZE(self));
    tinypy_value_t *method = tinypy_object_get_attr(string, spec->name, spec->name_size, out_error);
    if (method == NULL) {
        TINYPY_DECREF(string);
        return NULL;
    }
    for (index = 1U; index < argument_count; ++index) {
        tinypy_value_t *value = TINYPY_TUPLE_GET(args, index);

        converted[index - 1U] = spec->join != 0 && index == 1U
                                     ? __tinypy_bytearray_bridge_iterable(vm, value, out_error)
                                     : __tinypy_bytearray_bridge_argument(vm, value, out_error);
        if (converted[index - 1U] == NULL) {
            while (index > 1U) {
                TINYPY_DECREF(converted[--index - 1U]);
            }
            TINYPY_DECREF(method);
            TINYPY_DECREF(string);
            return NULL;
        }
    }
    tinypy_value_t *method_args = tinypy_tuple_from_items(vm, converted, argument_count - 1U);
    tinypy_value_t *value = tinypy_call(method, method_args, NULL, out_error);
    TINYPY_DECREF(method_args);
    while (argument_count > 1U) {
        TINYPY_DECREF(converted[--argument_count - 1U]);
    }
    TINYPY_DECREF(method);
    TINYPY_DECREF(string);
    if (value == NULL) {
        return NULL;
    }
    tinypy_value_t *return_value_1 = __tinypy_bytearray_bridge_value(vm, value, spec->result, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_bytearray_plain_index(tinypy_value_t *value, int64_t *out_index, tinypy_error_t **out_error) {
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);

    if (kind != TINYPY_VALUE_BOOL && kind != TINYPY_VALUE_INTEGER && kind != TINYPY_VALUE_LONG) {
        tinypy_internal_make_vm_error(TINYPY_VALUE_VM(value), TINYPY_ERROR_TYPE, "an integer is required", out_error);
        return TINYPY_FALSE;
    }
    tinypy_bool_t return_value_1 = tinypy_internal_index_as_i64(value, out_index, TINYPY_FALSE, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_bytearray_insert_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    int64_t index;
    uint8_t byte;

    (void)user_data;
    if (__tinypy_bytearray_method_arguments(vm, args, kwargs, 3U, 3U, out_error) == 0 || __tinypy_bytearray_plain_index(TINYPY_TUPLE_GET(args, 1U), &index, out_error) == 0 || __tinypy_bytearray_item(vm, TINYPY_TUPLE_GET(args, 2U), &byte, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *value = TINYPY_TUPLE_GET(args, 0U);
    size_t size = TINYPY_SIZED_SIZE(value);
    if (size == SIZE_MAX) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "bytearray is too large", out_error);
        return NULL;
    }
    if (index < 0) {
        index = index < -(int64_t)size ? 0 : index + (int64_t)size;
    }
    if ((uint64_t)index > (uint64_t)size) {
        index = (int64_t)size;
    }
    __tinypy_bytearray_reserve(value, size + 1U);
    if ((size_t)index < size) {
        (void)memmove(TINYPY_BYTEARRAY_OBJECT(value)->bytes + (size_t)index + 1U, TINYPY_BYTEARRAY_OBJECT(value)->bytes + (size_t)index, size - (size_t)index);
    }
    TINYPY_BYTEARRAY_OBJECT(value)->bytes[(size_t)index] = byte;
    TINYPY_SIZED_SIZE(value) = size + 1U;
    tinypy_value_t *return_value_1 = tinypy_none_get(vm);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_bytearray_pop_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    int64_t index;

    (void)user_data;
    if (__tinypy_bytearray_method_arguments(vm, args, kwargs, 1U, 2U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *value = TINYPY_TUPLE_GET(args, 0U);
    size_t size = TINYPY_SIZED_SIZE(value);
    if (size == 0U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_INDEX, "pop from empty bytearray", out_error);
        return NULL;
    }
    if (TINYPY_TUPLE_SIZE(args) == 1U) {
        index = (int64_t)size - 1;
    }
    else if (__tinypy_bytearray_plain_index(TINYPY_TUPLE_GET(args, 1U), &index, out_error) == 0) {
        return NULL;
    }
    if (index < 0) {
        index += (int64_t)size;
    }
    if (index < 0 || (uint64_t)index >= (uint64_t)size) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_INDEX, "pop index out of range", out_error);
        return NULL;
    }
    uint8_t byte = TINYPY_BYTEARRAY_OBJECT(value)->bytes[(size_t)index];
    __tinypy_bytearray_delete_index(value, (size_t)index);
    tinypy_value_t *return_value_1 = tinypy_integer_from_i64(vm, byte);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_bytearray_remove_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    uint8_t byte;
    size_t index;

    (void)user_data;
    if (__tinypy_bytearray_method_arguments(vm, args, kwargs, 2U, 2U, out_error) == 0 || __tinypy_bytearray_item(vm, TINYPY_TUPLE_GET(args, 1U), &byte, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *value = TINYPY_TUPLE_GET(args, 0U);
    for (index = 0U; index < TINYPY_SIZED_SIZE(value); ++index) {
        if (TINYPY_BYTEARRAY_OBJECT(value)->bytes[index] == byte) {
            __tinypy_bytearray_delete_index(value, index);
            tinypy_value_t *return_value_1 = tinypy_none_get(vm);
            return return_value_1;
        }
    }
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "value not found in bytearray", out_error);
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_bytearray_reverse_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    size_t left = 0U;
    size_t right;

    (void)user_data;
    if (__tinypy_bytearray_method_arguments(vm, args, kwargs, 1U, 1U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *value = TINYPY_TUPLE_GET(args, 0U);
    right = TINYPY_SIZED_SIZE(value);
    while (left < right && left < --right) {
        uint8_t byte = TINYPY_BYTEARRAY_OBJECT(value)->bytes[left];

        TINYPY_BYTEARRAY_OBJECT(value)->bytes[left] = TINYPY_BYTEARRAY_OBJECT(value)->bytes[right];
        TINYPY_BYTEARRAY_OBJECT(value)->bytes[right] = byte;
        left += 1U;
    }
    tinypy_value_t *return_value_1 = tinypy_none_get(vm);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_bytearray_alloc_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_bytearray_method_arguments(vm, args, kwargs, 1U, 1U, out_error) == 0) {
        return NULL;
    }
    tinypy_bytearray_object_t *self = TINYPY_BYTEARRAY_OBJECT(TINYPY_TUPLE_GET(args, 0U));
    size_t allocation = self->capacity != 0U ? self->capacity + 1U : 0U;
    tinypy_value_t *return_value_1 = tinypy_integer_from_i64(vm, (int64_t)allocation);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_bytearray_hex_digit(uint8_t byte) {
    if (byte >= (uint8_t)'0' && byte <= (uint8_t)'9') {
        return (int32_t)(byte - (uint8_t)'0');
    }
    if (byte >= (uint8_t)'a' && byte <= (uint8_t)'f') {
        return (int32_t)(byte - (uint8_t)'a') + 10;
    }
    if (byte >= (uint8_t)'A' && byte <= (uint8_t)'F') {
        return (int32_t)(byte - (uint8_t)'A') + 10;
    }
    return -1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_bytearray_hex_space(uint8_t byte) {
    return byte == (uint8_t)' ' || byte == (uint8_t)'\t' || byte == (uint8_t)'\n' || byte == (uint8_t)'\r' || byte == (uint8_t)'\v' || byte == (uint8_t)'\f' ? TINYPY_TRUE : TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_bytearray_fromhex_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    const uint8_t *text;
    size_t text_size;
    size_t input = 0U;
    size_t output = 0U;

    (void)user_data;
    if ((kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U) || TINYPY_TUPLE_SIZE(args) != 2U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "fromhex() requires one string argument", out_error);
        return NULL;
    }
    tinypy_value_t *class_value = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *source = TINYPY_TUPLE_GET(args, 1U);
    if (TINYPY_VALUE_KIND(class_value) != TINYPY_VALUE_TYPE || (TINYPY_VALUE_KIND(source) != TINYPY_VALUE_STRING && TINYPY_VALUE_KIND(source) != TINYPY_VALUE_UNICODE)) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "fromhex() requires one string argument", out_error);
        return NULL;
    }
    text = TINYPY_TEXT_BYTES(source);
    text_size = TINYPY_TEXT_BYTE_SIZE(source);
    tinypy_value_t *result = __tinypy_bytearray_allocate(vm, text_size / 2U);
    while (input < text_size) {
        int32_t high;
        int32_t low;

        while (input < text_size && __tinypy_bytearray_hex_space(text[input]) != 0) {
            input += 1U;
        }
        if (input == text_size) {
            break;
        }
        high = __tinypy_bytearray_hex_digit(text[input++]);
        if (high < 0 || input == text_size || (low = __tinypy_bytearray_hex_digit(text[input++])) < 0) {
            TINYPY_DECREF(result);
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "non-hexadecimal number found in fromhex() arg", out_error);
            return NULL;
        }
        TINYPY_BYTEARRAY_OBJECT(result)->bytes[output++] = (uint8_t)((high << 4) | low);
    }
    TINYPY_SIZED_SIZE(result) = output;
    return result;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_bytearray_register_method(tinypy_vm_t *vm, const char *name, size_t name_size, tinypy_native_function_callback_t callback) {
    tinypy_value_t *function = tinypy_native_function_new(vm, name, name_size, callback, NULL, NULL);
    tinypy_value_t *key = tinypy_string_from_bytes(vm, name, name_size);

    tinypy_dict_set(vm->types[TINYPY_VALUE_BYTEARRAY].dict, key, function);
    TINYPY_DECREF(key);
    TINYPY_DECREF(function);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_bytearray_register_bridge(tinypy_vm_t *vm, const tinypy_bytearray_bridge_spec_t *spec) {
    tinypy_value_t *function = tinypy_native_function_new(vm, spec->name, spec->name_size, __tinypy_bytearray_bridge_method, (void *)spec, NULL);

    tinypy_type_set_attr(&vm->types[TINYPY_VALUE_BYTEARRAY], spec->name, spec->name_size, function);
    TINYPY_DECREF(function);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_bytearray_register_class_method(tinypy_vm_t *vm, const char *name, size_t name_size, tinypy_native_function_callback_t callback) {
    tinypy_value_t *function = tinypy_native_function_new(vm, name, name_size, callback, NULL, NULL);
    tinypy_value_t *descriptor = tinypy_class_method_new(function);

    tinypy_type_set_attr(&vm->types[TINYPY_VALUE_BYTEARRAY], name, name_size, descriptor);
    TINYPY_DECREF(descriptor);
    TINYPY_DECREF(function);
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_initialize_bytearray_methods(tinypy_vm_t *vm) {
    static const tinypy_bytearray_bridge_spec_t bridge_specs[] = {
        {"capitalize", 10U, TINYPY_BYTEARRAY_BRIDGE_VALUE, TINYPY_FALSE},
        {"center", 6U, TINYPY_BYTEARRAY_BRIDGE_VALUE, TINYPY_FALSE},
        {"count", 5U, TINYPY_BYTEARRAY_BRIDGE_DIRECT, TINYPY_FALSE},
        {"decode", 6U, TINYPY_BYTEARRAY_BRIDGE_DIRECT, TINYPY_FALSE},
        {"endswith", 8U, TINYPY_BYTEARRAY_BRIDGE_DIRECT, TINYPY_FALSE},
        {"expandtabs", 10U, TINYPY_BYTEARRAY_BRIDGE_VALUE, TINYPY_FALSE},
        {"index", 5U, TINYPY_BYTEARRAY_BRIDGE_DIRECT, TINYPY_FALSE},
        {"isalnum", 7U, TINYPY_BYTEARRAY_BRIDGE_DIRECT, TINYPY_FALSE},
        {"isalpha", 7U, TINYPY_BYTEARRAY_BRIDGE_DIRECT, TINYPY_FALSE},
        {"isdigit", 7U, TINYPY_BYTEARRAY_BRIDGE_DIRECT, TINYPY_FALSE},
        {"islower", 7U, TINYPY_BYTEARRAY_BRIDGE_DIRECT, TINYPY_FALSE},
        {"isspace", 7U, TINYPY_BYTEARRAY_BRIDGE_DIRECT, TINYPY_FALSE},
        {"istitle", 7U, TINYPY_BYTEARRAY_BRIDGE_DIRECT, TINYPY_FALSE},
        {"isupper", 7U, TINYPY_BYTEARRAY_BRIDGE_DIRECT, TINYPY_FALSE},
        {"join", 4U, TINYPY_BYTEARRAY_BRIDGE_VALUE, TINYPY_TRUE},
        {"ljust", 5U, TINYPY_BYTEARRAY_BRIDGE_VALUE, TINYPY_FALSE},
        {"lower", 5U, TINYPY_BYTEARRAY_BRIDGE_VALUE, TINYPY_FALSE},
        {"lstrip", 6U, TINYPY_BYTEARRAY_BRIDGE_VALUE, TINYPY_FALSE},
        {"partition", 9U, TINYPY_BYTEARRAY_BRIDGE_TUPLE, TINYPY_FALSE},
        {"replace", 7U, TINYPY_BYTEARRAY_BRIDGE_VALUE, TINYPY_FALSE},
        {"rfind", 5U, TINYPY_BYTEARRAY_BRIDGE_DIRECT, TINYPY_FALSE},
        {"rindex", 6U, TINYPY_BYTEARRAY_BRIDGE_DIRECT, TINYPY_FALSE},
        {"rjust", 5U, TINYPY_BYTEARRAY_BRIDGE_VALUE, TINYPY_FALSE},
        {"rpartition", 10U, TINYPY_BYTEARRAY_BRIDGE_TUPLE, TINYPY_FALSE},
        {"rsplit", 6U, TINYPY_BYTEARRAY_BRIDGE_LIST, TINYPY_FALSE},
        {"rstrip", 6U, TINYPY_BYTEARRAY_BRIDGE_VALUE, TINYPY_FALSE},
        {"split", 5U, TINYPY_BYTEARRAY_BRIDGE_LIST, TINYPY_FALSE},
        {"splitlines", 10U, TINYPY_BYTEARRAY_BRIDGE_LIST, TINYPY_FALSE},
        {"startswith", 10U, TINYPY_BYTEARRAY_BRIDGE_DIRECT, TINYPY_FALSE},
        {"strip", 5U, TINYPY_BYTEARRAY_BRIDGE_VALUE, TINYPY_FALSE},
        {"swapcase", 8U, TINYPY_BYTEARRAY_BRIDGE_VALUE, TINYPY_FALSE},
        {"title", 5U, TINYPY_BYTEARRAY_BRIDGE_VALUE, TINYPY_FALSE},
        {"translate", 9U, TINYPY_BYTEARRAY_BRIDGE_VALUE, TINYPY_FALSE},
        {"upper", 5U, TINYPY_BYTEARRAY_BRIDGE_VALUE, TINYPY_FALSE},
        {"zfill", 5U, TINYPY_BYTEARRAY_BRIDGE_VALUE, TINYPY_FALSE}
    };
    tinypy_value_t *hash_key = tinypy_string_from_bytes(vm, "__hash__", 8U);
    size_t index;

    __tinypy_bytearray_register_method(vm, "__add__", 7U, __tinypy_bytearray_add_method);
    __tinypy_bytearray_register_method(vm, "__mul__", 7U, __tinypy_bytearray_multiply_method);
    __tinypy_bytearray_register_method(vm, "__rmul__", 8U, __tinypy_bytearray_multiply_method);
    __tinypy_bytearray_register_method(vm, "__iadd__", 8U, __tinypy_bytearray_inplace_add_method);
    __tinypy_bytearray_register_method(vm, "__imul__", 8U, __tinypy_bytearray_inplace_multiply_method);
    __tinypy_bytearray_register_method(vm, "append", 6U, __tinypy_bytearray_append_method);
    __tinypy_bytearray_register_method(vm, "extend", 6U, __tinypy_bytearray_extend_method);
    __tinypy_bytearray_register_method(vm, "find", 4U, __tinypy_bytearray_find_method);
    __tinypy_bytearray_register_method(vm, "insert", 6U, __tinypy_bytearray_insert_method);
    __tinypy_bytearray_register_method(vm, "pop", 3U, __tinypy_bytearray_pop_method);
    __tinypy_bytearray_register_method(vm, "remove", 6U, __tinypy_bytearray_remove_method);
    __tinypy_bytearray_register_method(vm, "reverse", 7U, __tinypy_bytearray_reverse_method);
    __tinypy_bytearray_register_method(vm, "__alloc__", 9U, __tinypy_bytearray_alloc_method);
    __tinypy_bytearray_register_class_method(vm, "fromhex", 7U, __tinypy_bytearray_fromhex_method);
    for (index = 0U; index < sizeof(bridge_specs) / sizeof(bridge_specs[0]); ++index) {
        __tinypy_bytearray_register_bridge(vm, &bridge_specs[index]);
    }
    tinypy_dict_set(vm->types[TINYPY_VALUE_BYTEARRAY].dict, hash_key, &vm->none_object.base);
    TINYPY_DECREF(hash_key);
}
