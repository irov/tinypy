#include "internal.h"

#include <assert.h>

static int32_t __tinypy_functools_no_keywords(tinypy_vm_t *vm, tinypy_value_t *kwargs, tinypy_error_t **out_error)
{
    if (kwargs == NULL || tinypy_dict_size(kwargs) == 0U) return INT32_C(1);
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "function does not accept keyword arguments", out_error);
    return INT32_C(0);
}

static void __tinypy_functools_dict_update(tinypy_value_t *target, tinypy_value_t *source)
{
    tinypy_dict_object_t *dict;
    size_t index;

    if (source == NULL) return;
    dict = TINYPY_DICT_OBJECT(source);
    for (index = 0U; index <= dict->mask; index += 1U) {
        tinypy_dict_entry_t *entry = &dict->table[index];

        if (entry->state == TINYPY_DICT_ENTRY_ACTIVE) tinypy_dict_set(target, entry->key, entry->value);
    }
}

void tinypy_internal_partial_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data)
{
    tinypy_partial_object_t *partial = TINYPY_PARTIAL_OBJECT(value);

    visit(partial->callable, user_data);
    visit(partial->args, user_data);
    visit(partial->keywords, user_data);
    if (partial->dict != NULL) visit(partial->dict, user_data);
}

tinypy_value_t *tinypy_internal_partial_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = type->vm;
    tinypy_partial_object_t *partial;
    tinypy_value_t *callable;
    size_t argument_count = tinypy_tuple_size(args);

    if (argument_count == 0U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "partial expected at least one argument", out_error);
        return NULL;
    }
    callable = tinypy_tuple_get(args, 0U);
    if (callable->type->call == NULL && tinypy_internal_object_has_special(callable, "__call__", 8U) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "the first argument must be callable", out_error);
        return NULL;
    }
    partial = (tinypy_partial_object_t *)tinypy_internal_value_allocate(vm, TINYPY_VALUE_PARTIAL, sizeof(*partial));
    partial->callable = callable;
    tinypy_retain(callable);
    partial->args = argument_count == 1U ? tinypy_tuple_from_items(vm, NULL, 0U) : tinypy_tuple_from_items(vm, &tinypy_internal_tuple_items(args)[1], argument_count - 1U);
    partial->keywords = tinypy_dict_new(vm);
    __tinypy_functools_dict_update(partial->keywords, kwargs);
    return &partial->base;
}

tinypy_value_t *tinypy_internal_partial_call(tinypy_value_t *callable, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error)
{
    tinypy_partial_object_t *partial = TINYPY_PARTIAL_OBJECT(callable);
    tinypy_vm_t *vm = tinypy_internal_value_vm(callable);
    size_t bound_count = tinypy_tuple_size(partial->args);
    size_t call_count = tinypy_tuple_size(args);
    tinypy_value_t **items;
    tinypy_value_t *combined_args;
    tinypy_value_t *combined_kwargs;
    tinypy_value_t *result;
    size_t index;

    assert(bound_count <= SIZE_MAX - call_count);
    assert(bound_count + call_count <= SIZE_MAX / sizeof(*items));
    if (bound_count + call_count == 0U) {
        combined_args = tinypy_tuple_from_items(vm, NULL, 0U);
    } else {
        items = (tinypy_value_t **)tinypy_internal_vm_allocate(vm, (bound_count + call_count) * sizeof(*items), (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
        for (index = 0U; index < bound_count; index += 1U) items[index] = tinypy_tuple_get(partial->args, index);
        for (index = 0U; index < call_count; index += 1U) items[bound_count + index] = tinypy_tuple_get(args, index);
        combined_args = tinypy_tuple_from_items(vm, items, bound_count + call_count);
        tinypy_internal_vm_deallocate(vm, items, (bound_count + call_count) * sizeof(*items), (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    }
    combined_kwargs = tinypy_dict_new(vm);
    __tinypy_functools_dict_update(combined_kwargs, partial->keywords);
    __tinypy_functools_dict_update(combined_kwargs, kwargs);
    result = tinypy_call(partial->callable, combined_args, combined_kwargs, out_error);
    tinypy_release(combined_kwargs);
    tinypy_release(combined_args);
    return result;
}

static tinypy_value_t *__tinypy_functools_reduce(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);
    size_t argument_count = tinypy_tuple_size(args);
    tinypy_value_t *iterator;
    tinypy_value_t *accumulator;
    tinypy_error_t *iteration_error = NULL;

    (void)user_data;
    if (__tinypy_functools_no_keywords(vm, kwargs, out_error) == 0) return NULL;
    if (argument_count < 2U || argument_count > 3U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "reduce expected two or three arguments", out_error);
        return NULL;
    }
    iterator = tinypy_iter(tinypy_tuple_get(args, 1U), out_error);
    if (iterator == NULL) return NULL;
    if (argument_count == 3U) {
        accumulator = tinypy_tuple_get(args, 2U);
        tinypy_retain(accumulator);
    } else {
        accumulator = tinypy_next(iterator, &iteration_error);
        if (accumulator == NULL) {
            tinypy_release(iterator);
            if (iteration_error != NULL) {
                if (out_error != NULL) *out_error = iteration_error;
                else tinypy_error_release(iteration_error);
            } else {
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "reduce() of empty sequence with no initial value", out_error);
            }
            return NULL;
        }
    }
    for (;;) {
        tinypy_value_t *item = tinypy_next(iterator, &iteration_error);
        tinypy_value_t *call_items[2];
        tinypy_value_t *call_args;
        tinypy_value_t *next;

        if (item == NULL) break;
        call_items[0] = accumulator;
        call_items[1] = item;
        call_args = tinypy_tuple_from_items(vm, call_items, 2U);
        next = tinypy_call(tinypy_tuple_get(args, 0U), call_args, NULL, out_error);
        tinypy_release(call_args);
        tinypy_release(item);
        if (next == NULL) {
            tinypy_release(accumulator);
            tinypy_release(iterator);
            return NULL;
        }
        tinypy_release(accumulator);
        accumulator = next;
    }
    tinypy_release(iterator);
    if (iteration_error != NULL) {
        tinypy_release(accumulator);
        if (out_error != NULL) *out_error = iteration_error;
        else tinypy_error_release(iteration_error);
        return NULL;
    }
    return accumulator;
}

void tinypy_internal_initialize_functools_module(tinypy_vm_t *vm)
{
    tinypy_value_t *module = tinypy_module_new(vm, "_functools", 10U);
    tinypy_value_t *name = tinypy_string_from_bytes(vm, "_functools", 10U);
    tinypy_value_t *reduce = tinypy_native_function_new(vm, "reduce", 6U, __tinypy_functools_reduce, NULL, NULL);

    tinypy_module_add_value(module, "__name__", 8U, name);
    tinypy_module_add_value(module, "partial", 7U, &vm->partial_type.base.base);
    tinypy_module_add_value(module, "reduce", 6U, reduce);
    tinypy_release(reduce);
    tinypy_release(name);
    tinypy_internal_register_module(vm, "_functools", 10U, module);
    tinypy_release(module);
}
