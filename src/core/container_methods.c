#include "internal.h"

#include <math.h>
#include <string.h>

#define TINYPY_LIST_SORT_MIN_GALLOP 7U
#define TINYPY_LIST_SORT_MAX_PENDING 85U
#define TINYPY_LIST_SORT_TEMPORARY_SIZE 256U

//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_container_integer_result(tinypy_value_t *value, int64_t *out_value, tinypy_error_t **out_error) {
    tinypy_bool_t result = tinypy_internal_index_as_i64(value, out_value, TINYPY_FALSE, out_error);

    if (result == 0 && TINYPY_VALUE_KIND(value) == TINYPY_VALUE_LONG) {
        if (out_error != NULL && *out_error != NULL) {
            tinypy_error_release(*out_error);
            *out_error = NULL;
        }
        tinypy_vm_clear_error(TINYPY_VALUE_VM(value));
        tinypy_internal_make_vm_error(TINYPY_VALUE_VM(value), TINYPY_ERROR_OVERFLOW, "Python int too large to convert to C long", out_error);
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_integer_as_ssize(tinypy_value_t *value, int64_t *out_value, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);

    if (kind == TINYPY_VALUE_FLOAT) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "integer argument expected, got float", out_error);
        return TINYPY_FALSE;
    }
    if (kind == TINYPY_VALUE_BOOL || kind == TINYPY_VALUE_INTEGER || value->type == &vm->types[TINYPY_VALUE_LONG]) {
        tinypy_bool_t result = __tinypy_container_integer_result(value, out_value, out_error);
        return result;
    }
    tinypy_bool_t handled;
    tinypy_value_t *converted = tinypy_internal_call_int_conversion(value, &handled, out_error);

    if (converted == NULL) {
        if (handled == 0) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "an integer is required", out_error);
        }
        return TINYPY_FALSE;
    }
    kind = TINYPY_VALUE_KIND(converted);
    if (kind != TINYPY_VALUE_BOOL && kind != TINYPY_VALUE_INTEGER && kind != TINYPY_VALUE_LONG) {
        TINYPY_DECREF(converted);
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "__int__ method should return an integer", out_error);
        return TINYPY_FALSE;
    }
    tinypy_bool_t result = __tinypy_container_integer_result(converted, out_value, out_error);

    TINYPY_DECREF(converted);
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* PyInt_AsSsize_t reads integer subtypes directly, then uses __int__. */
tinypy_bool_t tinypy_internal_number_as_ssize(tinypy_value_t *value, int64_t *out_value, tinypy_error_t **out_error) {
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);

    if (kind == TINYPY_VALUE_BOOL || kind == TINYPY_VALUE_INTEGER || kind == TINYPY_VALUE_LONG) {
        tinypy_bool_t result = tinypy_internal_index_as_i64(value, out_value, TINYPY_FALSE, out_error);
        if (result == TINYPY_FALSE && kind == TINYPY_VALUE_LONG) {
            if (out_error != NULL && *out_error != NULL) {
                tinypy_error_release(*out_error);
                *out_error = NULL;
            }
            tinypy_internal_exception_clear_raised(TINYPY_VALUE_VM(value));
            tinypy_internal_make_vm_error(TINYPY_VALUE_VM(value), TINYPY_ERROR_OVERFLOW, "long int too large to convert to int", out_error);
        }
        return result;
    }
    tinypy_bool_t result = tinypy_internal_number_as_i64(value, out_value, out_error);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_container_list_index(tinypy_vm_t *vm, int64_t index, size_t size, tinypy_bool_t allow_end, size_t *out_index, tinypy_error_t **out_error) {
    uint64_t distance;

    if (index < 0) {
        distance = (uint64_t)(-(index + INT64_C(1))) + UINT64_C(1);
        if (distance > size) {
            if (allow_end != 0) {
                *out_index = 0U;
                return TINYPY_TRUE;
            }
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_INDEX, "pop index out of range", out_error);
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
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_INDEX, "pop index out of range", out_error);
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
tinypy_bool_t tinypy_internal_list_extend_iterable(tinypy_value_t *list, tinypy_value_t *iterable, const char *negative_hint_message, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(list);

    if (iterable->type == &vm->types[TINYPY_VALUE_LIST]) {
        tinypy_bool_t result = tinypy_internal_list_extend_checked(list, TINYPY_LIST_OBJECT(iterable)->items, TINYPY_LIST_SIZE(iterable), out_error);
        return result;
    }
    if (iterable->type == &vm->types[TINYPY_VALUE_TUPLE]) {
        tinypy_value_t *const *items = tinypy_internal_tuple_items(iterable);
        tinypy_bool_t result = tinypy_internal_list_extend_checked(list, items, TINYPY_TUPLE_SIZE(iterable), out_error);
        return result;
    }
    if (list == iterable) {
        tinypy_value_t *snapshot = tinypy_list_from_items(vm, NULL, 0U);

        if (tinypy_internal_list_extend_iterable(snapshot, iterable, negative_hint_message, out_error) == 0) {
            TINYPY_DECREF(snapshot);
            return TINYPY_FALSE;
        }
        tinypy_bool_t result = tinypy_internal_list_extend_checked(list, TINYPY_LIST_OBJECT(snapshot)->items, TINYPY_LIST_SIZE(snapshot), out_error);

        TINYPY_DECREF(snapshot);
        return result;
    }
    tinypy_value_t *iterator = tinypy_iter(iterable, out_error);
    tinypy_error_t *iteration_error = NULL;

    if (iterator == NULL) {
        return TINYPY_FALSE;
    }
    int64_t hint;
    if (tinypy_internal_length_hint(iterable, INT64_C(8), &hint, out_error) == 0) {
        TINYPY_DECREF(iterator);
        return TINYPY_FALSE;
    }
    if (hint == INT64_C(-1)) {
        if (negative_hint_message != NULL) {
            tinypy_internal_exception_raise_system_error(vm, negative_hint_message, out_error);
        }
        TINYPY_DECREF(iterator);
        return TINYPY_FALSE;
    }
    /* Hint callbacks may mutate the destination. Python 2 ignores a hint
       whose addition would overflow, then appends each yielded item. */
    size_t size = TINYPY_LIST_SIZE(list);
    if (hint >= 0 && size <= (size_t)PTRDIFF_MAX && (uint64_t)hint <= (uint64_t)PTRDIFF_MAX - (uint64_t)size) {
        if (tinypy_internal_list_reserve_checked(vm, list, size + (size_t)hint, out_error) == 0) {
            TINYPY_DECREF(iterator);
            return TINYPY_FALSE;
        }
    }
    for (;;) {
        tinypy_value_t *item = tinypy_next(iterator, &iteration_error);

        if (item == NULL) {
            break;
        }
        if (tinypy_internal_list_append_checked(list, item, out_error) == 0) {
            TINYPY_DECREF(item);
            TINYPY_DECREF(iterator);
            return TINYPY_FALSE;
        }
        TINYPY_DECREF(item);
    }
    TINYPY_DECREF(iterator);
    if (iteration_error != NULL) {
        if (out_error != NULL) {
            *out_error = iteration_error;
        }
        else {
            tinypy_error_release(iteration_error);
        }
        return TINYPY_FALSE;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_list_append_method(tinypy_value_t *function, tinypy_value_t *self, tinypy_value_t *const *items, size_t count, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)count;
    (void)kwargs;
    if (tinypy_internal_list_append_checked(self, items[0U], out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *return_value_1 = TINYPY_RET_NONE(vm);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_list_extend_method(tinypy_value_t *function, tinypy_value_t *self, tinypy_value_t *const *items, size_t count, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)count;
    (void)kwargs;
    tinypy_value_t *list = self;
    tinypy_value_t *iterable = items[0U];
    if (tinypy_internal_list_extend_iterable(list, iterable, "error return without exception set", out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *return_value_1 = TINYPY_RET_NONE(vm);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_list_inplace_add_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {

    (void)user_data;
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 1U, 1U, TINYPY_ARITY_STYLE_WRAPPER, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *list = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *iterable = TINYPY_TUPLE_GET(args, 1U);
    if (tinypy_internal_list_extend_iterable(list, iterable, "error return without exception set", out_error) == 0) {
        return NULL;
    }
    return TINYPY_RET(list);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_list_inplace_multiply_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    int64_t count;

    (void)user_data;
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 1U, 1U, TINYPY_ARITY_STYLE_UNPACK, out_error) == 0) {
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
        /* list_inplace_repeat runs out of memory for any size it cannot hold. */
        if ((uint64_t)count > (uint64_t)(SIZE_MAX / unit_size)) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_MEMORY, "repeated list is too large", out_error);
            return NULL;
        }
        size_t total_size = unit_size * (size_t)count;
        if (total_size >= (size_t)PTRDIFF_MAX || total_size > SIZE_MAX / sizeof(tinypy_value_t *)) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_MEMORY, "repeated list is too large", out_error);
            return NULL;
        }
        if (tinypy_internal_list_reserve_checked(vm, list, total_size, out_error) == 0) {
            return NULL;
        }
        tinypy_value_t **items = TINYPY_LIST_OBJECT(list)->items;
        size_t index;
        for (index = unit_size; index < total_size; ++index) {
            items[index] = TINYPY_RET(items[index % unit_size]);
        }
        TINYPY_SIZED_SIZE(list) = total_size;
        TINYPY_LIST_OBJECT(list)->mutation_version += UINT64_C(1);
#if defined(TINYPY_CYCLE_DIAGNOSTICS)
        __tinypy_internal_cycle_diagnostics_list_extend(vm, list, unit_size, items + unit_size, total_size - unit_size);
#endif
    }
    return TINYPY_RET(list);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_list_insert_method(tinypy_value_t *function, tinypy_value_t *self, tinypy_value_t *const *items, size_t count, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    size_t index;

    (void)count;
    (void)kwargs;
    tinypy_value_t *list = self;
    tinypy_value_t *item_2 = items[0U];
    int64_t requested_index;

    if (tinypy_internal_integer_as_ssize(item_2, &requested_index, out_error) == 0) {
        return NULL;
    }
    size_t list_size = TINYPY_LIST_SIZE(list);
    if (__tinypy_container_list_index(vm, requested_index, list_size, TINYPY_TRUE, &index, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *item = items[1U];
    if (tinypy_internal_list_insert_checked(list, index, item, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *return_value_1 = TINYPY_RET_NONE(vm);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_list_pop_method(tinypy_value_t *function, tinypy_value_t *self, tinypy_value_t *const *items, size_t count, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    size_t size;
    size_t index;

    (void)kwargs;
    tinypy_value_t *list = self;
    int64_t requested_index = INT64_C(-1);

    if (count == 1U && tinypy_internal_integer_as_ssize(items[0U], &requested_index, out_error) == 0) {
        return NULL;
    }
    size = TINYPY_LIST_SIZE(list);
    if (size == 0U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_INDEX, "pop from empty list", out_error);
        return NULL;
    }
    if (__tinypy_container_list_index(vm, requested_index, size, TINYPY_FALSE, &index, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *return_value_1 = tinypy_list_pop(list, index);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_list_remove_method(tinypy_value_t *function, tinypy_value_t *self, tinypy_value_t *const *items, size_t count, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    size_t index = 0U;

    (void)count;
    (void)kwargs;
    tinypy_value_t *list = self;
    tinypy_value_t *needle = items[0U];
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
            tinypy_value_t *return_value_1 = TINYPY_RET_NONE(vm);
            return return_value_1;
        }
        index += 1U;
    }
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "list.remove(x): x not in list", out_error);
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_sequence_count_method(tinypy_value_t *function, tinypy_value_t *self, tinypy_value_t *const *items, size_t count, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    int64_t occurrences = 0;
    size_t index = 0U;

    (void)count;
    (void)kwargs;
    tinypy_value_t *sequence = self;
    tinypy_value_t *needle = items[0U];
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
            occurrences += INT64_C(1);
        }
        index += 1U;
    }
    tinypy_value_t *return_value_1 = tinypy_integer_from_i64(vm, occurrences);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_sequence_index_method(tinypy_value_t *function, tinypy_value_t *self, tinypy_value_t *const *items, size_t count, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    int64_t start = 0;
    int64_t stop;
    int64_t size;
    int64_t index;

    (void)kwargs;
    tinypy_value_t *sequence = self;
    tinypy_value_t *needle = items[0U];
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(sequence);
    size = (int64_t)__tinypy_container_sequence_size(sequence, kind);
    stop = size;
    tinypy_bool_t condition = count >= 2U;
    if (condition != 0) {
        tinypy_value_t *item_2 = items[1U];
        condition = tinypy_internal_slice_index_not_none(item_2, &start, out_error) == 0;
    }
    if (condition) {
        return NULL;
    }
    tinypy_bool_t condition_2 = count == 3U;
    if (condition_2 != 0) {
        tinypy_value_t *item_2 = items[2U];
        condition_2 = tinypy_internal_slice_index_not_none(item_2, &stop, out_error) == 0;
    }
    if (condition_2) {
        return NULL;
    }
    size = (int64_t)__tinypy_container_sequence_size(sequence, kind);
    if (start < 0) {
        start = start < -size ? 0 : start + size;
    }
    if (stop < 0) {
        stop = stop < -size ? 0 : stop + size;
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
    if (kind == TINYPY_VALUE_TUPLE) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "tuple.index(x): x not in tuple", out_error);
    }
    else {
        tinypy_value_t *needle_repr = tinypy_object_repr(needle, out_error);

        if (needle_repr == NULL) {
            return NULL;
        }
        tinypy_message_part_t parts[] = {
            TINYPY_MESSAGE_PART_TEXT(needle_repr),
            TINYPY_MESSAGE_PART_LITERAL(" is not in list"),
        };
        tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_VALUE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
        TINYPY_DECREF(needle_repr);
    }
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_list_reverse_method(tinypy_value_t *function, tinypy_value_t *self, tinypy_value_t *const *items, size_t count, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    size_t left;
    size_t right;

    (void)items;
    (void)count;
    (void)kwargs;
    (void)out_error;
    tinypy_value_t *list = self;
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
        __tinypy_internal_cycle_diagnostics_list_reindex(TINYPY_VALUE_VM(function), list);
#endif
    }
    tinypy_value_t *return_value_1 = TINYPY_RET_NONE(vm);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
typedef struct tinypy_list_sort_item_t {
    tinypy_value_t *value;
    tinypy_value_t *key;
} tinypy_list_sort_item_t;
//////////////////////////////////////////////////////////////////////////
typedef struct tinypy_list_sort_run_t {
    tinypy_list_sort_item_t *base;
    size_t length;
} tinypy_list_sort_run_t;
//////////////////////////////////////////////////////////////////////////
/* MergeState of listsort: the pending runs, the galloping threshold and the
   temporary storage, which spills to the heap only for long merges. */
typedef struct tinypy_list_sort_state_t {
    tinypy_vm_t *vm;
    tinypy_value_t *compare;
    size_t min_gallop;
    tinypy_list_sort_item_t *temporary;
    size_t temporary_capacity;
    size_t run_count;
    tinypy_list_sort_run_t runs[TINYPY_LIST_SORT_MAX_PENDING];
    tinypy_list_sort_item_t inline_temporary[TINYPY_LIST_SORT_TEMPORARY_SIZE];
} tinypy_list_sort_state_t;
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
    if (tinypy_internal_object_has_special_key(callable, vm->internal_special_call_key) != 0) {
        tinypy_value_t *method = tinypy_internal_object_get_special_key(callable, vm->internal_special_call_key, out_error);

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
            tinypy_message_part_t parts[] = {
                TINYPY_MESSAGE_PART_LITERAL("comparison function must return int, not "),
                TINYPY_MESSAGE_PART_TYPE_NAME(result)
            };

            tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, 2U, out_error);
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
static tinypy_bool_t __tinypy_list_sort_less(tinypy_list_sort_state_t *state, const tinypy_list_sort_item_t *left, const tinypy_list_sort_item_t *right, tinypy_bool_t *out_less, tinypy_error_t **out_error) {
    tinypy_bool_t compared = __tinypy_list_sort_compare(state->vm, left->key, right->key, state->compare, out_less, out_error);

    return compared;
}
//////////////////////////////////////////////////////////////////////////
/* binarysort: [low, high) is sorted by binary insertion, [low, start) being
   sorted already; a failed comparison leaves a permutation of the slice. */
static tinypy_bool_t __tinypy_list_sort_binary(tinypy_list_sort_state_t *state, tinypy_list_sort_item_t *low, tinypy_list_sort_item_t *high, tinypy_list_sort_item_t *start, tinypy_error_t **out_error) {
    if (low == start) {
        ++start;
    }
    for (; start < high; ++start) {
        tinypy_list_sort_item_t *left = low;
        tinypy_list_sort_item_t *right = start;
        tinypy_list_sort_item_t pivot = *right;

        do {
            tinypy_list_sort_item_t *middle = left + ((right - left) >> 1);
            tinypy_bool_t less;

            if (__tinypy_list_sort_less(state, &pivot, middle, &less, out_error) == 0) {
                return TINYPY_FALSE;
            }
            if (less != 0) {
                right = middle;
            }
            else {
                left = middle + 1;
            }
        } while (left < right);
        (void)memmove(left + 1, left, (size_t)(start - left) * sizeof(*left));
        *left = pivot;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
/* count_run: the longest ascending run, or the longest strictly descending
   one, which the caller may reverse without breaking stability. */
static tinypy_bool_t __tinypy_list_sort_count_run(tinypy_list_sort_state_t *state, tinypy_list_sort_item_t *low, tinypy_list_sort_item_t *high, size_t *out_length, tinypy_bool_t *out_descending, tinypy_error_t **out_error) {
    tinypy_bool_t less;
    size_t length = 2U;

    *out_descending = TINYPY_FALSE;
    ++low;
    if (low == high) {
        *out_length = 1U;
        return TINYPY_TRUE;
    }
    if (__tinypy_list_sort_less(state, low, low - 1, &less, out_error) == 0) {
        return TINYPY_FALSE;
    }
    *out_descending = less;
    for (low = low + 1; low < high; ++low, ++length) {
        tinypy_bool_t next_less;

        if (__tinypy_list_sort_less(state, low, low - 1, &next_less, out_error) == 0) {
            return TINYPY_FALSE;
        }
        if (next_less != less) {
            break;
        }
    }
    *out_length = length;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static ptrdiff_t __tinypy_list_sort_next_offset(ptrdiff_t offset, ptrdiff_t max_offset) {
    if (offset > (PTRDIFF_MAX - 1) / 2) {
        return max_offset;
    }
    return (offset << 1) + 1;
}
//////////////////////////////////////////////////////////////////////////
/* gallop_left: the position k with items[k - 1] < key <= items[k], found by
   galloping from hint and then searching binary. */
static tinypy_bool_t __tinypy_list_sort_gallop_left(tinypy_list_sort_state_t *state, const tinypy_list_sort_item_t *key, const tinypy_list_sort_item_t *items, ptrdiff_t count, ptrdiff_t hint, ptrdiff_t *out_position, tinypy_error_t **out_error) {
    const tinypy_list_sort_item_t *base = items + hint;
    ptrdiff_t last_offset = 0;
    ptrdiff_t offset = 1;
    tinypy_bool_t less;

    if (__tinypy_list_sort_less(state, base, key, &less, out_error) == 0) {
        return TINYPY_FALSE;
    }
    if (less != 0) {
        ptrdiff_t max_offset = count - hint;

        while (offset < max_offset) {
            if (__tinypy_list_sort_less(state, base + offset, key, &less, out_error) == 0) {
                return TINYPY_FALSE;
            }
            if (less == 0) {
                break;
            }
            last_offset = offset;
            offset = __tinypy_list_sort_next_offset(offset, max_offset);
        }
        if (offset > max_offset) {
            offset = max_offset;
        }
        last_offset += hint;
        offset += hint;
    }
    else {
        ptrdiff_t max_offset = hint + 1;

        while (offset < max_offset) {
            if (__tinypy_list_sort_less(state, base - offset, key, &less, out_error) == 0) {
                return TINYPY_FALSE;
            }
            if (less != 0) {
                break;
            }
            last_offset = offset;
            offset = __tinypy_list_sort_next_offset(offset, max_offset);
        }
        if (offset > max_offset) {
            offset = max_offset;
        }
        ptrdiff_t previous = last_offset;
        last_offset = hint - offset;
        offset = hint - previous;
    }
    ++last_offset;
    while (last_offset < offset) {
        ptrdiff_t middle = last_offset + ((offset - last_offset) >> 1);

        if (__tinypy_list_sort_less(state, items + middle, key, &less, out_error) == 0) {
            return TINYPY_FALSE;
        }
        if (less != 0) {
            last_offset = middle + 1;
        }
        else {
            offset = middle;
        }
    }
    *out_position = offset;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
/* gallop_right: like gallop_left, but after the rightmost item equal to key,
   so items[k - 1] <= key < items[k]. */
static tinypy_bool_t __tinypy_list_sort_gallop_right(tinypy_list_sort_state_t *state, const tinypy_list_sort_item_t *key, const tinypy_list_sort_item_t *items, ptrdiff_t count, ptrdiff_t hint, ptrdiff_t *out_position, tinypy_error_t **out_error) {
    const tinypy_list_sort_item_t *base = items + hint;
    ptrdiff_t last_offset = 0;
    ptrdiff_t offset = 1;
    tinypy_bool_t less;

    if (__tinypy_list_sort_less(state, key, base, &less, out_error) == 0) {
        return TINYPY_FALSE;
    }
    if (less != 0) {
        ptrdiff_t max_offset = hint + 1;

        while (offset < max_offset) {
            if (__tinypy_list_sort_less(state, key, base - offset, &less, out_error) == 0) {
                return TINYPY_FALSE;
            }
            if (less == 0) {
                break;
            }
            last_offset = offset;
            offset = __tinypy_list_sort_next_offset(offset, max_offset);
        }
        if (offset > max_offset) {
            offset = max_offset;
        }
        ptrdiff_t previous = last_offset;
        last_offset = hint - offset;
        offset = hint - previous;
    }
    else {
        ptrdiff_t max_offset = count - hint;

        while (offset < max_offset) {
            if (__tinypy_list_sort_less(state, key, base + offset, &less, out_error) == 0) {
                return TINYPY_FALSE;
            }
            if (less != 0) {
                break;
            }
            last_offset = offset;
            offset = __tinypy_list_sort_next_offset(offset, max_offset);
        }
        if (offset > max_offset) {
            offset = max_offset;
        }
        last_offset += hint;
        offset += hint;
    }
    ++last_offset;
    while (last_offset < offset) {
        ptrdiff_t middle = last_offset + ((offset - last_offset) >> 1);

        if (__tinypy_list_sort_less(state, key, items + middle, &less, out_error) == 0) {
            return TINYPY_FALSE;
        }
        if (less != 0) {
            offset = middle;
        }
        else {
            last_offset = middle + 1;
        }
    }
    *out_position = offset;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_list_sort_release(tinypy_list_sort_state_t *state) {
    if (state->temporary != state->inline_temporary) {
        tinypy_internal_vm_deallocate(state->vm, state->temporary, state->temporary_capacity * sizeof(*state->temporary));
    }
    state->temporary = state->inline_temporary;
    state->temporary_capacity = TINYPY_LIST_SORT_TEMPORARY_SIZE;
}
//////////////////////////////////////////////////////////////////////////
/* merge_getmem replaces the temporary storage without copying it. */
static tinypy_bool_t __tinypy_list_sort_reserve(tinypy_list_sort_state_t *state, ptrdiff_t need, tinypy_error_t **out_error) {
    if ((size_t)need <= state->temporary_capacity) {
        return TINYPY_TRUE;
    }
    __tinypy_list_sort_release(state);
    if ((size_t)need > SIZE_MAX / sizeof(*state->temporary)) {
        tinypy_internal_make_vm_error(state->vm, TINYPY_ERROR_MEMORY, "sort temporary storage is too large", out_error);
        return TINYPY_FALSE;
    }
    tinypy_list_sort_item_t *temporary = (tinypy_list_sort_item_t *)tinypy_internal_vm_allocate_checked(state->vm, (size_t)need * sizeof(*temporary), out_error);
    if (temporary == NULL) {
        return TINYPY_FALSE;
    }
    state->temporary = temporary;
    state->temporary_capacity = (size_t)need;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
/* merge_lo merges the shorter run a, copied aside, with the run b after it;
   items[a] > items[b] and the last of a ends the merge. */
static tinypy_bool_t __tinypy_list_sort_merge_low(tinypy_list_sort_state_t *state, tinypy_list_sort_item_t *a, ptrdiff_t a_count, tinypy_list_sort_item_t *b, ptrdiff_t b_count, tinypy_error_t **out_error) {
    size_t min_gallop = state->min_gallop;
    tinypy_bool_t merged = TINYPY_FALSE;
    tinypy_bool_t less;
    ptrdiff_t position;

    if (__tinypy_list_sort_reserve(state, a_count, out_error) == 0) {
        return TINYPY_FALSE;
    }
    (void)memcpy(state->temporary, a, (size_t)a_count * sizeof(*a));
    tinypy_list_sort_item_t *destination = a;
    a = state->temporary;

    *destination++ = *b++;
    --b_count;
    if (b_count == 0) {
        goto succeed;
    }
    if (a_count == 1) {
        goto copy_b;
    }
    for (;;) {
        size_t a_wins = 0U;
        size_t b_wins = 0U;

        for (;;) {
            if (__tinypy_list_sort_less(state, b, a, &less, out_error) == 0) {
                goto fail;
            }
            if (less != 0) {
                *destination++ = *b++;
                ++b_wins;
                a_wins = 0U;
                --b_count;
                if (b_count == 0) {
                    goto succeed;
                }
                if (b_wins >= min_gallop) {
                    break;
                }
            }
            else {
                *destination++ = *a++;
                ++a_wins;
                b_wins = 0U;
                --a_count;
                if (a_count == 1) {
                    goto copy_b;
                }
                if (a_wins >= min_gallop) {
                    break;
                }
            }
        }
        ++min_gallop;
        do {
            min_gallop -= min_gallop > 1U ? 1U : 0U;
            state->min_gallop = min_gallop;
            if (__tinypy_list_sort_gallop_right(state, b, a, a_count, 0, &position, out_error) == 0) {
                goto fail;
            }
            a_wins = (size_t)position;
            if (position != 0) {
                (void)memcpy(destination, a, (size_t)position * sizeof(*a));
                destination += position;
                a += position;
                a_count -= position;
                if (a_count == 1) {
                    goto copy_b;
                }
                if (a_count == 0) {
                    goto succeed;
                }
            }
            *destination++ = *b++;
            --b_count;
            if (b_count == 0) {
                goto succeed;
            }
            if (__tinypy_list_sort_gallop_left(state, a, b, b_count, 0, &position, out_error) == 0) {
                goto fail;
            }
            b_wins = (size_t)position;
            if (position != 0) {
                (void)memmove(destination, b, (size_t)position * sizeof(*b));
                destination += position;
                b += position;
                b_count -= position;
                if (b_count == 0) {
                    goto succeed;
                }
            }
            *destination++ = *a++;
            --a_count;
            if (a_count == 1) {
                goto copy_b;
            }
        } while (a_wins >= TINYPY_LIST_SORT_MIN_GALLOP || b_wins >= TINYPY_LIST_SORT_MIN_GALLOP);
        ++min_gallop;
        state->min_gallop = min_gallop;
    }
succeed:
    merged = TINYPY_TRUE;
fail:
    if (a_count != 0) {
        (void)memcpy(destination, a, (size_t)a_count * sizeof(*a));
    }
    return merged;
copy_b:
    (void)memmove(destination, b, (size_t)b_count * sizeof(*b));
    destination[b_count] = *a;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
/* merge_hi merges the run a with the shorter run b after it, copied aside,
   from the right end. */
static tinypy_bool_t __tinypy_list_sort_merge_high(tinypy_list_sort_state_t *state, tinypy_list_sort_item_t *a, ptrdiff_t a_count, tinypy_list_sort_item_t *b, ptrdiff_t b_count, tinypy_error_t **out_error) {
    size_t min_gallop = state->min_gallop;
    tinypy_bool_t merged = TINYPY_FALSE;
    tinypy_bool_t less;
    ptrdiff_t position;

    if (__tinypy_list_sort_reserve(state, b_count, out_error) == 0) {
        return TINYPY_FALSE;
    }
    tinypy_list_sort_item_t *destination = b + b_count - 1;
    (void)memcpy(state->temporary, b, (size_t)b_count * sizeof(*b));
    tinypy_list_sort_item_t *a_base = a;
    tinypy_list_sort_item_t *b_base = state->temporary;
    b = state->temporary + b_count - 1;
    a += a_count - 1;

    *destination-- = *a--;
    --a_count;
    if (a_count == 0) {
        goto succeed;
    }
    if (b_count == 1) {
        goto copy_a;
    }
    for (;;) {
        size_t a_wins = 0U;
        size_t b_wins = 0U;

        for (;;) {
            if (__tinypy_list_sort_less(state, b, a, &less, out_error) == 0) {
                goto fail;
            }
            if (less != 0) {
                *destination-- = *a--;
                ++a_wins;
                b_wins = 0U;
                --a_count;
                if (a_count == 0) {
                    goto succeed;
                }
                if (a_wins >= min_gallop) {
                    break;
                }
            }
            else {
                *destination-- = *b--;
                ++b_wins;
                a_wins = 0U;
                --b_count;
                if (b_count == 1) {
                    goto copy_a;
                }
                if (b_wins >= min_gallop) {
                    break;
                }
            }
        }
        ++min_gallop;
        do {
            min_gallop -= min_gallop > 1U ? 1U : 0U;
            state->min_gallop = min_gallop;
            if (__tinypy_list_sort_gallop_right(state, b, a_base, a_count, a_count - 1, &position, out_error) == 0) {
                goto fail;
            }
            position = a_count - position;
            a_wins = (size_t)position;
            if (position != 0) {
                destination -= position;
                a -= position;
                (void)memmove(destination + 1, a + 1, (size_t)position * sizeof(*a));
                a_count -= position;
                if (a_count == 0) {
                    goto succeed;
                }
            }
            *destination-- = *b--;
            --b_count;
            if (b_count == 1) {
                goto copy_a;
            }
            if (__tinypy_list_sort_gallop_left(state, a, b_base, b_count, b_count - 1, &position, out_error) == 0) {
                goto fail;
            }
            position = b_count - position;
            b_wins = (size_t)position;
            if (position != 0) {
                destination -= position;
                b -= position;
                (void)memcpy(destination + 1, b + 1, (size_t)position * sizeof(*b));
                b_count -= position;
                if (b_count == 1) {
                    goto copy_a;
                }
                if (b_count == 0) {
                    goto succeed;
                }
            }
            *destination-- = *a--;
            --a_count;
            if (a_count == 0) {
                goto succeed;
            }
        } while (a_wins >= TINYPY_LIST_SORT_MIN_GALLOP || b_wins >= TINYPY_LIST_SORT_MIN_GALLOP);
        ++min_gallop;
        state->min_gallop = min_gallop;
    }
succeed:
    merged = TINYPY_TRUE;
fail:
    if (b_count != 0) {
        (void)memcpy(destination - (b_count - 1), b_base, (size_t)b_count * sizeof(*b_base));
    }
    return merged;
copy_a:
    destination -= a_count;
    a -= a_count;
    (void)memmove(destination + 1, a + 1, (size_t)a_count * sizeof(*a));
    *destination = *b;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
/* merge_at merges the pending runs at index and index + 1, skipping the
   items of either that are already in place. */
static tinypy_bool_t __tinypy_list_sort_merge_at(tinypy_list_sort_state_t *state, size_t index, tinypy_error_t **out_error) {
    tinypy_list_sort_run_t *runs = state->runs;
    tinypy_list_sort_item_t *a = runs[index].base;
    ptrdiff_t a_count = (ptrdiff_t)runs[index].length;
    tinypy_list_sort_item_t *b = runs[index + 1U].base;
    ptrdiff_t b_count = (ptrdiff_t)runs[index + 1U].length;
    ptrdiff_t position;

    runs[index].length = (size_t)(a_count + b_count);
    if (index + 3U == state->run_count) {
        runs[index + 1U] = runs[index + 2U];
    }
    state->run_count -= 1U;
    if (__tinypy_list_sort_gallop_right(state, b, a, a_count, 0, &position, out_error) == 0) {
        return TINYPY_FALSE;
    }
    a += position;
    a_count -= position;
    if (a_count == 0) {
        return TINYPY_TRUE;
    }
    if (__tinypy_list_sort_gallop_left(state, a + a_count - 1, b, b_count, b_count - 1, &b_count, out_error) == 0) {
        return TINYPY_FALSE;
    }
    if (b_count == 0) {
        return TINYPY_TRUE;
    }
    tinypy_bool_t merged = a_count <= b_count
                               ? __tinypy_list_sort_merge_low(state, a, a_count, b, b_count, out_error)
                               : __tinypy_list_sort_merge_high(state, a, a_count, b, b_count, out_error);
    return merged;
}
//////////////////////////////////////////////////////////////////////////
/* merge_collapse restores the invariants of the pending runs:
   length[-3] > length[-2] + length[-1] and length[-2] > length[-1]. */
static tinypy_bool_t __tinypy_list_sort_merge_collapse(tinypy_list_sort_state_t *state, tinypy_error_t **out_error) {
    tinypy_list_sort_run_t *runs = state->runs;

    while (state->run_count > 1U) {
        size_t index = state->run_count - 2U;

        if ((index > 0U && runs[index - 1U].length <= runs[index].length + runs[index + 1U].length)
            || (index > 1U && runs[index - 2U].length <= runs[index - 1U].length + runs[index].length)) {
            if (runs[index - 1U].length < runs[index + 1U].length) {
                --index;
            }
        }
        else if (runs[index].length > runs[index + 1U].length) {
            break;
        }
        if (__tinypy_list_sort_merge_at(state, index, out_error) == 0) {
            return TINYPY_FALSE;
        }
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_list_sort_merge_force_collapse(tinypy_list_sort_state_t *state, tinypy_error_t **out_error) {
    tinypy_list_sort_run_t *runs = state->runs;

    while (state->run_count > 1U) {
        size_t index = state->run_count - 2U;

        if (index > 0U && runs[index - 1U].length < runs[index + 1U].length) {
            --index;
        }
        if (__tinypy_list_sort_merge_at(state, index, out_error) == 0) {
            return TINYPY_FALSE;
        }
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
/* merge_compute_minrun */
static size_t __tinypy_list_sort_minimum_run(size_t count) {
    size_t remainder = 0U;

    while (count >= 64U) {
        remainder |= count & 1U;
        count >>= 1U;
    }
    return count + remainder;
}
//////////////////////////////////////////////////////////////////////////
/* The mergesort of listsort in Python 2.7, step for step, so comparison
   counts and the order left by a failing comparison match. */
static tinypy_bool_t __tinypy_list_sort_items(tinypy_vm_t *vm, tinypy_list_sort_item_t *items, size_t size, tinypy_value_t *compare, tinypy_error_t **out_error) {
    tinypy_list_sort_state_t state;
    tinypy_bool_t sorted = TINYPY_TRUE;

    if (size < 2U) {
        return TINYPY_TRUE;
    }
    state.vm = vm;
    state.compare = compare;
    state.min_gallop = TINYPY_LIST_SORT_MIN_GALLOP;
    state.temporary = state.inline_temporary;
    state.temporary_capacity = TINYPY_LIST_SORT_TEMPORARY_SIZE;
    state.run_count = 0U;
    tinypy_list_sort_item_t *low = items;
    tinypy_list_sort_item_t *high = items + size;
    size_t remaining = size;
    size_t minimum_run = __tinypy_list_sort_minimum_run(size);
    while (remaining != 0U) {
        size_t length;
        tinypy_bool_t descending;

        if (__tinypy_list_sort_count_run(&state, low, high, &length, &descending, out_error) == 0) {
            sorted = TINYPY_FALSE;
            break;
        }
        if (descending != 0) {
            __tinypy_list_sort_reverse_items(low, 0U, length);
        }
        if (length < minimum_run) {
            size_t forced = remaining <= minimum_run ? remaining : minimum_run;

            if (__tinypy_list_sort_binary(&state, low, low + forced, low + length, out_error) == 0) {
                sorted = TINYPY_FALSE;
                break;
            }
            length = forced;
        }
        state.runs[state.run_count].base = low;
        state.runs[state.run_count].length = length;
        state.run_count += 1U;
        if (__tinypy_list_sort_merge_collapse(&state, out_error) == 0) {
            sorted = TINYPY_FALSE;
            break;
        }
        low += length;
        remaining -= length;
    }
    if (sorted != 0) {
        sorted = __tinypy_list_sort_merge_force_collapse(&state, out_error);
    }
    __tinypy_list_sort_release(&state);
    return sorted;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_list_sort_method(tinypy_value_t *function, tinypy_value_t *self, tinypy_value_t *const *items, size_t count, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *const names[3] = {vm->internal_cmp_key, vm->internal_key_key, vm->internal_reverse_key};
    tinypy_value_t *compare = NULL;
    tinypy_value_t *key_function = NULL;
    tinypy_bool_t reverse = TINYPY_FALSE;
    size_t size;
    tinypy_value_t **saved_items;
    size_t saved_allocated;
    uint64_t saved_version;
    tinypy_list_sort_item_t *sort_items = NULL;
    void *sort_storage = NULL;
    size_t sort_storage_size = 0U;
    tinypy_bool_t has_keys;
    tinypy_bool_t sorted = TINYPY_TRUE;
    tinypy_bool_t modified;
    tinypy_bool_t keys_ready;
    size_t key_count = 0U;
    size_t index;

    tinypy_value_t *list = self;
    size_t keyword_count = kwargs != NULL ? TINYPY_DICT_SIZE(kwargs) : 0U;
    size_t matched_keywords = 0U;

    /* PyArg_ParseTupleAndKeywords with "|OOi:sort". */
    if (count + keyword_count > 3U) {
        tinypy_internal_make_arity_error(vm, "sort", 4U, count + keyword_count, 0U, 3U, TINYPY_ARITY_STYLE_PARSED, out_error);
        return NULL;
    }
    for (size_t parameter = 0U; parameter < 3U; ++parameter) {
        tinypy_value_t *value = parameter < count ? items[parameter] : NULL;
        tinypy_value_t *keyword_value = NULL;

        if (matched_keywords < keyword_count) {
            keyword_value = tinypy_internal_constructor_keyword_optional(kwargs, names[parameter]);
        }
        if (keyword_value != NULL) {
            matched_keywords += 1U;
            if (value != NULL) {
                char position = (char)('1' + parameter);
                tinypy_message_part_t parts[] = {
                    TINYPY_MESSAGE_PART_LITERAL("Argument given by name ('"),
                    TINYPY_MESSAGE_PART_TEXT(names[parameter]),
                    TINYPY_MESSAGE_PART_LITERAL("') and position ("),
                    {&position, 1U},
                    TINYPY_MESSAGE_PART_LITERAL(")"),
                };

                tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
                return NULL;
            }
            value = keyword_value;
        }
        if (parameter == 0U) {
            compare = value;
        }
        else if (parameter == 1U) {
            key_function = value;
        }
        else if (value != NULL) {
            int64_t integer;

            if (tinypy_internal_integer_as_ssize(value, &integer, out_error) == 0) {
                return NULL;
            }
            if (integer < INT32_MIN || integer > INT32_MAX) {
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, integer < INT32_MIN ? "signed integer is less than minimum" : "signed integer is greater than maximum", out_error);
                return NULL;
            }
            reverse = integer != 0 ? TINYPY_TRUE : TINYPY_FALSE;
        }
    }
    if (matched_keywords != keyword_count) {
        for (size_t slot = 0U; slot <= TINYPY_DICT_OBJECT(kwargs)->mask; ++slot) {
            tinypy_dict_entry_t *entry = &TINYPY_DICT_OBJECT(kwargs)->table[slot];
            tinypy_bool_t known = TINYPY_FALSE;

            if (!TINYPY_DICT_ENTRY_IS_ACTIVE(entry)) {
                continue;
            }
            tinypy_value_type_e kind = TINYPY_VALUE_KIND(entry->key);
            if (kind != TINYPY_VALUE_STRING && kind != TINYPY_VALUE_UNICODE) {
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "keywords must be strings", out_error);
                return NULL;
            }
            /* The keyword is read as a C string, up to an embedded NUL. */
            const char *name = (const char *)TINYPY_TEXT_BYTES(entry->key);
            size_t name_size = TINYPY_TEXT_BYTE_SIZE(entry->key);
            const char *terminator = (const char *)memchr(name, 0, name_size);

            if (terminator != NULL) {
                name_size = (size_t)(terminator - name);
            }
            for (size_t parameter = 0U; parameter < 3U; ++parameter) {
                if (name_size == TINYPY_TEXT_BYTE_SIZE(names[parameter]) && memcmp(name, TINYPY_TEXT_BYTES(names[parameter]), name_size) == 0) {
                    known = TINYPY_TRUE;
                    break;
                }
            }
            if (known == 0) {
                tinypy_message_part_t parts[] = {
                    TINYPY_MESSAGE_PART_LITERAL("'"),
                    {name, name_size < 200U ? name_size : 200U},
                    TINYPY_MESSAGE_PART_LITERAL("' is an invalid keyword argument for this function"),
                };

                tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
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
        size_t item_storage_size;

        if (size > SIZE_MAX / sizeof(*sort_items)) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_MEMORY, "sort temporary storage is too large", out_error);
            sorted = TINYPY_FALSE;
        }
        else {
            item_storage_size = size * sizeof(*sort_items);
            sort_storage_size = item_storage_size;
            sort_storage = tinypy_internal_vm_allocate_checked(vm, sort_storage_size, out_error);
            if (sort_storage == NULL) {
                sorted = TINYPY_FALSE;
            }
            else {
                sort_items = (tinypy_list_sort_item_t *)sort_storage;
            }
        }
    }
    for (index = 0U; sorted != 0 && index < size; ++index) {
        sort_items[index].value = saved_items[index];
        if (has_keys != 0) {
            sort_items[index].key = __tinypy_container_call_items(vm, key_function, &saved_items[index], 1U, out_error);
            if (sort_items[index].key == NULL) {
                sorted = TINYPY_FALSE;
                break;
            }
            key_count += 1U;
        }
        else {
            sort_items[index].key = saved_items[index];
        }
    }
    keys_ready = sorted;
    if (keys_ready != 0 && reverse != 0) {
        __tinypy_list_sort_reverse_items(sort_items, 0U, size);
    }
    if (keys_ready != 0) {
        sorted = __tinypy_list_sort_items(vm, sort_items, size, compare, out_error);
    }
    if (keys_ready != 0 && reverse != 0) {
        __tinypy_list_sort_reverse_items(sort_items, 0U, size);
    }
    if (keys_ready != 0) {
        for (index = 0U; index < size; ++index) {
            saved_items[index] = sort_items[index].value;
        }
    }
    if (has_keys != 0) {
        for (index = 0U; index < key_count; ++index) {
            TINYPY_DECREF(sort_items[index].key);
        }
    }
    if (sort_storage != NULL) {
        tinypy_internal_vm_deallocate(vm, sort_storage, sort_storage_size);
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
    tinypy_value_t *return_value_1 = TINYPY_RET_NONE(vm);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_dict_get_method(tinypy_value_t *function, tinypy_value_t *self, tinypy_value_t *const *items, size_t count, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *result;

    (void)kwargs;
    tinypy_value_t *dict = self;
    tinypy_value_t *key = items[0U];
    if (tinypy_internal_dict_get_optional_checked(vm, dict, key, &result, out_error) == 0) {
        return NULL;
    }
    if (result == NULL && count == 2U) {
        result = items[1U];
    }
    else if (result == NULL) {
        tinypy_value_t *return_value_1 = TINYPY_RET_NONE(vm);
        return return_value_1;
    }
    return TINYPY_RET(result);
}
//////////////////////////////////////////////////////////////////////////
/* dict.fromkeys sizes an empty dictionary once for the keys of a dictionary
   or set and stores them with their hashes without growing the table. */
static tinypy_bool_t __tinypy_dict_fromkeys_table(tinypy_vm_t *vm, tinypy_value_t *result, tinypy_value_t *source_dict, tinypy_value_t *value, tinypy_error_t **out_error) {
    size_t position = 0U;

    if (tinypy_internal_dict_resize_checked(vm, result, TINYPY_DICT_SIZE(source_dict) / 2U * 3U, out_error) == 0) {
        return TINYPY_FALSE;
    }
    for (;;) {
        const tinypy_dict_entry_t *entry = tinypy_internal_dict_next_entry(source_dict, &position);

        if (entry == NULL) {
            return TINYPY_TRUE;
        }
        tinypy_value_t *key = entry->key;
        tinypy_hash_t hash = entry->hash;
        TINYPY_INCREF(key);
        tinypy_bool_t inserted = tinypy_internal_dict_insert_checked(vm, result, key, value, hash, out_error);
        TINYPY_DECREF(key);
        if (inserted == 0) {
            return TINYPY_FALSE;
        }
    }
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_dict_fromkeys_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *result;
    tinypy_value_t *iterator;
    tinypy_value_t *value;
    tinypy_error_t *iteration_error = NULL;

    (void)user_data;
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 1U, 2U, TINYPY_ARITY_STYLE_UNPACK, out_error) == 0) {
        return NULL;
    }
    if (TINYPY_VALUE_KIND(TINYPY_TUPLE_GET(args, 0U)) != TINYPY_VALUE_TYPE ||
        tinypy_type_is_subtype((tinypy_type_t *)TINYPY_TUPLE_GET(args, 0U), &vm->types[TINYPY_VALUE_DICT]) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "dict.fromkeys() requires a dict type", out_error);
        return NULL;
    }
    tinypy_value_t *constructor_args = TINYPY_RET_EMPTY_TUPLE(vm);
    result = tinypy_call(TINYPY_TUPLE_GET(args, 0U), constructor_args, NULL, out_error);
    TINYPY_DECREF(constructor_args);
    if (result == NULL) {
        return NULL;
    }
    tinypy_bool_t exact_dict = result->type == &vm->types[TINYPY_VALUE_DICT] ? TINYPY_TRUE : TINYPY_FALSE;
    value = TINYPY_TUPLE_SIZE(args) == 3U ? TINYPY_TUPLE_GET(args, 2U) : &vm->none_object.base;
    tinypy_value_t *source = TINYPY_TUPLE_GET(args, 1U);
    tinypy_value_t *source_dict = NULL;

    if (source->type == &vm->types[TINYPY_VALUE_DICT]) {
        source_dict = source;
    }
    else if (source->type == &vm->types[TINYPY_VALUE_SET] || source->type == &vm->types[TINYPY_VALUE_FROZENSET]) {
        source_dict = TINYPY_SET_OBJECT(source)->dict;
    }
    if (exact_dict != 0 && TINYPY_DICT_SIZE(result) == 0U && source_dict != NULL) {
        if (__tinypy_dict_fromkeys_table(vm, result, source_dict, value, out_error) == 0) {
            TINYPY_DECREF(result);
            return NULL;
        }
        return result;
    }
    iterator = tinypy_iter(source, out_error);
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
static tinypy_value_t *__tinypy_dict_has_key_method(tinypy_value_t *function, tinypy_value_t *self, tinypy_value_t *const *items, size_t count, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)count;
    (void)kwargs;
    tinypy_value_t *item_2 = items[0U];
    tinypy_bool_t dict_contains;
    if (tinypy_internal_dict_contains_checked(vm, self, item_2, &dict_contains, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *return_value_1 = tinypy_bool_from_i32(vm, dict_contains);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_dict_snapshot(tinypy_value_t *dict_value, int32_t mode, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(dict_value);
    tinypy_value_t *result = tinypy_list_from_items(vm, NULL, 0U);
    tinypy_dict_entry_t *iterator = TINYPY_DICT_ITERATOR_BEGIN(dict_value);
    tinypy_dict_entry_t *iterator_end = TINYPY_DICT_ITERATOR_END(dict_value);

    if (tinypy_internal_list_reserve_checked(vm, result, TINYPY_DICT_SIZE(dict_value), out_error) == 0) {
        TINYPY_DECREF(result);
        return NULL;
    }

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
            if (tinypy_internal_list_append_checked(result, item, out_error) == 0) {
                TINYPY_DECREF(item);
                TINYPY_DECREF(result);
                return NULL;
            }
            TINYPY_DECREF(item);
            continue;
        }
        if (tinypy_internal_list_append_checked(result, item, out_error) == 0) {
            TINYPY_DECREF(result);
            return NULL;
        }
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_dict_list_method(tinypy_value_t *function, tinypy_value_t *self, tinypy_value_t *const *items, size_t count, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    int32_t mode = (int32_t)(intptr_t)TINYPY_NATIVE_FUNCTION_OBJECT(function)->user_data;

    (void)items;
    (void)count;
    (void)kwargs;
    tinypy_value_t *return_value_1 = __tinypy_dict_snapshot(self, mode, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_dict_iter_method(tinypy_value_t *function, tinypy_value_t *self, tinypy_value_t *const *items, size_t count, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    int32_t mode = (int32_t)(intptr_t)TINYPY_NATIVE_FUNCTION_OBJECT(function)->user_data;

    (void)items;
    (void)count;
    (void)kwargs;
    (void)out_error;
    tinypy_value_t *return_value_1 = tinypy_internal_dict_iterator_new(self, mode);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_dict_view_method(tinypy_value_t *function, tinypy_value_t *self, tinypy_value_t *const *items, size_t count, tinypy_value_t *kwargs, tinypy_error_t **out_error) {

    (void)items;
    (void)count;
    (void)kwargs;
    (void)out_error;
    tinypy_value_t *return_value_1 = tinypy_dict_view_new(self, (tinypy_dict_view_kind_e)(intptr_t)TINYPY_NATIVE_FUNCTION_OBJECT(function)->user_data);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_dict_clear_method(tinypy_value_t *function, tinypy_value_t *self, tinypy_value_t *const *items, size_t count, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)items;
    (void)count;
    (void)kwargs;
    (void)out_error;
    tinypy_dict_clear(self);
    tinypy_value_t *return_value_1 = TINYPY_RET_NONE(vm);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_dict_update_from(tinypy_value_t *target, tinypy_value_t *source, tinypy_error_t **out_error) {
    tinypy_bool_t return_value_1 = tinypy_internal_dict_update_from(target, source, "error return without exception set", out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_dict_copy_method(tinypy_value_t *function, tinypy_value_t *self, tinypy_value_t *const *items, size_t count, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)items;
    (void)count;
    (void)kwargs;
    tinypy_value_t *result = tinypy_internal_dict_new_checked(vm, out_error);
    if (result == NULL) {
        return NULL;
    }
    if (__tinypy_dict_update_from(result, self, out_error) == 0) {
        TINYPY_DECREF(result);
        return NULL;
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_dict_update_method(tinypy_value_t *function, tinypy_value_t *self, tinypy_value_t *const *items, size_t count, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    if (tinypy_internal_native_arguments_check(function, count, NULL, 0U, 1U, TINYPY_ARITY_STYLE_UNPACK, out_error) == 0) {
        return NULL;
    }
    if (count == 1U && __tinypy_dict_update_from(self, items[0U], out_error) == 0) {
        return NULL;
    }
    if (kwargs != NULL && __tinypy_dict_update_from(self, kwargs, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *return_value_1 = TINYPY_RET_NONE(vm);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_dict_setdefault_method(tinypy_value_t *function, tinypy_value_t *self, tinypy_value_t *const *items, size_t count, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)kwargs;
    tinypy_value_t *dict = self;
    tinypy_value_t *key = items[0U];
    tinypy_value_t *default_value = count == 2U ? items[1U] : &vm->none_object.base;
    tinypy_value_t *result = tinypy_internal_dict_setdefault_checked(dict, key, default_value, out_error);

    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_dict_pop_method(tinypy_value_t *function, tinypy_value_t *self, tinypy_value_t *const *items, size_t count, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *value;
    size_t index;

    (void)kwargs;
    tinypy_value_t *dict = self;
    tinypy_value_t *key = items[0U];
    if (TINYPY_DICT_SIZE(dict) == 0U) {
        if (count == 2U) {
            value = TINYPY_RET(items[1U]);
            return value;
        }
        tinypy_internal_exception_raise_key_error(vm, key, out_error);
        return NULL;
    }
    if (tinypy_internal_dict_get_optional_index_checked(vm, dict, key, &index, &value, out_error) == 0) {
        return NULL;
    }
    if (value == NULL) {
        if (count == 2U) {
            value = TINYPY_RET(items[1U]);
            return value;
        }
        tinypy_internal_exception_raise_key_error(vm, key, out_error);
        return NULL;
    }
    tinypy_value_t *owned_key;
    tinypy_value_t *owned_value;

    (void)tinypy_internal_dict_delete_index(vm, dict, index, &owned_key, &owned_value);
    TINYPY_DECREF(owned_key);
    return owned_value;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_dict_popitem_method(tinypy_value_t *function, tinypy_value_t *self, tinypy_value_t *const *items, size_t count, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)items;
    (void)count;
    (void)kwargs;
    tinypy_value_t *dict = self;
    tinypy_value_t *entry[2];
    if (tinypy_internal_dict_pop_entry(vm, dict, &entry[0], &entry[1]) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_KEY, "popitem(): dictionary is empty", out_error);
        return NULL;
    }
    tinypy_value_t *result = tinypy_tuple_from_items(vm, entry, 2U);
    TINYPY_DECREF(entry[1]);
    TINYPY_DECREF(entry[0]);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_container_getitem_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {

    (void)user_data;
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 1U, 1U, TINYPY_ARITY_STYLE_SINGLE, out_error) == 0) {
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
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 2U, 2U, TINYPY_ARITY_STYLE_UNPACK, out_error) == 0) {
        return NULL;
    }
    if (tinypy_internal_set_item_builtin(TINYPY_TUPLE_GET(args, 0U), TINYPY_TUPLE_GET(args, 1U), TINYPY_TUPLE_GET(args, 2U), TINYPY_FALSE, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *return_value_1 = TINYPY_RET_NONE(vm);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_container_delitem_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 1U, 1U, TINYPY_ARITY_STYLE_SINGLE, out_error) == 0) {
        return NULL;
    }
    if (tinypy_internal_delete_item_builtin(TINYPY_TUPLE_GET(args, 0U), TINYPY_TUPLE_GET(args, 1U), out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *return_value_1 = TINYPY_RET_NONE(vm);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_container_contains_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 1U, 1U, TINYPY_ARITY_STYLE_SINGLE, out_error) == 0) {
        return NULL;
    }
    int32_t contained = tinypy_internal_contains_builtin(TINYPY_TUPLE_GET(args, 0U), TINYPY_TUPLE_GET(args, 1U), out_error);
    tinypy_value_t *return_value_1 = contained < 0 ? NULL : tinypy_bool_from_i32(vm, contained);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
/* int.__add__(1, 2L) is NotImplemented in Python 2.7: a numeric method only
   accepts operands its own type converts, so the owner type decides. */
static tinypy_bool_t __tinypy_container_numeric_operand_accepted(tinypy_value_t *function, tinypy_value_type_e operand_kind) {
    tinypy_native_function_object_t *native = TINYPY_NATIVE_FUNCTION_OBJECT(function);
    tinypy_type_t *owner = native->owner;

    if (owner == NULL && native->function != NULL) {
        owner = TINYPY_NATIVE_FUNCTION_OBJECT(native->function)->owner;
    }
    if (owner == NULL) {
        return TINYPY_TRUE;
    }
    switch (owner->layout_kind) {
    case TINYPY_VALUE_BOOL:
    case TINYPY_VALUE_INTEGER:
        return operand_kind == TINYPY_VALUE_BOOL || operand_kind == TINYPY_VALUE_INTEGER;
    case TINYPY_VALUE_LONG:
        return operand_kind == TINYPY_VALUE_BOOL || operand_kind == TINYPY_VALUE_INTEGER || operand_kind == TINYPY_VALUE_LONG;
    case TINYPY_VALUE_FLOAT:
        return operand_kind != TINYPY_VALUE_COMPLEX;
    default:
        return TINYPY_TRUE;
    }
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_container_binary_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    intptr_t mode = (intptr_t)user_data;
    intptr_t operation = mode >= 100 ? mode - 100 : mode;
    size_t maximum = operation == 8 ? 2U : 1U;
    tinypy_native_function_object_t *native = TINYPY_NATIVE_FUNCTION_OBJECT(function);
    tinypy_type_t *owner = native->owner;
    if (owner == NULL && native->function != NULL) {
        owner = TINYPY_NATIVE_FUNCTION_OBJECT(native->function)->owner;
    }
    tinypy_bool_t sequence_repeat = operation == 2 && owner != NULL
        && (owner->layout_kind == TINYPY_VALUE_LIST || owner->layout_kind == TINYPY_VALUE_TUPLE
            || owner->layout_kind == TINYPY_VALUE_STRING || owner->layout_kind == TINYPY_VALUE_UNICODE);
    /* wrap_ternaryfunc unpacks its arguments like sq_repeat's wrapper. */
    tinypy_arity_style_e style = sequence_repeat != TINYPY_FALSE || operation == 8 ? TINYPY_ARITY_STYLE_UNPACK : TINYPY_ARITY_STYLE_WRAPPER;

    if (tinypy_internal_native_method_arguments(function, args, kwargs, 1U, maximum, style, out_error) == TINYPY_FALSE) {
        return NULL;
    }
    tinypy_value_t *left = TINYPY_TUPLE_GET(args, mode >= 100 ? 1U : 0U);
    tinypy_value_t *right = TINYPY_TUPLE_GET(args, mode >= 100 ? 0U : 1U);
    if (mode >= 100) {
        mode -= 100;
    }
    if (mode == 8 && TINYPY_TUPLE_SIZE(args) == 3U) {
        tinypy_value_t *power = tinypy_internal_builtin_power_slot(TINYPY_VALUE_KIND(TINYPY_TUPLE_GET(args, 0U)), left, right, TINYPY_TUPLE_GET(args, 2U), out_error);
        return power;
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
        tinypy_value_t *result = TINYPY_RET_NOT_IMPLEMENTED(vm);
        return result;
    }
    if (self_numeric != 0 && (__tinypy_container_numeric_operand_accepted(function, left_kind) == 0 || __tinypy_container_numeric_operand_accepted(function, right_kind) == 0)) {
        tinypy_value_t *result = TINYPY_RET_NOT_IMPLEMENTED(vm);
        return result;
    }
    /* The __add__ of str, unicode, list and tuple wraps sq_concat, which
       never declines an operand. */
    if (mode == 0 && self_numeric == 0) {
        tinypy_value_t *concatenated = tinypy_internal_sequence_concat(TINYPY_TUPLE_GET(args, 0U), TINYPY_TUPLE_GET(args, 1U), out_error);
        return concatenated;
    }
    /* string_mod and unicode_mod only format a left operand of their type. */
    if (mode == 6 && self_numeric == 0 && left_kind != self_kind) {
        tinypy_value_t *result = TINYPY_RET_NOT_IMPLEMENTED(vm);
        return result;
    }
    if (mode == 2 && self_numeric == 0 && (left_sequence != 0 || right_sequence != 0)) {
        tinypy_value_t *sequence = TINYPY_TUPLE_GET(args, 0U);
        tinypy_value_t *multiplier_value = TINYPY_TUPLE_GET(args, 1U);
        tinypy_value_type_e multiplier = TINYPY_VALUE_KIND(multiplier_value);

        if (multiplier != TINYPY_VALUE_BOOL && multiplier != TINYPY_VALUE_INTEGER && multiplier != TINYPY_VALUE_LONG) {
            int64_t count;

            if (tinypy_internal_number_as_index(multiplier_value, TINYPY_ERROR_OVERFLOW, &count, out_error) == 0) {
                return NULL;
            }
            tinypy_value_t *count_value = tinypy_integer_from_i64(vm, count);
            tinypy_value_t *result = tinypy_internal_operator_builtin(sequence, count_value, (int32_t)mode, out_error);

            TINYPY_DECREF(count_value);
            return result;
        }
    }
    tinypy_value_t *result = tinypy_internal_operator_builtin(left, right, (int32_t)mode, out_error);

    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_container_compare_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    if (tinypy_internal_native_method_arguments(function, args, kwargs, 1U, 1U, TINYPY_ARITY_STYLE_WRAPPER, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *left = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *right = TINYPY_TUPLE_GET(args, 1U);
    tinypy_compare_operation_e operation = (tinypy_compare_operation_e)(intptr_t)user_data;
    tinypy_value_type_e left_kind = TINYPY_VALUE_KIND(left);
    tinypy_value_type_e right_kind = TINYPY_VALUE_KIND(right);
    tinypy_bool_t compatible = left_kind == right_kind ? TINYPY_TRUE : TINYPY_FALSE;

    /* float_richcompare leaves complex operands, string_richcompare unicode
       ones and dict_richcompare any ordering to the other operand. */
    if (left_kind == TINYPY_VALUE_BOOL || left_kind == TINYPY_VALUE_INTEGER || left_kind == TINYPY_VALUE_LONG || left_kind == TINYPY_VALUE_FLOAT || left_kind == TINYPY_VALUE_COMPLEX) {
        compatible = right_kind == TINYPY_VALUE_BOOL || right_kind == TINYPY_VALUE_INTEGER || right_kind == TINYPY_VALUE_LONG || right_kind == TINYPY_VALUE_FLOAT || (right_kind == TINYPY_VALUE_COMPLEX && left_kind != TINYPY_VALUE_FLOAT) ? TINYPY_TRUE : TINYPY_FALSE;
    }
    else if (left_kind == TINYPY_VALUE_STRING) {
        compatible = right_kind == TINYPY_VALUE_STRING ? TINYPY_TRUE : TINYPY_FALSE;
    }
    else if (left_kind == TINYPY_VALUE_UNICODE) {
        compatible = right_kind == TINYPY_VALUE_STRING || right_kind == TINYPY_VALUE_UNICODE || right_kind == TINYPY_VALUE_BUFFER ? TINYPY_TRUE : TINYPY_FALSE;
    }
    else if (left_kind == TINYPY_VALUE_DICT) {
        compatible = right_kind == TINYPY_VALUE_DICT && (operation == TINYPY_COMPARE_EQUAL || operation == TINYPY_COMPARE_NOT_EQUAL) ? TINYPY_TRUE : TINYPY_FALSE;
    }
    else if (left_kind == TINYPY_VALUE_SET || left_kind == TINYPY_VALUE_FROZENSET) {
        compatible = right_kind == TINYPY_VALUE_SET || right_kind == TINYPY_VALUE_FROZENSET ? TINYPY_TRUE : TINYPY_FALSE;
    }
    else if (left_kind == TINYPY_VALUE_BYTEARRAY) {
        const uint8_t *bytes;
        size_t size;

        compatible = tinypy_internal_bytes_view(right, &bytes, &size);
    }
    if (compatible == 0) {
        tinypy_value_t *not_implemented = TINYPY_RET_NOT_IMPLEMENTED(vm);
        return not_implemented;
    }
    tinypy_value_t *return_value_1 = tinypy_internal_compare_builtin_value(left, right, operation, out_error);
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

    if (tinypy_internal_native_method_arguments(function, args, kwargs, 1U, 1U, TINYPY_ARITY_STYLE_WRAPPER, out_error) == 0) {
        return NULL;
    }
    left = TINYPY_TUPLE_GET(args, 0U);
    right = TINYPY_TUPLE_GET(args, 1U);
    tinypy_value_type_e right_kind = TINYPY_VALUE_KIND(right);
    tinypy_bool_t accepted = expected == TINYPY_VALUE_DICT ? right_kind == TINYPY_VALUE_DICT
        : right_kind == TINYPY_VALUE_SET || right_kind == TINYPY_VALUE_FROZENSET;
    if (accepted == TINYPY_FALSE) {
        const tinypy_type_t *owner = left->type;
        tinypy_message_part_t parts[] = {
            {owner->name, owner->name_size},
            TINYPY_MESSAGE_PART_LITERAL(".__cmp__(x,y) requires y to be a '"),
            {owner->name, owner->name_size},
            TINYPY_MESSAGE_PART_LITERAL("', not a '"),
            TINYPY_MESSAGE_PART_TYPE_NAME(right),
            TINYPY_MESSAGE_PART_LITERAL("'"),
        };

        tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
        return NULL;
    }
    if (expected == TINYPY_VALUE_DICT) {
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
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "cannot compare sets using cmp()", out_error);
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_container_unary_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    intptr_t mode = (intptr_t)user_data;

    if (tinypy_internal_native_method_arguments(function, args, kwargs, 0U, 0U, TINYPY_ARITY_STYLE_WRAPPER, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *result = tinypy_internal_unary_builtin(TINYPY_TUPLE_GET(args, 0U), (int32_t)mode, out_error);

    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_container_conversion_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    intptr_t mode = (intptr_t)user_data;

    if (tinypy_internal_native_method_arguments(function, args, kwargs, 0U, 0U, TINYPY_ARITY_STYLE_WRAPPER, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *value = TINYPY_TUPLE_GET(args, 0U);
    if (mode == 3) {
        if (TINYPY_VALUE_KIND(value) != TINYPY_VALUE_INTEGER && TINYPY_VALUE_KIND(value) != TINYPY_VALUE_LONG && TINYPY_VALUE_KIND(value) != TINYPY_VALUE_BOOL) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "__index__ requires an integer", out_error);
            return NULL;
        }
        if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_BOOL) {
            tinypy_value_t *result = tinypy_integer_from_i64(vm, TINYPY_INTEGER_VALUE(value));
            return result;
        }
        TINYPY_INCREF(value);
        tinypy_value_t *result = tinypy_internal_immutable_subclass_copy(&vm->types[TINYPY_VALUE_KIND(value)], value, out_error);
        return result;
    }
    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_COMPLEX) {
        const char *message = mode == 0 ? "can't convert complex to int" : (mode == 1 ? "can't convert complex to long" : "can't convert complex to float");

        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, message, out_error);
        return NULL;
    }
    tinypy_value_type_e source_kind = TINYPY_VALUE_KIND(value);
    TINYPY_INCREF(value);
    tinypy_value_t *exact = source_kind == TINYPY_VALUE_BOOL ? value : tinypy_internal_immutable_subclass_copy(&vm->types[source_kind], value, out_error);
    tinypy_value_t *arguments = tinypy_tuple_from_items(vm, &exact, 1U);
    TINYPY_DECREF(exact);
    tinypy_type_t *target = mode == 0 ? &vm->types[TINYPY_VALUE_INTEGER] : (mode == 1 ? &vm->types[TINYPY_VALUE_LONG] : &vm->types[TINYPY_VALUE_FLOAT]);
    tinypy_value_t *result = target->create(target, arguments, NULL, out_error);
    TINYPY_DECREF(arguments);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_container_nonzero_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 0U, 0U, TINYPY_ARITY_STYLE_WRAPPER, out_error) == 0) {
        return NULL;
    }
    int32_t truth = tinypy_internal_truth_builtin(TINYPY_TUPLE_GET(args, 0U), out_error);
    tinypy_value_t *return_value_1 = truth < 0 ? NULL : tinypy_bool_from_i32(vm, truth);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_container_iter_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {

    (void)user_data;
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 0U, 0U, TINYPY_ARITY_STYLE_WRAPPER, out_error) == 0) {
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
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 0U, 0U, TINYPY_ARITY_STYLE_WRAPPER, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *value = TINYPY_TUPLE_GET(args, 0U);
    size = TINYPY_VALUE_KIND(value) == TINYPY_VALUE_DICT ? TINYPY_DICT_SIZE(value) : TINYPY_SIZED_SIZE(value);
    tinypy_value_t *return_value_1 = tinypy_integer_from_i64(vm, (int64_t)size);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_container_repr_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {

    (void)user_data;
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 0U, 0U, TINYPY_ARITY_STYLE_WRAPPER, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *value = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *return_value_1 = tinypy_internal_object_repr_builtin(value, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_container_str_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {

    (void)user_data;
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 0U, 0U, TINYPY_ARITY_STYLE_WRAPPER, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *return_value_1 = tinypy_internal_object_str_builtin(TINYPY_TUPLE_GET(args, 0U), out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_container_hash_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 0U, 0U, TINYPY_ARITY_STYLE_WRAPPER, out_error) == 0) {
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
    tinypy_value_t *self = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(self);
    tinypy_bool_t numeric = kind == TINYPY_VALUE_BOOL || kind == TINYPY_VALUE_INTEGER || kind == TINYPY_VALUE_LONG || kind == TINYPY_VALUE_FLOAT || kind == TINYPY_VALUE_COMPLEX;

    (void)user_data;
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 1U, 1U, TINYPY_ARITY_STYLE_PARSED, out_error) == TINYPY_FALSE) {
        return NULL;
    }
    tinypy_value_t *spec = TINYPY_TUPLE_GET(args, 1U);
    tinypy_value_type_e spec_kind = TINYPY_VALUE_KIND(spec);
    if (spec_kind != TINYPY_VALUE_STRING && spec_kind != TINYPY_VALUE_UNICODE && numeric != TINYPY_FALSE) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "__format__ requires str or unicode", out_error);
        return NULL;
    }
    if (spec_kind != TINYPY_VALUE_STRING && spec_kind != TINYPY_VALUE_UNICODE) {
        const tinypy_message_part_t parts[] = {
            TINYPY_MESSAGE_PART_LITERAL("__format__ arg must be str or unicode, not "),
            TINYPY_MESSAGE_PART_TYPE_NAME(spec)
        };

        tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
        return NULL;
    }
    /* Only unicode.__format__ reads a unicode spec as unicode; the others
       take its str(). An empty spec formats as str() or unicode() of the
       value, which honours an overriding subtype. */
    tinypy_value_t *converted = kind != TINYPY_VALUE_UNICODE && spec_kind == TINYPY_VALUE_UNICODE ? tinypy_object_str(spec, out_error) : NULL;
    if (kind != TINYPY_VALUE_UNICODE && spec_kind == TINYPY_VALUE_UNICODE && converted == NULL) {
        return NULL;
    }
    tinypy_value_t *format_spec = converted != NULL ? converted : spec;
    tinypy_value_t *result;
    if (TINYPY_TEXT_BYTE_SIZE(format_spec) == 0U) {
        result = kind == TINYPY_VALUE_UNICODE ? tinypy_internal_object_unicode(self, out_error) : tinypy_object_str(self, out_error);
    }
    else {
        result = tinypy_internal_string_format_builtin_value(vm, self, 0, TINYPY_TEXT_BYTES(format_spec), TINYPY_TEXT_BYTE_SIZE(format_spec), kind == TINYPY_VALUE_UNICODE && spec_kind == TINYPY_VALUE_UNICODE ? TINYPY_TRUE : TINYPY_FALSE, &result_unicode, out_error);
    }
    if (converted != NULL) {
        TINYPY_DECREF(converted);
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* Legacy slice slots receive C integer bounds and clamp negatives to zero. */
tinypy_value_t *tinypy_internal_legacy_slice_new(tinypy_value_t *args, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(args);
    int64_t bounds[2];

    for (size_t index = 0U; index < 2U; ++index) {
        tinypy_value_t *bound = TINYPY_TUPLE_GET(args, index + 1U);

        if (TINYPY_VALUE_KIND(bound) == TINYPY_VALUE_FLOAT) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "integer argument expected, got float", out_error);
            return NULL;
        }
        if (tinypy_internal_number_as_i64(bound, &bounds[index], out_error) == 0) {
            return NULL;
        }
        if (bounds[index] < 0) {
            bounds[index] = 0;
        }
    }
    tinypy_value_t *start = tinypy_integer_from_i64(vm, bounds[0]);
    tinypy_value_t *stop = tinypy_integer_from_i64(vm, bounds[1]);
    tinypy_value_t *slice = tinypy_slice_new(vm, start, stop, NULL);

    TINYPY_DECREF(stop);
    TINYPY_DECREF(start);
    return slice;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_container_getslice_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {

    (void)user_data;
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 2U, 2U, TINYPY_ARITY_STYLE_PARSED, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *slice = tinypy_internal_legacy_slice_new(args, out_error);
    if (slice == NULL) {
        return NULL;
    }
    tinypy_value_t *result = tinypy_internal_get_item_builtin(TINYPY_TUPLE_GET(args, 0U), slice, out_error);
    TINYPY_DECREF(slice);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_container_setslice_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 3U, 3U, TINYPY_ARITY_STYLE_PARSED, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *slice = tinypy_internal_legacy_slice_new(args, out_error);
    if (slice == NULL) {
        return NULL;
    }
    tinypy_bool_t assigned = tinypy_internal_set_item_builtin(TINYPY_TUPLE_GET(args, 0U), slice, TINYPY_TUPLE_GET(args, 3U), TINYPY_TRUE, out_error);
    TINYPY_DECREF(slice);
    if (assigned == 0) {
        return NULL;
    }
    tinypy_value_t *return_value_1 = TINYPY_RET_NONE(vm);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_container_delslice_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 2U, 2U, TINYPY_ARITY_STYLE_PARSED, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *slice = tinypy_internal_legacy_slice_new(args, out_error);
    if (slice == NULL) {
        return NULL;
    }
    tinypy_bool_t deleted = tinypy_internal_delete_item_builtin(TINYPY_TUPLE_GET(args, 0U), slice, out_error);
    TINYPY_DECREF(slice);
    if (deleted == 0) {
        return NULL;
    }
    tinypy_value_t *return_value_1 = TINYPY_RET_NONE(vm);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_container_reversed_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {

    (void)user_data;
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 0U, 0U, TINYPY_ARITY_STYLE_PARSED, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *return_value_1 = tinypy_internal_reversed_new(TINYPY_TUPLE_GET(args, 0U), out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_container_getnewargs_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 0U, 0U, TINYPY_ARITY_STYLE_PARSED, out_error) == 0) {
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
    tinypy_internal_type_add_method(type, type->vm->internal_special_lt_key, __tinypy_container_compare_method, (void *)(intptr_t)TINYPY_COMPARE_LESS, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(type, type->vm->internal_special_le_key, __tinypy_container_compare_method, (void *)(intptr_t)TINYPY_COMPARE_LESS_EQUAL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(type, type->vm->internal_special_eq_key, __tinypy_container_compare_method, (void *)(intptr_t)TINYPY_COMPARE_EQUAL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(type, type->vm->internal_special_ne_key, __tinypy_container_compare_method, (void *)(intptr_t)TINYPY_COMPARE_NOT_EQUAL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(type, type->vm->internal_special_gt_key, __tinypy_container_compare_method, (void *)(intptr_t)TINYPY_COMPARE_GREATER, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(type, type->vm->internal_special_ge_key, __tinypy_container_compare_method, (void *)(intptr_t)TINYPY_COMPARE_GREATER_EQUAL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_container_add_sequence_protocol(tinypy_type_t *type, tinypy_bool_t mutable, tinypy_bool_t explicit_iter) {
    if (explicit_iter != 0) {
        tinypy_internal_type_add_method(type, type->vm->internal_special_iter_key, __tinypy_container_iter_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    }
    tinypy_internal_type_add_method(type, type->vm->internal_special_length_key, __tinypy_container_len_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    if (type->layout_kind == TINYPY_VALUE_LIST || type->layout_kind == TINYPY_VALUE_DICT) {
        tinypy_internal_type_add_method(type, type->vm->internal_special_getitem_key, __tinypy_container_getitem_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_METHOD);
    }
    else {
        tinypy_internal_type_add_method(type, type->vm->internal_special_getitem_key, __tinypy_container_getitem_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    }
    if (type->layout_kind == TINYPY_VALUE_DICT) {
        tinypy_internal_type_add_method(type, type->vm->internal_special_contains_key, __tinypy_container_contains_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_METHOD);
    }
    else {
        tinypy_internal_type_add_method(type, type->vm->internal_special_contains_key, __tinypy_container_contains_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    }
    tinypy_internal_type_add_method(type, type->vm->internal_special_repr_key, __tinypy_container_repr_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    if (type->layout_kind == TINYPY_VALUE_STRING || type->layout_kind == TINYPY_VALUE_UNICODE || type->layout_kind == TINYPY_VALUE_BYTEARRAY) {
        tinypy_internal_type_add_method(type, type->vm->internal_special_str_key, __tinypy_container_str_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    }
    if (mutable != 0) {
        tinypy_internal_type_add_method(type, type->vm->internal_special_setitem_key, __tinypy_container_setitem_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
        tinypy_internal_type_add_method(type, type->vm->internal_special_delitem_key, __tinypy_container_delitem_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    }
    __tinypy_container_add_comparisons(type);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_container_add_sequence_arithmetic(tinypy_type_t *type, tinypy_bool_t modulo) {
    tinypy_internal_type_add_method(type, type->vm->internal_special_add_key, __tinypy_container_binary_method, (void *)(intptr_t)0, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(type, type->vm->internal_special_mul_key, __tinypy_container_binary_method, (void *)(intptr_t)2, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(type, type->vm->internal_special_rmul_key, __tinypy_container_binary_method, (void *)(intptr_t)102, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    if (modulo != 0) {
        tinypy_internal_type_add_method(type, type->vm->internal_special_mod_key, __tinypy_container_binary_method, (void *)(intptr_t)6, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
        tinypy_internal_type_add_method(type, type->vm->internal_special_rmod_key, __tinypy_container_binary_method, (void *)(intptr_t)106, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    }
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_container_add_numeric_protocol(tinypy_type_t *type, tinypy_bool_t integer, tinypy_bool_t comparisons) {
    const struct {
        tinypy_value_t *name;
        intptr_t mode;
    } operations[] = {
        {type->vm->internal_special_add_key, 0}, {type->vm->internal_special_radd_key, 100},
        {type->vm->internal_special_sub_key, 1}, {type->vm->internal_special_rsub_key, 101},
        {type->vm->internal_special_mul_key, 2}, {type->vm->internal_special_rmul_key, 102},
        {type->vm->internal_special_div_key, 3}, {type->vm->internal_special_rdiv_key, 103},
        {type->vm->internal_special_floordiv_key, 4}, {type->vm->internal_special_rfloordiv_key, 104},
        {type->vm->internal_special_truediv_key, 5}, {type->vm->internal_special_rtruediv_key, 105},
        {type->vm->internal_special_mod_key, 6}, {type->vm->internal_special_rmod_key, 106},
        {type->vm->internal_special_divmod_key, 7}, {type->vm->internal_special_rdivmod_key, 107},
        {type->vm->internal_special_pow_key, 8}, {type->vm->internal_special_rpow_key, 108}
    };
    size_t index;

    for (index = 0U; index < sizeof(operations) / sizeof(operations[0]); ++index) {
        tinypy_value_t *method_name = operations[index].name;
        tinypy_internal_type_add_method(type, method_name, __tinypy_container_binary_method, (void *)operations[index].mode, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    }
    if (integer != 0) {
        const struct {
            tinypy_value_t *name;
            intptr_t mode;
        } integer_operations[] = {
            {type->vm->internal_special_lshift_key, 9}, {type->vm->internal_special_rlshift_key, 109},
            {type->vm->internal_special_rshift_key, 10}, {type->vm->internal_special_rrshift_key, 110},
            {type->vm->internal_special_and_key, 11}, {type->vm->internal_special_rand_key, 111},
            {type->vm->internal_special_xor_key, 12}, {type->vm->internal_special_rxor_key, 112},
            {type->vm->internal_special_or_key, 13}, {type->vm->internal_special_ror_key, 113}
        };

        for (index = 0U; index < sizeof(integer_operations) / sizeof(integer_operations[0]); ++index) {
            tinypy_value_t *method_name = integer_operations[index].name;
            tinypy_internal_type_add_method(type, method_name, __tinypy_container_binary_method, (void *)integer_operations[index].mode, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
        }
        tinypy_internal_type_add_method(type, type->vm->internal_special_invert_key, __tinypy_container_unary_method, (void *)(intptr_t)2, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
        tinypy_internal_type_add_method(type, type->vm->internal_special_index_key, __tinypy_container_conversion_method, (void *)(intptr_t)3, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    }
    tinypy_internal_type_add_method(type, type->vm->internal_special_pos_key, __tinypy_container_unary_method, (void *)(intptr_t)0, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(type, type->vm->internal_special_neg_key, __tinypy_container_unary_method, (void *)(intptr_t)1, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(type, type->vm->internal_special_abs_key, __tinypy_container_unary_method, (void *)(intptr_t)3, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(type, type->vm->internal_special_int_key, __tinypy_container_conversion_method, (void *)(intptr_t)0, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(type, type->vm->internal_special_long_key, __tinypy_container_conversion_method, (void *)(intptr_t)1, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(type, type->vm->internal_special_float_key, __tinypy_container_conversion_method, (void *)(intptr_t)2, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(type, type->vm->internal_special_nonzero_key, __tinypy_container_nonzero_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(type, type->vm->internal_special_repr_key, __tinypy_container_repr_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(type, type->vm->internal_special_str_key, __tinypy_container_str_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(type, type->vm->internal_special_hash_key, __tinypy_container_hash_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(type, type->vm->internal_special_format_key, __tinypy_container_format_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    if (comparisons != 0) {
        __tinypy_container_add_comparisons(type);
    }
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_container_add_bool_protocol(tinypy_type_t *type) {
    const struct {
        tinypy_value_t *name;
        intptr_t mode;
    } operations[] = {
        {type->vm->internal_special_and_key, 11}, {type->vm->internal_special_rand_key, 111},
        {type->vm->internal_special_xor_key, 12}, {type->vm->internal_special_rxor_key, 112},
        {type->vm->internal_special_or_key, 13}, {type->vm->internal_special_ror_key, 113}
    };
    size_t index;

    for (index = 0U; index < sizeof(operations) / sizeof(operations[0]); ++index) {
        tinypy_value_t *method_name = operations[index].name;
        tinypy_internal_type_add_method(type, method_name, __tinypy_container_binary_method, (void *)operations[index].mode, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    }
    tinypy_internal_type_add_method(type, type->vm->internal_special_repr_key, __tinypy_container_repr_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(type, type->vm->internal_special_str_key, __tinypy_container_str_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_initialize_container_types(tinypy_vm_t *vm) {
    tinypy_internal_type_add_items_method(&vm->types[TINYPY_VALUE_TUPLE], vm->internal_count_key, __tinypy_sequence_count_method, NULL, 1U, 1U, TINYPY_ARITY_STYLE_SINGLE);
    tinypy_internal_type_add_items_method(&vm->types[TINYPY_VALUE_TUPLE], vm->internal_index_key, __tinypy_sequence_index_method, NULL, 1U, 3U, TINYPY_ARITY_STYLE_PARSED);
    tinypy_internal_type_add_method(&vm->types[TINYPY_VALUE_TUPLE], vm->internal_special_iter_key, __tinypy_container_iter_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(&vm->types[TINYPY_VALUE_TUPLE], vm->internal_special_length_key, __tinypy_container_len_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(&vm->types[TINYPY_VALUE_TUPLE], vm->internal_special_getitem_key, __tinypy_container_getitem_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(&vm->types[TINYPY_VALUE_TUPLE], vm->internal_special_repr_key, __tinypy_container_repr_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(&vm->types[TINYPY_VALUE_TUPLE], vm->internal_special_getslice_key, __tinypy_container_getslice_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(&vm->types[TINYPY_VALUE_TUPLE], vm->internal_special_getnewargs_key, __tinypy_container_getnewargs_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_items_method(&vm->types[TINYPY_VALUE_LIST], vm->internal_append_key, __tinypy_list_append_method, NULL, 1U, 1U, TINYPY_ARITY_STYLE_SINGLE);
    tinypy_internal_type_add_items_method(&vm->types[TINYPY_VALUE_LIST], vm->internal_extend_key, __tinypy_list_extend_method, NULL, 1U, 1U, TINYPY_ARITY_STYLE_SINGLE);
    tinypy_internal_type_add_items_method(&vm->types[TINYPY_VALUE_LIST], vm->internal_insert_key, __tinypy_list_insert_method, NULL, 2U, 2U, TINYPY_ARITY_STYLE_PARSED);
    tinypy_internal_type_add_items_method(&vm->types[TINYPY_VALUE_LIST], vm->internal_pop_key, __tinypy_list_pop_method, NULL, 0U, 1U, TINYPY_ARITY_STYLE_PARSED);
    tinypy_internal_type_add_items_method(&vm->types[TINYPY_VALUE_LIST], vm->internal_remove_key, __tinypy_list_remove_method, NULL, 1U, 1U, TINYPY_ARITY_STYLE_SINGLE);
    tinypy_internal_type_add_items_method(&vm->types[TINYPY_VALUE_LIST], vm->internal_count_key, __tinypy_sequence_count_method, NULL, 1U, 1U, TINYPY_ARITY_STYLE_SINGLE);
    tinypy_internal_type_add_items_method(&vm->types[TINYPY_VALUE_LIST], vm->internal_index_key, __tinypy_sequence_index_method, NULL, 1U, 3U, TINYPY_ARITY_STYLE_PARSED);
    tinypy_internal_type_add_items_method(&vm->types[TINYPY_VALUE_LIST], vm->internal_reverse_key, __tinypy_list_reverse_method, NULL, 0U, 0U, TINYPY_ARITY_STYLE_PARSED);
    tinypy_internal_type_add_items_method(&vm->types[TINYPY_VALUE_LIST], vm->internal_sort_key, __tinypy_list_sort_method, NULL, 0U, 3U, TINYPY_ARITY_STYLE_UNCHECKED);
    tinypy_internal_type_add_method(&vm->types[TINYPY_VALUE_LIST], vm->internal_special_iter_key, __tinypy_container_iter_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(&vm->types[TINYPY_VALUE_LIST], vm->internal_special_length_key, __tinypy_container_len_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(&vm->types[TINYPY_VALUE_LIST], vm->internal_special_getitem_key, __tinypy_container_getitem_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(&vm->types[TINYPY_VALUE_LIST], vm->internal_special_repr_key, __tinypy_container_repr_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(&vm->types[TINYPY_VALUE_LIST], vm->internal_special_iadd_key, __tinypy_list_inplace_add_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(&vm->types[TINYPY_VALUE_LIST], vm->internal_special_imul_key, __tinypy_list_inplace_multiply_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(&vm->types[TINYPY_VALUE_LIST], vm->internal_special_getslice_key, __tinypy_container_getslice_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(&vm->types[TINYPY_VALUE_LIST], vm->internal_special_setslice_key, __tinypy_container_setslice_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(&vm->types[TINYPY_VALUE_LIST], vm->internal_special_delslice_key, __tinypy_container_delslice_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(&vm->types[TINYPY_VALUE_LIST], vm->internal_special_reversed_key, __tinypy_container_reversed_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_items_method(&vm->types[TINYPY_VALUE_DICT], vm->internal_get_key, __tinypy_dict_get_method, NULL, 1U, 2U, TINYPY_ARITY_STYLE_UNPACK);
    tinypy_internal_type_add_class_method(&vm->types[TINYPY_VALUE_DICT], vm->internal_fromkeys_key, __tinypy_dict_fromkeys_method, NULL, NULL);
    tinypy_internal_type_add_items_method(&vm->types[TINYPY_VALUE_DICT], vm->internal_has_key_key, __tinypy_dict_has_key_method, NULL, 1U, 1U, TINYPY_ARITY_STYLE_SINGLE);
    tinypy_internal_type_add_items_method(&vm->types[TINYPY_VALUE_DICT], vm->internal_keys_key, __tinypy_dict_list_method, (void *)(intptr_t)0, 0U, 0U, TINYPY_ARITY_STYLE_PARSED);
    tinypy_internal_type_add_items_method(&vm->types[TINYPY_VALUE_DICT], vm->internal_values_key, __tinypy_dict_list_method, (void *)(intptr_t)1, 0U, 0U, TINYPY_ARITY_STYLE_PARSED);
    tinypy_internal_type_add_items_method(&vm->types[TINYPY_VALUE_DICT], vm->internal_items_key, __tinypy_dict_list_method, (void *)(intptr_t)2, 0U, 0U, TINYPY_ARITY_STYLE_PARSED);
    tinypy_internal_type_add_items_method(&vm->types[TINYPY_VALUE_DICT], vm->internal_iterkeys_key, __tinypy_dict_iter_method, (void *)(intptr_t)0, 0U, 0U, TINYPY_ARITY_STYLE_PARSED);
    tinypy_internal_type_add_items_method(&vm->types[TINYPY_VALUE_DICT], vm->internal_itervalues_key, __tinypy_dict_iter_method, (void *)(intptr_t)1, 0U, 0U, TINYPY_ARITY_STYLE_PARSED);
    tinypy_internal_type_add_items_method(&vm->types[TINYPY_VALUE_DICT], vm->internal_iteritems_key, __tinypy_dict_iter_method, (void *)(intptr_t)2, 0U, 0U, TINYPY_ARITY_STYLE_PARSED);
    tinypy_internal_type_add_items_method(&vm->types[TINYPY_VALUE_DICT], vm->internal_viewkeys_key, __tinypy_dict_view_method, (void *)(intptr_t)TINYPY_DICT_VIEW_KEYS, 0U, 0U, TINYPY_ARITY_STYLE_PARSED);
    tinypy_internal_type_add_items_method(&vm->types[TINYPY_VALUE_DICT], vm->internal_viewvalues_key, __tinypy_dict_view_method, (void *)(intptr_t)TINYPY_DICT_VIEW_VALUES, 0U, 0U, TINYPY_ARITY_STYLE_PARSED);
    tinypy_internal_type_add_items_method(&vm->types[TINYPY_VALUE_DICT], vm->internal_viewitems_key, __tinypy_dict_view_method, (void *)(intptr_t)TINYPY_DICT_VIEW_ITEMS, 0U, 0U, TINYPY_ARITY_STYLE_PARSED);
    tinypy_internal_type_add_items_method(&vm->types[TINYPY_VALUE_DICT], vm->internal_clear_key, __tinypy_dict_clear_method, NULL, 0U, 0U, TINYPY_ARITY_STYLE_PARSED);
    tinypy_internal_type_add_items_method(&vm->types[TINYPY_VALUE_DICT], vm->internal_copy_key, __tinypy_dict_copy_method, NULL, 0U, 0U, TINYPY_ARITY_STYLE_PARSED);
    tinypy_internal_type_add_items_method(&vm->types[TINYPY_VALUE_DICT], vm->internal_update_key, __tinypy_dict_update_method, NULL, 0U, 1U, TINYPY_ARITY_STYLE_UNCHECKED);
    tinypy_internal_type_add_items_method(&vm->types[TINYPY_VALUE_DICT], vm->internal_setdefault_key, __tinypy_dict_setdefault_method, NULL, 1U, 2U, TINYPY_ARITY_STYLE_UNPACK);
    tinypy_internal_type_add_items_method(&vm->types[TINYPY_VALUE_DICT], vm->internal_pop_key, __tinypy_dict_pop_method, NULL, 1U, 2U, TINYPY_ARITY_STYLE_UNPACK);
    tinypy_internal_type_add_items_method(&vm->types[TINYPY_VALUE_DICT], vm->internal_popitem_key, __tinypy_dict_popitem_method, NULL, 0U, 0U, TINYPY_ARITY_STYLE_PARSED);
    tinypy_internal_type_add_method(&vm->types[TINYPY_VALUE_DICT], vm->internal_special_iter_key, __tinypy_container_iter_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(&vm->types[TINYPY_VALUE_DICT], vm->internal_special_length_key, __tinypy_container_len_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(&vm->types[TINYPY_VALUE_DICT], vm->internal_special_getitem_key, __tinypy_container_getitem_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(&vm->types[TINYPY_VALUE_DICT], vm->internal_special_repr_key, __tinypy_container_repr_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(&vm->types[TINYPY_VALUE_DICT], vm->internal_special_cmp_key, __tinypy_container_cmp_method, (void *)(intptr_t)TINYPY_VALUE_DICT, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    __tinypy_container_add_sequence_protocol(&vm->types[TINYPY_VALUE_TUPLE], TINYPY_FALSE, TINYPY_TRUE);
    __tinypy_container_add_sequence_arithmetic(&vm->types[TINYPY_VALUE_TUPLE], TINYPY_FALSE);
    __tinypy_container_add_sequence_protocol(&vm->types[TINYPY_VALUE_LIST], TINYPY_TRUE, TINYPY_TRUE);
    __tinypy_container_add_sequence_arithmetic(&vm->types[TINYPY_VALUE_LIST], TINYPY_FALSE);
    __tinypy_container_add_sequence_protocol(&vm->types[TINYPY_VALUE_STRING], TINYPY_FALSE, TINYPY_FALSE);
    __tinypy_container_add_sequence_arithmetic(&vm->types[TINYPY_VALUE_STRING], TINYPY_TRUE);
    tinypy_internal_type_add_method(&vm->types[TINYPY_VALUE_STRING], vm->internal_special_hash_key, __tinypy_container_hash_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(&vm->types[TINYPY_VALUE_STRING], vm->internal_special_format_key, __tinypy_container_format_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(&vm->types[TINYPY_VALUE_STRING], vm->internal_special_getslice_key, __tinypy_container_getslice_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(&vm->types[TINYPY_VALUE_STRING], vm->internal_special_getnewargs_key, __tinypy_container_getnewargs_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    __tinypy_container_add_sequence_protocol(&vm->types[TINYPY_VALUE_UNICODE], TINYPY_FALSE, TINYPY_FALSE);
    __tinypy_container_add_sequence_arithmetic(&vm->types[TINYPY_VALUE_UNICODE], TINYPY_TRUE);
    tinypy_internal_type_add_method(&vm->types[TINYPY_VALUE_UNICODE], vm->internal_special_hash_key, __tinypy_container_hash_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(&vm->types[TINYPY_VALUE_UNICODE], vm->internal_special_format_key, __tinypy_container_format_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(&vm->types[TINYPY_VALUE_UNICODE], vm->internal_special_getslice_key, __tinypy_container_getslice_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(&vm->types[TINYPY_VALUE_UNICODE], vm->internal_special_getnewargs_key, __tinypy_container_getnewargs_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    __tinypy_container_add_sequence_protocol(&vm->types[TINYPY_VALUE_BYTEARRAY], TINYPY_TRUE, TINYPY_TRUE);
    __tinypy_container_add_sequence_protocol(&vm->types[TINYPY_VALUE_DICT], TINYPY_TRUE, TINYPY_TRUE);
    __tinypy_container_add_comparisons(&vm->types[TINYPY_VALUE_SET]);
    __tinypy_container_add_comparisons(&vm->types[TINYPY_VALUE_FROZENSET]);
    tinypy_internal_type_add_method(&vm->types[TINYPY_VALUE_SET], vm->internal_special_cmp_key, __tinypy_container_cmp_method, (void *)(intptr_t)TINYPY_VALUE_SET, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(&vm->types[TINYPY_VALUE_FROZENSET], vm->internal_special_cmp_key, __tinypy_container_cmp_method, (void *)(intptr_t)TINYPY_VALUE_FROZENSET, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    __tinypy_container_add_numeric_protocol(&vm->types[TINYPY_VALUE_INTEGER], TINYPY_TRUE, TINYPY_FALSE);
    __tinypy_container_add_bool_protocol(&vm->types[TINYPY_VALUE_BOOL]);
    __tinypy_container_add_numeric_protocol(&vm->types[TINYPY_VALUE_LONG], TINYPY_TRUE, TINYPY_FALSE);
    __tinypy_container_add_numeric_protocol(&vm->types[TINYPY_VALUE_FLOAT], TINYPY_FALSE, TINYPY_TRUE);
    __tinypy_container_add_numeric_protocol(&vm->types[TINYPY_VALUE_COMPLEX], TINYPY_FALSE, TINYPY_TRUE);
    tinypy_internal_type_add_method(&vm->types[TINYPY_VALUE_TUPLE], vm->internal_special_hash_key, __tinypy_container_hash_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_value_t *hash_key = vm->internal_special_hash_key;
    tinypy_dict_set(vm->types[TINYPY_VALUE_LIST].dict, hash_key, &vm->none_object.base);
    tinypy_dict_set(vm->types[TINYPY_VALUE_DICT].dict, hash_key, &vm->none_object.base);
}
