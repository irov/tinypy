#include "internal.h"

#include <math.h>
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
static size_t __tinypy_container_sequence_size(tinypy_value_t *sequence, tinypy_value_type_e kind) {
    size_t return_value_1 = kind == TINYPY_VALUE_TUPLE ? TINYPY_TUPLE_SIZE(sequence) : TINYPY_LIST_SIZE(sequence);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_container_sequence_get(tinypy_value_t *sequence, tinypy_value_type_e kind, size_t index) {
    tinypy_value_t *return_value_1 = kind == TINYPY_VALUE_TUPLE ? TINYPY_TUPLE_GET(sequence, index) : TINYPY_LIST_GET(sequence, index);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_container_collect(tinypy_vm_t *vm, tinypy_value_t *iterable, tinypy_error_t **out_error) {
    tinypy_value_t *iterator = tinypy_iter(iterable, out_error);
    tinypy_error_t *iteration_error = NULL;

    if (iterator == NULL) {
        return NULL;
    }
    tinypy_value_t *result = tinypy_list_from_items(vm, NULL, 0U);
    tinypy_internal_list_reserve(vm, result, tinypy_internal_iterable_size_hint(iterable));
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
static tinypy_value_t *__tinypy_list_inplace_add_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_container_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_container_argument_count(vm, args, 2U, 2U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *list = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *collected = __tinypy_container_collect(vm, TINYPY_TUPLE_GET(args, 1U), out_error);
    if (collected == NULL) {
        return NULL;
    }
    size_t size = TINYPY_LIST_SIZE(list);
    size_t extension_size = TINYPY_LIST_SIZE(collected);
    if (extension_size > SIZE_MAX - size || size + extension_size >= (size_t)PTRDIFF_MAX || size + extension_size > SIZE_MAX / sizeof(tinypy_value_t *)) {
        TINYPY_DECREF(collected);
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "extended list is too large", out_error);
        return NULL;
    }
    tinypy_list_extend(list, TINYPY_LIST_OBJECT(collected)->items, extension_size);
    TINYPY_DECREF(collected);
    TINYPY_INCREF(list);
    return list;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_list_inplace_multiply_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    int64_t count;

    (void)user_data;
    if (__tinypy_container_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_container_argument_count(vm, args, 2U, 2U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *list = TINYPY_TUPLE_GET(args, 0U);
    if (tinypy_internal_index_as_i64(TINYPY_TUPLE_GET(args, 1U), &count, TINYPY_FALSE, out_error) == 0) {
        return NULL;
    }
    size_t unit_size = TINYPY_LIST_SIZE(list);
    if (count <= 0) {
        tinypy_list_clear(list);
    }
    else if (count > 1 && unit_size != 0U) {
        if ((uint64_t)count > (uint64_t)(SIZE_MAX / unit_size)) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "repeated list is too large", out_error);
            return NULL;
        }
        size_t total_size = unit_size * (size_t)count;
        if (total_size >= (size_t)PTRDIFF_MAX || total_size > SIZE_MAX / sizeof(tinypy_value_t *)) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "repeated list is too large", out_error);
            return NULL;
        }
        tinypy_internal_list_reserve(vm, list, total_size);
        tinypy_value_t **items = TINYPY_LIST_OBJECT(list)->items;
        size_t index;
        for (index = unit_size; index < total_size; ++index) {
            items[index] = items[index % unit_size];
            TINYPY_INCREF(items[index]);
        }
        TINYPY_SIZED_SIZE(list) = total_size;
        TINYPY_LIST_OBJECT(list)->mutation_version += UINT64_C(1);
#if defined(TINYPY_CYCLE_DIAGNOSTICS)
        __tinypy_internal_cycle_diagnostics_list_extend(vm, list, unit_size, items + unit_size, total_size - unit_size);
#endif
    }
    TINYPY_INCREF(list);
    return list;
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
static tinypy_value_t *__tinypy_sequence_count_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    int64_t count = 0;
    size_t index = 0U;

    (void)user_data;
    if (__tinypy_container_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_container_argument_count(vm, args, 2U, 2U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *sequence = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *needle = TINYPY_TUPLE_GET(args, 1U);
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(sequence);
    while (index < __tinypy_container_sequence_size(sequence, kind)) {
        tinypy_value_t *item = __tinypy_container_sequence_get(sequence, kind, index);
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
static tinypy_value_t *__tinypy_sequence_index_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    int64_t start = 0;
    int64_t stop;
    int64_t size;
    int64_t index;

    (void)user_data;
    if (__tinypy_container_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_container_argument_count(vm, args, 2U, 4U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *sequence = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *needle = TINYPY_TUPLE_GET(args, 1U);
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(sequence);
    size = (int64_t)__tinypy_container_sequence_size(sequence, kind);
    stop = size;
    tinypy_bool_t condition = TINYPY_TUPLE_SIZE(args) >= 3U;
    if (condition != 0) {
        tinypy_value_t *item_2 = TINYPY_TUPLE_GET(args, 2U);
        condition = tinypy_internal_index_as_i64(item_2, &start, TINYPY_TRUE, out_error) == 0;
    }
    if (condition) {
        return NULL;
    }
    tinypy_bool_t condition_2 = TINYPY_TUPLE_SIZE(args) == 4U;
    if (condition_2 != 0) {
        tinypy_value_t *item_2 = TINYPY_TUPLE_GET(args, 3U);
        condition_2 = tinypy_internal_index_as_i64(item_2, &stop, TINYPY_TRUE, out_error) == 0;
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
    for (index = start; index < stop && index < (int64_t)__tinypy_container_sequence_size(sequence, kind); ++index) {
        tinypy_value_t *item = __tinypy_container_sequence_get(sequence, kind, (size_t)index);
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
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE,
        kind == TINYPY_VALUE_TUPLE ? "tuple.index(x): x not in tuple" : "list.index(x): x not in list", out_error);
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
        tinypy_value_t *method = tinypy_internal_object_get_special(callable, "__call__", 8U, out_error);

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

            if (!TINYPY_DICT_ENTRY_IS_ACTIVE(iterator) || TINYPY_VALUE_KIND(iterator->key) != TINYPY_VALUE_STRING) {
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
static tinypy_value_t *__tinypy_dict_fromkeys_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *result;
    tinypy_value_t *iterator;
    tinypy_value_t *value;
    tinypy_error_t *iteration_error = NULL;

    (void)user_data;
    if (__tinypy_container_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_container_argument_count(vm, args, 2U, 3U, out_error) == 0) {
        return NULL;
    }
    if (TINYPY_VALUE_KIND(TINYPY_TUPLE_GET(args, 0U)) != TINYPY_VALUE_TYPE ||
        tinypy_type_is_subtype((tinypy_type_t *)TINYPY_TUPLE_GET(args, 0U), &vm->types[TINYPY_VALUE_DICT]) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "dict.fromkeys() requires a dict type", out_error);
        return NULL;
    }
    tinypy_value_t *constructor_args = tinypy_tuple_from_items(vm, NULL, 0U);
    result = tinypy_call(TINYPY_TUPLE_GET(args, 0U), constructor_args, NULL, out_error);
    TINYPY_DECREF(constructor_args);
    if (result == NULL) {
        return NULL;
    }
    if (TINYPY_VALUE_KIND(result) != TINYPY_VALUE_DICT) {
        TINYPY_DECREF(result);
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "dict.fromkeys() constructor returned a non-dict", out_error);
        return NULL;
    }
    tinypy_bool_t exact_dict = result->type == &vm->types[TINYPY_VALUE_DICT] ? TINYPY_TRUE : TINYPY_FALSE;
    if (exact_dict != 0) {
        tinypy_internal_dict_reserve(vm, result, tinypy_internal_iterable_size_hint(TINYPY_TUPLE_GET(args, 1U)));
    }
    value = TINYPY_TUPLE_SIZE(args) == 3U ? TINYPY_TUPLE_GET(args, 2U) : &vm->none_object.base;
    iterator = tinypy_iter(TINYPY_TUPLE_GET(args, 1U), out_error);
    if (iterator == NULL) {
        TINYPY_DECREF(result);
        return NULL;
    }
    for (;;) {
        tinypy_value_t *key = tinypy_next(iterator, &iteration_error);

        if (key == NULL) {
            break;
        }
        tinypy_bool_t assigned = exact_dict != 0
                                     ? tinypy_internal_dict_set_checked(vm, result, key, value, out_error)
                                     : tinypy_set_item(result, key, value, out_error);
        if (assigned == 0) {
            TINYPY_DECREF(key);
            TINYPY_DECREF(iterator);
            TINYPY_DECREF(result);
            return NULL;
        }
        TINYPY_DECREF(key);
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

        if (!TINYPY_DICT_ENTRY_IS_ACTIVE(iterator)) {
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
    tinypy_internal_dict_reserve(vm, result, TINYPY_DICT_SIZE(source));
    iterator = TINYPY_DICT_ITERATOR_BEGIN(source);
    iterator_end = TINYPY_DICT_ITERATOR_END(source);
    for (; iterator != iterator_end; ++iterator) {
        if (TINYPY_DICT_ENTRY_IS_ACTIVE(iterator)) {
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
    tinypy_bool_t return_value_1 = tinypy_internal_dict_update_from(target, source, out_error);
    return return_value_1;
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

        if (TINYPY_DICT_ENTRY_IS_ACTIVE(iterator)) {
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
    tinypy_value_t *return_value_1 = tinypy_internal_get_item_builtin(item, item_2, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_container_setitem_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_container_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_container_argument_count(vm, args, 3U, 3U, out_error) == 0) {
        return NULL;
    }
    if (tinypy_internal_set_item_builtin(TINYPY_TUPLE_GET(args, 0U), TINYPY_TUPLE_GET(args, 1U), TINYPY_TUPLE_GET(args, 2U), out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *return_value_1 = tinypy_none_get(vm);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_container_delitem_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_container_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_container_argument_count(vm, args, 2U, 2U, out_error) == 0) {
        return NULL;
    }
    if (tinypy_internal_delete_item_builtin(TINYPY_TUPLE_GET(args, 0U), TINYPY_TUPLE_GET(args, 1U), out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *return_value_1 = tinypy_none_get(vm);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_container_contains_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_container_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_container_argument_count(vm, args, 2U, 2U, out_error) == 0) {
        return NULL;
    }
    int32_t contained = tinypy_internal_contains_builtin(TINYPY_TUPLE_GET(args, 0U), TINYPY_TUPLE_GET(args, 1U), out_error);
    tinypy_value_t *return_value_1 = contained < 0 ? NULL : tinypy_bool_from_i32(vm, contained);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_container_binary_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    intptr_t mode = (intptr_t)user_data;

    if (__tinypy_container_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_container_argument_count(vm, args, 2U, 2U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *left = TINYPY_TUPLE_GET(args, mode >= 100 ? 1U : 0U);
    tinypy_value_t *right = TINYPY_TUPLE_GET(args, mode >= 100 ? 0U : 1U);
    if (mode >= 100) {
        mode -= 100;
    }
    tinypy_value_type_e left_kind = TINYPY_VALUE_KIND(left);
    tinypy_value_type_e right_kind = TINYPY_VALUE_KIND(right);
    tinypy_bool_t left_numeric = left_kind == TINYPY_VALUE_BOOL || left_kind == TINYPY_VALUE_INTEGER || left_kind == TINYPY_VALUE_LONG || left_kind == TINYPY_VALUE_FLOAT || left_kind == TINYPY_VALUE_COMPLEX;
    tinypy_bool_t right_numeric = right_kind == TINYPY_VALUE_BOOL || right_kind == TINYPY_VALUE_INTEGER || right_kind == TINYPY_VALUE_LONG || right_kind == TINYPY_VALUE_FLOAT || right_kind == TINYPY_VALUE_COMPLEX;
    tinypy_value_type_e self_kind = TINYPY_VALUE_KIND(TINYPY_TUPLE_GET(args, 0U));
    tinypy_bool_t self_numeric = self_kind == TINYPY_VALUE_BOOL || self_kind == TINYPY_VALUE_INTEGER || self_kind == TINYPY_VALUE_LONG || self_kind == TINYPY_VALUE_FLOAT || self_kind == TINYPY_VALUE_COMPLEX;
    tinypy_bool_t left_sequence = left_kind == TINYPY_VALUE_STRING || left_kind == TINYPY_VALUE_UNICODE || left_kind == TINYPY_VALUE_TUPLE || left_kind == TINYPY_VALUE_LIST;
    tinypy_bool_t right_sequence = right_kind == TINYPY_VALUE_STRING || right_kind == TINYPY_VALUE_UNICODE || right_kind == TINYPY_VALUE_TUPLE || right_kind == TINYPY_VALUE_LIST;

    if (self_numeric != 0 && (left_numeric == 0 || right_numeric == 0)) {
        tinypy_value_t *result = &vm->not_implemented_object.base;
        TINYPY_INCREF(result);
        return result;
    }
    if (mode == 0 && (left_sequence != 0 || right_sequence != 0)) {
        tinypy_bool_t compatible_text = (left_kind == TINYPY_VALUE_STRING || left_kind == TINYPY_VALUE_UNICODE) && (right_kind == TINYPY_VALUE_STRING || right_kind == TINYPY_VALUE_UNICODE);
        tinypy_bool_t compatible_sequence = (left_kind == TINYPY_VALUE_LIST || left_kind == TINYPY_VALUE_TUPLE) && left_kind == right_kind;

        if (compatible_text == 0 && compatible_sequence == 0) {
            tinypy_value_t *result = &vm->not_implemented_object.base;
            TINYPY_INCREF(result);
            return result;
        }
    }
    if (mode == 2 && (left_sequence != 0 || right_sequence != 0)) {
        tinypy_value_type_e multiplier = left_sequence != 0 ? right_kind : left_kind;

        if (multiplier != TINYPY_VALUE_BOOL && multiplier != TINYPY_VALUE_INTEGER && multiplier != TINYPY_VALUE_LONG) {
            tinypy_value_t *result = &vm->not_implemented_object.base;
            TINYPY_INCREF(result);
            return result;
        }
    }
    tinypy_value_t *result = tinypy_internal_operator_builtin(left, right, (int32_t)mode, out_error);

    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_container_compare_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    if (__tinypy_container_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_container_argument_count(vm, args, 2U, 2U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *return_value_1 = tinypy_internal_compare_builtin_value(TINYPY_TUPLE_GET(args, 0U), TINYPY_TUPLE_GET(args, 1U), (tinypy_compare_operation_e)(intptr_t)user_data, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_container_cmp_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_type_e expected = (tinypy_value_type_e)(intptr_t)user_data;
    tinypy_value_t *left;
    tinypy_value_t *right;
    int32_t equal;
    int32_t less;

    if (__tinypy_container_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_container_argument_count(vm, args, 2U, 2U, out_error) == 0) {
        return NULL;
    }
    left = TINYPY_TUPLE_GET(args, 0U);
    right = TINYPY_TUPLE_GET(args, 1U);
    if (expected == TINYPY_VALUE_DICT) {
        if (TINYPY_VALUE_KIND(left) != TINYPY_VALUE_DICT || TINYPY_VALUE_KIND(right) != TINYPY_VALUE_DICT) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "dict comparison requires two dictionaries", out_error);
            return NULL;
        }
        equal = tinypy_compare_bool(left, right, TINYPY_COMPARE_EQUAL, out_error);
        if (equal < 0) {
            return NULL;
        }
        if (equal != 0) {
            tinypy_value_t *result = tinypy_integer_from_i64(vm, INT64_C(0));

            return result;
        }
        less = tinypy_compare_bool(left, right, TINYPY_COMPARE_LESS, out_error);
        if (less < 0) {
            return NULL;
        }
        tinypy_value_t *result = tinypy_integer_from_i64(vm, less != 0 ? INT64_C(-1) : INT64_C(1));

        return result;
    }
    if ((TINYPY_VALUE_KIND(left) != TINYPY_VALUE_SET && TINYPY_VALUE_KIND(left) != TINYPY_VALUE_FROZENSET) ||
        (TINYPY_VALUE_KIND(right) != TINYPY_VALUE_SET && TINYPY_VALUE_KIND(right) != TINYPY_VALUE_FROZENSET)) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "set comparison requires two sets", out_error);
        return NULL;
    }
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "cannot compare sets using cmp()", out_error);
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_container_unary_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    intptr_t mode = (intptr_t)user_data;

    if (__tinypy_container_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_container_argument_count(vm, args, 1U, 1U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *result = tinypy_internal_unary_builtin(TINYPY_TUPLE_GET(args, 0U), (int32_t)mode, out_error);

    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_container_conversion_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    intptr_t mode = (intptr_t)user_data;

    if (__tinypy_container_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_container_argument_count(vm, args, 1U, 1U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *value = TINYPY_TUPLE_GET(args, 0U);
    if (mode == 3) {
        if (TINYPY_VALUE_KIND(value) != TINYPY_VALUE_INTEGER && TINYPY_VALUE_KIND(value) != TINYPY_VALUE_LONG && TINYPY_VALUE_KIND(value) != TINYPY_VALUE_BOOL) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "__index__ requires an integer", out_error);
            return NULL;
        }
        TINYPY_INCREF(value);
        return value;
    }
    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_COMPLEX) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "cannot convert complex to a real number", out_error);
        return NULL;
    }
    tinypy_value_t *arguments = tinypy_tuple_from_items(vm, &value, 1U);
    tinypy_type_t *target = mode == 0 ? &vm->types[TINYPY_VALUE_INTEGER] : (mode == 1 ? &vm->types[TINYPY_VALUE_LONG] : &vm->types[TINYPY_VALUE_FLOAT]);
    tinypy_value_t *result = target->create(target, arguments, NULL, out_error);
    TINYPY_DECREF(arguments);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_container_nonzero_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_container_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_container_argument_count(vm, args, 1U, 1U, out_error) == 0) {
        return NULL;
    }
    int32_t truth = tinypy_internal_truth_builtin(TINYPY_TUPLE_GET(args, 0U), out_error);
    tinypy_value_t *return_value_1 = truth < 0 ? NULL : tinypy_bool_from_i32(vm, truth);
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
    tinypy_value_t *return_value_1 = tinypy_internal_iter_builtin(item, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_container_len_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    size_t size;

    (void)user_data;
    if (__tinypy_container_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_container_argument_count(vm, args, 1U, 1U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *value = TINYPY_TUPLE_GET(args, 0U);
    size = TINYPY_VALUE_KIND(value) == TINYPY_VALUE_DICT ? TINYPY_DICT_SIZE(value) : TINYPY_SIZED_SIZE(value);
    tinypy_value_t *return_value_1 = tinypy_integer_from_i64(vm, (int64_t)size);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_container_repr_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_container_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_container_argument_count(vm, args, 1U, 1U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *value = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *return_value_1 = tinypy_internal_object_repr_builtin(value, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_container_str_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_container_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_container_argument_count(vm, args, 1U, 1U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *return_value_1 = tinypy_internal_object_str_builtin(TINYPY_TUPLE_GET(args, 0U), out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_container_hash_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_container_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_container_argument_count(vm, args, 1U, 1U, out_error) == 0) {
        return NULL;
    }
    tinypy_hash_t hash = tinypy_internal_hash_builtin_value(TINYPY_TUPLE_GET(args, 0U), out_error);
    if (tinypy_vm_has_error(vm) != 0) {
        return NULL;
    }
    tinypy_value_t *return_value_1 = tinypy_integer_from_i64(vm, hash);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_container_format_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_bool_t result_unicode;

    (void)user_data;
    if (__tinypy_container_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_container_argument_count(vm, args, 2U, 2U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *spec = TINYPY_TUPLE_GET(args, 1U);
    tinypy_value_type_e spec_kind = TINYPY_VALUE_KIND(spec);
    if (spec_kind != TINYPY_VALUE_STRING && spec_kind != TINYPY_VALUE_UNICODE) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "format specification must be a string or unicode", out_error);
        return NULL;
    }
    tinypy_value_t *return_value_1 = tinypy_internal_string_format_builtin_value(vm, TINYPY_TUPLE_GET(args, 0U), 0, TINYPY_TEXT_BYTES(spec), TINYPY_TEXT_BYTE_SIZE(spec), spec_kind == TINYPY_VALUE_UNICODE ? TINYPY_TRUE : TINYPY_FALSE, &result_unicode, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_container_add_method(tinypy_type_t *type, const char *name, size_t name_size, tinypy_native_function_callback_t callback, void *user_data) {
    tinypy_value_t *function = tinypy_native_function_new(type->vm, name, name_size, callback, user_data, NULL);

    tinypy_type_set_attr(type, name, name_size, function);
    TINYPY_DECREF(function);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_container_add_class_method(tinypy_type_t *type, const char *name, size_t name_size, tinypy_native_function_callback_t callback) {
    tinypy_value_t *function = tinypy_native_function_new(type->vm, name, name_size, callback, NULL, NULL);
    tinypy_value_t *descriptor = tinypy_class_method_new(function);

    tinypy_type_set_attr(type, name, name_size, descriptor);
    TINYPY_DECREF(descriptor);
    TINYPY_DECREF(function);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_container_getslice_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_container_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_container_argument_count(vm, args, 3U, 3U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *slice = tinypy_slice_new(vm, TINYPY_TUPLE_GET(args, 1U), TINYPY_TUPLE_GET(args, 2U), NULL);
    tinypy_value_t *result = tinypy_internal_get_item_builtin(TINYPY_TUPLE_GET(args, 0U), slice, out_error);
    TINYPY_DECREF(slice);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_container_setslice_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_container_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_container_argument_count(vm, args, 4U, 4U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *slice = tinypy_slice_new(vm, TINYPY_TUPLE_GET(args, 1U), TINYPY_TUPLE_GET(args, 2U), NULL);
    tinypy_bool_t assigned = tinypy_internal_set_item_builtin(TINYPY_TUPLE_GET(args, 0U), slice, TINYPY_TUPLE_GET(args, 3U), out_error);
    TINYPY_DECREF(slice);
    if (assigned == 0) {
        return NULL;
    }
    tinypy_value_t *return_value_1 = tinypy_none_get(vm);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_container_delslice_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_container_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_container_argument_count(vm, args, 3U, 3U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *slice = tinypy_slice_new(vm, TINYPY_TUPLE_GET(args, 1U), TINYPY_TUPLE_GET(args, 2U), NULL);
    tinypy_bool_t deleted = tinypy_internal_delete_item_builtin(TINYPY_TUPLE_GET(args, 0U), slice, out_error);
    TINYPY_DECREF(slice);
    if (deleted == 0) {
        return NULL;
    }
    tinypy_value_t *return_value_1 = tinypy_none_get(vm);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_container_reversed_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_container_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_container_argument_count(vm, args, 1U, 1U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *return_value_1 = tinypy_internal_reversed_new(TINYPY_TUPLE_GET(args, 0U), out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_container_getnewargs_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_container_no_keywords(vm, kwargs, out_error) == 0 || __tinypy_container_argument_count(vm, args, 1U, 1U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *self = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *slice = tinypy_slice_new(vm, NULL, NULL, NULL);
    tinypy_value_t *copy = tinypy_internal_get_item_builtin(self, slice, out_error);
    TINYPY_DECREF(slice);
    if (copy == NULL) {
        return NULL;
    }
    tinypy_value_t *result = tinypy_tuple_from_items(vm, &copy, 1U);
    TINYPY_DECREF(copy);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_container_add_comparisons(tinypy_type_t *type) {
    __tinypy_container_add_method(type, "__lt__", 6U, __tinypy_container_compare_method, (void *)(intptr_t)TINYPY_COMPARE_LESS);
    __tinypy_container_add_method(type, "__le__", 6U, __tinypy_container_compare_method, (void *)(intptr_t)TINYPY_COMPARE_LESS_EQUAL);
    __tinypy_container_add_method(type, "__eq__", 6U, __tinypy_container_compare_method, (void *)(intptr_t)TINYPY_COMPARE_EQUAL);
    __tinypy_container_add_method(type, "__ne__", 6U, __tinypy_container_compare_method, (void *)(intptr_t)TINYPY_COMPARE_NOT_EQUAL);
    __tinypy_container_add_method(type, "__gt__", 6U, __tinypy_container_compare_method, (void *)(intptr_t)TINYPY_COMPARE_GREATER);
    __tinypy_container_add_method(type, "__ge__", 6U, __tinypy_container_compare_method, (void *)(intptr_t)TINYPY_COMPARE_GREATER_EQUAL);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_container_add_sequence_protocol(tinypy_type_t *type, tinypy_bool_t mutable) {
    __tinypy_container_add_method(type, "__iter__", 8U, __tinypy_container_iter_method, NULL);
    __tinypy_container_add_method(type, "__len__", 7U, __tinypy_container_len_method, NULL);
    __tinypy_container_add_method(type, "__getitem__", 11U, __tinypy_container_getitem_method, NULL);
    __tinypy_container_add_method(type, "__contains__", 12U, __tinypy_container_contains_method, NULL);
    __tinypy_container_add_method(type, "__repr__", 8U, __tinypy_container_repr_method, NULL);
    __tinypy_container_add_method(type, "__str__", 7U, __tinypy_container_str_method, NULL);
    if (mutable != 0) {
        __tinypy_container_add_method(type, "__setitem__", 11U, __tinypy_container_setitem_method, NULL);
        __tinypy_container_add_method(type, "__delitem__", 11U, __tinypy_container_delitem_method, NULL);
    }
    __tinypy_container_add_comparisons(type);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_container_add_sequence_arithmetic(tinypy_type_t *type, tinypy_bool_t modulo) {
    __tinypy_container_add_method(type, "__add__", 7U, __tinypy_container_binary_method, (void *)(intptr_t)0);
    __tinypy_container_add_method(type, "__mul__", 7U, __tinypy_container_binary_method, (void *)(intptr_t)2);
    __tinypy_container_add_method(type, "__rmul__", 8U, __tinypy_container_binary_method, (void *)(intptr_t)102);
    if (modulo != 0) {
        __tinypy_container_add_method(type, "__mod__", 7U, __tinypy_container_binary_method, (void *)(intptr_t)6);
        __tinypy_container_add_method(type, "__rmod__", 8U, __tinypy_container_binary_method, (void *)(intptr_t)106);
    }
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_container_add_numeric_protocol(tinypy_type_t *type, tinypy_bool_t integer) {
    static const struct {
        const char *name;
        size_t size;
        intptr_t mode;
    } operations[] = {
        {"__add__", 7U, 0}, {"__radd__", 8U, 100},
        {"__sub__", 7U, 1}, {"__rsub__", 8U, 101},
        {"__mul__", 7U, 2}, {"__rmul__", 8U, 102},
        {"__div__", 7U, 3}, {"__rdiv__", 8U, 103},
        {"__floordiv__", 12U, 4}, {"__rfloordiv__", 13U, 104},
        {"__truediv__", 11U, 5}, {"__rtruediv__", 12U, 105},
        {"__mod__", 7U, 6}, {"__rmod__", 8U, 106},
        {"__divmod__", 10U, 7}, {"__rdivmod__", 11U, 107},
        {"__pow__", 7U, 8}, {"__rpow__", 8U, 108}
    };
    size_t index;

    for (index = 0U; index < sizeof(operations) / sizeof(operations[0]); ++index) {
        __tinypy_container_add_method(type, operations[index].name, operations[index].size, __tinypy_container_binary_method, (void *)operations[index].mode);
    }
    if (integer != 0) {
        static const struct {
            const char *name;
            size_t size;
            intptr_t mode;
        } integer_operations[] = {
            {"__lshift__", 10U, 9}, {"__rlshift__", 11U, 109},
            {"__rshift__", 10U, 10}, {"__rrshift__", 11U, 110},
            {"__and__", 7U, 11}, {"__rand__", 8U, 111},
            {"__xor__", 7U, 12}, {"__rxor__", 8U, 112},
            {"__or__", 6U, 13}, {"__ror__", 7U, 113}
        };

        for (index = 0U; index < sizeof(integer_operations) / sizeof(integer_operations[0]); ++index) {
            __tinypy_container_add_method(type, integer_operations[index].name, integer_operations[index].size, __tinypy_container_binary_method, (void *)integer_operations[index].mode);
        }
        __tinypy_container_add_method(type, "__invert__", 10U, __tinypy_container_unary_method, (void *)(intptr_t)2);
        __tinypy_container_add_method(type, "__index__", 9U, __tinypy_container_conversion_method, (void *)(intptr_t)3);
    }
    __tinypy_container_add_method(type, "__pos__", 7U, __tinypy_container_unary_method, (void *)(intptr_t)0);
    __tinypy_container_add_method(type, "__neg__", 7U, __tinypy_container_unary_method, (void *)(intptr_t)1);
    __tinypy_container_add_method(type, "__abs__", 7U, __tinypy_container_unary_method, (void *)(intptr_t)3);
    __tinypy_container_add_method(type, "__int__", 7U, __tinypy_container_conversion_method, (void *)(intptr_t)0);
    __tinypy_container_add_method(type, "__long__", 8U, __tinypy_container_conversion_method, (void *)(intptr_t)1);
    __tinypy_container_add_method(type, "__float__", 9U, __tinypy_container_conversion_method, (void *)(intptr_t)2);
    __tinypy_container_add_method(type, "__nonzero__", 11U, __tinypy_container_nonzero_method, NULL);
    __tinypy_container_add_method(type, "__repr__", 8U, __tinypy_container_repr_method, NULL);
    __tinypy_container_add_method(type, "__str__", 7U, __tinypy_container_str_method, NULL);
    __tinypy_container_add_method(type, "__hash__", 8U, __tinypy_container_hash_method, NULL);
    __tinypy_container_add_method(type, "__format__", 10U, __tinypy_container_format_method, NULL);
    __tinypy_container_add_comparisons(type);
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_initialize_container_types(tinypy_vm_t *vm) {
    __tinypy_container_add_method(&vm->types[TINYPY_VALUE_TUPLE], "count", 5U, __tinypy_sequence_count_method, NULL);
    __tinypy_container_add_method(&vm->types[TINYPY_VALUE_TUPLE], "index", 5U, __tinypy_sequence_index_method, NULL);
    __tinypy_container_add_method(&vm->types[TINYPY_VALUE_TUPLE], "__iter__", 8U, __tinypy_container_iter_method, NULL);
    __tinypy_container_add_method(&vm->types[TINYPY_VALUE_TUPLE], "__len__", 7U, __tinypy_container_len_method, NULL);
    __tinypy_container_add_method(&vm->types[TINYPY_VALUE_TUPLE], "__getitem__", 11U, __tinypy_container_getitem_method, NULL);
    __tinypy_container_add_method(&vm->types[TINYPY_VALUE_TUPLE], "__repr__", 8U, __tinypy_container_repr_method, NULL);
    __tinypy_container_add_method(&vm->types[TINYPY_VALUE_TUPLE], "__getslice__", 12U, __tinypy_container_getslice_method, NULL);
    __tinypy_container_add_method(&vm->types[TINYPY_VALUE_TUPLE], "__getnewargs__", 14U, __tinypy_container_getnewargs_method, NULL);
    __tinypy_container_add_method(&vm->types[TINYPY_VALUE_LIST], "append", 6U, __tinypy_list_append_method, NULL);
    __tinypy_container_add_method(&vm->types[TINYPY_VALUE_LIST], "extend", 6U, __tinypy_list_extend_method, NULL);
    __tinypy_container_add_method(&vm->types[TINYPY_VALUE_LIST], "insert", 6U, __tinypy_list_insert_method, NULL);
    __tinypy_container_add_method(&vm->types[TINYPY_VALUE_LIST], "pop", 3U, __tinypy_list_pop_method, NULL);
    __tinypy_container_add_method(&vm->types[TINYPY_VALUE_LIST], "remove", 6U, __tinypy_list_remove_method, NULL);
    __tinypy_container_add_method(&vm->types[TINYPY_VALUE_LIST], "count", 5U, __tinypy_sequence_count_method, NULL);
    __tinypy_container_add_method(&vm->types[TINYPY_VALUE_LIST], "index", 5U, __tinypy_sequence_index_method, NULL);
    __tinypy_container_add_method(&vm->types[TINYPY_VALUE_LIST], "reverse", 7U, __tinypy_list_reverse_method, NULL);
    __tinypy_container_add_method(&vm->types[TINYPY_VALUE_LIST], "sort", 4U, __tinypy_list_sort_method, NULL);
    __tinypy_container_add_method(&vm->types[TINYPY_VALUE_LIST], "__iter__", 8U, __tinypy_container_iter_method, NULL);
    __tinypy_container_add_method(&vm->types[TINYPY_VALUE_LIST], "__len__", 7U, __tinypy_container_len_method, NULL);
    __tinypy_container_add_method(&vm->types[TINYPY_VALUE_LIST], "__getitem__", 11U, __tinypy_container_getitem_method, NULL);
    __tinypy_container_add_method(&vm->types[TINYPY_VALUE_LIST], "__repr__", 8U, __tinypy_container_repr_method, NULL);
    __tinypy_container_add_method(&vm->types[TINYPY_VALUE_LIST], "__iadd__", 8U, __tinypy_list_inplace_add_method, NULL);
    __tinypy_container_add_method(&vm->types[TINYPY_VALUE_LIST], "__imul__", 8U, __tinypy_list_inplace_multiply_method, NULL);
    __tinypy_container_add_method(&vm->types[TINYPY_VALUE_LIST], "__getslice__", 12U, __tinypy_container_getslice_method, NULL);
    __tinypy_container_add_method(&vm->types[TINYPY_VALUE_LIST], "__setslice__", 12U, __tinypy_container_setslice_method, NULL);
    __tinypy_container_add_method(&vm->types[TINYPY_VALUE_LIST], "__delslice__", 12U, __tinypy_container_delslice_method, NULL);
    __tinypy_container_add_method(&vm->types[TINYPY_VALUE_LIST], "__reversed__", 12U, __tinypy_container_reversed_method, NULL);
    __tinypy_container_add_method(&vm->types[TINYPY_VALUE_DICT], "get", 3U, __tinypy_dict_get_method, NULL);
    __tinypy_container_add_class_method(&vm->types[TINYPY_VALUE_DICT], "fromkeys", 8U, __tinypy_dict_fromkeys_method);
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
    __tinypy_container_add_method(&vm->types[TINYPY_VALUE_DICT], "__iter__", 8U, __tinypy_container_iter_method, NULL);
    __tinypy_container_add_method(&vm->types[TINYPY_VALUE_DICT], "__len__", 7U, __tinypy_container_len_method, NULL);
    __tinypy_container_add_method(&vm->types[TINYPY_VALUE_DICT], "__getitem__", 11U, __tinypy_container_getitem_method, NULL);
    __tinypy_container_add_method(&vm->types[TINYPY_VALUE_DICT], "__repr__", 8U, __tinypy_container_repr_method, NULL);
    __tinypy_container_add_method(&vm->types[TINYPY_VALUE_DICT], "__cmp__", 7U, __tinypy_container_cmp_method, (void *)(intptr_t)TINYPY_VALUE_DICT);
    __tinypy_container_add_sequence_protocol(&vm->types[TINYPY_VALUE_TUPLE], TINYPY_FALSE);
    __tinypy_container_add_sequence_arithmetic(&vm->types[TINYPY_VALUE_TUPLE], TINYPY_FALSE);
    __tinypy_container_add_sequence_protocol(&vm->types[TINYPY_VALUE_LIST], TINYPY_TRUE);
    __tinypy_container_add_sequence_arithmetic(&vm->types[TINYPY_VALUE_LIST], TINYPY_FALSE);
    __tinypy_container_add_sequence_protocol(&vm->types[TINYPY_VALUE_STRING], TINYPY_FALSE);
    __tinypy_container_add_sequence_arithmetic(&vm->types[TINYPY_VALUE_STRING], TINYPY_TRUE);
    __tinypy_container_add_method(&vm->types[TINYPY_VALUE_STRING], "__hash__", 8U, __tinypy_container_hash_method, NULL);
    __tinypy_container_add_method(&vm->types[TINYPY_VALUE_STRING], "__format__", 10U, __tinypy_container_format_method, NULL);
    __tinypy_container_add_method(&vm->types[TINYPY_VALUE_STRING], "__getslice__", 12U, __tinypy_container_getslice_method, NULL);
    __tinypy_container_add_method(&vm->types[TINYPY_VALUE_STRING], "__getnewargs__", 14U, __tinypy_container_getnewargs_method, NULL);
    __tinypy_container_add_sequence_protocol(&vm->types[TINYPY_VALUE_UNICODE], TINYPY_FALSE);
    __tinypy_container_add_sequence_arithmetic(&vm->types[TINYPY_VALUE_UNICODE], TINYPY_TRUE);
    __tinypy_container_add_method(&vm->types[TINYPY_VALUE_UNICODE], "__hash__", 8U, __tinypy_container_hash_method, NULL);
    __tinypy_container_add_method(&vm->types[TINYPY_VALUE_UNICODE], "__format__", 10U, __tinypy_container_format_method, NULL);
    __tinypy_container_add_method(&vm->types[TINYPY_VALUE_UNICODE], "__getslice__", 12U, __tinypy_container_getslice_method, NULL);
    __tinypy_container_add_method(&vm->types[TINYPY_VALUE_UNICODE], "__getnewargs__", 14U, __tinypy_container_getnewargs_method, NULL);
    __tinypy_container_add_sequence_protocol(&vm->types[TINYPY_VALUE_BYTEARRAY], TINYPY_TRUE);
    __tinypy_container_add_sequence_protocol(&vm->types[TINYPY_VALUE_DICT], TINYPY_TRUE);
    __tinypy_container_add_comparisons(&vm->types[TINYPY_VALUE_SET]);
    __tinypy_container_add_comparisons(&vm->types[TINYPY_VALUE_FROZENSET]);
    __tinypy_container_add_method(&vm->types[TINYPY_VALUE_SET], "__cmp__", 7U, __tinypy_container_cmp_method, (void *)(intptr_t)TINYPY_VALUE_SET);
    __tinypy_container_add_method(&vm->types[TINYPY_VALUE_FROZENSET], "__cmp__", 7U, __tinypy_container_cmp_method, (void *)(intptr_t)TINYPY_VALUE_FROZENSET);
    __tinypy_container_add_numeric_protocol(&vm->types[TINYPY_VALUE_INTEGER], TINYPY_TRUE);
    __tinypy_container_add_numeric_protocol(&vm->types[TINYPY_VALUE_LONG], TINYPY_TRUE);
    __tinypy_container_add_numeric_protocol(&vm->types[TINYPY_VALUE_FLOAT], TINYPY_FALSE);
    __tinypy_container_add_numeric_protocol(&vm->types[TINYPY_VALUE_COMPLEX], TINYPY_FALSE);
    __tinypy_container_add_method(&vm->types[TINYPY_VALUE_TUPLE], "__hash__", 8U, __tinypy_container_hash_method, NULL);
    tinypy_value_t *hash_key = tinypy_string_from_bytes(vm, "__hash__", 8U);
    tinypy_dict_set(vm->types[TINYPY_VALUE_LIST].dict, hash_key, &vm->none_object.base);
    tinypy_dict_set(vm->types[TINYPY_VALUE_DICT].dict, hash_key, &vm->none_object.base);
    TINYPY_DECREF(hash_key);
}
