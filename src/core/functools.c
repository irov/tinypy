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
        if (TINYPY_DICT_ENTRY_IS_ACTIVE(iterator)) {
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
    tinypy_partial_object_t *partial = (tinypy_partial_object_t *)tinypy_internal_object_allocate(vm, type, type->basic_size);
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
    tinypy_value_t *combined_args = tinypy_internal_tuple_concat_checked(vm, partial->args, args, out_error);
    if (combined_args == NULL) {
        return NULL;
    }
    tinypy_value_t *combined_kwargs;
    size_t stored_keyword_count = TINYPY_DICT_SIZE(partial->keywords);
    size_t call_keyword_count = kwargs != NULL ? TINYPY_DICT_SIZE(kwargs) : 0U;

    if (stored_keyword_count == 0U) {
        combined_kwargs = kwargs;
        if (combined_kwargs != NULL) {
            TINYPY_INCREF(combined_kwargs);
        }
    }
    else {
        combined_kwargs = tinypy_dict_new(vm);
        __tinypy_functools_dict_update(combined_kwargs, partial->keywords);
        if (call_keyword_count != 0U) {
            __tinypy_functools_dict_update(combined_kwargs, kwargs);
        }
    }
    tinypy_value_t *result = tinypy_call(partial->callable, combined_args, combined_kwargs, out_error);
    if (combined_kwargs != NULL) {
        TINYPY_DECREF(combined_kwargs);
    }
    TINYPY_DECREF(combined_args);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_partial_method_arguments(tinypy_vm_t *vm, tinypy_value_t *args, tinypy_value_t *kwargs, size_t count, tinypy_error_t **out_error) {
    if ((kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U) || TINYPY_TUPLE_SIZE(args) != count || TINYPY_VALUE_KIND(TINYPY_TUPLE_GET(args, 0U)) != TINYPY_VALUE_PARTIAL) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "partial method received invalid arguments", out_error);
        return TINYPY_FALSE;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_partial_call_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    size_t count = TINYPY_TUPLE_SIZE(args);
    tinypy_value_t *self;
    tinypy_value_t *call_args;
    tinypy_value_t *result;

    (void)user_data;
    if (count == 0U || TINYPY_VALUE_KIND(TINYPY_TUPLE_GET(args, 0U)) != TINYPY_VALUE_PARTIAL) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "partial.__call__ requires a partial object", out_error);
        return NULL;
    }
    self = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *const *items = tinypy_internal_tuple_items(args);
    call_args = tinypy_tuple_from_items(vm, items + 1U, count - 1U);
    result = tinypy_internal_partial_call(self, call_args, kwargs, out_error);
    TINYPY_DECREF(call_args);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_partial_reduce_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_partial_object_t *partial;
    tinypy_value_t *none;
    tinypy_value_t *constructor_items[1];
    tinypy_value_t *state_items[4];
    tinypy_value_t *result_items[3];
    tinypy_value_t *constructor_args;
    tinypy_value_t *state;
    tinypy_value_t *result;

    (void)user_data;
    if (__tinypy_partial_method_arguments(vm, args, kwargs, 1U, out_error) == 0) {
        return NULL;
    }
    partial = TINYPY_PARTIAL_OBJECT(TINYPY_TUPLE_GET(args, 0U));
    none = tinypy_none_get(vm);
    constructor_items[0] = partial->callable;
    constructor_args = tinypy_tuple_from_items(vm, constructor_items, 1U);
    state_items[0] = partial->callable;
    state_items[1] = partial->args;
    state_items[2] = partial->keywords;
    state_items[3] = partial->dict != NULL ? partial->dict : none;
    state = tinypy_tuple_from_items(vm, state_items, 4U);
    result_items[0] = &partial->base.type->base.base;
    result_items[1] = constructor_args;
    result_items[2] = state;
    result = tinypy_tuple_from_items(vm, result_items, 3U);
    TINYPY_DECREF(state);
    TINYPY_DECREF(constructor_args);
    TINYPY_DECREF(none);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_partial_setstate_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_partial_object_t *partial;
    tinypy_value_t *state;
    tinypy_value_t *callable;
    tinypy_value_t *stored_args;
    tinypy_value_t *keywords;
    tinypy_value_t *dict;
    tinypy_value_t *owned_keywords = NULL;

    (void)user_data;
    if (__tinypy_partial_method_arguments(vm, args, kwargs, 2U, out_error) == 0) {
        return NULL;
    }
    partial = TINYPY_PARTIAL_OBJECT(TINYPY_TUPLE_GET(args, 0U));
    state = TINYPY_TUPLE_GET(args, 1U);
    if (TINYPY_VALUE_KIND(state) != TINYPY_VALUE_TUPLE || TINYPY_TUPLE_SIZE(state) != 4U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "invalid partial state", out_error);
        return NULL;
    }
    callable = TINYPY_TUPLE_GET(state, 0U);
    stored_args = TINYPY_TUPLE_GET(state, 1U);
    keywords = TINYPY_TUPLE_GET(state, 2U);
    dict = TINYPY_TUPLE_GET(state, 3U);
    if (tinypy_is_callable(callable) == 0 || TINYPY_VALUE_KIND(stored_args) != TINYPY_VALUE_TUPLE || (TINYPY_VALUE_KIND(keywords) != TINYPY_VALUE_DICT && TINYPY_VALUE_KIND(keywords) != TINYPY_VALUE_NONE) || (TINYPY_VALUE_KIND(dict) != TINYPY_VALUE_DICT && TINYPY_VALUE_KIND(dict) != TINYPY_VALUE_NONE)) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "invalid partial state", out_error);
        return NULL;
    }
    if (TINYPY_VALUE_KIND(keywords) == TINYPY_VALUE_NONE) {
        owned_keywords = tinypy_dict_new(vm);
        keywords = owned_keywords;
    }
    TINYPY_INCREF(callable);
    TINYPY_INCREF(stored_args);
    TINYPY_INCREF(keywords);
    if (TINYPY_VALUE_KIND(dict) != TINYPY_VALUE_NONE) {
        TINYPY_INCREF(dict);
    }
    TINYPY_DECREF(partial->callable);
    TINYPY_DECREF(partial->args);
    TINYPY_DECREF(partial->keywords);
    if (partial->dict != NULL) {
        TINYPY_DECREF(partial->dict);
    }
    partial->callable = callable;
    partial->args = stored_args;
    partial->keywords = keywords;
    partial->dict = TINYPY_VALUE_KIND(dict) != TINYPY_VALUE_NONE ? dict : NULL;
    if (owned_keywords != NULL) {
        TINYPY_DECREF(owned_keywords);
    }
    tinypy_value_t *return_value_1 = tinypy_none_get(vm);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_initialize_partial_type(tinypy_vm_t *vm) {
    tinypy_type_t *type = &vm->types[TINYPY_VALUE_PARTIAL];
    tinypy_value_t *call = tinypy_native_function_new(vm, "__call__", 8U, __tinypy_partial_call_method, NULL, NULL);
    tinypy_value_t *reduce = tinypy_native_function_new(vm, "__reduce__", 10U, __tinypy_partial_reduce_method, NULL, NULL);
    tinypy_value_t *setstate = tinypy_native_function_new(vm, "__setstate__", 12U, __tinypy_partial_setstate_method, NULL, NULL);
    tinypy_value_t *module = tinypy_string_from_bytes(vm, "functools", 9U);
    tinypy_value_t *dict_descriptor = tinypy_internal_instance_dict_descriptor_new(type);

    tinypy_internal_constructor_add_builtin_new(type);
    tinypy_type_set_attr(type, "__module__", 10U, module);
    tinypy_type_set_attr(type, "__dict__", 8U, dict_descriptor);
    tinypy_type_set_attr(type, "__call__", 8U, call);
    tinypy_type_set_attr(type, "__reduce__", 10U, reduce);
    tinypy_type_set_attr(type, "__setstate__", 12U, setstate);
    TINYPY_DECREF(setstate);
    TINYPY_DECREF(reduce);
    TINYPY_DECREF(call);
    TINYPY_DECREF(dict_descriptor);
    TINYPY_DECREF(module);
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_functools_reduce(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
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
    tinypy_value_t *reduce = tinypy_native_function_new(vm, "reduce", 6U, tinypy_internal_functools_reduce, NULL, NULL);

    tinypy_module_add_value(module, "__name__", 8U, name);
    tinypy_module_add_value(module, "partial", 7U, &vm->types[TINYPY_VALUE_PARTIAL].base.base);
    tinypy_module_add_value(module, "reduce", 6U, reduce);
    TINYPY_DECREF(reduce);
    TINYPY_DECREF(name);
    tinypy_internal_register_module(vm, "_functools", 10U, module);
    TINYPY_DECREF(module);
}
