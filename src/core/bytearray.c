#include "tinypy/bytearray.h"

#include "internal.h"

#include <stdio.h>
#include <string.h>

tinypy_bool_t tinypy_internal_bytearray_resize_allowed(tinypy_value_t *value, size_t size, tinypy_error_t **out_error) {
    if (size != TINYPY_SIZED_SIZE(value) && TINYPY_BYTEARRAY_OBJECT(value)->exports != 0U) {
        tinypy_internal_make_vm_error(TINYPY_VALUE_VM(value), TINYPY_ERROR_BUFFER, "Existing exports of data: object cannot be re-sized", out_error);
        return TINYPY_FALSE;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_bytearray_reserve_checked(tinypy_value_t *value, size_t minimum, tinypy_error_t **out_error) {
    tinypy_bytearray_object_t *bytearray = TINYPY_BYTEARRAY_OBJECT(value);
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    size_t capacity;

    if (minimum <= bytearray->capacity) {
        return TINYPY_TRUE;
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
        uint8_t *bytes = (uint8_t *)tinypy_internal_vm_allocate_checked(vm, capacity, out_error);

        if (bytes == NULL) {
            return TINYPY_FALSE;
        }
        bytearray->bytes = bytes;
    }
    else {
        uint8_t *bytes = (uint8_t *)tinypy_internal_vm_reallocate_checked(vm, bytearray->bytes, bytearray->capacity, capacity, out_error);

        if (bytes == NULL) {
            return TINYPY_FALSE;
        }
        bytearray->bytes = bytes;
    }
    bytearray->capacity = capacity;
    return TINYPY_TRUE;
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
        if (tinypy_internal_memoryview_check(value) != 0) {
            *out_bytes = tinypy_internal_memoryview_view(value, out_size);
            return TINYPY_TRUE;
        }
        return TINYPY_FALSE;
    }
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_bytearray_item(tinypy_vm_t *vm, tinypy_value_t *value, uint8_t *out_byte, tinypy_error_t **out_error) {
    int64_t integer;

    if (value->type == &vm->types[TINYPY_VALUE_STRING]) {
        if (TINYPY_SIZED_SIZE(value) != 1U) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "string must be of size 1", out_error);
            return TINYPY_FALSE;
        }
        *out_byte = TINYPY_STRING_OBJECT(value)->bytes[0];
        return TINYPY_TRUE;
    }
    tinypy_error_t *index_error = NULL;
    if (tinypy_internal_index_as_i64(value, &integer, TINYPY_TRUE, &index_error) == TINYPY_FALSE) {
        if (index_error != NULL && tinypy_error_kind(index_error) == TINYPY_ERROR_TYPE) {
            tinypy_error_release(index_error);
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "an integer or string of size 1 is required", out_error);
        }
        else if (out_error != NULL) {
            *out_error = index_error;
        }
        else {
            tinypy_error_release(index_error);
        }
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
            bytes = (uint8_t *)tinypy_internal_vm_allocate_checked(vm, view_size, out_error);
            if (bytes == NULL) {
                return TINYPY_FALSE;
            }
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
                bytes = (uint8_t *)tinypy_internal_vm_allocate_checked(vm, new_capacity, out_error);
            }
            else {
                uint8_t *resized = (uint8_t *)tinypy_internal_vm_reallocate_checked(vm, bytes, capacity, new_capacity, out_error);

                if (resized == NULL) {
                    tinypy_internal_vm_deallocate(vm, bytes, capacity);
                    TINYPY_DECREF(iterator);
                    return TINYPY_FALSE;
                }
                bytes = resized;
            }
            if (bytes == NULL) {
                TINYPY_DECREF(iterator);
                return TINYPY_FALSE;
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
        uint8_t *resized = (uint8_t *)tinypy_internal_vm_reallocate_checked(vm, bytes, capacity, size, out_error);

        if (resized == NULL) {
            tinypy_internal_vm_deallocate(vm, bytes, capacity);
            return TINYPY_FALSE;
        }
        bytes = resized;
    }
    *out_bytes = bytes;
    *out_size = size;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_bytearray_extend_iterable(tinypy_value_t *value, tinypy_value_t *source, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    const uint8_t *view;
    size_t view_size;

    if (tinypy_internal_bytes_view(source, &view, &view_size) != 0) {
        size_t size = TINYPY_SIZED_SIZE(value);

        if (view_size > SIZE_MAX - size) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "bytearray is too large", out_error);
            return TINYPY_FALSE;
        }
        if (tinypy_internal_bytearray_resize_allowed(value, size + view_size, out_error) == 0) {
            return TINYPY_FALSE;
        }
        if (__tinypy_bytearray_reserve_checked(value, size + view_size, out_error) == 0) {
            return TINYPY_FALSE;
        }
        if (view_size != 0U) {
            (void)memmove(TINYPY_BYTEARRAY_OBJECT(value)->bytes + size, view, view_size);
        }
        TINYPY_SIZED_SIZE(value) = size + view_size;
        return TINYPY_TRUE;
    }
    tinypy_value_t *iterator = tinypy_iter(source, out_error);
    tinypy_error_t *iteration_error = NULL;

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
            return TINYPY_FALSE;
        }
        TINYPY_DECREF(item);
        size_t size = TINYPY_SIZED_SIZE(value);
        if (size == SIZE_MAX) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "bytearray is too large", out_error);
            TINYPY_DECREF(iterator);
            return TINYPY_FALSE;
        }
        if (tinypy_internal_bytearray_resize_allowed(value, size + 1U, out_error) == 0) {
            TINYPY_DECREF(iterator);
            return TINYPY_FALSE;
        }
        if (__tinypy_bytearray_reserve_checked(value, size + 1U, out_error) == 0) {
            TINYPY_DECREF(iterator);
            return TINYPY_FALSE;
        }
        TINYPY_BYTEARRAY_OBJECT(value)->bytes[size] = byte;
        TINYPY_SIZED_SIZE(value) = size + 1U;
    }
    TINYPY_DECREF(iterator);
    if (iteration_error != NULL) {
        if (out_error != NULL) {
            *out_error = iteration_error;
        }
        else {
            tinypy_error_release(iteration_error);
        }
        return TINYPY_FALSE;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_bytearray_index(tinypy_vm_t *vm, tinypy_value_t *key, tinypy_value_t *value, size_t *out_index, tinypy_error_t **out_error) {
    int64_t index;

    if (tinypy_internal_index_as_i64(key, &index, TINYPY_TRUE, out_error) == 0) {
        return TINYPY_FALSE;
    }
    size_t size = TINYPY_SIZED_SIZE(value);
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
static tinypy_bool_t __tinypy_bytearray_replace_slice(tinypy_value_t *value, const tinypy_internal_slice_indices_t *slice, const uint8_t *replacement, size_t replacement_size, tinypy_error_t **out_error) {
    tinypy_bytearray_object_t *bytearray = TINYPY_BYTEARRAY_OBJECT(value);
    size_t old_size = TINYPY_SIZED_SIZE(value);
    size_t index;

    if (slice->step == 1) {
        size_t new_size;
        size_t tail_start = (size_t)slice->start + slice->length;
        size_t tail_size = old_size - tail_start;

        new_size = old_size - slice->length + replacement_size;
        if (__tinypy_bytearray_reserve_checked(value, new_size, out_error) == 0) {
            return TINYPY_FALSE;
        }
        if (tail_size != 0U && replacement_size != slice->length) {
            (void)memmove(bytearray->bytes + (size_t)slice->start + replacement_size, bytearray->bytes + tail_start, tail_size);
        }
        if (replacement_size != 0U) {
            (void)memcpy(bytearray->bytes + (size_t)slice->start, replacement, replacement_size);
        }
        TINYPY_SIZED_SIZE(value) = new_size;
        return TINYPY_TRUE;
    }
    for (index = 0U; index < replacement_size; ++index) {
        bytearray->bytes[(size_t)(slice->start + (int64_t)index * slice->step)] = replacement[index];
    }
    return TINYPY_TRUE;
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
static tinypy_value_t *__tinypy_bytearray_allocate_checked(tinypy_vm_t *vm, size_t size, tinypy_error_t **out_error) {
    tinypy_bytearray_object_t *bytearray = (tinypy_bytearray_object_t *)tinypy_internal_object_allocate_checked(vm, &vm->types[TINYPY_VALUE_BYTEARRAY], sizeof(*bytearray), out_error);

    if (bytearray == NULL) {
        return NULL;
    }
    if (size != 0U) {
        bytearray->bytes = (uint8_t *)tinypy_internal_vm_allocate_checked(vm, size, out_error);
        if (bytearray->bytes == NULL) {
            TINYPY_DECREF(&bytearray->base.base);
            return NULL;
        }
        bytearray->capacity = size;
    }
    bytearray->base.size = size;
    return &bytearray->base.base;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_bytearray_adopt_checked(tinypy_vm_t *vm, uint8_t *bytes, size_t size, tinypy_error_t **out_error) {
    tinypy_bytearray_object_t *bytearray = (tinypy_bytearray_object_t *)tinypy_internal_object_allocate_checked(vm, &vm->types[TINYPY_VALUE_BYTEARRAY], sizeof(*bytearray), out_error);

    if (bytearray == NULL) {
        if (bytes != NULL) {
            tinypy_internal_vm_deallocate(vm, bytes, size);
        }
        return NULL;
    }

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
static tinypy_value_t *__tinypy_bytearray_from_bytes_checked(tinypy_vm_t *vm, const void *bytes, size_t size, tinypy_error_t **out_error) {
    tinypy_value_t *result = __tinypy_bytearray_allocate_checked(vm, size, out_error);

    if (result != NULL && size != 0U) {
        (void)memcpy(TINYPY_BYTEARRAY_OBJECT(result)->bytes, bytes, size);
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
    if (TINYPY_BYTEARRAY_OBJECT(left)->exports != 0U) {
        if (TINYPY_SIZED_SIZE(left) != 0U) {
            (void)memcpy(TINYPY_BYTEARRAY_OBJECT(left)->bytes, TINYPY_BYTEARRAY_OBJECT(right)->bytes, TINYPY_SIZED_SIZE(left));
        }
        return;
    }
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
static tinypy_bool_t __tinypy_bytearray_constructor_arguments(tinypy_vm_t *vm, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_value_t **out_source, tinypy_value_t **out_encoding, tinypy_value_t **out_errors, tinypy_error_t **out_error) {
    tinypy_value_t *const names[3] = {vm->internal_source_key, vm->internal_encoding_key, vm->internal_errors_key};
    tinypy_value_t *values[3];

    if (tinypy_internal_constructor_optional_arguments(vm, "bytearray", 9U, args, kwargs, names, 3U, UINT32_C(6), values, out_error) == 0) {
        return TINYPY_FALSE;
    }
    *out_source = values[0];
    *out_encoding = values[1];
    *out_errors = values[2];
    if (*out_source == NULL && (*out_encoding != NULL || *out_errors != NULL)) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "encoding or errors without sequence argument", out_error);
        return TINYPY_FALSE;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_bytearray_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = type->vm;
    uint8_t *bytes;
    size_t size;
    int64_t requested_size;
    tinypy_value_t *source;
    tinypy_value_t *encoding;
    tinypy_value_t *errors;

    if (__tinypy_bytearray_constructor_arguments(vm, args, kwargs, &source, &encoding, &errors, out_error) == 0) {
        return NULL;
    }
    if (source == NULL) {
        tinypy_value_t *return_value_1 = __tinypy_bytearray_from_bytes_checked(vm, NULL, 0U, out_error);
        return return_value_1;
    }
    tinypy_value_type_e source_kind = TINYPY_VALUE_KIND(source);
    if (source_kind == TINYPY_VALUE_UNICODE) {
        if (encoding == NULL) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "unicode argument without an encoding", out_error);
            return NULL;
        }
        tinypy_value_t *encoded = tinypy_internal_text_codec(vm, source, encoding, errors, TINYPY_FALSE, TINYPY_TRUE, NULL, out_error);
        if (encoded == NULL) {
            return NULL;
        }
        if (TINYPY_VALUE_KIND(encoded) != TINYPY_VALUE_STRING) {
            TINYPY_DECREF(encoded);
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "encoder did not return a string", out_error);
            return NULL;
        }
        tinypy_value_t *result = __tinypy_bytearray_from_bytes_checked(vm, TINYPY_TEXT_BYTES(encoded), TINYPY_TEXT_BYTE_SIZE(encoded), out_error);
        TINYPY_DECREF(encoded);
        return result;
    }
    if (source_kind != TINYPY_VALUE_STRING && (encoding != NULL || errors != NULL)) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "encoding or errors without a string argument", out_error);
        return NULL;
    }
    if (source_kind == TINYPY_VALUE_BOOL || source_kind == TINYPY_VALUE_INTEGER || source_kind == TINYPY_VALUE_LONG || tinypy_internal_object_has_special_key(source, vm->internal_special_index_key) != 0) {
        if (tinypy_internal_index_as_i64(source, &requested_size, TINYPY_FALSE, out_error) == 0) {
            return NULL;
        }
        if (requested_size < 0) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "negative count", out_error);
            return NULL;
        }
        tinypy_value_t *result = __tinypy_bytearray_from_bytes_checked(vm, NULL, 0U, out_error);
        if (result == NULL) {
            return NULL;
        }
        if (requested_size != 0) {
            if (__tinypy_bytearray_reserve_checked(result, (size_t)requested_size, out_error) == 0) {
                TINYPY_DECREF(result);
                return NULL;
            }
            (void)memset(TINYPY_BYTEARRAY_OBJECT(result)->bytes, 0, (size_t)requested_size);
            TINYPY_SIZED_SIZE(result) = (size_t)requested_size;
        }
        return result;
    }
    if (__tinypy_bytearray_collect(vm, source, &bytes, &size, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *return_value_1 = __tinypy_bytearray_adopt_checked(vm, bytes, size, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_bytearray_initialize(tinypy_value_t *value, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_value_t *source;
    tinypy_value_t *encoding;
    tinypy_value_t *errors;
    int64_t requested_size;

    if (tinypy_internal_bytearray_resize_allowed(value, 0U, out_error) == 0) {
        return TINYPY_FALSE;
    }
    TINYPY_SIZED_SIZE(value) = 0U;
    if (__tinypy_bytearray_constructor_arguments(vm, args, kwargs, &source, &encoding, &errors, out_error) == 0) {
        return TINYPY_FALSE;
    }
    if (source == NULL) {
        return TINYPY_TRUE;
    }
    tinypy_value_type_e source_kind = TINYPY_VALUE_KIND(source);
    if (source_kind == TINYPY_VALUE_UNICODE) {
        if (encoding == NULL) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "unicode argument without an encoding", out_error);
            return TINYPY_FALSE;
        }
        tinypy_value_t *encoded = tinypy_internal_text_codec(vm, source, encoding, errors, TINYPY_FALSE, TINYPY_TRUE, NULL, out_error);
        if (encoded == NULL) {
            return TINYPY_FALSE;
        }
        if (TINYPY_VALUE_KIND(encoded) != TINYPY_VALUE_STRING) {
            TINYPY_DECREF(encoded);
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "encoder did not return a string", out_error);
            return TINYPY_FALSE;
        }
        tinypy_bool_t extended = __tinypy_bytearray_extend_iterable(value, encoded, out_error);
        TINYPY_DECREF(encoded);
        return extended;
    }
    if (source_kind != TINYPY_VALUE_STRING && (encoding != NULL || errors != NULL)) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "encoding or errors without a string argument", out_error);
        return TINYPY_FALSE;
    }
    if (source_kind == TINYPY_VALUE_BOOL || source_kind == TINYPY_VALUE_INTEGER || source_kind == TINYPY_VALUE_LONG || tinypy_internal_object_has_special_key(source, vm->internal_special_index_key) != 0) {
        if (tinypy_internal_index_as_i64(source, &requested_size, TINYPY_FALSE, out_error) == 0) {
            return TINYPY_FALSE;
        }
        if (requested_size < 0) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "negative count", out_error);
            return TINYPY_FALSE;
        }
        if (tinypy_internal_bytearray_resize_allowed(value, (size_t)requested_size, out_error) == 0 || __tinypy_bytearray_reserve_checked(value, (size_t)requested_size, out_error) == 0) {
            return TINYPY_FALSE;
        }
        if (requested_size != 0) {
            (void)memset(TINYPY_BYTEARRAY_OBJECT(value)->bytes, 0, (size_t)requested_size);
        }
        TINYPY_SIZED_SIZE(value) = (size_t)requested_size;
        return TINYPY_TRUE;
    }
    tinypy_bool_t result = __tinypy_bytearray_extend_iterable(value, source, out_error);
    return result;
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
    size_t index;

    if (TINYPY_VALUE_KIND(key) == TINYPY_VALUE_SLICE) {
        tinypy_internal_slice_indices_t slice;
        tinypy_value_t *result;
        size_t selected_index;

        if (tinypy_internal_slice_unpack(key, &slice, out_error) == 0
            || tinypy_internal_slice_adjust_indices(vm, TINYPY_SIZED_SIZE(value), &slice, out_error) == 0) {
            return NULL;
        }
        if (slice.length == 0U) {
            tinypy_value_t *return_value_1 = __tinypy_bytearray_from_bytes_checked(vm, NULL, 0U, out_error);
            return return_value_1;
        }
        if (slice.step == 1) {
            tinypy_value_t *return_value_2 = __tinypy_bytearray_from_bytes_checked(vm, TINYPY_BYTEARRAY_OBJECT(value)->bytes + (size_t)slice.start, slice.length, out_error);
            return return_value_2;
        }
        result = __tinypy_bytearray_allocate_checked(vm, slice.length, out_error);
        if (result == NULL) {
            return NULL;
        }
        for (selected_index = 0U; selected_index < slice.length; ++selected_index) {
            TINYPY_BYTEARRAY_OBJECT(result)->bytes[selected_index] = TINYPY_BYTEARRAY_OBJECT(value)->bytes[(size_t)(slice.start + (int64_t)selected_index * slice.step)];
        }
        return result;
    }
    if (__tinypy_bytearray_index(vm, key, value, &index, out_error) == 0) {
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

        if (tinypy_internal_slice_unpack(key, &slice, out_error) == 0) {
            return TINYPY_FALSE;
        }
        if (item != NULL) {
            tinypy_value_type_e item_kind = TINYPY_VALUE_KIND(item);

            if (item == value || item_kind != TINYPY_VALUE_BYTEARRAY) {
                if (item_kind == TINYPY_VALUE_UNICODE || item_kind == TINYPY_VALUE_OLD_INSTANCE ||
                    tinypy_internal_object_has_special_key(item, vm->internal_special_int_key) != 0 ||
                    tinypy_internal_object_has_special_key(item, vm->internal_special_float_key) != 0) {
                    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "can assign only bytes, buffers, or iterables of ints in range(0, 256)", out_error);
                    return TINYPY_FALSE;
                }
                tinypy_value_t *args = tinypy_tuple_from_items(vm, &item, 1U);
                tinypy_value_t *copy = tinypy_internal_bytearray_create(&vm->types[TINYPY_VALUE_BYTEARRAY], args, NULL, out_error);

                TINYPY_DECREF(args);
                if (copy == NULL) {
                    return TINYPY_FALSE;
                }
                tinypy_bool_t assigned = tinypy_internal_bytearray_set_item(value, key, copy, out_error);

                TINYPY_DECREF(copy);
                return assigned;
            }
            size = TINYPY_SIZED_SIZE(value);
            if (tinypy_internal_slice_adjust_indices(vm, size, &slice, out_error) == 0) {
                return TINYPY_FALSE;
            }
            replacement_size = TINYPY_SIZED_SIZE(item);
            replacement = TINYPY_BYTEARRAY_OBJECT(item)->bytes;
            if (slice.step == 1 || replacement_size != 0U) {
                if (slice.step != 1 && replacement_size != slice.length) {
                    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "extended slice assignment has the wrong size", out_error);
                    return TINYPY_FALSE;
                }
                if (slice.step == 1 && replacement_size > SIZE_MAX - (size - slice.length)) {
                    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "bytearray is too large", out_error);
                    return TINYPY_FALSE;
                }
                if (slice.step == 1 && tinypy_internal_bytearray_resize_allowed(value, size - slice.length + replacement_size, out_error) == 0) {
                    return TINYPY_FALSE;
                }
                tinypy_bool_t assigned = __tinypy_bytearray_replace_slice(value, &slice, replacement, replacement_size, out_error);

                return assigned;
            }
        }
        if (item == NULL) {
            size = TINYPY_SIZED_SIZE(value);
            if (tinypy_internal_slice_adjust_indices(vm, size, &slice, out_error) == 0) {
                return TINYPY_FALSE;
            }
        }
        if (slice.length != 0U && tinypy_internal_bytearray_resize_allowed(value, size - slice.length, out_error) == 0) {
            return TINYPY_FALSE;
        }
        if (slice.step == 1) {
            (void)__tinypy_bytearray_replace_slice(value, &slice, NULL, 0U, out_error);
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
    uint8_t byte = 0U;
    if (__tinypy_bytearray_index(vm, key, value, &index, out_error) == 0) {
        return TINYPY_FALSE;
    }
    if (item != NULL && __tinypy_bytearray_item(vm, item, &byte, out_error) == 0) {
        return TINYPY_FALSE;
    }
    if (item == NULL) {
        if (tinypy_internal_bytearray_resize_allowed(value, TINYPY_SIZED_SIZE(value) - 1U, out_error) == 0) {
            return TINYPY_FALSE;
        }
        __tinypy_bytearray_delete_index(value, index);
        return TINYPY_TRUE;
    }
    TINYPY_BYTEARRAY_OBJECT(value)->bytes[index] = byte;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_bytearray_string(tinypy_value_t *value, tinypy_error_t **out_error) {
    TINYPY_CLEAR_ERROR(out_error);
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_value_t *return_value_1 = tinypy_internal_string_from_bytes_checked(vm, TINYPY_BYTEARRAY_OBJECT(value)->bytes, TINYPY_SIZED_SIZE(value), out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_bytearray_repr(tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_value_t *string = tinypy_internal_bytearray_string(value, out_error);
    const uint8_t *quoted_bytes;
    size_t quoted_size;
    size_t escaped_single_quotes = 0U;
    size_t quoted_index;
    size_t output_index;
    uint8_t *output;

    if (string == NULL) {
        return NULL;
    }
    tinypy_value_t *quoted = tinypy_object_repr(string, out_error);
    TINYPY_DECREF(string);
    if (quoted == NULL) {
        return NULL;
    }
    quoted_bytes = (const uint8_t *)tinypy_string_view(quoted, &quoted_size);
    if (quoted_size != 0U && quoted_bytes[0] == (uint8_t)'"') {
        for (quoted_index = 1U; quoted_index + 1U < quoted_size; ++quoted_index) {
            if (quoted_bytes[quoted_index] == (uint8_t)'\'') {
                escaped_single_quotes += 1U;
            }
        }
    }
    if (quoted_size > SIZE_MAX - 12U || escaped_single_quotes > SIZE_MAX - quoted_size - 12U) {
        TINYPY_DECREF(quoted);
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "bytearray representation is too large", out_error);
        return NULL;
    }
    tinypy_value_t *result = tinypy_internal_text_allocate_uninitialized_checked(vm, TINYPY_VALUE_STRING, quoted_size + escaped_single_quotes + 12U, quoted_size + escaped_single_quotes + 12U, &output, out_error);
    if (result == NULL) {
        TINYPY_DECREF(quoted);
        return NULL;
    }
    (void)memcpy(output, "bytearray(b", 11U);
    output_index = 11U;
    for (quoted_index = 0U; quoted_index < quoted_size; ++quoted_index) {
        if (escaped_single_quotes != 0U && quoted_bytes[quoted_index] == (uint8_t)'\'') {
            output[output_index++] = (uint8_t)'\\';
        }
        output[output_index++] = quoted_bytes[quoted_index];
    }
    output[output_index] = (uint8_t)')';
    TINYPY_DECREF(quoted);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_bytearray_index_argument(tinypy_value_t *value, int64_t *out_integer, tinypy_bool_t bound, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);

    if (tinypy_internal_object_has_special_key(value, vm->internal_special_index_key) == TINYPY_FALSE) {
        if (bound != TINYPY_FALSE) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "slice indices must be integers or None or have an __index__ method", out_error);
        }
        else {
            tinypy_message_part_t parts[] = {
                TINYPY_MESSAGE_PART_LITERAL("'"), TINYPY_MESSAGE_PART_TYPE_NAME(value),
                TINYPY_MESSAGE_PART_LITERAL("' object cannot be interpreted as an index"),
            };
            tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
        }
        return TINYPY_FALSE;
    }
    tinypy_bool_t parsed = tinypy_internal_index_as_i64(value, out_integer, bound, out_error);
    return parsed;
}
//////////////////////////////////////////////////////////////////////////
/* Python 2.7 lets a byte string concatenate with a bytearray and keeps the
   mutable type for the result. */
