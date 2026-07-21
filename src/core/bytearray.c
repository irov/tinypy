#include "tinypy/bytearray.h"

#include "internal.h"

#include <assert.h>
#include <string.h>

typedef struct tinypy_bytearray_slice_t {
    int64_t start;
    int64_t stop;
    int64_t step;
    size_t length;
} tinypy_bytearray_slice_t;

//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_bytearray_integer(tinypy_value_t *value, int64_t *out_value) {
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);

    if (kind == TINYPY_VALUE_BOOL || kind == TINYPY_VALUE_INTEGER) {
        *out_value = TINYPY_INTEGER_VALUE(value);
        return INT32_C(1);
    }
    if (kind == TINYPY_VALUE_LONG && TINYPY_LONG_DIGIT_COUNT(value) <= 5U) {
        uint64_t magnitude = UINT64_C(0);
        size_t index = TINYPY_LONG_DIGIT_COUNT(value);

        while (index != 0U) {
            index -= 1U;
            if (magnitude > (UINT64_MAX >> 15U)) {
                return INT32_C(0);
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
    return INT32_C(0);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_bytearray_reserve(tinypy_value_t *value, size_t minimum) {
    tinypy_bytearray_object_t *bytearray = TINYPY_BYTEARRAY_OBJECT(value);
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    size_t capacity;

    if (minimum <= bytearray->capacity) {
        return;
    }
    capacity = bytearray->capacity == 0U ? 16U : bytearray->capacity;
    while (capacity < minimum) {
        size_t grown = capacity + (capacity >> 1U) + 1U;

        assert(grown > capacity);
        capacity = grown;
    }
    if (bytearray->bytes == NULL) {
        bytearray->bytes = (unsigned char *)tinypy_internal_vm_allocate(vm, capacity, (uint32_t)TINYPY_ALLOC_TAG_BYTEARRAY_DATA);
    }
    else {
        bytearray->bytes = (unsigned char *)tinypy_internal_vm_reallocate(vm, bytearray->bytes, bytearray->capacity, capacity, (uint32_t)TINYPY_ALLOC_TAG_BYTEARRAY_DATA);
    }
    bytearray->capacity = capacity;
}
//////////////////////////////////////////////////////////////////////////
int32_t tinypy_internal_bytes_view(const tinypy_value_t *value, const unsigned char **out_bytes, size_t *out_size) {
    switch (TINYPY_VALUE_KIND(value)) {
    case TINYPY_VALUE_STRING:
        *out_bytes = (const unsigned char *)tinypy_string_view(value, out_size);
        return INT32_C(1);
    case TINYPY_VALUE_BYTEARRAY:
        *out_bytes = (const unsigned char *)tinypy_bytearray_view(value, out_size);
        return INT32_C(1);
    case TINYPY_VALUE_BUFFER:
        *out_bytes = (const unsigned char *)tinypy_buffer_view(value, out_size);
        return INT32_C(1);
    default:
        return INT32_C(0);
    }
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_bytearray_item(tinypy_vm_t *vm, tinypy_value_t *value, unsigned char *out_byte, tinypy_error_t **out_error) {
    int64_t integer;

    if (__tinypy_bytearray_integer(value, &integer) != 0 && integer >= 0 && integer <= 255) {
        *out_byte = (unsigned char)integer;
        return INT32_C(1);
    }
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "byte must be in range(0, 256)", out_error);
    return INT32_C(0);
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_bytearray_collect(tinypy_vm_t *vm, tinypy_value_t *source, unsigned char **out_bytes, size_t *out_size, tinypy_error_t **out_error) {
    const unsigned char *view;
    size_t view_size;
    unsigned char *bytes = NULL;
    size_t size = 0U;
    size_t capacity = 0U;
    tinypy_value_t *iterator;
    tinypy_error_t *iteration_error = NULL;

    *out_bytes = NULL;
    *out_size = 0U;
    if (tinypy_internal_bytes_view(source, &view, &view_size) != 0) {
        if (view_size != 0U) {
            bytes = (unsigned char *)tinypy_internal_vm_allocate(vm, view_size, (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
            (void)memcpy(bytes, view, view_size);
        }
        *out_bytes = bytes;
        *out_size = view_size;
        return INT32_C(1);
    }
    iterator = tinypy_iter(source, out_error);
    if (iterator == NULL) {
        return INT32_C(0);
    }
    for (;;) {
        tinypy_value_t *item = tinypy_next(iterator, &iteration_error);
        unsigned char byte;

        if (item == NULL) {
            break;
        }
        if (__tinypy_bytearray_item(vm, item, &byte, out_error) == 0) {
            TINYPY_DECREF(item);
            TINYPY_DECREF(iterator);
            if (bytes != NULL) {
                tinypy_internal_vm_deallocate(vm, bytes, capacity, (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
            }
            return INT32_C(0);
        }
        TINYPY_DECREF(item);
        if (size == capacity) {
            size_t new_capacity = capacity == 0U ? 16U : capacity * 2U;

            assert(new_capacity > capacity);
            if (bytes == NULL) {
                bytes = (unsigned char *)tinypy_internal_vm_allocate(vm, new_capacity, (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
            }
            else {
                bytes = (unsigned char *)tinypy_internal_vm_reallocate(vm, bytes, capacity, new_capacity, (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
            }
            capacity = new_capacity;
        }
        bytes[size] = byte;
        size += 1U;
    }
    TINYPY_DECREF(iterator);
    if (iteration_error != NULL) {
        if (bytes != NULL) {
            tinypy_internal_vm_deallocate(vm, bytes, capacity, (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
        }
        if (out_error != NULL) {
            *out_error = iteration_error;
        }
        else {
            tinypy_error_release(iteration_error);
        }
        return INT32_C(0);
    }
    if (size != 0U && capacity != size) {
        bytes = (unsigned char *)tinypy_internal_vm_reallocate(vm, bytes, capacity, size, (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    }
    *out_bytes = bytes;
    *out_size = size;
    return INT32_C(1);
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_bytearray_slice_integer(tinypy_vm_t *vm, tinypy_value_t *value, int64_t *out_value, tinypy_error_t **out_error) {
    if (__tinypy_bytearray_integer(value, out_value) != 0) {
        return INT32_C(1);
    }
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "slice indices must be integers or None", out_error);
    return INT32_C(0);
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_bytearray_slice(tinypy_vm_t *vm, tinypy_value_t *slice_value, size_t size, tinypy_bytearray_slice_t *out_slice, tinypy_error_t **out_error) {
    tinypy_value_t *start_value = tinypy_slice_start(slice_value);
    tinypy_value_t *stop_value = tinypy_slice_stop(slice_value);
    tinypy_value_t *step_value = tinypy_slice_step(slice_value);
    int64_t sequence_size;
    int64_t lower;
    int64_t upper;

    assert(size <= (size_t)INT64_MAX);
    sequence_size = (int64_t)size;
    if (TINYPY_VALUE_KIND(step_value) == TINYPY_VALUE_NONE) {
        out_slice->step = INT64_C(1);
    }
    else if (__tinypy_bytearray_slice_integer(vm, step_value, &out_slice->step, out_error) == 0) {
        return INT32_C(0);
    }
    if (out_slice->step == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "slice step cannot be zero", out_error);
        return INT32_C(0);
    }
    if (out_slice->step == INT64_MIN) {
        out_slice->step = -INT64_MAX;
    }
    lower = out_slice->step < 0 ? -1 : 0;
    upper = out_slice->step < 0 ? sequence_size - 1 : sequence_size;
    if (TINYPY_VALUE_KIND(start_value) == TINYPY_VALUE_NONE) {
        out_slice->start = out_slice->step < 0 ? upper : lower;
    }
    else {
        if (__tinypy_bytearray_slice_integer(vm, start_value, &out_slice->start, out_error) == 0) {
            return INT32_C(0);
        }
        if (out_slice->start < 0) {
            out_slice->start += sequence_size;
        }
        if (out_slice->start < lower) {
            out_slice->start = lower;
        }
        if (out_slice->start > upper) {
            out_slice->start = upper;
        }
    }
    if (TINYPY_VALUE_KIND(stop_value) == TINYPY_VALUE_NONE) {
        out_slice->stop = out_slice->step < 0 ? lower : upper;
    }
    else {
        if (__tinypy_bytearray_slice_integer(vm, stop_value, &out_slice->stop, out_error) == 0) {
            return INT32_C(0);
        }
        if (out_slice->stop < 0) {
            out_slice->stop += sequence_size;
        }
        if (out_slice->stop < lower) {
            out_slice->stop = lower;
        }
        if (out_slice->stop > upper) {
            out_slice->stop = upper;
        }
    }
    out_slice->length = 0U;
    if (out_slice->step < 0 && out_slice->stop < out_slice->start) {
        out_slice->length = (size_t)(1 + (out_slice->start - out_slice->stop - 1) / -out_slice->step);
    }
    else if (out_slice->step > 0 && out_slice->start < out_slice->stop) {
        out_slice->length = (size_t)(1 + (out_slice->stop - out_slice->start - 1) / out_slice->step);
    }
    return INT32_C(1);
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_bytearray_index(tinypy_vm_t *vm, tinypy_value_t *key, size_t size, size_t *out_index, tinypy_error_t **out_error) {
    int64_t index;

    if (__tinypy_bytearray_integer(key, &index) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "bytearray index must be an integer", out_error);
        return INT32_C(0);
    }
    if (index < 0) {
        uint64_t distance = (uint64_t)(-(index + INT64_C(1))) + UINT64_C(1);

        if (distance > size) {
            goto out_of_range;
        }
        *out_index = size - (size_t)distance;
        return INT32_C(1);
    }
    if ((uint64_t)index >= (uint64_t)size) {
        goto out_of_range;
    }
    *out_index = (size_t)index;
    return INT32_C(1);
out_of_range:
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_INDEX, "bytearray index out of range", out_error);
    return INT32_C(0);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_bytearray_replace_slice(tinypy_value_t *value, const tinypy_bytearray_slice_t *slice, const unsigned char *replacement, size_t replacement_size) {
    tinypy_bytearray_object_t *bytearray = TINYPY_BYTEARRAY_OBJECT(value);
    size_t old_size = TINYPY_SIZED_SIZE(value);
    size_t index;

    if (slice->step == 1) {
        size_t new_size;
        size_t tail_start = (size_t)slice->start + slice->length;
        size_t tail_size = old_size - tail_start;

        assert(old_size - slice->length <= SIZE_MAX - replacement_size);
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
    assert(replacement_size == slice->length);
    for (index = 0U; index < replacement_size; ++index) {
        bytearray->bytes[(size_t)(slice->start + (int64_t)index * slice->step)] = replacement[index];
    }
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_bytearray_delete_index(tinypy_value_t *value, size_t index) {
    tinypy_bytearray_object_t *bytearray = TINYPY_BYTEARRAY_OBJECT(value);
    size_t size = TINYPY_SIZED_SIZE(value);

    assert(index < size);
    if (index + 1U < size) {
        (void)memmove(bytearray->bytes + index, bytearray->bytes + index + 1U, size - index - 1U);
    }
    TINYPY_SIZED_SIZE(value) -= 1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_bytearray_from_bytes(tinypy_vm_t *vm, const void *bytes, size_t size) {
    tinypy_bytearray_object_t *bytearray;

    assert(tinypy_internal_vm_valid(vm));
    assert(bytes != NULL || size == 0U);
    bytearray = (tinypy_bytearray_object_t *)tinypy_internal_value_allocate(vm, TINYPY_VALUE_BYTEARRAY, sizeof(*bytearray));
    if (size != 0U) {
        bytearray->bytes = (unsigned char *)tinypy_internal_vm_allocate(vm, size, (uint32_t)TINYPY_ALLOC_TAG_BYTEARRAY_DATA);
        bytearray->capacity = size;
        (void)memcpy(bytearray->bytes, bytes, size);
    }
    bytearray->base.size = size;
    return &bytearray->base.base;
}
//////////////////////////////////////////////////////////////////////////
size_t tinypy_bytearray_size(const tinypy_value_t *value) {
    assert(value != NULL);
    assert(tinypy_internal_vm_valid(TINYPY_VALUE_VM(value)));
    assert(TINYPY_VALUE_KIND(value) == TINYPY_VALUE_BYTEARRAY);
    return TINYPY_SIZED_SIZE(value);
}
//////////////////////////////////////////////////////////////////////////
const void *tinypy_bytearray_view(const tinypy_value_t *value, size_t *out_size) {
    assert(value != NULL);
    assert(tinypy_internal_vm_valid(TINYPY_VALUE_VM(value)));
    assert(TINYPY_VALUE_KIND(value) == TINYPY_VALUE_BYTEARRAY);
    assert(out_size != NULL);
    *out_size = TINYPY_SIZED_SIZE(value);
    return TINYPY_BYTEARRAY_OBJECT((tinypy_value_t *)value)->bytes;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_bytearray_set(tinypy_value_t *value, size_t index, uint8_t byte) {
    assert(value != NULL);
    assert(tinypy_internal_vm_valid(TINYPY_VALUE_VM(value)));
    assert(TINYPY_VALUE_KIND(value) == TINYPY_VALUE_BYTEARRAY);
    assert(index < TINYPY_SIZED_SIZE(value));
    TINYPY_BYTEARRAY_OBJECT(value)->bytes[index] = byte;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_bytearray_destroy(tinypy_vm_t *vm, tinypy_value_t *value) {
    tinypy_bytearray_object_t *bytearray = TINYPY_BYTEARRAY_OBJECT(value);

    if (bytearray->bytes == NULL) {
        return;
    }
    tinypy_internal_vm_deallocate(vm, bytearray->bytes, bytearray->capacity, (uint32_t)TINYPY_ALLOC_TAG_BYTEARRAY_DATA);
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_bytearray_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = type->vm;
    tinypy_value_t *source;
    unsigned char *bytes;
    size_t size;
    int64_t requested_size;
    tinypy_value_t *result;

    if ((kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U) || TINYPY_TUPLE_SIZE(args) > 1U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "bytearray constructor received invalid arguments", out_error);
        return NULL;
    }
    if (TINYPY_TUPLE_SIZE(args) == 0U) {
        return tinypy_bytearray_from_bytes(vm, NULL, 0U);
    }
    source = TINYPY_TUPLE_GET(args, 0U);
    if (__tinypy_bytearray_integer(source, &requested_size) != 0) {
        if (requested_size < 0) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "negative count", out_error);
            return NULL;
        }
        assert((uint64_t)requested_size <= (uint64_t)SIZE_MAX);
        result = tinypy_bytearray_from_bytes(vm, NULL, 0U);
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
    result = tinypy_bytearray_from_bytes(vm, bytes, size);
    if (bytes != NULL) {
        tinypy_internal_vm_deallocate(vm, bytes, size, (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
ptrdiff_t tinypy_internal_bytearray_length(tinypy_value_t *value, tinypy_error_t **out_error) {
    TINYPY_CLEAR_ERROR(out_error);
    return TINYPY_SIZED_SIZE(value);
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_bytearray_get_item(tinypy_value_t *value, tinypy_value_t *key, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    size_t size = TINYPY_SIZED_SIZE(value);
    size_t index;

    if (TINYPY_VALUE_KIND(key) == TINYPY_VALUE_SLICE) {
        tinypy_bytearray_slice_t slice;
        tinypy_value_t *result;
        unsigned char *selected;
        size_t selected_index;

        if (__tinypy_bytearray_slice(vm, key, size, &slice, out_error) == 0) {
            return NULL;
        }
        if (slice.length == 0U) {
            return tinypy_bytearray_from_bytes(vm, NULL, 0U);
        }
        if (slice.step == 1) {
            return tinypy_bytearray_from_bytes(vm, TINYPY_BYTEARRAY_OBJECT(value)->bytes + (size_t)slice.start, slice.length);
        }
        selected = (unsigned char *)tinypy_internal_vm_allocate(vm, slice.length, (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
        for (selected_index = 0U; selected_index < slice.length; ++selected_index) {
            selected[selected_index] = TINYPY_BYTEARRAY_OBJECT(value)->bytes[(size_t)(slice.start + (int64_t)selected_index * slice.step)];
        }
        result = tinypy_bytearray_from_bytes(vm, selected, slice.length);
        tinypy_internal_vm_deallocate(vm, selected, slice.length, (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
        return result;
    }
    if (__tinypy_bytearray_index(vm, key, size, &index, out_error) == 0) {
        return NULL;
    }
    return tinypy_integer_from_i64(vm, (int64_t)TINYPY_BYTEARRAY_OBJECT(value)->bytes[index]);
}
//////////////////////////////////////////////////////////////////////////
int32_t tinypy_internal_bytearray_set_item(tinypy_value_t *value, tinypy_value_t *key, tinypy_value_t *item, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    size_t size = TINYPY_SIZED_SIZE(value);
    size_t index;

    if (TINYPY_VALUE_KIND(key) == TINYPY_VALUE_SLICE) {
        tinypy_bytearray_slice_t slice;
        unsigned char *replacement = NULL;
        size_t replacement_size = 0U;
        size_t deletion_index;

        if (__tinypy_bytearray_slice(vm, key, size, &slice, out_error) == 0) {
            return INT32_C(0);
        }
        if (item != NULL) {
            if (__tinypy_bytearray_collect(vm, item, &replacement, &replacement_size, out_error) == 0) {
                return INT32_C(0);
            }
            if (slice.step != 1 && replacement_size != slice.length) {
                if (replacement != NULL) {
                    tinypy_internal_vm_deallocate(vm, replacement, replacement_size, (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
                }
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "extended slice assignment has the wrong size", out_error);
                return INT32_C(0);
            }
            __tinypy_bytearray_replace_slice(value, &slice, replacement, replacement_size);
            if (replacement != NULL) {
                tinypy_internal_vm_deallocate(vm, replacement, replacement_size, (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
            }
            return INT32_C(1);
        }
        if (slice.step == 1) {
            __tinypy_bytearray_replace_slice(value, &slice, NULL, 0U);
            return INT32_C(1);
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
        return INT32_C(1);
    }
    if (__tinypy_bytearray_index(vm, key, size, &index, out_error) == 0) {
        return INT32_C(0);
    }
    if (item == NULL) {
        __tinypy_bytearray_delete_index(value, index);
        return INT32_C(1);
    }
    return __tinypy_bytearray_item(vm, item, &TINYPY_BYTEARRAY_OBJECT(value)->bytes[index], out_error);
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_bytearray_string(tinypy_value_t *value, tinypy_error_t **out_error) {
    TINYPY_CLEAR_ERROR(out_error);
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    return tinypy_string_from_bytes(vm, TINYPY_BYTEARRAY_OBJECT(value)->bytes, TINYPY_SIZED_SIZE(value));
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_bytearray_repr(tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_value_t *string = tinypy_internal_bytearray_string(value, out_error);
    tinypy_value_t *quoted;
    const unsigned char *quoted_bytes;
    size_t quoted_size;
    unsigned char *bytes;
    tinypy_value_t *result;

    if (string == NULL) {
        return NULL;
    }
    quoted = tinypy_object_repr(string, out_error);
    TINYPY_DECREF(string);
    if (quoted == NULL) {
        return NULL;
    }
    quoted_bytes = (const unsigned char *)tinypy_string_view(quoted, &quoted_size);
    assert(quoted_size <= SIZE_MAX - 12U);
    bytes = (unsigned char *)tinypy_internal_vm_allocate(vm, quoted_size + 12U, (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    (void)memcpy(bytes, "bytearray(b", 11U);
    (void)memcpy(bytes + 11U, quoted_bytes, quoted_size);
    bytes[quoted_size + 11U] = (unsigned char)')';
    result = tinypy_string_from_bytes(vm, bytes, quoted_size + 12U);
    tinypy_internal_vm_deallocate(vm, bytes, quoted_size + 12U, (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    TINYPY_DECREF(quoted);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_bytearray_method_arguments(tinypy_vm_t *vm, tinypy_value_t *args, tinypy_value_t *kwargs, size_t minimum, size_t maximum, tinypy_error_t **out_error) {
    size_t count = TINYPY_TUPLE_SIZE(args);

    if ((kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U) || count < minimum || count > maximum) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "bytearray method received invalid arguments", out_error);
        return INT32_C(0);
    }
    tinypy_value_t *item = TINYPY_TUPLE_GET(args, 0U);
    if (TINYPY_VALUE_KIND(item) != TINYPY_VALUE_BYTEARRAY) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "bytearray method requires a bytearray", out_error);
        return INT32_C(0);
    }
    return INT32_C(1);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_bytearray_add_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *left;
    const unsigned char *right_bytes;
    size_t left_size;
    size_t right_size;
    unsigned char *bytes;
    tinypy_value_t *result;

    (void)user_data;
    if (__tinypy_bytearray_method_arguments(vm, args, kwargs, 2U, 2U, out_error) == 0) {
        return NULL;
    }
    left = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *item = TINYPY_TUPLE_GET(args, 1U);
    if (tinypy_internal_bytes_view(item, &right_bytes, &right_size) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "cannot concatenate bytearray with this value", out_error);
        return NULL;
    }
    left_size = TINYPY_SIZED_SIZE(left);
    assert(left_size <= SIZE_MAX - right_size);
    if (left_size + right_size == 0U) {
        return tinypy_bytearray_from_bytes(vm, NULL, 0U);
    }
    bytes = (unsigned char *)tinypy_internal_vm_allocate(vm, left_size + right_size, (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    if (left_size != 0U) {
        (void)memcpy(bytes, TINYPY_BYTEARRAY_OBJECT(left)->bytes, left_size);
    }
    if (right_size != 0U) {
        (void)memcpy(bytes + left_size, right_bytes, right_size);
    }
    result = tinypy_bytearray_from_bytes(vm, bytes, left_size + right_size);
    tinypy_internal_vm_deallocate(vm, bytes, left_size + right_size, (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_bytearray_append_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *value;
    unsigned char byte;
    size_t size;

    (void)user_data;
    if (__tinypy_bytearray_method_arguments(vm, args, kwargs, 2U, 2U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *item = TINYPY_TUPLE_GET(args, 1U);
    if (__tinypy_bytearray_item(vm, item, &byte, out_error) == 0) {
        return NULL;
    }
    value = TINYPY_TUPLE_GET(args, 0U);
    size = TINYPY_SIZED_SIZE(value);
    assert(size != SIZE_MAX);
    __tinypy_bytearray_reserve(value, size + 1U);
    TINYPY_BYTEARRAY_OBJECT(value)->bytes[size] = byte;
    TINYPY_SIZED_SIZE(value) += 1;
    return tinypy_none_get(vm);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_bytearray_extend_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *value;
    unsigned char *extension;
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
    value = TINYPY_TUPLE_GET(args, 0U);
    size = TINYPY_SIZED_SIZE(value);
    assert(size <= SIZE_MAX - extension_size);
    __tinypy_bytearray_reserve(value, size + extension_size);
    if (extension_size != 0U) {
        (void)memcpy(TINYPY_BYTEARRAY_OBJECT(value)->bytes + size, extension, extension_size);
    }
    TINYPY_SIZED_SIZE(value) = size + extension_size;
    if (extension != NULL) {
        tinypy_internal_vm_deallocate(vm, extension, extension_size, (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    }
    return tinypy_none_get(vm);
}
//////////////////////////////////////////////////////////////////////////
static int64_t __tinypy_bytearray_bound(tinypy_vm_t *vm, tinypy_value_t *value, size_t size, int64_t fallback, tinypy_error_t **out_error) {
    int64_t bound;

    if (value == NULL || TINYPY_VALUE_KIND(value) == TINYPY_VALUE_NONE) {
        return fallback;
    }
    if (__tinypy_bytearray_integer(value, &bound) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "slice index must be an integer", out_error);
        return INT64_MIN;
    }
    if (bound < 0) {
        bound = bound < -(int64_t)size ? 0 : bound + (int64_t)size;
    }
    if ((uint64_t)bound > (uint64_t)size) {
        bound = (int64_t)size;
    }
    return bound;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_bytearray_find_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *value;
    const unsigned char *needle;
    size_t size;
    size_t needle_size;
    int64_t start;
    int64_t stop;
    ptrdiff_t found = -1;
    size_t index;
    size_t argument_count;
    tinypy_value_t *start_value;
    tinypy_value_t *stop_value;

    (void)user_data;
    if (__tinypy_bytearray_method_arguments(vm, args, kwargs, 2U, 4U, out_error) == 0) {
        return NULL;
    }
    value = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *item = TINYPY_TUPLE_GET(args, 1U);
    if (tinypy_internal_bytes_view(item, &needle, &needle_size) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "substring must support the buffer interface", out_error);
        return NULL;
    }
    size = TINYPY_SIZED_SIZE(value);
    argument_count = TINYPY_TUPLE_SIZE(args);
    start_value = argument_count >= 3U ? TINYPY_TUPLE_GET(args, 2U) : NULL;
    start = __tinypy_bytearray_bound(vm, start_value, size, 0, out_error);
    if (start == INT64_MIN && out_error != NULL && *out_error != NULL) {
        return NULL;
    }
    stop_value = argument_count >= 4U ? TINYPY_TUPLE_GET(args, 3U) : NULL;
    stop = __tinypy_bytearray_bound(vm, stop_value, size, (int64_t)size, out_error);
    if (stop == INT64_MIN && out_error != NULL && *out_error != NULL) {
        return NULL;
    }
    if (start <= stop && needle_size <= (size_t)(stop - start)) {
        if (needle_size == 0U) {
            found = (ptrdiff_t)start;
        }
        else {
            for (index = (size_t)start; index <= (size_t)stop - needle_size; ++index) {
                if (memcmp(TINYPY_BYTEARRAY_OBJECT(value)->bytes + index, needle, needle_size) == 0) {
                    found = (ptrdiff_t)index;
                    break;
                }
            }
        }
    }
    return tinypy_integer_from_i64(vm, (int64_t)found);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_bytearray_register_method(tinypy_vm_t *vm, const char *name, size_t name_size, tinypy_native_function_callback_t callback) {
    tinypy_value_t *function = tinypy_native_function_new(vm, name, name_size, callback, NULL, NULL);
    tinypy_value_t *key = tinypy_string_from_bytes(vm, name, name_size);

    tinypy_dict_set(vm->bytearray_type.dict, key, function);
    TINYPY_DECREF(key);
    TINYPY_DECREF(function);
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_initialize_bytearray_methods(tinypy_vm_t *vm) {
    tinypy_value_t *hash_key = tinypy_string_from_bytes(vm, "__hash__", 8U);

    __tinypy_bytearray_register_method(vm, "__add__", 7U, __tinypy_bytearray_add_method);
    __tinypy_bytearray_register_method(vm, "append", 6U, __tinypy_bytearray_append_method);
    __tinypy_bytearray_register_method(vm, "extend", 6U, __tinypy_bytearray_extend_method);
    __tinypy_bytearray_register_method(vm, "find", 4U, __tinypy_bytearray_find_method);
    tinypy_dict_set(vm->bytearray_type.dict, hash_key, &vm->none_object.base);
    TINYPY_DECREF(hash_key);
}
