#include "tinypy/method.h"

#include "internal.h"

#include <assert.h>

static tinypy_method_object_t *__tinypy_internal_method_validate(const tinypy_value_t *value)
{
    assert(value != NULL);
    assert(tinypy_internal_vm_valid(tinypy_internal_value_vm(value)));
    assert(tinypy_internal_value_kind(value) == TINYPY_VALUE_METHOD);
    return TINYPY_METHOD_OBJECT((tinypy_value_t *)value);
}

tinypy_value_t *tinypy_method_new(tinypy_value_t *function, tinypy_value_t *self, tinypy_value_t *owner)
{
    tinypy_vm_t *vm;
    tinypy_method_object_t *method;

    assert(function != NULL);
    vm = tinypy_internal_value_vm(function);
    assert(tinypy_internal_vm_valid(vm));
    assert(function->type->call != NULL);
    assert(self == NULL || tinypy_internal_value_belongs_to(vm, self));
    assert(owner != NULL);
    assert(tinypy_internal_value_belongs_to(vm, owner));
    assert(tinypy_internal_value_kind(owner) == TINYPY_VALUE_TYPE || tinypy_internal_value_kind(owner) == TINYPY_VALUE_CLASS);
    method = (tinypy_method_object_t *)tinypy_internal_value_allocate(vm, TINYPY_VALUE_METHOD, sizeof(*method));
    method->function = function;
    method->self = self;
    method->owner = owner;
    tinypy_retain(function);
    if (self != NULL) {
        tinypy_retain(self);
    }
    tinypy_retain(owner);
    return &method->base;
}

void tinypy_internal_method_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data)
{
    tinypy_method_object_t *method = TINYPY_METHOD_OBJECT(value);

    visit(method->function, user_data);
    if (method->self != NULL) {
        visit(method->self, user_data);
    }
    visit(method->owner, user_data);
}

tinypy_value_t *tinypy_internal_function_descriptor_get(tinypy_value_t *descriptor, tinypy_value_t *instance, tinypy_type_t *owner, tinypy_error_t **out_error)
{
    tinypy_internal_clear_error(out_error);
    return tinypy_method_new(descriptor, instance, &owner->base.base);
}

tinypy_value_t *tinypy_internal_method_call(tinypy_value_t *callable, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error)
{
    tinypy_method_object_t *method = TINYPY_METHOD_OBJECT(callable);
    tinypy_vm_t *vm = tinypy_internal_value_vm(callable);
    tinypy_value_t *bound_self = method->self;
    tinypy_value_t *call_args;
    tinypy_value_t *result;
    tinypy_value_t **items;
    size_t argument_count = tinypy_tuple_size(args);
    size_t output_count;
    size_t index;

    if (bound_self == NULL) {
        tinypy_value_t *first = argument_count != 0U ? tinypy_tuple_get(args, 0U) : NULL;
        int32_t valid_owner = INT32_C(0);

        if (first != NULL && tinypy_internal_value_kind(method->owner) == TINYPY_VALUE_TYPE) {
            tinypy_type_t *owner_type = (tinypy_type_t *)method->owner;

            valid_owner = (tinypy_internal_value_kind(first) == TINYPY_VALUE_TYPE && tinypy_type_is_subtype((tinypy_type_t *)first, owner_type) != 0) || (tinypy_internal_value_kind(first) != TINYPY_VALUE_TYPE && tinypy_type_is_subtype((tinypy_type_t *)tinypy_object_type(first), owner_type) != 0);
        } else if (first != NULL && tinypy_internal_value_kind(method->owner) == TINYPY_VALUE_CLASS) {
            if (tinypy_internal_value_kind(first) == TINYPY_VALUE_CLASS) valid_owner = tinypy_class_is_subclass(first, method->owner);
            else if (tinypy_internal_value_kind(first) == TINYPY_VALUE_OLD_INSTANCE) valid_owner = tinypy_class_is_subclass(tinypy_old_instance_class(first), method->owner);
        }

        if (valid_owner == 0) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "unbound method requires an instance of its owner", out_error);
            return NULL;
        }
        return tinypy_call(method->function, args, kwargs, out_error);
    }
    assert(argument_count < SIZE_MAX);
    output_count = argument_count + 1U;
    assert(output_count <= SIZE_MAX / sizeof(*items));
    items = (tinypy_value_t **)tinypy_internal_vm_allocate(vm, output_count * sizeof(*items), (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    items[0] = bound_self;
    for (index = 0U; index < argument_count; index += 1U) {
        items[index + 1U] = tinypy_tuple_get(args, index);
    }
    call_args = tinypy_tuple_from_items(vm, items, output_count);
    tinypy_internal_vm_deallocate(vm, items, output_count * sizeof(*items), (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    result = tinypy_call(method->function, call_args, kwargs, out_error);
    tinypy_release(call_args);
    return result;
}

tinypy_value_t *tinypy_method_function(const tinypy_value_t *method) { return __tinypy_internal_method_validate(method)->function; }
tinypy_value_t *tinypy_method_self(const tinypy_value_t *method) { return __tinypy_internal_method_validate(method)->self; }
tinypy_value_t *tinypy_method_owner(const tinypy_value_t *method) { return __tinypy_internal_method_validate(method)->owner; }