tinypy_value_t *tinypy_internal_bytearray_concat_bytes(tinypy_vm_t *vm, const uint8_t *left_bytes, size_t left_size, const uint8_t *right_bytes, size_t right_size, tinypy_error_t **out_error) {
    tinypy_value_t *result;
    uint8_t *bytes;

    if (right_size > SIZE_MAX - left_size) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "bytearray is too large", out_error);
        return NULL;
    }
    if (left_size + right_size == 0U) {
        tinypy_value_t *return_value_1 = __tinypy_bytearray_from_bytes_checked(vm, NULL, 0U, out_error);
        return return_value_1;
    }
    result = __tinypy_bytearray_allocate_checked(vm, left_size + right_size, out_error);
    if (result == NULL) {
        return NULL;
    }
    bytes = TINYPY_BYTEARRAY_OBJECT(result)->bytes;
    if (left_size != 0U) {
        (void)memcpy(bytes, left_bytes, left_size);
    }
    if (right_size != 0U) {
        (void)memcpy(bytes + left_size, right_bytes, right_size);
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_bytearray_add_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    const uint8_t *right_bytes;
    size_t left_size;
    size_t right_size;

    (void)user_data;
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 1U, 1U, TINYPY_ARITY_STYLE_WRAPPER, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *left = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *item = TINYPY_TUPLE_GET(args, 1U);
    if (tinypy_internal_bytes_view(item, &right_bytes, &right_size) == 0) {
        tinypy_message_part_t parts[] = {
            TINYPY_MESSAGE_PART_LITERAL("can't concat "), TINYPY_MESSAGE_PART_TYPE_NAME(left),
            TINYPY_MESSAGE_PART_LITERAL(" to "), TINYPY_MESSAGE_PART_TYPE_NAME(item),
        };
        tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
        return NULL;
    }
    left_size = TINYPY_SIZED_SIZE(left);
    tinypy_value_t *return_value_2 = tinypy_internal_bytearray_concat_bytes(vm, TINYPY_BYTEARRAY_OBJECT(left)->bytes, left_size, right_bytes, right_size, out_error);
    return return_value_2;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_bytearray_multiply_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    int64_t count;
    size_t unit_size;
    size_t total_size;

    (void)user_data;
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 1U, 1U, TINYPY_ARITY_STYLE_UNPACK, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *value = TINYPY_TUPLE_GET(args, 0U);
    if (__tinypy_bytearray_index_argument(TINYPY_TUPLE_GET(args, 1U), &count, TINYPY_FALSE, out_error) == 0) {
        return NULL;
    }
    unit_size = TINYPY_SIZED_SIZE(value);
    if (count <= 0 || unit_size == 0U) {
        tinypy_value_t *return_value_1 = __tinypy_bytearray_allocate_checked(vm, 0U, out_error);
        return return_value_1;
    }
    if ((uint64_t)count > (uint64_t)(SIZE_MAX / unit_size)) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "repeated bytearray is too large", out_error);
        return NULL;
    }
    total_size = unit_size * (size_t)count;
    tinypy_value_t *result = __tinypy_bytearray_allocate_checked(vm, total_size, out_error);
    if (result == NULL) {
        return NULL;
    }
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
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 1U, 1U, TINYPY_ARITY_STYLE_WRAPPER, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *left = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *right = TINYPY_TUPLE_GET(args, 1U);
    if (tinypy_internal_bytes_view(right, &right_bytes, &right_size) == 0) {
        tinypy_message_part_t parts[] = {
            TINYPY_MESSAGE_PART_LITERAL("can't concat "), TINYPY_MESSAGE_PART_TYPE_NAME(right),
            TINYPY_MESSAGE_PART_LITERAL(" to "), TINYPY_MESSAGE_PART_TYPE_NAME(left),
        };
        tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
        return NULL;
    }
    size_t left_size = TINYPY_SIZED_SIZE(left);
    if (left == right && right_size != 0U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_BUFFER, "Existing exports of data: object cannot be re-sized", out_error);
        return NULL;
    }
    if (right_size > SIZE_MAX - left_size) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "bytearray is too large", out_error);
        return NULL;
    }
    if (tinypy_internal_bytearray_resize_allowed(left, left_size + right_size, out_error) == 0) {
        return NULL;
    }
    uint8_t *snapshot = NULL;
    if (right_size != 0U) {
        snapshot = (uint8_t *)tinypy_internal_vm_allocate_checked(vm, right_size, out_error);
        if (snapshot == NULL) {
            return NULL;
        }
        (void)memcpy(snapshot, right_bytes, right_size);
    }
    if (__tinypy_bytearray_reserve_checked(left, left_size + right_size, out_error) == 0) {
        if (snapshot != NULL) {
            tinypy_internal_vm_deallocate(vm, snapshot, right_size);
        }
        return NULL;
    }
    uint8_t *left_bytes = TINYPY_BYTEARRAY_OBJECT(left)->bytes;
    if (right_size != 0U) {
        (void)memcpy(left_bytes + left_size, snapshot, right_size);
        tinypy_internal_vm_deallocate(vm, snapshot, right_size);
    }
    TINYPY_SIZED_SIZE(left) = left_size + right_size;
    return TINYPY_RET(left);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_bytearray_inplace_multiply_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    int64_t count;

    (void)user_data;
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 1U, 1U, TINYPY_ARITY_STYLE_UNPACK, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *value = TINYPY_TUPLE_GET(args, 0U);
    if (__tinypy_bytearray_index_argument(TINYPY_TUPLE_GET(args, 1U), &count, TINYPY_FALSE, out_error) == 0) {
        return NULL;
    }
    size_t unit_size = TINYPY_SIZED_SIZE(value);
    if (count <= 0) {
        if (tinypy_internal_bytearray_resize_allowed(value, 0U, out_error) == 0) {
            return NULL;
        }
        TINYPY_SIZED_SIZE(value) = 0U;
    }
    else if (count > 1 && unit_size != 0U) {
        if ((uint64_t)count > (uint64_t)(SIZE_MAX / unit_size)) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "repeated bytearray is too large", out_error);
            return NULL;
        }
        size_t total_size = unit_size * (size_t)count;
        if (tinypy_internal_bytearray_resize_allowed(value, total_size, out_error) == 0) {
            return NULL;
        }
        if (__tinypy_bytearray_reserve_checked(value, total_size, out_error) == 0) {
            return NULL;
        }
        uint8_t *bytes = TINYPY_BYTEARRAY_OBJECT(value)->bytes;
        size_t copied = unit_size;
        while (copied < total_size) {
            size_t chunk = copied < total_size - copied ? copied : total_size - copied;

            (void)memcpy(bytes + copied, bytes, chunk);
            copied += chunk;
        }
        TINYPY_SIZED_SIZE(value) = total_size;
    }
    return TINYPY_RET(value);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_bytearray_append_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    uint8_t byte;
    size_t size;

    (void)user_data;
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 1U, 1U, TINYPY_ARITY_STYLE_SINGLE, out_error) == 0) {
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
    if (tinypy_internal_bytearray_resize_allowed(value, size + 1U, out_error) == 0) {
        return NULL;
    }
    if (__tinypy_bytearray_reserve_checked(value, size + 1U, out_error) == 0) {
        return NULL;
    }
    TINYPY_BYTEARRAY_OBJECT(value)->bytes[size] = byte;
    TINYPY_SIZED_SIZE(value) += 1;
    tinypy_value_t *return_value_1 = TINYPY_RET_NONE(vm);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_bytearray_extend_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    uint8_t *extension;
    size_t extension_size;
    size_t size;

    (void)user_data;
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 1U, 1U, TINYPY_ARITY_STYLE_SINGLE, out_error) == 0) {
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
    if (tinypy_internal_bytearray_resize_allowed(value, size + extension_size, out_error) == 0) {
        if (extension != NULL) {
            tinypy_internal_vm_deallocate(vm, extension, extension_size);
        }
        return NULL;
    }
    if (__tinypy_bytearray_reserve_checked(value, size + extension_size, out_error) == 0) {
        if (extension != NULL) {
            tinypy_internal_vm_deallocate(vm, extension, extension_size);
        }
        return NULL;
    }
    if (extension_size != 0U) {
        (void)memcpy(TINYPY_BYTEARRAY_OBJECT(value)->bytes + size, extension, extension_size);
    }
    TINYPY_SIZED_SIZE(value) = size + extension_size;
    if (extension != NULL) {
        tinypy_internal_vm_deallocate(vm, extension, extension_size);
    }
    tinypy_value_t *return_value_1 = TINYPY_RET_NONE(vm);
    return return_value_1;
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

    tinypy_value_t *name = user_data != NULL ? (tinypy_value_t *)user_data : vm->internal_find_key;
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 0U, SIZE_MAX, TINYPY_ARITY_STYLE_PARSED, out_error) == 0) {
        return NULL;
    }
    size_t supplied = TINYPY_TUPLE_SIZE(args) - 1U;
    if (supplied < 1U || supplied > 3U) {
        tinypy_internal_make_arity_error(vm, "find/rfind/index/rindex", sizeof("find/rfind/index/rindex") - 1U, supplied, 1U, 3U, TINYPY_ARITY_STYLE_PARSED, out_error);
        return NULL;
    }
    tinypy_value_t *value = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *item = TINYPY_TUPLE_GET(args, 1U);
    argument_count = TINYPY_TUPLE_SIZE(args);
    tinypy_value_t *start_value = argument_count >= 3U ? TINYPY_TUPLE_GET(args, 2U) : NULL;
    stop_value = argument_count >= 4U ? TINYPY_TUPLE_GET(args, 3U) : NULL;
    start = INT64_C(0);
    stop = INT64_MAX;
    if (start_value != NULL && TINYPY_VALUE_KIND(start_value) != TINYPY_VALUE_NONE && __tinypy_bytearray_index_argument(start_value, &start, TINYPY_TRUE, out_error) == 0) {
        return NULL;
    }
    if (stop_value != NULL && TINYPY_VALUE_KIND(stop_value) != TINYPY_VALUE_NONE && __tinypy_bytearray_index_argument(stop_value, &stop, TINYPY_TRUE, out_error) == 0) {
        return NULL;
    }
    /* Bounds may run Python code: reacquire both buffers and their lengths. */
    size = TINYPY_SIZED_SIZE(value);
    if (tinypy_internal_bytes_view(item, &needle, &needle_size) == 0) {
        const tinypy_message_part_t parts[] = {
            TINYPY_MESSAGE_PART_LITERAL("Type "),
            TINYPY_MESSAGE_PART_TYPE_NAME(item),
            TINYPY_MESSAGE_PART_LITERAL(" doesn't support the buffer API")
        };
        tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
        return NULL;
    }
    if (start < INT64_C(0)) {
        start = start < -(int64_t)size ? INT64_C(0) : start + (int64_t)size;
    }
    if (stop < INT64_C(0)) {
        stop = stop < -(int64_t)size ? INT64_C(0) : stop + (int64_t)size;
    }
    if ((uint64_t)stop > (uint64_t)size) {
        stop = (int64_t)size;
    }
    if (start <= stop && needle_size <= (size_t)(stop - start)) {
        const uint8_t *haystack = TINYPY_BYTEARRAY_OBJECT(value)->bytes;
        tinypy_bool_t reverse = name == vm->internal_rfind_key || name == vm->internal_rindex_key ? TINYPY_TRUE : TINYPY_FALSE;
        found = tinypy_internal_find_bytes(haystack != NULL ? haystack + (size_t)start : NULL, (size_t)(stop - start), needle, needle_size, reverse);
        if (found >= 0) {
            found += (ptrdiff_t)start;
        }
    }
    if (found < 0 && (name == vm->internal_index_key || name == vm->internal_rindex_key)) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "subsection not found", out_error);
        return NULL;
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
    size_t name_offset;
    tinypy_bytearray_bridge_result_e result;
    tinypy_bool_t join;
    size_t minimum;
    size_t maximum;
    tinypy_arity_style_e arity;
} tinypy_bytearray_bridge_spec_t;
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_bytearray_bridge_argument(tinypy_vm_t *vm, tinypy_value_t *value, tinypy_error_t **out_error) {
    const uint8_t *bytes;
    size_t size;

    if (tinypy_internal_bytes_view(value, &bytes, &size) != 0) {
        tinypy_value_t *return_value_1 = tinypy_internal_string_from_bytes_checked(vm, bytes, size, out_error);
        return return_value_1;
    }
    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_TUPLE) {
        size_t count = TINYPY_TUPLE_SIZE(value);
        tinypy_value_t *result = tinypy_internal_tuple_new_checked(vm, count, out_error);
        size_t index;

        if (result == NULL) {
            return NULL;
        }
        for (index = 0U; index < count; ++index) {
            tinypy_value_t *item = __tinypy_bytearray_bridge_argument(vm, TINYPY_TUPLE_GET(value, index), out_error);

            if (item == NULL) {
                TINYPY_DECREF(result);
                return NULL;
            }
            TINYPY_DECREF(TINYPY_TUPLE_GET(result, index));
            TINYPY_TUPLE_GET(result, index) = item;
        }
        return result;
    }
    return TINYPY_RET(value);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_bytearray_join(tinypy_value_t *self, tinypy_value_t *iterable, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(self);
    size_t separator_size = TINYPY_SIZED_SIZE(self);
    tinypy_value_t *sequence;

    if (iterable->type == &vm->types[TINYPY_VALUE_LIST] || iterable->type == &vm->types[TINYPY_VALUE_TUPLE]) {
        sequence = TINYPY_RET(iterable);
    }
    else {
        tinypy_value_t *iterator = tinypy_iter(iterable, out_error);
        if (iterator == NULL) {
            if (tinypy_internal_exception_consume_kind(vm, TINYPY_EXCEPTION_TYPE_ERROR, out_error) != 0) {
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "can only join an iterable", out_error);
            }
            return NULL;
        }
        sequence = tinypy_internal_list_from_items_checked(vm, NULL, 0U, out_error);
        if (sequence == NULL) {
            TINYPY_DECREF(iterator);
            return NULL;
        }
        tinypy_bool_t extended = tinypy_internal_list_extend_iterable(sequence, iterator, "error return without exception set", out_error);
        TINYPY_DECREF(iterator);
        if (extended == 0) {
            TINYPY_DECREF(sequence);
            return NULL;
        }
    }
    size_t count = TINYPY_SIZED_SIZE(sequence);
    size_t total_size = 0U;
    size_t index;

    for (index = 0U; index < count; ++index) {
        tinypy_value_t *item = TINYPY_VALUE_KIND(sequence) == TINYPY_VALUE_LIST ? TINYPY_LIST_GET(sequence, index) : TINYPY_TUPLE_GET(sequence, index);
        if (TINYPY_VALUE_KIND(item) != TINYPY_VALUE_STRING && TINYPY_VALUE_KIND(item) != TINYPY_VALUE_BYTEARRAY) {
            char prefix[96];
            int written = snprintf(prefix, sizeof(prefix), "can only join an iterable of bytes (item %zu has type '", index);
            const tinypy_message_part_t parts[] = {{prefix, (size_t)written}, TINYPY_MESSAGE_PART_TYPE_NAME(item), TINYPY_MESSAGE_PART_LITERAL("')")};

            tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
            TINYPY_DECREF(sequence);
            return NULL;
        }
        size_t item_size = TINYPY_SIZED_SIZE(item);
        size_t between = index != 0U ? separator_size : 0U;
        if (between > (size_t)PTRDIFF_MAX - total_size || item_size > (size_t)PTRDIFF_MAX - total_size - between) {
            TINYPY_DECREF(sequence);
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_MEMORY, "bytearray is too large", out_error);
            return NULL;
        }
        total_size += between + item_size;
    }
    tinypy_value_t *result = __tinypy_bytearray_allocate_checked(vm, total_size, out_error);
    if (result == NULL) {
        TINYPY_DECREF(sequence);
        return NULL;
    }
    size_t offset = 0U;
    for (index = 0U; index < count; ++index) {
        tinypy_value_t *item = TINYPY_VALUE_KIND(sequence) == TINYPY_VALUE_LIST ? TINYPY_LIST_GET(sequence, index) : TINYPY_TUPLE_GET(sequence, index);
        const uint8_t *bytes;
        size_t size;
        if (index != 0U && separator_size != 0U) {
            size_t available = TINYPY_SIZED_SIZE(self) < separator_size ? TINYPY_SIZED_SIZE(self) : separator_size;
            (void)memcpy(TINYPY_BYTEARRAY_OBJECT(result)->bytes + offset, TINYPY_BYTEARRAY_OBJECT(self)->bytes, available);
            (void)memset(TINYPY_BYTEARRAY_OBJECT(result)->bytes + offset + available, 0, separator_size - available);
            offset += separator_size;
        }
        (void)tinypy_internal_bytes_view(item, &bytes, &size);
        if (size != 0U) {
            (void)memcpy(TINYPY_BYTEARRAY_OBJECT(result)->bytes + offset, bytes, size);
        }
        offset += size;
    }
    TINYPY_DECREF(sequence);
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
        tinypy_value_t *result = __tinypy_bytearray_from_bytes_checked(vm, bytes, size, out_error);
        TINYPY_DECREF(value);
        return result;
    }
    size_t count = result_kind == TINYPY_BYTEARRAY_BRIDGE_LIST ? TINYPY_LIST_SIZE(value) : TINYPY_TUPLE_SIZE(value);
    tinypy_value_t *result = result_kind == TINYPY_BYTEARRAY_BRIDGE_LIST
                                 ? tinypy_list_from_items(vm, NULL, 0U)
                                 : tinypy_internal_tuple_new_checked(vm, count, out_error);
    size_t index;

    if (result == NULL) {
        TINYPY_DECREF(value);
        return NULL;
    }
    if (result_kind == TINYPY_BYTEARRAY_BRIDGE_LIST && tinypy_internal_list_reserve_checked(vm, result, count, out_error) == 0) {
        TINYPY_DECREF(result);
        TINYPY_DECREF(value);
        return NULL;
    }
    for (index = 0U; index < count; ++index) {
        tinypy_value_t *item = result_kind == TINYPY_BYTEARRAY_BRIDGE_LIST ? TINYPY_LIST_GET(value, index) : TINYPY_TUPLE_GET(value, index);
        const uint8_t *bytes;
        size_t size;

        if (tinypy_internal_bytes_view(item, &bytes, &size) == 0) {
            TINYPY_DECREF(result);
            TINYPY_DECREF(value);
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "bytearray method returned a non-byte string", out_error);
            return NULL;
        }
        tinypy_value_t *converted = __tinypy_bytearray_from_bytes_checked(vm, bytes, size, out_error);

        if (converted == NULL) {
            TINYPY_DECREF(result);
            TINYPY_DECREF(value);
            return NULL;
        }
        if (result_kind == TINYPY_BYTEARRAY_BRIDGE_LIST) {
            if (tinypy_internal_list_append_checked(result, converted, out_error) == 0) {
                TINYPY_DECREF(converted);
                TINYPY_DECREF(result);
                TINYPY_DECREF(value);
                return NULL;
            }
            TINYPY_DECREF(converted);
        }
        else {
            TINYPY_DECREF(TINYPY_TUPLE_GET(result, index));
            TINYPY_TUPLE_GET(result, index) = converted;
        }
    }
    TINYPY_DECREF(value);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_bytearray_ascii_transform(tinypy_vm_t *vm, tinypy_value_t *self, tinypy_value_t *name, tinypy_error_t **out_error) {
    size_t size = TINYPY_SIZED_SIZE(self);
    tinypy_value_t *result = __tinypy_bytearray_from_bytes_checked(vm, TINYPY_BYTEARRAY_OBJECT(self)->bytes, size, out_error);
    if (result == NULL) {
        return NULL;
    }
    uint8_t *bytes = TINYPY_BYTEARRAY_OBJECT(result)->bytes;
    size_t index;

    for (index = 0U; index < size; ++index) {
        uint8_t byte = bytes[index];

        if (name == vm->internal_lower_key) {
            if (byte >= (uint8_t)'A' && byte <= (uint8_t)'Z') {
                bytes[index] = (uint8_t)(byte + ((uint8_t)'a' - (uint8_t)'A'));
            }
        }
        else if (name == vm->internal_upper_key) {
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
static tinypy_bool_t __tinypy_bytearray_buffer_argument(tinypy_value_t *value, tinypy_bool_t interface_error, const uint8_t **out_bytes, size_t *out_size, tinypy_error_t **out_error) {
    if (tinypy_internal_bytes_view(value, out_bytes, out_size) != TINYPY_FALSE) {
        return TINYPY_TRUE;
    }
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_message_part_t parts[] = {
        {interface_error != TINYPY_FALSE ? "'" : "Type ", interface_error != TINYPY_FALSE ? 1U : 5U},
        TINYPY_MESSAGE_PART_TYPE_NAME(value),
        {interface_error != TINYPY_FALSE ? "' does not have the buffer interface" : " doesn't support the buffer API", interface_error != TINYPY_FALSE ? sizeof("' does not have the buffer interface") - 1U : sizeof(" doesn't support the buffer API") - 1U},
    };
    tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
    return TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_bytearray_prefix(tinypy_value_t *self, tinypy_value_t *prefix, tinypy_value_t *const *normalized, size_t count, tinypy_bool_t suffix, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(self);
    int64_t size = (int64_t)TINYPY_SIZED_SIZE(self);
    int64_t start = count > 2U && normalized[1] != NULL ? tinypy_integer_as_i64(normalized[1]) : 0;
    int64_t end = count > 3U && normalized[2] != NULL ? tinypy_integer_as_i64(normalized[2]) : size;
    tinypy_bool_t tuple = TINYPY_VALUE_KIND(prefix) == TINYPY_VALUE_TUPLE;
    size_t prefixes = tuple != TINYPY_FALSE ? TINYPY_TUPLE_SIZE(prefix) : 1U;

    if (start < 0) {
        start = start < -size ? 0 : start + size;
    }
    if (end < 0) {
        end = end < -size ? 0 : end + size;
    }
    if (end > size) {
        end = size;
    }
    for (size_t index = 0U; index < prefixes; ++index) {
        tinypy_value_t *value = tuple != TINYPY_FALSE ? TINYPY_TUPLE_GET(prefix, index) : prefix;
        const uint8_t *bytes;
        size_t byte_size;
        if (__tinypy_bytearray_buffer_argument(value, TINYPY_FALSE, &bytes, &byte_size, out_error) == TINYPY_FALSE) {
            return NULL;
        }
        if (start <= end && byte_size <= (uint64_t)(end - start)) {
            size_t offset = suffix != TINYPY_FALSE ? (size_t)end - byte_size : (size_t)start;
            if (byte_size == 0U || memcmp(TINYPY_BYTEARRAY_OBJECT(self)->bytes + offset, bytes, byte_size) == 0) {
                tinypy_value_t *result = TINYPY_RET_TRUE(vm);
                return result;
            }
        }
    }
    tinypy_value_t *result = TINYPY_RET_FALSE(vm);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_bytearray_bridge_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    const tinypy_bytearray_bridge_spec_t *spec = (const tinypy_bytearray_bridge_spec_t *)user_data;
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *name = *(tinypy_value_t **)((uint8_t *)vm + spec->name_offset);
    size_t argument_count = TINYPY_TUPLE_SIZE(args);
    tinypy_value_t *converted[3] = {NULL, NULL, NULL};
    tinypy_value_t *normalized[3] = {NULL, NULL, NULL};
    size_t index;

    tinypy_bool_t decode = name == vm->internal_decode_key;
    if (decode != TINYPY_FALSE) {
        size_t supplied = argument_count - 1U + (kwargs != NULL ? TINYPY_DICT_SIZE(kwargs) : 0U);
        if (supplied > spec->maximum) {
            tinypy_internal_make_arity_error(vm, "decode", 6U, supplied, 0U, spec->maximum, TINYPY_ARITY_STYLE_PARSED, out_error);
            return NULL;
        }
    }
    if (tinypy_internal_native_method_arguments(function, args, decode != TINYPY_FALSE ? NULL : kwargs, spec->minimum, spec->maximum, spec->arity, out_error) == TINYPY_FALSE) {
        return NULL;
    }
    tinypy_value_t *self = TINYPY_TUPLE_GET(args, 0U);
    if (spec->join != 0 && argument_count == 2U) {
        tinypy_value_t *result = __tinypy_bytearray_join(self, TINYPY_TUPLE_GET(args, 1U), out_error);
        return result;
    }
    if (argument_count == 1U && (name == vm->internal_lower_key || name == vm->internal_upper_key || name == vm->internal_swapcase_key)) {
        tinypy_value_t *return_value_1 = __tinypy_bytearray_ascii_transform(vm, self, name, out_error);
        return return_value_1;
    }
    /* Mutable receivers and buffer arguments are copied after scalar parsers
       have run their Python callbacks, as the native bytearray methods do. */
    tinypy_bool_t bounds = name == vm->internal_count_key || name == vm->internal_rfind_key || name == vm->internal_index_key || name == vm->internal_rindex_key || name == vm->internal_startswith_key || name == vm->internal_endswith_key;
    size_t scalar_begin = bounds != 0 && argument_count >= 2U ? 2U : 0U;
    tinypy_bool_t c_integer = name == vm->internal_expandtabs_key || name == vm->internal_splitlines_key;
    if (((name == vm->internal_center_key || name == vm->internal_ljust_key || name == vm->internal_rjust_key) && argument_count >= 2U && argument_count <= 3U) ||
        (name == vm->internal_zfill_key && argument_count == 2U) || (c_integer != 0 && argument_count == 2U)) {
        scalar_begin = 1U;
    }
    else if ((name == vm->internal_split_key || name == vm->internal_rsplit_key) && argument_count == 3U) {
        scalar_begin = 2U;
    }
    else if (name == vm->internal_replace_key && argument_count == 4U) {
        scalar_begin = 3U;
    }
    size_t scalar_end = bounds != 0 ? argument_count : scalar_begin + 1U;
    for (index = scalar_begin; scalar_begin != 0U && index < scalar_end; ++index) {
        tinypy_value_t *source = TINYPY_TUPLE_GET(args, index);
        int64_t integer;
        tinypy_bool_t parsed;

        if (bounds != 0 && TINYPY_VALUE_KIND(source) == TINYPY_VALUE_NONE) {
            continue;
        }
        parsed = bounds != 0 ? __tinypy_bytearray_index_argument(source, &integer, TINYPY_TRUE, out_error) : tinypy_internal_integer_as_ssize(source, &integer, out_error);
        if (parsed == 0) {
            goto normalized_error;
        }
        if (c_integer != 0 && (integer < INT32_MIN || integer > INT32_MAX)) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "Python int too large to convert to C int", out_error);
            goto normalized_error;
        }
        normalized[index - 1U] = tinypy_integer_from_i64(vm, integer);
    }
    if (argument_count == 3U && (name == vm->internal_center_key || name == vm->internal_ljust_key || name == vm->internal_rjust_key)) {
        tinypy_value_t *fill = TINYPY_TUPLE_GET(args, 2U);
        tinypy_value_type_e fill_kind = TINYPY_VALUE_KIND(fill);
        if (fill_kind != TINYPY_VALUE_STRING || TINYPY_SIZED_SIZE(fill) != 1U) {
            tinypy_message_part_t parts[] = {
                TINYPY_MESSAGE_PART_TEXT(name), TINYPY_MESSAGE_PART_LITERAL("() argument 2 must be char, not "),
                {fill_kind == TINYPY_VALUE_NONE ? "None" : fill->type->name, fill_kind == TINYPY_VALUE_NONE ? 4U : fill->type->name_size},
            };
            tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
            goto normalized_error;
        }
    }
    if (name == vm->internal_startswith_key || name == vm->internal_endswith_key) {
        tinypy_value_t *result = __tinypy_bytearray_prefix(self, TINYPY_TUPLE_GET(args, 1U), normalized, argument_count, name == vm->internal_endswith_key, out_error);
        for (index = 0U; index < 3U; ++index) {
            if (normalized[index] != NULL) {
                TINYPY_DECREF(normalized[index]);
            }
        }
        return result;
    }
    size_t buffer_count = 0U;
    tinypy_bool_t optional_buffer = TINYPY_FALSE;
    tinypy_bool_t partition = name == vm->internal_partition_key || name == vm->internal_rpartition_key;
    if (name == vm->internal_replace_key) {
        buffer_count = 2U;
    }
    else if (name == vm->internal_count_key || partition != TINYPY_FALSE) {
        buffer_count = 1U;
    }
    else if (name == vm->internal_split_key || name == vm->internal_rsplit_key || name == vm->internal_strip_key || name == vm->internal_lstrip_key || name == vm->internal_rstrip_key || name == vm->internal_translate_key) {
        buffer_count = argument_count > 1U ? 1U : 0U;
        optional_buffer = TINYPY_TRUE;
    }
    for (index = 1U; index <= buffer_count; ++index) {
        tinypy_value_t *source = TINYPY_TUPLE_GET(args, index);
        if (optional_buffer != TINYPY_FALSE && TINYPY_VALUE_KIND(source) == TINYPY_VALUE_NONE) {
            continue;
        }
        const uint8_t *bytes;
        size_t size;
        if (__tinypy_bytearray_buffer_argument(source, partition, &bytes, &size, out_error) == TINYPY_FALSE) {
            goto normalized_error;
        }
        if (name == vm->internal_translate_key && size != 256U) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "translation table must be 256 characters long", out_error);
            goto normalized_error;
        }
    }
    if (name == vm->internal_translate_key && argument_count == 3U) {
        const uint8_t *bytes;
        size_t size;
        if (__tinypy_bytearray_buffer_argument(TINYPY_TUPLE_GET(args, 2U), TINYPY_FALSE, &bytes, &size, out_error) == TINYPY_FALSE) {
            goto normalized_error;
        }
    }
    tinypy_value_t *string = tinypy_internal_string_from_bytes_checked(vm, TINYPY_BYTEARRAY_OBJECT(self)->bytes, TINYPY_SIZED_SIZE(self), out_error);
    if (string == NULL) {
        goto normalized_error;
    }
    tinypy_value_t *method = tinypy_object_get_attr_value(string, name, out_error);
    if (method == NULL) {
        TINYPY_DECREF(string);
        goto normalized_error;
    }
    for (index = 1U; index < argument_count; ++index) {
        tinypy_value_t *value = normalized[index - 1U] != NULL ? normalized[index - 1U] : TINYPY_TUPLE_GET(args, index);
        converted[index - 1U] = __tinypy_bytearray_bridge_argument(vm, value, out_error);
        if (converted[index - 1U] == NULL) {
            while (index > 1U) {
                TINYPY_DECREF(converted[--index - 1U]);
            }
            TINYPY_DECREF(method);
            TINYPY_DECREF(string);
            goto normalized_error;
        }
    }
    tinypy_value_t *method_args = tinypy_tuple_from_items(vm, converted, argument_count - 1U);
    tinypy_value_t *value = tinypy_call(method, method_args, decode != TINYPY_FALSE ? kwargs : NULL, out_error);
    TINYPY_DECREF(method_args);
    while (argument_count > 1U) {
        TINYPY_DECREF(converted[--argument_count - 1U]);
    }
    TINYPY_DECREF(method);
    TINYPY_DECREF(string);
    for (index = 0U; index < 3U; ++index) {
        if (normalized[index] != NULL) {
            TINYPY_DECREF(normalized[index]);
        }
    }
    if (value == NULL) {
        return NULL;
    }
    tinypy_value_t *return_value_1 = __tinypy_bytearray_bridge_value(vm, value, spec->result, out_error);
    return return_value_1;
normalized_error:
    for (index = 0U; index < 3U; ++index) {
        if (normalized[index] != NULL) {
            TINYPY_DECREF(normalized[index]);
        }
    }
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_bytearray_plain_index(tinypy_value_t *value, int64_t *out_index, tinypy_error_t **out_error) {
    tinypy_bool_t return_value_1 = tinypy_internal_integer_as_ssize(value, out_index, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_bytearray_insert_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    int64_t index;
    uint8_t byte;

    (void)user_data;
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 2U, 2U, TINYPY_ARITY_STYLE_PARSED, out_error) == TINYPY_FALSE) {
        return NULL;
    }
    if (__tinypy_bytearray_plain_index(TINYPY_TUPLE_GET(args, 1U), &index, out_error) == TINYPY_FALSE) {
        return NULL;
    }
    if (__tinypy_bytearray_item(vm, TINYPY_TUPLE_GET(args, 2U), &byte, out_error) == TINYPY_FALSE) {
        return NULL;
    }
    tinypy_value_t *value = TINYPY_TUPLE_GET(args, 0U);
    size_t size = TINYPY_SIZED_SIZE(value);
    if (size == SIZE_MAX) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "bytearray is too large", out_error);
        return NULL;
    }
    if (tinypy_internal_bytearray_resize_allowed(value, size + 1U, out_error) == 0) {
        return NULL;
    }
    if (index < 0) {
        index = index < -(int64_t)size ? 0 : index + (int64_t)size;
    }
    if ((uint64_t)index > (uint64_t)size) {
        index = (int64_t)size;
    }
    if (__tinypy_bytearray_reserve_checked(value, size + 1U, out_error) == 0) {
        return NULL;
    }
    if ((size_t)index < size) {
        (void)memmove(TINYPY_BYTEARRAY_OBJECT(value)->bytes + (size_t)index + 1U, TINYPY_BYTEARRAY_OBJECT(value)->bytes + (size_t)index, size - (size_t)index);
    }
    TINYPY_BYTEARRAY_OBJECT(value)->bytes[(size_t)index] = byte;
    TINYPY_SIZED_SIZE(value) = size + 1U;
    tinypy_value_t *return_value_1 = TINYPY_RET_NONE(vm);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_bytearray_pop_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    int64_t index = INT64_C(-1);

    (void)user_data;
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 0U, 1U, TINYPY_ARITY_STYLE_PARSED, out_error) == 0) {
        return NULL;
    }
    if (TINYPY_TUPLE_SIZE(args) == 2U && __tinypy_bytearray_plain_index(TINYPY_TUPLE_GET(args, 1U), &index, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *value = TINYPY_TUPLE_GET(args, 0U);
    size_t size = TINYPY_SIZED_SIZE(value);
    if (size == 0U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_INDEX, "pop from empty bytearray", out_error);
        return NULL;
    }
    if (index < 0) {
        index += (int64_t)size;
    }
    if (index < 0 || (uint64_t)index >= (uint64_t)size) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_INDEX, "pop index out of range", out_error);
        return NULL;
    }
    if (tinypy_internal_bytearray_resize_allowed(value, size - 1U, out_error) == 0) {
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
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 1U, 1U, TINYPY_ARITY_STYLE_SINGLE, out_error) == TINYPY_FALSE) {
        return NULL;
    }
    if (__tinypy_bytearray_item(vm, TINYPY_TUPLE_GET(args, 1U), &byte, out_error) == TINYPY_FALSE) {
        return NULL;
    }
    tinypy_value_t *value = TINYPY_TUPLE_GET(args, 0U);
    for (index = 0U; index < TINYPY_SIZED_SIZE(value); ++index) {
        if (TINYPY_BYTEARRAY_OBJECT(value)->bytes[index] == byte) {
            if (tinypy_internal_bytearray_resize_allowed(value, TINYPY_SIZED_SIZE(value) - 1U, out_error) == 0) {
                return NULL;
            }
            __tinypy_bytearray_delete_index(value, index);
            tinypy_value_t *return_value_1 = TINYPY_RET_NONE(vm);
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
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 0U, 0U, TINYPY_ARITY_STYLE_PARSED, out_error) == 0) {
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
    tinypy_value_t *return_value_1 = TINYPY_RET_NONE(vm);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_bytearray_alloc_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 0U, 0U, TINYPY_ARITY_STYLE_PARSED, out_error) == 0) {
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
    return byte == (uint8_t)' ' ? TINYPY_TRUE : TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_bytearray_fromhex_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    const uint8_t *text;
    size_t text_size;
    size_t input = 0U;
    size_t output = 0U;

    (void)user_data;
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 1U, 1U, TINYPY_ARITY_STYLE_PARSED, out_error) == TINYPY_FALSE) {
        return NULL;
    }
    tinypy_value_t *source = TINYPY_TUPLE_GET(args, 1U);
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(source);
    if (kind != TINYPY_VALUE_STRING && kind != TINYPY_VALUE_UNICODE && kind != TINYPY_VALUE_BUFFER) {
        tinypy_message_part_t parts[] = {
            TINYPY_MESSAGE_PART_LITERAL("fromhex() argument 1 must be string or read-only buffer, not "),
            {kind == TINYPY_VALUE_NONE ? "None" : source->type->name, kind == TINYPY_VALUE_NONE ? 4U : source->type->name_size},
        };
        tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
        return NULL;
    }
    tinypy_value_t *encoded = NULL;
    if (TINYPY_VALUE_KIND(source) == TINYPY_VALUE_UNICODE) {
        encoded = tinypy_internal_text_codec(vm, source, NULL, NULL, TINYPY_FALSE, TINYPY_TRUE, NULL, out_error);
        if (encoded == NULL) {
            return NULL;
        }
        source = encoded;
    }
    if (kind == TINYPY_VALUE_BUFFER) {
        text = (const uint8_t *)tinypy_buffer_view(source, &text_size);
    }
    else {
        text = TINYPY_TEXT_BYTES(source);
        text_size = TINYPY_TEXT_BYTE_SIZE(source);
    }
    tinypy_value_t *result = __tinypy_bytearray_allocate_checked(vm, text_size / 2U, out_error);
    if (result == NULL) {
        if (encoded != NULL) {
            TINYPY_DECREF(encoded);
        }
        return NULL;
    }
    while (input < text_size) {
        int32_t high;
        int32_t low;

        while (input < text_size && __tinypy_bytearray_hex_space(text[input]) != 0) {
            input += 1U;
        }
        if (input == text_size) {
            break;
        }
        size_t position = input;
        high = __tinypy_bytearray_hex_digit(text[input++]);
        if (high < 0 || input == text_size || (low = __tinypy_bytearray_hex_digit(text[input++])) < 0) {
            TINYPY_DECREF(result);
            if (encoded != NULL) {
                TINYPY_DECREF(encoded);
            }
            char message[112];
            (void)snprintf(message, sizeof(message), "non-hexadecimal number found in fromhex() arg at position %zu", position);
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, message, out_error);
            return NULL;
        }
        TINYPY_BYTEARRAY_OBJECT(result)->bytes[output++] = (uint8_t)((high << 4) | low);
    }
    TINYPY_SIZED_SIZE(result) = output;
    if (encoded != NULL) {
        TINYPY_DECREF(encoded);
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_hash_t __tinypy_bytearray_hash(tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);

    if (value->type == &vm->types[TINYPY_VALUE_BYTEARRAY]) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "unhashable type", out_error);
        return (tinypy_hash_t)0;
    }
    tinypy_hash_t hash = (tinypy_hash_t)((uintptr_t)value >> 4U);
    return hash == (tinypy_hash_t)-1 ? (tinypy_hash_t)-2 : hash;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_initialize_bytearray_methods(tinypy_vm_t *vm) {
    static const tinypy_bytearray_bridge_spec_t bridge_specs[] = {
        {offsetof(tinypy_vm_t, internal_capitalize_key), TINYPY_BYTEARRAY_BRIDGE_VALUE, TINYPY_FALSE, 0U, 0U, TINYPY_ARITY_STYLE_PARSED},
        {offsetof(tinypy_vm_t, internal_center_key), TINYPY_BYTEARRAY_BRIDGE_VALUE, TINYPY_FALSE, 1U, 2U, TINYPY_ARITY_STYLE_PARSED},
        {offsetof(tinypy_vm_t, internal_count_key), TINYPY_BYTEARRAY_BRIDGE_DIRECT, TINYPY_FALSE, 1U, 3U, TINYPY_ARITY_STYLE_PARSED},
        {offsetof(tinypy_vm_t, internal_decode_key), TINYPY_BYTEARRAY_BRIDGE_DIRECT, TINYPY_FALSE, 0U, 2U, TINYPY_ARITY_STYLE_PARSED},
        {offsetof(tinypy_vm_t, internal_endswith_key), TINYPY_BYTEARRAY_BRIDGE_DIRECT, TINYPY_FALSE, 1U, 3U, TINYPY_ARITY_STYLE_PARSED},
        {offsetof(tinypy_vm_t, internal_expandtabs_key), TINYPY_BYTEARRAY_BRIDGE_VALUE, TINYPY_FALSE, 0U, 1U, TINYPY_ARITY_STYLE_PARSED},
        {offsetof(tinypy_vm_t, internal_isalnum_key), TINYPY_BYTEARRAY_BRIDGE_DIRECT, TINYPY_FALSE, 0U, 0U, TINYPY_ARITY_STYLE_PARSED},
        {offsetof(tinypy_vm_t, internal_isalpha_key), TINYPY_BYTEARRAY_BRIDGE_DIRECT, TINYPY_FALSE, 0U, 0U, TINYPY_ARITY_STYLE_PARSED},
        {offsetof(tinypy_vm_t, internal_isdigit_key), TINYPY_BYTEARRAY_BRIDGE_DIRECT, TINYPY_FALSE, 0U, 0U, TINYPY_ARITY_STYLE_PARSED},
        {offsetof(tinypy_vm_t, internal_islower_key), TINYPY_BYTEARRAY_BRIDGE_DIRECT, TINYPY_FALSE, 0U, 0U, TINYPY_ARITY_STYLE_PARSED},
        {offsetof(tinypy_vm_t, internal_isspace_key), TINYPY_BYTEARRAY_BRIDGE_DIRECT, TINYPY_FALSE, 0U, 0U, TINYPY_ARITY_STYLE_PARSED},
        {offsetof(tinypy_vm_t, internal_istitle_key), TINYPY_BYTEARRAY_BRIDGE_DIRECT, TINYPY_FALSE, 0U, 0U, TINYPY_ARITY_STYLE_PARSED},
        {offsetof(tinypy_vm_t, internal_isupper_key), TINYPY_BYTEARRAY_BRIDGE_DIRECT, TINYPY_FALSE, 0U, 0U, TINYPY_ARITY_STYLE_PARSED},
        {offsetof(tinypy_vm_t, internal_join_key), TINYPY_BYTEARRAY_BRIDGE_VALUE, TINYPY_TRUE, 1U, 1U, TINYPY_ARITY_STYLE_SINGLE},
        {offsetof(tinypy_vm_t, internal_ljust_key), TINYPY_BYTEARRAY_BRIDGE_VALUE, TINYPY_FALSE, 1U, 2U, TINYPY_ARITY_STYLE_PARSED},
        {offsetof(tinypy_vm_t, internal_lower_key), TINYPY_BYTEARRAY_BRIDGE_VALUE, TINYPY_FALSE, 0U, 0U, TINYPY_ARITY_STYLE_PARSED},
        {offsetof(tinypy_vm_t, internal_lstrip_key), TINYPY_BYTEARRAY_BRIDGE_VALUE, TINYPY_FALSE, 0U, 1U, TINYPY_ARITY_STYLE_PARSED},
        {offsetof(tinypy_vm_t, internal_partition_key), TINYPY_BYTEARRAY_BRIDGE_TUPLE, TINYPY_FALSE, 1U, 1U, TINYPY_ARITY_STYLE_SINGLE},
        {offsetof(tinypy_vm_t, internal_replace_key), TINYPY_BYTEARRAY_BRIDGE_VALUE, TINYPY_FALSE, 2U, 3U, TINYPY_ARITY_STYLE_PARSED},
        {offsetof(tinypy_vm_t, internal_rjust_key), TINYPY_BYTEARRAY_BRIDGE_VALUE, TINYPY_FALSE, 1U, 2U, TINYPY_ARITY_STYLE_PARSED},
        {offsetof(tinypy_vm_t, internal_rpartition_key), TINYPY_BYTEARRAY_BRIDGE_TUPLE, TINYPY_FALSE, 1U, 1U, TINYPY_ARITY_STYLE_SINGLE},
        {offsetof(tinypy_vm_t, internal_rsplit_key), TINYPY_BYTEARRAY_BRIDGE_LIST, TINYPY_FALSE, 0U, 2U, TINYPY_ARITY_STYLE_PARSED},
        {offsetof(tinypy_vm_t, internal_rstrip_key), TINYPY_BYTEARRAY_BRIDGE_VALUE, TINYPY_FALSE, 0U, 1U, TINYPY_ARITY_STYLE_PARSED},
        {offsetof(tinypy_vm_t, internal_split_key), TINYPY_BYTEARRAY_BRIDGE_LIST, TINYPY_FALSE, 0U, 2U, TINYPY_ARITY_STYLE_PARSED},
        {offsetof(tinypy_vm_t, internal_splitlines_key), TINYPY_BYTEARRAY_BRIDGE_LIST, TINYPY_FALSE, 0U, 1U, TINYPY_ARITY_STYLE_PARSED},
        {offsetof(tinypy_vm_t, internal_startswith_key), TINYPY_BYTEARRAY_BRIDGE_DIRECT, TINYPY_FALSE, 1U, 3U, TINYPY_ARITY_STYLE_PARSED},
        {offsetof(tinypy_vm_t, internal_strip_key), TINYPY_BYTEARRAY_BRIDGE_VALUE, TINYPY_FALSE, 0U, 1U, TINYPY_ARITY_STYLE_PARSED},
        {offsetof(tinypy_vm_t, internal_swapcase_key), TINYPY_BYTEARRAY_BRIDGE_VALUE, TINYPY_FALSE, 0U, 0U, TINYPY_ARITY_STYLE_PARSED},
        {offsetof(tinypy_vm_t, internal_title_key), TINYPY_BYTEARRAY_BRIDGE_VALUE, TINYPY_FALSE, 0U, 0U, TINYPY_ARITY_STYLE_PARSED},
        {offsetof(tinypy_vm_t, internal_translate_key), TINYPY_BYTEARRAY_BRIDGE_VALUE, TINYPY_FALSE, 1U, 2U, TINYPY_ARITY_STYLE_UNPACK},
        {offsetof(tinypy_vm_t, internal_upper_key), TINYPY_BYTEARRAY_BRIDGE_VALUE, TINYPY_FALSE, 0U, 0U, TINYPY_ARITY_STYLE_PARSED},
        {offsetof(tinypy_vm_t, internal_zfill_key), TINYPY_BYTEARRAY_BRIDGE_VALUE, TINYPY_FALSE, 1U, 1U, TINYPY_ARITY_STYLE_PARSED}
    };
    size_t index;

    tinypy_internal_type_add_method(&vm->types[TINYPY_VALUE_BYTEARRAY], vm->internal_special_add_key, __tinypy_bytearray_add_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(&vm->types[TINYPY_VALUE_BYTEARRAY], vm->internal_special_mul_key, __tinypy_bytearray_multiply_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(&vm->types[TINYPY_VALUE_BYTEARRAY], vm->internal_special_rmul_key, __tinypy_bytearray_multiply_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(&vm->types[TINYPY_VALUE_BYTEARRAY], vm->internal_special_iadd_key, __tinypy_bytearray_inplace_add_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(&vm->types[TINYPY_VALUE_BYTEARRAY], vm->internal_special_imul_key, __tinypy_bytearray_inplace_multiply_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(&vm->types[TINYPY_VALUE_BYTEARRAY], vm->internal_append_key, __tinypy_bytearray_append_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(&vm->types[TINYPY_VALUE_BYTEARRAY], vm->internal_extend_key, __tinypy_bytearray_extend_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(&vm->types[TINYPY_VALUE_BYTEARRAY], vm->internal_find_key, __tinypy_bytearray_find_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(&vm->types[TINYPY_VALUE_BYTEARRAY], vm->internal_rfind_key, __tinypy_bytearray_find_method, vm->internal_rfind_key, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(&vm->types[TINYPY_VALUE_BYTEARRAY], vm->internal_index_key, __tinypy_bytearray_find_method, vm->internal_index_key, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(&vm->types[TINYPY_VALUE_BYTEARRAY], vm->internal_rindex_key, __tinypy_bytearray_find_method, vm->internal_rindex_key, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(&vm->types[TINYPY_VALUE_BYTEARRAY], vm->internal_insert_key, __tinypy_bytearray_insert_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(&vm->types[TINYPY_VALUE_BYTEARRAY], vm->internal_pop_key, __tinypy_bytearray_pop_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(&vm->types[TINYPY_VALUE_BYTEARRAY], vm->internal_remove_key, __tinypy_bytearray_remove_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(&vm->types[TINYPY_VALUE_BYTEARRAY], vm->internal_reverse_key, __tinypy_bytearray_reverse_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(&vm->types[TINYPY_VALUE_BYTEARRAY], vm->internal_special_alloc_key, __tinypy_bytearray_alloc_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_class_method(&vm->types[TINYPY_VALUE_BYTEARRAY], vm->internal_fromhex_key, __tinypy_bytearray_fromhex_method, NULL, NULL);
    for (index = 0U; index < sizeof(bridge_specs) / sizeof(bridge_specs[0]); ++index) {
        const tinypy_bytearray_bridge_spec_t *bridge = &bridge_specs[index];
        tinypy_value_t *name = *(tinypy_value_t **)((uint8_t *)vm + bridge->name_offset);
        tinypy_internal_type_add_method(&vm->types[TINYPY_VALUE_BYTEARRAY], name, __tinypy_bytearray_bridge_method, (void *)bridge, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    }
    vm->types[TINYPY_VALUE_BYTEARRAY].hash = __tinypy_bytearray_hash;
}
