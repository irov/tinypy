#include "tinypy/method.h"

#include "internal.h"

//////////////////////////////////////////////////////////////////////////
static void __tinypy_method_receiver_error(tinypy_method_object_t *method, tinypy_value_t *receiver, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(&method->base);
    tinypy_value_t *name = TINYPY_VALUE_KIND(method->function) == TINYPY_VALUE_FUNCTION ? TINYPY_FUNCTION_OBJECT(method->function)->name
        : TINYPY_VALUE_KIND(method->function) == TINYPY_VALUE_NATIVE_FUNCTION ? TINYPY_NATIVE_FUNCTION_OBJECT(method->function)->name : NULL;
    const char *function_name = name != NULL && TINYPY_VALUE_KIND(name) == TINYPY_VALUE_STRING ? (const char *)TINYPY_TEXT_BYTES(name) : "?";
    size_t function_size = name != NULL && TINYPY_VALUE_KIND(name) == TINYPY_VALUE_STRING ? TINYPY_TEXT_BYTE_SIZE(name) : 1U;
    size_t owner_size = 1U;
    const char *owner_name = "?";
    size_t receiver_size = 7U;
    const char *receiver_name = "nothing";

    if (TINYPY_VALUE_KIND(method->owner) == TINYPY_VALUE_CLASS) {
        tinypy_value_t *class_name = TINYPY_CLASS_OBJECT(method->owner)->name;

        owner_name = (const char *)TINYPY_TEXT_BYTES(class_name);
        owner_size = TINYPY_TEXT_BYTE_SIZE(class_name);
    }
    else if (TINYPY_VALUE_KIND(method->owner) == TINYPY_VALUE_TYPE) {
        owner_name = tinypy_type_name((tinypy_type_t *)method->owner, &owner_size);
    }
    if (receiver != NULL) {
        if (TINYPY_VALUE_KIND(receiver) == TINYPY_VALUE_OLD_INSTANCE) {
            tinypy_value_t *class_name = TINYPY_CLASS_OBJECT(TINYPY_OLD_INSTANCE_OBJECT(receiver)->class_object)->name;

            receiver_name = (const char *)TINYPY_TEXT_BYTES(class_name);
            receiver_size = TINYPY_TEXT_BYTE_SIZE(class_name);
        }
        else {
            receiver_name = tinypy_type_name(receiver->type, &receiver_size);
        }
    }
    tinypy_message_part_t parts[] = {
        TINYPY_MESSAGE_PART_LITERAL("unbound method "), {function_name, function_size},
        TINYPY_MESSAGE_PART_LITERAL("() must be called with "), {owner_name, owner_size},
        TINYPY_MESSAGE_PART_LITERAL(" instance as first argument (got "), {receiver_name, receiver_size},
        {receiver != NULL ? " instance instead)" : " instead)", receiver != NULL ? 18U : 9U}
    };
    tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
}

