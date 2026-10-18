#include "tinypy/dict_view.h"

#include "internal.h"

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
        dict_value = tinypy_internal_dict_get_optional_suppressed(vm, view->dict, key);
        if (dict_value == NULL) {
            return INT32_C(0);
        }
        item_value = TINYPY_TUPLE_GET(item, 1U);
        TINYPY_INCREF(dict_value);
        int32_t equal = dict_value == item_value ? 1 : tinypy_compare_bool(item_value, dict_value, TINYPY_COMPARE_EQUAL, out_error);
        TINYPY_DECREF(dict_value);
        return equal;
    }
    tinypy_value_t *iterator = tinypy_internal_dict_iterator_new(view->dict, (int32_t)TINYPY_DICT_VIEW_VALUES);
    int32_t contains = tinypy_contains(iterator, item, out_error);

    TINYPY_DECREF(iterator);
    return contains;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_dict_view_is_set_like(const tinypy_value_t *value) {
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);

    return kind == TINYPY_VALUE_SET || kind == TINYPY_VALUE_FROZENSET || kind == TINYPY_VALUE_DICT_KEYS || kind == TINYPY_VALUE_DICT_ITEMS ? TINYPY_TRUE : TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
/* PyObject_Size of the other operand of a view comparison: a set subclass
   may override __len__, whose result slot_sq_length converts with
   PyInt_AsSsize_t. */
static tinypy_bool_t __tinypy_dict_view_set_like_size(tinypy_value_t *value, int64_t *out_size, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);

    if (kind != TINYPY_VALUE_SET && kind != TINYPY_VALUE_FROZENSET) {
        *out_size = (int64_t)TINYPY_DICT_SIZE(TINYPY_DICT_VIEW_OBJECT(value)->dict);
        return TINYPY_TRUE;
    }
    if (__tinypy_internal_object_overrides_dispatch(value, TINYPY_INTERNAL_DISPATCH_BIT(LENGTH)) == 0) {
        *out_size = (int64_t)tinypy_set_size(value);
        return TINYPY_TRUE;
    }
    tinypy_value_t *method = tinypy_internal_object_get_special_key(value, vm->internal_special_length_key, out_error);
    if (method == NULL) {
        return TINYPY_FALSE;
    }
    tinypy_value_t *empty = TINYPY_RET_EMPTY_TUPLE(vm);
    tinypy_value_t *result = tinypy_call(method, empty, NULL, out_error);
    TINYPY_DECREF(empty);
    TINYPY_DECREF(method);
    if (result == NULL) {
        return TINYPY_FALSE;
    }
    tinypy_bool_t converted = tinypy_internal_number_as_ssize(result, out_size, out_error);
    TINYPY_DECREF(result);
    if (converted == 0) {
        return TINYPY_FALSE;
    }
    if (*out_size < 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "__len__() should return >= 0", out_error);
        return TINYPY_FALSE;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
/* PySequence_Contains of an operand of a view comparison: a view or an exact
   set cannot override __contains__, a set subclass may. */
static int32_t __tinypy_dict_view_set_like_contains(tinypy_value_t *value, tinypy_value_t *item, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);

    if (kind == TINYPY_VALUE_DICT_KEYS || kind == TINYPY_VALUE_DICT_ITEMS) {
        int32_t view_contains = tinypy_internal_dict_view_contains(value, item, out_error);
        return view_contains;
    }
    if (value->type == &vm->types[TINYPY_VALUE_SET] || value->type == &vm->types[TINYPY_VALUE_FROZENSET]) {
        int32_t set_contains = tinypy_set_contains(value, item, out_error);
        return set_contains;
    }
    int32_t contains = tinypy_contains(value, item, out_error);
    return contains;
}
//////////////////////////////////////////////////////////////////////////
/* all_contained_in: every item the iteration of left gives is in right, as
   PySequence_Contains finds it. */
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
    tinypy_vm_t *vm = TINYPY_VALUE_VM(left);
    if (tinypy_internal_iteration_failed(vm, iteration_error, out_error) != 0) {
        return TINYPY_FALSE;
    }
    *out_subset = TINYPY_TRUE;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
/* dictview_richcompare of a view and a set-like operand, which a set on the
   left reaches reflected: the view is measured first, and the other operand
   is measured, searched and iterated through its own protocol. */
