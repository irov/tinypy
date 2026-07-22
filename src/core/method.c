#include "tinypy/method.h"

#include "internal.h"

#include <assert.h>

//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_method_new(tinypy_value_t *function, tinypy_value_t *self, tinypy_value_t *owner) {
    tinypy_method_object_t *method;

    assert(function != NULL);
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    assert(tinypy_internal_vm_valid(vm));
    assert(function->type->call != NULL);
    assert(self == NULL || tinypy_internal_value_belongs_to(vm, self));
    assert(owner != NULL);
    assert(tinypy_internal_value_belongs_to(vm, owner));
    assert(TINYPY_VALUE_KIND(owner) == TINYPY_VALUE_TYPE || TINYPY_VALUE_KIND(owner) == TINYPY_VALUE_CLASS);
    if (vm->method_free_list != NULL) {
        method = vm->method_free_list;
        vm->method_free_list = method->function != NULL ? TINYPY_METHOD_OBJECT(method->function) : NULL;
        assert(vm->method_free_count != 0U);
        vm->method_free_count -= 1U;
        assert(method->base.ref == 0);
        assert(method->base.type == &vm->method_type);
        method->base.ref = 1;
    }
    else {
        method = (tinypy_method_object_t *)tinypy_internal_value_allocate(vm, TINYPY_VALUE_METHOD, sizeof(*method));
    }
    method->function = function;
    method->self = self;
    method->owner = owner;
    TINYPY_INCREF(function);
    if (self != NULL) {
        TINYPY_INCREF(self);
    }
    TINYPY_INCREF(owner);
    return &method->base;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_method_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data) {
    tinypy_method_object_t *method = TINYPY_METHOD_OBJECT(value);

    visit(method->function, user_data);
    if (method->self != NULL) {
        visit(method->self, user_data);
    }
    visit(method->owner, user_data);
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_method_free_list_push(tinypy_vm_t *vm, tinypy_value_t *value) {
    tinypy_method_object_t *method = TINYPY_METHOD_OBJECT(value);

    assert(value->type == &vm->method_type);
    assert(value->ref == 0);
    assert(vm->method_free_count < TINYPY_METHOD_FREE_LIST_MAX);
    method->function = vm->method_free_list != NULL ? &vm->method_free_list->base : NULL;
    vm->method_free_list = method;
    vm->method_free_count += 1U;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_method_free_list_finalize(tinypy_vm_t *vm) {
    while (vm->method_free_list != NULL) {
        tinypy_method_object_t *method = vm->method_free_list;

        vm->method_free_list = method->function != NULL ? TINYPY_METHOD_OBJECT(method->function) : NULL;
        assert(vm->method_free_count != 0U);
        vm->method_free_count -= 1U;
        assert(vm->method_type.base.base.ref > 1);
        vm->method_type.base.base.ref -= 1;
        tinypy_internal_vm_deallocate(vm, method, sizeof(*method), (uint32_t)TINYPY_ALLOC_TAG_VALUE);
    }
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_function_descriptor_get(tinypy_value_t *descriptor, tinypy_value_t *instance, tinypy_type_t *owner, tinypy_error_t **out_error) {
    TINYPY_CLEAR_ERROR(out_error);
    return tinypy_method_new(descriptor, instance, &owner->base.base);
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_method_descriptor_get(tinypy_value_t *descriptor, tinypy_value_t *instance, tinypy_type_t *owner, tinypy_error_t **out_error) {
    tinypy_method_object_t *method = TINYPY_METHOD_OBJECT(descriptor);

    assert(owner != NULL);
    TINYPY_CLEAR_ERROR(out_error);
    if (method->self != NULL || TINYPY_VALUE_KIND(method->owner) != TINYPY_VALUE_TYPE || tinypy_type_is_subtype(owner, (tinypy_type_t *)method->owner) == 0) {
        TINYPY_INCREF(descriptor);
        return descriptor;
    }
    return tinypy_method_new(method->function, instance, &owner->base.base);
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_method_call(tinypy_value_t *callable, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_method_object_t *method = TINYPY_METHOD_OBJECT(callable);
    tinypy_vm_t *vm = TINYPY_VALUE_VM(callable);
    tinypy_value_t *bound_self = method->self;
    tinypy_value_t **items;
    size_t argument_count = TINYPY_TUPLE_SIZE(args);
    size_t output_count;
    size_t index;

    if (bound_self == NULL) {
        tinypy_value_t *first = argument_count != 0U ? TINYPY_TUPLE_GET(args, 0U) : NULL;
        int32_t valid_owner = INT32_C(0);

        if (first != NULL && TINYPY_VALUE_KIND(method->owner) == TINYPY_VALUE_TYPE) {
            tinypy_type_t *owner_type = (tinypy_type_t *)method->owner;

            int condition = (TINYPY_VALUE_KIND(first) == TINYPY_VALUE_TYPE && tinypy_type_is_subtype((tinypy_type_t *)first, owner_type) != 0);
            if (condition == 0) {
                int condition_2 = TINYPY_VALUE_KIND(first) != TINYPY_VALUE_TYPE;
                if (condition_2 != 0) {
                    const tinypy_type_t *type = tinypy_object_type(first);
                    condition_2 = tinypy_type_is_subtype((tinypy_type_t *)type, owner_type) != 0;
                }
                condition = (condition_2);
            }
            valid_owner = condition;
        }
        else if (first != NULL && TINYPY_VALUE_KIND(method->owner) == TINYPY_VALUE_CLASS) {
            if (TINYPY_VALUE_KIND(first) == TINYPY_VALUE_CLASS) {
                valid_owner = tinypy_class_is_subclass(first, method->owner);
            }
            else if (TINYPY_VALUE_KIND(first) == TINYPY_VALUE_OLD_INSTANCE) {
                tinypy_value_t *old_instance_class = tinypy_old_instance_class(first);
                valid_owner = tinypy_class_is_subclass(old_instance_class, method->owner);
            }
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
    for (index = 0U; index < argument_count; ++index) {
        items[index + 1U] = TINYPY_TUPLE_GET(args, index);
    }
    tinypy_value_t *call_args = tinypy_tuple_from_items(vm, items, output_count);
    tinypy_internal_vm_deallocate(vm, items, output_count * sizeof(*items), (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    tinypy_value_t *result = tinypy_call(method->function, call_args, kwargs, out_error);
    TINYPY_DECREF(call_args);
    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_method_function(const tinypy_value_t *method) {
    assert(method != NULL);
    assert(tinypy_internal_vm_valid(TINYPY_VALUE_VM(method)));
    assert(TINYPY_VALUE_KIND(method) == TINYPY_VALUE_METHOD);
    return TINYPY_METHOD_OBJECT((tinypy_value_t *)method)->function;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_method_self(const tinypy_value_t *method) {
    assert(method != NULL);
    assert(tinypy_internal_vm_valid(TINYPY_VALUE_VM(method)));
    assert(TINYPY_VALUE_KIND(method) == TINYPY_VALUE_METHOD);
    return TINYPY_METHOD_OBJECT((tinypy_value_t *)method)->self;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_method_owner(const tinypy_value_t *method) {
    assert(method != NULL);
    assert(tinypy_internal_vm_valid(TINYPY_VALUE_VM(method)));
    assert(TINYPY_VALUE_KIND(method) == TINYPY_VALUE_METHOD);
    return TINYPY_METHOD_OBJECT((tinypy_value_t *)method)->owner;
}