//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_method_new(tinypy_value_t *function, tinypy_value_t *self, tinypy_value_t *owner) {
    tinypy_method_object_t *method;

    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    if (vm->method_free_list != NULL) {
        method = vm->method_free_list;
        vm->method_free_list = method->function != NULL ? TINYPY_METHOD_OBJECT(method->function) : NULL;
        vm->method_free_count -= 1U;
        method->base.ref = 1;
#if defined(TINYPY_CYCLE_DIAGNOSTICS)
        __tinypy_internal_cycle_diagnostics_value_reuse(vm, &method->base);
#endif
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

    method->function = vm->method_free_list != NULL ? &vm->method_free_list->base : NULL;
    vm->method_free_list = method;
    vm->method_free_count += 1U;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_method_free_list_finalize(tinypy_vm_t *vm) {
    while (vm->method_free_list != NULL) {
        tinypy_method_object_t *method = vm->method_free_list;

        vm->method_free_list = method->function != NULL ? TINYPY_METHOD_OBJECT(method->function) : NULL;
        vm->method_free_count -= 1U;
        vm->types[TINYPY_VALUE_METHOD].base.base.ref -= 1;
#if defined(TINYPY_CYCLE_DIAGNOSTICS)
        __tinypy_internal_cycle_diagnostics_value_unregister(vm, &method->base);
#endif
        tinypy_internal_vm_deallocate(vm, method, sizeof(*method));
    }
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_function_descriptor_get(tinypy_value_t *descriptor, tinypy_value_t *instance, tinypy_type_t *owner, tinypy_error_t **out_error) {
    TINYPY_CLEAR_ERROR(out_error);
    tinypy_value_t *return_value_1 = tinypy_method_new(descriptor, instance, &owner->base.base);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_method_descriptor_get(tinypy_value_t *descriptor, tinypy_value_t *instance, tinypy_type_t *owner, tinypy_error_t **out_error) {
    tinypy_method_object_t *method = TINYPY_METHOD_OBJECT(descriptor);

    TINYPY_CLEAR_ERROR(out_error);
    if (method->self != NULL || TINYPY_VALUE_KIND(method->owner) != TINYPY_VALUE_TYPE || tinypy_type_is_subtype(owner, (tinypy_type_t *)method->owner) == 0) {
        TINYPY_INCREF(descriptor);
        return descriptor;
    }
    tinypy_value_t *return_value_1 = tinypy_method_new(method->function, instance, &owner->base.base);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_method_call(tinypy_value_t *callable, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_method_object_t *method = TINYPY_METHOD_OBJECT(callable);
    tinypy_vm_t *vm = TINYPY_VALUE_VM(callable);
    tinypy_value_t *bound_self = method->self;
    size_t argument_count = TINYPY_TUPLE_SIZE(args);

    if (bound_self == NULL) {
        tinypy_value_t *first = argument_count != 0U ? TINYPY_TUPLE_GET(args, 0U) : NULL;
        tinypy_bool_t valid_owner = TINYPY_FALSE;

        if (first != NULL && TINYPY_VALUE_KIND(method->owner) == TINYPY_VALUE_TYPE) {
            tinypy_type_t *owner_type = (tinypy_type_t *)method->owner;

            valid_owner = tinypy_type_is_subtype(first->type, owner_type);
        }
        else if (first != NULL && TINYPY_VALUE_KIND(method->owner) == TINYPY_VALUE_CLASS) {
            if (TINYPY_VALUE_KIND(first) == TINYPY_VALUE_OLD_INSTANCE) {
                tinypy_value_t *old_instance_class = tinypy_old_instance_class(first);
                valid_owner = tinypy_class_is_subclass(old_instance_class, method->owner);
            }
        }

        if (valid_owner == 0) {
            __tinypy_method_receiver_error(method, first, out_error);
            return NULL;
        }
        tinypy_value_t *return_value_1 = tinypy_call(method->function, args, kwargs, out_error);
        return return_value_1;
    }
    tinypy_value_t *call_args = tinypy_internal_tuple_prepend_checked(vm, bound_self, args, out_error);
    if (call_args == NULL) {
        return NULL;
    }
    tinypy_value_t *result = tinypy_call(method->function, call_args, kwargs, out_error);
    TINYPY_DECREF(call_args);
    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_method_compare(tinypy_value_t *left, tinypy_value_t *right, int32_t operation, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(left);
    tinypy_method_object_t *left_method;
    tinypy_method_object_t *right_method;
    int32_t equal;

    if (TINYPY_VALUE_KIND(right) != TINYPY_VALUE_METHOD) {
        tinypy_value_t *return_value_1 = tinypy_not_implemented_get(vm);
        return return_value_1;
    }
    if (operation != TINYPY_COMPARE_EQUAL && operation != TINYPY_COMPARE_NOT_EQUAL) {
        tinypy_value_t *return_value_2 = tinypy_not_implemented_get(vm);
        return return_value_2;
    }
    left_method = TINYPY_METHOD_OBJECT(left);
    right_method = TINYPY_METHOD_OBJECT(right);
    if (left_method->function != right_method->function) {
        equal = 0;
    }
    else if (left_method->self == right_method->self) {
        equal = 1;
    }
    else if (left_method->self == NULL || right_method->self == NULL) {
        equal = 0;
    }
    else {
        equal = tinypy_compare_bool(left_method->self, right_method->self, TINYPY_COMPARE_EQUAL, out_error);
        if (equal < 0) {
            return NULL;
        }
    }
    tinypy_value_t *return_value_3 = tinypy_bool_from_i32(vm, operation == TINYPY_COMPARE_EQUAL ? equal : equal == 0);
    return return_value_3;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_method_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = type->vm;
    size_t count = TINYPY_TUPLE_SIZE(args);
    tinypy_value_t *callable;
    tinypy_value_t *self;
    tinypy_value_t *owner;
    tinypy_bool_t owned_owner = TINYPY_FALSE;

    if ((kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U) || count < 2U || count > 3U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "instancemethod constructor received invalid arguments", out_error);
        return NULL;
    }
    callable = TINYPY_TUPLE_GET(args, 0U);
    self = TINYPY_TUPLE_GET(args, 1U);
    if (tinypy_is_callable(callable) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "first instancemethod argument must be callable", out_error);
        return NULL;
    }
    if (TINYPY_VALUE_KIND(self) == TINYPY_VALUE_NONE) {
        self = NULL;
    }
    if (count == 2U) {
        if (self == NULL) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "unbound instancemethod requires an owner", out_error);
            return NULL;
        }
        owner = tinypy_none_get(vm);
        owned_owner = TINYPY_TRUE;
    }
    else {
        owner = TINYPY_TUPLE_GET(args, 2U);
    }
    tinypy_value_t *result = tinypy_method_new(callable, self, owner);
    if (owned_owner != 0) {
        TINYPY_DECREF(owner);
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_method_function(const tinypy_value_t *method) {
    tinypy_value_t *return_value_1 = TINYPY_METHOD_OBJECT((tinypy_value_t *)method)->function;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_method_self(const tinypy_value_t *method) {
    tinypy_value_t *return_value_1 = TINYPY_METHOD_OBJECT((tinypy_value_t *)method)->self;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_method_owner(const tinypy_value_t *method) {
    tinypy_value_t *return_value_1 = TINYPY_METHOD_OBJECT((tinypy_value_t *)method)->owner;
    return return_value_1;
}
