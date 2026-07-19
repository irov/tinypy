#include "tinypy/buffer.h"

#include "internal.h"

#include <assert.h>

static int32_t __tinypy_buffer_supported(const tinypy_value_t *value)
{
    tinypy_value_type_e kind = tinypy_internal_value_kind(value);

    return kind == TINYPY_VALUE_STRING || kind == TINYPY_VALUE_UNICODE || kind == TINYPY_VALUE_BUFFER || kind == TINYPY_VALUE_BYTEARRAY;
}

static const unsigned char *__tinypy_buffer_owner_view(const tinypy_value_t *owner, size_t *out_size)
{
    switch (tinypy_internal_value_kind(owner)) {
    case TINYPY_VALUE_STRING:
        return (const unsigned char *)tinypy_string_view(owner, out_size);
    case TINYPY_VALUE_UNICODE: {
        size_t code_points;

        return (const unsigned char *)tinypy_unicode_utf8_view(owner, out_size, &code_points);
    }
    case TINYPY_VALUE_BUFFER:
        return (const unsigned char *)tinypy_buffer_view(owner, out_size);
    case TINYPY_VALUE_BYTEARRAY:
        return (const unsigned char *)tinypy_bytearray_view(owner, out_size);
    default:
        assert(!"object does not expose a byte buffer");
        *out_size = 0U;
        return NULL;
    }
}

tinypy_value_t *tinypy_buffer_from_object(tinypy_value_t *object, size_t offset, size_t size)
{
    tinypy_vm_t *vm;
    tinypy_buffer_object_t *buffer;
    size_t owner_size;

    assert(object != NULL);
    vm = tinypy_internal_value_vm(object);
    assert(tinypy_internal_vm_valid(vm));
    assert(__tinypy_buffer_supported(object) != 0);
    (void)__tinypy_buffer_owner_view(object, &owner_size);
    assert(offset <= owner_size);
    if (size == TINYPY_BUFFER_TO_END) size = owner_size - offset;
    assert(size <= owner_size - offset);
    buffer = (tinypy_buffer_object_t *)tinypy_internal_value_allocate(vm, TINYPY_VALUE_BUFFER, sizeof(*buffer));
    buffer->owner = object;
    buffer->offset = offset;
    buffer->size = size;
    tinypy_retain(object);
    return &buffer->base;
}

const void *tinypy_buffer_view(const tinypy_value_t *value, size_t *out_size)
{
    const tinypy_buffer_object_t *buffer;
    const unsigned char *bytes;
    size_t owner_size;

    assert(value != NULL);
    assert(tinypy_internal_vm_valid(tinypy_internal_value_vm(value)));
    assert(tinypy_internal_value_kind(value) == TINYPY_VALUE_BUFFER);
    assert(out_size != NULL);
    buffer = TINYPY_BUFFER_OBJECT((tinypy_value_t *)value);
    bytes = __tinypy_buffer_owner_view(buffer->owner, &owner_size);
    assert(buffer->offset <= owner_size);
    assert(buffer->size <= owner_size - buffer->offset);
    *out_size = buffer->size;
    return bytes + buffer->offset;
}

void tinypy_internal_buffer_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data)
{
    visit(TINYPY_BUFFER_OBJECT(value)->owner, user_data);
}

static int32_t __tinypy_buffer_integer_as_i64(tinypy_value_t *value, int64_t *out_value)
{
    tinypy_value_type_e kind = tinypy_internal_value_kind(value);

    if (kind == TINYPY_VALUE_BOOL || kind == TINYPY_VALUE_INTEGER) {
        *out_value = TINYPY_INTEGER_VALUE(value);
        return INT32_C(1);
    }
    if (kind == TINYPY_VALUE_LONG) {
        const uint16_t *digits = TINYPY_LONG_OBJECT(value)->digits;
        size_t count = TINYPY_LONG_DIGIT_COUNT(value);
        uint64_t magnitude = UINT64_C(0);
        uint64_t limit = TINYPY_LONG_SIGN(value) < 0 ? (uint64_t)INT64_MAX + UINT64_C(1) : (uint64_t)INT64_MAX;
        size_t index;

        if (count > 5U) return INT32_C(0);
        for (index = count; index != 0U; index -= 1U) {
            magnitude = (magnitude << 15U) | digits[index - 1U];
        }
        if (magnitude > limit) return INT32_C(0);
        *out_value = TINYPY_LONG_SIGN(value) < 0
            ? (magnitude == (uint64_t)INT64_MAX + UINT64_C(1) ? INT64_MIN : -(int64_t)magnitude)
            : (int64_t)magnitude;
        return INT32_C(1);
    }
    return INT32_C(0);
}

