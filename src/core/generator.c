#include "tinypy/generator.h"

#include "internal.h"

//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_generator_from_frame(tinypy_value_t *frame) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(frame);
    tinypy_generator_object_t *generator = (tinypy_generator_object_t *)tinypy_internal_value_allocate(vm, TINYPY_VALUE_GENERATOR, sizeof(*generator));
    generator->weakrefs = NULL;
    generator->frame = frame;
    generator->code = TINYPY_FRAME_OBJECT(frame)->code;
    TINYPY_INCREF(frame);
    TINYPY_INCREF(generator->code);
    return &generator->base;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_generator_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data) {
    tinypy_generator_object_t *generator = TINYPY_GENERATOR_OBJECT(value);

    if (generator->frame != NULL) {
        visit(generator->frame, user_data);
    }
    visit(generator->code, user_data);
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_generator_iter(tinypy_value_t *value, tinypy_error_t **out_error) {
    TINYPY_CLEAR_ERROR(out_error);
    return TINYPY_RET(value);
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_generator_send(tinypy_value_t *generator_value, tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_bool_t yielded = TINYPY_FALSE;

    tinypy_vm_t *vm = TINYPY_VALUE_VM(generator_value);
    tinypy_generator_object_t *generator = TINYPY_GENERATOR_OBJECT(generator_value);
    TINYPY_CLEAR_ERROR(out_error);
    if (generator->running != 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "generator already executing", out_error);
        return NULL;
    }
    if (generator->finished != 0) {
        return NULL;
    }
    if (generator->started == 0 && TINYPY_VALUE_KIND(value) != TINYPY_VALUE_NONE) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "can't send non-None value to a just-started generator", out_error);
        return NULL;
    }
    generator->running = 1;
    tinypy_value_t *result = tinypy_internal_eval_generator_resume(generator, value, NULL, NULL, NULL, &yielded, out_error);
    generator->running = 0;
    generator->started = 1;
    if (yielded != 0) {
        return result;
    }
    generator->finished = 1;
    TINYPY_DECREF(generator->frame);
    generator->frame = NULL;
    if (result != NULL) {
        TINYPY_DECREF(result);
        return NULL;
    }
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_generator_throw(tinypy_value_t *generator_value, tinypy_value_t *exception_type, tinypy_value_t *exception, tinypy_value_t *traceback, tinypy_error_t **out_error) {
    tinypy_value_t *result;
    tinypy_bool_t yielded = TINYPY_FALSE;

    tinypy_vm_t *vm = TINYPY_VALUE_VM(generator_value);
    tinypy_generator_object_t *generator = TINYPY_GENERATOR_OBJECT(generator_value);
    TINYPY_CLEAR_ERROR(out_error);
    if (generator->running != 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "generator already executing", out_error);
        return NULL;
    }
    if (generator->finished != 0) {
        if (exception_type != NULL) {
            tinypy_internal_exception_set_raised_type(vm, exception_type, exception, traceback);
        }
        else {
            tinypy_internal_exception_set_raised(vm, exception, traceback);
        }
        tinypy_internal_exception_make_diagnostic(vm, out_error);
        return NULL;
    }
    tinypy_value_t *none = TINYPY_RET_NONE(vm);
    generator->running = 1;
    result = tinypy_internal_eval_generator_resume(generator, none, exception_type, exception, traceback, &yielded, out_error);
    generator->running = 0;
    generator->started = 1;
    TINYPY_DECREF(none);
    if (yielded != 0) {
        return result;
    }
    generator->finished = 1;
    TINYPY_DECREF(generator->frame);
    generator->frame = NULL;
    if (result != NULL) {
        TINYPY_DECREF(result);
        return NULL;
    }
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_generator_throw(tinypy_value_t *generator_value, tinypy_value_t *exception, tinypy_value_t *traceback, tinypy_error_t **out_error) {
    tinypy_value_t *result = __tinypy_generator_throw(generator_value, NULL, exception, traceback, out_error);
    return result;
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
tinypy_bool_t tinypy_generator_close(tinypy_value_t *generator_value, tinypy_error_t **out_error) {
    tinypy_value_t *exception;
    tinypy_value_t *result;

    tinypy_vm_t *vm = TINYPY_VALUE_VM(generator_value);
    tinypy_generator_object_t *generator = TINYPY_GENERATOR_OBJECT(generator_value);
    TINYPY_CLEAR_ERROR(out_error);
    if (generator->finished != 0) {
        return TINYPY_TRUE;
    }
    tinypy_value_t *empty = TINYPY_RET_EMPTY_TUPLE(vm);
    exception = tinypy_internal_exception_instantiate(vm->exception_types[TINYPY_EXCEPTION_GENERATOR_EXIT], empty, NULL, out_error);
    TINYPY_DECREF(empty);
    if (exception == NULL) {
        return TINYPY_FALSE;
    }
    result = tinypy_generator_throw(generator_value, exception, NULL, out_error);
    TINYPY_DECREF(exception);
    if (result != NULL) {
        TINYPY_DECREF(result);
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_RUNTIME, "generator ignored GeneratorExit", out_error);
        return TINYPY_FALSE;
    }
    if (vm->raised_type != NULL) {
        if (TINYPY_VALUE_KIND(vm->raised_type) == TINYPY_VALUE_TYPE && (tinypy_type_is_subtype((tinypy_type_t *)vm->raised_type, vm->exception_types[TINYPY_EXCEPTION_GENERATOR_EXIT]) != 0 || tinypy_type_is_subtype((tinypy_type_t *)vm->raised_type, vm->exception_types[TINYPY_EXCEPTION_STOP_ITERATION]) != 0)) {
            __tinypy_generator_discard_error(vm, out_error);
            return TINYPY_TRUE;
        }
        return TINYPY_FALSE;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_generator_next(tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_value_t *none = TINYPY_RET_NONE(vm);
    tinypy_value_t *result = tinypy_generator_send(value, none, out_error);

    TINYPY_DECREF(none);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_generator_method_arguments(tinypy_vm_t *vm, tinypy_value_t *args, tinypy_value_t *kwargs, size_t count, tinypy_error_t **out_error) {
    if ((kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U) || TINYPY_TUPLE_SIZE(args) != count) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "generator method received invalid arguments", out_error);
        return TINYPY_FALSE;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_generator_bound_arguments(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, size_t minimum, size_t maximum, tinypy_arity_style_e style, tinypy_bool_t wrapper, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *name = TINYPY_NATIVE_FUNCTION_OBJECT(function)->name;
    size_t count = TINYPY_TUPLE_SIZE(args);
    size_t supplied = count != 0U ? count - 1U : 0U;

    if (kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U) {
        tinypy_message_part_t parts[] = {
            {wrapper != TINYPY_FALSE ? "wrapper " : "", wrapper != TINYPY_FALSE ? 8U : 0U},
            TINYPY_MESSAGE_PART_TEXT(name),
            {wrapper != TINYPY_FALSE ? " doesn't take keyword arguments" : "() takes no keyword arguments", wrapper != TINYPY_FALSE ? sizeof(" doesn't take keyword arguments") - 1U : sizeof("() takes no keyword arguments") - 1U},
        };
        tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, 3U, out_error);
        return TINYPY_FALSE;
    }
    if (count == 0U || supplied < minimum || supplied > maximum) {
        if (wrapper != TINYPY_FALSE) {
            char count_buffer[TINYPY_MESSAGE_SIZE_BUFFER];
            size_t count_size = tinypy_internal_format_size(count_buffer, supplied);
            tinypy_message_part_t parts[] = {
                TINYPY_MESSAGE_PART_LITERAL("expected 0 arguments, got "),
                {count_buffer, count_size},
            };
            tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, 2U, out_error);
        }
        else {
            tinypy_internal_make_arity_error(vm, (const char *)TINYPY_TEXT_BYTES(name), TINYPY_TEXT_BYTE_SIZE(name), supplied, minimum, maximum, style, out_error);
        }
        return TINYPY_FALSE;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_generator_next_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_error_t *iteration_error = NULL;

    (void)user_data;
    if (TINYPY_TUPLE_SIZE(args) != 0U && TINYPY_VALUE_KIND(TINYPY_TUPLE_GET(args, 0U)) == TINYPY_VALUE_GENERATOR
        ? __tinypy_generator_bound_arguments(function, args, kwargs, 0U, 0U, TINYPY_ARITY_STYLE_PARSED, TINYPY_TRUE, out_error) == TINYPY_FALSE
        : tinypy_internal_native_method_arguments(function, args, kwargs, 0U, 0U, TINYPY_ARITY_STYLE_WRAPPER, out_error) == TINYPY_FALSE) {
        return NULL;
    }
    tinypy_value_t *item = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *result = tinypy_internal_next_raw(item, &iteration_error);
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
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_generator_bound_arguments(function, args, kwargs, 1U, 1U, TINYPY_ARITY_STYLE_SINGLE, TINYPY_FALSE, out_error) == TINYPY_FALSE) {
        return NULL;
    }
    tinypy_value_t *item = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *item_2 = TINYPY_TUPLE_GET(args, 1U);
    tinypy_value_t *result = tinypy_generator_send(item, item_2, out_error);
    if (result != NULL || vm->raised_value != NULL) {
        return result;
    }
    tinypy_internal_exception_raise_stop_iteration(vm, out_error);
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_generator_throw_constructor_error(tinypy_value_t *generator, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(generator);
    tinypy_internal_exception_state_t state;
    tinypy_value_t *result;

    if (vm->raised_value == NULL) {
        return NULL;
    }
    tinypy_internal_exception_preserve_begin(vm, &state);
    if (out_error != NULL && *out_error != NULL) {
        tinypy_error_release(*out_error);
        *out_error = NULL;
    }
    result = __tinypy_generator_throw(generator, state.type, state.value, state.traceback, out_error);
    TINYPY_DECREF(state.type);
    TINYPY_DECREF(state.value);
    if (state.traceback != NULL) {
        TINYPY_DECREF(state.traceback);
    }
    if (result == NULL && vm->raised_value == NULL) {
        tinypy_internal_exception_raise_stop_iteration(vm, out_error);
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_generator_throw_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *exception = NULL;
    tinypy_value_t *traceback = NULL;
    tinypy_value_t *result;
    size_t count;

    (void)user_data;
    count = TINYPY_TUPLE_SIZE(args);
    if (__tinypy_generator_bound_arguments(function, args, kwargs, 1U, 3U, TINYPY_ARITY_STYLE_UNPACK, TINYPY_FALSE, out_error) == TINYPY_FALSE) {
        return NULL;
    }
    tinypy_value_t *generator = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *exception_argument = TINYPY_TUPLE_GET(args, 1U);
    tinypy_bool_t condition = count == 4U;
    if (condition != 0) {
        tinypy_value_t *item = TINYPY_TUPLE_GET(args, 3U);
        condition = TINYPY_VALUE_KIND(item) != TINYPY_VALUE_NONE;
    }
    if (condition) {
        traceback = TINYPY_TUPLE_GET(args, 3U);
        if (TINYPY_VALUE_KIND(traceback) != TINYPY_VALUE_TRACEBACK) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "throw() third argument must be a traceback object", out_error);
            return NULL;
        }
    }
    if (TINYPY_VALUE_KIND(exception_argument) == TINYPY_VALUE_TYPE && tinypy_type_is_subtype((tinypy_type_t *)exception_argument, vm->exception_types[TINYPY_EXCEPTION_BASE]) != 0) {
        tinypy_bool_t condition_2 = count >= 3U;
        if (condition_2 != 0) {
            tinypy_value_t *item = TINYPY_TUPLE_GET(args, 2U);
            condition_2 = tinypy_type_is_subtype(item->type, (tinypy_type_t *)exception_argument) != 0;
        }
        if (condition_2) {
            exception = TINYPY_RET(TINYPY_TUPLE_GET(args, 2U));
        }
        else {
            tinypy_value_t *exception_args;

            if (count >= 3U) {
                tinypy_value_t *argument = TINYPY_TUPLE_GET(args, 2U);
                if (TINYPY_VALUE_KIND(argument) == TINYPY_VALUE_TUPLE) {
                    exception_args = TINYPY_RET(argument);
                }
                else if (TINYPY_VALUE_KIND(argument) == TINYPY_VALUE_NONE) {
                    exception_args = TINYPY_RET_EMPTY_TUPLE(vm);
                }
                else {
                    exception_args = tinypy_tuple_from_items(vm, &argument, 1U);
                }
            }
            else {
                exception_args = TINYPY_RET_EMPTY_TUPLE(vm);
            }
            exception = tinypy_call(exception_argument, exception_args, NULL, out_error);
            TINYPY_DECREF(exception_args);
            if (exception == NULL) {
                tinypy_value_t *failure_result = __tinypy_generator_throw_constructor_error(generator, out_error);

                return failure_result;
            }
        }
    }
    else if (TINYPY_VALUE_KIND(exception_argument) == TINYPY_VALUE_CLASS) {
        /* Classic classes are legal exceptions in Python 2, and contextlib
           relies on throwing them into a generator. */
        tinypy_value_t *item = count >= 3U ? TINYPY_TUPLE_GET(args, 2U) : NULL;

        if (item != NULL && TINYPY_VALUE_KIND(item) == TINYPY_VALUE_OLD_INSTANCE && tinypy_class_is_subclass(tinypy_old_instance_class(item), exception_argument) != 0) {
            exception = TINYPY_RET(item);
        }
        else {
            tinypy_value_t *exception_args;

            if (item != NULL && TINYPY_VALUE_KIND(item) == TINYPY_VALUE_TUPLE) {
                exception_args = TINYPY_RET(item);
            }
            else {
                exception_args = item != NULL && TINYPY_VALUE_KIND(item) != TINYPY_VALUE_NONE
                                     ? tinypy_tuple_from_items(vm, &item, 1U)
                                     : TINYPY_RET_EMPTY_TUPLE(vm);
            }

            exception = tinypy_call(exception_argument, exception_args, NULL, out_error);
            TINYPY_DECREF(exception_args);
            if (exception == NULL) {
                tinypy_value_t *failure_result = __tinypy_generator_throw_constructor_error(generator, out_error);

                return failure_result;
            }
        }
    }
    else if (tinypy_type_is_subtype(exception_argument->type, vm->exception_types[TINYPY_EXCEPTION_BASE]) != 0 || TINYPY_VALUE_KIND(exception_argument) == TINYPY_VALUE_OLD_INSTANCE) {
        if (count >= 3U && TINYPY_VALUE_KIND(TINYPY_TUPLE_GET(args, 2U)) != TINYPY_VALUE_NONE) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "instance exception may not have a separate value", out_error);
            return NULL;
        }
        exception = TINYPY_RET(exception_argument);
    }
    else {
        tinypy_message_part_t parts[] = {
            TINYPY_MESSAGE_PART_LITERAL("exceptions must be classes, or instances, not "),
            TINYPY_MESSAGE_PART_TYPE_NAME(exception_argument),
        };
        tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, 2U, out_error);
        return NULL;
    }
    tinypy_value_t *exception_type = NULL;
    if (TINYPY_VALUE_KIND(exception_argument) == TINYPY_VALUE_TYPE && tinypy_type_is_subtype(exception->type, (tinypy_type_t *)exception_argument) == 0) {
        exception_type = exception_argument;
    }
    result = __tinypy_generator_throw(generator, exception_type, exception, traceback, out_error);
    TINYPY_DECREF(exception);
    if (result != NULL || vm->raised_value != NULL) {
        return result;
    }
    tinypy_internal_exception_raise_stop_iteration(vm, out_error);
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_generator_close_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_generator_bound_arguments(function, args, kwargs, 0U, 0U, TINYPY_ARITY_STYLE_PARSED, TINYPY_FALSE, out_error) == TINYPY_FALSE) {
        return NULL;
    }
    tinypy_value_t *item = TINYPY_TUPLE_GET(args, 0U);
    if (tinypy_generator_close(item, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *return_value_1 = TINYPY_RET_NONE(vm);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_generator_iter_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {

    (void)user_data;
    if (TINYPY_TUPLE_SIZE(args) != 0U && TINYPY_VALUE_KIND(TINYPY_TUPLE_GET(args, 0U)) == TINYPY_VALUE_GENERATOR
        ? __tinypy_generator_bound_arguments(function, args, kwargs, 0U, 0U, TINYPY_ARITY_STYLE_PARSED, TINYPY_TRUE, out_error) == TINYPY_FALSE
        : tinypy_internal_native_method_arguments(function, args, kwargs, 0U, 0U, TINYPY_ARITY_STYLE_WRAPPER, out_error) == TINYPY_FALSE) {
        return NULL;
    }
    tinypy_value_t *self = TINYPY_RET(TINYPY_TUPLE_GET(args, 0U));
    return self;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_generator_repr_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_generator_method_arguments(vm, args, kwargs, 1U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *self = TINYPY_TUPLE_GET(args, 0U);
    if (TINYPY_VALUE_KIND(self) != TINYPY_VALUE_GENERATOR) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "__repr__ requires a generator", out_error);
        return NULL;
    }
    tinypy_value_t *result = tinypy_internal_object_repr_builtin(self, out_error);

    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_reversed_length_hint_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 0U, 0U, TINYPY_ARITY_STYLE_PARSED, out_error) == TINYPY_FALSE) {
        return NULL;
    }
    tinypy_value_t *self = TINYPY_TUPLE_GET(args, 0U);
    if (TINYPY_VALUE_KIND(self) != TINYPY_VALUE_REVERSED) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "__length_hint__ requires a reversed object", out_error);
        return NULL;
    }
    size_t hint = tinypy_internal_reversed_size_hint(self, out_error);
    if (out_error != NULL && *out_error != NULL) {
        return NULL;
    }
    tinypy_value_t *return_value = self->type == vm->iterator_types[TINYPY_ITERATOR_TYPE_LIST_REVERSE]
                                      ? tinypy_long_from_i64(vm, (int64_t)hint)
                                      : tinypy_integer_from_i64(vm, (int64_t)hint);
    return return_value;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_initialize_generator_types(tinypy_vm_t *vm) {
    size_t index;

    tinypy_internal_type_add_method((&vm->types[TINYPY_VALUE_GENERATOR]), vm->internal_special_next_key, __tinypy_generator_next_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method((&vm->types[TINYPY_VALUE_GENERATOR]), vm->internal_send_key, __tinypy_generator_send_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method((&vm->types[TINYPY_VALUE_GENERATOR]), vm->internal_throw_key, __tinypy_generator_throw_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method((&vm->types[TINYPY_VALUE_GENERATOR]), vm->internal_close_key, __tinypy_generator_close_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method((&vm->types[TINYPY_VALUE_GENERATOR]), vm->internal_special_iter_key, __tinypy_generator_iter_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method((&vm->types[TINYPY_VALUE_GENERATOR]), vm->internal_special_repr_key, __tinypy_generator_repr_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method((&vm->types[TINYPY_VALUE_ITERATOR]), vm->internal_special_next_key, __tinypy_generator_next_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method((&vm->types[TINYPY_VALUE_ITERATOR]), vm->internal_special_iter_key, __tinypy_generator_iter_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method((&vm->types[TINYPY_VALUE_ITERATOR]), vm->internal_special_length_hint_key, tinypy_internal_iterator_length_hint_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    for (index = 0U; index < TINYPY_ITERATOR_TYPE_COUNT; ++index) {
        tinypy_internal_type_add_method((vm->iterator_types[index]), vm->internal_special_next_key, __tinypy_generator_next_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
        tinypy_internal_type_add_method((vm->iterator_types[index]), vm->internal_special_iter_key, __tinypy_generator_iter_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
        if (index == (size_t)TINYPY_ITERATOR_TYPE_LIST_REVERSE) {
            tinypy_internal_type_add_method((vm->iterator_types[index]), vm->internal_special_length_hint_key, __tinypy_reversed_length_hint_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
        }
        else if (index != (size_t)TINYPY_ITERATOR_TYPE_CALLABLE) {
            tinypy_internal_type_add_method((vm->iterator_types[index]), vm->internal_special_length_hint_key, tinypy_internal_iterator_length_hint_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
        }
    }
    tinypy_internal_type_add_method((&vm->types[TINYPY_VALUE_ENUMERATE]), vm->internal_special_next_key, __tinypy_generator_next_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method((&vm->types[TINYPY_VALUE_ENUMERATE]), vm->internal_special_iter_key, __tinypy_generator_iter_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method((&vm->types[TINYPY_VALUE_REVERSED]), vm->internal_special_next_key, __tinypy_generator_next_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method((&vm->types[TINYPY_VALUE_REVERSED]), vm->internal_special_iter_key, __tinypy_generator_iter_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method((&vm->types[TINYPY_VALUE_REVERSED]), vm->internal_special_length_hint_key, __tinypy_reversed_length_hint_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_generator_frame(const tinypy_value_t *generator) {
    tinypy_value_t *return_value_1 = TINYPY_GENERATOR_OBJECT((tinypy_value_t *)generator)->frame;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_generator_finished(const tinypy_value_t *generator) {
    tinypy_bool_t return_value_1 = TINYPY_GENERATOR_OBJECT((tinypy_value_t *)generator)->finished != 0 ? TINYPY_TRUE : TINYPY_FALSE;
    return return_value_1;
}
