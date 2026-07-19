#include "tinypy/iterator.h"

#include "internal.h"

#include <assert.h>

static tinypy_value_t *__tinypy_internal_iterator_new(tinypy_value_t *iterable)
{
    tinypy_vm_t *vm = tinypy_internal_value_vm(iterable);
    tinypy_iterator_object_t *iterator = (tinypy_iterator_object_t *)tinypy_internal_value_allocate(vm, TINYPY_VALUE_ITERATOR, sizeof(*iterator));

    iterator->iterable = iterable;
    if (tinypy_internal_value_kind(iterable) == TINYPY_VALUE_LIST) {
        iterator->expected_version = TINYPY_LIST_OBJECT(iterable)->mutation_version;
    } else if (tinypy_internal_value_kind(iterable) == TINYPY_VALUE_DICT) {
        iterator->expected_version = TINYPY_DICT_OBJECT(iterable)->mutation_version;
    }
    tinypy_retain(iterable);
    return &iterator->base;
}

tinypy_value_t *tinypy_internal_dict_iterator_new(tinypy_value_t *dict, int32_t mode)
{
    tinypy_iterator_object_t *iterator;

    assert(dict != NULL);
    assert(tinypy_internal_value_kind(dict) == TINYPY_VALUE_DICT);
    assert(mode >= INT32_C(0) && mode <= INT32_C(2));
    iterator = TINYPY_ITERATOR_OBJECT(__tinypy_internal_iterator_new(dict));
    iterator->mode = mode;
    return &iterator->base;
}

void tinypy_internal_iterator_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data)
{
    visit(TINYPY_ITERATOR_OBJECT(value)->iterable, user_data);
}

tinypy_value_t *tinypy_internal_iterator_iter(tinypy_value_t *value, tinypy_error_t **out_error)
{
    tinypy_internal_clear_error(out_error);
    tinypy_retain(value);
    return value;
}

static tinypy_value_t *__tinypy_internal_iterator_next_sequence(tinypy_iterator_object_t *iterator)
{
    tinypy_value_t *item;
    tinypy_value_type_e kind = tinypy_internal_value_kind(iterator->iterable);
    size_t size = kind == TINYPY_VALUE_TUPLE ? tinypy_tuple_size(iterator->iterable) : tinypy_list_size(iterator->iterable);

    if (iterator->index == size) {
        return NULL;
    }
    item = kind == TINYPY_VALUE_TUPLE ? tinypy_tuple_get(iterator->iterable, iterator->index) : tinypy_list_get(iterator->iterable, iterator->index);
    iterator->index += 1U;
    tinypy_retain(item);
    return item;
}

static tinypy_value_t *__tinypy_internal_iterator_next_string(tinypy_iterator_object_t *iterator)
{
    const unsigned char *bytes;
    size_t size;

    bytes = tinypy_internal_value_kind(iterator->iterable) == TINYPY_VALUE_BUFFER
        ? (const unsigned char *)tinypy_buffer_view(iterator->iterable, &size)
        : (const unsigned char *)tinypy_string_view(iterator->iterable, &size);
    if (iterator->index == size) {
        return NULL;
    }
    iterator->index += 1U;
    return tinypy_string_from_bytes(tinypy_internal_value_vm(iterator->iterable), bytes + iterator->index - 1U, 1U);
}

static tinypy_value_t *__tinypy_internal_iterator_next_unicode(tinypy_iterator_object_t *iterator)
{
    const char *utf8;
    size_t byte_size;
    size_t code_point_count;
    size_t byte_index = 0U;
    size_t scalar_index = 0U;
    size_t scalar_size;

    utf8 = tinypy_unicode_utf8_view(iterator->iterable, &byte_size, &code_point_count);
    if (iterator->index == code_point_count) return NULL;
    while (scalar_index < iterator->index) {
        unsigned char lead = (unsigned char)utf8[byte_index];

        byte_index += lead < 0x80U ? 1U : (lead < 0xe0U ? 2U : (lead < 0xf0U ? 3U : 4U));
        scalar_index += 1U;
    }
    {
        unsigned char lead = (unsigned char)utf8[byte_index];

        scalar_size = lead < 0x80U ? 1U : (lead < 0xe0U ? 2U : (lead < 0xf0U ? 3U : 4U));
    }
    iterator->index += 1U;
    return tinypy_unicode_from_utf8(tinypy_internal_value_vm(iterator->iterable), utf8 + byte_index, scalar_size);
}

