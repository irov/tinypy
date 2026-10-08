#include "internal.h"

//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_functools_no_keywords(tinypy_value_t *function, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    if (kwargs == NULL || TINYPY_DICT_SIZE(kwargs) == 0U) {
        return TINYPY_TRUE;
    }
    const tinypy_message_part_t parts[] = {
        TINYPY_MESSAGE_PART_TEXT(tinypy_native_function_name(function)),
        TINYPY_MESSAGE_PART_LITERAL("() takes no keyword arguments")
    };

    tinypy_internal_make_vm_error_parts(TINYPY_VALUE_VM(function), TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
    return TINYPY_FALSE;
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
static tinypy_value_t *__tinypy_functools_copy_keywords(tinypy_value_t *source, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(source);
    tinypy_value_t *result = tinypy_internal_dict_new_checked(vm, out_error);

    if (result == NULL) {
        return NULL;
    }
    TINYPY_INCREF(source);
    tinypy_bool_t copied = tinypy_internal_dict_update_from(result, source, "NULL result without error in PyObject_Call", out_error);

    TINYPY_DECREF(source);
    if (copied == 0) {
        TINYPY_DECREF(result);
        return NULL;
    }
    return result;
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
    if (callable->type->call == NULL && tinypy_internal_object_has_special_key(callable, vm->internal_special_call_key) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "the first argument must be callable", out_error);
        return NULL;
    }
    tinypy_value_t *keywords = kwargs != NULL ? __tinypy_functools_copy_keywords(kwargs, out_error) : tinypy_dict_new(vm);
    if (keywords == NULL) {
        return NULL;
    }
    tinypy_partial_object_t *partial = (tinypy_partial_object_t *)tinypy_internal_object_allocate(vm, type, type->basic_size);
    partial->callable = callable;
    TINYPY_INCREF(callable);
    tinypy_value_t *selected_value;
    if (argument_count == 1U) {
        selected_value = TINYPY_RET_EMPTY_TUPLE(vm);
    }
    else {
        tinypy_value_t *const *tuple_items = tinypy_internal_tuple_items(args);
        selected_value = tinypy_tuple_from_items(vm, &tuple_items[1], argument_count - 1U);
    }
    partial->args = selected_value;
    partial->keywords = keywords;
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
        combined_kwargs = __tinypy_functools_copy_keywords(partial->keywords, out_error);
        if (combined_kwargs == NULL) {
            TINYPY_DECREF(combined_args);
            return NULL;
        }
        if (call_keyword_count != 0U && tinypy_internal_dict_update_from(combined_kwargs, kwargs, "NULL result without error in PyObject_Call", out_error) == 0) {
            TINYPY_DECREF(combined_kwargs);
            TINYPY_DECREF(combined_args);
            return NULL;
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
    none = TINYPY_RET_NONE(vm);
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
    if (stored_args->type != &vm->types[TINYPY_VALUE_TUPLE]) {
        tinypy_value_t *arguments = tinypy_tuple_from_items(vm, &stored_args, 1U);

        stored_args = tinypy_internal_tuple_create(&vm->types[TINYPY_VALUE_TUPLE], arguments, NULL, out_error);
        TINYPY_DECREF(arguments);
        if (stored_args == NULL) {
            return NULL;
        }
    }
    else {
        TINYPY_INCREF(stored_args);
    }
    if (TINYPY_VALUE_KIND(keywords) == TINYPY_VALUE_NONE) {
        keywords = tinypy_dict_new(vm);
    }
    else if (keywords->type != &vm->types[TINYPY_VALUE_DICT]) {
        keywords = __tinypy_functools_copy_keywords(keywords, out_error);
        if (keywords == NULL) {
            TINYPY_DECREF(stored_args);
            return NULL;
        }
    }
    else {
        TINYPY_INCREF(keywords);
    }
    TINYPY_INCREF(callable);
    if (TINYPY_VALUE_KIND(dict) != TINYPY_VALUE_NONE) {
        TINYPY_INCREF(dict);
    }
    tinypy_value_t *old_callable = partial->callable;

    partial->callable = callable;
    TINYPY_DECREF(old_callable);
    tinypy_value_t *old_args = partial->args;

    partial->args = stored_args;
    TINYPY_DECREF(old_args);
    tinypy_value_t *old_keywords = partial->keywords;

    partial->keywords = keywords;
    TINYPY_DECREF(old_keywords);
    tinypy_value_t *old_dict = partial->dict;

    partial->dict = TINYPY_VALUE_KIND(dict) != TINYPY_VALUE_NONE ? dict : NULL;
    if (old_dict != NULL) {
        TINYPY_DECREF(old_dict);
    }
    tinypy_value_t *return_value_1 = TINYPY_RET_NONE(vm);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_initialize_partial_type(tinypy_vm_t *vm) {
    tinypy_type_t *type = &vm->types[TINYPY_VALUE_PARTIAL];
    tinypy_internal_type_add_method(type, vm->internal_special_call_key, __tinypy_partial_call_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(type, vm->internal_special_reduce_key, __tinypy_partial_reduce_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(type, vm->internal_special_setstate_key, __tinypy_partial_setstate_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_value_t *module = TINYPY_RET(vm->internal_functools_module_name);
    tinypy_value_t *dict_descriptor = tinypy_internal_instance_dict_descriptor_new(type);

    tinypy_internal_constructor_add_builtin_new(type);
    tinypy_type_set_attr_key(type, type->vm->internal_special_module_key, module);
    tinypy_type_set_attr_key(type, type->vm->internal_special_dict_key, dict_descriptor);
    TINYPY_DECREF(dict_descriptor);
    TINYPY_DECREF(module);
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_functools_reduce(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    size_t argument_count = TINYPY_TUPLE_SIZE(args);
    tinypy_value_t *accumulator = NULL;
    tinypy_error_t *iteration_error = NULL;

    (void)user_data;
    if (__tinypy_functools_no_keywords(function, kwargs, out_error) == 0) {
        return NULL;
    }
    if (argument_count < 2U || argument_count > 3U) {
        tinypy_value_t *name = tinypy_native_function_name(function);

        tinypy_internal_make_arity_error(vm, (const char *)TINYPY_TEXT_BYTES(name), TINYPY_TEXT_BYTE_SIZE(name), argument_count, 2U, 3U, TINYPY_ARITY_STYLE_UNPACK, out_error);
        return NULL;
    }
    tinypy_value_t *item_2 = TINYPY_TUPLE_GET(args, 1U);
    tinypy_error_t *iterator_error = NULL;
    tinypy_value_t *iterator = tinypy_iter(item_2, &iterator_error);
    if (iterator == NULL) {
        if (iterator_error != NULL) {
            tinypy_error_release(iterator_error);
        }
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "reduce() arg 2 must support iteration", out_error);
        return NULL;
    }
    if (argument_count == 3U) {
        accumulator = TINYPY_RET(TINYPY_TUPLE_GET(args, 2U));
    }
    tinypy_value_t *call_args = tinypy_internal_tuple_new_checked(vm, 2U, out_error);
    if (call_args == NULL) {
        if (accumulator != NULL) {
            TINYPY_DECREF(accumulator);
        }
        TINYPY_DECREF(iterator);
        return NULL;
    }
    for (;;) {
        if (call_args->ref > 1U) {
            TINYPY_DECREF(call_args);
            call_args = tinypy_internal_tuple_new_checked(vm, 2U, out_error);
            if (call_args == NULL) {
                if (accumulator != NULL) {
                    TINYPY_DECREF(accumulator);
                }
                TINYPY_DECREF(iterator);
                return NULL;
            }
        }
        tinypy_value_t *item = tinypy_next(iterator, &iteration_error);

        if (item == NULL) {
            break;
        }
        if (accumulator == NULL) {
            accumulator = item;
            continue;
        }
        tinypy_tuple_set(call_args, 0U, accumulator);
        TINYPY_DECREF(accumulator);
        tinypy_tuple_set(call_args, 1U, item);
        TINYPY_DECREF(item);
        tinypy_value_t *item_3 = TINYPY_TUPLE_GET(args, 0U);

        accumulator = tinypy_call(item_3, call_args, NULL, out_error);
        if (accumulator == NULL) {
            TINYPY_DECREF(call_args);
            TINYPY_DECREF(iterator);
            return NULL;
        }
    }
    TINYPY_DECREF(call_args);
    if (iteration_error != NULL) {
        if (accumulator != NULL) {
            TINYPY_DECREF(accumulator);
        }
        TINYPY_DECREF(iterator);
        if (out_error != NULL) {
            *out_error = iteration_error;
        }
        else {
            tinypy_error_release(iteration_error);
        }
        return NULL;
    }
    if (accumulator == NULL) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "reduce() of empty sequence with no initial value", out_error);
    }
    TINYPY_DECREF(iterator);
    return accumulator;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_initialize_functools_module(tinypy_vm_t *vm) {
    tinypy_value_t *module = tinypy_module_new_key(vm->internal_partial_module_name);
    tinypy_value_t *name = TINYPY_RET(vm->internal_partial_module_name);
    tinypy_value_t *reduce = tinypy_native_function_new_key(vm->internal_reduce_key, tinypy_internal_functools_reduce, NULL, NULL);

    tinypy_module_add_value_key(module, vm->internal_special_name_key, name);
    tinypy_module_add_value_key(module, vm->internal_partial_key, &vm->types[TINYPY_VALUE_PARTIAL].base.base);
    tinypy_module_add_value_key(module, vm->internal_reduce_key, reduce);
    TINYPY_DECREF(reduce);
    TINYPY_DECREF(name);
    tinypy_internal_register_module(vm, vm->internal_partial_module_name, module);
    TINYPY_DECREF(module);
}
