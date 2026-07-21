#include "tinypy/function.h"

#include "internal.h"

#include <assert.h>

//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_function_new(tinypy_value_t *code, tinypy_value_t *globals, tinypy_value_t *defaults, tinypy_value_t *closure) {
    tinypy_vm_t *vm;
    tinypy_function_object_t *function;
    tinypy_value_t *consts;
    tinypy_value_t *doc;
    tinypy_value_t *module_name;

    assert(code != NULL);
    vm = TINYPY_VALUE_VM(code);
    assert(tinypy_internal_vm_valid(vm));
    assert(TINYPY_VALUE_KIND(code) == TINYPY_VALUE_CODE);
    assert(globals != NULL);
    assert(tinypy_internal_value_belongs_to(vm, globals));
    assert(TINYPY_VALUE_KIND(globals) == TINYPY_VALUE_DICT);
    assert(defaults == NULL || tinypy_internal_value_belongs_to(vm, defaults));
    assert(defaults == NULL || TINYPY_VALUE_KIND(defaults) == TINYPY_VALUE_TUPLE);
    assert(closure == NULL || tinypy_internal_value_belongs_to(vm, closure));
    assert(closure == NULL || TINYPY_VALUE_KIND(closure) == TINYPY_VALUE_TUPLE);
    assert(closure == NULL || TINYPY_TUPLE_SIZE(closure) == TINYPY_TUPLE_SIZE(TINYPY_CODE_FREEVARS(code)));
    assert(closure == NULL || __tinypy_internal_values_have_kind(TINYPY_TUPLE_ITERATOR_BEGIN(closure), TINYPY_TUPLE_SIZE(closure), TINYPY_VALUE_CELL));

    function = (tinypy_function_object_t *)tinypy_internal_value_allocate(vm, TINYPY_VALUE_FUNCTION, sizeof(*function));
    function->code = code;
    function->globals = globals;
    function->defaults = defaults;
    function->closure = closure;
    function->name = TINYPY_CODE_NAME(code);
    consts = TINYPY_CODE_CONSTS(code);
    int condition = TINYPY_TUPLE_SIZE(consts) != 0U;
    if (condition != 0) {
        tinypy_value_t *item = TINYPY_TUPLE_GET(consts, 0U);
        int condition_2 = TINYPY_VALUE_KIND(item) == TINYPY_VALUE_STRING;
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
    module_name = tinypy_string_from_bytes(vm, "__name__", 8U);
    if (tinypy_dict_contains(globals, module_name) != 0) {
        function->module = tinypy_dict_get(globals, module_name);
        TINYPY_INCREF(function->module);
    }
    TINYPY_DECREF(module_name);

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
    return tinypy_internal_eval_function(callable, args, kwargs, out_error);
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_call(tinypy_value_t *callable, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm;

    assert(callable != NULL);
    vm = TINYPY_VALUE_VM(callable);
    assert(tinypy_internal_vm_valid(vm));
    assert(args != NULL);
    assert(tinypy_internal_value_belongs_to(vm, args));
    assert(TINYPY_VALUE_KIND(args) == TINYPY_VALUE_TUPLE);
    assert(kwargs == NULL || tinypy_internal_value_belongs_to(vm, kwargs));
    assert(kwargs == NULL || TINYPY_VALUE_KIND(kwargs) == TINYPY_VALUE_DICT);
    TINYPY_CLEAR_ERROR(out_error);
    if (callable->type->call == NULL) {
        if (tinypy_internal_object_has_special(callable, "__call__", 8U) != 0) {
            tinypy_value_t *method = tinypy_object_get_attr(callable, "__call__", 8U, out_error);
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
    return callable->type->call(callable, args, kwargs, out_error);
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_function_code(const tinypy_value_t *function) {
    assert(function != NULL);
    assert(tinypy_internal_vm_valid(TINYPY_VALUE_VM(function)));
    assert(TINYPY_VALUE_KIND(function) == TINYPY_VALUE_FUNCTION);
    return TINYPY_FUNCTION_OBJECT((tinypy_value_t *)function)->code;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_function_globals(const tinypy_value_t *function) {
    assert(function != NULL);
    assert(tinypy_internal_vm_valid(TINYPY_VALUE_VM(function)));
    assert(TINYPY_VALUE_KIND(function) == TINYPY_VALUE_FUNCTION);
    return TINYPY_FUNCTION_OBJECT((tinypy_value_t *)function)->globals;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_function_defaults(const tinypy_value_t *function) {
    assert(function != NULL);
    assert(tinypy_internal_vm_valid(TINYPY_VALUE_VM(function)));
    assert(TINYPY_VALUE_KIND(function) == TINYPY_VALUE_FUNCTION);
    return TINYPY_FUNCTION_OBJECT((tinypy_value_t *)function)->defaults;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_function_closure(const tinypy_value_t *function) {
    assert(function != NULL);
    assert(tinypy_internal_vm_valid(TINYPY_VALUE_VM(function)));
    assert(TINYPY_VALUE_KIND(function) == TINYPY_VALUE_FUNCTION);
    return TINYPY_FUNCTION_OBJECT((tinypy_value_t *)function)->closure;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_function_name(const tinypy_value_t *function) {
    assert(function != NULL);
    assert(tinypy_internal_vm_valid(TINYPY_VALUE_VM(function)));
    assert(TINYPY_VALUE_KIND(function) == TINYPY_VALUE_FUNCTION);
    return TINYPY_FUNCTION_OBJECT((tinypy_value_t *)function)->name;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_function_doc(const tinypy_value_t *function) {
    assert(function != NULL);
    assert(tinypy_internal_vm_valid(TINYPY_VALUE_VM(function)));
    assert(TINYPY_VALUE_KIND(function) == TINYPY_VALUE_FUNCTION);
    return TINYPY_FUNCTION_OBJECT((tinypy_value_t *)function)->doc;
}
