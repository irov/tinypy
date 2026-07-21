#include "tinypy/generator.h"

#include "internal.h"

#include <assert.h>

//////////////////////////////////////////////////////////////////////////
static tinypy_generator_object_t *__tinypy_generator_validate(const tinypy_value_t *value) {
    assert(value != NULL);
    assert(tinypy_internal_vm_valid(tinypy_internal_value_vm(value)));
    assert(tinypy_internal_value_kind(value) == TINYPY_VALUE_GENERATOR);
    return TINYPY_GENERATOR_OBJECT((tinypy_value_t *)value);
}

//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_generator_from_frame(tinypy_value_t *frame) {
    tinypy_vm_t *vm;
    tinypy_generator_object_t *generator;

    assert(frame != NULL);
    vm = tinypy_internal_value_vm(frame);
    assert(tinypy_internal_value_kind(frame) == TINYPY_VALUE_FRAME);
    generator = (tinypy_generator_object_t *)tinypy_internal_value_allocate(vm, TINYPY_VALUE_GENERATOR, sizeof(*generator));
    generator->frame = frame;
    tinypy_retain(frame);
    return &generator->base;
}

//////////////////////////////////////////////////////////////////////////
void tinypy_internal_generator_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data) {
    tinypy_generator_object_t *generator = TINYPY_GENERATOR_OBJECT(value);

    if (generator->frame != NULL) {
        visit(generator->frame, user_data);
    }
    if (generator->handled_type != NULL) {
        visit(generator->handled_type, user_data);
    }
    if (generator->handled_value != NULL) {
        visit(generator->handled_value, user_data);
    }
    if (generator->handled_traceback != NULL) {
        visit(generator->handled_traceback, user_data);
    }
}

//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_generator_iter(tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_internal_clear_error(out_error);
    tinypy_retain(value);
    return value;
}

//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_generator_send(tinypy_value_t *generator_value, tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_generator_object_t *generator = __tinypy_generator_validate(generator_value);
    tinypy_vm_t *vm = tinypy_internal_value_vm(generator_value);
    tinypy_value_t *result;
    int yielded = 0;

    assert(value != NULL);
    assert(tinypy_internal_value_belongs_to(vm, value));
    tinypy_internal_clear_error(out_error);
    if (generator->running != 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "generator already executing", out_error);
        return NULL;
    }
    if (generator->finished != 0) {
        return NULL;
    }
    if (generator->started == 0 && tinypy_internal_value_kind(value) != TINYPY_VALUE_NONE) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "cannot send a non-None value to a just-started generator", out_error);
        return NULL;
    }
    generator->running = 1;
    result = tinypy_internal_eval_generator_resume(generator, value, NULL, NULL, &yielded, out_error);
    generator->running = 0;
    generator->started = 1;
    if (yielded != 0) {
        return result;
    }
    generator->finished = 1;
    tinypy_release(generator->frame);
    generator->frame = NULL;
    if (result != NULL) {
        tinypy_release(result);
        return NULL;
    }
    if (vm->raised_value != NULL && tinypy_type_is_subtype(vm->raised_value->type, vm->exception_types[TINYPY_EXCEPTION_STOP_ITERATION]) != 0) {
        tinypy_internal_exception_clear_raised(vm);
        if (out_error != NULL && *out_error != NULL) {
            tinypy_error_release(*out_error);
            *out_error = NULL;
        }
    }
    return NULL;
}

//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_generator_throw(tinypy_value_t *generator_value, tinypy_value_t *exception, tinypy_value_t *traceback, tinypy_error_t **out_error) {
    tinypy_generator_object_t *generator = __tinypy_generator_validate(generator_value);
    tinypy_vm_t *vm = tinypy_internal_value_vm(generator_value);
    tinypy_value_t *none;
    tinypy_value_t *result;
    int yielded = 0;

    assert(exception != NULL);
    assert(tinypy_internal_value_belongs_to(vm, exception));
    assert(vm->exception_types[TINYPY_EXCEPTION_BASE] != NULL);
    assert(tinypy_type_is_subtype(exception->type, vm->exception_types[TINYPY_EXCEPTION_BASE]) != 0);
    assert(traceback == NULL || tinypy_internal_value_belongs_to(vm, traceback));
    assert(traceback == NULL || tinypy_internal_value_kind(traceback) == TINYPY_VALUE_TRACEBACK);
    tinypy_internal_clear_error(out_error);
    if (generator->running != 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "generator already executing", out_error);
        return NULL;
    }
    if (generator->finished != 0) {
        tinypy_internal_exception_set_raised(vm, exception, traceback);
        tinypy_internal_exception_make_diagnostic(vm, out_error);
        return NULL;
    }
    none = tinypy_none_get(vm);
    generator->running = 1;
    result = tinypy_internal_eval_generator_resume(generator, none, exception, traceback, &yielded, out_error);
    generator->running = 0;
    generator->started = 1;
    tinypy_release(none);
    if (yielded != 0) {
        return result;
    }
    generator->finished = 1;
    tinypy_release(generator->frame);
    generator->frame = NULL;
    if (result != NULL) {
        tinypy_release(result);
        return NULL;
    }
    return NULL;
}

