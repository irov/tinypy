#include "tinypy/item.h"

#include "internal.h"

#include <string.h>

//////////////////////////////////////////////////////////////////////////
static void __tinypy_item_invalid_index_result(tinypy_vm_t *vm, tinypy_value_t *result, tinypy_error_t **out_error) {
    tinypy_message_part_t parts[] = {
        TINYPY_MESSAGE_PART_LITERAL("__index__ returned non-(int,long) (type "),
        TINYPY_MESSAGE_PART_TYPE_NAME(result),
        TINYPY_MESSAGE_PART_LITERAL(")")
    };

    tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, 3U, out_error);
}
//////////////////////////////////////////////////////////////////////////
/* PyIndex_Check: classic instances always qualify and fail on conversion
   when they lack __index__. */
static tinypy_bool_t __tinypy_item_is_index(tinypy_value_t *value) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);

    if (kind == TINYPY_VALUE_BOOL || kind == TINYPY_VALUE_INTEGER || kind == TINYPY_VALUE_LONG || kind == TINYPY_VALUE_OLD_INSTANCE) {
        return TINYPY_TRUE;
    }
    tinypy_bool_t has_index = tinypy_internal_object_has_special_key(value, vm->internal_special_index_key) != 0 ? TINYPY_TRUE : TINYPY_FALSE;
    return has_index;
}
//////////////////////////////////////////////////////////////////////////
/* PyNumber_Index; a classic instance looks __index__ up like any attribute. */
tinypy_value_t *tinypy_internal_index_value(tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_value_t *method = NULL;

    if (kind == TINYPY_VALUE_BOOL) {
        tinypy_value_t *return_value_1 = tinypy_integer_from_i64(vm, TINYPY_INTEGER_VALUE(value));
        return return_value_1;
    }
    if (kind == TINYPY_VALUE_INTEGER || kind == TINYPY_VALUE_LONG) {
        return TINYPY_RET(value);
    }
    if (kind == TINYPY_VALUE_OLD_INSTANCE) {
        int32_t status = tinypy_internal_object_get_optional_attr_key(value, vm->internal_special_index_key, &method, out_error);

        if (status < 0) {
            return NULL;
        }
        if (status == 0) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "object cannot be interpreted as an index", out_error);
            return NULL;
        }
    }
    else if (tinypy_internal_object_has_special_key(value, vm->internal_special_index_key) != 0) {
        method = tinypy_internal_object_get_special_key(value, vm->internal_special_index_key, out_error);
        if (method == NULL) {
            return NULL;
        }
    }
    else {
        tinypy_message_part_t parts[] = {
            TINYPY_MESSAGE_PART_LITERAL("'"),
            TINYPY_MESSAGE_PART_TYPE_NAME(value),
            TINYPY_MESSAGE_PART_LITERAL("' object cannot be interpreted as an index")
        };

        tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, 3U, out_error);
        return NULL;
    }
    tinypy_value_t *args = TINYPY_RET_EMPTY_TUPLE(vm);
    tinypy_value_t *converted = tinypy_call(method, args, NULL, out_error);
    TINYPY_DECREF(args);
    TINYPY_DECREF(method);
    if (converted == NULL) {
        return NULL;
    }
    kind = TINYPY_VALUE_KIND(converted);
    if (kind != TINYPY_VALUE_BOOL && kind != TINYPY_VALUE_INTEGER && kind != TINYPY_VALUE_LONG) {
        __tinypy_item_invalid_index_result(vm, converted, out_error);
        TINYPY_DECREF(converted);
        return NULL;
    }
    if (kind == TINYPY_VALUE_BOOL) {
        tinypy_value_t *result = tinypy_integer_from_i64(vm, TINYPY_INTEGER_VALUE(converted));

        TINYPY_DECREF(converted);
        return result;
    }
    return converted;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_item_long_as_i64(const tinypy_value_t *value, int64_t *out_index) {
    const tinypy_long_object_t *long_value = TINYPY_LONG_OBJECT(value);
    size_t count = TINYPY_LONG_DIGIT_COUNT(value);
    uint64_t magnitude = 0U;
    size_t index;

    if (count > 5U) {
        return TINYPY_FALSE;
    }
    for (index = count; index != 0U; index -= 1U) {
        if (magnitude > (UINT64_MAX >> 15U)) {
            return TINYPY_FALSE;
        }
        magnitude = (magnitude << 15U) | long_value->digits[index - 1U];
    }
    if (TINYPY_LONG_SIGN(value) >= 0) {
        if (magnitude > (uint64_t)INT64_MAX) {
            return TINYPY_FALSE;
        }
        *out_index = (int64_t)magnitude;
        return TINYPY_TRUE;
    }
    if (magnitude > (uint64_t)INT64_MAX + UINT64_C(1)) {
        return TINYPY_FALSE;
    }
    *out_index = magnitude == (uint64_t)INT64_MAX + UINT64_C(1) ? INT64_MIN : -(int64_t)magnitude;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
/* PyNumber_AsSsize_t: an index beyond the int64 range is clamped, or raises
   the error kind the caller names about the original object. */
static tinypy_bool_t __tinypy_item_number_as_ssize(tinypy_value_t *value, tinypy_bool_t clamp, tinypy_error_kind_e overflow_error, int64_t *out_index, tinypy_error_t **out_error) {
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);

    if (kind == TINYPY_VALUE_BOOL || kind == TINYPY_VALUE_INTEGER) {
        *out_index = TINYPY_INTEGER_VALUE(value);
        return TINYPY_TRUE;
    }
    if (clamp == 0) {
        tinypy_bool_t converted = tinypy_internal_number_as_index(value, overflow_error, out_index, out_error);
        return converted;
    }
    tinypy_value_t *integer = tinypy_internal_index_value(value, out_error);
    if (integer == NULL) {
        return TINYPY_FALSE;
    }
    if (TINYPY_VALUE_KIND(integer) != TINYPY_VALUE_LONG) {
        *out_index = TINYPY_INTEGER_VALUE(integer);
    }
    else if (__tinypy_item_long_as_i64(integer, out_index) == 0) {
        *out_index = TINYPY_LONG_SIGN(integer) < 0 ? INT64_MIN : INT64_MAX;
    }
    TINYPY_DECREF(integer);
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
/* Callers negate clamped indices, so the clamp stops at -INT64_MAX. */
tinypy_bool_t tinypy_internal_index_as_i64(tinypy_value_t *value, int64_t *out_index, tinypy_bool_t clamp_overflow, tinypy_error_t **out_error) {
    if (__tinypy_item_number_as_ssize(value, clamp_overflow, TINYPY_ERROR_OVERFLOW, out_index, out_error) == 0) {
        return TINYPY_FALSE;
    }
    if (clamp_overflow != 0 && *out_index == INT64_MIN) {
        *out_index = -INT64_MAX;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
/* _PyEval_SliceIndexNotNone reads the bounds of list.index and tuple.index. */
tinypy_bool_t tinypy_internal_slice_index_not_none(tinypy_value_t *value, int64_t *out_index, tinypy_error_t **out_error) {
    if (__tinypy_item_is_index(value) == 0) {
        tinypy_internal_make_vm_error(TINYPY_VALUE_VM(value), TINYPY_ERROR_TYPE, "slice indices must be integers or have an __index__ method", out_error);
        return TINYPY_FALSE;
    }
    tinypy_bool_t converted = tinypy_internal_index_as_i64(value, out_index, TINYPY_TRUE, out_error);
    return converted;
}
//////////////////////////////////////////////////////////////////////////
/* _PyEval_SliceIndex */
static tinypy_bool_t __tinypy_item_slice_index(tinypy_value_t *value, int64_t *out_index, tinypy_error_t **out_error) {
    if (__tinypy_item_is_index(value) == 0) {
        tinypy_internal_make_vm_error(TINYPY_VALUE_VM(value), TINYPY_ERROR_TYPE, "slice indices must be integers or None or have an __index__ method", out_error);
        return TINYPY_FALSE;
    }
    tinypy_bool_t converted = __tinypy_item_number_as_ssize(value, TINYPY_TRUE, TINYPY_ERROR_OVERFLOW, out_index, out_error);
    return converted;
}
//////////////////////////////////////////////////////////////////////////
/* _PySlice_Unpack resolves callbacks before the caller reads the length of a
   mutable sequence. */
tinypy_bool_t tinypy_internal_slice_unpack(tinypy_value_t *slice_value, tinypy_internal_slice_indices_t *indices, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(slice_value);
    tinypy_slice_object_t *slice = TINYPY_SLICE_OBJECT(slice_value);

    indices->step = 1;
    if (TINYPY_VALUE_KIND(slice->step) != TINYPY_VALUE_NONE
        && __tinypy_item_slice_index(slice->step, &indices->step, out_error) == 0) {
        return TINYPY_FALSE;
    }
    if (indices->step == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "slice step cannot be zero", out_error);
        return TINYPY_FALSE;
    }
    if (indices->step == INT64_MIN) {
        indices->step = -INT64_MAX;
    }
    indices->start = indices->step < 0 ? INT64_MAX : 0;
    if (TINYPY_VALUE_KIND(slice->start) != TINYPY_VALUE_NONE
        && __tinypy_item_slice_index(slice->start, &indices->start, out_error) == 0) {
        return TINYPY_FALSE;
    }
    indices->stop = indices->step < 0 ? INT64_MIN : INT64_MAX;
    if (TINYPY_VALUE_KIND(slice->stop) != TINYPY_VALUE_NONE
        && __tinypy_item_slice_index(slice->stop, &indices->stop, out_error) == 0) {
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
static size_t __tinypy_item_sequence_size(tinypy_value_t *container) {
    size_t size;

    switch (TINYPY_VALUE_KIND(container)) {
    case TINYPY_VALUE_TUPLE:
        size = TINYPY_TUPLE_SIZE(container);
        break;
    case TINYPY_VALUE_LIST:
        size = TINYPY_LIST_SIZE(container);
        break;
    case TINYPY_VALUE_STRING:
        (void)tinypy_string_view(container, &size);
        break;
    case TINYPY_VALUE_UNICODE: {
        size_t byte_size;

        (void)tinypy_unicode_utf8_view(container, &byte_size, &size);
        break;
    }
    default:
        size = TINYPY_XRANGE_OBJECT(container)->length;
        break;
    }
    return size;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_item_indices_error(tinypy_vm_t *vm, tinypy_message_part_t sequence, tinypy_value_t *key, tinypy_error_t **out_error) {
    tinypy_message_part_t parts[] = {
        sequence,
        TINYPY_MESSAGE_PART_LITERAL(" indices must be integers, not "),
        TINYPY_MESSAGE_PART_TYPE_NAME(key)
    };

    tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, 3U, out_error);
}
//////////////////////////////////////////////////////////////////////////
/* PyObject_GetItem refuses a key without __index__ for a type that has only
   the sequence item slot. */
static void __tinypy_item_sequence_index_error(tinypy_vm_t *vm, tinypy_value_t *key, tinypy_error_t **out_error) {
    tinypy_message_part_t parts[] = {
        TINYPY_MESSAGE_PART_LITERAL("sequence index must be integer, not '"),
        TINYPY_MESSAGE_PART_TYPE_NAME(key),
        TINYPY_MESSAGE_PART_LITERAL("'")
    };

    tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, 3U, out_error);
}
//////////////////////////////////////////////////////////////////////////
/* list_subscript and its siblings word a key without __index__ after the
   sequence type and report a key too large for an index as IndexError. The
   size is read after the conversion, since __index__ may resize the list. */
static tinypy_bool_t __tinypy_item_position(tinypy_value_t *container, tinypy_value_t *key, const char *range_message, size_t *out_index, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(container);
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(container);
    int64_t index;

    if (__tinypy_item_is_index(key) == 0) {
        if (kind == TINYPY_VALUE_TUPLE) {
            __tinypy_item_indices_error(vm, (tinypy_message_part_t)TINYPY_MESSAGE_PART_LITERAL("tuple"), key, out_error);
        }
        else if (kind == TINYPY_VALUE_STRING) {
            __tinypy_item_indices_error(vm, (tinypy_message_part_t)TINYPY_MESSAGE_PART_LITERAL("string"), key, out_error);
        }
        else if (kind == TINYPY_VALUE_UNICODE) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "string indices must be integers", out_error);
        }
        else if (kind == TINYPY_VALUE_XRANGE) {
            __tinypy_item_sequence_index_error(vm, key, out_error);
        }
        else {
            __tinypy_item_indices_error(vm, (tinypy_message_part_t)TINYPY_MESSAGE_PART_LITERAL("list"), key, out_error);
        }
        return TINYPY_FALSE;
    }
    if (__tinypy_item_number_as_ssize(key, TINYPY_FALSE, TINYPY_ERROR_INDEX, &index, out_error) == 0) {
        return TINYPY_FALSE;
    }
    size_t size = __tinypy_item_sequence_size(container);
    uint64_t distance = index < 0 ? (uint64_t)(-(index + 1)) + UINT64_C(1) : UINT64_C(0);
    if ((index < 0 && distance > size) || (index >= 0 && (uint64_t)index >= (uint64_t)size)) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_INDEX, range_message, out_error);
        return TINYPY_FALSE;
    }
    *out_index = index < 0 ? size - (size_t)distance : (size_t)index;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
