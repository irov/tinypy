#include "tinypy/native.h"

#include "internal.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_native_function_new(tinypy_vm_t *vm, const char *name, size_t name_size, tinypy_native_function_callback_t callback, void *user_data, tinypy_native_function_finalize_t finalize) {
    tinypy_value_t *key = tinypy_internal_name_from_bytes(vm, name, name_size);
    tinypy_value_t *function = tinypy_native_function_new_key(key, callback, user_data, finalize);
    TINYPY_DECREF(key);
    return function;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_native_function_new_key(tinypy_value_t *name, tinypy_native_function_callback_t callback, void *user_data, tinypy_native_function_finalize_t finalize) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(name);
    tinypy_native_function_object_t *function = (tinypy_native_function_object_t *)tinypy_internal_value_allocate(vm, TINYPY_VALUE_NATIVE_FUNCTION, sizeof(*function));
    function->name = TINYPY_RET(name);
    function->module = NULL;
    function->function = NULL;
    function->self = NULL;
    function->owner = NULL;
    function->callback = callback;
    function->user_data = user_data;
    function->finalize = finalize;
    function->owner_retained = TINYPY_FALSE;
    function->descriptor_kind = TINYPY_NATIVE_DESCRIPTOR_AUTO;
    return &function->base;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_native_function_set_descriptor_kind(tinypy_value_t *function, tinypy_native_descriptor_kind_e descriptor_kind) {
    TINYPY_NATIVE_FUNCTION_OBJECT(function)->descriptor_kind = descriptor_kind;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_module_add_function(tinypy_value_t *module, tinypy_value_t *name, tinypy_native_function_callback_t callback, void *user_data, tinypy_native_function_finalize_t finalize) {
    tinypy_value_t *function = tinypy_native_function_new_key(name, callback, user_data, finalize);
    tinypy_module_add_value_key(module, name, function);
    TINYPY_DECREF(function);
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_type_add_method(tinypy_type_t *type, tinypy_value_t *name, tinypy_native_function_callback_t callback, void *user_data, tinypy_native_function_finalize_t finalize, tinypy_native_descriptor_kind_e descriptor_kind) {
    tinypy_value_t *function = tinypy_native_function_new_key(name, callback, user_data, finalize);
    tinypy_internal_native_function_set_descriptor_kind(function, descriptor_kind);
    tinypy_type_set_attr_key(type, name, function);
    TINYPY_DECREF(function);
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_type_add_property(tinypy_type_t *type, tinypy_value_t *name, tinypy_native_function_callback_t getter, void *user_data, tinypy_native_function_finalize_t finalize) {
    tinypy_value_t *function = tinypy_native_function_new_key(name, getter, user_data, finalize);
    tinypy_value_t *property = tinypy_property_new(type->vm, function, NULL, NULL, NULL);
    tinypy_type_set_attr_key(type, name, property);
    TINYPY_DECREF(property);
    TINYPY_DECREF(function);
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_type_add_class_method(tinypy_type_t *type, tinypy_value_t *name, tinypy_native_function_callback_t callback, void *user_data, tinypy_native_function_finalize_t finalize) {
    tinypy_value_t *function = tinypy_native_function_new_key(name, callback, user_data, finalize);
    tinypy_value_t *descriptor = tinypy_class_method_new(function);
    tinypy_type_set_attr_key(type, name, descriptor);
    TINYPY_DECREF(descriptor);
    TINYPY_DECREF(function);
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_type_add_static_method(tinypy_type_t *type, tinypy_value_t *name, tinypy_native_function_callback_t callback, void *user_data, tinypy_native_function_finalize_t finalize) {
    tinypy_value_t *function = tinypy_native_function_new_key(name, callback, user_data, finalize);
    tinypy_value_t *descriptor = tinypy_static_method_new(function);
    tinypy_type_set_attr_key(type, name, descriptor);
    TINYPY_DECREF(descriptor);
    TINYPY_DECREF(function);
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_native_function_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data) {
    tinypy_native_function_object_t *function = TINYPY_NATIVE_FUNCTION_OBJECT(value);

    visit(function->name, user_data);
    if (function->module != NULL) {
        visit(function->module, user_data);
    }
    if (function->function != NULL) {
        visit(function->function, user_data);
    }
    if (function->self != NULL) {
        visit(function->self, user_data);
    }
    if (function->owner_retained != 0) {
        visit(&function->owner->base.base, user_data);
    }
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_native_function_destroy(tinypy_value_t *value) {
    tinypy_internal_native_function_finalize(value);
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_native_function_finalize(tinypy_value_t *value) {
    tinypy_native_function_object_t *function = TINYPY_NATIVE_FUNCTION_OBJECT(value);

    if (function->finalize != NULL) {
        tinypy_native_function_finalize_t finalize = function->finalize;
        void *user_data = function->user_data;

        function->finalize = NULL;
        function->user_data = NULL;
        tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
        tinypy_internal_exception_state_t state;
        tinypy_internal_exception_preserve_begin(vm, &state);
        finalize(user_data);
        if (vm->state == TINYPY_VM_STATE_LIVE) {
            tinypy_internal_output_unraisable(vm, value);
        }
        tinypy_internal_exception_preserve_end(vm, &state);
    }
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_native_function_check_receiver(tinypy_value_t *callable, tinypy_value_t *receiver, tinypy_bool_t binding, tinypy_error_t **out_error) {
    tinypy_native_function_object_t *function = TINYPY_NATIVE_FUNCTION_OBJECT(callable);
    if (function->owner == NULL || (receiver != NULL && tinypy_type_is_subtype(receiver->type, function->owner) != TINYPY_FALSE)) {
        return TINYPY_TRUE;
    }
    tinypy_vm_t *vm = TINYPY_VALUE_VM(callable);
    if (receiver == NULL) {
        tinypy_message_part_t parts[] = {
            TINYPY_MESSAGE_PART_LITERAL("descriptor '"), TINYPY_MESSAGE_PART_TEXT(function->name),
            TINYPY_MESSAGE_PART_LITERAL("' of '"), {function->owner->name, function->owner->name_size},
            TINYPY_MESSAGE_PART_LITERAL("' object needs an argument"),
        };
        tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
    }
    else {
        tinypy_message_part_t parts[] = {
            TINYPY_MESSAGE_PART_LITERAL("descriptor '"), TINYPY_MESSAGE_PART_TEXT(function->name),
            TINYPY_MESSAGE_PART_LITERAL("' requires a '"), {function->owner->name, function->owner->name_size},
            TINYPY_MESSAGE_PART_LITERAL("' object but received a '"), TINYPY_MESSAGE_PART_TYPE_NAME(receiver),
            TINYPY_MESSAGE_PART_LITERAL("'"),
        };
        if (binding != TINYPY_FALSE) {
            parts[2].bytes = "' for '";
            parts[2].size = sizeof("' for '") - 1U;
            parts[4].bytes = "' objects doesn't apply to '";
            parts[4].size = sizeof("' objects doesn't apply to '") - 1U;
            parts[6].bytes = "' object";
            parts[6].size = sizeof("' object") - 1U;
        }
        tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
    }
    return TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_native_function_invoke(tinypy_value_t *callable, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_native_function_object_t *function = TINYPY_NATIVE_FUNCTION_OBJECT(callable);
    tinypy_value_t *result = function->callback(callable, args, kwargs, function->user_data, out_error);

    if (result == NULL && (out_error == NULL || *out_error == NULL)) {
        tinypy_vm_t *vm = TINYPY_VALUE_VM(callable);
        if (tinypy_vm_has_error(vm) != 0) {
            tinypy_internal_exception_make_diagnostic(vm, out_error);
        }
        else {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_RUNTIME, "native function failed without an error", out_error);
        }
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_native_method_arguments(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, size_t minimum, size_t maximum, tinypy_arity_style_e style, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *name = tinypy_native_function_name(function);
    tinypy_native_function_object_t *native = TINYPY_NATIVE_FUNCTION_OBJECT(function);
    tinypy_bool_t wrapper = function->type == vm->native_wrapper_descriptor_type
        || (native->function != NULL && native->function->type == vm->native_wrapper_descriptor_type);
    size_t count = TINYPY_TUPLE_SIZE(args);
    size_t supplied = count != 0U ? count - 1U : 0U;

    if (kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U) {
        tinypy_message_part_t parts[] = {
            {wrapper != TINYPY_FALSE ? "wrapper " : "", wrapper != TINYPY_FALSE ? 8U : 0U},
            TINYPY_MESSAGE_PART_TEXT(name),
            {wrapper != TINYPY_FALSE ? " doesn't take keyword arguments" : "() takes no keyword arguments", wrapper != TINYPY_FALSE ? sizeof(" doesn't take keyword arguments") - 1U : sizeof("() takes no keyword arguments") - 1U},
        };

        tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
        return TINYPY_FALSE;
    }
    if (count == 0U || supplied < minimum || supplied > maximum) {
        const char *name_bytes = (const char *)TINYPY_TEXT_BYTES(name);
        size_t name_size = TINYPY_TEXT_BYTE_SIZE(name);

        if (wrapper != TINYPY_FALSE) {
            if (style == TINYPY_ARITY_STYLE_PARSED) {
                name_bytes = NULL;
                name_size = 0U;
            }
            else if (style == TINYPY_ARITY_STYLE_UNPACK) {
                name_bytes = "";
                name_size = 0U;
            }
            else if (style == TINYPY_ARITY_STYLE_SINGLE) {
                style = TINYPY_ARITY_STYLE_WRAPPER;
            }
        }
        tinypy_internal_make_arity_error(vm, name_bytes, name_size, supplied, minimum, maximum, style, out_error);
        return TINYPY_FALSE;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_native_function_call(tinypy_value_t *callable, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_native_function_object_t *function = TINYPY_NATIVE_FUNCTION_OBJECT(callable);
    tinypy_value_t *call_args = args;

    if (function->self == NULL && function->owner != NULL) {
        tinypy_value_t *receiver = TINYPY_TUPLE_SIZE(args) != 0U ? TINYPY_TUPLE_GET(args, 0U) : NULL;

        if (__tinypy_native_function_check_receiver(callable, receiver, TINYPY_FALSE, out_error) == TINYPY_FALSE) {
            return NULL;
        }
    }

    if (function->self != NULL) {
        tinypy_vm_t *vm = TINYPY_VALUE_VM(callable);

        call_args = tinypy_internal_tuple_prepend_checked(vm, function->self, args, out_error);
        if (call_args == NULL) {
            return NULL;
        }
    }

    tinypy_value_t *result = __tinypy_native_function_invoke(callable, call_args, kwargs, out_error);

    if (call_args != args) {
        TINYPY_DECREF(call_args);
    }

    return result;
}
//////////////////////////////////////////////////////////////////////////
/* Bound native calls build one owned argument tuple, including self. The
   callback may retain it; its public ABI and recursion guard stay unchanged. */
tinypy_value_t *tinypy_internal_native_function_call_items(tinypy_value_t *callable, tinypy_value_t *const *items, size_t count, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(callable);
    tinypy_native_function_object_t *function = TINYPY_NATIVE_FUNCTION_OBJECT(callable);
    TINYPY_CLEAR_ERROR(out_error);
    if (tinypy_internal_recursion_check(vm, TINYPY_NATIVE_STACK_ADDRESS(), "maximum recursion depth exceeded while calling a Python object", out_error) == 0) {
        return NULL;
    }
    if (function->self == NULL && __tinypy_native_function_check_receiver(callable, count != 0U ? items[0] : NULL, TINYPY_FALSE, out_error) == TINYPY_FALSE) {
        return NULL;
    }
    tinypy_value_t *args = tinypy_internal_tuple_join_items_checked(vm, function->self, items, count, NULL, 0U, out_error);
    if (args == NULL) {
        return NULL;
    }
    vm->evaluation_depth += 1U;
    tinypy_value_t *result = __tinypy_native_function_invoke(callable, args, kwargs, out_error);
    TINYPY_DECREF(args);
    vm->evaluation_depth -= 1U;
    return result;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_native_method_free_list_push(tinypy_vm_t *vm, tinypy_value_t *value) {
    tinypy_native_function_object_t *method = TINYPY_NATIVE_FUNCTION_OBJECT(value);
    method->function = vm->native_method_free_list != NULL ? &vm->native_method_free_list->base : NULL;
    method->self = NULL;
    vm->native_method_free_list = method;
    vm->native_method_free_count += 1U;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_native_method_free_list_finalize(tinypy_vm_t *vm) {
    while (vm->native_method_free_list != NULL) {
        tinypy_native_function_object_t *method = vm->native_method_free_list;
        vm->native_method_free_list = method->function != NULL ? TINYPY_NATIVE_FUNCTION_OBJECT(method->function) : NULL;
        vm->native_method_free_count -= 1U;
        method->base.type->base.base.ref -= 1;
#if defined(TINYPY_CYCLE_DIAGNOSTICS)
        __tinypy_internal_cycle_diagnostics_value_unregister(vm, &method->base);
#endif
        tinypy_internal_vm_deallocate(vm, method, sizeof(*method));
    }
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_native_function_descriptor_get(tinypy_value_t *descriptor, tinypy_value_t *instance, tinypy_type_t *owner, tinypy_error_t **out_error) {
    tinypy_native_function_object_t *function = TINYPY_NATIVE_FUNCTION_OBJECT(descriptor);

    TINYPY_CLEAR_ERROR(out_error);
    if (instance == NULL || function->self != NULL) {
        return TINYPY_RET(descriptor);
    }
    if (__tinypy_native_function_check_receiver(descriptor, instance, TINYPY_TRUE, out_error) == TINYPY_FALSE) {
        return NULL;
    }

    (void)owner;

    tinypy_vm_t *vm = TINYPY_VALUE_VM(descriptor);
    tinypy_type_t *method_type = descriptor->type == vm->native_wrapper_descriptor_type && vm->native_method_wrapper_type != NULL ? vm->native_method_wrapper_type : &vm->types[TINYPY_VALUE_NATIVE_FUNCTION];
    tinypy_native_function_object_t *method;
    if (vm->native_method_free_list != NULL) {
        method = vm->native_method_free_list;
        vm->native_method_free_list = method->function != NULL ? TINYPY_NATIVE_FUNCTION_OBJECT(method->function) : NULL;
        vm->native_method_free_count -= 1U;
        if (method->base.type != method_type) {
            method->base.type->base.base.ref -= 1;
            method_type->base.base.ref += 1;
            method->base.type = method_type;
        }
        method->base.ref = 1;
#if defined(TINYPY_CYCLE_DIAGNOSTICS)
        __tinypy_internal_cycle_diagnostics_value_reuse(vm, &method->base);
#endif
    }
    else {
        method = (tinypy_native_function_object_t *)tinypy_internal_object_allocate_checked(vm, method_type, sizeof(*method), out_error);
        if (method == NULL) {
            return NULL;
        }
    }

    method->name = function->name;
    method->module = function->module;
    method->function = descriptor;
    method->self = instance;
    method->owner = NULL;
    method->callback = function->callback;
    method->user_data = function->user_data;
    method->finalize = NULL;
    method->owner_retained = TINYPY_FALSE;
    method->descriptor_kind = TINYPY_NATIVE_DESCRIPTOR_AUTO;
    TINYPY_INCREF(method->name);
    if (method->module != NULL) {
        TINYPY_INCREF(method->module);
    }
    TINYPY_INCREF(method->function);
    TINYPY_INCREF(method->self);
    return &method->base;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_native_function_call_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (TINYPY_TUPLE_SIZE(args) == 0U || TINYPY_VALUE_KIND(TINYPY_TUPLE_GET(args, 0U)) != TINYPY_VALUE_NATIVE_FUNCTION) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "builtin function __call__ requires a builtin function", out_error);
        return NULL;
    }
    tinypy_value_t *self = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *call_args = tinypy_internal_tuple_tail_checked(vm, args, 1U, out_error);
    if (call_args == NULL) {
        return NULL;
    }
    tinypy_value_t *result = tinypy_internal_native_function_call(self, call_args, kwargs, out_error);
    TINYPY_DECREF(call_args);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_native_function_repr_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    static const char function_prefix[] = "<built-in function ";
    static const char method_prefix[] = "<built-in method ";
    static const char wrapper_prefix[] = "<method-wrapper '";
    static const char method_middle[] = " of ";
    static const char object_middle[] = " object at ";
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 0U, 0U, TINYPY_ARITY_STYLE_WRAPPER, out_error) == 0) {
        return NULL;
    }
    tinypy_native_function_object_t *native = TINYPY_NATIVE_FUNCTION_OBJECT(TINYPY_TUPLE_GET(args, 0U));
    size_t name_size;
    const uint8_t *name = (const uint8_t *)tinypy_string_view(native->name, &name_size);
    const char *prefix = native->self == NULL ? function_prefix : method_prefix;
    size_t prefix_size = native->self == NULL ? sizeof(function_prefix) - 1U : sizeof(method_prefix) - 1U;
    const char *type_name = NULL;
    size_t type_name_size = 0U;
    char pointer_text[2U + sizeof(uintptr_t) * 2U + 1U];
    size_t pointer_size = 0U;
    size_t total_size;
    tinypy_bool_t bound_wrapper = native->base.type == vm->native_method_wrapper_type;
    if (bound_wrapper != 0) {
        prefix = wrapper_prefix;
        prefix_size = sizeof(wrapper_prefix) - 1U;
    }

    if (native->owner != NULL && native->self == NULL) {
        static const char method_descriptor_prefix[] = "<method '";
        static const char wrapper_descriptor_prefix[] = "<slot wrapper '";
        static const char descriptor_middle[] = "' of '";
        static const char descriptor_suffix[] = "' objects>";
        tinypy_bool_t wrapper = native->base.type == vm->native_wrapper_descriptor_type;
        const char *descriptor_prefix = wrapper != 0 ? wrapper_descriptor_prefix : method_descriptor_prefix;
        size_t descriptor_prefix_size = wrapper != 0 ? sizeof(wrapper_descriptor_prefix) - 1U : sizeof(method_descriptor_prefix) - 1U;
        size_t owner_name_size;
        const char *owner_name = tinypy_type_name(native->owner, &owner_name_size);
        size_t fixed_size = descriptor_prefix_size + sizeof(descriptor_middle) - 1U + sizeof(descriptor_suffix) - 1U;

        if (name_size > SIZE_MAX - fixed_size || owner_name_size > SIZE_MAX - fixed_size - name_size) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_MEMORY, "native descriptor representation is too large", out_error);
            return NULL;
        }
        total_size = fixed_size + name_size + owner_name_size;
        uint8_t *descriptor_output;
        tinypy_value_t *descriptor_result = tinypy_internal_text_allocate_uninitialized_checked(vm, TINYPY_VALUE_STRING, total_size, total_size, &descriptor_output, out_error);
        size_t descriptor_offset = 0U;

        if (descriptor_result == NULL) {
            return NULL;
        }
        (void)memcpy(descriptor_output + descriptor_offset, descriptor_prefix, descriptor_prefix_size);
        descriptor_offset += descriptor_prefix_size;
        (void)memcpy(descriptor_output + descriptor_offset, name, name_size);
        descriptor_offset += name_size;
        (void)memcpy(descriptor_output + descriptor_offset, descriptor_middle, sizeof(descriptor_middle) - 1U);
        descriptor_offset += sizeof(descriptor_middle) - 1U;
        (void)memcpy(descriptor_output + descriptor_offset, owner_name, owner_name_size);
        descriptor_offset += owner_name_size;
        (void)memcpy(descriptor_output + descriptor_offset, descriptor_suffix, sizeof(descriptor_suffix) - 1U);
        return descriptor_result;
    }

    if (native->self == NULL) {
        if (name_size > SIZE_MAX - prefix_size - 1U) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_MEMORY, "builtin function representation is too large", out_error);
            return NULL;
        }
        total_size = prefix_size + name_size + 1U;
    }
    else {
        type_name = tinypy_type_name(native->self->type, &type_name_size);
        int pointer_length = snprintf(pointer_text, sizeof(pointer_text), "0x%" PRIxPTR, (uintptr_t)native->self);
        if (pointer_length < 0 || (size_t)pointer_length >= sizeof(pointer_text)) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_RUNTIME, "failed to format builtin method address", out_error);
            return NULL;
        }
        pointer_size = (size_t)pointer_length;
        size_t fixed_size = prefix_size + (sizeof(method_middle) - 1U) + (sizeof(object_middle) - 1U) + pointer_size + 1U + (bound_wrapper != 0 ? 1U : 0U);
        if (name_size > SIZE_MAX - fixed_size || type_name_size > SIZE_MAX - fixed_size - name_size) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_MEMORY, "builtin method representation is too large", out_error);
            return NULL;
        }
        total_size = fixed_size + name_size + type_name_size;
    }
    uint8_t *output;
    tinypy_value_t *result = tinypy_internal_text_allocate_uninitialized_checked(vm, TINYPY_VALUE_STRING, total_size, total_size, &output, out_error);
    if (result == NULL) {
        return NULL;
    }
    size_t offset = 0U;

    (void)memcpy(output + offset, prefix, prefix_size);
    offset += prefix_size;
    (void)memcpy(output + offset, name, name_size);
    offset += name_size;
    if (native->self != NULL) {
        if (bound_wrapper != 0) {
            output[offset++] = (uint8_t)'\'';
        }
        (void)memcpy(output + offset, method_middle, sizeof(method_middle) - 1U);
        offset += sizeof(method_middle) - 1U;
        (void)memcpy(output + offset, type_name, type_name_size);
        offset += type_name_size;
        (void)memcpy(output + offset, object_middle, sizeof(object_middle) - 1U);
        offset += sizeof(object_middle) - 1U;
        (void)memcpy(output + offset, pointer_text, pointer_size);
        offset += pointer_size;
    }
    output[offset] = (uint8_t)'>';
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_hash_t __tinypy_native_function_hash_slot(tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_native_function_object_t *native = TINYPY_NATIVE_FUNCTION_OBJECT(value);
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_hash_t hash;

    if (value->type == vm->native_method_descriptor_type || value->type == vm->native_wrapper_descriptor_type) {
        value->type->flags &= ~TINYPY_TYPE_FLAG_NEEDS_ATTRIBUTE_READY;
        hash = (tinypy_hash_t)((uintptr_t)value >> 4U);
    }
    else {
        tinypy_value_t *previous_raised = vm->raised_value;
        tinypy_hash_t receiver_hash = native->self != NULL ? tinypy_internal_hash_value(native->self, out_error) : 0;
        if ((out_error != NULL && *out_error != NULL) || vm->raised_value != previous_raised) {
            return (tinypy_hash_t)0;
        }
        if (value->type == vm->native_method_wrapper_type) {
            uint32_t descriptor_hash = (uint32_t)((uintptr_t)native->function >> 4U);
            uint32_t combined = descriptor_hash ^ (uint32_t)receiver_hash;
            hash = (tinypy_hash_t)(int32_t)combined;
        }
        else {
            hash = receiver_hash ^ (tinypy_hash_t)(((uintptr_t)native->callback >> 4U) ^ ((uintptr_t)native->user_data >> 4U));
        }
    }
    return hash == (tinypy_hash_t)-1 ? (tinypy_hash_t)-2 : hash;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_native_function_hash_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 0U, 0U, TINYPY_ARITY_STYLE_WRAPPER, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *previous_raised = vm->raised_value;
    tinypy_hash_t hash = __tinypy_native_function_hash_slot(TINYPY_TUPLE_GET(args, 0U), out_error);
    if ((out_error != NULL && *out_error != NULL) || vm->raised_value != previous_raised) {
        return NULL;
    }
    tinypy_value_t *result = tinypy_integer_from_i64(vm, hash);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_native_function_compare_slot(tinypy_value_t *left, tinypy_value_t *right, int32_t operation, tinypy_error_t **out_error);
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_initialize_native_descriptor_types(tinypy_vm_t *vm) {
    tinypy_type_t **types[] = {&vm->native_method_descriptor_type, &vm->native_wrapper_descriptor_type, &vm->native_method_wrapper_type};
    tinypy_value_t *const names[] = {vm->internal_method_descriptor_key, vm->internal_wrapper_descriptor_key, vm->internal_method_wrapper_key};

    for (size_t index = 0U; index < sizeof(types) / sizeof(types[0]); ++index) {
        tinypy_type_t *type = tinypy_internal_type_new_configured(names[index], NULL, 0U, NULL, NULL, TINYPY_FALSE, TINYPY_FALSE, NULL);

        type->layout_kind = TINYPY_VALUE_NATIVE_FUNCTION;
        type->basic_size = sizeof(tinypy_native_function_object_t);
        type->slots_offset = 0U;
        type->dict_offset = 0U;
        type->weakref_offset = 0U;
        type->has_instance_dict = TINYPY_FALSE;
        type->release_references = tinypy_internal_native_function_release_references;
        type->traverse_references = tinypy_internal_native_function_release_references;
        type->destroy = tinypy_internal_native_function_destroy;
        type->call = tinypy_internal_native_function_call;
        type->descriptor_get = index == 2U ? NULL : tinypy_internal_native_function_descriptor_get;
        type->rich_compare = index == 2U ? __tinypy_native_function_compare_slot : NULL;
        type->hash = __tinypy_native_function_hash_slot;
        type->create = NULL;
        type->flags = (type->flags | TINYPY_TYPE_FLAG_IMMUTABLE) & ~TINYPY_TYPE_FLAG_BASE_TYPE;
        if (index == 0U || index == 2U) {
            type->flags |= TINYPY_TYPE_FLAG_NEEDS_ATTRIBUTE_READY;
        }
        *types[index] = type;
    }
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_native_descriptor_get_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    size_t count = TINYPY_TUPLE_SIZE(args);
    tinypy_value_t *descriptor;
    tinypy_value_t *instance;
    tinypy_type_t *owner;

    (void)user_data;
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 1U, 2U, TINYPY_ARITY_STYLE_UNPACK, out_error) == 0) {
        return NULL;
    }
    descriptor = TINYPY_TUPLE_GET(args, 0U);
    instance = TINYPY_TUPLE_GET(args, 1U);
    owner = TINYPY_NATIVE_FUNCTION_OBJECT(descriptor)->owner;
    if (TINYPY_VALUE_KIND(instance) == TINYPY_VALUE_NONE) {
        instance = NULL;
    }
    if (instance == NULL && (count == 2U || TINYPY_VALUE_KIND(TINYPY_TUPLE_GET(args, 2U)) == TINYPY_VALUE_NONE)) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "__get__(None, None) is invalid", out_error);
        return NULL;
    }
    tinypy_value_t *return_value_1 = tinypy_internal_native_function_descriptor_get(descriptor, instance, owner, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_native_function_compare_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 1U, 1U, TINYPY_ARITY_STYLE_WRAPPER, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *result = __tinypy_native_function_compare_slot(TINYPY_TUPLE_GET(args, 0U), TINYPY_TUPLE_GET(args, 1U), (int32_t)(intptr_t)user_data, out_error);
    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_native_function_compare_three_way(tinypy_value_t *left, tinypy_value_t *right, int32_t *out_order, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(left);
    tinypy_native_function_object_t *a = TINYPY_NATIVE_FUNCTION_OBJECT(left);
    tinypy_native_function_object_t *b = TINYPY_NATIVE_FUNCTION_OBJECT(right);

    if (left->type == vm->native_method_wrapper_type) {
        if (a->function == b->function) {
            tinypy_bool_t result = tinypy_internal_compare_three_way(a->self, b->self, out_order, out_error);
            return result;
        }
        *out_order = (uintptr_t)a->function < (uintptr_t)b->function ? -1 : 1;
    }
    else if (a->self != b->self) {
        *out_order = (uintptr_t)a->self < (uintptr_t)b->self ? -1 : 1;
    }
    else if (a->callback == b->callback && a->user_data == b->user_data) {
        *out_order = 0;
    }
    else {
        size_t a_size;
        size_t b_size;
        const char *a_name = tinypy_string_view(a->name, &a_size);
        const char *b_name = tinypy_string_view(b->name, &b_size);
        size_t common_size = a_size < b_size ? a_size : b_size;
        int order = memcmp(a_name, b_name, common_size);

        *out_order = order < 0 || (order == 0 && a_size < b_size) ? -1 : 1;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_native_function_compare_slot(tinypy_value_t *left, tinypy_value_t *right, int32_t operation, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(left);
    int32_t order;
    tinypy_bool_t result;

    if (left->type != right->type || (left->type != vm->native_method_wrapper_type && left->type != &vm->types[TINYPY_VALUE_NATIVE_FUNCTION])) {
        return TINYPY_RET_NOT_IMPLEMENTED(vm);
    }
    if (left->type == &vm->types[TINYPY_VALUE_NATIVE_FUNCTION]) {
        if (operation != TINYPY_COMPARE_EQUAL && operation != TINYPY_COMPARE_NOT_EQUAL) {
            return TINYPY_RET_NOT_IMPLEMENTED(vm);
        }
        tinypy_native_function_object_t *a = TINYPY_NATIVE_FUNCTION_OBJECT(left);
        tinypy_native_function_object_t *b = TINYPY_NATIVE_FUNCTION_OBJECT(right);
        result = a->self == b->self && a->callback == b->callback && a->user_data == b->user_data;
        if (operation == TINYPY_COMPARE_NOT_EQUAL) {
            result = result == 0;
        }
    }
    else {
        if (tinypy_internal_native_function_compare_three_way(left, right, &order, out_error) == 0) {
            return NULL;
        }
        switch (operation) {
        case TINYPY_COMPARE_LESS: result = order < 0; break;
        case TINYPY_COMPARE_LESS_EQUAL: result = order <= 0; break;
        case TINYPY_COMPARE_EQUAL: result = order == 0; break;
        case TINYPY_COMPARE_NOT_EQUAL: result = order != 0; break;
        case TINYPY_COMPARE_GREATER: result = order > 0; break;
        default: result = order >= 0; break;
        }
    }
    tinypy_value_t *value = tinypy_bool_from_i32(vm, result);
    return value;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_native_function_cmp_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 1U, 1U, TINYPY_ARITY_STYLE_WRAPPER, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *left = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *right = TINYPY_TUPLE_GET(args, 1U);
    if (left->type != right->type) {
        tinypy_message_part_t parts[] = {
            {left->type->name, left->type->name_size}, TINYPY_MESSAGE_PART_LITERAL(".__cmp__(x,y) requires y to be a '"),
            {left->type->name, left->type->name_size}, TINYPY_MESSAGE_PART_LITERAL("', not a '"),
            {right->type->name, right->type->name_size}, TINYPY_MESSAGE_PART_LITERAL("'")
        };

        tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, 6U, out_error);
        return NULL;
    }
    int32_t order;
    if (tinypy_internal_native_function_compare_three_way(left, right, &order, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *result = tinypy_integer_from_i64(vm, order);
    return result;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_initialize_native_function_type(tinypy_vm_t *vm) {
    const struct {
        tinypy_value_t *name;
        tinypy_compare_operation_e operation;
    } comparisons[] = {
        {vm->internal_special_lt_key, TINYPY_COMPARE_LESS},
        {vm->internal_special_le_key, TINYPY_COMPARE_LESS_EQUAL},
        {vm->internal_special_eq_key, TINYPY_COMPARE_EQUAL},
        {vm->internal_special_ne_key, TINYPY_COMPARE_NOT_EQUAL},
        {vm->internal_special_gt_key, TINYPY_COMPARE_GREATER},
        {vm->internal_special_ge_key, TINYPY_COMPARE_GREATER_EQUAL}};

    tinypy_type_t *function_type = &vm->types[TINYPY_VALUE_NATIVE_FUNCTION];
    tinypy_type_t *descriptor_types[] = {vm->native_method_descriptor_type, vm->native_wrapper_descriptor_type, vm->native_method_wrapper_type};
    tinypy_internal_type_add_method(function_type, vm->internal_special_call_key, __tinypy_native_function_call_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(function_type, vm->internal_special_repr_key, __tinypy_native_function_repr_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(function_type, vm->internal_special_hash_key, __tinypy_native_function_hash_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(function_type, vm->internal_special_cmp_key, __tinypy_native_function_cmp_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    for (size_t index = 0U; index < sizeof(comparisons) / sizeof(comparisons[0]); ++index) {
        tinypy_value_t *method_name = comparisons[index].name;
        tinypy_internal_type_add_method(function_type, method_name, __tinypy_native_function_compare_method, (void *)(intptr_t)comparisons[index].operation, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    }
    for (size_t type_index = 0U; type_index < sizeof(descriptor_types) / sizeof(descriptor_types[0]); ++type_index) {
        tinypy_type_t *type = descriptor_types[type_index];

        tinypy_internal_type_add_method(type, vm->internal_special_call_key, __tinypy_native_function_call_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
        if (type != vm->native_method_wrapper_type) {
            tinypy_internal_type_add_method(type, vm->internal_special_get_key, __tinypy_native_descriptor_get_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
        }
        tinypy_internal_type_add_method(type, vm->internal_special_repr_key, __tinypy_native_function_repr_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
        tinypy_internal_type_add_method(type, vm->internal_special_hash_key, __tinypy_native_function_hash_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
        if (type == vm->native_method_wrapper_type) {
            tinypy_internal_type_add_method(type, vm->internal_special_cmp_key, __tinypy_native_function_cmp_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
        }
    }
    vm->types[TINYPY_VALUE_NATIVE_FUNCTION].hash = __tinypy_native_function_hash_slot;
    vm->types[TINYPY_VALUE_NATIVE_FUNCTION].rich_compare = __tinypy_native_function_compare_slot;
    tinypy_internal_initialize_native_function_descriptors(vm);
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_native_function_name(const tinypy_value_t *function) {
    tinypy_value_t *return_value_1 = TINYPY_NATIVE_FUNCTION_OBJECT((tinypy_value_t *)function)->name;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
void *tinypy_native_function_user_data(const tinypy_value_t *function) {
    void *return_value_1 = TINYPY_NATIVE_FUNCTION_OBJECT((tinypy_value_t *)function)->user_data;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static void *__tinypy_internal_native_payload(tinypy_value_t *instance) {
    void *return_value_1 = (uint8_t *)instance + instance->type->native_payload_offset;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_native_call(tinypy_value_t *instance, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_native_type_spec_t *spec = &instance->type->native_spec;

    void *native_payload = __tinypy_internal_native_payload(instance);
    tinypy_value_t *return_value_1 = spec->call(instance, native_payload, args, kwargs, spec->user_data, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_native_repr(tinypy_value_t *instance, tinypy_error_t **out_error) {
    tinypy_native_type_spec_t *spec = &instance->type->native_spec;

    void *native_payload = __tinypy_internal_native_payload(instance);
    tinypy_value_t *return_value_1 = spec->repr(instance, native_payload, spec->user_data, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_hash_t __tinypy_internal_native_hash(tinypy_value_t *instance, tinypy_error_t **out_error) {
    tinypy_native_type_spec_t *spec = &instance->type->native_spec;

    void *native_payload = __tinypy_internal_native_payload(instance);
    tinypy_hash_t return_value_1 = spec->hash(instance, native_payload, spec->user_data, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_native_compare(tinypy_value_t *instance, tinypy_value_t *other, int32_t operation, tinypy_error_t **out_error) {
    tinypy_native_type_spec_t *spec = &instance->type->native_spec;

    void *native_payload = __tinypy_internal_native_payload(instance);
    tinypy_value_t *return_value_1 = spec->compare(instance, native_payload, other, (tinypy_compare_operation_e)operation, spec->user_data, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_native_get_attribute(tinypy_value_t *instance, tinypy_value_t *name, tinypy_error_t **out_error) {
    tinypy_native_type_spec_t *spec = &instance->type->native_spec;

    void *native_payload = __tinypy_internal_native_payload(instance);
    tinypy_value_t *return_value_1 = spec->get_attribute(instance, native_payload, name, spec->user_data, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_internal_native_set_attribute(tinypy_value_t *instance, tinypy_value_t *name, tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_native_type_spec_t *spec = &instance->type->native_spec;

    void *native_payload = __tinypy_internal_native_payload(instance);
    tinypy_bool_t return_value_1 = spec->set_attribute(instance, native_payload, name, value, spec->user_data, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_native_mapping_get(tinypy_value_t *instance, tinypy_value_t *key, tinypy_error_t **out_error) {
    tinypy_native_type_spec_t *spec = &instance->type->native_spec;

    void *native_payload = __tinypy_internal_native_payload(instance);
    tinypy_value_t *return_value_1 = spec->mapping_get(instance, native_payload, key, spec->user_data, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_internal_native_mapping_set(tinypy_value_t *instance, tinypy_value_t *key, tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_native_type_spec_t *spec = &instance->type->native_spec;

    void *native_payload = __tinypy_internal_native_payload(instance);
    tinypy_bool_t return_value_1 = spec->mapping_set(instance, native_payload, key, value, spec->user_data, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static ptrdiff_t __tinypy_internal_native_mapping_length(tinypy_value_t *instance, tinypy_error_t **out_error) {
    tinypy_native_type_spec_t *spec = &instance->type->native_spec;

    void *native_payload = __tinypy_internal_native_payload(instance);
    ptrdiff_t return_value_1 = spec->mapping_length(instance, native_payload, spec->user_data, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_native_sequence_get(tinypy_value_t *instance, tinypy_value_t *key, tinypy_error_t **out_error) {
    tinypy_native_type_spec_t *spec = &instance->type->native_spec;

    void *native_payload = __tinypy_internal_native_payload(instance);
    tinypy_value_t *return_value_1 = spec->sequence_get(instance, native_payload, key, spec->user_data, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_internal_native_sequence_set(tinypy_value_t *instance, tinypy_value_t *key, tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_native_type_spec_t *spec = &instance->type->native_spec;

    void *native_payload = __tinypy_internal_native_payload(instance);
    tinypy_bool_t return_value_1 = spec->sequence_set(instance, native_payload, key, value, spec->user_data, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static ptrdiff_t __tinypy_internal_native_sequence_length(tinypy_value_t *instance, tinypy_error_t **out_error) {
    tinypy_native_type_spec_t *spec = &instance->type->native_spec;

    void *native_payload = __tinypy_internal_native_payload(instance);
    ptrdiff_t return_value_1 = spec->sequence_length(instance, native_payload, spec->user_data, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_internal_native_contains(tinypy_value_t *instance, tinypy_value_t *item, tinypy_error_t **out_error) {
    tinypy_native_type_spec_t *spec = &instance->type->native_spec;

    void *native_payload = __tinypy_internal_native_payload(instance);
    int32_t return_value_1 = spec->contains(instance, native_payload, item, spec->user_data, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_native_iter(tinypy_value_t *instance, tinypy_error_t **out_error) {
    tinypy_native_type_spec_t *spec = &instance->type->native_spec;

    void *native_payload = __tinypy_internal_native_payload(instance);
    tinypy_value_t *return_value_1 = spec->iter(instance, native_payload, spec->user_data, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_native_next(tinypy_value_t *instance, tinypy_error_t **out_error) {
    tinypy_native_type_spec_t *spec = &instance->type->native_spec;

    void *native_payload = __tinypy_internal_native_payload(instance);
    tinypy_value_t *return_value_1 = spec->next(instance, native_payload, spec->user_data, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_native_negative(tinypy_value_t *instance, tinypy_error_t **out_error) {
    tinypy_native_type_spec_t *spec = &instance->type->native_spec;

    void *native_payload = __tinypy_internal_native_payload(instance);
    tinypy_value_t *return_value_1 = spec->negative(instance, native_payload, spec->user_data, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_native_absolute(tinypy_value_t *instance, tinypy_error_t **out_error) {
    tinypy_native_type_spec_t *spec = &instance->type->native_spec;

    void *native_payload = __tinypy_internal_native_payload(instance);
    tinypy_value_t *return_value_1 = spec->absolute(instance, native_payload, spec->user_data, out_error);
    return return_value_1;
}
#define TINYPY_NATIVE_BINARY_WRAPPER(name, field)                                                                                       \
    static tinypy_value_t *__tinypy_internal_native_##name(tinypy_value_t *instance, tinypy_value_t *other, tinypy_error_t **out_error) { \
        tinypy_native_type_spec_t *spec = &instance->type->native_spec;                                                                 \
        tinypy_value_t *return_value = spec->field(instance, __tinypy_internal_native_payload(instance), other, spec->user_data, out_error); \
        return return_value;                                                                                                           \
    }
TINYPY_NATIVE_BINARY_WRAPPER(add, add)
TINYPY_NATIVE_BINARY_WRAPPER(subtract, subtract)
TINYPY_NATIVE_BINARY_WRAPPER(multiply, multiply)
TINYPY_NATIVE_BINARY_WRAPPER(divide, divide)
TINYPY_NATIVE_BINARY_WRAPPER(inplace_add, inplace_add)
TINYPY_NATIVE_BINARY_WRAPPER(inplace_subtract, inplace_subtract)
TINYPY_NATIVE_BINARY_WRAPPER(inplace_multiply, inplace_multiply)
TINYPY_NATIVE_BINARY_WRAPPER(inplace_divide, inplace_divide)
TINYPY_NATIVE_BINARY_WRAPPER(reflected_add, reflected_add)
TINYPY_NATIVE_BINARY_WRAPPER(reflected_subtract, reflected_subtract)
TINYPY_NATIVE_BINARY_WRAPPER(reflected_multiply, reflected_multiply)
TINYPY_NATIVE_BINARY_WRAPPER(reflected_divide, reflected_divide)

#undef TINYPY_NATIVE_BINARY_WRAPPER
//////////////////////////////////////////////////////////////////////////
void tinypy_native_type_spec_init(tinypy_native_type_spec_t *spec) {
    (void)memset(spec, 0, sizeof(*spec));
    spec->abi_version = TINYPY_NATIVE_TYPE_ABI_VERSION;
    spec->struct_size = (uint32_t)sizeof(*spec);
    spec->payload_alignment = TINYPY_INTERNAL_ALIGNMENT;
    spec->has_instance_dict = TINYPY_TRUE;
    spec->has_weakrefs = TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_internal_native_spec_bool(const tinypy_native_type_spec_t *spec, size_t offset, tinypy_bool_t value) {
    if ((size_t)spec->struct_size < offset + sizeof(value)) {
        return TINYPY_TRUE;
    }
    return value != 0 ? TINYPY_TRUE : TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_internal_native_configure_slots(tinypy_type_t *type) {
    tinypy_native_type_spec_t *spec = &type->native_spec;

    (void)memset(&type->native_number_slots, 0, sizeof(type->native_number_slots));
    (void)memset(&type->native_sequence_slots, 0, sizeof(type->native_sequence_slots));
    (void)memset(&type->native_mapping_slots, 0, sizeof(type->native_mapping_slots));
    if (spec->call != NULL) {
        type->call = __tinypy_internal_native_call;
    }
    if (spec->repr != NULL) {
        type->repr = __tinypy_internal_native_repr;
    }
    if (spec->hash != NULL) {
        type->hash = __tinypy_internal_native_hash;
    }
    if (spec->compare != NULL) {
        type->rich_compare = __tinypy_internal_native_compare;
    }
    if (spec->get_attribute != NULL) {
        type->get_attribute = __tinypy_internal_native_get_attribute;
    }
    if (spec->set_attribute != NULL) {
        type->set_attribute = __tinypy_internal_native_set_attribute;
    }
    if (spec->iter != NULL) {
        type->iter = __tinypy_internal_native_iter;
    }
    if (spec->next != NULL) {
        type->next = __tinypy_internal_native_next;
    }
    if (spec->mapping_get != NULL || spec->mapping_set != NULL || spec->mapping_length != NULL) {
        type->native_mapping_slots.get_item = spec->mapping_get != NULL ? __tinypy_internal_native_mapping_get : NULL;
        type->native_mapping_slots.set_item = spec->mapping_set != NULL ? __tinypy_internal_native_mapping_set : NULL;
        type->native_mapping_slots.length = spec->mapping_length != NULL ? __tinypy_internal_native_mapping_length : NULL;
        type->mapping_slots = &type->native_mapping_slots;
    }
    if (spec->sequence_get != NULL || spec->sequence_set != NULL || spec->sequence_length != NULL || spec->contains != NULL) {
        type->native_sequence_slots.get_item = spec->sequence_get != NULL ? __tinypy_internal_native_sequence_get : NULL;
        type->native_sequence_slots.set_item = spec->sequence_set != NULL ? __tinypy_internal_native_sequence_set : NULL;
        type->native_sequence_slots.length = spec->sequence_length != NULL ? __tinypy_internal_native_sequence_length : NULL;
        type->native_sequence_slots.contains = spec->contains != NULL ? __tinypy_internal_native_contains : NULL;
        type->sequence_slots = &type->native_sequence_slots;
    }
    if (spec->negative != NULL || spec->absolute != NULL || spec->add != NULL || spec->subtract != NULL || spec->multiply != NULL || spec->divide != NULL || spec->inplace_add != NULL || spec->inplace_subtract != NULL || spec->inplace_multiply != NULL || spec->inplace_divide != NULL || spec->reflected_add != NULL || spec->reflected_subtract != NULL || spec->reflected_multiply != NULL || spec->reflected_divide != NULL) {
        type->native_number_slots.negative = spec->negative != NULL ? __tinypy_internal_native_negative : NULL;
        type->native_number_slots.absolute = spec->absolute != NULL ? __tinypy_internal_native_absolute : NULL;
        type->native_number_slots.add = spec->add != NULL ? __tinypy_internal_native_add : NULL;
        type->native_number_slots.subtract = spec->subtract != NULL ? __tinypy_internal_native_subtract : NULL;
        type->native_number_slots.multiply = spec->multiply != NULL ? __tinypy_internal_native_multiply : NULL;
        type->native_number_slots.divide = spec->divide != NULL ? __tinypy_internal_native_divide : NULL;
        type->native_number_slots.inplace_add = spec->inplace_add != NULL ? __tinypy_internal_native_inplace_add : NULL;
        type->native_number_slots.inplace_subtract = spec->inplace_subtract != NULL ? __tinypy_internal_native_inplace_subtract : NULL;
        type->native_number_slots.inplace_multiply = spec->inplace_multiply != NULL ? __tinypy_internal_native_inplace_multiply : NULL;
        type->native_number_slots.inplace_divide = spec->inplace_divide != NULL ? __tinypy_internal_native_inplace_divide : NULL;
        type->native_number_slots.reflected_add = spec->reflected_add != NULL ? __tinypy_internal_native_reflected_add : NULL;
        type->native_number_slots.reflected_subtract = spec->reflected_subtract != NULL ? __tinypy_internal_native_reflected_subtract : NULL;
        type->native_number_slots.reflected_multiply = spec->reflected_multiply != NULL ? __tinypy_internal_native_reflected_multiply : NULL;
        type->native_number_slots.reflected_divide = spec->reflected_divide != NULL ? __tinypy_internal_native_reflected_divide : NULL;
        type->number_slots = &type->native_number_slots;
    }
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_internal_native_validate_spec(tinypy_vm_t *vm, const tinypy_native_type_spec_t *spec, tinypy_error_t **out_error) {
    if (spec->abi_version != TINYPY_NATIVE_TYPE_ABI_VERSION || spec->struct_size < offsetof(tinypy_native_type_spec_t, user_data) + sizeof(spec->user_data)) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "native type spec ABI mismatch", out_error);
        return TINYPY_FALSE;
    }
    if (spec->payload_alignment == 0U || (spec->payload_alignment & (spec->payload_alignment - 1U)) != 0U || spec->payload_alignment > TINYPY_INTERNAL_ALIGNMENT) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "native payload alignment is unsupported", out_error);
        return TINYPY_FALSE;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
tinypy_type_t *tinypy_native_type_new(tinypy_vm_t *vm, const char *name, size_t name_size, const tinypy_type_t *const *bases, size_t base_count, tinypy_value_t *namespace_dict, const tinypy_native_type_spec_t *spec, tinypy_error_t **out_error) {
    tinypy_value_t *key = tinypy_internal_name_from_bytes(vm, name, name_size);
    tinypy_type_t *result = tinypy_native_type_new_key(key, bases, base_count, namespace_dict, spec, out_error);

    TINYPY_DECREF(key);
    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_type_t *tinypy_native_type_new_key(tinypy_value_t *name, const tinypy_type_t *const *bases, size_t base_count, tinypy_value_t *namespace_dict, const tinypy_native_type_spec_t *spec, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(name);
    size_t copied_size;
    size_t basic_size;
    tinypy_bool_t has_instance_dict;
    tinypy_bool_t has_weakrefs;

    TINYPY_CLEAR_ERROR(out_error);
    if (__tinypy_internal_native_validate_spec(vm, spec, out_error) == 0) {
        return NULL;
    }
    has_instance_dict = __tinypy_internal_native_spec_bool(spec, offsetof(tinypy_native_type_spec_t, has_instance_dict), spec->has_instance_dict);
    has_weakrefs = __tinypy_internal_native_spec_bool(spec, offsetof(tinypy_native_type_spec_t, has_weakrefs), spec->has_weakrefs);
    tinypy_type_t *type = tinypy_internal_type_new_configured(name, bases, base_count, NULL, namespace_dict, has_instance_dict, has_weakrefs, out_error);
    if (type == NULL) {
        return NULL;
    }
    if (type->slot_count != 0U) {
        tinypy_value_t *type_value = tinypy_type_as_value(type);
        TINYPY_DECREF(type_value);
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "native root types cannot declare Python slots", out_error);
        return NULL;
    }
    if (type->layout_kind == TINYPY_VALUE_NATIVE_INSTANCE && (type->native_payload_size != spec->payload_size || type->native_payload_alignment != spec->payload_alignment)) {
        tinypy_value_t *type_value = tinypy_type_as_value(type);
        TINYPY_DECREF(type_value);
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "native subtype payload layout differs from its base", out_error);
        return NULL;
    }
    tinypy_native_type_spec_init(&type->native_spec);
    copied_size = spec->struct_size < sizeof(type->native_spec) ? spec->struct_size : sizeof(type->native_spec);
    (void)memcpy(&type->native_spec, spec, copied_size);
    type->native_spec.abi_version = TINYPY_NATIVE_TYPE_ABI_VERSION;
    type->native_spec.struct_size = (uint32_t)sizeof(type->native_spec);
    type->layout_kind = TINYPY_VALUE_NATIVE_INSTANCE;
    type->native_payload_offset = offsetof(tinypy_native_instance_object_t, payload);
    type->native_payload_size = spec->payload_size;
    type->native_payload_alignment = spec->payload_alignment;
    basic_size = type->native_payload_offset + type->native_payload_size;
    basic_size = (basic_size + sizeof(void *) - 1U) & ~(sizeof(void *) - 1U);
    type->basic_size = basic_size;
    type->slots_offset = basic_size;
    type->dict_offset = has_instance_dict != 0 ? offsetof(tinypy_native_instance_object_t, dict) : 0U;
    type->weakref_offset = has_weakrefs != 0 ? offsetof(tinypy_native_instance_object_t, weakrefs) : 0U;
    type->has_instance_dict = has_instance_dict;
    type->release_references = tinypy_internal_native_instance_release_references;
    type->traverse_references = tinypy_internal_native_instance_release_references;
    type->destroy = tinypy_internal_native_instance_destroy;
    __tinypy_internal_native_configure_slots(type);
    return type;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_native_type_update_spec(tinypy_type_t *type, const tinypy_native_type_spec_t *spec, tinypy_error_t **out_error) {
    size_t copied_size;
    tinypy_bool_t has_instance_dict;
    tinypy_bool_t has_weakrefs;

    TINYPY_CLEAR_ERROR(out_error);
    if (type->layout_kind != TINYPY_VALUE_NATIVE_INSTANCE) {
        tinypy_internal_make_vm_error(type->vm, TINYPY_ERROR_TYPE, "type does not have a native layout", out_error);
        return TINYPY_FALSE;
    }
    if (__tinypy_internal_native_validate_spec(type->vm, spec, out_error) == 0) {
        return TINYPY_FALSE;
    }
    has_instance_dict = __tinypy_internal_native_spec_bool(spec, offsetof(tinypy_native_type_spec_t, has_instance_dict), spec->has_instance_dict);
    has_weakrefs = __tinypy_internal_native_spec_bool(spec, offsetof(tinypy_native_type_spec_t, has_weakrefs), spec->has_weakrefs);
    if (type->native_payload_size != spec->payload_size || type->native_payload_alignment != spec->payload_alignment || type->native_spec.construct != spec->construct || type->native_spec.finalize != spec->finalize || type->native_spec.user_data != spec->user_data || type->has_instance_dict != has_instance_dict || (type->weakref_offset != 0U) != (has_weakrefs != 0)) {
        tinypy_internal_make_vm_error(type->vm, TINYPY_ERROR_TYPE, "native type layout and lifetime callbacks are immutable", out_error);
        return TINYPY_FALSE;
    }
    copied_size = spec->struct_size < sizeof(type->native_spec) ? spec->struct_size : sizeof(type->native_spec);
    tinypy_native_type_spec_init(&type->native_spec);
    (void)memcpy(&type->native_spec, spec, copied_size);
    type->native_spec.abi_version = TINYPY_NATIVE_TYPE_ABI_VERSION;
    type->native_spec.struct_size = (uint32_t)sizeof(type->native_spec);
    __tinypy_internal_native_configure_slots(type);
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_native_instance_new(tinypy_type_t *type) {
    tinypy_value_t *return_value_1 = tinypy_internal_object_allocate(type->vm, type, type->basic_size);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_native_instance_construct(tinypy_value_t *instance, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    TINYPY_CLEAR_ERROR(out_error);
    tinypy_native_type_spec_t *spec = &instance->type->native_spec;
    if (spec->construct == NULL) {
        return TINYPY_TRUE;
    }
    void *native_payload = __tinypy_internal_native_payload(instance);
    tinypy_bool_t return_value_1 = spec->construct(instance, native_payload, args, kwargs, spec->user_data, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
void *tinypy_native_instance_payload(tinypy_value_t *instance) {
    void *return_value_1 = __tinypy_internal_native_payload(instance);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
const void *tinypy_native_instance_const_payload(const tinypy_value_t *instance) {
    const void *return_value_1 = tinypy_native_instance_payload((tinypy_value_t *)instance);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
const tinypy_native_type_spec_t *tinypy_native_type_spec(const tinypy_type_t *type) {
    return &type->native_spec;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_native_instance_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data) {
    tinypy_internal_instance_release_references(value, visit, user_data);
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_native_instance_finalize(tinypy_value_t *value) {
    tinypy_native_instance_object_t *instance = TINYPY_NATIVE_INSTANCE_OBJECT(value);
    tinypy_native_type_spec_t *spec = &value->type->native_spec;

    if (instance->finalized != 0) {
        return;
    }
    instance->finalized = TINYPY_TRUE;
    if (spec->finalize != NULL) {
        void *native_payload = __tinypy_internal_native_payload(value);
        tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
        tinypy_internal_exception_state_t state;
        tinypy_internal_exception_preserve_begin(vm, &state);
        spec->finalize(value, native_payload, spec->user_data);
        if (vm->state == TINYPY_VM_STATE_LIVE) {
            tinypy_internal_output_unraisable(vm, value);
        }
        tinypy_internal_exception_preserve_end(vm, &state);
    }
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_native_instance_destroy(tinypy_value_t *value) {
    tinypy_internal_native_instance_finalize(value);
}