//////////////////////////////////////////////////////////////////////////
static void __tinypy_generator_discard_error(tinypy_vm_t *vm, tinypy_error_t **out_error) {
    tinypy_internal_exception_clear_raised(vm);
    if (out_error != NULL && *out_error != NULL) {
        tinypy_error_release(*out_error);
        *out_error = NULL;
    }
}

//////////////////////////////////////////////////////////////////////////
int32_t tinypy_generator_close(tinypy_value_t *generator_value, tinypy_error_t **out_error) {
    tinypy_generator_object_t *generator = __tinypy_generator_validate(generator_value);
    tinypy_vm_t *vm = tinypy_internal_value_vm(generator_value);
    tinypy_value_t *empty;
    tinypy_value_t *exception;
    tinypy_value_t *result;

    tinypy_internal_clear_error(out_error);
    if (generator->finished != 0) {
        return INT32_C(1);
    }
    empty = tinypy_tuple_from_items(vm, NULL, 0U);
    exception = tinypy_internal_exception_instantiate(vm->exception_types[TINYPY_EXCEPTION_GENERATOR_EXIT], empty, NULL, out_error);
    tinypy_release(empty);
    if (exception == NULL) {
        return INT32_C(0);
    }
    result = tinypy_generator_throw(generator_value, exception, NULL, out_error);
    tinypy_release(exception);
    if (result != NULL) {
        tinypy_release(result);
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_RUNTIME, "generator ignored GeneratorExit", out_error);
        return INT32_C(0);
    }
    if (vm->raised_value != NULL) {
        if (tinypy_type_is_subtype(vm->raised_value->type, vm->exception_types[TINYPY_EXCEPTION_GENERATOR_EXIT]) != 0 || tinypy_type_is_subtype(vm->raised_value->type, vm->exception_types[TINYPY_EXCEPTION_STOP_ITERATION]) != 0) {
            __tinypy_generator_discard_error(vm, out_error);
            return INT32_C(1);
        }
        return INT32_C(0);
    }
    return INT32_C(1);
}

//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_generator_next(tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = tinypy_internal_value_vm(value);
    tinypy_value_t *none = tinypy_none_get(vm);
    tinypy_value_t *result = tinypy_generator_send(value, none, out_error);

    tinypy_release(none);
    return result;
}

//////////////////////////////////////////////////////////////////////////
static int __tinypy_generator_method_arguments(tinypy_vm_t *vm, tinypy_value_t *args, tinypy_value_t *kwargs, size_t count, tinypy_error_t **out_error) {
    if ((kwargs != NULL && tinypy_dict_size(kwargs) != 0U) || tinypy_tuple_size(args) != count) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "generator method received invalid arguments", out_error);
        return 0;
    }
    return 1;
}