/* Python 2.7 types with a sequence protocol word a missing subscript as
   unsupported indexing; others have no __getitem__ at all. */
static tinypy_bool_t __tinypy_item_has_sequence_protocol(const tinypy_value_t *container) {
    switch (TINYPY_VALUE_KIND(container)) {
    case TINYPY_VALUE_TUPLE:
    case TINYPY_VALUE_STRING:
    case TINYPY_VALUE_UNICODE:
    case TINYPY_VALUE_XRANGE:
    case TINYPY_VALUE_SET:
    case TINYPY_VALUE_FROZENSET:
    case TINYPY_VALUE_DICT_KEYS:
    case TINYPY_VALUE_DICT_VALUES:
    case TINYPY_VALUE_DICT_ITEMS:
        return TINYPY_TRUE;
    default:
        break;
    }
    return (container->type->flags & TINYPY_TYPE_FLAG_PYTHON_HEAP) != 0U ? TINYPY_TRUE : TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
/* The fallbacks of PyObject_GetItem, PyObject_SetItem and PyObject_DelItem:
   a sequence first converts an index key, then refuses the operation. */
static void __tinypy_item_unsupported(tinypy_value_t *container, tinypy_value_t *key, tinypy_message_part_t sequence_message, tinypy_message_part_t message, tinypy_error_t **out_error) {
    int64_t index;

    if (__tinypy_item_has_sequence_protocol(container) != 0 && __tinypy_item_is_index(key) != 0) {
        if (__tinypy_item_number_as_ssize(key, TINYPY_FALSE, TINYPY_ERROR_INDEX, &index, out_error) == 0) {
            return;
        }
        message = sequence_message;
    }
    tinypy_message_part_t parts[] = {
        TINYPY_MESSAGE_PART_LITERAL("'"),
        TINYPY_MESSAGE_PART_TYPE_NAME(container),
        message
    };
    tinypy_internal_make_vm_error_parts(TINYPY_VALUE_VM(container), TINYPY_ERROR_TYPE, parts, 3U, out_error);
}
//////////////////////////////////////////////////////////////////////////
/* slot_mp_ass_subscript serves assignment and deletion alike once a class
   defines either method, and the missing one then raises an AttributeError
   naming it. */
static tinypy_bool_t __tinypy_item_missing_pair_method(tinypy_value_t *container, tinypy_value_t *name, tinypy_value_t *pair_name, tinypy_error_t **out_error) {
    if ((container->type->flags & TINYPY_TYPE_FLAG_PYTHON_HEAP) == 0U || tinypy_internal_object_has_special_key(container, pair_name) == 0) {
        return TINYPY_FALSE;
    }
    tinypy_message_part_t parts[] = {
        TINYPY_MESSAGE_PART_TEXT(name)
    };

    tinypy_internal_make_vm_error_parts(TINYPY_VALUE_VM(container), TINYPY_ERROR_ATTRIBUTE, parts, 1U, out_error);
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_item_call_method(tinypy_value_t *container, tinypy_value_t *name, tinypy_value_t *const *items, size_t item_count, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(container);
    tinypy_value_t *function = item_count <= 2U ? tinypy_internal_type_function_key(container, name) : NULL;

    if (function != NULL) {
        tinypy_value_t *direct = tinypy_internal_call_type_function(function, container, items, item_count, out_error);
        return direct;
    }
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
    /* A contiguous list slice fits the reserved storage in one piece. */
    if (tuple == 0 && indices->step == 1 && indices->length != 0U) {
        tinypy_list_extend(result, TINYPY_LIST_OBJECT(container)->items + indices->start, indices->length);
        return result;
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
/* PySequence_Fast replaces the TypeError of a non-iterable with its own. */
static tinypy_value_t *__tinypy_item_collect_iterable(tinypy_value_t *value, const char *type_message, const char *negative_hint_message, tinypy_bool_t *out_aborted, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_value_t *items = tinypy_list_from_items(vm, NULL, 0U);
    tinypy_value_t *iterator = NULL;

    *out_aborted = TINYPY_FALSE;

    if (value->type != &vm->types[TINYPY_VALUE_LIST] && value->type != &vm->types[TINYPY_VALUE_TUPLE]) {
        iterator = tinypy_iter(value, out_error);
        if (iterator == NULL) {
            if (tinypy_internal_exception_consume_kind(vm, TINYPY_EXCEPTION_TYPE_ERROR, out_error) != TINYPY_FALSE) {
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, type_message, out_error);
            }
            TINYPY_DECREF(items);
            return NULL;
        }
        value = iterator;
    }
    tinypy_error_t *collection_error = NULL;
    tinypy_bool_t collected = tinypy_internal_list_extend_iterable(items, value, negative_hint_message, &collection_error);

    if (iterator != NULL) {
        TINYPY_DECREF(iterator);
    }
    if (collected == 0) {
        /* A NULL diagnostic permits only the helper's hint -1 abort. User
           exceptions remain owned errors, even with the same SystemError text. */
        *out_aborted = negative_hint_message == NULL && collection_error == NULL;
        if (collection_error != NULL) {
            if (out_error != NULL) {
                *out_error = collection_error;
            }
            else {
                tinypy_error_release(collection_error);
            }
        }
        TINYPY_DECREF(items);
        return NULL;
    }
    return items;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_item_extended_size_error(tinypy_vm_t *vm, size_t replacement_size, size_t length, tinypy_error_t **out_error) {
    char replacement_buffer[TINYPY_MESSAGE_SIZE_BUFFER];
    char length_buffer[TINYPY_MESSAGE_SIZE_BUFFER];
    size_t replacement_digits = tinypy_internal_format_size(replacement_buffer, replacement_size);
    size_t length_digits = tinypy_internal_format_size(length_buffer, length);
    tinypy_message_part_t parts[] = {
        TINYPY_MESSAGE_PART_LITERAL("attempt to assign sequence of size "),
        {replacement_buffer, replacement_digits},
        TINYPY_MESSAGE_PART_LITERAL(" to extended slice of size "),
        {length_buffer, length_digits}
    };

    tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_VALUE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
}
//////////////////////////////////////////////////////////////////////////
/* list_ass_subscript compares the replacement with the slice length taken
   before collecting it; positions a shrunken list no longer holds are then
   recomputed rather than written. */
static tinypy_bool_t __tinypy_item_list_set_slice(tinypy_value_t *list, tinypy_value_t *slice, tinypy_value_t *value, tinypy_bool_t ignore_negative_hint, tinypy_bool_t legacy_slice, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(list);
    tinypy_internal_slice_indices_t indices;
    size_t replacement_size;
    tinypy_value_t *const *replacement_items;
    tinypy_bool_t replaced;

    if (tinypy_internal_slice_unpack(slice, &indices, out_error) == 0) {
        return TINYPY_FALSE;
    }
    /* Mapping slices normalize before collecting the replacement. Legacy
       sequence slices keep raw nonnegative offsets until collection ends. */
    if (legacy_slice == 0 && tinypy_internal_slice_adjust_indices(vm, TINYPY_LIST_SIZE(list), &indices, out_error) == 0) {
        return TINYPY_FALSE;
    }
    tinypy_bool_t aborted = TINYPY_FALSE;
    const char *negative_hint_message = ignore_negative_hint != 0 ? NULL : "error return without exception set";
    const char *type_message = indices.step == 1 ? "can only assign an iterable" : "must assign iterable to extended slice";
    tinypy_value_t *replacement = value == list
                                      ? tinypy_list_from_items(vm, TINYPY_LIST_OBJECT(list)->items, TINYPY_LIST_SIZE(list))
                                      : __tinypy_item_collect_iterable(value, type_message, negative_hint_message, &aborted, out_error);
    if (replacement == NULL) {
        return aborted;
    }
    replacement_size = TINYPY_LIST_SIZE(replacement);
    if (indices.step != 1 && replacement_size != indices.length) {
        __tinypy_item_extended_size_error(vm, replacement_size, indices.length, out_error);
        TINYPY_DECREF(replacement);
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
    replacement_items = TINYPY_LIST_OBJECT(replacement)->items;
    if (indices.step == 1) {
        replaced = tinypy_internal_list_replace_range_checked(list, (size_t)indices.start, indices.length, replacement_items, replacement_size, out_error);
    }
    else if (replacement_size != indices.length) {
        __tinypy_item_extended_size_error(vm, replacement_size, indices.length, out_error);
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
/* instance_subscript and its siblings look the method up as an attribute, so
   a classic instance without it raises its own AttributeError. */
static tinypy_value_t *__tinypy_item_call_attribute(tinypy_value_t *container, tinypy_value_t *name, tinypy_value_t *const *items, size_t item_count, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(container);
    tinypy_value_t *method = tinypy_internal_object_get_attr_key(container, name, out_error);

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
/* The sq_length slot behind a negative sequence offset: a classic instance
   always has it, and raises the AttributeError of a missing __len__. */
static int32_t __tinypy_item_sequence_length(tinypy_value_t *container, int64_t *out_length, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(container);
    tinypy_value_t *length_value;

    if (TINYPY_VALUE_KIND(container) == TINYPY_VALUE_OLD_INSTANCE) {
        length_value = __tinypy_item_call_attribute(container, vm->internal_special_length_key, NULL, 0U, out_error);
    }
    else if (tinypy_internal_object_has_special_key(container, vm->internal_special_length_key) != 0) {
        length_value = __tinypy_item_call_method(container, vm->internal_special_length_key, NULL, 0U, out_error);
    }
    else {
        return INT32_C(0);
    }
    if (length_value == NULL) {
        return INT32_C(-1);
    }
    tinypy_bool_t converted = tinypy_internal_index_as_i64(length_value, out_length, TINYPY_TRUE, out_error);

    TINYPY_DECREF(length_value);
    return converted != 0 ? INT32_C(1) : INT32_C(-1);
}
//////////////////////////////////////////////////////////////////////////
/* An exception still subscripted by the __getitem__ of BaseException. */
static tinypy_bool_t __tinypy_item_is_exception_sequence(tinypy_value_t *container) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(container);
    tinypy_type_t *base = vm->exception_types[TINYPY_EXCEPTION_BASE];

    if (TINYPY_VALUE_KIND(container) != TINYPY_VALUE_NATIVE_INSTANCE || tinypy_type_is_subtype(container->type, base) == 0) {
        return TINYPY_FALSE;
    }
    tinypy_value_t *method = tinypy_internal_type_lookup_key(vm, container->type, vm->internal_special_getitem_key);
    tinypy_value_t *base_method = tinypy_internal_type_lookup_key(vm, base, vm->internal_special_getitem_key);

    return method == base_method ? TINYPY_TRUE : TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
/* BaseException has only sq_item: PyObject_GetItem demands an index, adds
   the length of a class defining __len__ to a negative one, and the item is
   read from the arguments. */
static tinypy_value_t *__tinypy_item_exception_get(tinypy_value_t *container, tinypy_value_t *key, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(container);
    int64_t index;
    int64_t length;

    if (__tinypy_item_is_index(key) == 0) {
        __tinypy_item_sequence_index_error(vm, key, out_error);
        return NULL;
    }
    if (__tinypy_item_number_as_ssize(key, TINYPY_FALSE, TINYPY_ERROR_INDEX, &index, out_error) == 0) {
        return NULL;
    }
    if (index < 0) {
        int32_t measured = __tinypy_item_sequence_length(container, &length, out_error);

        if (measured < 0) {
            return NULL;
        }
        if (measured > 0) {
            index += length;
        }
    }
    tinypy_value_t *position = tinypy_integer_from_i64(vm, index);
    tinypy_value_t *result = __tinypy_item_call_method(container, vm->internal_special_getitem_key, &position, 1U, out_error);

    TINYPY_DECREF(position);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_item_get_missing(tinypy_value_t *container, tinypy_value_t *key, tinypy_bool_t dispatch_special, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(container);

    if (dispatch_special != 0 && TINYPY_VALUE_KIND(container) == TINYPY_VALUE_OLD_INSTANCE) {
        tinypy_value_t *result = __tinypy_item_call_attribute(container, vm->internal_special_getitem_key, &key, 1U, out_error);
        return result;
    }
    if (TINYPY_VALUE_KIND(container) == TINYPY_VALUE_XRANGE) {
        __tinypy_item_sequence_index_error(vm, key, out_error);
        return NULL;
    }
    __tinypy_item_unsupported(container, key, (tinypy_message_part_t)TINYPY_MESSAGE_PART_LITERAL("' object does not support indexing"), (tinypy_message_part_t)TINYPY_MESSAGE_PART_LITERAL("' object has no attribute '__getitem__'"), out_error);
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_get_item(tinypy_value_t *container, tinypy_value_t *key, tinypy_bool_t dispatch_special, tinypy_error_t **out_error) {
    tinypy_value_type_e kind;
    size_t index;
    tinypy_value_t *item;

    tinypy_vm_t *vm = TINYPY_VALUE_VM(container);
    TINYPY_CLEAR_ERROR(out_error);
    /* instance_subscript fetches a classic __getitem__ like any attribute. */
    if (dispatch_special != 0 && (TINYPY_VALUE_KIND(container) == TINYPY_VALUE_OLD_INSTANCE || __tinypy_internal_object_overrides_dispatch(container, TINYPY_INTERNAL_DISPATCH_BIT(GETITEM)) != 0)) {
        tinypy_value_t *return_value_1 = __tinypy_item_is_exception_sequence(container) != 0
                                             ? __tinypy_item_exception_get(container, key, out_error)
                                             : __tinypy_item_call_method(container, vm->internal_special_getitem_key, &key, 1U, out_error);
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
            tinypy_value_t *missing = __tinypy_item_get_missing(container, key, dispatch_special, out_error);
            return missing;
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
        if (__tinypy_item_position(container, key, "tuple index out of range", &index, out_error) == 0) {
            return NULL;
        }
        item = TINYPY_RET(TINYPY_TUPLE_GET(container, index));
        return item;
    }
    if (kind == TINYPY_VALUE_LIST) {
        if (__tinypy_item_position(container, key, "list index out of range", &index, out_error) == 0) {
            return NULL;
        }
        item = TINYPY_RET(TINYPY_LIST_GET(container, index));
        return item;
    }
    if (kind == TINYPY_VALUE_STRING) {
        if (__tinypy_item_position(container, key, "string index out of range", &index, out_error) == 0) {
            return NULL;
        }
        size_t size;
        const uint8_t *bytes = (const uint8_t *)tinypy_string_view(container, &size);
        tinypy_value_t *return_value_7 = tinypy_string_from_bytes(vm, bytes + index, 1U);
        return return_value_7;
    }
    if (kind == TINYPY_VALUE_UNICODE) {
        if (__tinypy_item_position(container, key, "string index out of range", &index, out_error) == 0) {
            return NULL;
        }
        tinypy_value_t *return_value_8 = __tinypy_item_unicode_get(container, index);
        return return_value_8;
    }
    if (kind == TINYPY_VALUE_XRANGE) {
        if (__tinypy_item_position(container, key, "xrange object index out of range", &index, out_error) == 0) {
            return NULL;
        }
        tinypy_value_t *return_value_9 = tinypy_integer_from_i64(vm, tinypy_internal_xrange_item_value(TINYPY_XRANGE_OBJECT(container), index));
        return return_value_9;
    }
    if (dispatch_special != 0 && tinypy_internal_object_has_special_key(container, vm->internal_special_getitem_key) != 0) {
        tinypy_value_t *return_value_10 = __tinypy_item_call_method(container, vm->internal_special_getitem_key, &key, 1U, out_error);
        return return_value_10;
    }
    tinypy_value_t *missing = __tinypy_item_get_missing(container, key, dispatch_special, out_error);
    return missing;
}
//////////////////////////////////////////////////////////////////////////
/* ISINDEX of ceval: an omitted bound or one with an index. */
static tinypy_bool_t __tinypy_item_is_slice_offset(tinypy_value_t *bound) {
    tinypy_bool_t offset = bound == NULL || __tinypy_item_is_index(bound) != 0 ? TINYPY_TRUE : TINYPY_FALSE;

    return offset;
}
//////////////////////////////////////////////////////////////////////////
/* apply_slice reads offsets like _PyEval_SliceIndex, and PySequence_GetSlice
   adds the length to a negative one. */
static tinypy_bool_t __tinypy_item_slice_offsets(tinypy_value_t *container, tinypy_value_t *start, tinypy_value_t *stop, int64_t *out_start, int64_t *out_stop, tinypy_error_t **out_error) {
    int64_t length;

    *out_start = 0;
    *out_stop = INT64_MAX;
    if (start != NULL && __tinypy_item_number_as_ssize(start, TINYPY_TRUE, TINYPY_ERROR_OVERFLOW, out_start, out_error) == 0) {
        return TINYPY_FALSE;
    }
    if (stop != NULL && __tinypy_item_number_as_ssize(stop, TINYPY_TRUE, TINYPY_ERROR_OVERFLOW, out_stop, out_error) == 0) {
        return TINYPY_FALSE;
    }
    if (*out_start >= 0 && *out_stop >= 0) {
        return TINYPY_TRUE;
    }
    int32_t measured = __tinypy_item_sequence_length(container, &length, out_error);

    if (measured <= 0) {
        return measured == 0 ? TINYPY_TRUE : TINYPY_FALSE;
    }
    if (*out_start < 0) {
        *out_start += length;
    }
    if (*out_stop < 0) {
        *out_stop += length;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_item_offsets_slice(tinypy_vm_t *vm, int64_t start, int64_t stop) {
    tinypy_value_t *start_value = tinypy_integer_from_i64(vm, start);
    tinypy_value_t *stop_value = tinypy_integer_from_i64(vm, stop);
    tinypy_value_t *slice = tinypy_slice_new(vm, start_value, stop_value, NULL);

    TINYPY_DECREF(stop_value);
    TINYPY_DECREF(start_value);
    return slice;
}
//////////////////////////////////////////////////////////////////////////
/* sq_slice and sq_ass_slice: __getslice__ and its siblings receive the
   offsets; a classic instance without them passes a slice of the offsets to
   __getitem__ and its siblings, as instance_slice does. */
static tinypy_value_t *__tinypy_item_call_offsets(tinypy_value_t *container, tinypy_value_t *slice_name, tinypy_value_t *item_name, tinypy_value_t *start, tinypy_value_t *stop, tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(container);
    tinypy_value_t *items[3];
    int64_t low;
    int64_t high;

    if (__tinypy_item_slice_offsets(container, start, stop, &low, &high, out_error) == 0) {
        return NULL;
    }
    /* instance_slice finds __getslice__ and its siblings like any attribute
       of a classic instance, __getattr__ included. */
    tinypy_value_t *method = NULL;
    if (TINYPY_VALUE_KIND(container) == TINYPY_VALUE_OLD_INSTANCE) {
        int32_t found = tinypy_internal_object_lookup_special_key(container, slice_name, &method, out_error);

        if (found < 0) {
            return NULL;
        }
    }
    if (TINYPY_VALUE_KIND(container) != TINYPY_VALUE_OLD_INSTANCE || method != NULL) {
        items[0] = tinypy_integer_from_i64(vm, low);
        items[1] = tinypy_integer_from_i64(vm, high);
        items[2] = value;
        size_t item_count = value != NULL ? 3U : 2U;
        tinypy_value_t *result;
        if (method != NULL) {
            tinypy_value_t *args = tinypy_tuple_from_items(vm, items, item_count);
            result = tinypy_call(method, args, NULL, out_error);
            TINYPY_DECREF(args);
            TINYPY_DECREF(method);
        }
        else {
            result = __tinypy_item_call_method(container, slice_name, items, item_count, out_error);
        }
        TINYPY_DECREF(items[1]);
        TINYPY_DECREF(items[0]);
        return result;
    }
    items[0] = __tinypy_item_offsets_slice(vm, low, high);
    items[1] = value;
    tinypy_value_t *result = __tinypy_item_call_attribute(container, item_name, items, value != NULL ? 2U : 1U, out_error);
    TINYPY_DECREF(items[0]);
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* Whether apply_slice and assign_slice use sq_slice and sq_ass_slice: both
   bounds must be offsets, and only classic instances and types with the
   legacy method have the slot, which subclasses keep even when they override
   __getitem__ and its siblings. A built-in type itself slices the same way
   without it. */
static tinypy_bool_t __tinypy_item_uses_offsets(tinypy_value_t *container, tinypy_value_t *slice_name, tinypy_value_t *start, tinypy_value_t *stop) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(container);
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(container);

    if (__tinypy_item_is_slice_offset(start) == 0 || __tinypy_item_is_slice_offset(stop) == 0) {
        return TINYPY_FALSE;
    }
    if (kind == TINYPY_VALUE_OLD_INSTANCE) {
        return TINYPY_TRUE;
    }
    if ((size_t)kind < TINYPY_BUILTIN_TYPE_COUNT && container->type == &vm->types[kind]) {
        return TINYPY_FALSE;
    }
    tinypy_bool_t slot = tinypy_internal_object_has_special_key(container, slice_name);
    return slot;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_get_slice(tinypy_value_t *container, tinypy_value_t *start, tinypy_value_t *stop, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(container);

    if (__tinypy_item_uses_offsets(container, vm->internal_special_getslice_key, start, stop) != 0) {
        tinypy_value_t *return_value_1 = __tinypy_item_call_offsets(container, vm->internal_special_getslice_key, vm->internal_special_getitem_key, start, stop, NULL, out_error);
        return return_value_1;
    }
    tinypy_value_t *slice = tinypy_slice_new(vm, start, stop, NULL);
    tinypy_value_t *result = tinypy_get_item(container, slice, out_error);
    TINYPY_DECREF(slice);
    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_set_slice(tinypy_value_t *container, tinypy_value_t *start, tinypy_value_t *stop, tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(container);
    tinypy_bool_t stored;

    /* list_ass_slice, also the slot of a list subclass that keeps
       __setslice__, clamps the offsets only after collecting the
       replacement, which may resize the list. */
    if (TINYPY_VALUE_KIND(container) == TINYPY_VALUE_LIST && __tinypy_item_is_slice_offset(start) != 0 && __tinypy_item_is_slice_offset(stop) != 0
        && tinypy_internal_object_has_special_override_key(container, vm->internal_special_setslice_key) == 0) {
        int64_t low;
        int64_t high;

        if (__tinypy_item_slice_offsets(container, start, stop, &low, &high, out_error) == 0) {
            return TINYPY_FALSE;
        }
        tinypy_value_t *slice = __tinypy_item_offsets_slice(vm, low < 0 ? 0 : low, high < 0 ? 0 : high);
        stored = __tinypy_item_list_set_slice(container, slice, value, TINYPY_FALSE, TINYPY_TRUE, out_error);
        TINYPY_DECREF(slice);
        return stored;
    }
    if (__tinypy_item_uses_offsets(container, vm->internal_special_setslice_key, start, stop) != 0) {
        tinypy_value_t *result = __tinypy_item_call_offsets(container, vm->internal_special_setslice_key, vm->internal_special_setitem_key, start, stop, value, out_error);

        if (result == NULL) {
            return TINYPY_FALSE;
        }
        TINYPY_DECREF(result);
        return TINYPY_TRUE;
    }
    tinypy_value_t *slice = tinypy_slice_new(vm, start, stop, NULL);
    stored = tinypy_set_item(container, slice, value, out_error);
    TINYPY_DECREF(slice);
    return stored;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_delete_slice(tinypy_value_t *container, tinypy_value_t *start, tinypy_value_t *stop, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(container);

    if (__tinypy_item_uses_offsets(container, vm->internal_special_delslice_key, start, stop) != 0) {
        tinypy_value_t *result = __tinypy_item_call_offsets(container, vm->internal_special_delslice_key, vm->internal_special_delitem_key, start, stop, NULL, out_error);

        if (result == NULL) {
            return TINYPY_FALSE;
        }
        TINYPY_DECREF(result);
        return TINYPY_TRUE;
    }
    tinypy_value_t *slice = tinypy_slice_new(vm, start, stop, NULL);
    tinypy_bool_t deleted = tinypy_delete_item(container, slice, out_error);
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
static tinypy_bool_t __tinypy_set_item(tinypy_value_t *container, tinypy_value_t *key, tinypy_value_t *value, tinypy_bool_t dispatch_special, tinypy_bool_t legacy_slice, tinypy_error_t **out_error) {
    tinypy_value_type_e kind;
    size_t index;

    tinypy_vm_t *vm = TINYPY_VALUE_VM(container);
    TINYPY_CLEAR_ERROR(out_error);
    if (dispatch_special != 0 && (TINYPY_VALUE_KIND(container) == TINYPY_VALUE_OLD_INSTANCE || __tinypy_internal_object_overrides_dispatch(container, TINYPY_INTERNAL_DISPATCH_BIT(SETITEM)) != 0)) {
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
            tinypy_bool_t return_value_3 = __tinypy_item_list_set_slice(container, key, value, dispatch_special == 0, legacy_slice, out_error);
            return return_value_3;
        }
        if (__tinypy_item_position(container, key, "list assignment index out of range", &index, out_error) == 0) {
            return TINYPY_FALSE;
        }
        tinypy_list_set(container, index, value);
        return TINYPY_TRUE;
    }
    tinypy_value_t *items[2] = {key, value};
    if (dispatch_special != 0 && (kind == TINYPY_VALUE_OLD_INSTANCE || tinypy_internal_object_has_special_key(container, vm->internal_special_setitem_key) != 0)) {
        tinypy_value_t *result = kind == TINYPY_VALUE_OLD_INSTANCE
                                     ? __tinypy_item_call_attribute(container, vm->internal_special_setitem_key, items, 2U, out_error)
                                     : __tinypy_item_call_method(container, vm->internal_special_setitem_key, items, 2U, out_error);

        if (result == NULL) {
            return TINYPY_FALSE;
        }
        TINYPY_DECREF(result);
        return TINYPY_TRUE;
    }
    if (dispatch_special != 0 && __tinypy_item_missing_pair_method(container, vm->internal_special_setitem_key, vm->internal_special_delitem_key, out_error) != 0) {
        return TINYPY_FALSE;
    }
    __tinypy_item_unsupported(container, key, (tinypy_message_part_t)TINYPY_MESSAGE_PART_LITERAL("' object does not support item assignment"), (tinypy_message_part_t)TINYPY_MESSAGE_PART_LITERAL("' object does not support item assignment"), out_error);
    return TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_set_item_builtin(tinypy_value_t *container, tinypy_value_t *key, tinypy_value_t *value, tinypy_bool_t legacy_slice, tinypy_error_t **out_error) {
    tinypy_bool_t result = __tinypy_set_item(container, key, value, TINYPY_FALSE, legacy_slice, out_error);

    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_set_item(tinypy_value_t *container, tinypy_value_t *key, tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_bool_t result = __tinypy_set_item(container, key, value, TINYPY_TRUE, TINYPY_FALSE, out_error);

    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_delete_item(tinypy_value_t *container, tinypy_value_t *key, tinypy_bool_t dispatch_special, tinypy_error_t **out_error) {
    tinypy_value_type_e kind;
    size_t index;

    tinypy_vm_t *vm = TINYPY_VALUE_VM(container);
    TINYPY_CLEAR_ERROR(out_error);
    if (dispatch_special != 0 && (TINYPY_VALUE_KIND(container) == TINYPY_VALUE_OLD_INSTANCE || __tinypy_internal_object_overrides_dispatch(container, TINYPY_INTERNAL_DISPATCH_BIT(DELITEM)) != 0)) {
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
        if (__tinypy_item_position(container, key, "list assignment index out of range", &index, out_error) == 0) {
            return TINYPY_FALSE;
        }
        tinypy_list_delete(container, index);
        return TINYPY_TRUE;
    }
    if (dispatch_special != 0 && (kind == TINYPY_VALUE_OLD_INSTANCE || tinypy_internal_object_has_special_key(container, vm->internal_special_delitem_key) != 0)) {
        tinypy_value_t *result = kind == TINYPY_VALUE_OLD_INSTANCE
                                     ? __tinypy_item_call_attribute(container, vm->internal_special_delitem_key, &key, 1U, out_error)
                                     : __tinypy_item_call_method(container, vm->internal_special_delitem_key, &key, 1U, out_error);

        if (result == NULL) {
            return TINYPY_FALSE;
        }
        TINYPY_DECREF(result);
        return TINYPY_TRUE;
    }
    if (dispatch_special != 0 && __tinypy_item_missing_pair_method(container, vm->internal_special_delitem_key, vm->internal_special_setitem_key, out_error) != 0) {
        return TINYPY_FALSE;
    }
    __tinypy_item_unsupported(container, key, (tinypy_message_part_t)TINYPY_MESSAGE_PART_LITERAL("' object doesn't support item deletion"), (tinypy_message_part_t)TINYPY_MESSAGE_PART_LITERAL("' object does not support item deletion"), out_error);
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