static tinypy_value_t *__tinypy_internal_iterator_next_bytearray(tinypy_iterator_object_t *iterator)
{
    size_t size;
    const unsigned char *bytes = (const unsigned char *)tinypy_bytearray_view(iterator->iterable, &size);

    if (iterator->index == size) return NULL;
    iterator->index += 1U;
    return tinypy_integer_from_i64(tinypy_internal_value_vm(iterator->iterable), (int64_t)bytes[iterator->index - 1U]);
}

static tinypy_value_t *__tinypy_internal_iterator_next_dict(tinypy_iterator_object_t *iterator, tinypy_error_t **out_error)
{
    tinypy_dict_object_t *dict = TINYPY_DICT_OBJECT(iterator->iterable);

    if (dict->mutation_version != iterator->expected_version) {
        tinypy_internal_make_vm_error(tinypy_internal_value_vm(iterator->iterable), TINYPY_ERROR_RUNTIME, "dictionary changed size during iteration", out_error);
        return NULL;
    }
    while (iterator->table_position <= dict->mask) {
        tinypy_dict_entry_t *entry = &dict->table[iterator->table_position];

        iterator->table_position += 1U;
        if (entry->state == TINYPY_DICT_ENTRY_ACTIVE) {
            tinypy_value_t *items[2];

            iterator->index += 1U;
            if (iterator->mode == INT32_C(1)) {
                tinypy_retain(entry->value);
                return entry->value;
            }
            if (iterator->mode == INT32_C(2)) {
                items[0] = entry->key;
                items[1] = entry->value;
                return tinypy_tuple_from_items(tinypy_internal_value_vm(iterator->iterable), items, 2U);
            }
            tinypy_retain(entry->key);
            return entry->key;
        }
    }
    return NULL;
}

tinypy_value_t *tinypy_internal_iterator_next(tinypy_value_t *value, tinypy_error_t **out_error)
{
    tinypy_iterator_object_t *iterator = TINYPY_ITERATOR_OBJECT(value);
    tinypy_value_type_e kind = tinypy_internal_value_kind(iterator->iterable);

    tinypy_internal_clear_error(out_error);
    if (iterator->mode == INT32_C(3)) {
        tinypy_value_t *result;

        if (iterator->remaining == 0U) return NULL;
        result = tinypy_integer_from_i64(tinypy_internal_value_vm(value), iterator->current);
        iterator->remaining -= 1U;
        if (iterator->remaining != 0U) {
            assert((iterator->step >= 0 && iterator->current <= INT64_MAX - iterator->step) || (iterator->step < 0 && iterator->current >= INT64_MIN - iterator->step));
            iterator->current += iterator->step;
        }
        return result;
    }
    if (iterator->mode == INT32_C(4)) {
        tinypy_vm_t *vm = tinypy_internal_value_vm(value);
        tinypy_value_t *key = tinypy_integer_from_i64(vm, (int64_t)iterator->index);
        tinypy_error_t *item_error = NULL;
        tinypy_value_t *result = tinypy_get_item(iterator->iterable, key, &item_error);

        tinypy_release(key);
        if (result != NULL) {
            iterator->index += 1U;
            return result;
        }
        if (vm->raised_value != NULL && tinypy_type_is_subtype(vm->raised_value->type, vm->exception_types[TINYPY_EXCEPTION_INDEX_ERROR]) != 0) {
            if (item_error != NULL) tinypy_error_release(item_error);
            tinypy_internal_exception_clear_raised(vm);
            return NULL;
        }
        if (out_error != NULL) *out_error = item_error;
        else if (item_error != NULL) tinypy_error_release(item_error);
        return NULL;
    }
    if (kind == TINYPY_VALUE_LIST && TINYPY_LIST_OBJECT(iterator->iterable)->mutation_version != iterator->expected_version) {
        tinypy_internal_make_vm_error(tinypy_internal_value_vm(value), TINYPY_ERROR_RUNTIME, "list changed size during iteration", out_error);
        return NULL;
    }
    switch (kind) {
    case TINYPY_VALUE_TUPLE:
    case TINYPY_VALUE_LIST: return __tinypy_internal_iterator_next_sequence(iterator);
    case TINYPY_VALUE_STRING:
    case TINYPY_VALUE_BUFFER: return __tinypy_internal_iterator_next_string(iterator);
    case TINYPY_VALUE_BYTEARRAY: return __tinypy_internal_iterator_next_bytearray(iterator);
    case TINYPY_VALUE_UNICODE: return __tinypy_internal_iterator_next_unicode(iterator);
    case TINYPY_VALUE_DICT: return __tinypy_internal_iterator_next_dict(iterator, out_error);
    default:
        assert(!"invalid builtin iterator source");
        return NULL;
    }
}

