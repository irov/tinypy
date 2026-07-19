#include "internal.h"

#include <string.h>

static int32_t __tinypy_container_no_keywords(tinypy_vm_t *vm, tinypy_value_t *kwargs, tinypy_error_t **out_error)
{
    if (kwargs != NULL && tinypy_dict_size(kwargs) != 0U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "method does not accept keyword arguments", out_error);
        return INT32_C(0);
    }
    return INT32_C(1);
}

static int32_t __tinypy_container_argument_count(tinypy_vm_t *vm, tinypy_value_t *args, size_t minimum, size_t maximum, tinypy_error_t **out_error)
{
    size_t count = tinypy_tuple_size(args);

    if (count < minimum || count > maximum) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "method received the wrong number of arguments", out_error);
        return INT32_C(0);
    }
    return INT32_C(1);
}

static int32_t __tinypy_container_integer_as_i64(tinypy_vm_t *vm, tinypy_value_t *value, int64_t *out_value, tinypy_error_t **out_error)
{
    tinypy_value_type_e kind = tinypy_internal_value_kind(value);

    if (kind == TINYPY_VALUE_BOOL || kind == TINYPY_VALUE_INTEGER) {
        *out_value = TINYPY_INTEGER_VALUE(value);
        return INT32_C(1);
    }
    if (kind == TINYPY_VALUE_LONG && TINYPY_LONG_DIGIT_COUNT(value) <= 5U) {
        uint64_t magnitude = 0U;
        size_t index = TINYPY_LONG_DIGIT_COUNT(value);

        while (index != 0U) {
            index -= 1U;
            if (magnitude > (UINT64_MAX >> 15U)) goto overflow;
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
overflow:
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "integer argument required", out_error);
    return INT32_C(0);
}

static int32_t __tinypy_container_list_index(tinypy_vm_t *vm, tinypy_value_t *value, size_t size, int32_t allow_end, size_t *out_index, tinypy_error_t **out_error)
{
    int64_t index;
    uint64_t distance;

    if (__tinypy_container_integer_as_i64(vm, value, &index, out_error) == 0) return INT32_C(0);
    if (index < 0) {
        distance = (uint64_t)(-(index + INT64_C(1))) + UINT64_C(1);
        if (distance > size) {
            if (allow_end != 0) {
                *out_index = 0U;
                return INT32_C(1);
            }
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_INDEX, "list index out of range", out_error);
            return INT32_C(0);
        }
        *out_index = size - (size_t)distance;
        return INT32_C(1);
    }
    if ((uint64_t)index > (uint64_t)size || (allow_end == 0 && (uint64_t)index == (uint64_t)size)) {
        if (allow_end != 0) {
            *out_index = size;
            return INT32_C(1);
        }
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_INDEX, "list index out of range", out_error);
        return INT32_C(0);
    }
    *out_index = (size_t)index;
    return INT32_C(1);
}

static tinypy_value_t *__tinypy_container_collect(tinypy_vm_t *vm, tinypy_value_t *iterable, tinypy_error_t **out_error)
{
    tinypy_value_t *iterator = tinypy_iter(iterable, out_error);
    tinypy_value_t *result;
    tinypy_error_t *iteration_error = NULL;

    if (iterator == NULL) return NULL;
    result = tinypy_list_from_items(vm, NULL, 0U);
    for (;;) {
        tinypy_value_t *item = tinypy_next(iterator, &iteration_error);

        if (item == NULL) break;
        tinypy_list_append(result, item);
        tinypy_release(item);
    }
    tinypy_release(iterator);
    if (iteration_error != NULL) {
        tinypy_release(result);
        if (out_error != NULL) *out_error = iteration_error;
        else tinypy_error_release(iteration_error);
        return NULL;
    }
    return result;
}

static tinypy_value_t *__tinypy_list_append_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);

    (void)user_data;
    if (__tinypy_container_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_container_argument_count(vm, args, 2U, 2U, out_error) == 0) return NULL;
    tinypy_list_append(tinypy_tuple_get(args, 0U), tinypy_tuple_get(args, 1U));
    return tinypy_none_get(vm);
}

