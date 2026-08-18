#include "internal.h"

#include <string.h>

//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_container_no_keywords(tinypy_vm_t *vm, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    if (kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "method does not accept keyword arguments", out_error);
        return TINYPY_FALSE;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_container_argument_count(tinypy_vm_t *vm, tinypy_value_t *args, size_t minimum, size_t maximum, tinypy_error_t **out_error) {
    size_t count = TINYPY_TUPLE_SIZE(args);

    if (count < minimum || count > maximum) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "method received the wrong number of arguments", out_error);
        return TINYPY_FALSE;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_container_integer_as_i64(tinypy_vm_t *vm, tinypy_value_t *value, int64_t *out_value, tinypy_error_t **out_error) {
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);

    if (kind == TINYPY_VALUE_BOOL || kind == TINYPY_VALUE_INTEGER) {
        *out_value = TINYPY_INTEGER_VALUE(value);
        return TINYPY_TRUE;
    }
    if (kind == TINYPY_VALUE_LONG && TINYPY_LONG_DIGIT_COUNT(value) <= 5U) {
        uint64_t magnitude = 0U;
        size_t index = TINYPY_LONG_DIGIT_COUNT(value);

        while (index != 0U) {
            index -= 1U;
            if (magnitude > (UINT64_MAX >> 15U)) {
                goto overflow;
            }
            magnitude = (magnitude << 15U) | TINYPY_LONG_OBJECT(value)->digits[index];
        }
        if (TINYPY_LONG_SIGN(value) >= 0 && magnitude <= (uint64_t)INT64_MAX) {
            *out_value = (int64_t)magnitude;
            return TINYPY_TRUE;
        }
        if (TINYPY_LONG_SIGN(value) < 0 && magnitude <= (uint64_t)INT64_MAX + UINT64_C(1)) {
            *out_value = magnitude == (uint64_t)INT64_MAX + UINT64_C(1) ? INT64_MIN : -(int64_t)magnitude;
            return TINYPY_TRUE;
        }
    }
