#include "tinypy/output.h"

#include "internal.h"

//////////////////////////////////////////////////////////////////////////
void tinypy_output_emit(tinypy_vm_t *vm, tinypy_output_channel_e channel, const void *bytes, size_t size) {
    if (vm->has_host != 0 && vm->host.emit_output != NULL && size != 0U) {
        vm->host.emit_output(vm->host.user_data, channel, bytes, size);
    }
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_output_stream_new(tinypy_vm_t *vm, tinypy_output_channel_e channel) {
    tinypy_output_stream_object_t *stream = (tinypy_output_stream_object_t *)tinypy_internal_value_allocate(vm, TINYPY_VALUE_OUTPUT_STREAM, sizeof(*stream));
    stream->channel = channel;
    return &stream->base;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_output_write(tinypy_value_t *target, const void *bytes, size_t size, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(target);
    TINYPY_CLEAR_ERROR(out_error);
    if (TINYPY_VALUE_KIND(target) == TINYPY_VALUE_OUTPUT_STREAM) {
        tinypy_output_emit(vm, TINYPY_OUTPUT_STREAM_OBJECT(target)->channel, bytes, size);
        return TINYPY_TRUE;
    }
    if (TINYPY_VALUE_KIND(target) == TINYPY_VALUE_NONE) {
        tinypy_output_emit(vm, TINYPY_OUTPUT_STDOUT, bytes, size);
        return TINYPY_TRUE;
    }
    tinypy_value_t *write_method = tinypy_object_get_attr(target, "write", 5U, out_error);
    tinypy_value_t *text;
    tinypy_value_t *args;
    tinypy_value_t *result;

    if (write_method == NULL) {
        return TINYPY_FALSE;
    }
    text = tinypy_string_from_bytes(vm, bytes, size);
    args = tinypy_tuple_from_items(vm, &text, 1U);
    result = tinypy_call(write_method, args, NULL, out_error);
    TINYPY_DECREF(args);
    TINYPY_DECREF(text);
    TINYPY_DECREF(write_method);
    if (result == NULL) {
        return TINYPY_FALSE;
    }
    TINYPY_DECREF(result);
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_output_soft_space(tinypy_value_t *target) {
    tinypy_value_type_e kind;

    kind = TINYPY_VALUE_KIND(target);
    if (kind == TINYPY_VALUE_OUTPUT_STREAM) {
        tinypy_bool_t return_value_1 = TINYPY_OUTPUT_STREAM_OBJECT(target)->soft_space;
        return return_value_1;
    }
    if (kind == TINYPY_VALUE_NATIVE_INSTANCE) {
        tinypy_vm_t *vm = TINYPY_VALUE_VM(target);
        tinypy_value_t *key = tinypy_string_from_bytes(vm, "softspace", 9U);
        tinypy_error_t *error = NULL;
        tinypy_value_t *value;
        int32_t status = tinypy_internal_object_get_optional_attr_key(target, key, &value, &error);
        tinypy_bool_t soft_space = TINYPY_FALSE;

        TINYPY_DECREF(key);
        if (status > 0) {
            soft_space = tinypy_truth(value, &error);
            TINYPY_DECREF(value);
        }
        if (error != NULL) {
            tinypy_error_release(error);
            tinypy_vm_clear_error(vm);
        }
        return soft_space > 0 ? TINYPY_TRUE : TINYPY_FALSE;
    }
    if (kind == TINYPY_VALUE_INSTANCE && TINYPY_INSTANCE_OBJECT(target)->dict != NULL) {
        tinypy_vm_t *vm = TINYPY_VALUE_VM(target);
        tinypy_value_t *key = tinypy_string_from_bytes(vm, "softspace", 9U);
        tinypy_value_t *value = tinypy_dict_get_optional(TINYPY_INSTANCE_OBJECT(target)->dict, key);
        tinypy_bool_t soft_space = TINYPY_FALSE;

        if (value != NULL) {
            tinypy_value_type_e value_kind = TINYPY_VALUE_KIND(value);

            if (value_kind == TINYPY_VALUE_BOOL || value_kind == TINYPY_VALUE_INTEGER) {
                soft_space = TINYPY_INTEGER_VALUE(value) != 0 ? INT32_C(1) : INT32_C(0);
            }
        }
        TINYPY_DECREF(key);
        return soft_space;
    }
    return TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_output_set_soft_space(tinypy_value_t *target, tinypy_bool_t soft_space) {
    if (TINYPY_VALUE_KIND(target) == TINYPY_VALUE_OUTPUT_STREAM) {
        TINYPY_OUTPUT_STREAM_OBJECT(target)->soft_space = soft_space != 0 ? INT32_C(1) : INT32_C(0);
    }
    else if (TINYPY_VALUE_KIND(target) == TINYPY_VALUE_NATIVE_INSTANCE) {
        tinypy_vm_t *vm = TINYPY_VALUE_VM(target);
        tinypy_error_t *error = NULL;
        tinypy_value_t *value = tinypy_bool_from_i32(vm, soft_space);

        (void)tinypy_object_set_attr(target, "softspace", 9U, value, &error);
        TINYPY_DECREF(value);
        if (error != NULL) {
            tinypy_error_release(error);
            tinypy_vm_clear_error(vm);
        }
    }
    else if (TINYPY_VALUE_KIND(target) == TINYPY_VALUE_INSTANCE) {
        tinypy_vm_t *vm = TINYPY_VALUE_VM(target);
        tinypy_value_t *value = tinypy_bool_from_i32(vm, soft_space);

        tinypy_instance_set_attr(target, "softspace", 9U, value);
        TINYPY_DECREF(value);
    }
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
    tinypy_value_t *return_value_1 = tinypy_none_get(vm);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_output_flush_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_output_method_arguments(vm, args, kwargs, 1U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *return_value_1 = tinypy_none_get(vm);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_output_isatty_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_output_method_arguments(vm, args, kwargs, 1U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *return_value_1 = tinypy_bool_from_i32(vm, INT32_C(0));
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
    tinypy_value_t *return_value_1 = tinypy_none_get(vm);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_output_type_method(tinypy_vm_t *vm, const char *name, size_t name_size, tinypy_native_function_callback_t callback) {
    tinypy_value_t *key = tinypy_string_from_bytes(vm, name, name_size);
    tinypy_value_t *function = tinypy_native_function_new(vm, name, name_size, callback, NULL, NULL);

    tinypy_dict_set(vm->types[TINYPY_VALUE_OUTPUT_STREAM].dict, key, function);
    TINYPY_DECREF(function);
    TINYPY_DECREF(key);
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_initialize_output_type(tinypy_vm_t *vm) {
    __tinypy_output_type_method(vm, "write", 5U, __tinypy_output_write_method);
    __tinypy_output_type_method(vm, "writelines", 10U, __tinypy_output_writelines_method);
    __tinypy_output_type_method(vm, "flush", 5U, __tinypy_output_flush_method);
    __tinypy_output_type_method(vm, "isatty", 6U, __tinypy_output_isatty_method);
}
