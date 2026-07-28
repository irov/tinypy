#include "tinypy/iterator.h"

#include "internal.h"

//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_iterator_new(tinypy_value_t *iterable) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(iterable);
    tinypy_iterator_object_t *iterator = (tinypy_iterator_object_t *)tinypy_internal_value_allocate(vm, TINYPY_VALUE_ITERATOR, sizeof(*iterator));

    iterator->iterable = iterable;
    if (TINYPY_VALUE_KIND(iterable) == TINYPY_VALUE_LIST) {
        iterator->expected_state = TINYPY_LIST_OBJECT(iterable)->mutation_version;
    }
    else if (TINYPY_VALUE_KIND(iterable) == TINYPY_VALUE_DICT) {
        iterator->expected_state = (uint64_t)TINYPY_DICT_OBJECT(iterable)->used;
    }
    TINYPY_INCREF(iterable);
    return &iterator->base;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_dict_iterator_new(tinypy_value_t *dict, int32_t mode) {
    tinypy_iterator_object_t *iterator = TINYPY_ITERATOR_OBJECT(__tinypy_internal_iterator_new(dict));
    iterator->mode = mode;
    return &iterator->base;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_iterator_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data) {
    visit(TINYPY_ITERATOR_OBJECT(value)->iterable, user_data);
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_iterator_iter(tinypy_value_t *value, tinypy_error_t **out_error) {
    TINYPY_CLEAR_ERROR(out_error);
    TINYPY_INCREF(value);
    return value;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_iterator_next_sequence(tinypy_iterator_object_t *iterator) {
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(iterator->iterable);
    size_t size = kind == TINYPY_VALUE_TUPLE ? TINYPY_TUPLE_SIZE(iterator->iterable) : TINYPY_LIST_SIZE(iterator->iterable);

    if (iterator->index == size) {
        return NULL;
    }
    tinypy_value_t *item = kind == TINYPY_VALUE_TUPLE ? TINYPY_TUPLE_GET(iterator->iterable, iterator->index) : TINYPY_LIST_GET(iterator->iterable, iterator->index);
    iterator->index += 1U;
    TINYPY_INCREF(item);
    return item;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_iterator_next_string(tinypy_iterator_object_t *iterator) {
    const uint8_t *bytes;
    size_t size;

    bytes = TINYPY_VALUE_KIND(iterator->iterable) == TINYPY_VALUE_BUFFER
                ? (const uint8_t *)tinypy_buffer_view(iterator->iterable, &size)
                : (const uint8_t *)tinypy_string_view(iterator->iterable, &size);
    if (iterator->index == size) {
        return NULL;
    }
    iterator->index += 1U;
    tinypy_vm_t *vm = TINYPY_VALUE_VM(iterator->iterable);
    tinypy_value_t *return_value_1 = tinypy_string_from_bytes(vm, bytes + iterator->index - 1U, 1U);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_iterator_next_unicode(tinypy_iterator_object_t *iterator) {
    const char *utf8;
    size_t byte_size;
    size_t code_point_count;
    size_t byte_index = 0U;
    size_t scalar_index = 0U;
    size_t scalar_size;

    utf8 = tinypy_unicode_utf8_view(iterator->iterable, &byte_size, &code_point_count);
    if (iterator->index == code_point_count) {
        return NULL;
    }
    while (scalar_index < iterator->index) {
        uint8_t lead = (uint8_t)utf8[byte_index];

        byte_index += lead < 0x80U ? 1U : (lead < 0xe0U ? 2U : (lead < 0xf0U ? 3U : 4U));
        ++scalar_index;
    }
    uint8_t lead = (uint8_t)utf8[byte_index];

    scalar_size = lead < 0x80U ? 1U : (lead < 0xe0U ? 2U : (lead < 0xf0U ? 3U : 4U));
    iterator->index += 1U;
    tinypy_vm_t *vm = TINYPY_VALUE_VM(iterator->iterable);
    tinypy_value_t *return_value_1 = tinypy_unicode_from_utf8(vm, utf8 + byte_index, scalar_size);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_iterator_next_bytearray(tinypy_iterator_object_t *iterator) {
    size_t size;
    const uint8_t *bytes = (const uint8_t *)tinypy_bytearray_view(iterator->iterable, &size);

    if (iterator->index == size) {
        return NULL;
    }
    iterator->index += 1U;
    tinypy_vm_t *vm = TINYPY_VALUE_VM(iterator->iterable);
    tinypy_value_t *return_value_1 = tinypy_integer_from_i64(vm, (int64_t)bytes[iterator->index - 1U]);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_iterator_next_dict(tinypy_iterator_object_t *iterator, tinypy_error_t **out_error) {
    tinypy_dict_object_t *dict = TINYPY_DICT_OBJECT(iterator->iterable);

    if ((uint64_t)dict->used != iterator->expected_state) {
        tinypy_vm_t *vm = TINYPY_VALUE_VM(iterator->iterable);
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_RUNTIME, "dictionary changed size during iteration", out_error);
        return NULL;
    }
    while (iterator->table_position <= dict->mask) {
        tinypy_dict_entry_t *entry = &dict->table[iterator->table_position];

        iterator->table_position += 1U;
        if (entry->state == TINYPY_DICT_ENTRY_ACTIVE) {
            tinypy_value_t *items[2];

            iterator->index += 1U;
            if (iterator->mode == INT32_C(1)) {
                TINYPY_INCREF(entry->value);
                return entry->value;
            }
            if (iterator->mode == INT32_C(2)) {
                items[0] = entry->key;
                items[1] = entry->value;
                tinypy_vm_t *vm = TINYPY_VALUE_VM(iterator->iterable);
                tinypy_value_t *return_value_1 = tinypy_tuple_from_items(vm, items, 2U);
                return return_value_1;
            }
            TINYPY_INCREF(entry->key);
            return entry->key;
        }
    }
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_iterator_next(tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_value_t * function_result;
    tinypy_iterator_object_t *iterator = TINYPY_ITERATOR_OBJECT(value);
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(iterator->iterable);

    TINYPY_CLEAR_ERROR(out_error);
    if (iterator->mode == INT32_C(3)) {
        tinypy_value_t *result;

        if (iterator->remaining == 0U) {
            return NULL;
        }
        tinypy_vm_t *vm_2 = TINYPY_VALUE_VM(value);
        result = tinypy_integer_from_i64(vm_2, iterator->current);
        iterator->remaining -= 1U;
        if (iterator->remaining != 0U) {
            iterator->current += iterator->step;
        }
        return result;
    }
    if (iterator->mode == INT32_C(4)) {
        tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
        tinypy_value_t *key = tinypy_integer_from_i64(vm, (int64_t)iterator->index);
        tinypy_error_t *item_error = NULL;
        tinypy_value_t *result = tinypy_get_item(iterator->iterable, key, &item_error);

        TINYPY_DECREF(key);
        if (result != NULL) {
            iterator->index += 1U;
            return result;
        }
        if (vm->raised_value != NULL && tinypy_type_is_subtype(vm->raised_value->type, vm->exception_types[TINYPY_EXCEPTION_INDEX_ERROR]) != 0) {
            if (item_error != NULL) {
                tinypy_error_release(item_error);
            }
            tinypy_internal_exception_clear_raised(vm);
            return NULL;
        }
        if (out_error != NULL) {
            *out_error = item_error;
        }
        else if (item_error != NULL) {
            tinypy_error_release(item_error);
        }
        return NULL;
    }
    if (kind == TINYPY_VALUE_LIST && TINYPY_LIST_OBJECT(iterator->iterable)->mutation_version != iterator->expected_state) {
        tinypy_vm_t *vm_2 = TINYPY_VALUE_VM(value);
        tinypy_internal_make_vm_error(vm_2, TINYPY_ERROR_RUNTIME, "list changed size during iteration", out_error);
        return NULL;
    }
    switch (kind) {
    case TINYPY_VALUE_TUPLE:
    case TINYPY_VALUE_LIST:
        function_result = __tinypy_internal_iterator_next_sequence(iterator);
        return function_result;
    case TINYPY_VALUE_STRING:
    case TINYPY_VALUE_BUFFER:
        function_result = __tinypy_internal_iterator_next_string(iterator);
        return function_result;
    case TINYPY_VALUE_BYTEARRAY:
        function_result = __tinypy_internal_iterator_next_bytearray(iterator);
        return function_result;
    case TINYPY_VALUE_UNICODE:
        function_result = __tinypy_internal_iterator_next_unicode(iterator);
        return function_result;
    case TINYPY_VALUE_DICT:
        function_result = __tinypy_internal_iterator_next_dict(iterator, out_error);
        return function_result;
    default:
        return NULL;
    }
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_xrange_new(tinypy_vm_t *vm, int64_t start, int64_t step, size_t length) {
    tinypy_xrange_object_t *range = (tinypy_xrange_object_t *)tinypy_internal_value_allocate(vm, TINYPY_VALUE_XRANGE, sizeof(*range));

    range->start = start;
    range->step = step;
    range->length = length;
    return &range->base;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_iterator_integer(tinypy_vm_t *vm, tinypy_value_t *value, int64_t *out_value, tinypy_error_t **out_error) {
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);

    if (kind == TINYPY_VALUE_BOOL || kind == TINYPY_VALUE_INTEGER) {
        *out_value = TINYPY_INTEGER_VALUE(value);
        return TINYPY_TRUE;
    }
    if (kind == TINYPY_VALUE_LONG && TINYPY_LONG_DIGIT_COUNT(value) <= 5U) {
        uint64_t magnitude = UINT64_C(0);
        size_t index = TINYPY_LONG_DIGIT_COUNT(value);

        while (index != 0U) {
            index -= 1U;
            if (magnitude > (UINT64_MAX >> 15U)) {
                break;
            }
            magnitude = (magnitude << 15U) | TINYPY_LONG_OBJECT(value)->digits[index];
        }
        if (index == 0U && TINYPY_LONG_SIGN(value) >= 0 && magnitude <= (uint64_t)INT64_MAX) {
            *out_value = (int64_t)magnitude;
            return TINYPY_TRUE;
        }
        if (index == 0U && TINYPY_LONG_SIGN(value) < 0 && magnitude <= (uint64_t)INT64_MAX + UINT64_C(1)) {
            *out_value = magnitude == (uint64_t)INT64_MAX + UINT64_C(1) ? INT64_MIN : -(int64_t)magnitude;
            return TINYPY_TRUE;
        }
    }
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "integer argument required", out_error);
    return TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_xrange_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = type->vm;
    size_t argument_count = TINYPY_TUPLE_SIZE(args);
    int64_t start;
    int64_t stop;
    int64_t step;
    uint64_t distance;
    uint64_t step_magnitude;
    uint64_t length;

    if ((kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U) || argument_count < 1U || argument_count > 3U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "xrange received invalid arguments", out_error);
        return NULL;
    }
    if (argument_count == 1U) {
        start = 0;
        tinypy_value_t *item_3 = TINYPY_TUPLE_GET(args, 0U);
        if (__tinypy_iterator_integer(vm, item_3, &stop, out_error) == 0) {
            return NULL;
        }
        step = 1;
    }
    else {
        tinypy_value_t *item = TINYPY_TUPLE_GET(args, 0U);
        tinypy_bool_t condition = __tinypy_iterator_integer(vm, item, &start, out_error) == 0;
        if (condition == 0) {
            tinypy_value_t *item_2 = TINYPY_TUPLE_GET(args, 1U);
            condition = __tinypy_iterator_integer(vm, item_2, &stop, out_error) == 0;
        }
        if (condition) {
            return NULL;
        }
        if (argument_count == 3U) {
            tinypy_value_t *item_3 = TINYPY_TUPLE_GET(args, 2U);
            if (__tinypy_iterator_integer(vm, item_3, &step, out_error) == 0) {
                return NULL;
            }
        }
        else {
            step = 1;
        }
    }
    if (step == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "xrange step cannot be zero", out_error);
        return NULL;
    }
    if ((step > 0 && start >= stop) || (step < 0 && start <= stop)) {
        tinypy_value_t *return_value_1 = tinypy_internal_xrange_new(vm, start, step, 0U);
        return return_value_1;
    }
    if (step > 0) {
        distance = (uint64_t)stop - (uint64_t)start;
        step_magnitude = (uint64_t)step;
    }
    else {
        distance = (uint64_t)start - (uint64_t)stop;
        step_magnitude = (uint64_t)(-(step + INT64_C(1))) + UINT64_C(1);
    }
    length = (distance - UINT64_C(1)) / step_magnitude + UINT64_C(1);
    if (length > (uint64_t)SIZE_MAX) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "xrange has too many items", out_error);
        return NULL;
    }
    tinypy_value_t *return_value_2 = tinypy_internal_xrange_new(vm, start, step, (size_t)length);
    return return_value_2;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_xrange_iter(tinypy_value_t *value, tinypy_error_t **out_error) {
    TINYPY_CLEAR_ERROR(out_error);
    tinypy_iterator_object_t *iterator = TINYPY_ITERATOR_OBJECT(__tinypy_internal_iterator_new(value));
    iterator->mode = INT32_C(3);
    iterator->current = TINYPY_XRANGE_OBJECT(value)->start;
    iterator->step = TINYPY_XRANGE_OBJECT(value)->step;
    iterator->remaining = TINYPY_XRANGE_OBJECT(value)->length;
    return &iterator->base;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_enumerate_new(tinypy_value_t *iterable, int64_t start, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(iterable);
    tinypy_value_t *iterator = tinypy_iter(iterable, out_error);

    if (iterator == NULL) {
        return NULL;
    }
    tinypy_enumerate_object_t *enumerate = (tinypy_enumerate_object_t *)tinypy_internal_value_allocate(vm, TINYPY_VALUE_ENUMERATE, sizeof(*enumerate));
    enumerate->iterator = iterator;
    enumerate->index = start;
    return &enumerate->base;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_enumerate_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data) {
    visit(TINYPY_ENUMERATE_OBJECT(value)->iterator, user_data);
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_enumerate_iter(tinypy_value_t *value, tinypy_error_t **out_error) {
    TINYPY_CLEAR_ERROR(out_error);
    TINYPY_INCREF(value);
    return value;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_enumerate_next(tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_enumerate_object_t *enumerate = TINYPY_ENUMERATE_OBJECT(value);
    tinypy_value_t *item = tinypy_next(enumerate->iterator, out_error);
    tinypy_value_t *items[2];

    if (item == NULL) {
        return NULL;
    }
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_value_t *index = tinypy_integer_from_i64(vm, enumerate->index);
    items[0] = index;
    items[1] = item;
    tinypy_value_t *result = tinypy_tuple_from_items(vm, items, 2U);
    TINYPY_DECREF(index);
    TINYPY_DECREF(item);
    enumerate->index += 1;
    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_reversed_new(tinypy_value_t *sequence, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(sequence);
    size_t size;

    switch (TINYPY_VALUE_KIND(sequence)) {
    case TINYPY_VALUE_LIST:
        size = TINYPY_LIST_SIZE(sequence);
        break;
    case TINYPY_VALUE_TUPLE:
        size = TINYPY_TUPLE_SIZE(sequence);
        break;
    case TINYPY_VALUE_STRING:
        size = TINYPY_TEXT_BYTE_SIZE(sequence);
        break;
    case TINYPY_VALUE_UNICODE:
        size = TINYPY_SIZED_SIZE(sequence);
        break;
    case TINYPY_VALUE_XRANGE:
        size = TINYPY_XRANGE_OBJECT(sequence)->length;
        break;
    default:
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "reversed argument must be a sequence", out_error);
        return NULL;
    }
    tinypy_reversed_object_t *reversed = (tinypy_reversed_object_t *)tinypy_internal_value_allocate(vm, TINYPY_VALUE_REVERSED, sizeof(*reversed));
    reversed->sequence = sequence;
    reversed->index = size;
    TINYPY_INCREF(sequence);
    return &reversed->base;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_reversed_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data) {
    visit(TINYPY_REVERSED_OBJECT(value)->sequence, user_data);
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_reversed_iter(tinypy_value_t *value, tinypy_error_t **out_error) {
    TINYPY_CLEAR_ERROR(out_error);
    TINYPY_INCREF(value);
    return value;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_reversed_next(tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_reversed_object_t *reversed = TINYPY_REVERSED_OBJECT(value);

    if (reversed->index == 0U) {
        return NULL;
    }
    reversed->index -= 1U;
    if (TINYPY_VALUE_KIND(reversed->sequence) == TINYPY_VALUE_XRANGE) {
        tinypy_xrange_object_t *range = TINYPY_XRANGE_OBJECT(reversed->sequence);

        tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
        tinypy_value_t *return_value_1 = tinypy_integer_from_i64(vm, range->start + (int64_t)reversed->index * range->step);
        return return_value_1;
    }
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_value_t *key = tinypy_integer_from_i64(vm, (int64_t)reversed->index);
    tinypy_value_t *result = tinypy_get_item(reversed->sequence, key, out_error);
    TINYPY_DECREF(key);
    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_iter(tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_value_type_e kind;

    TINYPY_CLEAR_ERROR(out_error);
    if (value->type->iter != NULL) {
        tinypy_value_t *return_value_1 = value->type->iter(value, out_error);
        return return_value_1;
    }
    kind = TINYPY_VALUE_KIND(value);
    if (kind == TINYPY_VALUE_TUPLE || kind == TINYPY_VALUE_LIST || kind == TINYPY_VALUE_STRING || kind == TINYPY_VALUE_UNICODE || kind == TINYPY_VALUE_DICT || kind == TINYPY_VALUE_BUFFER || kind == TINYPY_VALUE_BYTEARRAY) {
        tinypy_value_t *return_value_2 = __tinypy_internal_iterator_new(value);
        return return_value_2;
    }
    if (tinypy_internal_object_has_special(value, "__iter__", 8U) != 0) {
        tinypy_value_t *method = tinypy_object_get_attr(value, "__iter__", 8U, out_error);
        tinypy_value_t *args;
        tinypy_value_t *result;

        if (method == NULL) {
            return NULL;
        }
        tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
        args = tinypy_tuple_from_items(vm, NULL, 0U);
        result = tinypy_call(method, args, NULL, out_error);
        TINYPY_DECREF(args);
        TINYPY_DECREF(method);
        if (result != NULL && result->type->next == NULL && tinypy_internal_object_has_special(result, "next", 4U) == 0) {
            TINYPY_DECREF(result);
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "__iter__ returned a non-iterator", out_error);
            return NULL;
        }
        return result;
    }
    if (tinypy_internal_object_has_special(value, "__getitem__", 11U) != 0) {
        tinypy_value_t *iterator = __tinypy_internal_iterator_new(value);

        TINYPY_ITERATOR_OBJECT(iterator)->mode = INT32_C(4);
        return iterator;
    }
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "object is not iterable", out_error);
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_next(tinypy_value_t *iterator, tinypy_error_t **out_error) {
    TINYPY_CLEAR_ERROR(out_error);
    if (iterator->type->next == NULL) {
        if (tinypy_internal_object_has_special(iterator, "next", 4U) != 0) {
            tinypy_value_t *method = tinypy_object_get_attr(iterator, "next", 4U, out_error);
            tinypy_value_t *args;
            tinypy_value_t *result;

            if (method == NULL) {
                return NULL;
            }
            tinypy_vm_t *vm = TINYPY_VALUE_VM(iterator);
            args = tinypy_tuple_from_items(vm, NULL, 0U);
            result = tinypy_call(method, args, NULL, out_error);
            TINYPY_DECREF(args);
            TINYPY_DECREF(method);
            if (result == NULL) {
                (void)tinypy_internal_exception_consume_stop_iteration(vm, out_error);
            }
            return result;
        }
        tinypy_vm_t *vm = TINYPY_VALUE_VM(iterator);
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "object is not an iterator", out_error);
        return NULL;
    }
    tinypy_value_t *return_value_1 = iterator->type->next(iterator, out_error);
    return return_value_1;
}
