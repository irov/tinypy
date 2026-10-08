#include "tinypy/output.h"

#include "internal.h"

//////////////////////////////////////////////////////////////////////////
void tinypy_output_emit(tinypy_vm_t *vm, tinypy_output_channel_e channel, const void *bytes, size_t size) {
    if (vm->has_host != 0 && vm->host.emit_output != NULL && size != 0U) {
        vm->host.emit_output(vm->host.user_data, channel, bytes, size);
    }
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_output_unraisable_text(tinypy_value_t *stream, const void *bytes, size_t size) {
    tinypy_error_t *error = NULL;

    (void)tinypy_internal_output_write(stream, bytes, size, &error);
    if (error != NULL) {
        tinypy_error_release(error);
    }
    tinypy_internal_exception_clear_raised(TINYPY_VALUE_VM(stream));
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_output_unraisable_repr(tinypy_value_t *stream, tinypy_value_t *value, const char *fallback) {
    tinypy_error_t *error = NULL;
    tinypy_value_t *text = value != NULL ? tinypy_object_repr(value, &error) : NULL;

    if (text != NULL) {
        __tinypy_output_unraisable_text(stream, TINYPY_TEXT_BYTES(text), TINYPY_TEXT_BYTE_SIZE(text));
        TINYPY_DECREF(text);
    }
    else {
        __tinypy_output_unraisable_text(stream, fallback, strlen(fallback));
    }
    if (error != NULL) {
        tinypy_error_release(error);
    }
    tinypy_internal_exception_clear_raised(TINYPY_VALUE_VM(stream));
}
//////////////////////////////////////////////////////////////////////////
/* The caller has already separated the exception that was pending before
   its callback. Reporting consumes only the callback's ignored exception. */
void tinypy_internal_output_unraisable(tinypy_vm_t *vm, tinypy_value_t *object) {
    tinypy_internal_exception_state_t state;
    tinypy_value_t *stream;
    tinypy_value_t *module;
    tinypy_error_t *error = NULL;

    if (vm->raised_type == NULL || vm->sys_module == NULL) {
        return;
    }
    tinypy_internal_exception_preserve_begin(vm, &state);
    stream = tinypy_internal_dict_get_optional(vm, TINYPY_MODULE_OBJECT(vm->sys_module)->dict, vm->internal_stderr_key);
    if (stream != NULL) {
        TINYPY_INCREF(stream);
        __tinypy_output_unraisable_text(stream, "Exception ", 10U);
        module = tinypy_object_get_attr_value(state.type, vm->internal_special_module_key, &error);
        if (module != NULL && TINYPY_VALUE_KIND(module) == TINYPY_VALUE_STRING) {
            size_t size = TINYPY_TEXT_BYTE_SIZE(module);

            if (TINYPY_NAME_EQ(module, vm->internal_exception_module_name) == TINYPY_FALSE) {
                __tinypy_output_unraisable_text(stream, TINYPY_TEXT_BYTES(module), size);
                __tinypy_output_unraisable_text(stream, ".", 1U);
            }
        }
        else if (module == NULL) {
            __tinypy_output_unraisable_text(stream, "<unknown>", 9U);
        }
        if (module != NULL) {
            TINYPY_DECREF(module);
        }
        if (error != NULL) {
            tinypy_error_release(error);
        }
        tinypy_internal_exception_clear_raised(vm);
        if (TINYPY_VALUE_KIND(state.type) == TINYPY_VALUE_TYPE) {
            tinypy_type_t *type = (tinypy_type_t *)state.type;

            __tinypy_output_unraisable_text(stream, type->name, type->name_size);
        }
        else if (TINYPY_VALUE_KIND(state.type) == TINYPY_VALUE_CLASS) {
            tinypy_value_t *name = TINYPY_CLASS_OBJECT(state.type)->name;

            __tinypy_output_unraisable_text(stream, TINYPY_TEXT_BYTES(name), TINYPY_TEXT_BYTE_SIZE(name));
        }
        if (state.value != NULL && TINYPY_VALUE_KIND(state.value) != TINYPY_VALUE_NONE) {
            __tinypy_output_unraisable_text(stream, ": ", 2U);
            __tinypy_output_unraisable_repr(stream, state.value, "<exception repr() failed>");
        }
        __tinypy_output_unraisable_text(stream, " in ", 4U);
        __tinypy_output_unraisable_repr(stream, object, "<object repr() failed>");
        __tinypy_output_unraisable_text(stream, " ignored\n", 9U);
        TINYPY_DECREF(stream);
    }
    if (state.type != NULL) {
        TINYPY_DECREF(state.type);
    }
    if (state.value != NULL) {
        TINYPY_DECREF(state.value);
    }
    if (state.traceback != NULL) {
        TINYPY_DECREF(state.traceback);
    }
    tinypy_internal_exception_clear_raised(vm);
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_output_stream_new(tinypy_vm_t *vm, tinypy_output_channel_e channel) {
    tinypy_output_stream_object_t *stream = (tinypy_output_stream_object_t *)tinypy_internal_value_allocate(vm, TINYPY_VALUE_OUTPUT_STREAM, sizeof(*stream));
    stream->channel = channel;
    return &stream->base;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_internal_output_call_write(tinypy_value_t *target, tinypy_value_t *text, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(target);
    tinypy_value_t *write_method = tinypy_object_get_attr_value(target, vm->internal_write_key, out_error);
    tinypy_value_t *args;
    tinypy_value_t *result;

    if (write_method == NULL) {
        return TINYPY_FALSE;
    }
    args = tinypy_tuple_from_items(vm, &text, 1U);
    result = tinypy_call(write_method, args, NULL, out_error);
    TINYPY_DECREF(args);
    TINYPY_DECREF(write_method);
    if (result == NULL) {
        return TINYPY_FALSE;
    }
    TINYPY_DECREF(result);
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_output_write(tinypy_value_t *target, const void *bytes, size_t size, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(target);

    TINYPY_CLEAR_ERROR(out_error);
    if (TINYPY_VALUE_KIND(target) == TINYPY_VALUE_OUTPUT_STREAM) {
        tinypy_output_emit(vm, TINYPY_OUTPUT_STREAM_OBJECT(target)->channel, bytes, size);
        return TINYPY_TRUE;
    }
    tinypy_value_t *text = tinypy_string_from_bytes(vm, bytes, size);
    tinypy_bool_t result = __tinypy_internal_output_call_write(target, text, out_error);

    TINYPY_DECREF(text);
    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_output_write_value(tinypy_value_t *target, tinypy_value_t *text, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(target);
    tinypy_value_type_e target_kind = TINYPY_VALUE_KIND(target);
    tinypy_value_type_e text_kind = TINYPY_VALUE_KIND(text);
    const void *bytes;
    size_t size;

    TINYPY_CLEAR_ERROR(out_error);
    if (text_kind != TINYPY_VALUE_STRING && text_kind != TINYPY_VALUE_UNICODE) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "output write requires a string", out_error);
        return TINYPY_FALSE;
    }
    if (target_kind != TINYPY_VALUE_OUTPUT_STREAM) {
        tinypy_bool_t return_value_1 = __tinypy_internal_output_call_write(target, text, out_error);
        return return_value_1;
    }
    if (text_kind == TINYPY_VALUE_STRING) {
        bytes = tinypy_string_view(text, &size);
    }
    else {
        size_t code_points;

        bytes = tinypy_unicode_utf8_view(text, &size, &code_points);
    }
    tinypy_output_emit(vm, TINYPY_OUTPUT_STREAM_OBJECT(target)->channel, bytes, size);
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_output_soft_space(tinypy_value_t *target, tinypy_bool_t new_flag) {
    if (TINYPY_VALUE_KIND(target) == TINYPY_VALUE_OUTPUT_STREAM) {
        tinypy_output_stream_object_t *stream = TINYPY_OUTPUT_STREAM_OBJECT(target);
        tinypy_bool_t previous = stream->soft_space;

        stream->soft_space = new_flag != TINYPY_FALSE ? TINYPY_TRUE : TINYPY_FALSE;
        return previous;
    }
    tinypy_vm_t *vm = TINYPY_VALUE_VM(target);
    tinypy_internal_exception_state_t state;
    tinypy_error_t *error = NULL;
    tinypy_bool_t previous = TINYPY_FALSE;

    tinypy_internal_exception_preserve_begin(vm, &state);
    tinypy_value_t *value = tinypy_object_get_attr_value(target, vm->internal_softspace_key, &error);

    if (value != NULL) {
        tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);

        if (kind == TINYPY_VALUE_BOOL || kind == TINYPY_VALUE_INTEGER) {
            previous = TINYPY_INTEGER_VALUE(value) != 0 ? TINYPY_TRUE : TINYPY_FALSE;
        }
        TINYPY_DECREF(value);
    }
    if (error != NULL) {
        tinypy_error_release(error);
        error = NULL;
    }
    tinypy_internal_exception_clear_raised(vm);
    value = tinypy_integer_from_i64(vm, new_flag != TINYPY_FALSE ? INT64_C(1) : INT64_C(0));
    (void)tinypy_object_set_attr_value(target, vm->internal_softspace_key, value, &error);
    TINYPY_DECREF(value);
    if (error != NULL) {
        tinypy_error_release(error);
    }
    tinypy_internal_exception_preserve_end(vm, &state);
    return previous;
}
//////////////////////////////////////////////////////////////////////////
/* Py_FlushLine: the newline a print statement left pending on sys.stdout. */
tinypy_bool_t tinypy_internal_output_flush_line(tinypy_vm_t *vm, tinypy_error_t **out_error) {
    tinypy_value_t *stream = tinypy_internal_dict_get_optional_suppressed(vm, TINYPY_MODULE_OBJECT(vm->sys_module)->dict, vm->internal_stdout_key);

    if (stream == NULL) {
        return TINYPY_TRUE;
    }
    TINYPY_INCREF(stream);
    tinypy_bool_t success = TINYPY_TRUE;
    if (tinypy_internal_output_soft_space(stream, TINYPY_FALSE) != TINYPY_FALSE) {
        success = tinypy_internal_output_write(stream, "\n", 1U, out_error);
    }
    TINYPY_DECREF(stream);
    return success;
}
//////////////////////////////////////////////////////////////////////////
/* A host ends or reports a run like PyRun_SimpleFile and PyErr_Print: a
   failed write is dropped and the exception being reported stays raised. */
void tinypy_output_flush_line(tinypy_vm_t *vm) {
    tinypy_internal_exception_state_t state;
    tinypy_error_t *error = NULL;

    tinypy_internal_exception_preserve_begin(vm, &state);
    (void)tinypy_internal_output_flush_line(vm, &error);
    if (error != NULL) {
        tinypy_error_release(error);
    }
    tinypy_internal_exception_preserve_end(vm, &state);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_output_method_arguments(tinypy_vm_t *vm, tinypy_value_t *args, tinypy_value_t *kwargs, size_t count, tinypy_error_t **out_error) {
    if ((kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U) || TINYPY_TUPLE_SIZE(args) != count) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "output stream method received invalid arguments", out_error);
        return TINYPY_FALSE;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_output_write_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    const void *bytes;
    size_t size;

    (void)user_data;
    if (__tinypy_output_method_arguments(vm, args, kwargs, 2U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *stream = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *text = TINYPY_TUPLE_GET(args, 1U);
    if (TINYPY_VALUE_KIND(stream) != TINYPY_VALUE_OUTPUT_STREAM) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "descriptor requires an output stream object", out_error);
        return NULL;
    }
    if (TINYPY_VALUE_KIND(text) == TINYPY_VALUE_STRING) {
        bytes = tinypy_string_view(text, &size);
    }
    else if (TINYPY_VALUE_KIND(text) == TINYPY_VALUE_UNICODE) {
        size_t code_points;

        bytes = tinypy_unicode_utf8_view(text, &size, &code_points);
    }
    else {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "output stream write requires a string", out_error);
        return NULL;
    }
    tinypy_output_emit(vm, TINYPY_OUTPUT_STREAM_OBJECT(stream)->channel, bytes, size);
    tinypy_value_t *return_value_1 = TINYPY_RET_NONE(vm);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_output_flush_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_output_method_arguments(vm, args, kwargs, 1U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *return_value_1 = TINYPY_RET_NONE(vm);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_output_isatty_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_output_method_arguments(vm, args, kwargs, 1U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *return_value_1 = TINYPY_RET_FALSE(vm);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_output_writelines_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_error_t *iteration_error = NULL;

    (void)user_data;
    if (__tinypy_output_method_arguments(vm, args, kwargs, 2U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *stream = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *item = TINYPY_TUPLE_GET(args, 1U);
    if (TINYPY_VALUE_KIND(stream) != TINYPY_VALUE_OUTPUT_STREAM) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "descriptor requires an output stream object", out_error);
        return NULL;
    }
    tinypy_value_t *iterator = tinypy_iter(item, out_error);
    if (iterator == NULL) {
        return NULL;
    }
    for (;;) {
        tinypy_value_t *line = tinypy_next(iterator, &iteration_error);
        const void *bytes;
        size_t size;

        if (line == NULL) {
            break;
        }
        if (TINYPY_VALUE_KIND(line) == TINYPY_VALUE_STRING) {
            bytes = tinypy_string_view(line, &size);
        }
        else if (TINYPY_VALUE_KIND(line) == TINYPY_VALUE_UNICODE) {
            size_t code_points;

            bytes = tinypy_unicode_utf8_view(line, &size, &code_points);
        }
        else {
            TINYPY_DECREF(line);
            TINYPY_DECREF(iterator);
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "writelines requires strings", out_error);
            return NULL;
        }
        tinypy_output_emit(vm, TINYPY_OUTPUT_STREAM_OBJECT(stream)->channel, bytes, size);
        TINYPY_DECREF(line);
    }
    TINYPY_DECREF(iterator);
    if (iteration_error != NULL) {
        if (out_error != NULL) {
            *out_error = iteration_error;
        }
        else {
            tinypy_error_release(iteration_error);
        }
        return NULL;
    }
    tinypy_value_t *return_value_1 = TINYPY_RET_NONE(vm);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
/* The softspace flag of print statements, read and written as an int the
   way file objects expose it. */
static tinypy_value_t *__tinypy_output_softspace_property(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    size_t count = user_data != NULL ? 2U : 1U;

    if (__tinypy_output_method_arguments(vm, args, kwargs, count, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *stream = TINYPY_TUPLE_GET(args, 0U);
    if (TINYPY_VALUE_KIND(stream) != TINYPY_VALUE_OUTPUT_STREAM) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "descriptor requires an output stream object", out_error);
        return NULL;
    }
    if (user_data == NULL) {
        tinypy_value_t *result = tinypy_integer_from_i64(vm, TINYPY_OUTPUT_STREAM_OBJECT(stream)->soft_space != 0 ? INT64_C(1) : INT64_C(0));

        return result;
    }
    int64_t flag;
    if (tinypy_internal_number_as_i64(TINYPY_TUPLE_GET(args, 1U), &flag, out_error) == 0) {
        return NULL;
    }
    TINYPY_OUTPUT_STREAM_OBJECT(stream)->soft_space = flag != 0 ? TINYPY_TRUE : TINYPY_FALSE;
    tinypy_value_t *result = TINYPY_RET_NONE(vm);

    return result;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_initialize_output_type(tinypy_vm_t *vm) {
    tinypy_value_t *getter = tinypy_native_function_new_key(vm->internal_softspace_key, __tinypy_output_softspace_property, NULL, NULL);
    tinypy_value_t *setter = tinypy_native_function_new_key(vm->internal_softspace_key, __tinypy_output_softspace_property, (void *)(intptr_t)1, NULL);
    tinypy_value_t *softspace = tinypy_property_new(vm, getter, setter, NULL, NULL);

    tinypy_type_set_attr_key(&vm->types[TINYPY_VALUE_OUTPUT_STREAM], vm->internal_softspace_key, softspace);
    TINYPY_DECREF(softspace);
    TINYPY_DECREF(setter);
    TINYPY_DECREF(getter);
    tinypy_internal_type_add_method(&vm->types[TINYPY_VALUE_OUTPUT_STREAM], vm->internal_write_key, __tinypy_output_write_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(&vm->types[TINYPY_VALUE_OUTPUT_STREAM], vm->internal_writelines_key, __tinypy_output_writelines_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(&vm->types[TINYPY_VALUE_OUTPUT_STREAM], vm->internal_flush_key, __tinypy_output_flush_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(&vm->types[TINYPY_VALUE_OUTPUT_STREAM], vm->internal_isatty_key, __tinypy_output_isatty_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
}