static int32_t __tinypy_buffer_constructor_integer(tinypy_vm_t *vm, tinypy_value_t *value, int64_t *out_value, tinypy_error_t **out_error)
{
    if (__tinypy_buffer_integer_as_i64(value, out_value) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "buffer offset and size must be integers", out_error);
        return INT32_C(0);
    }
    return INT32_C(1);
}

tinypy_value_t *tinypy_internal_buffer_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = type->vm;
    size_t argument_count = tinypy_tuple_size(args);
    tinypy_value_t *owner;
    size_t owner_size;
    int64_t offset = INT64_C(0);
    int64_t requested_size = INT64_C(-1);

    if ((kwargs != NULL && tinypy_dict_size(kwargs) != 0U) || argument_count < 1U || argument_count > 3U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "buffer constructor expects object, optional offset and size", out_error);
        return NULL;
    }
    owner = tinypy_tuple_get(args, 0U);
    if (__tinypy_buffer_supported(owner) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "object does not support the buffer interface", out_error);
        return NULL;
    }
    if (argument_count >= 2U && __tinypy_buffer_constructor_integer(vm, tinypy_tuple_get(args, 1U), &offset, out_error) == 0) return NULL;
    if (argument_count >= 3U && __tinypy_buffer_constructor_integer(vm, tinypy_tuple_get(args, 2U), &requested_size, out_error) == 0) return NULL;
    (void)__tinypy_buffer_owner_view(owner, &owner_size);
    if (offset < 0 || (uint64_t)offset > (uint64_t)owner_size) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "buffer offset is outside the source", out_error);
        return NULL;
    }
    if (requested_size < -1 || (requested_size >= 0 && (uint64_t)requested_size > (uint64_t)(owner_size - (size_t)offset))) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "buffer size is outside the source", out_error);
        return NULL;
    }
    return tinypy_buffer_from_object(owner, (size_t)offset, requested_size < 0 ? TINYPY_BUFFER_TO_END : (size_t)requested_size);
}

ptrdiff_t tinypy_internal_buffer_length(tinypy_value_t *value, tinypy_error_t **out_error)
{
    size_t size = TINYPY_BUFFER_OBJECT(value)->size;

    tinypy_internal_clear_error(out_error);
    assert(size <= (size_t)PTRDIFF_MAX);
    return (ptrdiff_t)size;
}

static int32_t __tinypy_buffer_normalize_index(tinypy_vm_t *vm, tinypy_value_t *key, size_t size, size_t *out_index, tinypy_error_t **out_error)
{
    int64_t index;

    if (__tinypy_buffer_integer_as_i64(key, &index) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "buffer index must be an integer", out_error);
        return INT32_C(0);
    }
    if (index < 0) {
        uint64_t distance = (uint64_t)(-(index + 1)) + UINT64_C(1);

        if (distance > size) goto out_of_range;
        *out_index = size - (size_t)distance;
        return INT32_C(1);
    }
    if ((uint64_t)index >= (uint64_t)size) goto out_of_range;
    *out_index = (size_t)index;
    return INT32_C(1);
out_of_range:
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_INDEX, "buffer index is out of range", out_error);
    return INT32_C(0);
}

static int32_t __tinypy_buffer_slice_bound(tinypy_value_t *value, int64_t fallback, int64_t *out_value)
{
    if (tinypy_internal_value_kind(value) == TINYPY_VALUE_NONE) {
        *out_value = fallback;
        return INT32_C(1);
    }
    return __tinypy_buffer_integer_as_i64(value, out_value);
}

