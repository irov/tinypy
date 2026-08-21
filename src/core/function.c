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
        function->doc = TINYPY_TUPLE_GET(consts, 0U);
        TINYPY_INCREF(function->doc);
    }
    else {
        doc = tinypy_none_get(vm);
        function->doc = doc;
    }
    function->module = tinypy_dict_get_optional(globals, vm->special_name_key);
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
tinypy_value_t *tinypy_call(tinypy_value_t *callable, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(callable);
    TINYPY_CLEAR_ERROR(out_error);
    if ((callable->type->flags & TINYPY_TYPE_FLAG_HEAP) != 0U && tinypy_internal_object_has_special_override(callable, "__call__", 8U) != 0) {
        tinypy_value_t *method = tinypy_internal_object_get_special(callable, "__call__", 8U, out_error);
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
    if (tinypy_internal_object_has_special(callable, "__call__", 8U) != 0) {
        tinypy_value_t *method = tinypy_internal_object_get_special(callable, "__call__", 8U, out_error);
        tinypy_value_t *result;

        if (method == NULL) {
            return NULL;
        }
        result = tinypy_call(method, args, kwargs, out_error);
        TINYPY_DECREF(method);
        return result;
    }
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "object is not callable", out_error);
    return NULL;
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
    static const char *const names[] = {"code", "globals", "name", "argdefs", "closure"};
    static const size_t sizes[] = {4U, 7U, 4U, 7U, 7U};
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(key);
    size_t key_size;
    const uint8_t *key_bytes;
    size_t index;

    if (kind != TINYPY_VALUE_STRING && kind != TINYPY_VALUE_UNICODE) {
        return -INT32_C(1);
    }
    key_size = TINYPY_TEXT_BYTE_SIZE(key);
    key_bytes = TINYPY_TEXT_BYTES(key);
    for (index = 0U; index < sizeof(names) / sizeof(names[0]); ++index) {
        if (key_size == sizes[index] && memcmp(key_bytes, names[index], key_size) == 0) {
            return (int32_t)index;
        }
    }
    return -INT32_C(1);
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_function_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = type->vm;
    size_t count = TINYPY_TUPLE_SIZE(args);
    tinypy_value_t *arguments[5] = {NULL, NULL, NULL, NULL, NULL};
    tinypy_bool_t provided[5] = {TINYPY_FALSE, TINYPY_FALSE, TINYPY_FALSE, TINYPY_FALSE, TINYPY_FALSE};
    tinypy_value_t *code;
    tinypy_value_t *globals;
    tinypy_value_t *name;
    tinypy_value_t *defaults = NULL;
    tinypy_value_t *closure = NULL;
    size_t index;

    if (type != &vm->types[TINYPY_VALUE_FUNCTION] || count > 5U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "function() received invalid arguments", out_error);
        return NULL;
    }
    for (index = 0U; index < count; ++index) {
        arguments[index] = TINYPY_TUPLE_GET(args, index);
        provided[index] = TINYPY_TRUE;
    }
    if (kwargs != NULL) {
        tinypy_dict_entry_t *iterator = TINYPY_DICT_ITERATOR_BEGIN(kwargs);
        tinypy_dict_entry_t *end = TINYPY_DICT_ITERATOR_END(kwargs);

        for (; iterator != end; ++iterator) {
            int32_t keyword_index;

            if (TINYPY_DICT_ENTRY_IS_ACTIVE(iterator) == 0) {
                continue;
            }
            keyword_index = __tinypy_function_keyword_index(iterator->key);
            if (keyword_index < 0) {
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "function() received an unexpected keyword argument", out_error);
                return NULL;
            }
            index = (size_t)keyword_index;
            if (provided[index] != 0) {
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "function() received multiple values for an argument", out_error);
                return NULL;
            }
            arguments[index] = iterator->value;
            provided[index] = TINYPY_TRUE;
        }
    }
    if (provided[0] == 0 || provided[1] == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "function() requires code and globals arguments", out_error);
        return NULL;
    }
    code = arguments[0];
    globals = arguments[1];
    if (TINYPY_VALUE_KIND(code) != TINYPY_VALUE_CODE || TINYPY_VALUE_KIND(globals) != TINYPY_VALUE_DICT) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "function() received an invalid code or globals", out_error);
        return NULL;
    }
    name = provided[2] != 0 && TINYPY_VALUE_KIND(arguments[2]) != TINYPY_VALUE_NONE ? arguments[2] : TINYPY_CODE_NAME(code);
    if (TINYPY_VALUE_KIND(name) != TINYPY_VALUE_STRING) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "function name must be a string", out_error);
        return NULL;
    }
    if (provided[3] != 0 && TINYPY_VALUE_KIND(arguments[3]) != TINYPY_VALUE_NONE) {
        defaults = arguments[3];
        if (TINYPY_VALUE_KIND(defaults) != TINYPY_VALUE_TUPLE) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "function defaults must be a tuple", out_error);
            return NULL;
        }
    }
    if (provided[4] != 0 && TINYPY_VALUE_KIND(arguments[4]) != TINYPY_VALUE_NONE) {
        closure = arguments[4];
        if (TINYPY_VALUE_KIND(closure) != TINYPY_VALUE_TUPLE) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "function closure must be a tuple", out_error);
            return NULL;
        }
    }
    size_t freevar_count = TINYPY_TUPLE_SIZE(TINYPY_CODE_FREEVARS(code));
    size_t closure_count = closure != NULL ? TINYPY_TUPLE_SIZE(closure) : 0U;
    if (freevar_count != closure_count) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "function closure has the wrong size", out_error);
        return NULL;
    }
    if (closure != NULL) {
        tinypy_value_t *const *item = TINYPY_TUPLE_ITERATOR_BEGIN(closure);
        tinypy_value_t *const *end = TINYPY_TUPLE_ITERATOR_END(closure);

        for (; item != end; ++item) {
            if (TINYPY_VALUE_KIND(*item) != TINYPY_VALUE_CELL) {
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "function closure contains a non-cell", out_error);
                return NULL;
            }
        }
    }
    tinypy_value_t *result = tinypy_function_new(code, globals, defaults, closure);
    tinypy_function_object_t *function = TINYPY_FUNCTION_OBJECT(result);

    if (name != function->name) {
        TINYPY_INCREF(name);
        TINYPY_DECREF(function->name);
        function->name = name;
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
    tinypy_value_t *call = tinypy_native_function_new(vm, "__call__", 8U, __tinypy_function_call_method, NULL, NULL);

    type->create = tinypy_internal_function_create;
    tinypy_internal_constructor_add_builtin_new(type);
    tinypy_type_set_attr(type, "__call__", 8U, call);
    TINYPY_DECREF(call);
}