static tinypy_value_t *__tinypy_list_extend_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);
    tinypy_value_t *collected;

    (void)user_data;
    if (__tinypy_container_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_container_argument_count(vm, args, 2U, 2U, out_error) == 0) return NULL;
    collected = __tinypy_container_collect(vm, tinypy_tuple_get(args, 1U), out_error);
    if (collected == NULL) return NULL;
    tinypy_list_extend(tinypy_tuple_get(args, 0U), TINYPY_LIST_OBJECT(collected)->items, tinypy_list_size(collected));
    tinypy_release(collected);
    return tinypy_none_get(vm);
}

static tinypy_value_t *__tinypy_list_insert_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);
    tinypy_value_t *list;
    size_t index;

    (void)user_data;
    if (__tinypy_container_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_container_argument_count(vm, args, 3U, 3U, out_error) == 0) return NULL;
    list = tinypy_tuple_get(args, 0U);
    if (__tinypy_container_list_index(vm, tinypy_tuple_get(args, 1U), tinypy_list_size(list), INT32_C(1), &index, out_error) == 0) return NULL;
    tinypy_list_insert(list, index, tinypy_tuple_get(args, 2U));
    return tinypy_none_get(vm);
}

static tinypy_value_t *__tinypy_list_pop_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);
    tinypy_value_t *list;
    size_t size;
    size_t index;

    (void)user_data;
    if (__tinypy_container_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_container_argument_count(vm, args, 1U, 2U, out_error) == 0) return NULL;
    list = tinypy_tuple_get(args, 0U);
    size = tinypy_list_size(list);
    if (size == 0U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_INDEX, "pop from empty list", out_error);
        return NULL;
    }
    if (tinypy_tuple_size(args) == 1U) index = size - 1U;
    else if (__tinypy_container_list_index(vm, tinypy_tuple_get(args, 1U), size, INT32_C(0), &index, out_error) == 0) return NULL;
    return tinypy_list_pop(list, index);
}

static tinypy_value_t *__tinypy_list_remove_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);
    tinypy_value_t *list;
    tinypy_value_t *needle;
    size_t index;

    (void)user_data;
    if (__tinypy_container_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_container_argument_count(vm, args, 2U, 2U, out_error) == 0) return NULL;
    list = tinypy_tuple_get(args, 0U);
    needle = tinypy_tuple_get(args, 1U);
    for (index = 0U; index < tinypy_list_size(list); index += 1U) {
        if (tinypy_equal(tinypy_list_get(list, index), needle) != 0) {
            tinypy_list_delete(list, index);
            return tinypy_none_get(vm);
        }
    }
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "list.remove(x): x not in list", out_error);
    return NULL;
}

static tinypy_value_t *__tinypy_list_count_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);
    tinypy_value_t *list;
    tinypy_value_t *needle;
    size_t index;
    int64_t count = 0;

    (void)user_data;
    if (__tinypy_container_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_container_argument_count(vm, args, 2U, 2U, out_error) == 0) return NULL;
    list = tinypy_tuple_get(args, 0U);
    needle = tinypy_tuple_get(args, 1U);
    for (index = 0U; index < tinypy_list_size(list); index += 1U) if (tinypy_equal(tinypy_list_get(list, index), needle) != 0) count += INT64_C(1);
    return tinypy_integer_from_i64(vm, count);
}

static tinypy_value_t *__tinypy_list_index_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);
    tinypy_value_t *list;
    tinypy_value_t *needle;
    int64_t start = 0;
    int64_t stop;
    int64_t size;
    int64_t index;

    (void)user_data;
    if (__tinypy_container_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_container_argument_count(vm, args, 2U, 4U, out_error) == 0) return NULL;
    list = tinypy_tuple_get(args, 0U);
    needle = tinypy_tuple_get(args, 1U);
    size = (int64_t)tinypy_list_size(list);
    stop = size;
    if (tinypy_tuple_size(args) >= 3U && __tinypy_container_integer_as_i64(vm, tinypy_tuple_get(args, 2U), &start, out_error) == 0) return NULL;
    if (tinypy_tuple_size(args) == 4U && __tinypy_container_integer_as_i64(vm, tinypy_tuple_get(args, 3U), &stop, out_error) == 0) return NULL;
    if (start < 0) start = start < -size ? 0 : start + size;
    if (start > size) start = size;
    if (stop < 0) stop = stop < -size ? 0 : stop + size;
    if (stop > size) stop = size;
    for (index = start; index < stop; index += 1) if (tinypy_equal(tinypy_list_get(list, (size_t)index), needle) != 0) return tinypy_integer_from_i64(vm, index);
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "list.index(x): x not in list", out_error);
    return NULL;
}