tinypy_value_t *tinypy_internal_xrange_new(tinypy_vm_t *vm, int64_t start, int64_t step, size_t length)
{
    tinypy_xrange_object_t *range = (tinypy_xrange_object_t *)tinypy_internal_value_allocate(vm, TINYPY_VALUE_XRANGE, sizeof(*range));

    range->start = start;
    range->step = step;
    range->length = length;
    return &range->base;
}

static int32_t __tinypy_iterator_integer(tinypy_vm_t *vm, tinypy_value_t *value, int64_t *out_value, tinypy_error_t **out_error)
{
    tinypy_value_type_e kind = tinypy_internal_value_kind(value);

    if (kind == TINYPY_VALUE_BOOL || kind == TINYPY_VALUE_INTEGER) {
        *out_value = TINYPY_INTEGER_VALUE(value);
        return INT32_C(1);
    }
    if (kind == TINYPY_VALUE_LONG && TINYPY_LONG_DIGIT_COUNT(value) <= 5U) {
        uint64_t magnitude = UINT64_C(0);
        size_t index = TINYPY_LONG_DIGIT_COUNT(value);

        while (index != 0U) {
            index -= 1U;
            if (magnitude > (UINT64_MAX >> 15U)) break;
            magnitude = (magnitude << 15U) | TINYPY_LONG_OBJECT(value)->digits[index];
        }
        if (index == 0U && TINYPY_LONG_SIGN(value) >= 0 && magnitude <= (uint64_t)INT64_MAX) {
            *out_value = (int64_t)magnitude;
            return INT32_C(1);
        }
        if (index == 0U && TINYPY_LONG_SIGN(value) < 0 && magnitude <= (uint64_t)INT64_MAX + UINT64_C(1)) {
            *out_value = magnitude == (uint64_t)INT64_MAX + UINT64_C(1) ? INT64_MIN : -(int64_t)magnitude;
            return INT32_C(1);
        }
    }
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "integer argument required", out_error);
    return INT32_C(0);
}

tinypy_value_t *tinypy_internal_xrange_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = type->vm;
    size_t argument_count = tinypy_tuple_size(args);
    int64_t start;
    int64_t stop;
    int64_t step;
    uint64_t distance;
    uint64_t step_magnitude;
    uint64_t length;

    if ((kwargs != NULL && tinypy_dict_size(kwargs) != 0U) || argument_count < 1U || argument_count > 3U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "xrange received invalid arguments", out_error);
        return NULL;
    }
    if (argument_count == 1U) {
        start = 0;
        if (__tinypy_iterator_integer(vm, tinypy_tuple_get(args, 0U), &stop, out_error) == 0) return NULL;
        step = 1;
    } else {
        if (__tinypy_iterator_integer(vm, tinypy_tuple_get(args, 0U), &start, out_error) == 0 || __tinypy_iterator_integer(vm, tinypy_tuple_get(args, 1U), &stop, out_error) == 0) return NULL;
        if (argument_count == 3U) {
            if (__tinypy_iterator_integer(vm, tinypy_tuple_get(args, 2U), &step, out_error) == 0) return NULL;
        } else step = 1;
    }
    if (step == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "xrange step cannot be zero", out_error);
        return NULL;
    }
    if ((step > 0 && start >= stop) || (step < 0 && start <= stop)) return tinypy_internal_xrange_new(vm, start, step, 0U);
    if (step > 0) {
        distance = (uint64_t)stop - (uint64_t)start;
        step_magnitude = (uint64_t)step;
    } else {
        distance = (uint64_t)start - (uint64_t)stop;
        step_magnitude = (uint64_t)(-(step + INT64_C(1))) + UINT64_C(1);
    }
    length = (distance - UINT64_C(1)) / step_magnitude + UINT64_C(1);
    if (length > (uint64_t)SIZE_MAX) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "xrange has too many items", out_error);
        return NULL;
    }
    return tinypy_internal_xrange_new(vm, start, step, (size_t)length);
}

