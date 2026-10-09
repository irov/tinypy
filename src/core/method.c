#include "tinypy/method.h"

#include "internal.h"

//////////////////////////////////////////////////////////////////////////
static void __tinypy_method_class_name(tinypy_value_t *class_object, char *buffer, size_t capacity) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(class_object);
    tinypy_internal_exception_state_t state;
    tinypy_value_t *name;

    buffer[0] = '?';
    buffer[1] = '\0';
    tinypy_internal_exception_preserve_begin(vm, &state);
    name = tinypy_object_get_attr_value(class_object, vm->internal_special_name_key, NULL);
    if (name != NULL) {
        if (TINYPY_VALUE_KIND(name) == TINYPY_VALUE_STRING) {
            size_t size = TINYPY_TEXT_BYTE_SIZE(name);

            if (size >= capacity) {
                size = capacity - 1U;
            }
            memcpy(buffer, TINYPY_TEXT_BYTES(name), size);
            buffer[size] = '\0';
        }
        TINYPY_DECREF(name);
    }
    tinypy_internal_exception_preserve_end(vm, &state);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_method_receiver_error(tinypy_method_object_t *method, tinypy_value_t *receiver, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(&method->base);
    char owner_name[256];
    char receiver_name[256];

    __tinypy_method_class_name(method->owner, owner_name, sizeof(owner_name));
    if (receiver != NULL) {
        tinypy_internal_exception_state_t state;
        tinypy_value_t *class_object;

        tinypy_internal_exception_preserve_begin(vm, &state);
        class_object = tinypy_object_get_attr_value(receiver, vm->internal_special_class_key, NULL);
        tinypy_internal_exception_preserve_end(vm, &state);
        if (class_object == NULL) {
            class_object = TINYPY_RET(&receiver->type->base.base);
        }
        __tinypy_method_class_name(class_object, receiver_name, sizeof(receiver_name));
        TINYPY_DECREF(class_object);
    }
    else {
        memcpy(receiver_name, "nothing", sizeof("nothing"));
    }
    tinypy_value_t *name = TINYPY_VALUE_KIND(method->function) == TINYPY_VALUE_FUNCTION ? TINYPY_FUNCTION_OBJECT(method->function)->name
        : TINYPY_VALUE_KIND(method->function) == TINYPY_VALUE_NATIVE_FUNCTION ? TINYPY_NATIVE_FUNCTION_OBJECT(method->function)->name : NULL;
    const char *function_name = name != NULL && TINYPY_VALUE_KIND(name) == TINYPY_VALUE_STRING ? (const char *)TINYPY_TEXT_BYTES(name) : "?";
    size_t function_size = name != NULL && TINYPY_VALUE_KIND(name) == TINYPY_VALUE_STRING ? TINYPY_TEXT_BYTE_SIZE(name) : 1U;

    if (name != NULL) {
        TINYPY_INCREF(name);
    }
    tinypy_message_part_t parts[] = {
        TINYPY_MESSAGE_PART_LITERAL("unbound method "), {function_name, function_size},
        TINYPY_MESSAGE_PART_LITERAL("() must be called with "), {owner_name, strlen(owner_name)},
        TINYPY_MESSAGE_PART_LITERAL(" instance as first argument (got "), {receiver_name, strlen(receiver_name)},
        {receiver != NULL ? " instance instead)" : " instead)", receiver != NULL ? 18U : 9U}
    };
    tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
    if (name != NULL) {
        TINYPY_DECREF(name);
    }
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
    method->weakrefs = NULL;
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
/* Drops a reference to a bound method. A method needs no finalization, so
   the last reference to one without weak references returns it to the free
   list directly. */
void tinypy_internal_method_release(tinypy_value_t *value) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_method_object_t *method = TINYPY_METHOD_OBJECT(value);

    if (TINYPY_REFCNT(value) != 1U || method->weakrefs != NULL || vm->state != TINYPY_VM_STATE_LIVE || vm->method_free_count >= TINYPY_METHOD_FREE_LIST_MAX) {
        TINYPY_DECREF(value);
        return;
    }
    value->ref = 0;
    TINYPY_DECREF(method->function);
    if (method->self != NULL) {
        TINYPY_DECREF(method->self);
    }
    TINYPY_DECREF(method->owner);
    tinypy_internal_method_free_list_push(vm, value);
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
    tinypy_value_t *result = tinypy_internal_method_bind(descriptor, instance, &owner->base.base, out_error);

    return result;
}
//////////////////////////////////////////////////////////////////////////
/* instancemethod_descr_get: a bound method, or an unbound method whose class
   is not a base of the owner (classic or new-style), stays unchanged. */
tinypy_value_t *tinypy_internal_method_bind(tinypy_value_t *descriptor, tinypy_value_t *instance, tinypy_value_t *owner, tinypy_error_t **out_error) {
    tinypy_method_object_t *method = TINYPY_METHOD_OBJECT(descriptor);

    TINYPY_CLEAR_ERROR(out_error);
    if (method->self != NULL) {
        return TINYPY_RET(descriptor);
    }
    if (TINYPY_VALUE_KIND(method->owner) != TINYPY_VALUE_NONE && TINYPY_VALUE_KIND(owner) != TINYPY_VALUE_NONE) {
        int32_t subclass = tinypy_internal_object_instance_check(owner, method->owner, TINYPY_TRUE, out_error);

        if (subclass < 0) {
            return NULL;
        }
        if (subclass == 0) {
            return TINYPY_RET(descriptor);
        }
    }
    tinypy_value_t *result = tinypy_method_new(method->function, instance, owner);
    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_method_call(tinypy_value_t *callable, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_method_object_t *method = TINYPY_METHOD_OBJECT(callable);
    tinypy_vm_t *vm = TINYPY_VALUE_VM(callable);
    tinypy_value_t *bound_self = method->self;
    size_t argument_count = TINYPY_TUPLE_SIZE(args);

    if (bound_self == NULL) {
        tinypy_value_t *first = argument_count != 0U ? TINYPY_TUPLE_GET(args, 0U) : NULL;
        int32_t valid_owner = INT32_C(0);

        if (first != NULL) {
            valid_owner = tinypy_internal_object_instance_check(first, method->owner, TINYPY_FALSE, out_error);
            if (valid_owner < 0) {
                return NULL;
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
        tinypy_value_t *return_value_1 = TINYPY_RET_NOT_IMPLEMENTED(vm);
        return return_value_1;
    }
    if (operation != TINYPY_COMPARE_EQUAL && operation != TINYPY_COMPARE_NOT_EQUAL) {
        tinypy_value_t *return_value_2 = TINYPY_RET_NOT_IMPLEMENTED(vm);
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

    if (kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "instancemethod does not take keyword arguments", out_error);
        return NULL;
    }
    if (count < 2U || count > 3U) {
        tinypy_internal_make_arity_error(vm, "instancemethod", 14U, count, 2U, 3U, TINYPY_ARITY_STYLE_UNPACK, out_error);
        return NULL;
    }
    callable = TINYPY_TUPLE_GET(args, 0U);
    self = TINYPY_TUPLE_GET(args, 1U);
    if (tinypy_is_callable(callable) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "first argument must be callable", out_error);
        return NULL;
    }
    if (TINYPY_VALUE_KIND(self) == TINYPY_VALUE_NONE) {
        self = NULL;
    }
    if (count == 2U) {
        if (self == NULL) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "unbound methods must have non-NULL im_class", out_error);
            return NULL;
        }
        owner = TINYPY_RET_NONE(vm);
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
