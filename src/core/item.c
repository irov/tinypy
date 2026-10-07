#include "tinypy/item.h"

#include "internal.h"

#include <string.h>

//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_index_value(tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);

    if (kind == TINYPY_VALUE_BOOL) {
        tinypy_value_t *return_value_1 = tinypy_integer_from_i64(vm, TINYPY_INTEGER_VALUE(value));
        return return_value_1;
    }
    if (kind == TINYPY_VALUE_INTEGER || kind == TINYPY_VALUE_LONG) {
        return TINYPY_RET(value);
    }
    if (tinypy_internal_object_has_special_key(value, vm->internal_special_index_key) != 0) {
        tinypy_value_t *method = tinypy_internal_object_get_special_key(value, vm->internal_special_index_key, out_error);
        tinypy_value_t *args;
        tinypy_value_t *converted;

        if (method == NULL) {
            return NULL;
        }
        args = TINYPY_RET_EMPTY_TUPLE(vm);
        converted = tinypy_call(method, args, NULL, out_error);
        TINYPY_DECREF(args);
        TINYPY_DECREF(method);
        if (converted == NULL) {
            return NULL;
        }
        kind = TINYPY_VALUE_KIND(converted);
        if (kind != TINYPY_VALUE_BOOL && kind != TINYPY_VALUE_INTEGER && kind != TINYPY_VALUE_LONG) {
            TINYPY_DECREF(converted);
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "__index__ returned a non-integer", out_error);
            return NULL;
        }
        if (kind == TINYPY_VALUE_BOOL) {
            tinypy_value_t *result = tinypy_integer_from_i64(vm, TINYPY_INTEGER_VALUE(converted));

            TINYPY_DECREF(converted);
            return result;
        }
        return converted;
    }
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "integer argument required", out_error);
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_index_as_i64(tinypy_value_t *value, int64_t *out_index, tinypy_bool_t clamp_overflow, tinypy_error_t **out_error) {
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_value_t *converted = NULL;

    if (kind == TINYPY_VALUE_BOOL || kind == TINYPY_VALUE_INTEGER) {
        *out_index = TINYPY_INTEGER_VALUE(value);
        return TINYPY_TRUE;
    }
    if (kind == TINYPY_VALUE_LONG) {
        const tinypy_long_object_t *long_value = TINYPY_LONG_OBJECT(value);
        size_t count = TINYPY_LONG_DIGIT_COUNT(value);
        uint64_t magnitude = 0U;
        size_t index;

        if (count > 5U) {
            goto overflow;
        }
        for (index = count; index != 0U; index -= 1U) {
            if (magnitude > (UINT64_MAX >> 15U)) {
                goto overflow;
            }
            magnitude = (magnitude << 15U) | long_value->digits[index - 1U];
        }
        if (TINYPY_LONG_SIGN(value) >= 0) {
            if (magnitude > (uint64_t)INT64_MAX) {
                goto overflow;
            }
            *out_index = (int64_t)magnitude;
        }
        else {
            if (magnitude > (uint64_t)INT64_MAX + UINT64_C(1)) {
                goto overflow;
            }
            *out_index = magnitude == (uint64_t)INT64_MAX + UINT64_C(1) ? INT64_MIN : -(int64_t)magnitude;
        }
        return TINYPY_TRUE;
    }
    if (tinypy_internal_object_has_special_key(value, vm->internal_special_index_key) != 0) {
        tinypy_value_t *method = tinypy_internal_object_get_special_key(value, vm->internal_special_index_key, out_error);
        tinypy_value_t *args;

        if (method == NULL) {
            return TINYPY_FALSE;
        }
        args = TINYPY_RET_EMPTY_TUPLE(vm);
        converted = tinypy_call(method, args, NULL, out_error);
        TINYPY_DECREF(args);
        TINYPY_DECREF(method);
        if (converted == NULL) {
            return TINYPY_FALSE;
        }
        kind = TINYPY_VALUE_KIND(converted);
        if (kind != TINYPY_VALUE_BOOL && kind != TINYPY_VALUE_INTEGER && kind != TINYPY_VALUE_LONG) {
            TINYPY_DECREF(converted);
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "__index__ returned a non-integer", out_error);
            return TINYPY_FALSE;
        }
        tinypy_bool_t result = tinypy_internal_index_as_i64(converted, out_index, clamp_overflow, out_error);
        TINYPY_DECREF(converted);
        return result;
    }
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "integer argument required", out_error);
    return TINYPY_FALSE;