tinypy_value_t *tinypy_internal_xrange_iter(tinypy_value_t *value, tinypy_error_t **out_error)
{
    tinypy_iterator_object_t *iterator;

    tinypy_internal_clear_error(out_error);
    iterator = TINYPY_ITERATOR_OBJECT(__tinypy_internal_iterator_new(value));
    iterator->mode = INT32_C(3);
    iterator->current = TINYPY_XRANGE_OBJECT(value)->start;
    iterator->step = TINYPY_XRANGE_OBJECT(value)->step;
    iterator->remaining = TINYPY_XRANGE_OBJECT(value)->length;
    return &iterator->base;
}

tinypy_value_t *tinypy_internal_enumerate_new(tinypy_value_t *iterable, int64_t start, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = tinypy_internal_value_vm(iterable);
    tinypy_value_t *iterator = tinypy_iter(iterable, out_error);
    tinypy_enumerate_object_t *enumerate;

    if (iterator == NULL) return NULL;
    enumerate = (tinypy_enumerate_object_t *)tinypy_internal_value_allocate(vm, TINYPY_VALUE_ENUMERATE, sizeof(*enumerate));
    enumerate->iterator = iterator;
    enumerate->index = start;
    return &enumerate->base;
}

void tinypy_internal_enumerate_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data)
{
    visit(TINYPY_ENUMERATE_OBJECT(value)->iterator, user_data);
}

tinypy_value_t *tinypy_internal_enumerate_iter(tinypy_value_t *value, tinypy_error_t **out_error)
{
    tinypy_internal_clear_error(out_error);
    tinypy_retain(value);
    return value;
}

tinypy_value_t *tinypy_internal_enumerate_next(tinypy_value_t *value, tinypy_error_t **out_error)
{
    tinypy_enumerate_object_t *enumerate = TINYPY_ENUMERATE_OBJECT(value);
    tinypy_value_t *item = tinypy_next(enumerate->iterator, out_error);
    tinypy_value_t *index;
    tinypy_value_t *items[2];
    tinypy_value_t *result;

    if (item == NULL) return NULL;
    index = tinypy_integer_from_i64(tinypy_internal_value_vm(value), enumerate->index);
    items[0] = index;
    items[1] = item;
    result = tinypy_tuple_from_items(tinypy_internal_value_vm(value), items, 2U);
    tinypy_release(index);
    tinypy_release(item);
    assert(enumerate->index != INT64_MAX);
    enumerate->index += 1;
    return result;
}

tinypy_value_t *tinypy_internal_reversed_new(tinypy_value_t *sequence, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = tinypy_internal_value_vm(sequence);
    tinypy_reversed_object_t *reversed;
    size_t size;

    switch (tinypy_internal_value_kind(sequence)) {
    case TINYPY_VALUE_LIST: size = tinypy_list_size(sequence); break;
    case TINYPY_VALUE_TUPLE: size = tinypy_tuple_size(sequence); break;
    case TINYPY_VALUE_STRING: size = tinypy_internal_text_byte_size(sequence); break;
    case TINYPY_VALUE_UNICODE: size = (size_t)TINYPY_SIZE(sequence); break;
    case TINYPY_VALUE_XRANGE: size = TINYPY_XRANGE_OBJECT(sequence)->length; break;
    default:
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "reversed argument must be a sequence", out_error);
        return NULL;
    }
    reversed = (tinypy_reversed_object_t *)tinypy_internal_value_allocate(vm, TINYPY_VALUE_REVERSED, sizeof(*reversed));
    reversed->sequence = sequence;
    reversed->index = size;
    tinypy_retain(sequence);
    return &reversed->base;
}

void tinypy_internal_reversed_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data)
{
    visit(TINYPY_REVERSED_OBJECT(value)->sequence, user_data);
}

tinypy_value_t *tinypy_internal_reversed_iter(tinypy_value_t *value, tinypy_error_t **out_error)
{
    tinypy_internal_clear_error(out_error);
    tinypy_retain(value);
    return value;
}