static tinypy_value_t *__tinypy_buffer_slice(tinypy_value_t *value, tinypy_value_t *slice, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = tinypy_internal_value_vm(value);
    const unsigned char *bytes = (const unsigned char *)tinypy_buffer_view(value, &(size_t){0U});
    size_t size = TINYPY_BUFFER_OBJECT(value)->size;
    int64_t step;
    int64_t start;
    int64_t stop;
    size_t length = 0U;
    unsigned char *selected;
    tinypy_value_t *result;
    int64_t source;
    size_t index;

    if (__tinypy_buffer_slice_bound(tinypy_slice_step(slice), INT64_C(1), &step) == 0 || step == 0) {
        tinypy_internal_make_vm_error(vm, step == 0 ? TINYPY_ERROR_VALUE : TINYPY_ERROR_TYPE, step == 0 ? "slice step cannot be zero" : "slice indices must be integers", out_error);
        return NULL;
    }
    if (__tinypy_buffer_slice_bound(tinypy_slice_start(slice), step < 0 ? (int64_t)size - 1 : 0, &start) == 0 || __tinypy_buffer_slice_bound(tinypy_slice_stop(slice), step < 0 ? -1 : (int64_t)size, &stop) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "slice indices must be integers", out_error);
        return NULL;
    }
    if (start < 0) start += (int64_t)size;
    if (stop < 0 && !(step < 0 && tinypy_internal_value_kind(tinypy_slice_stop(slice)) == TINYPY_VALUE_NONE)) stop += (int64_t)size;
    if (step > 0) {
        if (start < 0) start = 0;
        if (stop < 0) stop = 0;
        if (start > (int64_t)size) start = (int64_t)size;
        if (stop > (int64_t)size) stop = (int64_t)size;
        if (start < stop) length = (size_t)(1 + (stop - start - 1) / step);
    } else {
        if (start < -1) start = -1;
        if (stop < -1) stop = -1;
        if (start >= (int64_t)size) start = (int64_t)size - 1;
        if (stop >= (int64_t)size) stop = (int64_t)size - 1;
        if (stop < start) length = (size_t)(1 + (start - stop - 1) / -step);
    }
    if (length == 0U) return tinypy_string_from_bytes(vm, NULL, 0U);
    if (step == 1) return tinypy_string_from_bytes(vm, bytes + (size_t)start, length);
    selected = (unsigned char *)tinypy_internal_vm_allocate(vm, length, (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    source = start;
    for (index = 0U; index < length; index += 1U) {
        selected[index] = bytes[(size_t)source];
        source += step;
    }
    result = tinypy_string_from_bytes(vm, selected, length);
    tinypy_internal_vm_deallocate(vm, selected, length, (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    return result;
}

tinypy_value_t *tinypy_internal_buffer_get_item(tinypy_value_t *value, tinypy_value_t *key, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = tinypy_internal_value_vm(value);
    const unsigned char *bytes;
    size_t size;
    size_t index;

    tinypy_internal_clear_error(out_error);
    if (tinypy_internal_value_kind(key) == TINYPY_VALUE_SLICE) return __tinypy_buffer_slice(value, key, out_error);
    bytes = (const unsigned char *)tinypy_buffer_view(value, &size);
    if (__tinypy_buffer_normalize_index(vm, key, size, &index, out_error) == 0) return NULL;
    return tinypy_string_from_bytes(vm, bytes + index, 1U);
}

tinypy_value_t *tinypy_internal_buffer_string(tinypy_value_t *value, tinypy_error_t **out_error)
{
    const void *bytes;
    size_t size;

    tinypy_internal_clear_error(out_error);
    bytes = tinypy_buffer_view(value, &size);
    return tinypy_string_from_bytes(tinypy_internal_value_vm(value), bytes, size);
}

tinypy_value_t *tinypy_internal_buffer_repr(tinypy_value_t *value, tinypy_error_t **out_error)
{
    (void)value;
    tinypy_internal_clear_error(out_error);
    return tinypy_string_from_bytes(tinypy_internal_value_vm(value), "<read-only buffer>", 18U);
}