overflow:
    if (clamp_overflow != 0) {
        *out_index = TINYPY_LONG_SIGN(value) < 0 ? -INT64_MAX : INT64_MAX;
        return TINYPY_TRUE;
    }
    tinypy_message_part_t parts[] = {
        TINYPY_MESSAGE_PART_LITERAL("cannot fit '"),
        TINYPY_MESSAGE_PART_TYPE_NAME(value),
        TINYPY_MESSAGE_PART_LITERAL("' into an index-sized integer"),
    };
    tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_OVERFLOW, parts, sizeof(parts) / sizeof(parts[0]), out_error);
    return TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_item_slice_index_value(tinypy_vm_t *vm, const tinypy_value_t *value, int64_t *out_index, tinypy_error_t **out_error) {
    (void)vm;
    tinypy_bool_t return_value_1 = tinypy_internal_index_as_i64((tinypy_value_t *)value, out_index, TINYPY_TRUE, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
/* Resolve callbacks before reading the length of a mutable sequence. */
tinypy_bool_t tinypy_internal_slice_unpack(tinypy_value_t *slice_value, tinypy_internal_slice_indices_t *indices, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(slice_value);
    tinypy_slice_object_t *slice = TINYPY_SLICE_OBJECT(slice_value);

    indices->step = 1;
    if (TINYPY_VALUE_KIND(slice->step) != TINYPY_VALUE_NONE
        && __tinypy_item_slice_index_value(vm, slice->step, &indices->step, out_error) == 0) {
        return TINYPY_FALSE;
    }
    if (indices->step == INT64_MIN) {
        indices->step = -INT64_MAX;
    }
    if (indices->step == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "slice step cannot be zero", out_error);
        return TINYPY_FALSE;
    }
    indices->start = indices->step < 0 ? INT64_MAX : 0;
    if (TINYPY_VALUE_KIND(slice->start) != TINYPY_VALUE_NONE
        && __tinypy_item_slice_index_value(vm, slice->start, &indices->start, out_error) == 0) {
        return TINYPY_FALSE;
    }
    indices->stop = indices->step < 0 ? INT64_MIN : INT64_MAX;
    if (TINYPY_VALUE_KIND(slice->stop) != TINYPY_VALUE_NONE
        && __tinypy_item_slice_index_value(vm, slice->stop, &indices->stop, out_error) == 0) {
        return TINYPY_FALSE;
    }
    indices->length = 0U;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_slice_adjust_indices(tinypy_vm_t *vm, size_t size, tinypy_internal_slice_indices_t *indices, tinypy_error_t **out_error) {
#if SIZE_MAX > INT64_MAX
    if (size > (size_t)INT64_MAX) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "sequence is too large to normalize a slice", out_error);
        return TINYPY_FALSE;
    }
#endif
    (void)vm;
    (void)out_error;
    int64_t sequence_size = (int64_t)size;
    int64_t lower = indices->step < 0 ? -1 : 0;
    int64_t upper = indices->step < 0 ? sequence_size - 1 : sequence_size;

    if (indices->start < 0) {
        indices->start += sequence_size;
    }
    if (indices->start < lower) {
        indices->start = lower;
    }
    if (indices->start > upper) {
        indices->start = upper;
    }
    if (indices->stop < 0) {
        indices->stop += sequence_size;
    }
    if (indices->stop < lower) {
        indices->stop = lower;
    }
    if (indices->stop > upper) {
        indices->stop = upper;
    }
    indices->length = 0U;
    if (indices->step < 0 && indices->stop < indices->start) {
        indices->length = (size_t)(1 + (indices->start - indices->stop - 1) / -indices->step);
    }
    else if (indices->step > 0 && indices->start < indices->stop) {
        indices->length = (size_t)(1 + (indices->stop - indices->start - 1) / indices->step);
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_slice_indices(tinypy_value_t *slice_value, size_t size, tinypy_internal_slice_indices_t *out_indices, tinypy_error_t **out_error) {
    if (tinypy_internal_slice_unpack(slice_value, out_indices, out_error) == 0) {
        return TINYPY_FALSE;
    }
    tinypy_bool_t adjusted = tinypy_internal_slice_adjust_indices(TINYPY_VALUE_VM(slice_value), size, out_indices, out_error);
    return adjusted;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_item_adjust_index(tinypy_vm_t *vm, int64_t index, size_t size, size_t *out_index, tinypy_error_t **out_error) {
    if (index < 0) {
        uint64_t distance = (uint64_t)(-(index + 1)) + UINT64_C(1);
        if (distance > size) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_INDEX, "container index is out of range", out_error);
            return TINYPY_FALSE;
        }
        *out_index = size - (size_t)distance;
        return TINYPY_TRUE;
    }
    if ((uint64_t)index >= (uint64_t)size) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_INDEX, "container index is out of range", out_error);
        return TINYPY_FALSE;
    }
    *out_index = (size_t)index;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_item_normalize_index(tinypy_vm_t *vm, tinypy_value_t *key, size_t size, size_t *out_index, tinypy_error_t **out_error) {
    int64_t index;
    if (tinypy_internal_index_as_i64(key, &index, TINYPY_TRUE, out_error) == 0) {
        return TINYPY_FALSE;
    }
    tinypy_bool_t adjusted = __tinypy_item_adjust_index(vm, index, size, out_index, out_error);
    return adjusted;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_item_list_index(tinypy_value_t *list, tinypy_value_t *key, size_t *out_index, tinypy_error_t **out_error) {
    int64_t index;
    if (tinypy_internal_index_as_i64(key, &index, TINYPY_TRUE, out_error) == 0) {
        return TINYPY_FALSE;
    }
    tinypy_bool_t adjusted = __tinypy_item_adjust_index(TINYPY_VALUE_VM(list), index, TINYPY_LIST_SIZE(list), out_index, out_error);
    return adjusted;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_item_call_method(tinypy_value_t *container, tinypy_value_t *name, tinypy_value_t *const *items, size_t item_count, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(container);
    tinypy_value_t *method = tinypy_internal_object_get_special_key(container, name, out_error);

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
    size_t byte_index;
    size_t scalar_size;

    utf8 = tinypy_unicode_utf8_view(container, &byte_size, &code_point_count);
    (void)byte_size;
    (void)code_point_count;
    byte_index = tinypy_internal_unicode_byte_offset(container, scalar_index);
    uint8_t lead = (uint8_t)utf8[byte_index];
    scalar_size = lead < 0x80U ? 1U : (lead < 0xe0U ? 2U : (lead < 0xf0U ? 3U : 4U));
    tinypy_vm_t *vm = TINYPY_VALUE_VM(container);
    tinypy_value_t *return_value_1 = tinypy_unicode_from_utf8(vm, utf8 + byte_index, scalar_size);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_item_sequence_slice(tinypy_value_t *container, const tinypy_internal_slice_indices_t *indices, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(container);
    tinypy_bool_t tuple = TINYPY_VALUE_KIND(container) == TINYPY_VALUE_TUPLE;

    /* A full-width slice of an exact tuple is the tuple itself, as tupleslice
       short-circuits in Python 2.7. */
    if (tuple != 0 && container->type == &vm->types[TINYPY_VALUE_TUPLE] && indices->step == 1 && indices->start == 0 && (size_t)indices->length == TINYPY_TUPLE_SIZE(container)) {
        TINYPY_CLEAR_ERROR(out_error);
        return TINYPY_RET(container);
    }
    tinypy_value_t *result = tuple != 0
                                 ? tinypy_internal_tuple_new_checked(vm, indices->length, out_error)
                                 : tinypy_list_from_items(vm, NULL, 0U);
    size_t index;

    if (result == NULL) {
        return NULL;
    }
    if (tuple == 0 && tinypy_internal_list_reserve_checked(vm, result, indices->length, out_error) == 0) {
        TINYPY_DECREF(result);
        return NULL;
    }
    for (index = 0U; index < indices->length; ++index) {
        size_t source_index = (size_t)(indices->start + (int64_t)index * indices->step);
        tinypy_value_t *item = tuple != 0 ? TINYPY_TUPLE_GET(container, source_index) : TINYPY_LIST_GET(container, source_index);

        if (tuple != 0) {
            TINYPY_INCREF(item);
            TINYPY_DECREF(TINYPY_TUPLE_GET(result, index));
            TINYPY_TUPLE_GET(result, index) = item;
        }
        else if (tinypy_internal_list_append_checked(result, item, out_error) == 0) {
            TINYPY_DECREF(result);
            return NULL;
        }
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_item_string_slice(tinypy_value_t *container, const tinypy_internal_slice_indices_t *indices, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(container);
    const uint8_t *bytes;
    uint8_t *selected;
    size_t byte_size;
    size_t index;

    bytes = (const uint8_t *)tinypy_string_view(container, &byte_size);
    if (indices->length == 0U) {
        tinypy_value_t *return_value_1 = tinypy_internal_string_from_bytes_checked(vm, NULL, 0U, out_error);
        return return_value_1;
    }
    if (indices->step == 1) {
        if (indices->start == 0 && indices->length == byte_size && container->type == &vm->types[TINYPY_VALUE_STRING]) {
            return TINYPY_RET(container);
        }
        tinypy_value_t *return_value_2 = tinypy_internal_string_from_bytes_checked(vm, bytes + (size_t)indices->start, indices->length, out_error);
        return return_value_2;
    }
    if (indices->length == 1U) {
        tinypy_value_t *return_value_3 = tinypy_internal_string_from_bytes_checked(vm, bytes + (size_t)indices->start, 1U, out_error);
        return return_value_3;
    }
    tinypy_value_t *result = tinypy_internal_text_allocate_uninitialized_checked(vm, TINYPY_VALUE_STRING, indices->length, indices->length, &selected, out_error);
    if (result == NULL) {
        return NULL;
    }
    for (index = 0U; index < indices->length; ++index) {
        selected[index] = bytes[(size_t)(indices->start + (int64_t)index * indices->step)];
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_item_unicode_slice(tinypy_value_t *container, const tinypy_internal_slice_indices_t *indices) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(container);
    const char *utf8;
    size_t byte_size;
    size_t code_point_count;
    char *selected;
    size_t selected_capacity;
    size_t selected_size = 0U;
    size_t byte_index = 0U;
    size_t scalar_index = 0U;
    size_t index;

    utf8 = tinypy_unicode_utf8_view(container, &byte_size, &code_point_count);
    if (indices->length == 0U) {
        tinypy_value_t *return_value_1 = tinypy_unicode_from_utf8(vm, NULL, 0U);
        return return_value_1;
    }
    if (indices->step == 1) {
        if (indices->start == 0 && indices->length == code_point_count && container->type == &vm->types[TINYPY_VALUE_UNICODE]) {
            return TINYPY_RET(container);
        }
        if (byte_size == code_point_count) {
            tinypy_value_t *return_value_2 = tinypy_unicode_from_utf8(vm, utf8 + (size_t)indices->start, indices->length);
            return return_value_2;
        }
        size_t begin = tinypy_internal_unicode_byte_offset(container, (size_t)indices->start);
        size_t end_scalar = (size_t)indices->start + indices->length;
        size_t end = tinypy_internal_unicode_byte_offset(container, end_scalar);
        tinypy_value_t *return_value_3 = tinypy_unicode_from_utf8(vm, utf8 + begin, end - begin);
        return return_value_3;
    }
    selected_capacity = indices->length > SIZE_MAX / 4U ? byte_size : indices->length * 4U;
    if (selected_capacity > byte_size) {
        selected_capacity = byte_size;
    }
    selected = (char *)tinypy_internal_vm_allocate(vm, selected_capacity);
    if (indices->step > 0) {
        for (index = 0U; index < indices->length; ++index) {
            size_t target = (size_t)(indices->start + (int64_t)index * indices->step);
            size_t scalar_size;

            while (scalar_index < target) {
                uint8_t lead = (uint8_t)utf8[byte_index];

                byte_index += lead < 0x80U ? 1U : (lead < 0xe0U ? 2U : (lead < 0xf0U ? 3U : 4U));
                scalar_index += 1U;
            }
            uint8_t lead = (uint8_t)utf8[byte_index];
            scalar_size = lead < 0x80U ? 1U : (lead < 0xe0U ? 2U : (lead < 0xf0U ? 3U : 4U));
            (void)memcpy(selected + selected_size, utf8 + byte_index, scalar_size);
            selected_size += scalar_size;
            byte_index += scalar_size;
            scalar_index += 1U;
        }
    }
    else {
        byte_index = byte_size;
        scalar_index = code_point_count;
        for (index = 0U; index < indices->length; ++index) {
            size_t target = (size_t)(indices->start + (int64_t)index * indices->step);
            size_t scalar_size;

            while (scalar_index > target) {
                byte_index -= 1U;
                while (byte_index != 0U && ((uint8_t)utf8[byte_index] & 0xc0U) == 0x80U) {
                    byte_index -= 1U;
                }
                scalar_index -= 1U;
            }
            uint8_t lead = (uint8_t)utf8[byte_index];
            scalar_size = lead < 0x80U ? 1U : (lead < 0xe0U ? 2U : (lead < 0xf0U ? 3U : 4U));
            (void)memcpy(selected + selected_size, utf8 + byte_index, scalar_size);
            selected_size += scalar_size;
        }
    }
    tinypy_value_t *result = tinypy_unicode_from_utf8(vm, selected, selected_size);
    tinypy_internal_vm_deallocate(vm, selected, selected_capacity);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_item_collect_iterable(tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_value_t *items = tinypy_list_from_items(vm, NULL, 0U);
    tinypy_value_t *iterator = NULL;

    if (value->type != &vm->types[TINYPY_VALUE_LIST] && value->type != &vm->types[TINYPY_VALUE_TUPLE]) {
        iterator = tinypy_iter(value, out_error);
        if (iterator == NULL) {
            TINYPY_DECREF(items);
            return NULL;
        }
        value = iterator;
    }
    tinypy_bool_t collected = tinypy_internal_list_extend_iterable(items, value, out_error);

    if (iterator != NULL) {
        TINYPY_DECREF(iterator);
    }
    if (collected == 0) {
        TINYPY_DECREF(items);
        return NULL;
    }
    return items;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_item_list_set_slice(tinypy_value_t *list, tinypy_value_t *slice, tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(list);
    tinypy_internal_slice_indices_t indices;
    size_t replacement_size;
    tinypy_value_t *const *replacement_items;
    tinypy_bool_t replaced;

    if (tinypy_internal_slice_unpack(slice, &indices, out_error) == 0) {
        return TINYPY_FALSE;
    }
    /* Contiguous assignment retains the offsets normalized before the
       replacement iterator runs, then clamps them to the live list. */
    if (tinypy_internal_slice_adjust_indices(vm, TINYPY_LIST_SIZE(list), &indices, out_error) == 0) {
        return TINYPY_FALSE;
    }
    tinypy_value_t *replacement = value == list
                                      ? tinypy_list_from_items(vm, TINYPY_LIST_OBJECT(list)->items, TINYPY_LIST_SIZE(list))
                                      : __tinypy_item_collect_iterable(value, out_error);
    if (replacement == NULL) {
        return TINYPY_FALSE;
    }
    size_t size = TINYPY_LIST_SIZE(list);
    tinypy_bool_t adjust = indices.step == 1;

    if (indices.length != 0U && indices.step != 1) {
        int64_t last = indices.start + (int64_t)(indices.length - 1U) * indices.step;

        adjust = (uint64_t)indices.start >= (uint64_t)size || (uint64_t)last >= (uint64_t)size;
        if (adjust != 0 && indices.stop == -1) {
            indices.stop = INT64_MIN;
        }
    }
    if (adjust != 0 && tinypy_internal_slice_adjust_indices(vm, size, &indices, out_error) == 0) {
        TINYPY_DECREF(replacement);
        return TINYPY_FALSE;
    }
    replacement_size = TINYPY_LIST_SIZE(replacement);
    replacement_items = TINYPY_LIST_OBJECT(replacement)->items;
    if (indices.step == 1) {
        replaced = tinypy_internal_list_replace_range_checked(list, (size_t)indices.start, indices.length, replacement_items, replacement_size, out_error);
    }
    else if (replacement_size != indices.length) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "extended slice assignment has the wrong size", out_error);
        replaced = TINYPY_FALSE;
    }
    else {
        replaced = tinypy_internal_list_replace_strided_checked(list, (size_t)indices.start, indices.step, indices.length, replacement_items, out_error);
    }
    TINYPY_DECREF(replacement);
    return replaced;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_item_list_delete_slice(tinypy_value_t *list, tinypy_value_t *slice, tinypy_error_t **out_error) {
    tinypy_internal_slice_indices_t indices;
    tinypy_bool_t deleted;

    if (tinypy_internal_slice_unpack(slice, &indices, out_error) == 0
        || tinypy_internal_slice_adjust_indices(TINYPY_VALUE_VM(list), TINYPY_LIST_SIZE(list), &indices, out_error) == 0) {
        return TINYPY_FALSE;
    }
    if (indices.step == 1) {
        deleted = tinypy_internal_list_replace_range_checked(list, (size_t)indices.start, indices.length, NULL, 0U, out_error);
    }
    else {
        deleted = tinypy_internal_list_delete_strided_checked(list, (size_t)indices.start, indices.step, indices.length, out_error);
    }
    return deleted;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_get_item(tinypy_value_t *container, tinypy_value_t *key, tinypy_bool_t dispatch_special, tinypy_error_t **out_error) {
    tinypy_value_type_e kind;
    size_t index;
    tinypy_value_t *item;

    tinypy_vm_t *vm = TINYPY_VALUE_VM(container);
    TINYPY_CLEAR_ERROR(out_error);
    if (dispatch_special != 0 && tinypy_internal_object_has_special_override_key(container, vm->internal_special_getitem_key) != 0) {
        tinypy_value_t *return_value_1 = __tinypy_item_call_method(container, vm->internal_special_getitem_key, &key, 1U, out_error);
        return return_value_1;
    }
    if (container->type->mapping_slots != NULL && container->type->mapping_slots->get_item != NULL) {
        tinypy_value_t *return_value_2 = container->type->mapping_slots->get_item(container, key, out_error);
        return return_value_2;
    }
    if (container->type->sequence_slots != NULL && container->type->sequence_slots->get_item != NULL) {
        tinypy_value_t *return_value_3 = container->type->sequence_slots->get_item(container, key, out_error);
        return return_value_3;
    }
    kind = TINYPY_VALUE_KIND(container);
    if (kind == TINYPY_VALUE_DICT) {
        if (tinypy_internal_dict_get_optional_checked(vm, container, key, &item, out_error) == 0) {
            return NULL;
        }
        if (item == NULL) {
            if (container->type != &vm->types[TINYPY_VALUE_DICT] && tinypy_internal_object_has_special_key(container, vm->internal_special_missing_key) != 0) {
                tinypy_value_t *result = __tinypy_item_call_method(container, vm->internal_special_missing_key, &key, 1U, out_error);

                return result;
            }
            tinypy_internal_exception_raise_key_error(vm, key, out_error);
            return NULL;
        }
        return TINYPY_RET(item);
    }
    if (TINYPY_VALUE_KIND(key) == TINYPY_VALUE_SLICE) {
        tinypy_internal_slice_indices_t indices;
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
            if (dispatch_special != 0 && tinypy_internal_object_has_special_key(container, vm->internal_special_getitem_key) != 0) {
                tinypy_value_t *return_value_3 = __tinypy_item_call_method(container, vm->internal_special_getitem_key, &key, 1U, out_error);
                return return_value_3;
            }
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "object does not support slicing", out_error);
            return NULL;
        }
        if (tinypy_internal_slice_unpack(key, &indices, out_error) == 0
            || tinypy_internal_slice_adjust_indices(vm, kind == TINYPY_VALUE_LIST ? TINYPY_LIST_SIZE(container) : size, &indices, out_error) == 0) {
            return NULL;
        }
        if (kind == TINYPY_VALUE_TUPLE || kind == TINYPY_VALUE_LIST) {
            tinypy_value_t *return_value_4 = __tinypy_item_sequence_slice(container, &indices, out_error);
            return return_value_4;
        }
        if (kind == TINYPY_VALUE_STRING) {
            tinypy_value_t *return_value_5 = __tinypy_item_string_slice(container, &indices, out_error);
            return return_value_5;
        }
        tinypy_value_t *return_value_6 = __tinypy_item_unicode_slice(container, &indices);
        return return_value_6;
    }
    if (kind == TINYPY_VALUE_TUPLE) {
        size_t tuple_size = TINYPY_TUPLE_SIZE(container);
        if (__tinypy_item_normalize_index(vm, key, tuple_size, &index, out_error) == 0) {
            return NULL;
        }
        item = TINYPY_RET(TINYPY_TUPLE_GET(container, index));
        return item;
    }
    if (kind == TINYPY_VALUE_LIST) {
        if (__tinypy_item_list_index(container, key, &index, out_error) == 0) {
            return NULL;
        }
        item = TINYPY_RET(TINYPY_LIST_GET(container, index));
        return item;
    }
    if (kind == TINYPY_VALUE_STRING) {
        const uint8_t *bytes;
        size_t size;

        bytes = (const uint8_t *)tinypy_string_view(container, &size);
        if (__tinypy_item_normalize_index(vm, key, size, &index, out_error) == 0) {
            return NULL;
        }
        tinypy_value_t *return_value_7 = tinypy_string_from_bytes(vm, bytes + index, 1U);
        return return_value_7;
    }
    if (kind == TINYPY_VALUE_UNICODE) {
        size_t byte_size;
        size_t code_point_count;

        (void)tinypy_unicode_utf8_view(container, &byte_size, &code_point_count);
        if (__tinypy_item_normalize_index(vm, key, code_point_count, &index, out_error) == 0) {
            return NULL;
        }
        tinypy_value_t *return_value_8 = __tinypy_item_unicode_get(container, index);
        return return_value_8;
    }
    if (kind == TINYPY_VALUE_XRANGE) {
        tinypy_xrange_object_t *range = TINYPY_XRANGE_OBJECT(container);

        if (__tinypy_item_normalize_index(vm, key, range->length, &index, out_error) == 0) {
            return NULL;
        }
        tinypy_value_t *return_value_9 = tinypy_integer_from_i64(vm, tinypy_internal_xrange_item_value(range, index));
        return return_value_9;
    }
    if (dispatch_special != 0 && tinypy_internal_object_has_special_key(container, vm->internal_special_getitem_key) != 0) {
        tinypy_value_t *return_value_10 = __tinypy_item_call_method(container, vm->internal_special_getitem_key, &key, 1U, out_error);
        return return_value_10;
    }
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "object does not support item access", out_error);
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_item_slice_bound(tinypy_value_t *bound, int64_t fallback, int64_t *out_bound, tinypy_error_t **out_error) {
    if (bound == NULL || TINYPY_VALUE_KIND(bound) == TINYPY_VALUE_NONE) {
        *out_bound = fallback;
        return TINYPY_TRUE;
    }
    tinypy_bool_t return_value_1 = tinypy_internal_index_as_i64(bound, out_bound, TINYPY_TRUE, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
/* The legacy two-argument slice protocol receives plain offsets: a missing
   upper bound becomes the largest index and negative bounds are resolved
   against __len__ when the container provides one. */
static tinypy_bool_t __tinypy_item_legacy_slice_bounds(tinypy_value_t *container, tinypy_value_t *start, tinypy_value_t *stop, int64_t *out_start, int64_t *out_stop, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(container);
    tinypy_value_t *length_value;
    int64_t length;

    if (__tinypy_item_slice_bound(start, 0, out_start, out_error) == 0) {
        return TINYPY_FALSE;
    }
    if (__tinypy_item_slice_bound(stop, INT64_MAX, out_stop, out_error) == 0) {
        return TINYPY_FALSE;
    }
    if (*out_start >= 0 && *out_stop >= 0) {
        return TINYPY_TRUE;
    }
    if (tinypy_internal_object_has_special_key(container, vm->internal_special_length_key) == 0) {
        return TINYPY_TRUE;
    }
    length_value = __tinypy_item_call_method(container, vm->internal_special_length_key, NULL, 0U, out_error);
    if (length_value == NULL) {
        return TINYPY_FALSE;
    }
    if (tinypy_internal_index_as_i64(length_value, &length, TINYPY_TRUE, out_error) == 0) {
        TINYPY_DECREF(length_value);
        return TINYPY_FALSE;
    }
    TINYPY_DECREF(length_value);
    if (*out_start < 0) {
        *out_start += length;
    }
    if (*out_stop < 0) {
        *out_stop += length;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_item_call_legacy_slice(tinypy_value_t *container, tinypy_value_t *name, tinypy_value_t *start, tinypy_value_t *stop, tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(container);
    tinypy_value_t *items[3];
    tinypy_value_t *result;
    size_t item_count = value != NULL ? 3U : 2U;
    int64_t low;
    int64_t high;

    if (__tinypy_item_legacy_slice_bounds(container, start, stop, &low, &high, out_error) == 0) {
        return NULL;
    }
    items[0] = tinypy_integer_from_i64(vm, low);
    items[1] = tinypy_integer_from_i64(vm, high);
    items[2] = value;
    result = __tinypy_item_call_method(container, name, items, item_count, out_error);
    TINYPY_DECREF(items[1]);
    TINYPY_DECREF(items[0]);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_item_fallback_slice(tinypy_value_t *container, tinypy_value_t *start, tinypy_value_t *stop, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(container);

    if (TINYPY_VALUE_KIND(container) != TINYPY_VALUE_OLD_INSTANCE) {
        tinypy_value_t *slice = tinypy_slice_new(vm, start, stop, NULL);
        return slice;
    }
    int64_t low;
    int64_t high;

    if (__tinypy_item_legacy_slice_bounds(container, start, stop, &low, &high, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *low_value = tinypy_integer_from_i64(vm, low);
    tinypy_value_t *high_value = tinypy_integer_from_i64(vm, high);
    tinypy_value_t *slice = tinypy_slice_new(vm, low_value, high_value, NULL);

    TINYPY_DECREF(high_value);
    TINYPY_DECREF(low_value);
    return slice;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_get_slice(tinypy_value_t *container, tinypy_value_t *start, tinypy_value_t *stop, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(container);
    tinypy_value_t *slice;
    tinypy_value_t *result;

    if (tinypy_internal_object_has_special_override_key(container, vm->internal_special_getslice_key) != 0) {
        tinypy_value_t *return_value_1 = __tinypy_item_call_legacy_slice(container, vm->internal_special_getslice_key, start, stop, NULL, out_error);
        return return_value_1;
    }
    slice = __tinypy_item_fallback_slice(container, start, stop, out_error);
    if (slice == NULL) {
        return NULL;
    }
    result = tinypy_get_item(container, slice, out_error);
    TINYPY_DECREF(slice);
    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_set_slice(tinypy_value_t *container, tinypy_value_t *start, tinypy_value_t *stop, tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(container);
    tinypy_value_t *slice;
    tinypy_bool_t stored;

    if (tinypy_internal_object_has_special_override_key(container, vm->internal_special_setslice_key) != 0) {
        tinypy_value_t *result = __tinypy_item_call_legacy_slice(container, vm->internal_special_setslice_key, start, stop, value, out_error);

        if (result == NULL) {
            return TINYPY_FALSE;
        }
        TINYPY_DECREF(result);
        return TINYPY_TRUE;
    }
    slice = __tinypy_item_fallback_slice(container, start, stop, out_error);
    if (slice == NULL) {
        return TINYPY_FALSE;
    }
    stored = tinypy_set_item(container, slice, value, out_error);
    TINYPY_DECREF(slice);
    return stored;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_delete_slice(tinypy_value_t *container, tinypy_value_t *start, tinypy_value_t *stop, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(container);
    tinypy_value_t *slice;
    tinypy_bool_t deleted;

    if (tinypy_internal_object_has_special_override_key(container, vm->internal_special_delslice_key) != 0) {
        tinypy_value_t *result = __tinypy_item_call_legacy_slice(container, vm->internal_special_delslice_key, start, stop, NULL, out_error);

        if (result == NULL) {
            return TINYPY_FALSE;
        }
        TINYPY_DECREF(result);
        return TINYPY_TRUE;
    }
    slice = __tinypy_item_fallback_slice(container, start, stop, out_error);
    if (slice == NULL) {
        return TINYPY_FALSE;
    }
    deleted = tinypy_delete_item(container, slice, out_error);
    TINYPY_DECREF(slice);
    return deleted;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_get_item_builtin(tinypy_value_t *container, tinypy_value_t *key, tinypy_error_t **out_error) {
    tinypy_value_t *result = __tinypy_get_item(container, key, TINYPY_FALSE, out_error);

    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_get_item(tinypy_value_t *container, tinypy_value_t *key, tinypy_error_t **out_error) {
    tinypy_value_t *result = __tinypy_get_item(container, key, TINYPY_TRUE, out_error);

    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_set_item(tinypy_value_t *container, tinypy_value_t *key, tinypy_value_t *value, tinypy_bool_t dispatch_special, tinypy_error_t **out_error) {
    tinypy_value_type_e kind;
    size_t index;

    tinypy_vm_t *vm = TINYPY_VALUE_VM(container);
    TINYPY_CLEAR_ERROR(out_error);
    if (dispatch_special != 0 && tinypy_internal_object_has_special_override_key(container, vm->internal_special_setitem_key) != 0) {
        tinypy_value_t *items[2] = {key, value};
        tinypy_value_t *result = __tinypy_item_call_method(container, vm->internal_special_setitem_key, items, 2U, out_error);

        if (result == NULL) {
            return TINYPY_FALSE;
        }
        TINYPY_DECREF(result);
        return TINYPY_TRUE;
    }
    if (container->type->mapping_slots != NULL && container->type->mapping_slots->set_item != NULL) {
        tinypy_bool_t return_value_1 = container->type->mapping_slots->set_item(container, key, value, out_error);
        return return_value_1;
    }
    if (container->type->sequence_slots != NULL && container->type->sequence_slots->set_item != NULL) {
        tinypy_bool_t return_value_2 = container->type->sequence_slots->set_item(container, key, value, out_error);
        return return_value_2;
    }
    kind = TINYPY_VALUE_KIND(container);
    if (kind == TINYPY_VALUE_DICT) {
        tinypy_bool_t return_value_3 = tinypy_internal_dict_set_checked(vm, container, key, value, out_error);
        return return_value_3;
    }
    if (kind == TINYPY_VALUE_LIST) {
        if (TINYPY_VALUE_KIND(key) == TINYPY_VALUE_SLICE) {
            tinypy_bool_t return_value_3 = __tinypy_item_list_set_slice(container, key, value, out_error);
            return return_value_3;
        }
        if (__tinypy_item_list_index(container, key, &index, out_error) == 0) {
            return TINYPY_FALSE;
        }
        tinypy_list_set(container, index, value);
        return TINYPY_TRUE;
    }
    if (dispatch_special != 0 && tinypy_internal_object_has_special_key(container, vm->internal_special_setitem_key) != 0) {
        tinypy_value_t *items[2] = {key, value};
        tinypy_value_t *result = __tinypy_item_call_method(container, vm->internal_special_setitem_key, items, 2U, out_error);

        if (result == NULL) {
            return TINYPY_FALSE;
        }
        TINYPY_DECREF(result);
        return TINYPY_TRUE;
    }
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "object does not support item assignment", out_error);
    return TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_set_item_builtin(tinypy_value_t *container, tinypy_value_t *key, tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_bool_t result = __tinypy_set_item(container, key, value, TINYPY_FALSE, out_error);

    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_set_item(tinypy_value_t *container, tinypy_value_t *key, tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_bool_t result = __tinypy_set_item(container, key, value, TINYPY_TRUE, out_error);

    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_delete_item(tinypy_value_t *container, tinypy_value_t *key, tinypy_bool_t dispatch_special, tinypy_error_t **out_error) {
    tinypy_value_type_e kind;
    size_t index;

    tinypy_vm_t *vm = TINYPY_VALUE_VM(container);
    TINYPY_CLEAR_ERROR(out_error);
    if (dispatch_special != 0 && tinypy_internal_object_has_special_override_key(container, vm->internal_special_delitem_key) != 0) {
        tinypy_value_t *result = __tinypy_item_call_method(container, vm->internal_special_delitem_key, &key, 1U, out_error);

        if (result == NULL) {
            return TINYPY_FALSE;
        }
        TINYPY_DECREF(result);
        return TINYPY_TRUE;
    }
    if (container->type->mapping_slots != NULL && container->type->mapping_slots->set_item != NULL) {
        tinypy_bool_t return_value_1 = container->type->mapping_slots->set_item(container, key, NULL, out_error);
        return return_value_1;
    }
    kind = TINYPY_VALUE_KIND(container);
    if (kind == TINYPY_VALUE_DICT) {
        tinypy_bool_t deleted;

        if (tinypy_internal_dict_delete_optional_checked(vm, container, key, &deleted, out_error) == 0) {
            return TINYPY_FALSE;
        }
        if (deleted == 0) {
            tinypy_internal_exception_raise_key_error(vm, key, out_error);
            return TINYPY_FALSE;
        }
        return TINYPY_TRUE;
    }
    if (kind == TINYPY_VALUE_LIST) {
        if (TINYPY_VALUE_KIND(key) == TINYPY_VALUE_SLICE) {
            tinypy_bool_t return_value_2 = __tinypy_item_list_delete_slice(container, key, out_error);
            return return_value_2;
        }
        if (__tinypy_item_list_index(container, key, &index, out_error) == 0) {
            return TINYPY_FALSE;
        }
        tinypy_list_delete(container, index);
        return TINYPY_TRUE;
    }
    if (dispatch_special != 0 && tinypy_internal_object_has_special_key(container, vm->internal_special_delitem_key) != 0) {
        tinypy_value_t *result = __tinypy_item_call_method(container, vm->internal_special_delitem_key, &key, 1U, out_error);

        if (result == NULL) {
            return TINYPY_FALSE;
        }
        TINYPY_DECREF(result);
        return TINYPY_TRUE;
    }
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "object does not support item deletion", out_error);
    return TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_delete_item_builtin(tinypy_value_t *container, tinypy_value_t *key, tinypy_error_t **out_error) {
    tinypy_bool_t result = __tinypy_delete_item(container, key, TINYPY_FALSE, out_error);

    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_delete_item(tinypy_value_t *container, tinypy_value_t *key, tinypy_error_t **out_error) {
    tinypy_bool_t result = __tinypy_delete_item(container, key, TINYPY_TRUE, out_error);

    return result;
}
