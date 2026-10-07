#include "tinypy/function.h"

#include "internal.h"

#include <string.h>

//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_function_new(tinypy_value_t *code, tinypy_value_t *globals, tinypy_value_t *defaults, tinypy_value_t *closure) {
    tinypy_value_t *doc;

    tinypy_vm_t *vm = TINYPY_VALUE_VM(code);

    tinypy_function_object_t *function = (tinypy_function_object_t *)tinypy_internal_value_allocate(vm, TINYPY_VALUE_FUNCTION, sizeof(*function));
    function->code = code;
    function->globals = globals;
    function->defaults = defaults;
    function->closure = closure;
    function->name = TINYPY_CODE_NAME(code);
    tinypy_value_t *consts = TINYPY_CODE_CONSTS(code);
    tinypy_bool_t condition = TINYPY_TUPLE_SIZE(consts) != 0U;
    if (condition != 0) {
        tinypy_value_t *item = TINYPY_TUPLE_GET(consts, 0U);
        tinypy_bool_t condition_2 = TINYPY_VALUE_KIND(item) == TINYPY_VALUE_STRING;
        if (condition_2 == 0) {
            tinypy_value_t *item_2 = TINYPY_TUPLE_GET(consts, 0U);
            condition_2 = TINYPY_VALUE_KIND(item_2) == TINYPY_VALUE_UNICODE;
        }
        condition = (condition_2);
    }
    if (condition) {
        function->doc = TINYPY_RET(TINYPY_TUPLE_GET(consts, 0U));
    }
    else {
        doc = TINYPY_RET_NONE(vm);
        function->doc = doc;
    }
    function->module = tinypy_internal_dict_get_optional_suppressed(vm, globals, vm->internal_special_name_key);
    if (function->module != NULL) {
        TINYPY_INCREF(function->module);
    }

    TINYPY_INCREF(code);
    TINYPY_INCREF(globals);
    if (defaults != NULL) {
        TINYPY_INCREF(defaults);
    }
    if (closure != NULL) {
        TINYPY_INCREF(closure);
    }
    TINYPY_INCREF(function->name);
    return &function->base;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_function_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data) {
    tinypy_function_object_t *function = TINYPY_FUNCTION_OBJECT(value);

    visit(function->code, user_data);
    visit(function->globals, user_data);
    if (function->defaults != NULL) {
        visit(function->defaults, user_data);
    }
    if (function->closure != NULL) {
        visit(function->closure, user_data);
    }
    visit(function->doc, user_data);
    visit(function->name, user_data);
    if (function->dict != NULL) {
        visit(function->dict, user_data);
    }
    if (function->module != NULL) {
        visit(function->module, user_data);
    }
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_function_call(tinypy_value_t *callable, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_value_t *return_value_1 = tinypy_internal_eval_function(callable, args, kwargs, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_call(tinypy_value_t *callable, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(callable);
    TINYPY_CLEAR_ERROR(out_error);
    if (TINYPY_VALUE_KIND(callable) == TINYPY_VALUE_OLD_INSTANCE) {
        tinypy_value_t *method = NULL;
        int32_t found = tinypy_internal_object_get_optional_attr_key(callable, vm->internal_special_call_key, &method, out_error);

        if (found < 0) {
            return NULL;
        }
        if (found != 0) {
            tinypy_value_t *result = tinypy_call(method, args, kwargs, out_error);

            TINYPY_DECREF(method);
            return result;
        }
        tinypy_value_t *name_value = tinypy_class_name(tinypy_old_instance_class(callable));
        const char *name = (const char *)TINYPY_TEXT_BYTES(name_value);
        size_t name_size = TINYPY_TEXT_BYTE_SIZE(name_value);
        size_t limit = name_size < 200U ? name_size : 200U;
        const char *terminator = (const char *)memchr(name, '\0', limit);
        tinypy_message_part_t parts[] = {
            {name, terminator != NULL ? (size_t)(terminator - name) : limit},
            TINYPY_MESSAGE_PART_LITERAL(" instance has no __call__ method")
        };

        tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_ATTRIBUTE, parts, 2U, out_error);
        return NULL;
    }
    if ((callable->type->flags & TINYPY_TYPE_FLAG_HEAP) != 0U && tinypy_internal_object_has_special_override_key(callable, vm->internal_special_call_key) != 0) {
        tinypy_value_t *method = tinypy_internal_object_get_special_key(callable, vm->internal_special_call_key, out_error);
        tinypy_value_t *result;

        if (method == NULL) {
            return NULL;
        }
        result = tinypy_call(method, args, kwargs, out_error);
        TINYPY_DECREF(method);
        return result;
    }
    if (callable->type->call != NULL) {
        tinypy_value_t *return_value_1 = callable->type->call(callable, args, kwargs, out_error);
        return return_value_1;
    }
    if (tinypy_internal_object_has_special_key(callable, vm->internal_special_call_key) != 0) {
        tinypy_value_t *method = tinypy_internal_object_get_special_key(callable, vm->internal_special_call_key, out_error);
        tinypy_value_t *result;

        if (method == NULL) {
            return NULL;
        }
        result = tinypy_call(method, args, kwargs, out_error);
        TINYPY_DECREF(method);
        return result;
    }
    if (vm->raised_value != NULL) {
        if (out_error != NULL && *out_error == NULL) {
            tinypy_internal_exception_make_diagnostic(vm, out_error);
        }
        return NULL;
    }
    size_t name_size = callable->type->name_size < 200U ? callable->type->name_size : 200U;
    const char *terminator = (const char *)memchr(callable->type->name, '\0', name_size);
    tinypy_message_part_t parts[] = {
        TINYPY_MESSAGE_PART_LITERAL("'"),
        {callable->type->name, terminator != NULL ? (size_t)(terminator - callable->type->name) : name_size},
        TINYPY_MESSAGE_PART_LITERAL("' object is not callable")
    };

    tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, 3U, out_error);
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_call(tinypy_value_t *callable, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(callable);

    /* Python functions are guarded by the evaluator. Native callbacks and
       descriptor dispatch also consume C stack, even without a Python frame. */
    if (TINYPY_VALUE_KIND(callable) == TINYPY_VALUE_FUNCTION) {
        tinypy_value_t *result = __tinypy_call(callable, args, kwargs, out_error);
        return result;
    }
    if (tinypy_internal_recursion_check(vm, TINYPY_NATIVE_STACK_ADDRESS(), "maximum recursion depth exceeded while calling a Python object", out_error) == 0) {
        return NULL;
    }
    vm->evaluation_depth += 1U;
    tinypy_value_t *result = __tinypy_call(callable, args, kwargs, out_error);
    vm->evaluation_depth -= 1U;
    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_function_code(const tinypy_value_t *function) {
    tinypy_value_t *return_value_1 = TINYPY_FUNCTION_OBJECT((tinypy_value_t *)function)->code;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_function_globals(const tinypy_value_t *function) {
    tinypy_value_t *return_value_1 = TINYPY_FUNCTION_OBJECT((tinypy_value_t *)function)->globals;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_function_defaults(const tinypy_value_t *function) {
    tinypy_value_t *return_value_1 = TINYPY_FUNCTION_OBJECT((tinypy_value_t *)function)->defaults;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_function_closure(const tinypy_value_t *function) {
    tinypy_value_t *return_value_1 = TINYPY_FUNCTION_OBJECT((tinypy_value_t *)function)->closure;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_function_name(const tinypy_value_t *function) {
    tinypy_value_t *return_value_1 = TINYPY_FUNCTION_OBJECT((tinypy_value_t *)function)->name;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_function_doc(const tinypy_value_t *function) {
    tinypy_value_t *return_value_1 = TINYPY_FUNCTION_OBJECT((tinypy_value_t *)function)->doc;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_function_keyword_index(const tinypy_value_t *key) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(key);
    tinypy_value_t *const names[] = {vm->internal_code_key, vm->internal_globals_key, vm->internal_name_key, vm->internal_argdefs_key, vm->internal_closure_key};
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(key);
    size_t index;

    if (kind != TINYPY_VALUE_STRING && kind != TINYPY_VALUE_UNICODE) {
        return -INT32_C(1);
    }
    const uint8_t *bytes = TINYPY_TEXT_BYTES(key);
    size_t size = TINYPY_TEXT_BYTE_SIZE(key);
    const uint8_t *terminator = (const uint8_t *)memchr(bytes, '\0', size);

    if (terminator != NULL) {
        size = (size_t)(terminator - bytes);
    }
    for (index = 0U; index < sizeof(names) / sizeof(names[0]); ++index) {
        if (size == TINYPY_TEXT_BYTE_SIZE(names[index]) && memcmp(bytes, TINYPY_TEXT_BYTES(names[index]), size) == 0) {
            return (int32_t)index;
        }
    }
    return -INT32_C(1);
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_function_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = type->vm;
    size_t count = TINYPY_TUPLE_SIZE(args);
    size_t keywords = kwargs != NULL ? TINYPY_DICT_SIZE(kwargs) : 0U;
    size_t recognized = 0U;
    tinypy_value_t *const names[] = {vm->internal_code_key, vm->internal_globals_key, vm->internal_name_key, vm->internal_argdefs_key, vm->internal_closure_key};
    static const char *positions[] = {"1", "2", "3", "4", "5"};
    tinypy_value_t *arguments[5] = {NULL, NULL, NULL, NULL, NULL};
    tinypy_value_t *code;
    tinypy_value_t *globals;
    tinypy_value_t *name;
    tinypy_value_t *defaults = NULL;
    tinypy_value_t *closure = NULL;
    tinypy_value_t *result = NULL;
    size_t index;

    if (type != &vm->types[TINYPY_VALUE_FUNCTION]) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "function() received invalid arguments", out_error);
        return NULL;
    }
    if (count > 5U || keywords > 5U - count) {
        tinypy_internal_make_arity_error(vm, "function", 8U, count + keywords, 0U, 5U, TINYPY_ARITY_STYLE_PARSED, out_error);
        return NULL;
    }
    for (index = 0U; index < 5U; ++index) {
        tinypy_value_t *keyword = recognized < keywords ? tinypy_internal_constructor_keyword_optional(kwargs, names[index]) : NULL;
        tinypy_value_t *argument = index < count ? TINYPY_TUPLE_GET(args, index) : NULL;

        if (keyword != NULL) {
            recognized += 1U;
            if (argument != NULL) {
                tinypy_message_part_t parts[] = {
                    TINYPY_MESSAGE_PART_LITERAL("Argument given by name ('"), TINYPY_MESSAGE_PART_TEXT(names[index]),
                    TINYPY_MESSAGE_PART_LITERAL("') and position ("), {positions[index], 1U}, TINYPY_MESSAGE_PART_LITERAL(")")
                };

                tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, 5U, out_error);
                goto cleanup;
            }
            argument = keyword;
        }
        if (argument == NULL && index < 2U) {
            tinypy_message_part_t parts[] = {
                TINYPY_MESSAGE_PART_LITERAL("Required argument '"), TINYPY_MESSAGE_PART_TEXT(names[index]),
                TINYPY_MESSAGE_PART_LITERAL("' (pos "), {positions[index], 1U}, TINYPY_MESSAGE_PART_LITERAL(") not found")
            };

            tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, 5U, out_error);
            goto cleanup;
        }
        if (argument != NULL) {
            arguments[index] = TINYPY_RET(argument);
            if (index < 2U && TINYPY_VALUE_KIND(argument) != (index == 0U ? TINYPY_VALUE_CODE : TINYPY_VALUE_DICT)) {
                size_t type_size = argument->type->name_size < 50U ? argument->type->name_size : 50U;
                tinypy_message_part_t parts[] = {
                    TINYPY_MESSAGE_PART_LITERAL("function() argument "), {positions[index], 1U},
                    {index == 0U ? " must be code, not " : " must be dict, not ", 19U},
                    {TINYPY_VALUE_KIND(argument) == TINYPY_VALUE_NONE ? "None" : argument->type->name, TINYPY_VALUE_KIND(argument) == TINYPY_VALUE_NONE ? 4U : type_size}
                };

                tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, 4U, out_error);
                goto cleanup;
            }
        }
    }
    if (recognized != keywords) {
        tinypy_dict_entry_t *iterator = TINYPY_DICT_ITERATOR_BEGIN(kwargs);
        tinypy_dict_entry_t *end = TINYPY_DICT_ITERATOR_END(kwargs);

        for (; iterator != end; ++iterator) {
            int32_t keyword_index;

            if (TINYPY_DICT_ENTRY_IS_ACTIVE(iterator) == 0) {
                continue;
            }
            keyword_index = __tinypy_function_keyword_index(iterator->key);
            if (keyword_index < 0) {
                const uint8_t *bytes = TINYPY_TEXT_BYTES(iterator->key);
                size_t size = TINYPY_TEXT_BYTE_SIZE(iterator->key);
                const uint8_t *terminator = (const uint8_t *)memchr(bytes, '\0', size);

                if (terminator != NULL) {
                    size = (size_t)(terminator - bytes);
                }
                tinypy_message_part_t parts[] = {
                    TINYPY_MESSAGE_PART_LITERAL("'"), {(const char *)bytes, size},
                    TINYPY_MESSAGE_PART_LITERAL("' is an invalid keyword argument for this function")
                };

                tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, 3U, out_error);
                goto cleanup;
            }
        }
    }
    code = arguments[0];
    globals = arguments[1];
    name = arguments[2] != NULL && TINYPY_VALUE_KIND(arguments[2]) != TINYPY_VALUE_NONE ? arguments[2] : TINYPY_CODE_NAME(code);
    if (TINYPY_VALUE_KIND(name) != TINYPY_VALUE_STRING) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "arg 3 (name) must be None or string", out_error);
        goto cleanup;
    }
    if (arguments[3] != NULL && TINYPY_VALUE_KIND(arguments[3]) != TINYPY_VALUE_NONE) {
        defaults = arguments[3];
        if (TINYPY_VALUE_KIND(defaults) != TINYPY_VALUE_TUPLE) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "arg 4 (defaults) must be None or tuple", out_error);
            goto cleanup;
        }
    }
    if (arguments[4] != NULL && TINYPY_VALUE_KIND(arguments[4]) != TINYPY_VALUE_NONE) {
        closure = arguments[4];
        if (TINYPY_VALUE_KIND(closure) != TINYPY_VALUE_TUPLE) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "arg 5 (closure) must be None or tuple", out_error);
            goto cleanup;
        }
    }
    size_t freevar_count = TINYPY_TUPLE_SIZE(TINYPY_CODE_FREEVARS(code));
    size_t closure_count = closure != NULL ? TINYPY_TUPLE_SIZE(closure) : 0U;
    if (freevar_count != 0U && closure == NULL) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "arg 5 (closure) must be tuple", out_error);
        goto cleanup;
    }
    if (freevar_count != closure_count) {
        char expected_text[TINYPY_MESSAGE_SIZE_BUFFER];
        char actual_text[TINYPY_MESSAGE_SIZE_BUFFER];
        size_t expected_size = tinypy_internal_format_size(expected_text, freevar_count);
        size_t actual_size = tinypy_internal_format_size(actual_text, closure_count);
        const uint8_t *name_bytes = TINYPY_TEXT_BYTES(TINYPY_CODE_NAME(code));
        size_t name_size = TINYPY_TEXT_BYTE_SIZE(TINYPY_CODE_NAME(code));
        const uint8_t *null_byte = (const uint8_t *)memchr(name_bytes, 0, name_size);
        tinypy_message_part_t parts[] = {
            {(const char *)name_bytes, null_byte != NULL ? (size_t)(null_byte - name_bytes) : name_size},
            TINYPY_MESSAGE_PART_LITERAL(" requires closure of length "), {expected_text, expected_size},
            TINYPY_MESSAGE_PART_LITERAL(", not "), {actual_text, actual_size}
        };

        tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_VALUE, parts, 5U, out_error);
        goto cleanup;
    }
    if (closure != NULL) {
        tinypy_value_t *const *item = TINYPY_TUPLE_ITERATOR_BEGIN(closure);
        tinypy_value_t *const *end = TINYPY_TUPLE_ITERATOR_END(closure);

        for (; item != end; ++item) {
            if (TINYPY_VALUE_KIND(*item) != TINYPY_VALUE_CELL) {
                size_t type_size = (*item)->type->name_size < 100U ? (*item)->type->name_size : 100U;
                tinypy_message_part_t parts[] = {
                    TINYPY_MESSAGE_PART_LITERAL("arg 5 (closure) expected cell, found "), {(*item)->type->name, type_size}
                };

                tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, 2U, out_error);
                goto cleanup;
            }
        }
    }
    result = tinypy_function_new(code, globals, defaults, closure);
    tinypy_function_object_t *function = TINYPY_FUNCTION_OBJECT(result);

    if (name != function->name) {
        TINYPY_INCREF(name);
        TINYPY_DECREF(function->name);
        function->name = name;
    }
cleanup:
    for (index = 0U; index < 5U; ++index) {
        if (arguments[index] != NULL) {
            TINYPY_DECREF(arguments[index]);
        }
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_function_call_method(tinypy_value_t *native_function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(native_function);
    size_t count = TINYPY_TUPLE_SIZE(args);
    tinypy_value_t *function;
    tinypy_value_t *const *items;

    (void)user_data;
    if (count == 0U || TINYPY_VALUE_KIND(TINYPY_TUPLE_GET(args, 0U)) != TINYPY_VALUE_FUNCTION) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "function.__call__ requires a function", out_error);
        return NULL;
    }
    function = TINYPY_TUPLE_GET(args, 0U);
    items = tinypy_internal_tuple_items(args);
    tinypy_value_t *return_value_1 = tinypy_internal_eval_function_items(function, items + 1U, count - 1U, kwargs, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_initialize_function_type(tinypy_vm_t *vm) {
    tinypy_type_t *type = &vm->types[TINYPY_VALUE_FUNCTION];
    tinypy_internal_type_add_method(type, vm->internal_special_call_key, __tinypy_function_call_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);

    type->create = tinypy_internal_function_create;
    tinypy_internal_constructor_add_builtin_new(type);
}