static tinypy_value_t *__tinypy_list_reverse_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);
    tinypy_value_t *list;
    size_t left;
    size_t right;

    (void)user_data;
    if (__tinypy_container_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_container_argument_count(vm, args, 1U, 1U, out_error) == 0) return NULL;
    list = tinypy_tuple_get(args, 0U);
    left = 0U;
    right = tinypy_list_size(list);
    while (left < right && left < --right) {
        tinypy_value_t *left_value = tinypy_list_get(list, left);
        tinypy_value_t *right_value = tinypy_list_get(list, right);

        tinypy_retain(left_value);
        tinypy_retain(right_value);
        tinypy_list_set(list, left, right_value);
        tinypy_list_set(list, right, left_value);
        tinypy_release(right_value);
        tinypy_release(left_value);
        left += 1U;
    }
    return tinypy_none_get(vm);
}

static int32_t __tinypy_list_sort_compare(tinypy_vm_t *vm, tinypy_value_t *left, tinypy_value_t *right, tinypy_value_t *compare, int32_t reverse, int32_t *out_less, tinypy_error_t **out_error)
{
    int32_t less;

    if (compare != NULL && tinypy_internal_value_kind(compare) != TINYPY_VALUE_NONE) {
        tinypy_value_t *items[2] = {left, right};
        tinypy_value_t *args = tinypy_tuple_from_items(vm, items, 2U);
        tinypy_value_t *result = tinypy_call(compare, args, NULL, out_error);
        int64_t order;

        tinypy_release(args);
        if (result == NULL) return INT32_C(0);
        if (__tinypy_container_integer_as_i64(vm, result, &order, out_error) == 0) {
            tinypy_release(result);
            return INT32_C(0);
        }
        tinypy_release(result);
        less = order < 0 ? INT32_C(1) : INT32_C(0);
    } else {
        less = tinypy_compare_bool(left, right, TINYPY_COMPARE_LESS, out_error);
        if (less < 0) return INT32_C(0);
    }
    *out_less = reverse != 0 ? (tinypy_equal(left, right) == 0 && less == 0 ? INT32_C(1) : INT32_C(0)) : less;
    return INT32_C(1);
}

