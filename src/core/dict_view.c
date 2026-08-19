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
static tinypy_value_t *__tinypy_dict_view_as_set(tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);

    if (kind == TINYPY_VALUE_SET || kind == TINYPY_VALUE_FROZENSET) {
        TINYPY_INCREF(value);
        return value;
    }
    if (kind == TINYPY_VALUE_DICT_KEYS || kind == TINYPY_VALUE_DICT_ITEMS) {
        tinypy_value_t *return_value_1 = tinypy_set_from_iterable(value, TINYPY_FALSE, out_error);
        return return_value_1;
    }
    return NULL;
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
    tinypy_value_t *left_set = __tinypy_dict_view_as_set(left, out_error);
    if (left_set == NULL) {
        return NULL;
    }
    tinypy_value_t *right_set = __tinypy_dict_view_as_set(right, out_error);
    if (right_set == NULL) {
        TINYPY_DECREF(left_set);
        return NULL;
    }
    tinypy_value_t *result = tinypy_compare_value(left_set, right_set, (tinypy_compare_operation_e)(intptr_t)user_data, out_error);
    TINYPY_DECREF(right_set);
    TINYPY_DECREF(left_set);
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
    tinypy_value_t *left_set = __tinypy_dict_view_as_set(left, out_error);
    if (left_set == NULL) {
        return NULL;
    }
    tinypy_value_t *right_set = __tinypy_dict_view_as_set(right, out_error);
    if (right_set == NULL) {
        TINYPY_DECREF(left_set);
        return NULL;
    }
    tinypy_value_t *result = tinypy_internal_set_binary(left_set, right_set, (int32_t)(mode % 100), out_error);
    TINYPY_DECREF(right_set);
    TINYPY_DECREF(left_set);
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
    tinypy_internal_list_reserve(vm, list, TINYPY_DICT_SIZE(TINYPY_DICT_VIEW_OBJECT(value)->dict));
    for (;;) {
        tinypy_value_t *item = tinypy_next(iterator, &iteration_error);

        if (item == NULL) {
            break;
        }
        tinypy_list_append(list, item);
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
    size_t result_size = prefix_sizes[view_kind] + list_size + 1U;
    uint8_t *result_bytes;
    tinypy_value_t *result = tinypy_internal_text_allocate_uninitialized(vm, TINYPY_VALUE_STRING, result_size, result_size, &result_bytes);

    (void)memcpy(result_bytes, prefixes[view_kind], prefix_sizes[view_kind]);
    (void)memcpy(result_bytes + prefix_sizes[view_kind], list_bytes, list_size);
    result_bytes[result_size - 1U] = (uint8_t)')';
    TINYPY_DECREF(list_repr);
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
    tinypy_type_set_attr(type, "__hash__", 8U, &vm->none_object.base);
    type->hash = __tinypy_dict_view_unhashable;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_initialize_dict_view_types(tinypy_vm_t *vm) {
    __tinypy_dict_view_initialize_set_like(vm, &vm->types[TINYPY_VALUE_DICT_KEYS]);
    __tinypy_dict_view_initialize_set_like(vm, &vm->types[TINYPY_VALUE_DICT_ITEMS]);
    vm->types[TINYPY_VALUE_DICT_KEYS].repr = __tinypy_dict_view_repr;
    vm->types[TINYPY_VALUE_DICT_VALUES].repr = __tinypy_dict_view_repr;
    vm->types[TINYPY_VALUE_DICT_ITEMS].repr = __tinypy_dict_view_repr;
}
