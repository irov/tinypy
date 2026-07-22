#include "tinypy/item.h"

#include "internal.h"

#include <assert.h>
#include <string.h>

typedef struct tinypy_item_slice_indices_t {
    int64_t start;
    int64_t stop;
    int64_t step;
    size_t length;
} tinypy_item_slice_indices_t;

//////////////////////////////////////////////////////////////////////////
static int __tinypy_item_index_value(const tinypy_value_t *key, int64_t *out_index) {
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(key);

    if (kind == TINYPY_VALUE_BOOL || kind == TINYPY_VALUE_INTEGER) {
        *out_index = TINYPY_INTEGER_VALUE(key);
        return 1;
    }
    if (kind == TINYPY_VALUE_LONG) {
        const tinypy_long_object_t *long_value = TINYPY_LONG_OBJECT((tinypy_value_t *)key);
        size_t count = TINYPY_LONG_DIGIT_COUNT(key);
        uint64_t magnitude = 0U;
        size_t index;

        if (count > 5U) {
            return 0;
        }
        for (index = count; index != 0U; index -= 1U) {
            if (magnitude > (UINT64_MAX >> 15U)) {
                return 0;
            }
            magnitude = (magnitude << 15U) | long_value->digits[index - 1U];
        }
        if (TINYPY_LONG_SIGN(key) >= 0) {
            if (magnitude > (uint64_t)INT64_MAX) {
                return 0;
            }
            *out_index = (int64_t)magnitude;
        }
        else {
            if (magnitude > (uint64_t)INT64_MAX + UINT64_C(1)) {
                return 0;
            }
            *out_index = magnitude == (uint64_t)INT64_MAX + UINT64_C(1) ? INT64_MIN : -(int64_t)magnitude;
        }
        return 1;
    }
    return 0;
}
//////////////////////////////////////////////////////////////////////////
static int __tinypy_item_slice_index_value(tinypy_vm_t *vm, const tinypy_value_t *value, int64_t *out_index, tinypy_error_t **out_error) {
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);

    if (kind == TINYPY_VALUE_BOOL || kind == TINYPY_VALUE_INTEGER) {
        *out_index = TINYPY_INTEGER_VALUE(value);
        return 1;
    }
    if (kind == TINYPY_VALUE_LONG) {
        const tinypy_long_object_t *long_value = TINYPY_LONG_OBJECT((tinypy_value_t *)value);
        size_t count = TINYPY_LONG_DIGIT_COUNT(value);
        uint64_t magnitude = 0U;
        size_t index;

        if (count > 5U) {
            *out_index = TINYPY_LONG_SIGN(value) < 0 ? -INT64_MAX : INT64_MAX;
            return 1;
        }
        for (index = count; index != 0U; index -= 1U) {
            if (magnitude > (UINT64_MAX >> 15U)) {
                *out_index = TINYPY_LONG_SIGN(value) < 0 ? -INT64_MAX : INT64_MAX;
                return 1;
            }
            magnitude = (magnitude << 15U) | long_value->digits[index - 1U];
        }
        if (TINYPY_LONG_SIGN(value) < 0) {
            *out_index = magnitude > (uint64_t)INT64_MAX ? -INT64_MAX : -(int64_t)magnitude;
        }
        else {
            *out_index = magnitude > (uint64_t)INT64_MAX ? INT64_MAX : (int64_t)magnitude;
        }
        return 1;
    }
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "slice index is not an integer or None", out_error);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
static int __tinypy_item_slice_indices(tinypy_vm_t *vm, tinypy_value_t *slice_value, size_t size, tinypy_item_slice_indices_t *out_indices, tinypy_error_t **out_error) {
    tinypy_slice_object_t *slice = TINYPY_SLICE_OBJECT(slice_value);
    int64_t sequence_size;
    int64_t lower;
    int64_t upper;

    assert(size <= (size_t)INT64_MAX);
    sequence_size = (int64_t)size;
    if (TINYPY_VALUE_KIND(slice->step) == TINYPY_VALUE_NONE) {
        out_indices->step = 1;
    }
    else if (__tinypy_item_slice_index_value(vm, slice->step, &out_indices->step, out_error) == 0) {
        return 0;
    }
    if (out_indices->step == INT64_MIN) {
        out_indices->step = -INT64_MAX;
    }
    if (out_indices->step == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "slice step cannot be zero", out_error);
        return 0;
    }
    lower = out_indices->step < 0 ? -1 : 0;
    upper = out_indices->step < 0 ? sequence_size - 1 : sequence_size;

    if (TINYPY_VALUE_KIND(slice->start) == TINYPY_VALUE_NONE) {
        out_indices->start = out_indices->step < 0 ? upper : lower;
    }
    else {
        if (__tinypy_item_slice_index_value(vm, slice->start, &out_indices->start, out_error) == 0) {
            return 0;
        }
        if (out_indices->start < 0) {
            out_indices->start += sequence_size;
        }
        if (out_indices->start < lower) {
            out_indices->start = lower;
        }
        if (out_indices->start > upper) {
            out_indices->start = upper;
        }
    }
    if (TINYPY_VALUE_KIND(slice->stop) == TINYPY_VALUE_NONE) {
        out_indices->stop = out_indices->step < 0 ? lower : upper;
    }
    else {
        if (__tinypy_item_slice_index_value(vm, slice->stop, &out_indices->stop, out_error) == 0) {
            return 0;
        }
        if (out_indices->stop < 0) {
            out_indices->stop += sequence_size;
        }
        if (out_indices->stop < lower) {
            out_indices->stop = lower;
        }
        if (out_indices->stop > upper) {
            out_indices->stop = upper;
        }
    }

    out_indices->length = 0U;
    if (out_indices->step < 0 && out_indices->stop < out_indices->start) {
        out_indices->length = (size_t)(1 + (out_indices->start - out_indices->stop - 1) / -out_indices->step);
    }
    else if (out_indices->step > 0 && out_indices->start < out_indices->stop) {
        out_indices->length = (size_t)(1 + (out_indices->stop - out_indices->start - 1) / out_indices->step);
    }
    return 1;
}
//////////////////////////////////////////////////////////////////////////
static int __tinypy_item_normalize_index(tinypy_vm_t *vm, tinypy_value_t *key, size_t size, size_t *out_index, tinypy_error_t **out_error) {
    int64_t index;

    if (__tinypy_item_index_value(key, &index) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "container index is not an integer", out_error);
        return 0;
    }
    if (index < 0) {
        uint64_t distance = (uint64_t)(-(index + 1)) + UINT64_C(1);
        if (distance > size) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_INDEX, "container index is out of range", out_error);
            return 0;
        }
        *out_index = size - (size_t)distance;
        return 1;
    }
    if ((uint64_t)index >= (uint64_t)size) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_INDEX, "container index is out of range", out_error);
        return 0;
    }
    *out_index = (size_t)index;
    return 1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_item_call_method(tinypy_value_t *container, const char *name, size_t name_size, tinypy_value_t *const *items, size_t item_count, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(container);
    tinypy_value_t *method = tinypy_object_get_attr(container, name, name_size, out_error);

    if (method == NULL) {
        return NULL;
    }
    tinypy_value_t *args = tinypy_tuple_from_items(vm, items, item_count);
    tinypy_value_t *result = tinypy_call(method, args, NULL, out_error);
    TINYPY_DECREF(args);
    TINYPY_DECREF(method);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_item_unicode_get(tinypy_value_t *container, size_t scalar_index) {
    const char *utf8;
    size_t byte_size;
    size_t code_point_count;
    size_t byte_index = 0U;
    size_t current = 0U;
    size_t scalar_size;

    utf8 = tinypy_unicode_utf8_view(container, &byte_size, &code_point_count);
    assert(scalar_index < code_point_count);
    while (current < scalar_index) {
        unsigned char lead = (unsigned char)utf8[byte_index];
        byte_index += lead < 0x80U ? 1U : (lead < 0xe0U ? 2U : (lead < 0xf0U ? 3U : 4U));
        current += 1U;
    } {
        unsigned char lead = (unsigned char)utf8[byte_index];
        scalar_size = lead < 0x80U ? 1U : (lead < 0xe0U ? 2U : (lead < 0xf0U ? 3U : 4U));
    }
    tinypy_vm_t *vm = TINYPY_VALUE_VM(container);
    return tinypy_unicode_from_utf8(vm, utf8 + byte_index, scalar_size);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_item_sequence_slice(tinypy_value_t *container, const tinypy_item_slice_indices_t *indices) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(container);
    tinypy_value_t **items = NULL;
    size_t index;
    int64_t source_index = indices->start;

    if (indices->length != 0U) {
        assert(indices->length <= SIZE_MAX / sizeof(*items));
        items = (tinypy_value_t **)tinypy_internal_vm_allocate(vm, indices->length * sizeof(*items), (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
        for (index = 0U; index < indices->length; ++index) {
            items[index] = TINYPY_VALUE_KIND(container) == TINYPY_VALUE_TUPLE ? TINYPY_TUPLE_GET(container, (size_t)source_index) : TINYPY_LIST_GET(container, (size_t)source_index);
            source_index += indices->step;
        }
    }
    tinypy_value_t *result = TINYPY_VALUE_KIND(container) == TINYPY_VALUE_TUPLE ? tinypy_tuple_from_items(vm, items, indices->length) : tinypy_list_from_items(vm, items, indices->length);
    if (items != NULL) {
        tinypy_internal_vm_deallocate(vm, items, indices->length * sizeof(*items), (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_item_string_slice(tinypy_value_t *container, const tinypy_item_slice_indices_t *indices) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(container);
    const unsigned char *bytes;
    unsigned char *selected;
    size_t byte_size;
    size_t index;
    int64_t source_index = indices->start;

    bytes = (const unsigned char *)tinypy_string_view(container, &byte_size);
    if (indices->length == 0U) {
        return tinypy_string_from_bytes(vm, NULL, 0U);
    }
    if (indices->step == 1) {
        return tinypy_string_from_bytes(vm, bytes + (size_t)indices->start, indices->length);
    }
    selected = (unsigned char *)tinypy_internal_vm_allocate(vm, indices->length, (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    for (index = 0U; index < indices->length; ++index) {
        selected[index] = bytes[(size_t)source_index];
        source_index += indices->step;
    }
    tinypy_value_t *result = tinypy_string_from_bytes(vm, selected, indices->length);
    tinypy_internal_vm_deallocate(vm, selected, indices->length, (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_item_unicode_slice(tinypy_value_t *container, const tinypy_item_slice_indices_t *indices) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(container);
    const char *utf8;
    size_t byte_size;
    size_t code_point_count;
    size_t *offsets;
    char *selected;
    size_t selected_size = 0U;
    size_t byte_index = 0U;
    size_t scalar_index;
    size_t index;
    int64_t source_index = indices->start;

    utf8 = tinypy_unicode_utf8_view(container, &byte_size, &code_point_count);
    if (indices->length == 0U) {
        return tinypy_unicode_from_utf8(vm, NULL, 0U);
    }
    assert(code_point_count < SIZE_MAX / sizeof(*offsets));
    offsets = (size_t *)tinypy_internal_vm_allocate(vm, (code_point_count + 1U) * sizeof(*offsets), (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    for (scalar_index = 0U; scalar_index < code_point_count; ++scalar_index) {
        unsigned char lead = (unsigned char)utf8[byte_index];

        offsets[scalar_index] = byte_index;
        byte_index += lead < 0x80U ? 1U : (lead < 0xe0U ? 2U : (lead < 0xf0U ? 3U : 4U));
    }
    offsets[code_point_count] = byte_size;
    selected = (char *)tinypy_internal_vm_allocate(vm, byte_size, (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    for (index = 0U; index < indices->length; ++index) {
        size_t scalar = (size_t)source_index;
        size_t scalar_size = offsets[scalar + 1U] - offsets[scalar];

        (void)memcpy(selected + selected_size, utf8 + offsets[scalar], scalar_size);
        selected_size += scalar_size;
        source_index += indices->step;
    }
    tinypy_value_t *result = tinypy_unicode_from_utf8(vm, selected, selected_size);
    tinypy_internal_vm_deallocate(vm, selected, byte_size, (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    tinypy_internal_vm_deallocate(vm, offsets, (code_point_count + 1U) * sizeof(*offsets), (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_item_collect_iterable(tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_error_t *iteration_error = NULL;
    tinypy_value_t *iterator = tinypy_iter(value, &iteration_error);

    if (iterator == NULL) {
        if (out_error != NULL) {
            *out_error = iteration_error;
        }
        else if (iteration_error != NULL) {
            tinypy_error_release(iteration_error);
        }
        return NULL;
    }
    tinypy_value_t *items = tinypy_list_from_items(vm, NULL, 0U);
    for (;;) {
        tinypy_value_t *item = tinypy_next(iterator, &iteration_error);

        if (item == NULL) {
            break;
        }
        tinypy_list_append(items, item);
        TINYPY_DECREF(item);
    }
    TINYPY_DECREF(iterator);
    if (iteration_error != NULL) {
        TINYPY_DECREF(items);
        if (out_error != NULL) {
            *out_error = iteration_error;
        }
        else {
            tinypy_error_release(iteration_error);
        }
        return NULL;
    }
    return items;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_item_list_set_slice(tinypy_value_t *list, tinypy_value_t *slice, tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(list);
    tinypy_item_slice_indices_t indices;
    size_t replacement_size;
    size_t index;
    int64_t target_index;

    size_t list_size = TINYPY_LIST_SIZE(list);
    if (__tinypy_item_slice_indices(vm, slice, list_size, &indices, out_error) == 0) {
        return 0;
    }
    tinypy_value_t *replacement = __tinypy_item_collect_iterable(value, out_error);
    if (replacement == NULL) {
        return 0;
    }
    replacement_size = TINYPY_LIST_SIZE(replacement);
    if (indices.step != 1 && replacement_size != indices.length) {
        TINYPY_DECREF(replacement);
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "extended slice assignment has the wrong size", out_error);
        return 0;
    }
    if (indices.step == 1) {
        for (index = 0U; index < indices.length; ++index) {
            tinypy_list_delete(list, (size_t)indices.start);
        }
        for (index = 0U; index < replacement_size; ++index) {
            tinypy_value_t *item_2 = TINYPY_LIST_GET(replacement, index);
            tinypy_list_insert(list, (size_t)indices.start + index, item_2);
        }
    }
    else {
        target_index = indices.start;
        for (index = 0U; index < indices.length; ++index) {
            tinypy_value_t *item = TINYPY_LIST_GET(replacement, index);
            tinypy_list_set(list, (size_t)target_index, item);
            target_index += indices.step;
        }
    }
    TINYPY_DECREF(replacement);
    return 1;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_item_list_delete_slice(tinypy_value_t *list, tinypy_value_t *slice, tinypy_error_t **out_error) {
    tinypy_item_slice_indices_t indices;
    size_t index;

    tinypy_vm_t *vm = TINYPY_VALUE_VM(list);
    size_t list_size = TINYPY_LIST_SIZE(list);
    if (__tinypy_item_slice_indices(vm, slice, list_size, &indices, out_error) == 0) {
        return 0;
    }
    if (indices.step > 0) {
        for (index = indices.length; index != 0U; index -= 1U) {
            tinypy_list_delete(list, (size_t)(indices.start + (int64_t)(index - 1U) * indices.step));
        }
    }
    else {
        for (index = 0U; index < indices.length; ++index) {
            tinypy_list_delete(list, (size_t)(indices.start + (int64_t)index * indices.step));
        }
    }
    return 1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_get_item(tinypy_value_t *container, tinypy_value_t *key, tinypy_error_t **out_error) {
    tinypy_value_type_e kind;
    size_t index;
    tinypy_value_t *item;

    assert(container != NULL && key != NULL);
    tinypy_vm_t *vm = TINYPY_VALUE_VM(container);
    assert(tinypy_internal_vm_valid(vm));
    assert(tinypy_internal_value_belongs_to(vm, key));
    TINYPY_CLEAR_ERROR(out_error);
    if (container->type->mapping_slots != NULL && container->type->mapping_slots->get_item != NULL) {
        return container->type->mapping_slots->get_item(container, key, out_error);
    }
    if (container->type->sequence_slots != NULL && container->type->sequence_slots->get_item != NULL) {
        return container->type->sequence_slots->get_item(container, key, out_error);
    }
    kind = TINYPY_VALUE_KIND(container);
    if (kind == TINYPY_VALUE_DICT) {
        item = tinypy_internal_dict_get_optional(vm, container, key);
        if (item == NULL) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_KEY, "dictionary key is absent", out_error);
            return NULL;
        }
        TINYPY_INCREF(item);
        return item;
    }
    if (TINYPY_VALUE_KIND(key) == TINYPY_VALUE_SLICE) {
        tinypy_item_slice_indices_t indices;
        size_t size;

        if (kind == TINYPY_VALUE_TUPLE) {
            size = TINYPY_TUPLE_SIZE(container);
        }
        else if (kind == TINYPY_VALUE_LIST) {
            size = TINYPY_LIST_SIZE(container);
        }
        else if (kind == TINYPY_VALUE_STRING) {
            (void)tinypy_string_view(container, &size);
        }
        else if (kind == TINYPY_VALUE_UNICODE) {
            size_t byte_size;
            (void)tinypy_unicode_utf8_view(container, &byte_size, &size);
        }
        else {
            if (tinypy_internal_object_has_special(container, "__getitem__", 11U) != 0) {
                return __tinypy_item_call_method(container, "__getitem__", 11U, &key, 1U, out_error);
            }
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "object does not support slicing", out_error);
            return NULL;
        }
        if (__tinypy_item_slice_indices(vm, key, size, &indices, out_error) == 0) {
            return NULL;
        }
        if (kind == TINYPY_VALUE_TUPLE || kind == TINYPY_VALUE_LIST) {
            return __tinypy_item_sequence_slice(container, &indices);
        }
        if (kind == TINYPY_VALUE_STRING) {
            return __tinypy_item_string_slice(container, &indices);
        }
        return __tinypy_item_unicode_slice(container, &indices);
    }
    if (kind == TINYPY_VALUE_TUPLE) {
        size_t tuple_size = TINYPY_TUPLE_SIZE(container);
        if (__tinypy_item_normalize_index(vm, key, tuple_size, &index, out_error) == 0) {
            return NULL;
        }
        item = TINYPY_TUPLE_GET(container, index);
        TINYPY_INCREF(item);
        return item;
    }
    if (kind == TINYPY_VALUE_LIST) {
        size_t list_size = TINYPY_LIST_SIZE(container);
        if (__tinypy_item_normalize_index(vm, key, list_size, &index, out_error) == 0) {
            return NULL;
        }
        item = TINYPY_LIST_GET(container, index);
        TINYPY_INCREF(item);
        return item;
    }
    if (kind == TINYPY_VALUE_STRING) {
        const unsigned char *bytes;
        size_t size;

        bytes = (const unsigned char *)tinypy_string_view(container, &size);
        if (__tinypy_item_normalize_index(vm, key, size, &index, out_error) == 0) {
            return NULL;
        }
        return tinypy_string_from_bytes(vm, bytes + index, 1U);
    }
    if (kind == TINYPY_VALUE_UNICODE) {
        size_t byte_size;
        size_t code_point_count;

        (void)tinypy_unicode_utf8_view(container, &byte_size, &code_point_count);
        if (__tinypy_item_normalize_index(vm, key, code_point_count, &index, out_error) == 0) {
            return NULL;
        }
        return __tinypy_item_unicode_get(container, index);
    }
    if (kind == TINYPY_VALUE_XRANGE) {
        tinypy_xrange_object_t *range = TINYPY_XRANGE_OBJECT(container);

        if (__tinypy_item_normalize_index(vm, key, range->length, &index, out_error) == 0) {
            return NULL;
        }
        assert(index <= (size_t)INT64_MAX);
        return tinypy_integer_from_i64(vm, range->start + (int64_t)index * range->step);
    }
    if (tinypy_internal_object_has_special(container, "__getitem__", 11U) != 0) {
        return __tinypy_item_call_method(container, "__getitem__", 11U, &key, 1U, out_error);
    }
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "object does not support item access", out_error);
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
int32_t tinypy_set_item(tinypy_value_t *container, tinypy_value_t *key, tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_value_type_e kind;
    size_t index;

    assert(container != NULL && key != NULL && value != NULL);
    tinypy_vm_t *vm = TINYPY_VALUE_VM(container);
    assert(tinypy_internal_vm_valid(vm));
    assert(tinypy_internal_value_belongs_to(vm, key));
    assert(tinypy_internal_value_belongs_to(vm, value));
    TINYPY_CLEAR_ERROR(out_error);
    if (container->type->mapping_slots != NULL && container->type->mapping_slots->set_item != NULL) {
        return container->type->mapping_slots->set_item(container, key, value, out_error);
    }
    if (container->type->sequence_slots != NULL && container->type->sequence_slots->set_item != NULL) {
        return container->type->sequence_slots->set_item(container, key, value, out_error);
    }
    kind = TINYPY_VALUE_KIND(container);
    if (kind == TINYPY_VALUE_DICT) {
        tinypy_dict_set(container, key, value);
        return 1;
    }
    if (kind == TINYPY_VALUE_LIST) {
        if (TINYPY_VALUE_KIND(key) == TINYPY_VALUE_SLICE) {
            return __tinypy_item_list_set_slice(container, key, value, out_error);
        }
        size_t list_size = TINYPY_LIST_SIZE(container);
        if (__tinypy_item_normalize_index(vm, key, list_size, &index, out_error) == 0) {
            return 0;
        }
        tinypy_list_set(container, index, value);
        return 1;
    }
    if (tinypy_internal_object_has_special(container, "__setitem__", 11U) != 0) {
        tinypy_value_t *items[2] = {key, value};
        tinypy_value_t *result = __tinypy_item_call_method(container, "__setitem__", 11U, items, 2U, out_error);

        if (result == NULL) {
            return 0;
        }
        TINYPY_DECREF(result);
        return 1;
    }
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "object does not support item assignment", out_error);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
int32_t tinypy_delete_item(tinypy_value_t *container, tinypy_value_t *key, tinypy_error_t **out_error) {
    tinypy_value_type_e kind;
    size_t index;

    assert(container != NULL && key != NULL);
    tinypy_vm_t *vm = TINYPY_VALUE_VM(container);
    assert(tinypy_internal_vm_valid(vm));
    assert(tinypy_internal_value_belongs_to(vm, key));
    TINYPY_CLEAR_ERROR(out_error);
    if (container->type->mapping_slots != NULL && container->type->mapping_slots->set_item != NULL) {
        return container->type->mapping_slots->set_item(container, key, NULL, out_error);
    }
    kind = TINYPY_VALUE_KIND(container);
    if (kind == TINYPY_VALUE_DICT) {
        if (tinypy_dict_contains(container, key) == 0) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_KEY, "dictionary key is absent", out_error);
            return 0;
        }
        tinypy_dict_delete(container, key);
        return 1;
    }
    if (kind == TINYPY_VALUE_LIST) {
        if (TINYPY_VALUE_KIND(key) == TINYPY_VALUE_SLICE) {
            return __tinypy_item_list_delete_slice(container, key, out_error);
        }
        size_t list_size = TINYPY_LIST_SIZE(container);
        if (__tinypy_item_normalize_index(vm, key, list_size, &index, out_error) == 0) {
            return 0;
        }
        tinypy_list_delete(container, index);
        return 1;
    }
    if (tinypy_internal_object_has_special(container, "__delitem__", 11U) != 0) {
        tinypy_value_t *result = __tinypy_item_call_method(container, "__delitem__", 11U, &key, 1U, out_error);

        if (result == NULL) {
            return 0;
        }
        TINYPY_DECREF(result);
        return 1;
    }
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "object does not support item deletion", out_error);
    return 0;
}