static tinypy_value_t *__tinypy_list_sort_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);
    tinypy_value_t *list;
    tinypy_value_t *compare = NULL;
    tinypy_value_t *key_function = NULL;
    int32_t reverse = INT32_C(0);
    size_t size;
    tinypy_value_t **keys = NULL;
    size_t index;

    (void)user_data;
    if (__tinypy_container_argument_count(vm, args, 1U, 4U, out_error) == 0) return NULL;
    list = tinypy_tuple_get(args, 0U);
    if (tinypy_tuple_size(args) >= 2U) compare = tinypy_tuple_get(args, 1U);
    if (tinypy_tuple_size(args) >= 3U) key_function = tinypy_tuple_get(args, 2U);
    if (tinypy_tuple_size(args) >= 4U) {
        reverse = tinypy_truth(tinypy_tuple_get(args, 3U), out_error);
        if (reverse < 0) return NULL;
    }
    if (kwargs != NULL && tinypy_dict_size(kwargs) != 0U) {
        tinypy_dict_object_t *dict = TINYPY_DICT_OBJECT(kwargs);

        for (index = 0U; index <= dict->mask; index += 1U) {
            tinypy_dict_entry_t *entry = &dict->table[index];
            const unsigned char *name;
            size_t name_size;

            if (entry->state != TINYPY_DICT_ENTRY_ACTIVE || tinypy_internal_value_kind(entry->key) != TINYPY_VALUE_STRING) continue;
            name = tinypy_string_view(entry->key, &name_size);
            if (name_size == 3U && memcmp(name, "cmp", 3U) == 0) compare = entry->value;
            else if (name_size == 3U && memcmp(name, "key", 3U) == 0) key_function = entry->value;
            else if (name_size == 7U && memcmp(name, "reverse", 7U) == 0) {
                reverse = tinypy_truth(entry->value, out_error);
                if (reverse < 0) return NULL;
            } else {
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "sort received an unexpected keyword", out_error);
                return NULL;
            }
        }
    }
    size = tinypy_list_size(list);
    if (size > 1U) keys = (tinypy_value_t **)tinypy_internal_vm_allocate(vm, size * sizeof(*keys), (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    for (index = 0U; index < size; index += 1U) {
        if (key_function != NULL && tinypy_internal_value_kind(key_function) != TINYPY_VALUE_NONE) {
            tinypy_value_t *item = tinypy_list_get(list, index);
            tinypy_value_t *key_args = tinypy_tuple_from_items(vm, &item, 1U);

            keys[index] = tinypy_call(key_function, key_args, NULL, out_error);
            tinypy_release(key_args);
            if (keys[index] == NULL) {
                size_t release_index;

                for (release_index = 0U; release_index < index; release_index += 1U) tinypy_release(keys[release_index]);
                if (keys != NULL) tinypy_internal_vm_deallocate(vm, keys, size * sizeof(*keys), (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
                return NULL;
            }
        } else if (keys != NULL) keys[index] = tinypy_list_get(list, index);
    }
    for (index = 1U; index < size; index += 1U) {
        tinypy_value_t *item = TINYPY_LIST_OBJECT(list)->items[index];
        tinypy_value_t *key = keys[index];
        size_t position = index;

        while (position != 0U) {
            int32_t less;

            if (__tinypy_list_sort_compare(vm, key, keys[position - 1U], compare, reverse, &less, out_error) == 0) {
                size_t release_index;

                if (key_function != NULL && tinypy_internal_value_kind(key_function) != TINYPY_VALUE_NONE) for (release_index = 0U; release_index < size; release_index += 1U) tinypy_release(keys[release_index]);
                tinypy_internal_vm_deallocate(vm, keys, size * sizeof(*keys), (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
                return NULL;
            }
            if (less == 0) break;
            TINYPY_LIST_OBJECT(list)->items[position] = TINYPY_LIST_OBJECT(list)->items[position - 1U];
            keys[position] = keys[position - 1U];
            position -= 1U;
        }
        TINYPY_LIST_OBJECT(list)->items[position] = item;
        keys[position] = key;
    }
    if (size > 1U) {
        if (key_function != NULL && tinypy_internal_value_kind(key_function) != TINYPY_VALUE_NONE) for (index = 0U; index < size; index += 1U) tinypy_release(keys[index]);
        tinypy_internal_vm_deallocate(vm, keys, size * sizeof(*keys), (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
        assert(TINYPY_LIST_OBJECT(list)->mutation_version != UINT64_MAX);
        TINYPY_LIST_OBJECT(list)->mutation_version += UINT64_C(1);
    }
    return tinypy_none_get(vm);
}

static tinypy_value_t *__tinypy_dict_get_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);
    tinypy_value_t *dict;
    tinypy_value_t *key;
    tinypy_value_t *result;

    (void)user_data;
    if (__tinypy_container_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_container_argument_count(vm, args, 2U, 3U, out_error) == 0) return NULL;
    dict = tinypy_tuple_get(args, 0U);
    key = tinypy_tuple_get(args, 1U);
    if (tinypy_dict_contains(dict, key) != 0) result = tinypy_dict_get(dict, key);
    else if (tinypy_tuple_size(args) == 3U) result = tinypy_tuple_get(args, 2U);
    else return tinypy_none_get(vm);
    tinypy_retain(result);
    return result;
}

static tinypy_value_t *__tinypy_dict_has_key_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);

    (void)user_data;
    if (__tinypy_container_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_container_argument_count(vm, args, 2U, 2U, out_error) == 0) return NULL;
    return tinypy_bool_from_i32(vm, tinypy_dict_contains(tinypy_tuple_get(args, 0U), tinypy_tuple_get(args, 1U)));
}

static tinypy_value_t *__tinypy_dict_snapshot(tinypy_value_t *dict_value, int32_t mode)
{
    tinypy_vm_t *vm = tinypy_internal_value_vm(dict_value);
    tinypy_dict_object_t *dict = TINYPY_DICT_OBJECT(dict_value);
    tinypy_value_t *result = tinypy_list_from_items(vm, NULL, 0U);
    size_t index;

    for (index = 0U; index <= dict->mask; index += 1U) {
        tinypy_dict_entry_t *entry = &dict->table[index];
        tinypy_value_t *item;

        if (entry->state != TINYPY_DICT_ENTRY_ACTIVE) continue;
        if (mode == INT32_C(0)) item = entry->key;
        else if (mode == INT32_C(1)) item = entry->value;
        else {
            tinypy_value_t *items[2] = {entry->key, entry->value};

            item = tinypy_tuple_from_items(vm, items, 2U);
            tinypy_list_append(result, item);
            tinypy_release(item);
            continue;
        }
        tinypy_list_append(result, item);
    }
    return result;
}

static tinypy_value_t *__tinypy_dict_list_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);
    int32_t mode = (int32_t)(intptr_t)user_data;

    if (__tinypy_container_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_container_argument_count(vm, args, 1U, 1U, out_error) == 0) return NULL;
    return __tinypy_dict_snapshot(tinypy_tuple_get(args, 0U), mode);
}

static tinypy_value_t *__tinypy_dict_iter_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);
    int32_t mode = (int32_t)(intptr_t)user_data;

    if (__tinypy_container_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_container_argument_count(vm, args, 1U, 1U, out_error) == 0) return NULL;
    return tinypy_internal_dict_iterator_new(tinypy_tuple_get(args, 0U), mode);
}

