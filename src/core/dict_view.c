#include "tinypy/dict_view.h"

#include "internal.h"

#include <string.h>

//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_dict_view_new(tinypy_value_t *dict, tinypy_dict_view_kind_e kind) {
    tinypy_value_type_e value_kind;

    tinypy_vm_t *vm = TINYPY_VALUE_VM(dict);
    value_kind = kind == TINYPY_DICT_VIEW_KEYS ? TINYPY_VALUE_DICT_KEYS : (kind == TINYPY_DICT_VIEW_VALUES ? TINYPY_VALUE_DICT_VALUES : TINYPY_VALUE_DICT_ITEMS);
    tinypy_dict_view_object_t *view = (tinypy_dict_view_object_t *)tinypy_internal_value_allocate(vm, value_kind, sizeof(*view));
    view->dict = dict;
    view->kind = kind;
    TINYPY_INCREF(dict);
    return &view->base;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_dict_view_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data) {
    visit(TINYPY_DICT_VIEW_OBJECT(value)->dict, user_data);
}
//////////////////////////////////////////////////////////////////////////
ptrdiff_t tinypy_internal_dict_view_length(tinypy_value_t *value, tinypy_error_t **out_error) {
    TINYPY_CLEAR_ERROR(out_error);
    ptrdiff_t return_value_1 = (ptrdiff_t)TINYPY_DICT_SIZE(TINYPY_DICT_VIEW_OBJECT(value)->dict);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_dict_view_iter(tinypy_value_t *value, tinypy_error_t **out_error) {
    TINYPY_CLEAR_ERROR(out_error);
    tinypy_value_t *return_value_1 = tinypy_internal_dict_iterator_new(TINYPY_DICT_VIEW_OBJECT(value)->dict, (int32_t)TINYPY_DICT_VIEW_OBJECT(value)->kind);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
int32_t tinypy_internal_dict_view_contains(tinypy_value_t *value, tinypy_value_t *item, tinypy_error_t **out_error) {
    tinypy_dict_view_object_t *view = TINYPY_DICT_VIEW_OBJECT(value);

    TINYPY_CLEAR_ERROR(out_error);
    if (view->kind == TINYPY_DICT_VIEW_KEYS) {
        tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
        tinypy_bool_t contains;

        if (tinypy_internal_dict_contains_checked(vm, view->dict, item, &contains, out_error) == 0) {
            return INT32_C(-1);
        }
        return contains != 0 ? INT32_C(1) : INT32_C(0);
    }
    if (view->kind == TINYPY_DICT_VIEW_ITEMS) {
        tinypy_value_t *key;
        tinypy_value_t *dict_value;
        tinypy_value_t *item_value;

        if (TINYPY_VALUE_KIND(item) != TINYPY_VALUE_TUPLE || TINYPY_TUPLE_SIZE(item) != 2U) {
            return INT32_C(0);
        }
        key = TINYPY_TUPLE_GET(item, 0U);
        tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
        if (tinypy_internal_dict_get_optional_checked(vm, view->dict, key, &dict_value, out_error) == 0) {
            return INT32_C(-1);
        }
        if (dict_value == NULL) {
            return INT32_C(0);
        }
        item_value = TINYPY_TUPLE_GET(item, 1U);
        TINYPY_INCREF(dict_value);
        int32_t equal = dict_value == item_value ? 1 : tinypy_compare_bool(dict_value, item_value, TINYPY_COMPARE_EQUAL, out_error);
        TINYPY_DECREF(dict_value);
        return equal;
    }
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_dict_entry_t *entries = TINYPY_DICT_ITERATOR_BEGIN(view->dict);
    size_t capacity = TINYPY_DICT_OBJECT(view->dict)->mask + 1U;
    uint64_t version = TINYPY_DICT_OBJECT(view->dict)->mutation_version;
    size_t index;

    for (index = 0U; index < capacity; ++index) {
        if (TINYPY_DICT_ENTRY_IS_ACTIVE(&entries[index])) {
            tinypy_value_t *dict_value = entries[index].value;
            int32_t equal;

            TINYPY_INCREF(dict_value);
            equal = dict_value == item ? 1 : tinypy_compare_bool(dict_value, item, TINYPY_COMPARE_EQUAL, out_error);
            TINYPY_DECREF(dict_value);
            if (equal < 0) {
                return INT32_C(-1);
            }
            if (TINYPY_DICT_OBJECT(view->dict)->mutation_version != version || TINYPY_DICT_OBJECT(view->dict)->table != entries) {
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_RUNTIME, "dictionary changed size during iteration", out_error);
                return INT32_C(-1);
            }
            if (equal != 0) {
                return INT32_C(1);
            }
        }
    }
    return INT32_C(0);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_dict_view_arguments(tinypy_vm_t *vm, tinypy_value_t *args, tinypy_value_t *kwargs, size_t count, tinypy_error_t **out_error) {
    if ((kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U) || TINYPY_TUPLE_SIZE(args) != count) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "dict view method received invalid arguments", out_error);
        return TINYPY_FALSE;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_dict_view_is_set_like(const tinypy_value_t *value) {
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);

    return kind == TINYPY_VALUE_SET || kind == TINYPY_VALUE_FROZENSET || kind == TINYPY_VALUE_DICT_KEYS || kind == TINYPY_VALUE_DICT_ITEMS ? TINYPY_TRUE : TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
static size_t __tinypy_dict_view_set_like_size(const tinypy_value_t *value) {
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);

    if (kind == TINYPY_VALUE_SET || kind == TINYPY_VALUE_FROZENSET) {
        size_t return_value_1 = tinypy_set_size(value);
        return return_value_1;
    }
    size_t return_value_2 = TINYPY_DICT_SIZE(TINYPY_DICT_VIEW_OBJECT((tinypy_value_t *)value)->dict);
    return return_value_2;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_dict_view_set_like_contains(tinypy_value_t *value, tinypy_value_t *item, tinypy_error_t **out_error) {
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);

    if (kind == TINYPY_VALUE_SET || kind == TINYPY_VALUE_FROZENSET) {
        int32_t return_value_1 = tinypy_set_contains(value, item, out_error);
        return return_value_1;
    }
    int32_t return_value_2 = tinypy_internal_dict_view_contains(value, item, out_error);
    return return_value_2;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_dict_view_subset_checked(tinypy_value_t *left, tinypy_value_t *right, tinypy_bool_t *out_subset, tinypy_error_t **out_error) {
    tinypy_value_t *iterator = tinypy_iter(left, out_error);
    tinypy_error_t *iteration_error = NULL;

    if (iterator == NULL) {
        return TINYPY_FALSE;
    }
    for (;;) {
        tinypy_value_t *item = tinypy_next(iterator, &iteration_error);
        int32_t contains;

        if (item == NULL) {
            break;
        }
        contains = __tinypy_dict_view_set_like_contains(right, item, out_error);
        TINYPY_DECREF(item);
        if (contains < 0) {
            TINYPY_DECREF(iterator);
            return TINYPY_FALSE;
        }
        if (contains == 0) {
            TINYPY_DECREF(iterator);
            *out_subset = TINYPY_FALSE;
            return TINYPY_TRUE;
        }
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
    *out_subset = TINYPY_TRUE;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_set_like_compare_checked(tinypy_value_t *left, tinypy_value_t *right, tinypy_compare_operation_e operation, tinypy_bool_t *out_result, tinypy_error_t **out_error) {
    size_t left_size;
    size_t right_size;
    tinypy_bool_t subset;

    TINYPY_CLEAR_ERROR(out_error);
    if (__tinypy_dict_view_is_set_like(left) == 0 || __tinypy_dict_view_is_set_like(right) == 0 || operation > TINYPY_COMPARE_GREATER_EQUAL) {
        return TINYPY_FALSE;
    }
    left_size = __tinypy_dict_view_set_like_size(left);
    right_size = __tinypy_dict_view_set_like_size(right);
    if (operation == TINYPY_COMPARE_GREATER || operation == TINYPY_COMPARE_GREATER_EQUAL) {
        tinypy_value_t *temporary_value = left;
        size_t temporary_size = left_size;

        left = right;
        right = temporary_value;
        left_size = right_size;
        right_size = temporary_size;
        operation = operation == TINYPY_COMPARE_GREATER ? TINYPY_COMPARE_LESS : TINYPY_COMPARE_LESS_EQUAL;
    }
    if (operation == TINYPY_COMPARE_EQUAL || operation == TINYPY_COMPARE_NOT_EQUAL) {
        if (left_size != right_size) {
            *out_result = operation == TINYPY_COMPARE_NOT_EQUAL ? TINYPY_TRUE : TINYPY_FALSE;
            return TINYPY_TRUE;
        }
    }
    else if (left_size > right_size || (operation == TINYPY_COMPARE_LESS && left_size == right_size)) {
        *out_result = TINYPY_FALSE;
        return TINYPY_TRUE;
    }
    if (__tinypy_dict_view_subset_checked(left, right, &subset, out_error) == 0) {
        return TINYPY_FALSE;
    }
    *out_result = operation == TINYPY_COMPARE_NOT_EQUAL ? (subset == 0 ? TINYPY_TRUE : TINYPY_FALSE) : subset;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_dict_view_compare_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    if (__tinypy_dict_view_arguments(vm, args, kwargs, 2U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *left = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *right = TINYPY_TUPLE_GET(args, 1U);
    if (__tinypy_dict_view_is_set_like(left) == 0 || __tinypy_dict_view_is_set_like(right) == 0) {
        tinypy_value_t *return_value_1 = &vm->not_implemented_object.base;
        TINYPY_INCREF(return_value_1);
        return return_value_1;
    }
    tinypy_bool_t comparison;
    if (tinypy_internal_set_like_compare_checked(left, right, (tinypy_compare_operation_e)(intptr_t)user_data, &comparison, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *result = tinypy_bool_from_i32(vm, comparison);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_dict_view_binary_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    intptr_t mode = (intptr_t)user_data;

    if (__tinypy_dict_view_arguments(vm, args, kwargs, 2U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *left = TINYPY_TUPLE_GET(args, mode >= 100 ? 1U : 0U);
    tinypy_value_t *right = TINYPY_TUPLE_GET(args, mode >= 100 ? 0U : 1U);
    if (__tinypy_dict_view_is_set_like(left) == 0 || __tinypy_dict_view_is_set_like(right) == 0) {
        tinypy_value_t *return_value_1 = &vm->not_implemented_object.base;
        TINYPY_INCREF(return_value_1);
        return return_value_1;
    }
    tinypy_value_t *result = tinypy_internal_set_binary(left, right, (int32_t)(mode % 100), out_error);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_dict_view_len_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_dict_view_arguments(vm, args, kwargs, 1U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *self = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(self);
    if (kind != TINYPY_VALUE_DICT_KEYS && kind != TINYPY_VALUE_DICT_VALUES && kind != TINYPY_VALUE_DICT_ITEMS) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "__len__ requires a dict view", out_error);
        return NULL;
    }
    tinypy_value_t *result = tinypy_integer_from_i64(vm, (int64_t)TINYPY_DICT_SIZE(TINYPY_DICT_VIEW_OBJECT(self)->dict));
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_dict_view_iter_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_dict_view_arguments(vm, args, kwargs, 1U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *self = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(self);
    if (kind != TINYPY_VALUE_DICT_KEYS && kind != TINYPY_VALUE_DICT_VALUES && kind != TINYPY_VALUE_DICT_ITEMS) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "__iter__ requires a dict view", out_error);
        return NULL;
    }
    tinypy_value_t *result = tinypy_internal_dict_view_iter(self, out_error);

    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_dict_view_contains_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_dict_view_arguments(vm, args, kwargs, 2U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *self = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(self);
    if (kind != TINYPY_VALUE_DICT_KEYS && kind != TINYPY_VALUE_DICT_ITEMS) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "__contains__ requires a set-like dict view", out_error);
        return NULL;
    }
    int32_t contains = tinypy_internal_dict_view_contains(self, TINYPY_TUPLE_GET(args, 1U), out_error);
    tinypy_value_t *result = contains < 0 ? NULL : tinypy_bool_from_i32(vm, contains);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_dict_view_repr(tinypy_value_t *value, tinypy_error_t **out_error) {
    static const char *const prefixes[] = {"dict_keys(", "dict_values(", "dict_items("};
    static const size_t prefix_sizes[] = {10U, 12U, 11U};
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_value_t *list = tinypy_list_from_items(vm, NULL, 0U);
    tinypy_value_t *iterator = tinypy_internal_dict_view_iter(value, out_error);
    tinypy_error_t *iteration_error = NULL;

    if (iterator == NULL) {
        TINYPY_DECREF(list);
        return NULL;
    }
    if (tinypy_internal_list_reserve_checked(vm, list, TINYPY_DICT_SIZE(TINYPY_DICT_VIEW_OBJECT(value)->dict), out_error) == 0) {
        TINYPY_DECREF(iterator);
        TINYPY_DECREF(list);
        return NULL;
    }
    for (;;) {
        tinypy_value_t *item = tinypy_next(iterator, &iteration_error);

        if (item == NULL) {
            break;
        }
        if (tinypy_internal_list_append_checked(list, item, out_error) == 0) {
            TINYPY_DECREF(item);
            TINYPY_DECREF(iterator);
            TINYPY_DECREF(list);
            return NULL;
        }
        TINYPY_DECREF(item);
    }
    TINYPY_DECREF(iterator);
    if (iteration_error != NULL) {
        TINYPY_DECREF(list);
        if (out_error != NULL) {
            *out_error = iteration_error;
        }
        else {
            tinypy_error_release(iteration_error);
        }
        return NULL;
    }
    tinypy_value_t *list_repr = tinypy_object_repr(list, out_error);
    TINYPY_DECREF(list);
    if (list_repr == NULL) {
        return NULL;
    }
    tinypy_dict_view_kind_e view_kind = TINYPY_DICT_VIEW_OBJECT(value)->kind;
    size_t list_size;
    const char *list_bytes = tinypy_string_view(list_repr, &list_size);
    if (list_size > SIZE_MAX - prefix_sizes[view_kind] - 1U) {
        TINYPY_DECREF(list_repr);
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_MEMORY, "dictionary view representation is too large", out_error);
        return NULL;
    }
    size_t result_size = prefix_sizes[view_kind] + list_size + 1U;
    uint8_t *result_bytes;
    tinypy_value_t *result = tinypy_internal_text_allocate_uninitialized_checked(vm, TINYPY_VALUE_STRING, result_size, result_size, &result_bytes, out_error);

    if (result == NULL) {
        TINYPY_DECREF(list_repr);
        return NULL;
    }

    (void)memcpy(result_bytes, prefixes[view_kind], prefix_sizes[view_kind]);
    (void)memcpy(result_bytes + prefix_sizes[view_kind], list_bytes, list_size);
    result_bytes[result_size - 1U] = (uint8_t)')';
    TINYPY_DECREF(list_repr);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_dict_view_repr_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_dict_view_arguments(vm, args, kwargs, 1U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *self = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(self);
    if (kind != TINYPY_VALUE_DICT_KEYS && kind != TINYPY_VALUE_DICT_VALUES && kind != TINYPY_VALUE_DICT_ITEMS) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "__repr__ requires a dict view", out_error);
        return NULL;
    }
    tinypy_value_t *result = __tinypy_dict_view_repr(self, out_error);

    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_hash_t __tinypy_dict_view_unhashable(tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_internal_make_vm_error(TINYPY_VALUE_VM(value), TINYPY_ERROR_TYPE, "unhashable type", out_error);
    return (tinypy_hash_t)0;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_dict_view_add_method(tinypy_type_t *type, const char *name, size_t name_size, tinypy_native_function_callback_t callback, void *user_data) {
    tinypy_value_t *function = tinypy_native_function_new(type->vm, name, name_size, callback, user_data, NULL);

    tinypy_type_set_attr(type, name, name_size, function);
    TINYPY_DECREF(function);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_dict_view_initialize_set_like(tinypy_vm_t *vm, tinypy_type_t *type) {
    static const char *const comparison_names[] = {"__lt__", "__le__", "__eq__", "__ne__", "__gt__", "__ge__"};
    static const tinypy_compare_operation_e comparison_operations[] = {TINYPY_COMPARE_LESS, TINYPY_COMPARE_LESS_EQUAL, TINYPY_COMPARE_EQUAL, TINYPY_COMPARE_NOT_EQUAL, TINYPY_COMPARE_GREATER, TINYPY_COMPARE_GREATER_EQUAL};
    static const char *const binary_names[] = {"__and__", "__rand__", "__xor__", "__rxor__", "__or__", "__ror__", "__sub__", "__rsub__"};
    static const intptr_t binary_operations[] = {0, 100, 1, 101, 2, 102, 3, 103};
    size_t index;

    for (index = 0U; index < sizeof(comparison_names) / sizeof(comparison_names[0]); ++index) {
        __tinypy_dict_view_add_method(type, comparison_names[index], 6U, __tinypy_dict_view_compare_method, (void *)(intptr_t)comparison_operations[index]);
    }
    for (index = 0U; index < sizeof(binary_names) / sizeof(binary_names[0]); ++index) {
        __tinypy_dict_view_add_method(type, binary_names[index], strlen(binary_names[index]), __tinypy_dict_view_binary_method, (void *)(intptr_t)binary_operations[index]);
    }
    __tinypy_dict_view_add_method(type, "__contains__", 12U, __tinypy_dict_view_contains_method, NULL);
    tinypy_type_set_attr(type, "__hash__", 8U, &vm->none_object.base);
    type->hash = __tinypy_dict_view_unhashable;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_initialize_dict_view_types(tinypy_vm_t *vm) {
    tinypy_type_t *types[] = {
        &vm->types[TINYPY_VALUE_DICT_KEYS],
        &vm->types[TINYPY_VALUE_DICT_VALUES],
        &vm->types[TINYPY_VALUE_DICT_ITEMS]};
    size_t index;

    for (index = 0U; index < sizeof(types) / sizeof(types[0]); ++index) {
        __tinypy_dict_view_add_method(types[index], "__len__", 7U, __tinypy_dict_view_len_method, NULL);
        __tinypy_dict_view_add_method(types[index], "__iter__", 8U, __tinypy_dict_view_iter_method, NULL);
        __tinypy_dict_view_add_method(types[index], "__repr__", 8U, __tinypy_dict_view_repr_method, NULL);
    }
    __tinypy_dict_view_initialize_set_like(vm, &vm->types[TINYPY_VALUE_DICT_KEYS]);
    __tinypy_dict_view_initialize_set_like(vm, &vm->types[TINYPY_VALUE_DICT_ITEMS]);
    vm->types[TINYPY_VALUE_DICT_KEYS].repr = __tinypy_dict_view_repr;
    vm->types[TINYPY_VALUE_DICT_VALUES].repr = __tinypy_dict_view_repr;
    vm->types[TINYPY_VALUE_DICT_ITEMS].repr = __tinypy_dict_view_repr;
}
