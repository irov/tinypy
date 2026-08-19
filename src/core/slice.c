#include "tinypy/slice.h"

#include "internal.h"

//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_slice_new(tinypy_vm_t *vm, tinypy_value_t *start, tinypy_value_t *stop, tinypy_value_t *step) {
    tinypy_slice_object_t *slice = (tinypy_slice_object_t *)tinypy_internal_value_allocate(vm, TINYPY_VALUE_SLICE, sizeof(*slice));
    slice->start = start != NULL ? start : &vm->none_object.base;
    slice->stop = stop != NULL ? stop : &vm->none_object.base;
    slice->step = step != NULL ? step : &vm->none_object.base;
    TINYPY_INCREF(slice->start);
    TINYPY_INCREF(slice->stop);
    TINYPY_INCREF(slice->step);
    return &slice->base;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_slice_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data) {
    tinypy_slice_object_t *slice = TINYPY_SLICE_OBJECT(value);

    visit(slice->start, user_data);
    visit(slice->stop, user_data);
    visit(slice->step, user_data);
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_slice_start(const tinypy_value_t *slice) {
    tinypy_value_t *return_value_1 = TINYPY_SLICE_OBJECT((tinypy_value_t *)slice)->start;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_slice_stop(const tinypy_value_t *slice) {
    tinypy_value_t *return_value_1 = TINYPY_SLICE_OBJECT((tinypy_value_t *)slice)->stop;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_slice_step(const tinypy_value_t *slice) {
    tinypy_value_t *return_value_1 = TINYPY_SLICE_OBJECT((tinypy_value_t *)slice)->step;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_slice_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = type->vm;
    size_t argument_count = TINYPY_TUPLE_SIZE(args);

    TINYPY_CLEAR_ERROR(out_error);
    if (kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "slice does not accept keyword arguments", out_error);
        return NULL;
    }
    if (argument_count == 1U) {
        tinypy_value_t *item = TINYPY_TUPLE_GET(args, 0U);
        tinypy_value_t *return_value_1 = tinypy_slice_new(vm, NULL, item, NULL);
        return return_value_1;
    }
    if (argument_count == 2U) {
        tinypy_value_t *item = TINYPY_TUPLE_GET(args, 0U);
        tinypy_value_t *item_2 = TINYPY_TUPLE_GET(args, 1U);
        tinypy_value_t *return_value_2 = tinypy_slice_new(vm, item, item_2, NULL);
        return return_value_2;
    }
    if (argument_count == 3U) {
        tinypy_value_t *item = TINYPY_TUPLE_GET(args, 0U);
        tinypy_value_t *item_2 = TINYPY_TUPLE_GET(args, 1U);
        tinypy_value_t *item_3 = TINYPY_TUPLE_GET(args, 2U);
        tinypy_value_t *return_value_3 = tinypy_slice_new(vm, item, item_2, item_3);
        return return_value_3;
    }
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "slice requires one to three arguments", out_error);
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_slice_method_arguments(tinypy_vm_t *vm, tinypy_value_t *args, tinypy_value_t *kwargs, size_t count, tinypy_error_t **out_error) {
    if ((kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U) || TINYPY_TUPLE_SIZE(args) != count) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "slice method received invalid arguments", out_error);
        return TINYPY_FALSE;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_slice_field_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    if (__tinypy_slice_method_arguments(vm, args, kwargs, 1U, out_error) == 0) {
        return NULL;
    }
    tinypy_slice_object_t *slice = TINYPY_SLICE_OBJECT(TINYPY_TUPLE_GET(args, 0U));
    tinypy_value_t *result;
    switch ((intptr_t)user_data) {
    case 0:
        result = slice->start;
        break;
    case 1:
        result = slice->stop;
        break;
    default:
        result = slice->step;
        break;
    }
    TINYPY_INCREF(result);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_slice_indices_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    int64_t length;
    tinypy_internal_slice_indices_t indices;
    tinypy_value_t *items[3];

    (void)user_data;
    if (__tinypy_slice_method_arguments(vm, args, kwargs, 2U, out_error) == 0) {
        return NULL;
    }
    if (tinypy_internal_index_as_i64(TINYPY_TUPLE_GET(args, 1U), &length, TINYPY_FALSE, out_error) == 0) {
        return NULL;
    }
    if (length < 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "length should not be negative", out_error);
        return NULL;
    }
    if (tinypy_internal_slice_indices(TINYPY_TUPLE_GET(args, 0U), (size_t)length, &indices, out_error) == 0) {
        return NULL;
    }
    items[0] = tinypy_integer_from_i64(vm, indices.start);
    items[1] = tinypy_integer_from_i64(vm, indices.stop);
    items[2] = tinypy_integer_from_i64(vm, indices.step);
    tinypy_value_t *result = tinypy_tuple_from_items(vm, items, 3U);
    TINYPY_DECREF(items[2]);
    TINYPY_DECREF(items[1]);
    TINYPY_DECREF(items[0]);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_slice_cmp_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_slice_method_arguments(vm, args, kwargs, 2U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *left = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *right = TINYPY_TUPLE_GET(args, 1U);
    if (TINYPY_VALUE_KIND(right) != TINYPY_VALUE_SLICE) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "slice.__cmp__ requires a slice operand", out_error);
        return NULL;
    }
    int32_t equal = tinypy_compare_bool(left, right, TINYPY_COMPARE_EQUAL, out_error);
    if (equal < 0) {
        return NULL;
    }
    int64_t order = 0;
    if (equal == 0) {
        int32_t less = tinypy_compare_bool(left, right, TINYPY_COMPARE_LESS, out_error);
        if (less < 0) {
            return NULL;
        }
        order = less != 0 ? INT64_C(-1) : INT64_C(1);
    }
    tinypy_value_t *return_value_1 = tinypy_integer_from_i64(vm, order);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_slice_repr_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_slice_method_arguments(vm, args, kwargs, 1U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *return_value_1 = tinypy_object_repr(TINYPY_TUPLE_GET(args, 0U), out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_slice_hash_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_slice_method_arguments(vm, args, kwargs, 1U, out_error) == 0) {
        return NULL;
    }
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "unhashable type", out_error);
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_slice_add_method(tinypy_type_t *type, const char *name, size_t name_size, tinypy_native_function_callback_t callback, void *user_data) {
    tinypy_value_t *function = tinypy_native_function_new(type->vm, name, name_size, callback, user_data, NULL);

    tinypy_type_set_attr(type, name, name_size, function);
    TINYPY_DECREF(function);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_slice_add_property(tinypy_type_t *type, const char *name, size_t name_size, void *user_data) {
    tinypy_value_t *function = tinypy_native_function_new(type->vm, name, name_size, __tinypy_slice_field_method, user_data, NULL);
    tinypy_value_t *property = tinypy_property_new(type->vm, function, NULL, NULL, NULL);

    tinypy_type_set_attr(type, name, name_size, property);
    TINYPY_DECREF(property);
    TINYPY_DECREF(function);
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_initialize_slice_type(tinypy_vm_t *vm) {
    __tinypy_slice_add_property(&vm->types[TINYPY_VALUE_SLICE], "start", 5U, (void *)(intptr_t)0);
    __tinypy_slice_add_property(&vm->types[TINYPY_VALUE_SLICE], "stop", 4U, (void *)(intptr_t)1);
    __tinypy_slice_add_property(&vm->types[TINYPY_VALUE_SLICE], "step", 4U, (void *)(intptr_t)2);
    __tinypy_slice_add_method(&vm->types[TINYPY_VALUE_SLICE], "indices", 7U, __tinypy_slice_indices_method, NULL);
    __tinypy_slice_add_method(&vm->types[TINYPY_VALUE_SLICE], "__cmp__", 7U, __tinypy_slice_cmp_method, NULL);
    __tinypy_slice_add_method(&vm->types[TINYPY_VALUE_SLICE], "__repr__", 8U, __tinypy_slice_repr_method, NULL);
    __tinypy_slice_add_method(&vm->types[TINYPY_VALUE_SLICE], "__hash__", 8U, __tinypy_slice_hash_method, NULL);
}