static tinypy_value_t *__tinypy_dict_view_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);

    if (__tinypy_container_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_container_argument_count(vm, args, 1U, 1U, out_error) == 0) return NULL;
    return tinypy_dict_view_new(tinypy_tuple_get(args, 0U), (tinypy_dict_view_kind_e)(intptr_t)user_data);
}

static tinypy_value_t *__tinypy_dict_clear_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);

    (void)user_data;
    if (__tinypy_container_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_container_argument_count(vm, args, 1U, 1U, out_error) == 0) return NULL;
    tinypy_dict_clear(tinypy_tuple_get(args, 0U));
    return tinypy_none_get(vm);
}

static tinypy_value_t *__tinypy_dict_copy_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);
    tinypy_value_t *source;
    tinypy_value_t *result;
    tinypy_dict_object_t *dict;
    size_t index;

    (void)user_data;
    if (__tinypy_container_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_container_argument_count(vm, args, 1U, 1U, out_error) == 0) return NULL;
    source = tinypy_tuple_get(args, 0U);
    result = tinypy_dict_new(vm);
    dict = TINYPY_DICT_OBJECT(source);
    for (index = 0U; index <= dict->mask; index += 1U) if (dict->table[index].state == TINYPY_DICT_ENTRY_ACTIVE) tinypy_dict_set(result, dict->table[index].key, dict->table[index].value);
    return result;
}

static int32_t __tinypy_dict_update_from(tinypy_value_t *target, tinypy_value_t *source, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = tinypy_internal_value_vm(target);

    if (tinypy_internal_value_kind(source) == TINYPY_VALUE_DICT) {
        tinypy_dict_object_t *dict = TINYPY_DICT_OBJECT(source);
        size_t index;

        for (index = 0U; index <= dict->mask; index += 1U) if (dict->table[index].state == TINYPY_DICT_ENTRY_ACTIVE) tinypy_dict_set(target, dict->table[index].key, dict->table[index].value);
        return INT32_C(1);
    }
    {
        tinypy_value_t *pairs = __tinypy_container_collect(vm, source, out_error);
        size_t index;

        if (pairs == NULL) return INT32_C(0);
        for (index = 0U; index < tinypy_list_size(pairs); index += 1U) {
            tinypy_value_t *pair = tinypy_list_get(pairs, index);
            tinypy_value_type_e kind = tinypy_internal_value_kind(pair);
            size_t pair_size = kind == TINYPY_VALUE_TUPLE ? tinypy_tuple_size(pair) : (kind == TINYPY_VALUE_LIST ? tinypy_list_size(pair) : 0U);
            tinypy_value_t *key;
            tinypy_value_t *value;

            if (pair_size != 2U) {
                tinypy_release(pairs);
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "dictionary update item does not have length two", out_error);
                return INT32_C(0);
            }
            key = kind == TINYPY_VALUE_TUPLE ? tinypy_tuple_get(pair, 0U) : tinypy_list_get(pair, 0U);
            value = kind == TINYPY_VALUE_TUPLE ? tinypy_tuple_get(pair, 1U) : tinypy_list_get(pair, 1U);
            tinypy_dict_set(target, key, value);
        }
        tinypy_release(pairs);
    }
    return INT32_C(1);
}