tinypy_value_t *tinypy_internal_reversed_next(tinypy_value_t *value, tinypy_error_t **out_error)
{
    tinypy_reversed_object_t *reversed = TINYPY_REVERSED_OBJECT(value);
    tinypy_value_t *key;
    tinypy_value_t *result;

    if (reversed->index == 0U) return NULL;
    reversed->index -= 1U;
    if (tinypy_internal_value_kind(reversed->sequence) == TINYPY_VALUE_XRANGE) {
        tinypy_xrange_object_t *range = TINYPY_XRANGE_OBJECT(reversed->sequence);

        assert(reversed->index == 0U || range->step >= 0 || range->start >= INT64_MIN - (int64_t)reversed->index * range->step);
        return tinypy_integer_from_i64(tinypy_internal_value_vm(value), range->start + (int64_t)reversed->index * range->step);
    }
    key = tinypy_integer_from_i64(tinypy_internal_value_vm(value), (int64_t)reversed->index);
    result = tinypy_get_item(reversed->sequence, key, out_error);
    tinypy_release(key);
    return result;
}

tinypy_value_t *tinypy_iter(tinypy_value_t *value, tinypy_error_t **out_error)
{
    tinypy_value_type_e kind;

    assert(value != NULL);
    assert(tinypy_internal_vm_valid(tinypy_internal_value_vm(value)));
    tinypy_internal_clear_error(out_error);
    if (value->type->iter != NULL) {
        return value->type->iter(value, out_error);
    }
    kind = tinypy_internal_value_kind(value);
    if (kind == TINYPY_VALUE_TUPLE || kind == TINYPY_VALUE_LIST || kind == TINYPY_VALUE_STRING || kind == TINYPY_VALUE_UNICODE || kind == TINYPY_VALUE_DICT || kind == TINYPY_VALUE_BUFFER || kind == TINYPY_VALUE_BYTEARRAY) {
        return __tinypy_internal_iterator_new(value);
    }
    if (tinypy_internal_object_has_special(value, "__iter__", 8U) != 0) {
        tinypy_value_t *method = tinypy_object_get_attr(value, "__iter__", 8U, out_error);
        tinypy_value_t *args;
        tinypy_value_t *result;

        if (method == NULL) return NULL;
        args = tinypy_tuple_from_items(tinypy_internal_value_vm(value), NULL, 0U);
        result = tinypy_call(method, args, NULL, out_error);
        tinypy_release(args);
        tinypy_release(method);
        if (result != NULL && result->type->next == NULL && tinypy_internal_object_has_special(result, "next", 4U) == 0) {
            tinypy_release(result);
            tinypy_internal_make_vm_error(tinypy_internal_value_vm(value), TINYPY_ERROR_TYPE, "__iter__ returned a non-iterator", out_error);
            return NULL;
        }
        return result;
    }
    if (tinypy_internal_object_has_special(value, "__getitem__", 11U) != 0) {
        tinypy_value_t *iterator = __tinypy_internal_iterator_new(value);

        TINYPY_ITERATOR_OBJECT(iterator)->mode = INT32_C(4);
        return iterator;
    }
    tinypy_internal_make_vm_error(tinypy_internal_value_vm(value), TINYPY_ERROR_TYPE, "object is not iterable", out_error);
    return NULL;
}

tinypy_value_t *tinypy_next(tinypy_value_t *iterator, tinypy_error_t **out_error)
{
    assert(iterator != NULL);
    assert(tinypy_internal_vm_valid(tinypy_internal_value_vm(iterator)));
    tinypy_internal_clear_error(out_error);
    if (iterator->type->next == NULL) {
        if (tinypy_internal_object_has_special(iterator, "next", 4U) != 0) {
            tinypy_value_t *method = tinypy_object_get_attr(iterator, "next", 4U, out_error);
            tinypy_value_t *args;
            tinypy_value_t *result;

            if (method == NULL) return NULL;
            args = tinypy_tuple_from_items(tinypy_internal_value_vm(iterator), NULL, 0U);
            result = tinypy_call(method, args, NULL, out_error);
            tinypy_release(args);
            tinypy_release(method);
            if (result == NULL) (void)tinypy_internal_exception_consume_stop_iteration(tinypy_internal_value_vm(iterator), out_error);
            return result;
        }
        tinypy_internal_make_vm_error(tinypy_internal_value_vm(iterator), TINYPY_ERROR_TYPE, "object is not an iterator", out_error);
        return NULL;
    }
    return iterator->type->next(iterator, out_error);
}
