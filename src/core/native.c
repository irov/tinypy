#include "tinypy/native.h"

#include "internal.h"

#include <assert.h>

static tinypy_native_function_object_t *__tinypy_internal_native_function_validate(const tinypy_value_t *value)
{
    assert(value != NULL);
    assert(tinypy_internal_vm_valid(tinypy_internal_value_vm(value)));
    assert(tinypy_internal_value_kind(value) == TINYPY_VALUE_NATIVE_FUNCTION);
    return TINYPY_NATIVE_FUNCTION_OBJECT((tinypy_value_t *)value);
}

tinypy_value_t *tinypy_native_function_new(tinypy_vm_t *vm, const char *name, size_t name_size, tinypy_native_function_callback_t callback, void *user_data, tinypy_native_function_finalize_t finalize)
{
    tinypy_native_function_object_t *function;

    assert(tinypy_internal_vm_valid(vm));
    assert(name != NULL || name_size == 0U);
    assert(callback != NULL);
    function = (tinypy_native_function_object_t *)tinypy_internal_value_allocate(vm, TINYPY_VALUE_NATIVE_FUNCTION, sizeof(*function));
    function->name = tinypy_string_from_bytes(vm, name, name_size);
    function->callback = callback;
    function->user_data = user_data;
    function->finalize = finalize;
    return &function->base;
}

void tinypy_internal_native_function_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data)
{
    visit(TINYPY_NATIVE_FUNCTION_OBJECT(value)->name, user_data);
}

void tinypy_internal_native_function_destroy(tinypy_vm_t *vm, tinypy_value_t *value)
{
    tinypy_native_function_object_t *function = TINYPY_NATIVE_FUNCTION_OBJECT(value);

    (void)vm;
    if (function->finalize != NULL) {
        tinypy_native_function_finalize_t finalize = function->finalize;
        void *user_data = function->user_data;

        function->finalize = NULL;
        function->user_data = NULL;
        finalize(user_data);
    }
}

tinypy_value_t *tinypy_internal_native_function_call(tinypy_value_t *callable, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error)
{
    tinypy_native_function_object_t *function = TINYPY_NATIVE_FUNCTION_OBJECT(callable);
    tinypy_value_t *result = function->callback(callable, args, kwargs, function->user_data, out_error);

    if (result == NULL && (out_error == NULL || *out_error == NULL)) tinypy_internal_make_vm_error(tinypy_internal_value_vm(callable), TINYPY_ERROR_RUNTIME, "native function failed without an error", out_error);
    assert(result == NULL || tinypy_internal_value_belongs_to(tinypy_internal_value_vm(callable), result));
    return result;
}

tinypy_value_t *tinypy_native_function_name(const tinypy_value_t *function)
{
    return __tinypy_internal_native_function_validate(function)->name;
}

void *tinypy_native_function_user_data(const tinypy_value_t *function)
{
    return __tinypy_internal_native_function_validate(function)->user_data;
}