static tinypy_value_t *__tinypy_dict_update_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);
    tinypy_value_t *target;

    (void)user_data;
    if (__tinypy_container_argument_count(vm, args, 1U, 2U, out_error) == 0) return NULL;
    target = tinypy_tuple_get(args, 0U);
    if (tinypy_tuple_size(args) == 2U && __tinypy_dict_update_from(target, tinypy_tuple_get(args, 1U), out_error) == 0) return NULL;
    if (kwargs != NULL && __tinypy_dict_update_from(target, kwargs, out_error) == 0) return NULL;
    return tinypy_none_get(vm);
}

static tinypy_value_t *__tinypy_dict_setdefault_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);
    tinypy_value_t *dict;
    tinypy_value_t *key;
    tinypy_value_t *value;

    (void)user_data;
    if (__tinypy_container_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_container_argument_count(vm, args, 2U, 3U, out_error) == 0) return NULL;
    dict = tinypy_tuple_get(args, 0U);
    key = tinypy_tuple_get(args, 1U);
    if (tinypy_dict_contains(dict, key) != 0) value = tinypy_dict_get(dict, key);
    else {
        if (tinypy_tuple_size(args) == 3U) value = tinypy_tuple_get(args, 2U);
        else {
            value = tinypy_none_get(vm);
            tinypy_dict_set(dict, key, value);
            return value;
        }
        tinypy_dict_set(dict, key, value);
    }
    tinypy_retain(value);
    return value;
}

static tinypy_value_t *__tinypy_dict_pop_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);
    tinypy_value_t *dict;
    tinypy_value_t *key;
    tinypy_value_t *value;

    (void)user_data;
    if (__tinypy_container_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_container_argument_count(vm, args, 2U, 3U, out_error) == 0) return NULL;
    dict = tinypy_tuple_get(args, 0U);
    key = tinypy_tuple_get(args, 1U);
    if (tinypy_dict_contains(dict, key) == 0) {
        if (tinypy_tuple_size(args) == 3U) {
            value = tinypy_tuple_get(args, 2U);
            tinypy_retain(value);
            return value;
        }
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_KEY, "dictionary key is absent", out_error);
        return NULL;
    }
    value = tinypy_dict_get(dict, key);
    tinypy_retain(value);
    tinypy_dict_delete(dict, key);
    return value;
}

static tinypy_value_t *__tinypy_dict_popitem_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);
    tinypy_value_t *dict_value;
    tinypy_dict_object_t *dict;
    size_t index;

    (void)user_data;
    if (__tinypy_container_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_container_argument_count(vm, args, 1U, 1U, out_error) == 0) return NULL;
    dict_value = tinypy_tuple_get(args, 0U);
    dict = TINYPY_DICT_OBJECT(dict_value);
    for (index = dict->mask + 1U; index != 0U; index -= 1U) {
        tinypy_dict_entry_t *entry = &dict->table[index - 1U];

        if (entry->state == TINYPY_DICT_ENTRY_ACTIVE) {
            tinypy_value_t *items[2] = {entry->key, entry->value};
            tinypy_value_t *result = tinypy_tuple_from_items(vm, items, 2U);

            tinypy_dict_delete(dict_value, entry->key);
            return result;
        }
    }
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_KEY, "popitem(): dictionary is empty", out_error);
    return NULL;
}

static tinypy_value_t *__tinypy_container_getitem_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);

    (void)user_data;
    if (__tinypy_container_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_container_argument_count(vm, args, 2U, 2U, out_error) == 0) return NULL;
    return tinypy_get_item(tinypy_tuple_get(args, 0U), tinypy_tuple_get(args, 1U), out_error);
}

