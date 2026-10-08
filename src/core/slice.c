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
    tinypy_internal_make_arity_error(vm, "slice", 5U, argument_count, 1U, 3U, TINYPY_ARITY_STYLE_UNPACK, out_error);
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
    /* The getter is reachable unbound through the property's fget. */
    tinypy_value_t *self = TINYPY_TUPLE_GET(args, 0U);
    if (TINYPY_VALUE_KIND(self) != TINYPY_VALUE_SLICE) {
        const tinypy_message_part_t parts[] = {
            TINYPY_MESSAGE_PART_LITERAL("descriptor '"),
            TINYPY_MESSAGE_PART_TEXT(tinypy_native_function_name(function)),
            TINYPY_MESSAGE_PART_LITERAL("' for 'slice' objects doesn't apply to '"),
            TINYPY_MESSAGE_PART_TYPE_NAME(self),
            TINYPY_MESSAGE_PART_LITERAL("' object")
        };

        tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
        return NULL;
    }
    tinypy_slice_object_t *slice = TINYPY_SLICE_OBJECT(self);
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
    return TINYPY_RET(result);
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
        if (tinypy_internal_slice_unpack(TINYPY_TUPLE_GET(args, 0U), &indices, out_error) == 0) {
            return NULL;
        }
        int64_t *bounds[] = {&indices.start, &indices.stop};
        int64_t upper = indices.step < 0 ? (int64_t)((uint64_t)length - UINT64_C(1)) : length;

        for (size_t index = 0U; index < 2U; ++index) {
            if (*bounds[index] < 0) {
                /* Python 2 exposes machine-word wrapping for this legacy
                   case. Unsigned addition keeps it defined in the runtime. */
                *bounds[index] = (int64_t)((uint64_t)*bounds[index] + (uint64_t)length);
                if (*bounds[index] < 0) {
                    *bounds[index] = indices.step < 0 ? -1 : 0;
                }
            }
            else if (*bounds[index] >= length) {
                *bounds[index] = upper;
            }
        }
    }
    else if (tinypy_internal_slice_indices(TINYPY_TUPLE_GET(args, 0U), (size_t)length, &indices, out_error) == 0) {
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
void tinypy_internal_initialize_slice_type(tinypy_vm_t *vm) {
    tinypy_internal_type_add_property((&vm->types[TINYPY_VALUE_SLICE]), vm->internal_start_key, __tinypy_slice_field_method, (void *)(intptr_t)0, NULL);
    tinypy_internal_type_add_property((&vm->types[TINYPY_VALUE_SLICE]), vm->internal_stop_key, __tinypy_slice_field_method, (void *)(intptr_t)1, NULL);
    tinypy_internal_type_add_property((&vm->types[TINYPY_VALUE_SLICE]), vm->internal_step_key, __tinypy_slice_field_method, (void *)(intptr_t)2, NULL);
    tinypy_internal_type_add_method((&vm->types[TINYPY_VALUE_SLICE]), vm->internal_indices_key, __tinypy_slice_indices_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method((&vm->types[TINYPY_VALUE_SLICE]), vm->internal_special_cmp_key, __tinypy_slice_cmp_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method((&vm->types[TINYPY_VALUE_SLICE]), vm->internal_special_repr_key, __tinypy_slice_repr_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method((&vm->types[TINYPY_VALUE_SLICE]), vm->internal_special_hash_key, __tinypy_slice_hash_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
}