//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_generator_next_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);
    tinypy_error_t *iteration_error = NULL;
    tinypy_value_t *result;

    (void)user_data;
    if (__tinypy_generator_method_arguments(vm, args, kwargs, 1U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *item = tinypy_tuple_get(args, 0U);
    result = tinypy_next(item, &iteration_error);
    if (result != NULL) {
        return result;
    }
    if (iteration_error != NULL) {
        if (out_error != NULL) {
            *out_error = iteration_error;
        }
        else {
            tinypy_error_release(iteration_error);
        }
        return NULL;
    }
    tinypy_internal_exception_raise_stop_iteration(vm, out_error);
    return NULL;
}

//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_generator_send_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);
    tinypy_value_t *result;

    (void)user_data;
    if (__tinypy_generator_method_arguments(vm, args, kwargs, 2U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *item = tinypy_tuple_get(args, 0U);
    tinypy_value_t *item_2 = tinypy_tuple_get(args, 1U);
    result = tinypy_generator_send(item, item_2, out_error);
    if (result != NULL || vm->raised_value != NULL) {
        return result;
    }
    tinypy_internal_exception_raise_stop_iteration(vm, out_error);
    return NULL;
}

//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_generator_throw_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);
    tinypy_value_t *generator;
    tinypy_value_t *exception_argument;
    tinypy_value_t *exception = NULL;
    tinypy_value_t *traceback = NULL;
    tinypy_value_t *result;
    size_t count;

    (void)user_data;
    count = tinypy_tuple_size(args);
    if ((kwargs != NULL && tinypy_dict_size(kwargs) != 0U) || count < 2U || count > 4U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "generator.throw received invalid arguments", out_error);
        return NULL;
    }
    generator = tinypy_tuple_get(args, 0U);
    exception_argument = tinypy_tuple_get(args, 1U);
    int condition = count == 4U;
    if (condition != 0) {
        tinypy_value_t *item = tinypy_tuple_get(args, 3U);
        condition = tinypy_internal_value_kind(item) != TINYPY_VALUE_NONE;
    }
    if (condition) {
        traceback = tinypy_tuple_get(args, 3U);
        if (tinypy_internal_value_kind(traceback) != TINYPY_VALUE_TRACEBACK) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "throw traceback must be a traceback", out_error);
            return NULL;
        }
    }
    if (tinypy_internal_value_kind(exception_argument) == TINYPY_VALUE_TYPE && tinypy_type_is_subtype((tinypy_type_t *)exception_argument, vm->exception_types[TINYPY_EXCEPTION_BASE]) != 0) {
        int condition_2 = count >= 3U;
        if (condition_2 != 0) {
            tinypy_value_t *item = tinypy_tuple_get(args, 2U);
            condition_2 = tinypy_type_is_subtype(item->type, (tinypy_type_t *)exception_argument) != 0;
        }
        if (condition_2) {
            exception = tinypy_tuple_get(args, 2U);
            tinypy_retain(exception);
        }
        else {
            tinypy_value_t *exception_args;

            if (count >= 3U) {
                tinypy_value_t *const *tuple_items = tinypy_internal_tuple_items(args);
                exception_args = tinypy_tuple_from_items(vm, &tuple_items[2], 1U);
            }
            else {
                exception_args = tinypy_tuple_from_items(vm, NULL, 0U);
            }
            exception = tinypy_call(exception_argument, exception_args, NULL, out_error);
            tinypy_release(exception_args);
            if (exception == NULL) {
                return NULL;
            }
        }
    }
    else if (tinypy_type_is_subtype(exception_argument->type, vm->exception_types[TINYPY_EXCEPTION_BASE]) != 0 && count == 2U) {
        exception = exception_argument;
        tinypy_retain(exception);
    }
    else {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "exceptions must be old-style classes or derived from BaseException", out_error);
        return NULL;
    }
    result = tinypy_generator_throw(generator, exception, traceback, out_error);
    tinypy_release(exception);
    if (result != NULL || vm->raised_value != NULL) {
        return result;
    }
    tinypy_internal_exception_raise_stop_iteration(vm, out_error);
    return NULL;
}

//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_generator_close_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);

    (void)user_data;
    if (__tinypy_generator_method_arguments(vm, args, kwargs, 1U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *item = tinypy_tuple_get(args, 0U);
    if (tinypy_generator_close(item, out_error) == 0) {
        return NULL;
    }
    return tinypy_none_get(vm);
}

//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_generator_iter_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);
    tinypy_value_t *self;

    (void)user_data;
    if (__tinypy_generator_method_arguments(vm, args, kwargs, 1U, out_error) == 0) {
        return NULL;
    }
    self = tinypy_tuple_get(args, 0U);
    tinypy_retain(self);
    return self;
}

//////////////////////////////////////////////////////////////////////////
static void __tinypy_generator_type_set(tinypy_vm_t *vm, tinypy_type_t *type, const char *name, size_t name_size, tinypy_native_function_callback_t callback) {
    tinypy_value_t *key = tinypy_string_from_bytes(vm, name, name_size);
    tinypy_value_t *function = tinypy_native_function_new(vm, name, name_size, callback, NULL, NULL);

    tinypy_dict_set(type->dict, key, function);
    tinypy_release(function);
    tinypy_release(key);
}

//////////////////////////////////////////////////////////////////////////
void tinypy_internal_initialize_generator_types(tinypy_vm_t *vm) {
    __tinypy_generator_type_set(vm, &vm->generator_type, "next", 4U, __tinypy_generator_next_method);
    __tinypy_generator_type_set(vm, &vm->generator_type, "send", 4U, __tinypy_generator_send_method);
    __tinypy_generator_type_set(vm, &vm->generator_type, "throw", 5U, __tinypy_generator_throw_method);
    __tinypy_generator_type_set(vm, &vm->generator_type, "close", 5U, __tinypy_generator_close_method);
    __tinypy_generator_type_set(vm, &vm->generator_type, "__iter__", 8U, __tinypy_generator_iter_method);
    __tinypy_generator_type_set(vm, &vm->iterator_type, "next", 4U, __tinypy_generator_next_method);
    __tinypy_generator_type_set(vm, &vm->iterator_type, "__iter__", 8U, __tinypy_generator_iter_method);
}

//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_generator_frame(const tinypy_value_t *generator) {
    return __tinypy_generator_validate(generator)->frame;
}
//////////////////////////////////////////////////////////////////////////
int32_t tinypy_generator_finished(const tinypy_value_t *generator) {
    return __tinypy_generator_validate(generator)->finished != 0 ? 1 : 0;
}