static void __tinypy_container_add_method(tinypy_type_t *type, const char *name, size_t name_size, tinypy_native_function_callback_t callback, void *user_data)
{
    tinypy_value_t *function = tinypy_native_function_new(type->vm, name, name_size, callback, user_data, NULL);

    tinypy_type_set_attr(type, name, name_size, function);
    tinypy_release(function);
}

void tinypy_internal_initialize_container_types(tinypy_vm_t *vm)
{
    __tinypy_container_add_method(&vm->list_type, "append", 6U, __tinypy_list_append_method, NULL);
    __tinypy_container_add_method(&vm->list_type, "extend", 6U, __tinypy_list_extend_method, NULL);
    __tinypy_container_add_method(&vm->list_type, "insert", 6U, __tinypy_list_insert_method, NULL);
    __tinypy_container_add_method(&vm->list_type, "pop", 3U, __tinypy_list_pop_method, NULL);
    __tinypy_container_add_method(&vm->list_type, "remove", 6U, __tinypy_list_remove_method, NULL);
    __tinypy_container_add_method(&vm->list_type, "count", 5U, __tinypy_list_count_method, NULL);
    __tinypy_container_add_method(&vm->list_type, "index", 5U, __tinypy_list_index_method, NULL);
    __tinypy_container_add_method(&vm->list_type, "reverse", 7U, __tinypy_list_reverse_method, NULL);
    __tinypy_container_add_method(&vm->list_type, "sort", 4U, __tinypy_list_sort_method, NULL);
    __tinypy_container_add_method(&vm->dict_type, "get", 3U, __tinypy_dict_get_method, NULL);
    __tinypy_container_add_method(&vm->dict_type, "has_key", 7U, __tinypy_dict_has_key_method, NULL);
    __tinypy_container_add_method(&vm->dict_type, "keys", 4U, __tinypy_dict_list_method, (void *)(intptr_t)0);
    __tinypy_container_add_method(&vm->dict_type, "values", 6U, __tinypy_dict_list_method, (void *)(intptr_t)1);
    __tinypy_container_add_method(&vm->dict_type, "items", 5U, __tinypy_dict_list_method, (void *)(intptr_t)2);
    __tinypy_container_add_method(&vm->dict_type, "iterkeys", 8U, __tinypy_dict_iter_method, (void *)(intptr_t)0);
    __tinypy_container_add_method(&vm->dict_type, "itervalues", 10U, __tinypy_dict_iter_method, (void *)(intptr_t)1);
    __tinypy_container_add_method(&vm->dict_type, "iteritems", 9U, __tinypy_dict_iter_method, (void *)(intptr_t)2);
    __tinypy_container_add_method(&vm->dict_type, "viewkeys", 8U, __tinypy_dict_view_method, (void *)(intptr_t)TINYPY_DICT_VIEW_KEYS);
    __tinypy_container_add_method(&vm->dict_type, "viewvalues", 10U, __tinypy_dict_view_method, (void *)(intptr_t)TINYPY_DICT_VIEW_VALUES);
    __tinypy_container_add_method(&vm->dict_type, "viewitems", 9U, __tinypy_dict_view_method, (void *)(intptr_t)TINYPY_DICT_VIEW_ITEMS);
    __tinypy_container_add_method(&vm->dict_type, "clear", 5U, __tinypy_dict_clear_method, NULL);
    __tinypy_container_add_method(&vm->dict_type, "copy", 4U, __tinypy_dict_copy_method, NULL);
    __tinypy_container_add_method(&vm->dict_type, "update", 6U, __tinypy_dict_update_method, NULL);
    __tinypy_container_add_method(&vm->dict_type, "setdefault", 10U, __tinypy_dict_setdefault_method, NULL);
    __tinypy_container_add_method(&vm->dict_type, "pop", 3U, __tinypy_dict_pop_method, NULL);
    __tinypy_container_add_method(&vm->dict_type, "popitem", 7U, __tinypy_dict_popitem_method, NULL);
    __tinypy_container_add_method(&vm->dict_type, "__getitem__", 11U, __tinypy_container_getitem_method, NULL);
}
