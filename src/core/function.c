#include "tinypy/function.h"

#include "internal.h"

#include <assert.h>

//////////////////////////////////////////////////////////////////////////
static tinypy_function_object_t *__tinypy_internal_function_validate(const tinypy_value_t *value) {
    assert(value != NULL);
    assert(tinypy_internal_vm_valid(tinypy_internal_value_vm(value)));
    assert(tinypy_internal_value_kind(value) == TINYPY_VALUE_FUNCTION);
    return TINYPY_FUNCTION_OBJECT((tinypy_value_t *)value);
}

//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_function_new(tinypy_value_t *code, tinypy_value_t *globals, tinypy_value_t *defaults, tinypy_value_t *closure) {
    tinypy_vm_t *vm;
    tinypy_function_object_t *function;
    tinypy_value_t *consts;
    tinypy_value_t *doc;
    tinypy_value_t *module_name;

    assert(code != NULL);
    vm = tinypy_internal_value_vm(code);
    assert(tinypy_internal_vm_valid(vm));
    assert(tinypy_internal_value_kind(code) == TINYPY_VALUE_CODE);
    assert(globals != NULL);
    assert(tinypy_internal_value_belongs_to(vm, globals));
    assert(tinypy_internal_value_kind(globals) == TINYPY_VALUE_DICT);
    assert(defaults == NULL || tinypy_internal_value_belongs_to(vm, defaults));
    assert(defaults == NULL || tinypy_internal_value_kind(defaults) == TINYPY_VALUE_TUPLE);
    assert(closure == NULL || tinypy_internal_value_belongs_to(vm, closure));
    assert(closure == NULL || tinypy_internal_value_kind(closure) == TINYPY_VALUE_TUPLE);
    assert(closure == NULL || tinypy_tuple_size(closure) == tinypy_tuple_size(tinypy_code_freevars(code)));
    if (closure != NULL) {
        size_t closure_index;

        for (closure_index = 0U; closure_index < tinypy_tuple_size(closure); closure_index += 1U) {
            assert(tinypy_internal_value_kind(tinypy_tuple_get(closure, closure_index)) == TINYPY_VALUE_CELL);
        }
    }

    function = (tinypy_function_object_t *)tinypy_internal_value_allocate(vm, TINYPY_VALUE_FUNCTION, sizeof(*function));
    function->code = code;
    function->globals = globals;
    function->defaults = defaults;
    function->closure = closure;
    function->name = tinypy_code_name(code);
    consts = tinypy_code_consts(code);
    int condition = tinypy_tuple_size(consts) != 0U;
    if (condition != 0) {
        tinypy_value_t *item = tinypy_tuple_get(consts, 0U);
        int condition_2 = tinypy_internal_value_kind(item) == TINYPY_VALUE_STRING;
        if (condition_2 == 0) {
            tinypy_value_t *item_2 = tinypy_tuple_get(consts, 0U);
            condition_2 = tinypy_internal_value_kind(item_2) == TINYPY_VALUE_UNICODE;
        }
        condition = (condition_2);
    }
    if (condition) {
        function->doc = tinypy_tuple_get(consts, 0U);
        tinypy_retain(function->doc);
    }
    else {
        doc = tinypy_none_get(vm);
        function->doc = doc;
    }
    module_name = tinypy_string_from_bytes(vm, "__name__", 8U);
    if (tinypy_dict_contains(globals, module_name) != 0) {
        function->module = tinypy_dict_get(globals, module_name);
        tinypy_retain(function->module);
    }
    tinypy_release(module_name);

    tinypy_retain(code);
    tinypy_retain(globals);
    if (defaults != NULL) {
        tinypy_retain(defaults);
    }
    if (closure != NULL) {
        tinypy_retain(closure);
    }
    tinypy_retain(function->name);
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
    vm = tinypy_internal_value_vm(callable);
    assert(tinypy_internal_vm_valid(vm));
    assert(args != NULL);
    assert(tinypy_internal_value_belongs_to(vm, args));
    assert(tinypy_internal_value_kind(args) == TINYPY_VALUE_TUPLE);
    assert(kwargs == NULL || tinypy_internal_value_belongs_to(vm, kwargs));
    assert(kwargs == NULL || tinypy_internal_value_kind(kwargs) == TINYPY_VALUE_DICT);
    tinypy_internal_clear_error(out_error);
    if (callable->type->call == NULL) {
        if (tinypy_internal_object_has_special(callable, "__call__", 8U) != 0) {
            tinypy_value_t *method = tinypy_object_get_attr(callable, "__call__", 8U, out_error);
            tinypy_value_t *result;

            if (method == NULL) {
                return NULL;
            }
            result = tinypy_call(method, args, kwargs, out_error);
            tinypy_release(method);
            return result;
        }
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "object is not callable", out_error);
        return NULL;
    }
    return callable->type->call(callable, args, kwargs, out_error);
}

//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_function_code(const tinypy_value_t *function) {
    return __tinypy_internal_function_validate(function)->code;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_function_globals(const tinypy_value_t *function) {
    return __tinypy_internal_function_validate(function)->globals;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_function_defaults(const tinypy_value_t *function) {
    return __tinypy_internal_function_validate(function)->defaults;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_function_closure(const tinypy_value_t *function) {
    return __tinypy_internal_function_validate(function)->closure;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_function_name(const tinypy_value_t *function) {
    return __tinypy_internal_function_validate(function)->name;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_function_doc(const tinypy_value_t *function) {
    return __tinypy_internal_function_validate(function)->doc;
}