tinypy_bool_t tinypy_internal_set_like_compare_checked(tinypy_value_t *left, tinypy_value_t *right, tinypy_compare_operation_e operation, tinypy_bool_t *out_result, tinypy_error_t **out_error) {
    static const tinypy_compare_operation_e reflected[] = {TINYPY_COMPARE_GREATER, TINYPY_COMPARE_GREATER_EQUAL, TINYPY_COMPARE_EQUAL, TINYPY_COMPARE_NOT_EQUAL, TINYPY_COMPARE_LESS, TINYPY_COMPARE_LESS_EQUAL};
    int64_t left_size;
    int64_t right_size;
    tinypy_bool_t subset;

    TINYPY_CLEAR_ERROR(out_error);
    if (__tinypy_dict_view_is_set_like(left) == 0 || __tinypy_dict_view_is_set_like(right) == 0 || operation > TINYPY_COMPARE_GREATER_EQUAL) {
        return TINYPY_FALSE;
    }
    if (TINYPY_VALUE_KIND(left) == TINYPY_VALUE_SET || TINYPY_VALUE_KIND(left) == TINYPY_VALUE_FROZENSET) {
        tinypy_value_t *view = right;

        right = left;
        left = view;
        operation = reflected[(size_t)operation];
    }
    left_size = (int64_t)TINYPY_DICT_SIZE(TINYPY_DICT_VIEW_OBJECT(left)->dict);
    if (__tinypy_dict_view_set_like_size(right, &right_size, out_error) == 0) {
        return TINYPY_FALSE;
    }
    if (operation == TINYPY_COMPARE_GREATER || operation == TINYPY_COMPARE_GREATER_EQUAL) {
        tinypy_value_t *temporary_value = left;
        int64_t temporary_size = left_size;

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

    if (tinypy_internal_native_method_arguments(function, args, kwargs, 1U, 1U, TINYPY_ARITY_STYLE_WRAPPER, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *left = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *right = TINYPY_TUPLE_GET(args, 1U);
    if (__tinypy_dict_view_is_set_like(left) == 0 || __tinypy_dict_view_is_set_like(right) == 0) {
        tinypy_value_t *return_value_1 = TINYPY_RET_NOT_IMPLEMENTED(vm);
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
    intptr_t mode = (intptr_t)user_data;

    if (tinypy_internal_native_method_arguments(function, args, kwargs, 1U, 1U, TINYPY_ARITY_STYLE_WRAPPER, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *left = TINYPY_TUPLE_GET(args, mode >= 100 ? 1U : 0U);
    tinypy_value_t *right = TINYPY_TUPLE_GET(args, mode >= 100 ? 0U : 1U);
    tinypy_value_t *result = tinypy_internal_set_binary(left, right, (int32_t)(mode % 100), out_error);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_dict_view_len_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 0U, 0U, TINYPY_ARITY_STYLE_WRAPPER, out_error) == 0) {
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
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 0U, 0U, TINYPY_ARITY_STYLE_WRAPPER, out_error) == 0) {
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
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 1U, 1U, TINYPY_ARITY_STYLE_WRAPPER, out_error) == 0) {
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
static tinypy_value_t *__tinypy_dict_view_repr_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 0U, 0U, TINYPY_ARITY_STYLE_WRAPPER, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *self = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(self);
    if (kind != TINYPY_VALUE_DICT_KEYS && kind != TINYPY_VALUE_DICT_VALUES && kind != TINYPY_VALUE_DICT_ITEMS) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "__repr__ requires a dict view", out_error);
        return NULL;
    }
    tinypy_value_t *result = tinypy_internal_object_repr_builtin(self, out_error);

    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_hash_t __tinypy_dict_view_unhashable(tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_internal_hash_unhashable_error(value, out_error);
    return (tinypy_hash_t)0;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_dict_view_initialize_set_like(tinypy_vm_t *vm, tinypy_type_t *type) {
    tinypy_value_t *const comparison_names[] = {vm->internal_special_lt_key, vm->internal_special_le_key, vm->internal_special_eq_key, vm->internal_special_ne_key, vm->internal_special_gt_key, vm->internal_special_ge_key};
    static const tinypy_compare_operation_e comparison_operations[] = {TINYPY_COMPARE_LESS, TINYPY_COMPARE_LESS_EQUAL, TINYPY_COMPARE_EQUAL, TINYPY_COMPARE_NOT_EQUAL, TINYPY_COMPARE_GREATER, TINYPY_COMPARE_GREATER_EQUAL};
    tinypy_value_t *const binary_names[] = {vm->internal_special_and_key, vm->internal_special_rand_key, vm->internal_special_xor_key, vm->internal_special_rxor_key, vm->internal_special_or_key, vm->internal_special_ror_key, vm->internal_special_sub_key, vm->internal_special_rsub_key};
    static const intptr_t binary_operations[] = {0, 100, 1, 101, 2, 102, 3, 103};
    size_t index;

    for (index = 0U; index < sizeof(comparison_names) / sizeof(comparison_names[0]); ++index) {
        tinypy_value_t *method_name = comparison_names[index];
        tinypy_internal_type_add_method(type, method_name, __tinypy_dict_view_compare_method, (void *)(intptr_t)comparison_operations[index], NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    }
    for (index = 0U; index < sizeof(binary_names) / sizeof(binary_names[0]); ++index) {
        tinypy_value_t *method_name = binary_names[index];
        tinypy_internal_type_add_method(type, method_name, __tinypy_dict_view_binary_method, (void *)(intptr_t)binary_operations[index], NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    }
    tinypy_internal_type_add_method(type, vm->internal_special_contains_key, __tinypy_dict_view_contains_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
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
        tinypy_internal_type_add_method((types[index]), vm->internal_special_length_key, __tinypy_dict_view_len_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
        tinypy_internal_type_add_method((types[index]), vm->internal_special_iter_key, __tinypy_dict_view_iter_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
        tinypy_internal_type_add_method((types[index]), vm->internal_special_repr_key, __tinypy_dict_view_repr_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    }
    __tinypy_dict_view_initialize_set_like(vm, &vm->types[TINYPY_VALUE_DICT_KEYS]);
    __tinypy_dict_view_initialize_set_like(vm, &vm->types[TINYPY_VALUE_DICT_ITEMS]);
}