overflow:
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "integer argument required", out_error);
    return TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_container_list_index(tinypy_vm_t *vm, tinypy_value_t *value, size_t size, tinypy_bool_t allow_end, size_t *out_index, tinypy_error_t **out_error) {
    int64_t index;
    uint64_t distance;

    if (__tinypy_container_integer_as_i64(vm, value, &index, out_error) == 0) {
        return TINYPY_FALSE;
    }
    if (index < 0) {
        distance = (uint64_t)(-(index + INT64_C(1))) + UINT64_C(1);
        if (distance > size) {
            if (allow_end != 0) {
                *out_index = 0U;
                return TINYPY_TRUE;
            }
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_INDEX, "list index out of range", out_error);
            return TINYPY_FALSE;
        }
        *out_index = size - (size_t)distance;
        return TINYPY_TRUE;
    }
    if ((uint64_t)index > (uint64_t)size || (allow_end == 0 && (uint64_t)index == (uint64_t)size)) {
        if (allow_end != 0) {
            *out_index = size;
            return TINYPY_TRUE;
        }
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_INDEX, "list index out of range", out_error);
        return TINYPY_FALSE;
    }
    *out_index = (size_t)index;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_container_collect(tinypy_vm_t *vm, tinypy_value_t *iterable, tinypy_error_t **out_error) {
    tinypy_value_t *iterator = tinypy_iter(iterable, out_error);
    tinypy_error_t *iteration_error = NULL;

    if (iterator == NULL) {
        return NULL;
    }
    tinypy_value_t *result = tinypy_list_from_items(vm, NULL, 0U);
    for (;;) {
        tinypy_value_t *item = tinypy_next(iterator, &iteration_error);

        if (item == NULL) {
            break;
        }
        tinypy_list_append(result, item);
        TINYPY_DECREF(item);
    }
    TINYPY_DECREF(iterator);
    if (iteration_error != NULL) {
        TINYPY_DECREF(result);
        if (out_error != NULL) {
            *out_error = iteration_error;
        }
        else {
            tinypy_error_release(iteration_error);
        }
        return NULL;
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_list_append_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_container_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_container_argument_count(vm, args, 2U, 2U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *item = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *item_2 = TINYPY_TUPLE_GET(args, 1U);
    tinypy_list_append(item, item_2);
    tinypy_value_t *return_value_1 = tinypy_none_get(vm);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_list_extend_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_container_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_container_argument_count(vm, args, 2U, 2U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *item = TINYPY_TUPLE_GET(args, 1U);
    tinypy_value_t *collected = __tinypy_container_collect(vm, item, out_error);
    if (collected == NULL) {
        return NULL;
    }
    tinypy_value_t *item_2 = TINYPY_TUPLE_GET(args, 0U);
    size_t list_size = TINYPY_LIST_SIZE(collected);
    tinypy_list_extend(item_2, TINYPY_LIST_OBJECT(collected)->items, list_size);
    TINYPY_DECREF(collected);
    tinypy_value_t *return_value_1 = tinypy_none_get(vm);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_list_insert_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    size_t index;

    (void)user_data;
    if (__tinypy_container_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_container_argument_count(vm, args, 3U, 3U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *list = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *item_2 = TINYPY_TUPLE_GET(args, 1U);
    size_t list_size = TINYPY_LIST_SIZE(list);
    if (__tinypy_container_list_index(vm, item_2, list_size, INT32_C(1), &index, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *item = TINYPY_TUPLE_GET(args, 2U);
    tinypy_list_insert(list, index, item);
    tinypy_value_t *return_value_1 = tinypy_none_get(vm);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_list_pop_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    size_t size;
    size_t index;

    (void)user_data;
    if (__tinypy_container_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_container_argument_count(vm, args, 1U, 2U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *list = TINYPY_TUPLE_GET(args, 0U);
    size = TINYPY_LIST_SIZE(list);
    if (size == 0U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_INDEX, "pop from empty list", out_error);
        return NULL;
    }
    if (TINYPY_TUPLE_SIZE(args) == 1U) {
        index = size - 1U;
    }
    else {
        tinypy_value_t *index_value = TINYPY_TUPLE_GET(args, 1U);
        if (__tinypy_container_list_index(vm, index_value, size, INT32_C(0), &index, out_error) == 0) {
            return NULL;
        }
    }
    tinypy_value_t *return_value_1 = tinypy_list_pop(list, index);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_list_remove_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    size_t index = 0U;

    (void)user_data;
    if (__tinypy_container_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_container_argument_count(vm, args, 2U, 2U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *list = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *needle = TINYPY_TUPLE_GET(args, 1U);
    while (index < TINYPY_LIST_SIZE(list)) {
        tinypy_value_t *item = TINYPY_LIST_GET(list, index);
        int32_t equal;

        TINYPY_INCREF(item);
        equal = item == needle ? 1 : tinypy_compare_bool(item, needle, TINYPY_COMPARE_EQUAL, out_error);
        TINYPY_DECREF(item);
        if (equal < 0) {
            return NULL;
        }
        if (equal != 0) {
            if (index < TINYPY_LIST_SIZE(list)) {
                tinypy_list_delete(list, index);
            }
            tinypy_value_t *return_value_1 = tinypy_none_get(vm);
            return return_value_1;
        }
        index += 1U;
    }
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "list.remove(x): x not in list", out_error);
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_list_count_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    int64_t count = 0;
    size_t index = 0U;

    (void)user_data;
    if (__tinypy_container_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_container_argument_count(vm, args, 2U, 2U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *list = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *needle = TINYPY_TUPLE_GET(args, 1U);
    while (index < TINYPY_LIST_SIZE(list)) {
        tinypy_value_t *item = TINYPY_LIST_GET(list, index);
        int32_t equal;

        TINYPY_INCREF(item);
        equal = item == needle ? 1 : tinypy_compare_bool(item, needle, TINYPY_COMPARE_EQUAL, out_error);
        TINYPY_DECREF(item);
        if (equal < 0) {
            return NULL;
        }
        if (equal != 0) {
            count += INT64_C(1);
        }
        index += 1U;
    }
    tinypy_value_t *return_value_1 = tinypy_integer_from_i64(vm, count);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_list_index_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    int64_t start = 0;
    int64_t stop;
    int64_t size;
    int64_t index;

    (void)user_data;
    if (__tinypy_container_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_container_argument_count(vm, args, 2U, 4U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *list = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *needle = TINYPY_TUPLE_GET(args, 1U);
    size = (int64_t)TINYPY_LIST_SIZE(list);
    stop = size;
    tinypy_bool_t condition = TINYPY_TUPLE_SIZE(args) >= 3U;
    if (condition != 0) {
        tinypy_value_t *item_2 = TINYPY_TUPLE_GET(args, 2U);
        condition = __tinypy_container_integer_as_i64(vm, item_2, &start, out_error) == 0;
    }
    if (condition) {
        return NULL;
    }
    tinypy_bool_t condition_2 = TINYPY_TUPLE_SIZE(args) == 4U;
    if (condition_2 != 0) {
        tinypy_value_t *item_2 = TINYPY_TUPLE_GET(args, 3U);
        condition_2 = __tinypy_container_integer_as_i64(vm, item_2, &stop, out_error) == 0;
    }
    if (condition_2) {
        return NULL;
    }
    if (start < 0) {
        start = start < -size ? 0 : start + size;
    }
    if (start > size) {
        start = size;
    }
    if (stop < 0) {
        stop = stop < -size ? 0 : stop + size;
    }
    if (stop > size) {
        stop = size;
    }
    for (index = start; index < stop && index < (int64_t)TINYPY_LIST_SIZE(list); ++index) {
        tinypy_value_t *item = TINYPY_LIST_GET(list, (size_t)index);
        int32_t equal;

        TINYPY_INCREF(item);
        equal = item == needle ? 1 : tinypy_compare_bool(item, needle, TINYPY_COMPARE_EQUAL, out_error);
        TINYPY_DECREF(item);
        if (equal < 0) {
            return NULL;
        }
        if (equal != 0) {
            tinypy_value_t *return_value_1 = tinypy_integer_from_i64(vm, index);
            return return_value_1;
        }
    }
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "list.index(x): x not in list", out_error);
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_list_reverse_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    size_t left;
    size_t right;

    (void)user_data;
    if (__tinypy_container_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_container_argument_count(vm, args, 1U, 1U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *list = TINYPY_TUPLE_GET(args, 0U);
    left = 0U;
    right = TINYPY_LIST_SIZE(list);
    while (left < right && left < --right) {
        tinypy_value_t *left_value = TINYPY_LIST_OBJECT(list)->items[left];

        TINYPY_LIST_OBJECT(list)->items[left] = TINYPY_LIST_OBJECT(list)->items[right];
        TINYPY_LIST_OBJECT(list)->items[right] = left_value;
        left += 1U;
    }
    if (TINYPY_LIST_SIZE(list) > 1U) {
        TINYPY_LIST_OBJECT(list)->mutation_version += UINT64_C(1);
#if defined(TINYPY_CYCLE_DIAGNOSTICS)
        __tinypy_internal_cycle_diagnostics_list_reindex(vm, list);
#endif
    }
    tinypy_value_t *return_value_1 = tinypy_none_get(vm);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
typedef struct tinypy_list_sort_item_t {
    tinypy_value_t *value;
    tinypy_value_t *key;
} tinypy_list_sort_item_t;
//////////////////////////////////////////////////////////////////////////
typedef struct tinypy_list_sort_run_t {
    size_t base;
    size_t length;
} tinypy_list_sort_run_t;
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_container_call_items(tinypy_vm_t *vm, tinypy_value_t *callable, tinypy_value_t *const *items, size_t item_count, tinypy_error_t **out_error) {
    if (callable->type == &vm->types[TINYPY_VALUE_FUNCTION]) {
        tinypy_value_t *return_value_1 = tinypy_internal_eval_function_items(callable, items, item_count, NULL, out_error);
        return return_value_1;
    }
    if (callable->type == &vm->types[TINYPY_VALUE_METHOD]) {
        tinypy_method_object_t *method = TINYPY_METHOD_OBJECT(callable);

        if (method->self != NULL && method->function->type == &vm->types[TINYPY_VALUE_FUNCTION] && item_count <= 2U) {
            tinypy_value_t *bound_items[3];
            size_t index;

            bound_items[0] = method->self;
            for (index = 0U; index < item_count; ++index) {
                bound_items[index + 1U] = items[index];
            }
            tinypy_value_t *return_value_2 = tinypy_internal_eval_function_items(method->function, bound_items, item_count + 1U, NULL, out_error);
            return return_value_2;
        }
    }
    if (tinypy_internal_object_has_special(callable, "__call__", 8U) != 0) {
        tinypy_value_t *method = tinypy_object_get_attr(callable, "__call__", 8U, out_error);

        if (method == NULL) {
            return NULL;
        }
        if (method != callable && (method->type == &vm->types[TINYPY_VALUE_FUNCTION] || method->type == &vm->types[TINYPY_VALUE_METHOD])) {
            tinypy_value_t *result = __tinypy_container_call_items(vm, method, items, item_count, out_error);

            TINYPY_DECREF(method);
            return result;
        }
        TINYPY_DECREF(method);
    }
    tinypy_value_t *args = tinypy_tuple_from_items(vm, items, item_count);
    tinypy_value_t *result = tinypy_call(callable, args, NULL, out_error);

    TINYPY_DECREF(args);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_list_sort_compare(tinypy_vm_t *vm, tinypy_value_t *left, tinypy_value_t *right, tinypy_value_t *compare, tinypy_bool_t *out_less, tinypy_error_t **out_error) {
    int32_t less;

    if (compare != NULL && TINYPY_VALUE_KIND(compare) != TINYPY_VALUE_NONE) {
        tinypy_value_t *items[2] = {left, right};
        tinypy_value_t *result = __tinypy_container_call_items(vm, compare, items, 2U, out_error);
        int64_t order;

        if (result == NULL) {
            return TINYPY_FALSE;
        }
        if (TINYPY_VALUE_KIND(result) != TINYPY_VALUE_BOOL && TINYPY_VALUE_KIND(result) != TINYPY_VALUE_INTEGER) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "comparison function must return int", out_error);
            TINYPY_DECREF(result);
            return TINYPY_FALSE;
        }
        order = tinypy_integer_as_i64(result);
        TINYPY_DECREF(result);
        less = order < 0 ? INT32_C(1) : INT32_C(0);
    }
    else {
        less = tinypy_compare_bool(left, right, TINYPY_COMPARE_LESS, out_error);
        if (less < 0) {
            return TINYPY_FALSE;
        }
    }
    *out_less = less != 0 ? TINYPY_TRUE : TINYPY_FALSE;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_list_sort_reverse_items(tinypy_list_sort_item_t *items, size_t begin, size_t end) {
    while (begin < end && begin < --end) {
        tinypy_list_sort_item_t item = items[begin];

        items[begin] = items[end];
        items[end] = item;
        begin += 1U;
    }
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_list_sort_merge(tinypy_vm_t *vm, tinypy_list_sort_item_t *items, tinypy_list_sort_item_t *temporary, size_t left, size_t middle, size_t right, tinypy_value_t *compare, tinypy_error_t **out_error) {
    size_t left_size = middle - left;
    size_t right_size = right - middle;

    if (left_size <= right_size) {
        size_t left_index = 0U;
        size_t right_index = middle;
        size_t output = left;

        (void)memcpy(temporary, items + left, left_size * sizeof(*items));
        while (left_index < left_size && right_index < right) {
            tinypy_bool_t right_is_less;

            if (__tinypy_list_sort_compare(vm, items[right_index].key, temporary[left_index].key, compare, &right_is_less, out_error) == 0) {
                return TINYPY_FALSE;
            }
            if (right_is_less != 0) {
                items[output++] = items[right_index++];
            }
            else {
                items[output++] = temporary[left_index++];
            }
        }
        if (left_index < left_size) {
            (void)memcpy(items + output, temporary + left_index, (left_size - left_index) * sizeof(*items));
        }
    }
    else {
        size_t left_index = middle;
        size_t right_index = right_size;
        size_t output = right;

        (void)memcpy(temporary, items + middle, right_size * sizeof(*items));
        while (left_index > left && right_index > 0U) {
            tinypy_bool_t right_is_less;

            if (__tinypy_list_sort_compare(vm, temporary[right_index - 1U].key, items[left_index - 1U].key, compare, &right_is_less, out_error) == 0) {
                return TINYPY_FALSE;
            }
            if (right_is_less != 0) {
                items[--output] = items[--left_index];
            }
            else {
                items[--output] = temporary[--right_index];
            }
        }
        if (right_index > 0U) {
            (void)memcpy(items + left, temporary, right_index * sizeof(*items));
        }
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_list_sort_merge_at(tinypy_vm_t *vm, tinypy_list_sort_item_t *items, tinypy_list_sort_item_t *temporary, tinypy_list_sort_run_t *runs, size_t *run_count, size_t run_index, tinypy_value_t *compare, tinypy_error_t **out_error) {
    size_t middle = runs[run_index + 1U].base;
    size_t right = middle + runs[run_index + 1U].length;
    size_t index;

    if (__tinypy_list_sort_merge(vm, items, temporary, runs[run_index].base, middle, right, compare, out_error) == 0) {
        return TINYPY_FALSE;
    }
    runs[run_index].length += runs[run_index + 1U].length;
    for (index = run_index + 1U; index + 1U < *run_count; ++index) {
        runs[index] = runs[index + 1U];
    }
    *run_count -= 1U;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_list_sort_merge_collapse(tinypy_vm_t *vm, tinypy_list_sort_item_t *items, tinypy_list_sort_item_t *temporary, tinypy_list_sort_run_t *runs, size_t *run_count, tinypy_value_t *compare, tinypy_error_t **out_error) {
    while (*run_count > 1U) {
        size_t run_index = *run_count - 2U;

        if ((run_index > 0U && runs[run_index - 1U].length <= runs[run_index].length + runs[run_index + 1U].length)
            || (run_index > 1U && runs[run_index - 2U].length <= runs[run_index - 1U].length + runs[run_index].length)) {
            if (runs[run_index - 1U].length < runs[run_index + 1U].length) {
                run_index -= 1U;
            }
        }
        else if (runs[run_index].length > runs[run_index + 1U].length) {
            break;
        }
        if (__tinypy_list_sort_merge_at(vm, items, temporary, runs, run_count, run_index, compare, out_error) == 0) {
            return TINYPY_FALSE;
        }
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_list_sort_adaptive(tinypy_vm_t *vm, tinypy_list_sort_item_t *items, size_t size, tinypy_value_t *compare, tinypy_error_t **out_error) {
    tinypy_list_sort_item_t *temporary;
    tinypy_list_sort_run_t runs[128];
    size_t run_count = 0U;
    size_t begin = 0U;
    size_t temporary_size;

    if (size < 2U) {
        return TINYPY_TRUE;
    }
    temporary_size = size / 2U + size % 2U;
    temporary = (tinypy_list_sort_item_t *)tinypy_internal_vm_allocate(vm, temporary_size * sizeof(*temporary));
    while (begin < size) {
        size_t end = begin + 1U;

        if (end < size) {
            tinypy_bool_t descending;

            if (__tinypy_list_sort_compare(vm, items[end].key, items[end - 1U].key, compare, &descending, out_error) == 0) {
                goto error;
            }
            end += 1U;
            if (descending != 0) {
                while (end < size) {
                    if (__tinypy_list_sort_compare(vm, items[end].key, items[end - 1U].key, compare, &descending, out_error) == 0) {
                        goto error;
                    }
                    if (descending == 0) {
                        break;
                    }
                    end += 1U;
                }
                __tinypy_list_sort_reverse_items(items, begin, end);
            }
            else {
                while (end < size) {
                    tinypy_bool_t less;

                    if (__tinypy_list_sort_compare(vm, items[end].key, items[end - 1U].key, compare, &less, out_error) == 0) {
                        goto error;
                    }
                    if (less != 0) {
                        break;
                    }
                    end += 1U;
                }
            }
        }
        runs[run_count].base = begin;
        runs[run_count].length = end - begin;
        run_count += 1U;
        begin = end;
        if (__tinypy_list_sort_merge_collapse(vm, items, temporary, runs, &run_count, compare, out_error) == 0) {
            goto error;
        }
    }
    while (run_count > 1U) {
        if (__tinypy_list_sort_merge_at(vm, items, temporary, runs, &run_count, run_count - 2U, compare, out_error) == 0) {
            goto error;
        }
    }
    tinypy_internal_vm_deallocate(vm, temporary, temporary_size * sizeof(*temporary));
    return TINYPY_TRUE;

error:
    tinypy_internal_vm_deallocate(vm, temporary, temporary_size * sizeof(*temporary));
    return TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_list_sort_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *compare = NULL;
    tinypy_value_t *key_function = NULL;
    tinypy_bool_t reverse = TINYPY_FALSE;
    size_t size;
    tinypy_value_t **saved_items;
    size_t saved_allocated;
    uint64_t saved_version;
    tinypy_list_sort_item_t *items = NULL;
    tinypy_value_t **owned_keys = NULL;
    tinypy_bool_t has_keys;
    tinypy_bool_t sorted = TINYPY_TRUE;
    tinypy_bool_t modified;
    size_t argument_count;
    size_t key_count = 0U;
    size_t index;

    (void)user_data;
    if (__tinypy_container_argument_count(vm, args, 1U, 4U, out_error) == 0) {
        return NULL;
    }
    argument_count = TINYPY_TUPLE_SIZE(args);
    tinypy_value_t *list = TINYPY_TUPLE_GET(args, 0U);
    if (TINYPY_TUPLE_SIZE(args) >= 2U) {
        compare = TINYPY_TUPLE_GET(args, 1U);
    }
    if (TINYPY_TUPLE_SIZE(args) >= 3U) {
        key_function = TINYPY_TUPLE_GET(args, 2U);
    }
    if (TINYPY_TUPLE_SIZE(args) >= 4U) {
        tinypy_value_t *item_2 = TINYPY_TUPLE_GET(args, 3U);
        int32_t truth = tinypy_truth(item_2, out_error);
        if (truth < 0) {
            return NULL;
        }
        reverse = truth != 0 ? TINYPY_TRUE : TINYPY_FALSE;
    }
    if (kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U) {
        tinypy_dict_entry_t *iterator = TINYPY_DICT_ITERATOR_BEGIN(kwargs);
        tinypy_dict_entry_t *iterator_end = TINYPY_DICT_ITERATOR_END(kwargs);

        for (; iterator != iterator_end; ++iterator) {
            const uint8_t *name;
            size_t name_size;

            if (iterator->state != TINYPY_DICT_ENTRY_ACTIVE || TINYPY_VALUE_KIND(iterator->key) != TINYPY_VALUE_STRING) {
                continue;
            }
            name = tinypy_string_view(iterator->key, &name_size);
            if (name_size == 3U && memcmp(name, "cmp", 3U) == 0) {
                if (argument_count >= 2U) {
                    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "sort received multiple values for cmp", out_error);
                    return NULL;
                }
                compare = iterator->value;
            }
            else if (name_size == 3U && memcmp(name, "key", 3U) == 0) {
                if (argument_count >= 3U) {
                    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "sort received multiple values for key", out_error);
                    return NULL;
                }
                key_function = iterator->value;
            }
            else if (name_size == 7U && memcmp(name, "reverse", 7U) == 0) {
                if (argument_count >= 4U) {
                    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "sort received multiple values for reverse", out_error);
                    return NULL;
                }
                int32_t truth = tinypy_truth(iterator->value, out_error);
                if (truth < 0) {
                    return NULL;
                }
                reverse = truth != 0 ? TINYPY_TRUE : TINYPY_FALSE;
            }
            else {
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "sort received an unexpected keyword", out_error);
                return NULL;
            }
        }
    }
    size = TINYPY_LIST_SIZE(list);
    saved_items = TINYPY_LIST_OBJECT(list)->items;
    saved_allocated = TINYPY_LIST_OBJECT(list)->allocated;
    saved_version = TINYPY_LIST_OBJECT(list)->mutation_version;
#if defined(TINYPY_CYCLE_DIAGNOSTICS)
    __tinypy_internal_cycle_diagnostics_list_clear(vm, list);
#endif
    TINYPY_LIST_OBJECT(list)->items = NULL;
    TINYPY_LIST_OBJECT(list)->allocated = 0U;
    TINYPY_SIZED_SIZE(list) = 0U;
    has_keys = key_function != NULL && TINYPY_VALUE_KIND(key_function) != TINYPY_VALUE_NONE;
    if (size != 0U) {
        items = (tinypy_list_sort_item_t *)tinypy_internal_vm_allocate(vm, size * sizeof(*items));
        if (has_keys != 0) {
            owned_keys = (tinypy_value_t **)tinypy_internal_vm_allocate(vm, size * sizeof(*owned_keys));
        }
    }
    for (index = 0U; index < size; ++index) {
        items[index].value = saved_items[index];
        if (has_keys != 0) {
            owned_keys[index] = __tinypy_container_call_items(vm, key_function, &saved_items[index], 1U, out_error);
            if (owned_keys[index] == NULL) {
                sorted = TINYPY_FALSE;
                break;
            }
            items[index].key = owned_keys[index];
            key_count += 1U;
        }
        else {
            items[index].key = saved_items[index];
        }
    }
    if (sorted != 0 && reverse != 0) {
        __tinypy_list_sort_reverse_items(items, 0U, size);
    }
    if (sorted != 0) {
        sorted = __tinypy_list_sort_adaptive(vm, items, size, compare, out_error);
    }
    if (sorted != 0 && reverse != 0) {
        __tinypy_list_sort_reverse_items(items, 0U, size);
    }
    if (sorted != 0) {
        for (index = 0U; index < size; ++index) {
            saved_items[index] = items[index].value;
        }
    }
    if (has_keys != 0) {
        if (sorted != 0) {
            for (index = 0U; index < size; ++index) {
                TINYPY_DECREF(items[index].key);
            }
        }
        else {
            for (index = 0U; index < key_count; ++index) {
                TINYPY_DECREF(owned_keys[index]);
            }
        }
        if (owned_keys != NULL) {
            tinypy_internal_vm_deallocate(vm, owned_keys, size * sizeof(*owned_keys));
        }
    }
    if (items != NULL) {
        tinypy_internal_vm_deallocate(vm, items, size * sizeof(*items));
    }
    modified = TINYPY_LIST_OBJECT(list)->mutation_version != saved_version;
    tinypy_value_t **added_items = TINYPY_LIST_OBJECT(list)->items;
    size_t added_size = TINYPY_LIST_SIZE(list);
    size_t added_allocated = TINYPY_LIST_OBJECT(list)->allocated;
#if defined(TINYPY_CYCLE_DIAGNOSTICS)
    __tinypy_internal_cycle_diagnostics_list_clear(vm, list);
#endif
    TINYPY_LIST_OBJECT(list)->items = saved_items;
    TINYPY_LIST_OBJECT(list)->allocated = saved_allocated;
    TINYPY_SIZED_SIZE(list) = size;
    if (size > 1U) {
        TINYPY_LIST_OBJECT(list)->mutation_version += UINT64_C(1);
    }
#if defined(TINYPY_CYCLE_DIAGNOSTICS)
    __tinypy_internal_cycle_diagnostics_list_extend(vm, list, 0U, saved_items, size);
#endif
    for (index = 0U; index < added_size; ++index) {
        TINYPY_DECREF(added_items[index]);
    }
    if (added_items != NULL) {
        tinypy_internal_vm_deallocate(vm, added_items, added_allocated * sizeof(*added_items));
    }
    if (sorted == 0) {
        return NULL;
    }
    if (modified != 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "list modified during sort", out_error);
        return NULL;
    }
    tinypy_value_t *return_value_1 = tinypy_none_get(vm);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_dict_get_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *result;

    (void)user_data;
    if (__tinypy_container_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_container_argument_count(vm, args, 2U, 3U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *dict = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *key = TINYPY_TUPLE_GET(args, 1U);
    if (tinypy_internal_dict_get_optional_checked(vm, dict, key, &result, out_error) == 0) {
        return NULL;
    }
    if (result == NULL && TINYPY_TUPLE_SIZE(args) == 3U) {
        result = TINYPY_TUPLE_GET(args, 2U);
    }
    else if (result == NULL) {
        tinypy_value_t *return_value_1 = tinypy_none_get(vm);
        return return_value_1;
    }
    TINYPY_INCREF(result);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_dict_has_key_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_container_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_container_argument_count(vm, args, 2U, 2U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *item = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *item_2 = TINYPY_TUPLE_GET(args, 1U);
    tinypy_bool_t dict_contains;
    if (tinypy_internal_dict_contains_checked(vm, item, item_2, &dict_contains, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *return_value_1 = tinypy_bool_from_i32(vm, dict_contains);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_dict_snapshot(tinypy_value_t *dict_value, int32_t mode) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(dict_value);
    tinypy_value_t *result = tinypy_list_from_items(vm, NULL, 0U);
    tinypy_dict_entry_t *iterator = TINYPY_DICT_ITERATOR_BEGIN(dict_value);
    tinypy_dict_entry_t *iterator_end = TINYPY_DICT_ITERATOR_END(dict_value);

    for (; iterator != iterator_end; ++iterator) {
        tinypy_value_t *item;

        if (iterator->state != TINYPY_DICT_ENTRY_ACTIVE) {
            continue;
        }
        if (mode == INT32_C(0)) {
            item = iterator->key;
        }
        else if (mode == INT32_C(1)) {
            item = iterator->value;
        }
        else {
            tinypy_value_t *items[2] = {iterator->key, iterator->value};

            item = tinypy_tuple_from_items(vm, items, 2U);
            tinypy_list_append(result, item);
            TINYPY_DECREF(item);
            continue;
        }
        tinypy_list_append(result, item);
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_dict_list_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    int32_t mode = (int32_t)(intptr_t)user_data;

    if (__tinypy_container_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_container_argument_count(vm, args, 1U, 1U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *item = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *return_value_1 = __tinypy_dict_snapshot(item, mode);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_dict_iter_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    int32_t mode = (int32_t)(intptr_t)user_data;

    if (__tinypy_container_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_container_argument_count(vm, args, 1U, 1U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *item = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *return_value_1 = tinypy_internal_dict_iterator_new(item, mode);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_dict_view_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    if (__tinypy_container_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_container_argument_count(vm, args, 1U, 1U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *item = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *return_value_1 = tinypy_dict_view_new(item, (tinypy_dict_view_kind_e)(intptr_t)user_data);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_dict_clear_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_container_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_container_argument_count(vm, args, 1U, 1U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *item = TINYPY_TUPLE_GET(args, 0U);
    tinypy_dict_clear(item);
    tinypy_value_t *return_value_1 = tinypy_none_get(vm);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_dict_copy_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_dict_entry_t *iterator;
    tinypy_dict_entry_t *iterator_end;

    (void)user_data;
    if (__tinypy_container_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_container_argument_count(vm, args, 1U, 1U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *source = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *result = tinypy_dict_new(vm);
    iterator = TINYPY_DICT_ITERATOR_BEGIN(source);
    iterator_end = TINYPY_DICT_ITERATOR_END(source);
    for (; iterator != iterator_end; ++iterator) {
        if (iterator->state == TINYPY_DICT_ENTRY_ACTIVE) {
            if (tinypy_internal_dict_set_checked(vm, result, iterator->key, iterator->value, out_error) == 0) {
                TINYPY_DECREF(result);
                return NULL;
            }
        }
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_dict_update_from(tinypy_value_t *target, tinypy_value_t *source, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(target);

    if (TINYPY_VALUE_KIND(source) == TINYPY_VALUE_DICT) {
        tinypy_dict_entry_t *iterator = TINYPY_DICT_ITERATOR_BEGIN(source);
        tinypy_dict_entry_t *iterator_end = TINYPY_DICT_ITERATOR_END(source);

        for (; iterator != iterator_end; ++iterator) {
            if (iterator->state == TINYPY_DICT_ENTRY_ACTIVE) {
                if (tinypy_internal_dict_set_checked(vm, target, iterator->key, iterator->value, out_error) == 0) {
                    return TINYPY_FALSE;
                }
            }
        }
        return TINYPY_TRUE;
    }
    tinypy_value_t *pairs = __tinypy_container_collect(vm, source, out_error);
    tinypy_value_t *const *iterator;
    tinypy_value_t *const *iterator_end;

    if (pairs == NULL) {
        return TINYPY_FALSE;
    }
    iterator = TINYPY_LIST_ITERATOR_BEGIN(pairs);
    iterator_end = TINYPY_LIST_ITERATOR_END(pairs);
    for (; iterator != iterator_end; ++iterator) {
        tinypy_value_t *pair = *iterator;
        tinypy_value_type_e kind = TINYPY_VALUE_KIND(pair);
        size_t pair_size = kind == TINYPY_VALUE_TUPLE ? TINYPY_TUPLE_SIZE(pair) : (kind == TINYPY_VALUE_LIST ? TINYPY_LIST_SIZE(pair) : 0U);
        tinypy_value_t *key;
        tinypy_value_t *value;

        if (pair_size != 2U) {
            TINYPY_DECREF(pairs);
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "dictionary update item does not have length two", out_error);
            return TINYPY_FALSE;
        }
        key = kind == TINYPY_VALUE_TUPLE ? TINYPY_TUPLE_GET(pair, 0U) : TINYPY_LIST_GET(pair, 0U);
        value = kind == TINYPY_VALUE_TUPLE ? TINYPY_TUPLE_GET(pair, 1U) : TINYPY_LIST_GET(pair, 1U);
        if (tinypy_internal_dict_set_checked(vm, target, key, value, out_error) == 0) {
            TINYPY_DECREF(pairs);
            return TINYPY_FALSE;
        }
    }
    TINYPY_DECREF(pairs);
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_dict_update_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_container_argument_count(vm, args, 1U, 2U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *target = TINYPY_TUPLE_GET(args, 0U);
    tinypy_bool_t condition_3 = TINYPY_TUPLE_SIZE(args) == 2U;
    if (condition_3 != 0) {
        tinypy_value_t *item = TINYPY_TUPLE_GET(args, 1U);
        condition_3 = __tinypy_dict_update_from(target, item, out_error) == 0;
    }
    if (condition_3) {
        return NULL;
    }
    if (kwargs != NULL && __tinypy_dict_update_from(target, kwargs, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *return_value_1 = tinypy_none_get(vm);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_dict_setdefault_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *value;

    (void)user_data;
    if (__tinypy_container_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_container_argument_count(vm, args, 2U, 3U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *dict = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *key = TINYPY_TUPLE_GET(args, 1U);
    if (tinypy_internal_dict_get_optional_checked(vm, dict, key, &value, out_error) == 0) {
        return NULL;
    }
    if (value == NULL) {
        if (TINYPY_TUPLE_SIZE(args) == 3U) {
            value = TINYPY_TUPLE_GET(args, 2U);
        }
        else {
            value = tinypy_none_get(vm);
            if (tinypy_internal_dict_set_checked(vm, dict, key, value, out_error) == 0) {
                TINYPY_DECREF(value);
                return NULL;
            }
            return value;
        }
        if (tinypy_internal_dict_set_checked(vm, dict, key, value, out_error) == 0) {
            return NULL;
        }
    }
    TINYPY_INCREF(value);
    return value;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_dict_pop_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *value;
    size_t index;

    (void)user_data;
    if (__tinypy_container_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_container_argument_count(vm, args, 2U, 3U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *dict = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *key = TINYPY_TUPLE_GET(args, 1U);
    if (TINYPY_DICT_SIZE(dict) == 0U) {
        if (TINYPY_TUPLE_SIZE(args) == 3U) {
            value = TINYPY_TUPLE_GET(args, 2U);
            TINYPY_INCREF(value);
            return value;
        }
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_KEY, "dictionary key is absent", out_error);
        return NULL;
    }
    if (tinypy_internal_dict_get_optional_index_checked(vm, dict, key, &index, &value, out_error) == 0) {
        return NULL;
    }
    if (value == NULL) {
        if (TINYPY_TUPLE_SIZE(args) == 3U) {
            value = TINYPY_TUPLE_GET(args, 2U);
            TINYPY_INCREF(value);
            return value;
        }
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_KEY, "dictionary key is absent", out_error);
        return NULL;
    }
    tinypy_value_t *owned_key;
    tinypy_value_t *owned_value;

    (void)tinypy_internal_dict_delete_index(vm, dict, index, &owned_key, &owned_value);
    TINYPY_DECREF(owned_key);
    return owned_value;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_dict_popitem_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_dict_entry_t *iterator_begin;

    (void)user_data;
    if (__tinypy_container_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_container_argument_count(vm, args, 1U, 1U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *dict_value = TINYPY_TUPLE_GET(args, 0U);
    tinypy_dict_entry_t *iterator = TINYPY_DICT_ITERATOR_END(dict_value);
    iterator_begin = TINYPY_DICT_ITERATOR_BEGIN(dict_value);
    while (iterator != iterator_begin) {
        iterator -= 1;

        if (iterator->state == TINYPY_DICT_ENTRY_ACTIVE) {
            tinypy_value_t *items[2] = {iterator->key, iterator->value};
            tinypy_value_t *result = tinypy_tuple_from_items(vm, items, 2U);
            size_t index = (size_t)(iterator - iterator_begin);

            (void)tinypy_internal_dict_delete_index(vm, dict_value, index, NULL, NULL);
            return result;
        }
    }
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_KEY, "popitem(): dictionary is empty", out_error);
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_container_getitem_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_container_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_container_argument_count(vm, args, 2U, 2U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *item = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *item_2 = TINYPY_TUPLE_GET(args, 1U);
    tinypy_value_t *return_value_1 = tinypy_get_item(item, item_2, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_container_iter_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_container_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_container_argument_count(vm, args, 1U, 1U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *item = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *return_value_1 = tinypy_iter(item, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_container_add_method(tinypy_type_t *type, const char *name, size_t name_size, tinypy_native_function_callback_t callback, void *user_data) {
    tinypy_value_t *function = tinypy_native_function_new(type->vm, name, name_size, callback, user_data, NULL);

    tinypy_type_set_attr(type, name, name_size, function);
    TINYPY_DECREF(function);
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_initialize_container_types(tinypy_vm_t *vm) {
    __tinypy_container_add_method(&vm->types[TINYPY_VALUE_LIST], "append", 6U, __tinypy_list_append_method, NULL);
    __tinypy_container_add_method(&vm->types[TINYPY_VALUE_LIST], "extend", 6U, __tinypy_list_extend_method, NULL);
    __tinypy_container_add_method(&vm->types[TINYPY_VALUE_LIST], "insert", 6U, __tinypy_list_insert_method, NULL);
    __tinypy_container_add_method(&vm->types[TINYPY_VALUE_LIST], "pop", 3U, __tinypy_list_pop_method, NULL);
    __tinypy_container_add_method(&vm->types[TINYPY_VALUE_LIST], "remove", 6U, __tinypy_list_remove_method, NULL);
    __tinypy_container_add_method(&vm->types[TINYPY_VALUE_LIST], "count", 5U, __tinypy_list_count_method, NULL);
    __tinypy_container_add_method(&vm->types[TINYPY_VALUE_LIST], "index", 5U, __tinypy_list_index_method, NULL);
    __tinypy_container_add_method(&vm->types[TINYPY_VALUE_LIST], "reverse", 7U, __tinypy_list_reverse_method, NULL);
    __tinypy_container_add_method(&vm->types[TINYPY_VALUE_LIST], "sort", 4U, __tinypy_list_sort_method, NULL);
    __tinypy_container_add_method(&vm->types[TINYPY_VALUE_LIST], "__iter__", 8U, __tinypy_container_iter_method, NULL);
    __tinypy_container_add_method(&vm->types[TINYPY_VALUE_DICT], "get", 3U, __tinypy_dict_get_method, NULL);
    __tinypy_container_add_method(&vm->types[TINYPY_VALUE_DICT], "has_key", 7U, __tinypy_dict_has_key_method, NULL);
    __tinypy_container_add_method(&vm->types[TINYPY_VALUE_DICT], "keys", 4U, __tinypy_dict_list_method, (void *)(intptr_t)0);
    __tinypy_container_add_method(&vm->types[TINYPY_VALUE_DICT], "values", 6U, __tinypy_dict_list_method, (void *)(intptr_t)1);
    __tinypy_container_add_method(&vm->types[TINYPY_VALUE_DICT], "items", 5U, __tinypy_dict_list_method, (void *)(intptr_t)2);
    __tinypy_container_add_method(&vm->types[TINYPY_VALUE_DICT], "iterkeys", 8U, __tinypy_dict_iter_method, (void *)(intptr_t)0);
    __tinypy_container_add_method(&vm->types[TINYPY_VALUE_DICT], "itervalues", 10U, __tinypy_dict_iter_method, (void *)(intptr_t)1);
    __tinypy_container_add_method(&vm->types[TINYPY_VALUE_DICT], "iteritems", 9U, __tinypy_dict_iter_method, (void *)(intptr_t)2);
    __tinypy_container_add_method(&vm->types[TINYPY_VALUE_DICT], "viewkeys", 8U, __tinypy_dict_view_method, (void *)(intptr_t)TINYPY_DICT_VIEW_KEYS);
    __tinypy_container_add_method(&vm->types[TINYPY_VALUE_DICT], "viewvalues", 10U, __tinypy_dict_view_method, (void *)(intptr_t)TINYPY_DICT_VIEW_VALUES);
    __tinypy_container_add_method(&vm->types[TINYPY_VALUE_DICT], "viewitems", 9U, __tinypy_dict_view_method, (void *)(intptr_t)TINYPY_DICT_VIEW_ITEMS);
    __tinypy_container_add_method(&vm->types[TINYPY_VALUE_DICT], "clear", 5U, __tinypy_dict_clear_method, NULL);
    __tinypy_container_add_method(&vm->types[TINYPY_VALUE_DICT], "copy", 4U, __tinypy_dict_copy_method, NULL);
    __tinypy_container_add_method(&vm->types[TINYPY_VALUE_DICT], "update", 6U, __tinypy_dict_update_method, NULL);
    __tinypy_container_add_method(&vm->types[TINYPY_VALUE_DICT], "setdefault", 10U, __tinypy_dict_setdefault_method, NULL);
    __tinypy_container_add_method(&vm->types[TINYPY_VALUE_DICT], "pop", 3U, __tinypy_dict_pop_method, NULL);
    __tinypy_container_add_method(&vm->types[TINYPY_VALUE_DICT], "popitem", 7U, __tinypy_dict_popitem_method, NULL);
    __tinypy_container_add_method(&vm->types[TINYPY_VALUE_DICT], "__getitem__", 11U, __tinypy_container_getitem_method, NULL);
}
