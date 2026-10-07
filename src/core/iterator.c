#include "tinypy/iterator.h"

#include "internal.h"

static tinypy_value_t *__tinypy_enumerate_new(tinypy_type_t *type, tinypy_value_t *iterable, tinypy_value_t *start, tinypy_error_t **out_error);
static tinypy_value_t *__tinypy_reversed_new(tinypy_type_t *type, tinypy_value_t *sequence, tinypy_error_t **out_error);

//////////////////////////////////////////////////////////////////////////
void tinypy_internal_iterator_clear(tinypy_iterator_object_t *iterator) {
    tinypy_value_t *iterable = iterator->iterable;
    tinypy_value_t *sentinel = iterator->sentinel;

    iterator->iterable = NULL;
    iterator->sentinel = NULL;
    iterator->mode = INT32_C(-1);
    if (iterable != NULL) {
        TINYPY_DECREF(iterable);
    }
    if (sentinel != NULL) {
        TINYPY_DECREF(sentinel);
    }
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_iterator_new_with_type(tinypy_value_t *iterable, tinypy_type_t *type) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(iterable);
    tinypy_iterator_object_t *iterator = (tinypy_iterator_object_t *)tinypy_internal_object_allocate(vm, type, sizeof(*iterator));

    iterator->iterable = iterable;
    iterator->sentinel = NULL;
    if (TINYPY_VALUE_KIND(iterable) == TINYPY_VALUE_DICT) {
        iterator->expected_state = (uint64_t)TINYPY_DICT_OBJECT(iterable)->used;
    }
    TINYPY_INCREF(iterable);
    return &iterator->base;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_iterator_new(tinypy_value_t *iterable) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(iterable);
    tinypy_type_t *type = &vm->types[TINYPY_VALUE_ITERATOR];

    switch (TINYPY_VALUE_KIND(iterable)) {
    case TINYPY_VALUE_LIST:
        type = vm->iterator_types[TINYPY_ITERATOR_TYPE_LIST];
        break;
    case TINYPY_VALUE_TUPLE:
        type = vm->iterator_types[TINYPY_ITERATOR_TYPE_TUPLE];
        break;
    case TINYPY_VALUE_DICT:
        type = vm->iterator_types[TINYPY_ITERATOR_TYPE_DICT_KEY];
        break;
    default:
        break;
    }

    tinypy_value_t *return_value_1 = __tinypy_internal_iterator_new_with_type(iterable, type);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_call_iterator_new(tinypy_value_t *callable, tinypy_value_t *sentinel, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(callable);
    tinypy_iterator_object_t *iterator;

    if (callable->type->call == NULL && tinypy_internal_object_has_special(callable, "__call__", 8U) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "iter(v, w): v must be callable", out_error);
        return NULL;
    }
    iterator = TINYPY_ITERATOR_OBJECT(__tinypy_internal_iterator_new_with_type(callable, vm->iterator_types[TINYPY_ITERATOR_TYPE_CALLABLE]));
    iterator->mode = INT32_C(5);
    iterator->sentinel = sentinel;
    TINYPY_INCREF(sentinel);
    return &iterator->base;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_dict_iterator_new(tinypy_value_t *dict, int32_t mode) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(dict);
    size_t type_index = mode >= INT32_C(0) && mode <= INT32_C(2)
                            ? (size_t)TINYPY_ITERATOR_TYPE_DICT_KEY + (size_t)mode
                            : (size_t)TINYPY_ITERATOR_TYPE_DICT_KEY;
    tinypy_iterator_object_t *iterator = TINYPY_ITERATOR_OBJECT(__tinypy_internal_iterator_new_with_type(dict, vm->iterator_types[type_index]));

    iterator->mode = mode;
    return &iterator->base;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_set_iterator_new(tinypy_value_t *value) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_value_t *dict = TINYPY_SET_OBJECT(value)->dict;
    tinypy_iterator_object_t *iterator = TINYPY_ITERATOR_OBJECT(__tinypy_internal_iterator_new_with_type(dict, vm->iterator_types[TINYPY_ITERATOR_TYPE_SET]));

    iterator->mode = INT32_C(0);
    return &iterator->base;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_formatter_iterator_new(tinypy_value_t *text, int32_t mode) {
    tinypy_iterator_object_t *iterator = TINYPY_ITERATOR_OBJECT(__tinypy_internal_iterator_new(text));

    iterator->mode = mode;
    return &iterator->base;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_iterator_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data) {
    tinypy_iterator_object_t *iterator = TINYPY_ITERATOR_OBJECT(value);

    if (iterator->iterable != NULL) {
        visit(iterator->iterable, user_data);
    }
    if (iterator->sentinel != NULL) {
        visit(iterator->sentinel, user_data);
    }
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_iterator_iter(tinypy_value_t *value, tinypy_error_t **out_error) {
    TINYPY_CLEAR_ERROR(out_error);
    TINYPY_INCREF(value);
    return value;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_iterator_next_list_value(tinypy_iterator_object_t *iterator) {
    size_t size = TINYPY_LIST_SIZE(iterator->iterable);

    if (iterator->index >= size) {
        tinypy_internal_iterator_clear(iterator);
        return NULL;
    }
    tinypy_value_t *item = TINYPY_LIST_GET(iterator->iterable, iterator->index);
    iterator->index += 1U;
    TINYPY_INCREF(item);
    return item;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_iterator_next_tuple_value(tinypy_iterator_object_t *iterator) {
    size_t size = TINYPY_TUPLE_SIZE(iterator->iterable);

    if (iterator->index >= size) {
        tinypy_internal_iterator_clear(iterator);
        return NULL;
    }
    tinypy_value_t *item = TINYPY_TUPLE_GET(iterator->iterable, iterator->index);
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
    if (iterator->index >= size) {
        tinypy_internal_iterator_clear(iterator);
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
    size_t byte_index;
    size_t scalar_size;

    utf8 = tinypy_unicode_utf8_view(iterator->iterable, &byte_size, &code_point_count);
    if (iterator->index >= code_point_count) {
        tinypy_internal_iterator_clear(iterator);
        return NULL;
    }
    byte_index = iterator->table_position;
    uint8_t lead = (uint8_t)utf8[byte_index];

    scalar_size = lead < 0x80U ? 1U : (lead < 0xe0U ? 2U : (lead < 0xf0U ? 3U : 4U));
    iterator->table_position += scalar_size;
    iterator->index += 1U;
    tinypy_vm_t *vm = TINYPY_VALUE_VM(iterator->iterable);
    tinypy_value_t *return_value_1 = tinypy_unicode_from_utf8(vm, utf8 + byte_index, scalar_size);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_iterator_next_bytearray(tinypy_iterator_object_t *iterator) {
    size_t size;
    const uint8_t *bytes = (const uint8_t *)tinypy_bytearray_view(iterator->iterable, &size);

    if (iterator->index >= size) {
        tinypy_internal_iterator_clear(iterator);
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
        iterator->expected_state = UINT64_MAX;
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_RUNTIME, "dictionary changed size during iteration", out_error);
        return NULL;
    }
    while (iterator->table_position <= dict->mask) {
        tinypy_dict_entry_t *entry = &dict->table[iterator->table_position];

        iterator->table_position += 1U;
        if (TINYPY_DICT_ENTRY_IS_ACTIVE(entry)) {
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
    tinypy_internal_iterator_clear(iterator);
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_list_iterator_next(tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_iterator_object_t *iterator = TINYPY_ITERATOR_OBJECT(value);

    TINYPY_CLEAR_ERROR(out_error);
    tinypy_value_t *return_value_1 = iterator->iterable != NULL ? __tinypy_internal_iterator_next_list_value(iterator) : NULL;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_tuple_iterator_next(tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_iterator_object_t *iterator = TINYPY_ITERATOR_OBJECT(value);

    TINYPY_CLEAR_ERROR(out_error);
    tinypy_value_t *return_value_1 = iterator->iterable != NULL ? __tinypy_internal_iterator_next_tuple_value(iterator) : NULL;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_dict_iterator_next(tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_iterator_object_t *iterator = TINYPY_ITERATOR_OBJECT(value);

    TINYPY_CLEAR_ERROR(out_error);
    tinypy_value_t *return_value_1 = iterator->iterable != NULL ? __tinypy_internal_iterator_next_dict(iterator, out_error) : NULL;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_set_iterator_next(tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_iterator_object_t *iterator = TINYPY_ITERATOR_OBJECT(value);

    TINYPY_CLEAR_ERROR(out_error);
    if (iterator->iterable == NULL) {
        return NULL;
    }
    if ((uint64_t)TINYPY_DICT_OBJECT(iterator->iterable)->used != iterator->expected_state) {
        iterator->expected_state = UINT64_MAX;
        tinypy_internal_make_vm_error(TINYPY_VALUE_VM(value), TINYPY_ERROR_RUNTIME, "set changed size during iteration", out_error);
        return NULL;
    }
    tinypy_value_t *return_value_1 = __tinypy_internal_iterator_next_dict(iterator, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_range_iterator_next(tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_iterator_object_t *iterator = TINYPY_ITERATOR_OBJECT(value);
    tinypy_value_t *result;

    TINYPY_CLEAR_ERROR(out_error);
    if (iterator->remaining == 0U) {
        iterator->mode = INT32_C(-1);
        return NULL;
    }
    result = tinypy_integer_from_i64(TINYPY_VALUE_VM(value), iterator->current);
    iterator->remaining -= 1U;
    if (iterator->remaining != 0U) {
        uint64_t bits = (uint64_t)iterator->current + (uint64_t)iterator->step;

        iterator->current = bits <= (uint64_t)INT64_MAX ? (int64_t)bits : -(int64_t)(~bits) - INT64_C(1);
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_iterator_next(tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_value_t * function_result;
    tinypy_iterator_object_t *iterator = TINYPY_ITERATOR_OBJECT(value);
    tinypy_value_type_e kind;

    TINYPY_CLEAR_ERROR(out_error);
    if (iterator->mode == INT32_C(-1) || (iterator->mode != INT32_C(3) && iterator->iterable == NULL)) {
        return NULL;
    }
    if (iterator->mode == INT32_C(3)) {
        tinypy_value_t *result;

        if (iterator->remaining == 0U) {
            iterator->mode = INT32_C(-1);
            return NULL;
        }
        tinypy_vm_t *vm_2 = TINYPY_VALUE_VM(value);
        result = tinypy_integer_from_i64(vm_2, iterator->current);
        iterator->remaining -= 1U;
        if (iterator->remaining != 0U) {
            uint64_t bits = (uint64_t)iterator->current + (uint64_t)iterator->step;

            iterator->current = bits <= (uint64_t)INT64_MAX ? (int64_t)bits : -(int64_t)(~bits) - INT64_C(1);
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
        if (tinypy_internal_exception_consume_stop_iteration(vm, &item_error) != 0 || (vm->raised_value != NULL && tinypy_type_is_subtype(vm->raised_value->type, vm->exception_types[TINYPY_EXCEPTION_INDEX_ERROR]) != 0)) {
            if (item_error != NULL) {
                tinypy_error_release(item_error);
            }
            if (vm->raised_value != NULL) {
                tinypy_internal_exception_clear_raised(vm);
            }
            tinypy_internal_iterator_clear(iterator);
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
    if (iterator->mode == INT32_C(5)) {
        tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
        tinypy_value_t *args = tinypy_tuple_from_items(vm, NULL, 0U);
        tinypy_error_t *call_error = NULL;
        tinypy_value_t *result = tinypy_call(iterator->iterable, args, NULL, &call_error);
        int32_t equal;

        TINYPY_DECREF(args);
        if (result == NULL) {
            if (tinypy_internal_exception_consume_stop_iteration(vm, &call_error) != 0) {
                tinypy_internal_iterator_clear(iterator);
                return NULL;
            }
            if (out_error != NULL) {
                *out_error = call_error;
            }
            else if (call_error != NULL) {
                tinypy_error_release(call_error);
            }
            return NULL;
        }
        equal = tinypy_compare_bool(result, iterator->sentinel, TINYPY_COMPARE_EQUAL, out_error);
        if (equal < 0) {
            TINYPY_DECREF(result);
            return NULL;
        }
        if (equal != 0) {
            TINYPY_DECREF(result);
            tinypy_internal_iterator_clear(iterator);
            return NULL;
        }
        return result;
    }
    if (iterator->mode == INT32_C(6)) {
        function_result = tinypy_internal_string_formatter_parser_next(iterator, out_error);
        return function_result;
    }
    if (iterator->mode == INT32_C(7)) {
        function_result = tinypy_internal_string_formatter_field_next(iterator, out_error);
        return function_result;
    }
    kind = TINYPY_VALUE_KIND(iterator->iterable);
    switch (kind) {
    case TINYPY_VALUE_LIST:
        function_result = __tinypy_internal_iterator_next_list_value(iterator);
        return function_result;
    case TINYPY_VALUE_TUPLE:
        function_result = __tinypy_internal_iterator_next_tuple_value(iterator);
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
int64_t tinypy_internal_xrange_item_value(const tinypy_xrange_object_t *range, size_t index) {
    uint64_t bits = (uint64_t)range->start + (uint64_t)range->step * (uint64_t)index;
    uint64_t magnitude;

    if (bits <= (uint64_t)INT64_MAX) {
        return (int64_t)bits;
    }
    magnitude = (~bits) + UINT64_C(1);
    return magnitude == (uint64_t)INT64_MAX + UINT64_C(1) ? INT64_MIN : -(int64_t)magnitude;
}
//////////////////////////////////////////////////////////////////////////
int64_t tinypy_internal_xrange_stop_value(const tinypy_xrange_object_t *range) {
    int64_t last;

    if (range->length == 0U) {
        return range->start;
    }
    last = tinypy_internal_xrange_item_value(range, range->length - 1U);
    if (range->step > 0 && last > INT64_MAX - range->step) {
        return INT64_MAX;
    }
    if (range->step < 0 && last < INT64_MIN - range->step) {
        return INT64_MIN;
    }
    return last + range->step;
}
//////////////////////////////////////////////////////////////////////////
size_t tinypy_internal_iterator_size_hint(const tinypy_iterator_object_t *iterator) {
    size_t size;

    if (iterator->mode == INT32_C(-1)) {
        return 0U;
    }
    if (iterator->mode == INT32_C(3)) {
        return iterator->remaining;
    }
    if (iterator->mode == INT32_C(5) || iterator->mode == INT32_C(6) || iterator->mode == INT32_C(7) || iterator->iterable == NULL) {
        return 0U;
    }
    if (TINYPY_VALUE_KIND(iterator->iterable) == TINYPY_VALUE_DICT && (uint64_t)TINYPY_DICT_SIZE(iterator->iterable) != iterator->expected_state) {
        return 0U;
    }
    size = tinypy_internal_iterable_size_hint(iterator->iterable);
    return iterator->index < size ? size - iterator->index : 0U;
}
//////////////////////////////////////////////////////////////////////////
size_t tinypy_internal_iterable_size_hint(const tinypy_value_t *value) {
    size_t size;

    switch (TINYPY_VALUE_KIND(value)) {
    case TINYPY_VALUE_STRING:
    case TINYPY_VALUE_UNICODE:
    case TINYPY_VALUE_TUPLE:
    case TINYPY_VALUE_LIST:
    case TINYPY_VALUE_BYTEARRAY:
        size = TINYPY_SIZED_SIZE(value);
        break;
    case TINYPY_VALUE_DICT:
        size = TINYPY_DICT_SIZE(value);
        break;
    case TINYPY_VALUE_SET:
    case TINYPY_VALUE_FROZENSET:
        size = TINYPY_DICT_SIZE(TINYPY_SET_OBJECT(value)->dict);
        break;
    case TINYPY_VALUE_XRANGE:
        size = TINYPY_XRANGE_OBJECT(value)->length;
        break;
    case TINYPY_VALUE_BUFFER:
        (void)tinypy_buffer_view(value, &size);
        break;
    case TINYPY_VALUE_ITERATOR:
        size = tinypy_internal_iterator_size_hint(TINYPY_ITERATOR_OBJECT((tinypy_value_t *)value));
        break;
    case TINYPY_VALUE_ENUMERATE:
        size = tinypy_internal_iterable_size_hint(TINYPY_ENUMERATE_OBJECT((tinypy_value_t *)value)->iterator);
        break;
    case TINYPY_VALUE_REVERSED:
        size = TINYPY_REVERSED_OBJECT((tinypy_value_t *)value)->index;
        break;
    default:
        size = 0U;
        break;
    }
    return size;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_length_hint_consume_fallback_error(tinypy_vm_t *vm, tinypy_error_t **out_error) {
    if (tinypy_internal_exception_consume_kind(vm, TINYPY_EXCEPTION_TYPE_ERROR, out_error) != 0) {
        return TINYPY_TRUE;
    }
    tinypy_bool_t result = tinypy_internal_exception_consume_kind(vm, TINYPY_EXCEPTION_ATTRIBUTE_ERROR, out_error);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_length_hint_call(tinypy_value_t *value, const char *name, size_t name_size, tinypy_bool_t *out_called, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_value_t *method = tinypy_internal_object_get_special(value, name, name_size, out_error);

    if (out_called != NULL) {
        *out_called = TINYPY_FALSE;
    }
    if (method == NULL) {
        return NULL;
    }
    if (out_called != NULL) {
        *out_called = TINYPY_TRUE;
    }
    tinypy_value_t *empty = tinypy_tuple_from_items(vm, NULL, 0U);
    tinypy_value_t *result = tinypy_call(method, empty, NULL, out_error);

    TINYPY_DECREF(empty);
    TINYPY_DECREF(method);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_length_hint_builtin_length(tinypy_value_t *value, int64_t *out_length) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);
    size_t size;

    if ((size_t)kind >= TINYPY_BUILTIN_TYPE_COUNT || value->type != &vm->types[kind]) {
        return TINYPY_FALSE;
    }
    switch (kind) {
    case TINYPY_VALUE_STRING:
    case TINYPY_VALUE_TUPLE:
    case TINYPY_VALUE_LIST:
    case TINYPY_VALUE_BYTEARRAY:
        size = TINYPY_SIZED_SIZE(value);
        break;
    case TINYPY_VALUE_UNICODE: {
        size_t byte_size;

        (void)tinypy_unicode_utf8_view(value, &byte_size, &size);
        break;
    }
    case TINYPY_VALUE_DICT:
        size = TINYPY_DICT_SIZE(value);
        break;
    case TINYPY_VALUE_SET:
    case TINYPY_VALUE_FROZENSET:
        size = TINYPY_DICT_SIZE(TINYPY_SET_OBJECT(value)->dict);
        break;
    case TINYPY_VALUE_XRANGE:
        size = TINYPY_XRANGE_OBJECT(value)->length;
        break;
    case TINYPY_VALUE_BUFFER:
        (void)tinypy_buffer_view(value, &size);
        break;
    default:
        return TINYPY_FALSE;
    }
    if (size > (size_t)INT64_MAX) {
        return TINYPY_FALSE;
    }
    *out_length = (int64_t)size;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_length_hint_result(tinypy_value_t *result, int64_t default_hint, int64_t *out_hint, tinypy_error_t **out_error) {
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(result);
    int64_t hint;

    if (kind == TINYPY_VALUE_BOOL || kind == TINYPY_VALUE_INTEGER || kind == TINYPY_VALUE_LONG) {
        if (tinypy_internal_index_as_i64(result, &hint, TINYPY_FALSE, out_error) == 0) {
            return TINYPY_FALSE;
        }
    }
    else if (result->type->number_slots != NULL || kind == TINYPY_VALUE_OLD_INSTANCE || tinypy_internal_object_has_special(result, "__int__", 7U) != 0 || tinypy_internal_object_has_special(result, "__float__", 9U) != 0) {
        if (tinypy_internal_number_as_ssize(result, &hint, out_error) == 0) {
            return TINYPY_FALSE;
        }
    }
    else {
        *out_hint = default_hint;
        return TINYPY_TRUE;
    }
    *out_hint = hint;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_length_hint_length_result(tinypy_value_t *value, tinypy_value_t *result, int64_t *out_length, tinypy_error_t **out_error) {
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(result);

    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_OLD_INSTANCE && kind != TINYPY_VALUE_BOOL && kind != TINYPY_VALUE_INTEGER) {
        tinypy_internal_make_vm_error(TINYPY_VALUE_VM(value), TINYPY_ERROR_TYPE, "__len__ should return an int", out_error);
        return TINYPY_FALSE;
    }
    tinypy_bool_t converted = tinypy_internal_number_as_ssize(result, out_length, out_error);
    return converted;
}
//////////////////////////////////////////////////////////////////////////
/* Python 2's _PyObject_LengthHint is observable: it invokes __len__ first,
   suppresses only TypeError/AttributeError, and then tries __length_hint__.
   Keep this checked path separate from the structural hint used internally. */
tinypy_bool_t tinypy_internal_length_hint(tinypy_value_t *value, int64_t default_hint, int64_t *out_hint, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_value_t *result = NULL;
    tinypy_length_slot_t length_slot;
    int64_t length;

    if (tinypy_internal_object_has_special_override(value, "__len__", 7U) != 0) {
        result = __tinypy_length_hint_call(value, "__len__", 7U, NULL, out_error);
        if (result == NULL) {
            if (__tinypy_length_hint_consume_fallback_error(vm, out_error) == 0) {
                return TINYPY_FALSE;
            }
            goto length_unavailable;
        }
        if (__tinypy_length_hint_length_result(value, result, &length, out_error) == 0) {
            TINYPY_DECREF(result);
            if (__tinypy_length_hint_consume_fallback_error(vm, out_error) == 0) {
                return TINYPY_FALSE;
            }
            goto length_unavailable;
        }
        TINYPY_DECREF(result);
        if (length < 0) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "__len__ returned a negative value", out_error);
            return TINYPY_FALSE;
        }
        *out_hint = length;
        return TINYPY_TRUE;
    }
    if (__tinypy_length_hint_builtin_length(value, &length) != 0) {
        *out_hint = length;
        return TINYPY_TRUE;
    }
    length_slot = value->type->mapping_slots != NULL && value->type->mapping_slots->length != NULL
                      ? value->type->mapping_slots->length
                      : (value->type->sequence_slots != NULL ? value->type->sequence_slots->length : NULL);
    if (length_slot != NULL) {
        ptrdiff_t slot_length = length_slot(value, out_error);

        if (slot_length >= 0) {
            *out_hint = (int64_t)slot_length;
            return TINYPY_TRUE;
        }
        if (tinypy_vm_has_error(vm) == 0 && (out_error == NULL || *out_error == NULL)) {
            goto length_unavailable;
        }
        if (__tinypy_length_hint_consume_fallback_error(vm, out_error) == 0) {
            return TINYPY_FALSE;
        }
    }
    else if (tinypy_internal_object_has_special(value, "__len__", 7U) != 0) {
        result = __tinypy_length_hint_call(value, "__len__", 7U, NULL, out_error);
        if (result == NULL) {
            if (__tinypy_length_hint_consume_fallback_error(vm, out_error) == 0) {
                return TINYPY_FALSE;
            }
            goto length_unavailable;
        }
        if (__tinypy_length_hint_length_result(value, result, &length, out_error) == 0) {
            TINYPY_DECREF(result);
            if (__tinypy_length_hint_consume_fallback_error(vm, out_error) == 0) {
                return TINYPY_FALSE;
            }
            goto length_unavailable;
        }
        TINYPY_DECREF(result);
        if (length < 0) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "__len__ returned a negative value", out_error);
            return TINYPY_FALSE;
        }
        *out_hint = length;
        return TINYPY_TRUE;
    }

length_unavailable:
    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_OLD_INSTANCE || tinypy_internal_object_has_special(value, "__length_hint__", 15U) == 0) {
        *out_hint = default_hint;
        return TINYPY_TRUE;
    }
    tinypy_bool_t called;
    result = __tinypy_length_hint_call(value, "__length_hint__", 15U, &called, out_error);
    if (result == NULL) {
        if (called != 0 && __tinypy_length_hint_consume_fallback_error(vm, out_error) != 0) {
            *out_hint = default_hint;
            return TINYPY_TRUE;
        }
        return TINYPY_FALSE;
    }
    tinypy_bool_t converted = __tinypy_length_hint_result(result, default_hint, out_hint, out_error);

    TINYPY_DECREF(result);
    return converted;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_iterator_integer(tinypy_vm_t *vm, tinypy_value_t *value, int64_t *out_value, tinypy_error_t **out_error) {
    (void)vm;
    tinypy_bool_t return_value_1 = tinypy_internal_integer_as_ssize(value, out_value, out_error);
    return return_value_1;
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
        if (__tinypy_iterator_integer(vm, item, &start, out_error) == 0) {
            return NULL;
        }
        tinypy_value_t *item_2 = TINYPY_TUPLE_GET(args, 1U);
        if (__tinypy_iterator_integer(vm, item_2, &stop, out_error) == 0) {
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
    if (length > (uint64_t)PTRDIFF_MAX) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "xrange has too many items", out_error);
        return NULL;
    }
    tinypy_value_t *return_value_2 = tinypy_internal_xrange_new(vm, start, step, (size_t)length);
    return return_value_2;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_enumerate_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    static const char *const names[2] = {"sequence", "start"};
    static const size_t name_sizes[2] = {8U, 5U};
    tinypy_vm_t *vm = type->vm;
    size_t positional_count = TINYPY_TUPLE_SIZE(args);
    size_t keyword_count = kwargs != NULL ? TINYPY_DICT_SIZE(kwargs) : 0U;
    size_t recognized_keyword_count = 0U;
    tinypy_value_t *values[2] = {NULL, NULL};

    if (positional_count > 2U || keyword_count > 2U - positional_count) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "enumerate received too many positional arguments", out_error);
        return NULL;
    }
    for (size_t parameter = 0U; parameter < 2U; ++parameter) {
        tinypy_value_t *keyword = NULL;

        values[parameter] = parameter < positional_count ? TINYPY_TUPLE_GET(args, parameter) : NULL;
        if (recognized_keyword_count < keyword_count) {
            keyword = tinypy_internal_constructor_keyword_optional(vm, kwargs, names[parameter], name_sizes[parameter]);
        }
        if (keyword != NULL) {
            recognized_keyword_count += 1U;
            if (values[parameter] != NULL) {
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "enumerate received duplicate arguments", out_error);
                return NULL;
            }
            values[parameter] = keyword;
        }
        if (parameter == 0U && values[parameter] == NULL) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "enumerate is missing the sequence argument", out_error);
            return NULL;
        }
    }
    if (recognized_keyword_count != keyword_count) {
        for (size_t slot = 0U; slot <= TINYPY_DICT_OBJECT(kwargs)->mask; ++slot) {
            tinypy_dict_entry_t *entry = &TINYPY_DICT_OBJECT(kwargs)->table[slot];
            tinypy_bool_t known = TINYPY_FALSE;

            if (!TINYPY_DICT_ENTRY_IS_ACTIVE(entry)) {
                continue;
            }
            tinypy_value_type_e kind = TINYPY_VALUE_KIND(entry->key);
            if (kind == TINYPY_VALUE_STRING || kind == TINYPY_VALUE_UNICODE) {
                for (size_t parameter = 0U; parameter < 2U; ++parameter) {
                    if (TINYPY_TEXT_BYTE_SIZE(entry->key) == name_sizes[parameter] && memcmp(TINYPY_TEXT_BYTES(entry->key), names[parameter], name_sizes[parameter]) == 0) {
                        known = TINYPY_TRUE;
                        break;
                    }
                }
            }
            if (known == 0) {
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "enumerate received an unexpected keyword argument", out_error);
                return NULL;
            }
        }
    }
    tinypy_value_t *iterable = values[0];
    tinypy_value_t *start = values[1];
    tinypy_value_t *counter = start != NULL ? tinypy_internal_index_value(start, out_error) : tinypy_integer_from_i64(vm, INT64_C(0));

    if (counter == NULL) {
        return NULL;
    }
    tinypy_internal_exception_state_t exception_state;
    tinypy_error_t *conversion_error = NULL;
    int64_t index;

    tinypy_internal_exception_preserve_begin(vm, &exception_state);
    tinypy_bool_t fits = tinypy_internal_index_as_i64(counter, &index, TINYPY_FALSE, &conversion_error);
    if (conversion_error != NULL) {
        tinypy_error_release(conversion_error);
    }
    tinypy_internal_exception_preserve_end(vm, &exception_state);
    if (fits != 0) {
        TINYPY_DECREF(counter);
        counter = tinypy_integer_from_i64(vm, index);
    }
    tinypy_value_t *result = __tinypy_enumerate_new(type, iterable, counter, out_error);
    TINYPY_DECREF(counter);
    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_reversed_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = type->vm;

    if ((type == &vm->types[TINYPY_VALUE_REVERSED] && kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U) || TINYPY_TUPLE_SIZE(args) != 1U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "reversed received invalid arguments", out_error);
        return NULL;
    }
    tinypy_value_t *sequence = TINYPY_TUPLE_GET(args, 0U);
    if (tinypy_internal_object_has_special(sequence, "__reversed__", 12U) != 0) {
        tinypy_value_t *method = tinypy_internal_object_get_special(sequence, "__reversed__", 12U, out_error);

        if (method == NULL) {
            return NULL;
        }
        tinypy_value_t *empty = tinypy_tuple_from_items(vm, NULL, 0U);
        tinypy_value_t *result = tinypy_call(method, empty, NULL, out_error);
        TINYPY_DECREF(empty);
        TINYPY_DECREF(method);
        return result;
    }
    tinypy_value_t *return_value = __tinypy_reversed_new(type, sequence, out_error);
    return return_value;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_xrange_iter(tinypy_value_t *value, tinypy_error_t **out_error) {
    TINYPY_CLEAR_ERROR(out_error);
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_iterator_object_t *iterator = TINYPY_ITERATOR_OBJECT(__tinypy_internal_iterator_new_with_type(value, vm->iterator_types[TINYPY_ITERATOR_TYPE_RANGE]));
    iterator->mode = INT32_C(3);
    iterator->current = TINYPY_XRANGE_OBJECT(value)->start;
    iterator->step = TINYPY_XRANGE_OBJECT(value)->step;
    iterator->remaining = TINYPY_XRANGE_OBJECT(value)->length;
    TINYPY_DECREF(iterator->iterable);
    iterator->iterable = NULL;
    return &iterator->base;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_enumerate_new(tinypy_type_t *type, tinypy_value_t *iterable, tinypy_value_t *start, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(iterable);
    tinypy_value_t *iterator = tinypy_iter(iterable, out_error);

    if (iterator == NULL) {
        return NULL;
    }
    tinypy_enumerate_object_t *enumerate = (tinypy_enumerate_object_t *)tinypy_internal_object_allocate(vm, type, type->basic_size);
    enumerate->iterator = iterator;
    enumerate->index = start;
    TINYPY_INCREF(start);
    return &enumerate->base;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_enumerate_new(tinypy_value_t *iterable, tinypy_value_t *start, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(iterable);
    tinypy_value_t *return_value = __tinypy_enumerate_new(&vm->types[TINYPY_VALUE_ENUMERATE], iterable, start, out_error);
    return return_value;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_enumerate_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data) {
    visit(TINYPY_ENUMERATE_OBJECT(value)->iterator, user_data);
    visit(TINYPY_ENUMERATE_OBJECT(value)->index, user_data);
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
    tinypy_value_t *next_index;

    if (TINYPY_VALUE_KIND(enumerate->index) == TINYPY_VALUE_INTEGER && TINYPY_INTEGER_VALUE(enumerate->index) != INT64_MAX) {
        next_index = tinypy_integer_from_i64(vm, TINYPY_INTEGER_VALUE(enumerate->index) + INT64_C(1));
    }
    else {
        tinypy_value_t *one = tinypy_integer_from_i64(vm, INT64_C(1));

        next_index = tinypy_add(enumerate->index, one, out_error);
        TINYPY_DECREF(one);
    }
    if (next_index == NULL) {
        TINYPY_DECREF(item);
        return NULL;
    }
    items[0] = enumerate->index;
    items[1] = item;
    tinypy_value_t *result = tinypy_tuple_from_items(vm, items, 2U);
    TINYPY_DECREF(item);
    TINYPY_DECREF(enumerate->index);
    enumerate->index = next_index;
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_reversed_sequence_size(tinypy_value_t *sequence, size_t *out_size, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(sequence);
    tinypy_length_slot_t length_slot = sequence->type->sequence_slots != NULL ? sequence->type->sequence_slots->length : NULL;
    tinypy_bool_t has_get_item = sequence->type->sequence_slots != NULL && sequence->type->sequence_slots->get_item != NULL
                               ? TINYPY_TRUE
                               : tinypy_internal_object_has_special(sequence, "__getitem__", 11U);

    if (has_get_item == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "reversed argument must be a sequence", out_error);
        return TINYPY_FALSE;
    }
    if (length_slot != NULL) {
        ptrdiff_t length = length_slot(sequence, out_error);

        if (length < 0) {
            if (out_error == NULL || *out_error == NULL) {
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "length slot returned a negative value", out_error);
            }
            return TINYPY_FALSE;
        }
        *out_size = (size_t)length;
        return TINYPY_TRUE;
    }
    if (tinypy_internal_object_has_special(sequence, "__len__", 7U) != 0) {
        tinypy_value_t *method = tinypy_internal_object_get_special(sequence, "__len__", 7U, out_error);
        tinypy_value_t *empty;
        tinypy_value_t *length_value;
        int64_t length;

        if (method == NULL) {
            return TINYPY_FALSE;
        }
        empty = tinypy_tuple_from_items(vm, NULL, 0U);
        length_value = tinypy_call(method, empty, NULL, out_error);
        TINYPY_DECREF(empty);
        TINYPY_DECREF(method);
        if (length_value == NULL) {
            return TINYPY_FALSE;
        }
        if (__tinypy_length_hint_length_result(sequence, length_value, &length, out_error) == 0) {
            TINYPY_DECREF(length_value);
            return TINYPY_FALSE;
        }
        TINYPY_DECREF(length_value);
        if (length < 0) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "__len__ returned a negative value", out_error);
            return TINYPY_FALSE;
        }
        *out_size = (size_t)length;
        return TINYPY_TRUE;
    }
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "reversed argument must be a sequence", out_error);
    return TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_reversed_new(tinypy_type_t *type, tinypy_value_t *sequence, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(sequence);
    size_t size;

    /* PySequence_Check excludes dictionaries outright and otherwise demands an
       item protocol, so mappings never reach the index-counting path. */
    if (tinypy_internal_memoryview_check(sequence) != 0 || TINYPY_VALUE_KIND(sequence) == TINYPY_VALUE_DICT || tinypy_internal_object_has_special(sequence, "__getitem__", 11U) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "argument to reversed() must be a sequence", out_error);
        return NULL;
    }
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(sequence);
    if (kind != TINYPY_VALUE_LIST && ((size_t)kind >= TINYPY_BUILTIN_TYPE_COUNT || sequence->type != &vm->types[kind])) {
        if (__tinypy_reversed_sequence_size(sequence, &size, out_error) == 0) {
            return NULL;
        }
    }
    else if (kind == TINYPY_VALUE_LIST || kind == TINYPY_VALUE_TUPLE) {
        size = TINYPY_SIZED_SIZE(sequence);
    }
    else if (TINYPY_VALUE_KIND(sequence) == TINYPY_VALUE_STRING) {
        size = TINYPY_TEXT_BYTE_SIZE(sequence);
    }
    else if (TINYPY_VALUE_KIND(sequence) == TINYPY_VALUE_UNICODE) {
        size = TINYPY_SIZED_SIZE(sequence);
    }
    else if (TINYPY_VALUE_KIND(sequence) == TINYPY_VALUE_XRANGE) {
        size = TINYPY_XRANGE_OBJECT(sequence)->length;
    }
    else if (__tinypy_reversed_sequence_size(sequence, &size, out_error) == 0) {
        return NULL;
    }
    tinypy_reversed_object_t *reversed = (tinypy_reversed_object_t *)tinypy_internal_object_allocate(vm, type, type->basic_size);
    reversed->sequence = sequence;
    reversed->index = size;
    TINYPY_INCREF(sequence);
    return &reversed->base;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_reversed_new(tinypy_value_t *sequence, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(sequence);
    tinypy_type_t *type = TINYPY_VALUE_KIND(sequence) == TINYPY_VALUE_LIST ? vm->iterator_types[TINYPY_ITERATOR_TYPE_LIST_REVERSE] : &vm->types[TINYPY_VALUE_REVERSED];
    tinypy_value_t *return_value = __tinypy_reversed_new(type, sequence, out_error);
    return return_value;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_reversed_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data) {
    tinypy_value_t *sequence = TINYPY_REVERSED_OBJECT(value)->sequence;

    if (sequence != NULL) {
        visit(sequence, user_data);
    }
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_reversed_iter(tinypy_value_t *value, tinypy_error_t **out_error) {
    TINYPY_CLEAR_ERROR(out_error);
    TINYPY_INCREF(value);
    return value;
}
//////////////////////////////////////////////////////////////////////////
size_t tinypy_internal_reversed_size_hint(tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_reversed_object_t *reversed = TINYPY_REVERSED_OBJECT(value);
    size_t size;

    TINYPY_CLEAR_ERROR(out_error);
    if (reversed->sequence == NULL) {
        return 0U;
    }
    if (TINYPY_VALUE_KIND(reversed->sequence) == TINYPY_VALUE_LIST) {
        size = TINYPY_LIST_SIZE(reversed->sequence);
    }
    else if (__tinypy_reversed_sequence_size(reversed->sequence, &size, out_error) == 0) {
        return 0U;
    }
    return size < reversed->index ? 0U : reversed->index;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_reversed_next(tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_reversed_object_t *reversed = TINYPY_REVERSED_OBJECT(value);

    if (reversed->sequence == NULL) {
        return NULL;
    }
    if (reversed->index == 0U) {
        TINYPY_DECREF(reversed->sequence);
        reversed->sequence = NULL;
        return NULL;
    }
    reversed->index -= 1U;
    if (TINYPY_VALUE_KIND(reversed->sequence) == TINYPY_VALUE_LIST) {
        if (reversed->index >= TINYPY_LIST_SIZE(reversed->sequence)) {
            TINYPY_DECREF(reversed->sequence);
            reversed->sequence = NULL;
            reversed->index = 0U;
            return NULL;
        }
        tinypy_value_t *result = TINYPY_LIST_GET(reversed->sequence, reversed->index);

        TINYPY_INCREF(result);
        return result;
    }
    if (TINYPY_VALUE_KIND(reversed->sequence) == TINYPY_VALUE_XRANGE) {
        tinypy_xrange_object_t *range = TINYPY_XRANGE_OBJECT(reversed->sequence);

        tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
        tinypy_value_t *return_value_1 = tinypy_integer_from_i64(vm, tinypy_internal_xrange_item_value(range, reversed->index));
        return return_value_1;
    }
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_value_t *key = tinypy_integer_from_i64(vm, (int64_t)reversed->index);
    tinypy_error_t *item_error = NULL;
    tinypy_value_t *result = tinypy_get_item(reversed->sequence, key, &item_error);
    TINYPY_DECREF(key);
    if (result == NULL && (tinypy_internal_exception_consume_stop_iteration(vm, &item_error) != 0 || (vm->raised_value != NULL && tinypy_type_is_subtype(vm->raised_value->type, vm->exception_types[TINYPY_EXCEPTION_INDEX_ERROR]) != 0))) {
        reversed->index = 0U;
        if (item_error != NULL) {
            tinypy_error_release(item_error);
        }
        if (vm->raised_value != NULL) {
            tinypy_internal_exception_clear_raised(vm);
        }
        TINYPY_DECREF(reversed->sequence);
        reversed->sequence = NULL;
        return NULL;
    }
    if (result == NULL) {
        reversed->index = 0U;
        TINYPY_DECREF(reversed->sequence);
        reversed->sequence = NULL;
        if (out_error != NULL) {
            *out_error = item_error;
        }
        else if (item_error != NULL) {
            tinypy_error_release(item_error);
        }
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_iterator_method_arguments(tinypy_vm_t *vm, tinypy_value_t *args, tinypy_value_t *kwargs, size_t count, tinypy_error_t **out_error) {
    if ((kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U) || TINYPY_TUPLE_SIZE(args) != count) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "iterator method received invalid arguments", out_error);
        return TINYPY_FALSE;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_xrange_method_arguments(tinypy_vm_t *vm, tinypy_value_t *args, tinypy_value_t *kwargs, size_t count, tinypy_error_t **out_error) {
    if (__tinypy_iterator_method_arguments(vm, args, kwargs, count, out_error) == 0) {
        return TINYPY_FALSE;
    }
    if (TINYPY_VALUE_KIND(TINYPY_TUPLE_GET(args, 0U)) != TINYPY_VALUE_XRANGE) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "xrange method requires an xrange object", out_error);
        return TINYPY_FALSE;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_xrange_iter_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_xrange_method_arguments(vm, args, kwargs, 1U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *return_value = tinypy_internal_xrange_iter(TINYPY_TUPLE_GET(args, 0U), out_error);
    return return_value;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_xrange_getitem_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_xrange_method_arguments(vm, args, kwargs, 2U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *return_value = tinypy_internal_get_item_builtin(TINYPY_TUPLE_GET(args, 0U), TINYPY_TUPLE_GET(args, 1U), out_error);
    return return_value;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_xrange_len_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_xrange_method_arguments(vm, args, kwargs, 1U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *self = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *return_value = tinypy_integer_from_i64(vm, (int64_t)TINYPY_XRANGE_OBJECT(self)->length);
    return return_value;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_xrange_reversed_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_xrange_method_arguments(vm, args, kwargs, 1U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *value = TINYPY_TUPLE_GET(args, 0U);
    tinypy_xrange_object_t *range = TINYPY_XRANGE_OBJECT(value);
    tinypy_iterator_object_t *iterator = TINYPY_ITERATOR_OBJECT(tinypy_internal_xrange_iter(value, out_error));

    iterator->current = range->length != 0U ? tinypy_internal_xrange_item_value(range, range->length - 1U) : range->start;
    iterator->step = range->step == INT64_MIN ? INT64_MIN : -range->step;
    return &iterator->base;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_xrange_repr_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_xrange_method_arguments(vm, args, kwargs, 1U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *return_value = tinypy_object_repr(TINYPY_TUPLE_GET(args, 0U), out_error);
    return return_value;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_iterator_type_set(tinypy_vm_t *vm, tinypy_type_t *type, const char *name, size_t name_size, tinypy_native_function_callback_t callback) {
    tinypy_value_t *function = tinypy_native_function_new(vm, name, name_size, callback, NULL, NULL);

    tinypy_type_set_attr(type, name, name_size, function);
    TINYPY_DECREF(function);
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_initialize_iterator_types(tinypy_vm_t *vm) {
    static const char *const names[TINYPY_ITERATOR_TYPE_COUNT] = {
        "listiterator",
        "tupleiterator",
        "dictionary-keyiterator",
        "dictionary-valueiterator",
        "dictionary-itemiterator",
        "setiterator",
        "rangeiterator",
        "callable-iterator",
        "listreverseiterator"};
    static const size_t name_sizes[TINYPY_ITERATOR_TYPE_COUNT] = {
        12U, 13U, 22U, 24U, 23U, 11U, 13U, 17U, 19U};
    static tinypy_next_slot_t const next_slots[TINYPY_ITERATOR_TYPE_COUNT] = {
        __tinypy_internal_list_iterator_next,
        __tinypy_internal_tuple_iterator_next,
        __tinypy_internal_dict_iterator_next,
        __tinypy_internal_dict_iterator_next,
        __tinypy_internal_dict_iterator_next,
        __tinypy_internal_set_iterator_next,
        __tinypy_internal_range_iterator_next,
        tinypy_internal_iterator_next,
        tinypy_internal_reversed_next};
    tinypy_type_t *xrange_type = &vm->types[TINYPY_VALUE_XRANGE];
    size_t index;

    for (index = 0U; index < TINYPY_ITERATOR_TYPE_COUNT; ++index) {
        tinypy_type_t *type = tinypy_internal_type_new_configured(vm, names[index], name_sizes[index], NULL, 0U, NULL, NULL, TINYPY_FALSE, TINYPY_FALSE, NULL);

        tinypy_bool_t reversed_list = index == (size_t)TINYPY_ITERATOR_TYPE_LIST_REVERSE ? TINYPY_TRUE : TINYPY_FALSE;

        type->layout_kind = reversed_list != 0 ? TINYPY_VALUE_REVERSED : TINYPY_VALUE_ITERATOR;
        type->basic_size = reversed_list != 0 ? sizeof(tinypy_reversed_object_t) : sizeof(tinypy_iterator_object_t);
        type->slots_offset = 0U;
        type->dict_offset = 0U;
        type->weakref_offset = 0U;
        type->has_instance_dict = INT32_C(0);
        type->release_references = reversed_list != 0 ? tinypy_internal_reversed_release_references : tinypy_internal_iterator_release_references;
        type->traverse_references = type->release_references;
        type->iter = reversed_list != 0 ? tinypy_internal_reversed_iter : tinypy_internal_iterator_iter;
        type->next = next_slots[index];
        type->flags = (type->flags | TINYPY_TYPE_FLAG_IMMUTABLE) & ~TINYPY_TYPE_FLAG_BASE_TYPE;
        vm->iterator_types[index] = type;
    }

    __tinypy_iterator_type_set(vm, xrange_type, "__iter__", 8U, __tinypy_xrange_iter_method);
    __tinypy_iterator_type_set(vm, xrange_type, "__getitem__", 11U, __tinypy_xrange_getitem_method);
    __tinypy_iterator_type_set(vm, xrange_type, "__len__", 7U, __tinypy_xrange_len_method);
    __tinypy_iterator_type_set(vm, xrange_type, "__reversed__", 12U, __tinypy_xrange_reversed_method);
    __tinypy_iterator_type_set(vm, xrange_type, "__repr__", 8U, __tinypy_xrange_repr_method);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_iter_non_iterator_error(tinypy_vm_t *vm, tinypy_value_t *result, tinypy_error_t **out_error) {
    tinypy_message_part_t parts[] = {
        TINYPY_MESSAGE_PART_LITERAL("iter() returned non-iterator of type '"),
        TINYPY_MESSAGE_PART_TYPE_NAME(result),
        TINYPY_MESSAGE_PART_LITERAL("'"),
    };

    tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_iter(tinypy_value_t *value, tinypy_bool_t dispatch_special, tinypy_error_t **out_error) {
    tinypy_value_type_e kind;

    TINYPY_CLEAR_ERROR(out_error);
    if (dispatch_special != 0 && tinypy_internal_object_has_special_override(value, "__iter__", 8U) != 0) {
        tinypy_value_t *method = tinypy_internal_object_get_special(value, "__iter__", 8U, out_error);
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
        if (result != NULL && TINYPY_VALUE_KIND(result) != TINYPY_VALUE_OLD_INSTANCE && result->type->next == NULL && tinypy_internal_object_has_special(result, "next", 4U) == 0) {
            __tinypy_iter_non_iterator_error(vm, result, out_error);
            TINYPY_DECREF(result);
            return NULL;
        }
        return result;
    }
    if (value->type->iter != NULL) {
        tinypy_value_t *return_value_1 = value->type->iter(value, out_error);
        return return_value_1;
    }
    kind = TINYPY_VALUE_KIND(value);
    if (kind == TINYPY_VALUE_TUPLE || kind == TINYPY_VALUE_LIST || kind == TINYPY_VALUE_STRING || kind == TINYPY_VALUE_UNICODE || kind == TINYPY_VALUE_DICT || kind == TINYPY_VALUE_BUFFER || kind == TINYPY_VALUE_BYTEARRAY) {
        tinypy_value_t *return_value_2 = __tinypy_internal_iterator_new(value);
        return return_value_2;
    }
    if (dispatch_special != 0 && tinypy_internal_object_has_special(value, "__iter__", 8U) != 0) {
        tinypy_value_t *method = tinypy_internal_object_get_special(value, "__iter__", 8U, out_error);
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
        if (result != NULL && TINYPY_VALUE_KIND(result) != TINYPY_VALUE_OLD_INSTANCE && result->type->next == NULL && tinypy_internal_object_has_special(result, "next", 4U) == 0) {
            __tinypy_iter_non_iterator_error(vm, result, out_error);
            TINYPY_DECREF(result);
            return NULL;
        }
        return result;
    }
    if (dispatch_special != 0 && tinypy_internal_object_has_special(value, "__getitem__", 11U) != 0) {
        tinypy_value_t *iterator = __tinypy_internal_iterator_new(value);

        TINYPY_ITERATOR_OBJECT(iterator)->mode = INT32_C(4);
        return iterator;
    }
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_message_part_t parts[] = {
        TINYPY_MESSAGE_PART_LITERAL("'"),
        TINYPY_MESSAGE_PART_TYPE_NAME(value),
        TINYPY_MESSAGE_PART_LITERAL("' object is not iterable"),
    };
    tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_iter_builtin(tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_value_t *result = __tinypy_iter(value, TINYPY_FALSE, out_error);

    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_iter(tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_value_t *result = __tinypy_iter(value, TINYPY_TRUE, out_error);

    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_next_raw(tinypy_value_t *iterator, tinypy_error_t **out_error) {
    TINYPY_CLEAR_ERROR(out_error);
    if (iterator->type->next == NULL) {
        if (tinypy_internal_object_has_special(iterator, "next", 4U) != 0) {
            tinypy_value_t *method = tinypy_internal_object_get_special(iterator, "next", 4U, out_error);
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
            return result;
        }
        tinypy_vm_t *vm = TINYPY_VALUE_VM(iterator);
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "object is not an iterator", out_error);
        return NULL;
    }
    tinypy_value_t *return_value_1 = iterator->type->next(iterator, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_next(tinypy_value_t *iterator, tinypy_error_t **out_error) {
    tinypy_value_t *result = tinypy_internal_next_raw(iterator, out_error);

    if (result == NULL) {
        (void)tinypy_internal_exception_consume_stop_iteration(TINYPY_VALUE_VM(iterator), out_error);
    }
    return result;
}
