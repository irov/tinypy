#include "internal.h"

//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_functools_no_keywords(tinypy_vm_t *vm, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    if (kwargs == NULL || TINYPY_DICT_SIZE(kwargs) == 0U) {
        return TINYPY_TRUE;
    }
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "function does not accept keyword arguments", out_error);
    return TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_functools_dict_update(tinypy_value_t *target, tinypy_value_t *source) {
    if (source == NULL) {
        return;
    }
    tinypy_dict_entry_t *iterator = TINYPY_DICT_ITERATOR_BEGIN(source);
    tinypy_dict_entry_t *iterator_end = TINYPY_DICT_ITERATOR_END(source);
    for (; iterator != iterator_end; ++iterator) {
        if (iterator->state == TINYPY_DICT_ENTRY_ACTIVE) {
            tinypy_dict_set(target, iterator->key, iterator->value);
        }
    }
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_partial_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data) {
    tinypy_partial_object_t *partial = TINYPY_PARTIAL_OBJECT(value);

    visit(partial->callable, user_data);
    visit(partial->args, user_data);
    visit(partial->keywords, user_data);
    if (partial->dict != NULL) {
        visit(partial->dict, user_data);
    }
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_partial_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = type->vm;
    size_t argument_count = TINYPY_TUPLE_SIZE(args);

    if (argument_count == 0U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "partial expected at least one argument", out_error);
        return NULL;
    }
    tinypy_value_t *callable = TINYPY_TUPLE_GET(args, 0U);
    if (callable->type->call == NULL && tinypy_internal_object_has_special(callable, "__call__", 8U) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "the first argument must be callable", out_error);
        return NULL;
    }
    tinypy_partial_object_t *partial = (tinypy_partial_object_t *)tinypy_internal_value_allocate(vm, TINYPY_VALUE_PARTIAL, sizeof(*partial));
    partial->callable = callable;
    TINYPY_INCREF(callable);
    tinypy_value_t *selected_value;
    if (argument_count == 1U) {
        selected_value = tinypy_tuple_from_items(vm, NULL, 0U);
    }
    else {
        tinypy_value_t *const *tuple_items = tinypy_internal_tuple_items(args);
        selected_value = tinypy_tuple_from_items(vm, &tuple_items[1], argument_count - 1U);
    }
    partial->args = selected_value;
    partial->keywords = tinypy_dict_new(vm);
    __tinypy_functools_dict_update(partial->keywords, kwargs);
    return &partial->base;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_partial_call(tinypy_value_t *callable, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_partial_object_t *partial = TINYPY_PARTIAL_OBJECT(callable);
    tinypy_vm_t *vm = TINYPY_VALUE_VM(callable);
    size_t bound_count = TINYPY_TUPLE_SIZE(partial->args);
    size_t call_count = TINYPY_TUPLE_SIZE(args);
    tinypy_value_t **items;
    tinypy_value_t *combined_args;
    size_t index;

    if (bound_count + call_count == 0U) {
        combined_args = tinypy_tuple_from_items(vm, NULL, 0U);
    }
    else {
        items = (tinypy_value_t **)tinypy_internal_vm_allocate(vm, (bound_count + call_count) * sizeof(*items), TINYPY_ALLOC_TAG_TEMPORARY);
        for (index = 0U; index < bound_count; ++index) {
            items[index] = TINYPY_TUPLE_GET(partial->args, index);
        }
        for (index = 0U; index < call_count; ++index) {
            items[bound_count + index] = TINYPY_TUPLE_GET(args, index);
        }
        combined_args = tinypy_tuple_from_items(vm, items, bound_count + call_count);
        tinypy_internal_vm_deallocate(vm, items, (bound_count + call_count) * sizeof(*items), TINYPY_ALLOC_TAG_TEMPORARY);
    }
    tinypy_value_t *combined_kwargs = tinypy_dict_new(vm);
    __tinypy_functools_dict_update(combined_kwargs, partial->keywords);
    __tinypy_functools_dict_update(combined_kwargs, kwargs);
    tinypy_value_t *result = tinypy_call(partial->callable, combined_args, combined_kwargs, out_error);
    TINYPY_DECREF(combined_kwargs);
    TINYPY_DECREF(combined_args);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_functools_reduce(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    size_t argument_count = TINYPY_TUPLE_SIZE(args);
    tinypy_value_t *accumulator;
    tinypy_error_t *iteration_error = NULL;

    (void)user_data;
    if (__tinypy_functools_no_keywords(vm, kwargs, out_error) == 0) {
        return NULL;
    }
    if (argument_count < 2U || argument_count > 3U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "reduce expected two or three arguments", out_error);
        return NULL;
    }
    tinypy_value_t *item_2 = TINYPY_TUPLE_GET(args, 1U);
    tinypy_value_t *iterator = tinypy_iter(item_2, out_error);
    if (iterator == NULL) {
        return NULL;
    }
    if (argument_count == 3U) {
        accumulator = TINYPY_TUPLE_GET(args, 2U);
        TINYPY_INCREF(accumulator);
    }
    else {
        accumulator = tinypy_next(iterator, &iteration_error);
        if (accumulator == NULL) {
            TINYPY_DECREF(iterator);
            if (iteration_error != NULL) {
                if (out_error != NULL) {
                    *out_error = iteration_error;
                }
                else {
                    tinypy_error_release(iteration_error);
                }
            }
            else {
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "reduce() of empty sequence with no initial value", out_error);
            }
            return NULL;
        }
    }
    for (;;) {
        tinypy_value_t *item = tinypy_next(iterator, &iteration_error);
        tinypy_value_t *call_items[2];
        tinypy_value_t *call_args;
        tinypy_value_t *next;

        if (item == NULL) {
            break;
        }
        call_items[0] = accumulator;
        call_items[1] = item;
        call_args = tinypy_tuple_from_items(vm, call_items, 2U);
        tinypy_value_t *item_3 = TINYPY_TUPLE_GET(args, 0U);
        next = tinypy_call(item_3, call_args, NULL, out_error);
        TINYPY_DECREF(call_args);
        TINYPY_DECREF(item);
        if (next == NULL) {
            TINYPY_DECREF(accumulator);
            TINYPY_DECREF(iterator);
            return NULL;
        }
        TINYPY_DECREF(accumulator);
        accumulator = next;
    }
    TINYPY_DECREF(iterator);
    if (iteration_error != NULL) {
        TINYPY_DECREF(accumulator);
        if (out_error != NULL) {
            *out_error = iteration_error;
        }
        else {
            tinypy_error_release(iteration_error);
        }
        return NULL;
    }
    return accumulator;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_initialize_functools_module(tinypy_vm_t *vm) {
    tinypy_value_t *module = tinypy_module_new(vm, "_functools", 10U);
    tinypy_value_t *name = tinypy_string_from_bytes(vm, "_functools", 10U);
    tinypy_value_t *reduce = tinypy_native_function_new(vm, "reduce", 6U, __tinypy_functools_reduce, NULL, NULL);

    tinypy_module_add_value(module, "__name__", 8U, name);
    tinypy_module_add_value(module, "partial", 7U, &vm->types[TINYPY_VALUE_PARTIAL].base.base);
    tinypy_module_add_value(module, "reduce", 6U, reduce);
    TINYPY_DECREF(reduce);
    TINYPY_DECREF(name);
    tinypy_internal_register_module(vm, "_functools", 10U, module);
    TINYPY_DECREF(module);
}
