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
    function->items_callback = NULL;
    function->minimum = 0U;
    function->maximum = SIZE_MAX;
    function->arity_style = TINYPY_ARITY_STYLE_UNCHECKED;
    function->call_check = TINYPY_NATIVE_CALL_CHECK_NONE;
    function->user_data = user_data;
    function->finalize = finalize;
    function->owner_reference = NULL;
    function->owner_retained = TINYPY_FALSE;
    function->descriptor_kind = TINYPY_NATIVE_DESCRIPTOR_AUTO;
    if (finalize != NULL) {
        vm->native_finalizable_created += 1U;
    }
    return &function->base;
}
//////////////////////////////////////////////////////////////////////////
/* A function of the items convention; minimum and maximum bound the positional
   arguments after self unless the style is TINYPY_ARITY_STYLE_UNCHECKED. */
static tinypy_value_t *__tinypy_native_items_function_new(tinypy_value_t *name, tinypy_native_items_callback_t callback, void *user_data, tinypy_native_function_finalize_t finalize, size_t minimum, size_t maximum, tinypy_arity_style_e style) {
    tinypy_value_t *function = tinypy_native_function_new_key(name, NULL, user_data, finalize);
    tinypy_native_function_object_t *native = TINYPY_NATIVE_FUNCTION_OBJECT(function);

    native->items_callback = callback;
    native->minimum = minimum;
    native->maximum = maximum;
    native->arity_style = style;
    return function;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_native_items_function_new(tinypy_value_t *name, tinypy_native_items_callback_t callback, void *user_data, size_t minimum, size_t maximum, tinypy_arity_style_e style) {
    tinypy_value_t *function = __tinypy_native_items_function_new(name, callback, user_data, NULL, minimum, maximum, style);
    return function;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_native_function_new_items(tinypy_vm_t *vm, const char *name, size_t name_size, tinypy_native_items_callback_t callback, size_t minimum, size_t maximum, void *user_data, tinypy_native_function_finalize_t finalize) {
    tinypy_value_t *key = tinypy_internal_name_from_bytes(vm, name, name_size);
    tinypy_value_t *function = __tinypy_native_items_function_new(key, callback, user_data, finalize, minimum, maximum, TINYPY_ARITY_STYLE_PARSED);
    TINYPY_DECREF(key);
    return function;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_native_function_new_items_keywords(tinypy_vm_t *vm, const char *name, size_t name_size, tinypy_native_items_callback_t callback, void *user_data, tinypy_native_function_finalize_t finalize) {
    tinypy_value_t *key = tinypy_internal_name_from_bytes(vm, name, name_size);
    tinypy_value_t *function = __tinypy_native_items_function_new(key, callback, user_data, finalize, 0U, SIZE_MAX, TINYPY_ARITY_STYLE_UNCHECKED);
    TINYPY_DECREF(key);
    return function;
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
void tinypy_internal_type_add_items_method(tinypy_type_t *type, tinypy_value_t *name, tinypy_native_items_callback_t callback, void *user_data, size_t minimum, size_t maximum, tinypy_arity_style_e style) {
    tinypy_value_t *function = tinypy_internal_native_items_function_new(name, callback, user_data, minimum, maximum, style);
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
/* Built-in class methods become classmethod_descriptor objects of their type
   and bind as built-in methods of the class. */
void tinypy_internal_type_add_class_method(tinypy_type_t *type, tinypy_value_t *name, tinypy_native_function_callback_t callback, void *user_data, tinypy_native_function_finalize_t finalize) {
    tinypy_internal_type_add_method(type, name, callback, user_data, finalize, TINYPY_NATIVE_DESCRIPTOR_CLASS_METHOD);
}
//////////////////////////////////////////////////////////////////////////
/* The type's dictionary holds a static method of a built-in type as the
   function itself, as add_tp_new_wrapper stores the __new__ of a type. */
void tinypy_internal_type_add_static_method(tinypy_type_t *type, tinypy_value_t *name, tinypy_native_function_callback_t callback, void *user_data, tinypy_native_function_finalize_t finalize) {
    tinypy_internal_type_add_method(type, name, callback, user_data, finalize, TINYPY_NATIVE_DESCRIPTOR_STATIC);
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
    if (function->owner_reference != NULL) {
        visit(function->owner_reference, user_data);
    }
    if (function->owner_retained != 0) {
        visit(&function->owner->base.base, user_data);
    }
}
//////////////////////////////////////////////////////////////////////////
/* The memory sweep of tinypy_vm_destroy runs no finalizer. */
void tinypy_internal_native_function_destroy(tinypy_value_t *value) {
    if (TINYPY_VALUE_VM(value)->state == TINYPY_VM_STATE_LIVE) {
        tinypy_internal_native_function_finalize(value);
    }
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_native_function_finalize(tinypy_value_t *value) {
    tinypy_native_function_object_t *function = TINYPY_NATIVE_FUNCTION_OBJECT(value);

    if (function->finalize != NULL) {
        tinypy_native_function_finalize_t finalize = function->finalize;
        void *user_data = function->user_data;

        function->finalize = NULL;
        function->user_data = NULL;
        function->call_check = TINYPY_NATIVE_CALL_CHECK_FINALIZED;
        tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
        tinypy_internal_exception_state_t state;
        tinypy_internal_exception_preserve_begin(vm, &state);
        finalize(user_data);
        if (vm->state == TINYPY_VM_STATE_LIVE) {
            tinypy_internal_output_unraisable(vm, value);
        }
        tinypy_internal_exception_preserve_restore(vm, &state);
    }
}
//////////////////////////////////////////////////////////////////////////
/* The type a method descriptor belongs to, NULL for a plain function. A heap
   type owns its descriptors through its dictionary and is only referenced
   weakly by them, so it reads as NULL once it no longer exists. */
tinypy_type_t *tinypy_internal_native_function_owner(const tinypy_value_t *function) {
    const tinypy_native_function_object_t *native = TINYPY_NATIVE_FUNCTION_OBJECT((tinypy_value_t *)function);

    if (native->owner_reference != NULL) {
        tinypy_type_t *owner = (tinypy_type_t *)TINYPY_WEAKREF_OBJECT(native->owner_reference)->object;
        return owner;
    }
    return native->owner;
}
//////////////////////////////////////////////////////////////////////////
/* Names the owner type of a method descriptor in a message, or the type that
   no longer exists, of which no object can be an instance. */
static void __tinypy_native_function_owner_name(tinypy_value_t *callable, tinypy_message_part_t out_parts[3]) {
    tinypy_type_t *owner = tinypy_internal_native_function_owner(callable);

    if (owner != NULL) {
        tinypy_internal_type_message_name(owner, out_parts);
        return;
    }
    out_parts[0] = (tinypy_message_part_t)TINYPY_MESSAGE_PART_LITERAL("");
    out_parts[1] = (tinypy_message_part_t)TINYPY_MESSAGE_PART_LITERAL("");
    out_parts[2] = (tinypy_message_part_t)TINYPY_MESSAGE_PART_LITERAL("<deleted type>");
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_native_function_check_receiver(tinypy_value_t *callable, tinypy_value_t *receiver, tinypy_bool_t binding, tinypy_error_t **out_error) {
    tinypy_native_function_object_t *function = TINYPY_NATIVE_FUNCTION_OBJECT(callable);
    tinypy_type_t *owner_type = tinypy_internal_native_function_owner(callable);
    if (function->owner == NULL || (owner_type != NULL && receiver != NULL && tinypy_type_is_subtype(receiver->type, owner_type) != TINYPY_FALSE)) {
        return TINYPY_TRUE;
    }
    tinypy_vm_t *vm = TINYPY_VALUE_VM(callable);
    /* methoddescr_call shows the type names as "%.100s", descr_check whole. */
    size_t type_precision = binding != TINYPY_FALSE ? SIZE_MAX : 100U;
    tinypy_message_part_t owner[3];

    __tinypy_native_function_owner_name(callable, owner);
    tinypy_internal_message_parts_precision(owner, 3U, type_precision);
    if (receiver == NULL) {
        tinypy_message_part_t parts[] = {
            TINYPY_MESSAGE_PART_LITERAL("descriptor '"), TINYPY_MESSAGE_PART_TEXT(function->name),
            TINYPY_MESSAGE_PART_LITERAL("' of '"), owner[0], owner[1], owner[2],
            TINYPY_MESSAGE_PART_LITERAL("' object needs an argument"),
        };
        tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
    }
    else {
        tinypy_message_part_t receiver_name[3];

        tinypy_internal_type_message_name(receiver->type, receiver_name);
        tinypy_internal_message_parts_precision(receiver_name, 3U, type_precision);
        tinypy_message_part_t parts[] = {
            TINYPY_MESSAGE_PART_LITERAL("descriptor '"), TINYPY_MESSAGE_PART_TEXT(function->name),
            TINYPY_MESSAGE_PART_LITERAL("' requires a '"), owner[0], owner[1], owner[2],
            TINYPY_MESSAGE_PART_LITERAL("' object but received a '"), receiver_name[0], receiver_name[1], receiver_name[2],
            TINYPY_MESSAGE_PART_LITERAL("'"),
        };
        if (binding != TINYPY_FALSE) {
            parts[2].bytes = "' for '";
            parts[2].size = sizeof("' for '") - 1U;
            parts[6].bytes = "' objects doesn't apply to '";
            parts[6].size = sizeof("' objects doesn't apply to '") - 1U;
            parts[10].bytes = "' object";
            parts[10].size = sizeof("' object") - 1U;
        }
        tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
    }
    return TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_native_function_result_error(tinypy_value_t *callable, tinypy_value_t *result, tinypy_error_t **out_error) {
    if (result == NULL && (out_error == NULL || *out_error == NULL)) {
        tinypy_vm_t *vm = TINYPY_VALUE_VM(callable);
        if (tinypy_vm_has_error(vm) != 0) {
            tinypy_internal_exception_make_diagnostic(vm, out_error);
        }
        else {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_RUNTIME, "native function failed without an error", out_error);
        }
    }
}
//////////////////////////////////////////////////////////////////////////
/* The function that owns the user data of a call: a bound method reads it
   from its descriptor, whose finalizer releases it. */
static const tinypy_native_function_object_t *__tinypy_native_function_source(const tinypy_value_t *callable) {
    const tinypy_native_function_object_t *function = TINYPY_NATIVE_FUNCTION_OBJECT((tinypy_value_t *)callable);

    if (function->function != NULL) {
        const tinypy_native_function_object_t *descriptor = TINYPY_NATIVE_FUNCTION_OBJECT(function->function);
        return descriptor;
    }
    return function;
}
//////////////////////////////////////////////////////////////////////////
/* Out of line, so that the slots it guards keep their small frames. */
TINYPY_NOINLINE void tinypy_internal_native_instance_finalized_error(tinypy_value_t *instance, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(instance);
    tinypy_message_part_t parts[] = {
        TINYPY_MESSAGE_PART_LITERAL("'"),
        TINYPY_MESSAGE_PART_TYPE_NAME(instance),
        TINYPY_MESSAGE_PART_LITERAL("' object was finalized"),
    };

    tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_RUNTIME, parts, sizeof(parts) / sizeof(parts[0]), out_error);
}
//////////////////////////////////////////////////////////////////////////
/* A native instance whose finalizer ran, as tinypy_vm_destroy runs it on a
   reachable instance, has no host object left for the callbacks of its
   type, as a finalized function has no user data. */
static TINYPY_ALWAYS_INLINE tinypy_bool_t __tinypy_internal_native_instance_check_live(tinypy_value_t *instance, tinypy_error_t **out_error) {
    if (TINYPY_NATIVE_INSTANCE_OBJECT(instance)->finalization != TINYPY_NATIVE_FINALIZATION_DONE) {
        return TINYPY_TRUE;
    }
    tinypy_internal_native_instance_finalized_error(instance, out_error);
    return TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
/* A finalized function has no user data left for its callback, and a method
   of a host native type no payload once its receiver is finalized. An
   accessor, called directly as well, takes any first argument or none. */
static tinypy_bool_t __tinypy_native_function_check_live(tinypy_value_t *callable, const tinypy_native_function_object_t *source, tinypy_value_t *receiver, tinypy_error_t **out_error) {
    if (source->call_check == TINYPY_NATIVE_CALL_CHECK_NONE) {
        return TINYPY_TRUE;
    }
    if (source->call_check == TINYPY_NATIVE_CALL_CHECK_RECEIVER) {
        tinypy_bool_t live = __tinypy_internal_native_instance_check_live(receiver, out_error);
        return live;
    }
    if (source->call_check == TINYPY_NATIVE_CALL_CHECK_ACCESSOR) {
        if (receiver == NULL || TINYPY_VALUE_KIND(receiver) != TINYPY_VALUE_NATIVE_INSTANCE) {
            return TINYPY_TRUE;
        }
        tinypy_bool_t live = __tinypy_internal_native_instance_check_live(receiver, out_error);
        return live;
    }
    tinypy_vm_t *vm = TINYPY_VALUE_VM(callable);
    tinypy_message_part_t parts[] = {
        TINYPY_MESSAGE_PART_LITERAL("native function '"),
        TINYPY_MESSAGE_PART_TEXT(source->name),
        TINYPY_MESSAGE_PART_LITERAL("' was finalized"),
    };

    tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_RUNTIME, parts, sizeof(parts) / sizeof(parts[0]), out_error);
    return TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_native_function_invoke(tinypy_value_t *callable, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_native_function_object_t *function = TINYPY_NATIVE_FUNCTION_OBJECT(callable);
    const tinypy_native_function_object_t *source = __tinypy_native_function_source(callable);
    tinypy_value_t *receiver = source->call_check == TINYPY_NATIVE_CALL_CHECK_RECEIVER || (source->call_check == TINYPY_NATIVE_CALL_CHECK_ACCESSOR && TINYPY_TUPLE_SIZE(args) != 0U) ? TINYPY_TUPLE_GET(args, 0U) : NULL;

    if (__tinypy_native_function_check_live(callable, source, receiver, out_error) == TINYPY_FALSE) {
        return NULL;
    }
    tinypy_value_t *result = function->callback(callable, args, kwargs, source->user_data, out_error);

    __tinypy_native_function_result_error(callable, result, out_error);
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* Runs an items callback after the argument check its function declares. */
static tinypy_value_t *__tinypy_native_function_invoke_items(tinypy_value_t *callable, tinypy_value_t *self, tinypy_value_t *const *items, size_t count, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_native_function_object_t *function = TINYPY_NATIVE_FUNCTION_OBJECT(callable);
    const tinypy_native_function_object_t *source = __tinypy_native_function_source(callable);
    tinypy_value_t *receiver = self == NULL && source->call_check == TINYPY_NATIVE_CALL_CHECK_ACCESSOR && count != 0U ? items[0] : self;

    if (__tinypy_native_function_check_live(callable, source, receiver, out_error) == TINYPY_FALSE) {
        return NULL;
    }
    if (function->arity_style != TINYPY_ARITY_STYLE_UNCHECKED && tinypy_internal_native_arguments_check(callable, count, kwargs, function->minimum, function->maximum, function->arity_style, out_error) == TINYPY_FALSE) {
        return NULL;
    }
    tinypy_value_t *result = function->items_callback(callable, self, items, count, kwargs, out_error);

    __tinypy_native_function_result_error(callable, result, out_error);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_native_arguments_error(tinypy_value_t *function, size_t supplied, tinypy_value_t *kwargs, size_t minimum, size_t maximum, tinypy_arity_style_e style, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *name = TINYPY_NATIVE_FUNCTION_OBJECT(function)->name;
    tinypy_native_function_object_t *native = TINYPY_NATIVE_FUNCTION_OBJECT(function);
    tinypy_bool_t wrapper = function->type == vm->native_wrapper_descriptor_type
        || (native->function != NULL && native->function->type == vm->native_wrapper_descriptor_type);

    if (kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U) {
        tinypy_message_part_t parts[] = {
            {wrapper != TINYPY_FALSE ? "wrapper " : "", wrapper != TINYPY_FALSE ? 8U : 0U},
            TINYPY_MESSAGE_PART_TEXT(name),
            {wrapper != TINYPY_FALSE ? " doesn't take keyword arguments" : "() takes no keyword arguments", wrapper != TINYPY_FALSE ? sizeof(" doesn't take keyword arguments") - 1U : sizeof("() takes no keyword arguments") - 1U},
        };

        tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
        return;
    }
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
}
//////////////////////////////////////////////////////////////////////////
/* The PyArg_ParseTuple-style check of the positional arguments after self. */
tinypy_bool_t tinypy_internal_native_arguments_check(tinypy_value_t *function, size_t count, tinypy_value_t *kwargs, size_t minimum, size_t maximum, tinypy_arity_style_e style, tinypy_error_t **out_error) {
    if ((kwargs == NULL || TINYPY_DICT_SIZE(kwargs) == 0U) && count >= minimum && count <= maximum) {
        return TINYPY_TRUE;
    }
    __tinypy_native_arguments_error(function, count, kwargs, minimum, maximum, style, out_error);
    return TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_native_method_arguments(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, size_t minimum, size_t maximum, tinypy_arity_style_e style, tinypy_error_t **out_error) {
    size_t count = TINYPY_TUPLE_SIZE(args);

    if (count == 0U) {
        __tinypy_native_arguments_error(function, 0U, kwargs, minimum, maximum, style, out_error);
        return TINYPY_FALSE;
    }
    tinypy_bool_t result = tinypy_internal_native_arguments_check(function, count - 1U, kwargs, minimum, maximum, style, out_error);
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* As PyEval_EvalCodeEx, too many arguments are reported first, then in
   dictionary order a keyword that is not a str or unicode, names no
   parameter or names a bound one, and then an unbound required parameter,
   counting the bound ones as given. Binding runs no code, so the keywords
   are read in place. */
tinypy_value_t *tinypy_internal_native_bind_arguments(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_value_t *const *names, size_t count, size_t required, tinypy_bool_t variadic, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *name = TINYPY_NATIVE_FUNCTION_OBJECT(function)->name;
    size_t positional_count = TINYPY_TUPLE_SIZE(args);
    size_t keyword_count = kwargs != NULL ? TINYPY_DICT_SIZE(kwargs) : 0U;
    size_t given = positional_count < count ? positional_count : count;

    if (variadic == 0 && (positional_count > count || (count == 0U && keyword_count != 0U))) {
        tinypy_internal_make_arity_error(vm, (const char *)TINYPY_TEXT_BYTES(name), TINYPY_TEXT_BYTE_SIZE(name), positional_count + keyword_count, required, count, TINYPY_ARITY_STYLE_PARSED, out_error);
        return NULL;
    }
    if (keyword_count == 0U && given >= required) {
        tinypy_value_t *result = TINYPY_RET(args);
        return result;
    }
    size_t size = positional_count > count ? positional_count : count;
    tinypy_value_t *bound = tinypy_internal_tuple_new_checked(vm, size, out_error);
    uint32_t taken = given != 0U ? UINT32_MAX >> (32U - given) : 0U;

    if (bound == NULL) {
        return NULL;
    }
    for (size_t index = 0U; index < positional_count; ++index) {
        tinypy_tuple_set(bound, index, TINYPY_TUPLE_GET(args, index));
    }
    tinypy_dict_entry_t *entry = keyword_count != 0U ? TINYPY_DICT_ITERATOR_BEGIN(kwargs) : NULL;
    tinypy_dict_entry_t *end = keyword_count != 0U ? TINYPY_DICT_ITERATOR_END(kwargs) : NULL;

    for (; entry != end; ++entry) {
        size_t index = 0U;

        if (TINYPY_DICT_ENTRY_IS_ACTIVE(entry) == 0) {
            continue;
        }
        if (TINYPY_VALUE_KIND(entry->key) != TINYPY_VALUE_STRING && TINYPY_VALUE_KIND(entry->key) != TINYPY_VALUE_UNICODE) {
            tinypy_message_part_t parts[] = {
                TINYPY_MESSAGE_PART_TEXT_PRECISION(name, 200U),
                TINYPY_MESSAGE_PART_LITERAL("() keywords must be strings")
            };

            tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, 2U, out_error);
            goto failed;
        }
        while (index < count && TINYPY_NAME_EQ(entry->key, names[index]) == 0) {
            index += 1U;
        }
        if (index == count || (taken & (UINT32_C(1) << index)) != 0U) {
            tinypy_value_t *keyword = tinypy_internal_keyword_as_string(vm, entry->key, out_error);

            if (keyword != NULL) {
                tinypy_message_part_t parts[] = {
                    TINYPY_MESSAGE_PART_TEXT_PRECISION(name, 200U),
                    {index == count ? "() got an unexpected keyword argument '" : "() got multiple values for keyword argument '", index == count ? 39U : 45U},
                    TINYPY_MESSAGE_PART_TEXT_PRECISION(keyword, 400U),
                    TINYPY_MESSAGE_PART_LITERAL("'")
                };

                tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, 4U, out_error);
                TINYPY_DECREF(keyword);
            }
            goto failed;
        }
        tinypy_tuple_set(bound, index, entry->value);
        taken |= UINT32_C(1) << index;
        given += 1U;
    }
    for (size_t index = 0U; index < required; ++index) {
        if ((taken & (UINT32_C(1) << index)) == 0U) {
            char required_buffer[TINYPY_MESSAGE_SIZE_BUFFER];
            size_t required_size = tinypy_internal_format_size(required_buffer, required);
            char given_buffer[TINYPY_MESSAGE_SIZE_BUFFER];
            size_t given_size = tinypy_internal_format_size(given_buffer, given);
            tinypy_bool_t exact = variadic == 0 && required == count ? TINYPY_TRUE : TINYPY_FALSE;
            tinypy_message_part_t parts[] = {
                TINYPY_MESSAGE_PART_TEXT_PRECISION(name, 200U),
                {exact != 0 ? "() takes exactly " : "() takes at least ", exact != 0 ? 17U : 18U},
                {required_buffer, required_size},
                {required == 1U ? " argument (" : " arguments (", required == 1U ? 11U : 12U},
                {given_buffer, given_size},
                TINYPY_MESSAGE_PART_LITERAL(" given)")
            };

            tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, 6U, out_error);
            goto failed;
        }
    }
    if (required != count && (taken & (UINT32_C(1) << (count - 1U))) == 0U) {
        tinypy_value_t *const *items = tinypy_internal_tuple_items(bound);
        tinypy_value_t *result = tinypy_internal_tuple_from_items_checked(vm, items, count - 1U, out_error);

        TINYPY_DECREF(bound);
        return result;
    }
    return bound;

failed:
    TINYPY_DECREF(bound);
    return NULL;
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

    if (function->items_callback != NULL) {
        tinypy_value_t *const *items = tinypy_internal_tuple_items(args);
        size_t count = TINYPY_TUPLE_SIZE(args);
        tinypy_value_t *self = function->self;

        if (self == NULL && function->owner != NULL) {
            self = items[0];
            items += 1;
            count -= 1U;
        }
        /* The argument tuple keeps the items, but the caller may hold the
           callable borrowed, as a property holds its getter, and a bound
           receiver lives only as long as the callable. */
        TINYPY_INCREF(callable);
        tinypy_value_t *result = __tinypy_native_function_invoke_items(callable, self, items, count, kwargs, out_error);
        TINYPY_DECREF(callable);
        return result;
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
/* Fills an argument tuple that an earlier native call released unshared. */
static tinypy_value_t *__tinypy_native_arguments_acquire(tinypy_vm_t *vm, tinypy_value_t *first, tinypy_value_t *const *items, size_t count, tinypy_error_t **out_error) {
    size_t size = (first != NULL ? 1U : 0U) + count;

    if (size == 0U) {
        tinypy_value_t *empty = TINYPY_RET_EMPTY_TUPLE(vm);
        return empty;
    }
    if (size > TINYPY_NATIVE_ARGUMENT_CACHE_SIZE || vm->native_argument_tuples[size - 1U] == NULL) {
        tinypy_value_t *created = tinypy_internal_tuple_join_items_checked(vm, first, items, count, NULL, 0U, out_error);

        return created;
    }
    tinypy_value_t *args = vm->native_argument_tuples[size - 1U];
    tinypy_value_t **slots = TINYPY_TUPLE_OBJECT(args)->items;
    size_t offset = size - count;

    vm->native_argument_tuples[size - 1U] = NULL;
#if defined(TINYPY_CYCLE_DIAGNOSTICS)
    __tinypy_internal_cycle_diagnostics_value_reuse(vm, args);
#endif
    if (first != NULL) {
        slots[0] = TINYPY_RET(first);
    }
    for (size_t index = 0U; index < count; ++index) {
        slots[offset + index] = TINYPY_RET(items[index]);
    }
    TINYPY_SIZED_SIZE(args) = size;
    return args;
}
//////////////////////////////////////////////////////////////////////////
/* Frees an emptied argument tuple of the given item count. */
static void __tinypy_native_arguments_free(tinypy_vm_t *vm, tinypy_value_t *args, size_t size) {
    TINYPY_SIZED_SIZE(args) = size;
    args->type->base.base.ref -= 1;
#if defined(TINYPY_CYCLE_DIAGNOSTICS)
    __tinypy_internal_cycle_diagnostics_value_unregister(vm, args);
#endif
    size_t allocation_size = tinypy_internal_value_allocation_size(args);
    tinypy_internal_vm_deallocate(vm, args, allocation_size);
}
//////////////////////////////////////////////////////////////////////////
/* A tuple the callback did not keep is emptied, so code its items run while
   they are released cannot reach it, and then cached for the next call. The
   native call path keeps it inline although the public release shares it. */
static TINYPY_ALWAYS_INLINE void __tinypy_native_arguments_release(tinypy_vm_t *vm, tinypy_value_t *args) {
    size_t size = TINYPY_TUPLE_SIZE(args);

    if (TINYPY_REFCNT(args) != 1U || size == 0U || size > TINYPY_NATIVE_ARGUMENT_CACHE_SIZE || vm->state != TINYPY_VM_STATE_LIVE) {
        TINYPY_DECREF(args);
        return;
    }
    tinypy_value_t **items = TINYPY_TUPLE_OBJECT(args)->items;

    TINYPY_SIZED_SIZE(args) = 0U;
    for (size_t index = 0U; index < size; ++index) {
        TINYPY_DECREF(items[index]);
    }
    if (vm->native_argument_tuples[size - 1U] != NULL) {
        __tinypy_native_arguments_free(vm, args, size);
        return;
    }
    vm->native_argument_tuples[size - 1U] = args;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_native_arguments_acquire(tinypy_vm_t *vm, tinypy_value_t *self, tinypy_value_t *const *items, size_t count, tinypy_error_t **out_error) {
    TINYPY_CLEAR_ERROR(out_error);
    tinypy_value_t *args = __tinypy_native_arguments_acquire(vm, self, items, count, out_error);
    return args;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_native_arguments_release(tinypy_value_t *args) {
    __tinypy_native_arguments_release(TINYPY_VALUE_VM(args), args);
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_native_argument_cache_finalize(tinypy_vm_t *vm) {
    for (size_t index = 0U; index < TINYPY_NATIVE_ARGUMENT_CACHE_SIZE; ++index) {
        tinypy_value_t *args = vm->native_argument_tuples[index];

        if (args == NULL) {
            continue;
        }
        vm->native_argument_tuples[index] = NULL;
        __tinypy_native_arguments_free(vm, args, index + 1U);
    }
}
//////////////////////////////////////////////////////////////////////////
/* An items callback takes self and the items as they are; a tuple callback
   takes one owned argument tuple of both, which it may retain. Its public ABI
   stays unchanged. */
static TINYPY_ALWAYS_INLINE tinypy_value_t *__tinypy_native_function_invoke_self(tinypy_vm_t *vm, tinypy_value_t *callable, tinypy_value_t *self, tinypy_value_t *const *items, size_t count, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_native_function_object_t *function = TINYPY_NATIVE_FUNCTION_OBJECT(callable);

    if (function->items_callback != NULL) {
        tinypy_value_t *result = __tinypy_native_function_invoke_items(callable, self, items, count, kwargs, out_error);
        return result;
    }
    tinypy_value_t *args = __tinypy_native_arguments_acquire(vm, self, items, count, out_error);
    if (args == NULL) {
        return NULL;
    }
    tinypy_value_t *result = __tinypy_native_function_invoke(callable, args, kwargs, out_error);
    __tinypy_native_arguments_release(vm, args);
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* A called built-in function takes a recursion level for the Python calls
   it makes. */
static tinypy_value_t *__tinypy_native_function_call_self(tinypy_vm_t *vm, tinypy_value_t *callable, tinypy_value_t *self, tinypy_value_t *const *items, size_t count, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    vm->evaluation_depth += 1U;
    tinypy_value_t *result = __tinypy_native_function_invoke_self(vm, callable, self, items, count, kwargs, out_error);
    vm->evaluation_depth -= 1U;
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* The call of a built-in function by the evaluation loop: like a C function
   called by ceval it takes no recursion check, so lowering the limit below
   the current depth fails only the next deeper call; its level still counts
   for the Python calls it makes. */
tinypy_value_t *tinypy_internal_native_function_call_items(tinypy_value_t *callable, tinypy_value_t *const *items, size_t count, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(callable);
    tinypy_native_function_object_t *function = TINYPY_NATIVE_FUNCTION_OBJECT(callable);
    TINYPY_CLEAR_ERROR(out_error);
    tinypy_value_t *self = function->self;
    if (self == NULL && function->owner != NULL) {
        if (__tinypy_native_function_check_receiver(callable, count != 0U ? items[0] : NULL, TINYPY_FALSE, out_error) == TINYPY_FALSE) {
            return NULL;
        }
        self = items[0];
        items += 1;
        count -= 1U;
    }
    tinypy_value_t *result = __tinypy_native_function_call_self(vm, callable, self, items, count, kwargs, out_error);
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* An unbound method descriptor applies to the instances of its owner. */
static tinypy_bool_t __tinypy_native_function_applies(tinypy_value_t *descriptor, tinypy_value_t *instance) {
    tinypy_native_function_object_t *function = TINYPY_NATIVE_FUNCTION_OBJECT(descriptor);

    if (function->self != NULL) {
        return TINYPY_FALSE;
    }
    tinypy_type_t *owner = tinypy_internal_native_function_owner(descriptor);
    tinypy_bool_t result = function->owner == NULL || (owner != NULL && tinypy_type_is_subtype(instance->type, owner) != TINYPY_FALSE) ? TINYPY_TRUE : TINYPY_FALSE;

    return result;
}
//////////////////////////////////////////////////////////////////////////
/* Whether a method call may run a method descriptor with the instance as self
   instead of the built-in method binding would create. */
tinypy_bool_t tinypy_internal_native_method_calls_unbound(tinypy_value_t *descriptor, tinypy_value_t *instance) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(descriptor);

    if (descriptor->type != vm->native_method_descriptor_type || TINYPY_NATIVE_FUNCTION_OBJECT(descriptor)->items_callback == NULL) {
        return TINYPY_FALSE;
    }
    tinypy_bool_t result = __tinypy_native_function_applies(descriptor, instance);

    return result;
}
//////////////////////////////////////////////////////////////////////////
/* Whether a method or slot wrapper descriptor may run with the instance as
   self, as the C slot it stands for does, instead of the built-in method
   binding would create. */
tinypy_bool_t tinypy_internal_native_method_applies(tinypy_value_t *descriptor, tinypy_value_t *instance) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(descriptor);

    if (descriptor->type != vm->native_method_descriptor_type && descriptor->type != vm->native_wrapper_descriptor_type) {
        return TINYPY_FALSE;
    }
    tinypy_bool_t result = __tinypy_native_function_applies(descriptor, instance);

    return result;
}
//////////////////////////////////////////////////////////////////////////
/* Calls a method descriptor as the built-in method binding self would. */
tinypy_value_t *tinypy_internal_native_function_call_method(tinypy_value_t *descriptor, tinypy_value_t *self, tinypy_value_t *const *items, size_t count, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(descriptor);
    TINYPY_CLEAR_ERROR(out_error);
    if (tinypy_internal_recursion_check(vm, TINYPY_NATIVE_STACK_ADDRESS(), "maximum recursion depth exceeded while calling a Python object", out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *result = __tinypy_native_function_call_self(vm, descriptor, self, items, count, kwargs, out_error);
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* Runs a method or slot wrapper descriptor that applies to self, found under
   the name of a slot, as the slot of the type does. update_one_slot installs
   the C function of the slot wrapper of that name itself, such as
   BaseException_str under PyObject_Str: the caller guards the recursion,
   and the slot takes no level of its own. Any other built-in method runs
   through the slot function, whose call takes a level. */
tinypy_value_t *tinypy_internal_native_function_call_slot(tinypy_value_t *descriptor, tinypy_value_t *name, tinypy_value_t *self, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(descriptor);

    if (descriptor->type != vm->native_wrapper_descriptor_type || TINYPY_NAME_EQ(TINYPY_NATIVE_FUNCTION_OBJECT(descriptor)->name, name) == 0) {
        tinypy_value_t *called = tinypy_internal_native_function_call_method(descriptor, self, NULL, 0U, NULL, out_error);
        return called;
    }
    TINYPY_CLEAR_ERROR(out_error);
    tinypy_value_t *result = __tinypy_native_function_invoke_self(vm, descriptor, self, NULL, 0U, NULL, out_error);
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
/* Creates a built-in method with self bound, reusing the method free list. */
static tinypy_value_t *__tinypy_native_function_bind(tinypy_value_t *descriptor, tinypy_value_t *instance, tinypy_error_t **out_error) {
    tinypy_native_function_object_t *function = TINYPY_NATIVE_FUNCTION_OBJECT(descriptor);
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
    method->items_callback = function->items_callback;
    method->minimum = function->minimum;
    method->maximum = function->maximum;
    method->arity_style = function->arity_style;
    method->call_check = TINYPY_NATIVE_CALL_CHECK_NONE;
    method->user_data = function->user_data;
    method->finalize = NULL;
    method->owner_reference = NULL;
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
tinypy_value_t *tinypy_internal_native_function_descriptor_get(tinypy_value_t *descriptor, tinypy_value_t *instance, tinypy_value_t *owner, tinypy_error_t **out_error) {
    tinypy_native_function_object_t *function = TINYPY_NATIVE_FUNCTION_OBJECT(descriptor);

    TINYPY_CLEAR_ERROR(out_error);
    if (instance == NULL || function->self != NULL) {
        return TINYPY_RET(descriptor);
    }
    if (__tinypy_native_function_check_receiver(descriptor, instance, TINYPY_TRUE, out_error) == TINYPY_FALSE) {
        return NULL;
    }

    (void)owner;

    tinypy_value_t *method = __tinypy_native_function_bind(descriptor, instance, out_error);
    return method;
}
//////////////////////////////////////////////////////////////////////////
/* classmethod_get: the class must be a type deriving from the method's
   owner, and becomes the self of a built-in method. */
tinypy_value_t *tinypy_internal_native_function_bind_class(tinypy_value_t *descriptor, tinypy_value_t *class_value, tinypy_error_t **out_error) {
    tinypy_native_function_object_t *function = TINYPY_NATIVE_FUNCTION_OBJECT(descriptor);
    tinypy_vm_t *vm = TINYPY_VALUE_VM(descriptor);

    TINYPY_CLEAR_ERROR(out_error);
    tinypy_type_t *owner = tinypy_internal_native_function_owner(descriptor);
    if (TINYPY_VALUE_KIND(class_value) != TINYPY_VALUE_TYPE || owner == NULL || tinypy_type_is_subtype((tinypy_type_t *)class_value, owner) == 0) {
        tinypy_bool_t is_type = TINYPY_VALUE_KIND(class_value) == TINYPY_VALUE_TYPE ? TINYPY_TRUE : TINYPY_FALSE;
        tinypy_message_part_t owner_name[3];
        tinypy_message_part_t class_name[3];

        __tinypy_native_function_owner_name(descriptor, owner_name);
        tinypy_internal_type_message_name(is_type != 0 ? (tinypy_type_t *)class_value : class_value->type, class_name);
        tinypy_message_part_t parts[] = {
            TINYPY_MESSAGE_PART_LITERAL("descriptor '"), TINYPY_MESSAGE_PART_TEXT(function->name),
            TINYPY_MESSAGE_PART_LITERAL("' for type '"), owner_name[0], owner_name[1], owner_name[2],
            TINYPY_MESSAGE_PART_LITERAL("' doesn't apply to type '"), class_name[0], class_name[1], class_name[2],
            TINYPY_MESSAGE_PART_LITERAL("'"),
        };

        if (is_type == 0) {
            parts[6].bytes = "' needs a type, not a '";
            parts[6].size = sizeof("' needs a type, not a '") - 1U;
            parts[10].bytes = "' as arg 2";
            parts[10].size = sizeof("' as arg 2") - 1U;
        }
        tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
        return NULL;
    }
    tinypy_value_t *method = __tinypy_native_function_bind(descriptor, class_value, out_error);
    return method;
}
//////////////////////////////////////////////////////////////////////////
/* classmethod_get: the class the descriptor is found through, or the type of
   the instance. */
static tinypy_value_t *__tinypy_native_class_method_descriptor_get(tinypy_value_t *descriptor, tinypy_value_t *instance, tinypy_value_t *owner, tinypy_error_t **out_error) {
    tinypy_value_t *class_value = owner != NULL ? owner : &instance->type->base.base;
    tinypy_value_t *method = tinypy_internal_native_function_bind_class(descriptor, class_value, out_error);

    return method;
}
//////////////////////////////////////////////////////////////////////////
/* classmethoddescr_call: the first argument is the class, which receives the
   call like the self of a bound built-in method. */
static tinypy_value_t *__tinypy_native_class_method_descriptor_call(tinypy_value_t *callable, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_native_function_object_t *function = TINYPY_NATIVE_FUNCTION_OBJECT(callable);
    tinypy_vm_t *vm = TINYPY_VALUE_VM(callable);

    if (TINYPY_TUPLE_SIZE(args) == 0U) {
        (void)__tinypy_native_function_check_receiver(callable, NULL, TINYPY_FALSE, out_error);
        return NULL;
    }
    tinypy_value_t *class_value = TINYPY_TUPLE_GET(args, 0U);
    tinypy_message_part_t received_name[3];

    /* classmethoddescr_call shows the type names as "%.100s". */
    tinypy_internal_type_message_name(class_value->type, received_name);
    tinypy_internal_message_parts_precision(received_name, 3U, 100U);
    if (TINYPY_VALUE_KIND(class_value) != TINYPY_VALUE_TYPE) {
        tinypy_message_part_t parts[] = {
            TINYPY_MESSAGE_PART_LITERAL("descriptor '"), TINYPY_MESSAGE_PART_TEXT(function->name),
            TINYPY_MESSAGE_PART_LITERAL("' requires a type but received a '"), received_name[0], received_name[1], received_name[2],
            TINYPY_MESSAGE_PART_LITERAL("'"),
        };

        tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
        return NULL;
    }
    tinypy_type_t *owner = tinypy_internal_native_function_owner(callable);
    if (owner == NULL || tinypy_type_is_subtype((tinypy_type_t *)class_value, owner) == 0) {
        tinypy_message_part_t owner_name[3];

        __tinypy_native_function_owner_name(callable, owner_name);
        tinypy_internal_message_parts_precision(owner_name, 3U, 100U);
        tinypy_message_part_t parts[] = {
            TINYPY_MESSAGE_PART_LITERAL("descriptor '"), TINYPY_MESSAGE_PART_TEXT(function->name),
            TINYPY_MESSAGE_PART_LITERAL("' requires a subtype of '"), owner_name[0], owner_name[1], owner_name[2],
            TINYPY_MESSAGE_PART_LITERAL("' but received '"), received_name[0], received_name[1], received_name[2],
        };

        tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
        return NULL;
    }
    if (function->items_callback != NULL) {
        /* The caller may hold the descriptor borrowed. */
        TINYPY_INCREF(callable);
        tinypy_value_t *items_result = __tinypy_native_function_invoke_items(callable, class_value, tinypy_internal_tuple_items(args) + 1, TINYPY_TUPLE_SIZE(args) - 1U, kwargs, out_error);
        TINYPY_DECREF(callable);
        return items_result;
    }
    tinypy_value_t *result = __tinypy_native_function_invoke(callable, args, kwargs, out_error);
    return result;
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
    tinypy_value_t *result = self->type->call(self, call_args, kwargs, out_error);
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
        static const char deleted_owner[] = "<deleted type>";
        tinypy_bool_t wrapper = native->base.type == vm->native_wrapper_descriptor_type;
        const char *descriptor_prefix = wrapper != 0 ? wrapper_descriptor_prefix : method_descriptor_prefix;
        size_t descriptor_prefix_size = wrapper != 0 ? sizeof(wrapper_descriptor_prefix) - 1U : sizeof(method_descriptor_prefix) - 1U;
        tinypy_type_t *owner = tinypy_internal_native_function_owner(&native->base);
        size_t owner_name_size = sizeof(deleted_owner) - 1U;
        const char *owner_name = deleted_owner;

        if (owner != NULL) {
            owner_name = tinypy_type_name(owner, &owner_name_size);
        }
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

    if (value->type == vm->native_method_descriptor_type || value->type == vm->native_wrapper_descriptor_type || value->type == vm->native_class_method_descriptor_type) {
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
            hash = receiver_hash ^ (tinypy_hash_t)(((uintptr_t)native->callback >> 4U) ^ ((uintptr_t)native->items_callback >> 4U) ^ ((uintptr_t)native->user_data >> 4U));
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
    tinypy_type_t **types[] = {&vm->native_method_descriptor_type, &vm->native_wrapper_descriptor_type, &vm->native_method_wrapper_type, &vm->native_class_method_descriptor_type};
    tinypy_value_t *const names[] = {vm->internal_method_descriptor_key, vm->internal_wrapper_descriptor_key, vm->internal_method_wrapper_key, vm->internal_classmethod_descriptor_key};
    static const tinypy_call_slot_t calls[] = {tinypy_internal_native_function_call, tinypy_internal_native_function_call, tinypy_internal_native_function_call, __tinypy_native_class_method_descriptor_call};
    static const tinypy_descriptor_get_slot_t getters[] = {tinypy_internal_native_function_descriptor_get, tinypy_internal_native_function_descriptor_get, NULL, __tinypy_native_class_method_descriptor_get};

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
        type->call = calls[index];
        type->descriptor_get = getters[index];
        type->rich_compare = index == 2U ? __tinypy_native_function_compare_slot : NULL;
        type->hash = __tinypy_native_function_hash_slot;
        type->create = NULL;
        type->flags = (type->flags | TINYPY_TYPE_FLAG_IMMUTABLE) & ~TINYPY_TYPE_FLAG_BASE_TYPE;
        if (index != 1U) {
            type->flags |= TINYPY_TYPE_FLAG_NEEDS_ATTRIBUTE_READY;
        }
        *types[index] = type;
    }
}
//////////////////////////////////////////////////////////////////////////
/* wrap_descr_get: None stands for a missing instance or owner. */
static tinypy_value_t *__tinypy_native_descriptor_get_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    size_t count = TINYPY_TUPLE_SIZE(args);
    tinypy_value_t *descriptor;
    tinypy_value_t *instance;
    tinypy_value_t *owner;

    (void)user_data;
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 1U, 2U, TINYPY_ARITY_STYLE_UNPACK, out_error) == 0) {
        return NULL;
    }
    descriptor = TINYPY_TUPLE_GET(args, 0U);
    instance = TINYPY_TUPLE_GET(args, 1U);
    owner = count == 3U ? TINYPY_TUPLE_GET(args, 2U) : NULL;
    if (TINYPY_VALUE_KIND(instance) == TINYPY_VALUE_NONE) {
        instance = NULL;
    }
    if (owner != NULL && TINYPY_VALUE_KIND(owner) == TINYPY_VALUE_NONE) {
        owner = NULL;
    }
    if (instance == NULL && owner == NULL) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "__get__(None, None) is invalid", out_error);
        return NULL;
    }
    tinypy_value_t *return_value_1 = descriptor->type->descriptor_get(descriptor, instance, owner, out_error);
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
    else if (a->callback == b->callback && a->items_callback == b->items_callback && a->user_data == b->user_data) {
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
        result = a->self == b->self && a->callback == b->callback && a->items_callback == b->items_callback && a->user_data == b->user_data;
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
    tinypy_type_t *descriptor_types[] = {vm->native_method_descriptor_type, vm->native_wrapper_descriptor_type, vm->native_method_wrapper_type, vm->native_class_method_descriptor_type};
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
        /* The descriptor types inherit the __hash__ of object, as their
           tp_hash does; only method-wrapper defines its own. */
        if (type == vm->native_method_wrapper_type) {
            tinypy_internal_type_add_method(type, vm->internal_special_hash_key, __tinypy_native_function_hash_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
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
    const tinypy_native_function_object_t *source = __tinypy_native_function_source(function);

    return source->user_data;
}
//////////////////////////////////////////////////////////////////////////
static void *__tinypy_internal_native_payload(tinypy_value_t *instance) {
    void *return_value_1 = (uint8_t *)instance + instance->type->native_payload_offset;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
/* A caller may run Python code between reading a slot and calling it, which
   can remove the callback through tinypy_native_type_update_spec: the slot
   then fails like an operation the type does not support, and a number or
   comparison slot returns NotImplemented. */
static void __tinypy_internal_native_slot_removed(tinypy_value_t *instance, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(instance);
    tinypy_message_part_t parts[] = {
        TINYPY_MESSAGE_PART_LITERAL("'"),
        TINYPY_MESSAGE_PART_TYPE_NAME(instance),
        TINYPY_MESSAGE_PART_LITERAL("' object no longer supports this operation"),
    };

    tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
}
//////////////////////////////////////////////////////////////////////////
/* Callbacks may report a failure through the VM error alone, as pybind does;
   the slot then reports it through out_error too, as a native function does,
   so that no caller reads the failure as an exhausted iterator. */
static void __tinypy_internal_native_slot_failed(tinypy_vm_t *vm, const tinypy_value_t *previous_raised, tinypy_error_t **out_error) {
    if (out_error != NULL && *out_error == NULL && vm->raised_value != previous_raised) {
        tinypy_internal_exception_make_diagnostic(vm, out_error);
    }
}
//////////////////////////////////////////////////////////////////////////
/* The slot that dispatches a spec callback of the same name. */
#define TINYPY_NATIVE_SLOT_WRAPPER(field, result_type, failure_result, failed, parameters, arguments)  \
    static result_type __tinypy_internal_native_##field parameters {                                   \
        tinypy_native_type_spec_t *spec = &instance->type->native_spec;                               \
        tinypy_vm_t *vm = TINYPY_VALUE_VM(instance);                                                  \
                                                                                                      \
        if (spec->field == NULL) {                                                                    \
            __tinypy_internal_native_slot_removed(instance, out_error);                               \
            return failure_result;                                                                    \
        }                                                                                             \
        if (__tinypy_internal_native_instance_check_live(instance, out_error) == TINYPY_FALSE) {      \
            return failure_result;                                                                    \
        }                                                                                             \
        tinypy_value_t *previous_raised = vm->raised_value;                                           \
        void *native_payload = __tinypy_internal_native_payload(instance);                            \
        result_type result = spec->field arguments;                                                   \
                                                                                                      \
        if (failed) {                                                                                 \
            __tinypy_internal_native_slot_failed(vm, previous_raised, out_error);                     \
        }                                                                                             \
        return result;                                                                                \
    }
TINYPY_NATIVE_SLOT_WRAPPER(call, tinypy_value_t *, NULL, result == NULL, (tinypy_value_t *instance, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error), (instance, native_payload, args, kwargs, spec->user_data, out_error))
TINYPY_NATIVE_SLOT_WRAPPER(repr, tinypy_value_t *, NULL, result == NULL, (tinypy_value_t *instance, tinypy_error_t **out_error), (instance, native_payload, spec->user_data, out_error))
TINYPY_NATIVE_SLOT_WRAPPER(get_attribute, tinypy_value_t *, NULL, result == NULL, (tinypy_value_t *instance, tinypy_value_t *name, tinypy_error_t **out_error), (instance, native_payload, name, spec->user_data, out_error))
TINYPY_NATIVE_SLOT_WRAPPER(set_attribute, tinypy_bool_t, TINYPY_FALSE, result == TINYPY_FALSE, (tinypy_value_t *instance, tinypy_value_t *name, tinypy_value_t *value, tinypy_error_t **out_error), (instance, native_payload, name, value, spec->user_data, out_error))
TINYPY_NATIVE_SLOT_WRAPPER(mapping_get, tinypy_value_t *, NULL, result == NULL, (tinypy_value_t *instance, tinypy_value_t *key, tinypy_error_t **out_error), (instance, native_payload, key, spec->user_data, out_error))
TINYPY_NATIVE_SLOT_WRAPPER(mapping_set, tinypy_bool_t, TINYPY_FALSE, result == TINYPY_FALSE, (tinypy_value_t *instance, tinypy_value_t *key, tinypy_value_t *value, tinypy_error_t **out_error), (instance, native_payload, key, value, spec->user_data, out_error))
TINYPY_NATIVE_SLOT_WRAPPER(mapping_length, ptrdiff_t, -1, result < 0, (tinypy_value_t *instance, tinypy_error_t **out_error), (instance, native_payload, spec->user_data, out_error))
TINYPY_NATIVE_SLOT_WRAPPER(sequence_get, tinypy_value_t *, NULL, result == NULL, (tinypy_value_t *instance, tinypy_value_t *key, tinypy_error_t **out_error), (instance, native_payload, key, spec->user_data, out_error))
TINYPY_NATIVE_SLOT_WRAPPER(sequence_set, tinypy_bool_t, TINYPY_FALSE, result == TINYPY_FALSE, (tinypy_value_t *instance, tinypy_value_t *key, tinypy_value_t *value, tinypy_error_t **out_error), (instance, native_payload, key, value, spec->user_data, out_error))
TINYPY_NATIVE_SLOT_WRAPPER(sequence_length, ptrdiff_t, -1, result < 0, (tinypy_value_t *instance, tinypy_error_t **out_error), (instance, native_payload, spec->user_data, out_error))
TINYPY_NATIVE_SLOT_WRAPPER(contains, int32_t, -1, result < 0, (tinypy_value_t *instance, tinypy_value_t *item, tinypy_error_t **out_error), (instance, native_payload, item, spec->user_data, out_error))
TINYPY_NATIVE_SLOT_WRAPPER(iter, tinypy_value_t *, NULL, result == NULL, (tinypy_value_t *instance, tinypy_error_t **out_error), (instance, native_payload, spec->user_data, out_error))
TINYPY_NATIVE_SLOT_WRAPPER(next, tinypy_value_t *, NULL, result == NULL, (tinypy_value_t *instance, tinypy_error_t **out_error), (instance, native_payload, spec->user_data, out_error))
TINYPY_NATIVE_SLOT_WRAPPER(negative, tinypy_value_t *, NULL, result == NULL, (tinypy_value_t *instance, tinypy_error_t **out_error), (instance, native_payload, spec->user_data, out_error))
TINYPY_NATIVE_SLOT_WRAPPER(absolute, tinypy_value_t *, NULL, result == NULL, (tinypy_value_t *instance, tinypy_error_t **out_error), (instance, native_payload, spec->user_data, out_error))
#undef TINYPY_NATIVE_SLOT_WRAPPER
//////////////////////////////////////////////////////////////////////////
/* -1 is never a hash value: as for a __hash__ method, a callback result of -1
   reads as -2, and failures are reported through the error. */
static tinypy_hash_t __tinypy_internal_native_hash(tinypy_value_t *instance, tinypy_error_t **out_error) {
    tinypy_native_type_spec_t *spec = &instance->type->native_spec;
    tinypy_vm_t *vm = TINYPY_VALUE_VM(instance);

    if (spec->hash == NULL) {
        __tinypy_internal_native_slot_removed(instance, out_error);
        return (tinypy_hash_t)-1;
    }
    if (__tinypy_internal_native_instance_check_live(instance, out_error) == TINYPY_FALSE) {
        return (tinypy_hash_t)-1;
    }
    tinypy_value_t *previous_raised = vm->raised_value;
    void *native_payload = __tinypy_internal_native_payload(instance);
    tinypy_hash_t hash = spec->hash(instance, native_payload, spec->user_data, out_error);

    __tinypy_internal_native_slot_failed(vm, previous_raised, out_error);
    return hash == (tinypy_hash_t)-1 ? (tinypy_hash_t)-2 : hash;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_native_compare(tinypy_value_t *instance, tinypy_value_t *other, int32_t operation, tinypy_error_t **out_error) {
    tinypy_native_type_spec_t *spec = &instance->type->native_spec;
    tinypy_vm_t *vm = TINYPY_VALUE_VM(instance);

    if (spec->compare == NULL) {
        return TINYPY_RET_NOT_IMPLEMENTED(vm);
    }
    if (__tinypy_internal_native_instance_check_live(instance, out_error) == TINYPY_FALSE) {
        return NULL;
    }
    tinypy_value_t *previous_raised = vm->raised_value;
    void *native_payload = __tinypy_internal_native_payload(instance);
    tinypy_value_t *result = spec->compare(instance, native_payload, other, (tinypy_compare_operation_e)operation, spec->user_data, out_error);

    if (result == NULL) {
        __tinypy_internal_native_slot_failed(vm, previous_raised, out_error);
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
#define TINYPY_NATIVE_BINARY_WRAPPER(field)                                                                                    \
    static tinypy_value_t *__tinypy_internal_native_##field(tinypy_value_t *instance, tinypy_value_t *other, tinypy_error_t **out_error) { \
        tinypy_native_type_spec_t *spec = &instance->type->native_spec;                                                        \
        tinypy_vm_t *vm = TINYPY_VALUE_VM(instance);                                                                           \
                                                                                                                               \
        if (spec->field == NULL) {                                                                                             \
            return TINYPY_RET_NOT_IMPLEMENTED(vm);                                                                             \
        }                                                                                                                      \
        if (__tinypy_internal_native_instance_check_live(instance, out_error) == TINYPY_FALSE) {                               \
            return NULL;                                                                                                       \
        }                                                                                                                      \
        tinypy_value_t *previous_raised = vm->raised_value;                                                                    \
        void *native_payload = __tinypy_internal_native_payload(instance);                                                     \
        tinypy_value_t *result = spec->field(instance, native_payload, other, spec->user_data, out_error);                     \
                                                                                                                               \
        if (result == NULL) {                                                                                                  \
            __tinypy_internal_native_slot_failed(vm, previous_raised, out_error);                                              \
        }                                                                                                                      \
        return result;                                                                                                         \
    }
TINYPY_NATIVE_BINARY_WRAPPER(add)
TINYPY_NATIVE_BINARY_WRAPPER(subtract)
TINYPY_NATIVE_BINARY_WRAPPER(multiply)
TINYPY_NATIVE_BINARY_WRAPPER(divide)
TINYPY_NATIVE_BINARY_WRAPPER(inplace_add)
TINYPY_NATIVE_BINARY_WRAPPER(inplace_subtract)
TINYPY_NATIVE_BINARY_WRAPPER(inplace_multiply)
TINYPY_NATIVE_BINARY_WRAPPER(inplace_divide)
TINYPY_NATIVE_BINARY_WRAPPER(reflected_add)
TINYPY_NATIVE_BINARY_WRAPPER(reflected_subtract)
TINYPY_NATIVE_BINARY_WRAPPER(reflected_multiply)
TINYPY_NATIVE_BINARY_WRAPPER(reflected_divide)

#undef TINYPY_NATIVE_BINARY_WRAPPER
//////////////////////////////////////////////////////////////////////////
/* The calling conventions of the slot wrappers of a host native type, as
   the wrapper functions of add_operators: those taking no argument after
   self, then one, then two, then any. */
typedef enum tinypy_native_wrapper_kind_e {
    TINYPY_NATIVE_WRAPPER_UNARY = 0,
    TINYPY_NATIVE_WRAPPER_NEXT = 1,
    TINYPY_NATIVE_WRAPPER_HASH = 2,
    TINYPY_NATIVE_WRAPPER_LENGTH = 3,
    TINYPY_NATIVE_WRAPPER_NUMBER = 4,
    TINYPY_NATIVE_WRAPPER_GET_ITEM = 5,
    TINYPY_NATIVE_WRAPPER_DELETE_ITEM = 6,
    TINYPY_NATIVE_WRAPPER_CONTAINS = 7,
    TINYPY_NATIVE_WRAPPER_COMPARE = 8,
    TINYPY_NATIVE_WRAPPER_GET_ATTRIBUTE = 9,
    TINYPY_NATIVE_WRAPPER_DELETE_ATTRIBUTE = 10,
    TINYPY_NATIVE_WRAPPER_SET_ITEM = 11,
    TINYPY_NATIVE_WRAPPER_SET_ATTRIBUTE = 12,
    TINYPY_NATIVE_WRAPPER_CALL = 13
} tinypy_native_wrapper_kind_e;
//////////////////////////////////////////////////////////////////////////
/* A slot wrapper runs the callback at callback_offset, or when the spec
   leaves it NULL, the one at sequence_offset: the slot wrapper of a name
   that a mapping callback shares with a sequence one runs the mapping
   callback first, as the first slot of add_operators names it. */
typedef struct tinypy_native_slot_wrapper_t {
    size_t name_offset;
    size_t callback_offset;
    size_t sequence_offset;
    tinypy_native_wrapper_kind_e kind;
    tinypy_compare_operation_e operation;
} tinypy_native_slot_wrapper_t;
//////////////////////////////////////////////////////////////////////////
/* The slot wrappers of the callbacks of a spec in the order of the slots of
   add_operators. */
static const tinypy_native_slot_wrapper_t __tinypy_native_slot_wrappers[] = {
    {offsetof(tinypy_vm_t, internal_special_repr_key), offsetof(tinypy_native_type_spec_t, repr), offsetof(tinypy_native_type_spec_t, repr), TINYPY_NATIVE_WRAPPER_UNARY, TINYPY_COMPARE_LESS},
    {offsetof(tinypy_vm_t, internal_special_hash_key), offsetof(tinypy_native_type_spec_t, hash), offsetof(tinypy_native_type_spec_t, hash), TINYPY_NATIVE_WRAPPER_HASH, TINYPY_COMPARE_LESS},
    {offsetof(tinypy_vm_t, internal_special_call_key), offsetof(tinypy_native_type_spec_t, call), offsetof(tinypy_native_type_spec_t, call), TINYPY_NATIVE_WRAPPER_CALL, TINYPY_COMPARE_LESS},
    {offsetof(tinypy_vm_t, internal_special_getattribute_key), offsetof(tinypy_native_type_spec_t, get_attribute), offsetof(tinypy_native_type_spec_t, get_attribute), TINYPY_NATIVE_WRAPPER_GET_ATTRIBUTE, TINYPY_COMPARE_LESS},
    {offsetof(tinypy_vm_t, internal_special_setattr_key), offsetof(tinypy_native_type_spec_t, set_attribute), offsetof(tinypy_native_type_spec_t, set_attribute), TINYPY_NATIVE_WRAPPER_SET_ATTRIBUTE, TINYPY_COMPARE_LESS},
    {offsetof(tinypy_vm_t, internal_special_delattr_key), offsetof(tinypy_native_type_spec_t, set_attribute), offsetof(tinypy_native_type_spec_t, set_attribute), TINYPY_NATIVE_WRAPPER_DELETE_ATTRIBUTE, TINYPY_COMPARE_LESS},
    {offsetof(tinypy_vm_t, internal_special_lt_key), offsetof(tinypy_native_type_spec_t, compare), offsetof(tinypy_native_type_spec_t, compare), TINYPY_NATIVE_WRAPPER_COMPARE, TINYPY_COMPARE_LESS},
    {offsetof(tinypy_vm_t, internal_special_le_key), offsetof(tinypy_native_type_spec_t, compare), offsetof(tinypy_native_type_spec_t, compare), TINYPY_NATIVE_WRAPPER_COMPARE, TINYPY_COMPARE_LESS_EQUAL},
    {offsetof(tinypy_vm_t, internal_special_eq_key), offsetof(tinypy_native_type_spec_t, compare), offsetof(tinypy_native_type_spec_t, compare), TINYPY_NATIVE_WRAPPER_COMPARE, TINYPY_COMPARE_EQUAL},
    {offsetof(tinypy_vm_t, internal_special_ne_key), offsetof(tinypy_native_type_spec_t, compare), offsetof(tinypy_native_type_spec_t, compare), TINYPY_NATIVE_WRAPPER_COMPARE, TINYPY_COMPARE_NOT_EQUAL},
    {offsetof(tinypy_vm_t, internal_special_gt_key), offsetof(tinypy_native_type_spec_t, compare), offsetof(tinypy_native_type_spec_t, compare), TINYPY_NATIVE_WRAPPER_COMPARE, TINYPY_COMPARE_GREATER},
    {offsetof(tinypy_vm_t, internal_special_ge_key), offsetof(tinypy_native_type_spec_t, compare), offsetof(tinypy_native_type_spec_t, compare), TINYPY_NATIVE_WRAPPER_COMPARE, TINYPY_COMPARE_GREATER_EQUAL},
    {offsetof(tinypy_vm_t, internal_special_iter_key), offsetof(tinypy_native_type_spec_t, iter), offsetof(tinypy_native_type_spec_t, iter), TINYPY_NATIVE_WRAPPER_UNARY, TINYPY_COMPARE_LESS},
    {offsetof(tinypy_vm_t, internal_special_next_key), offsetof(tinypy_native_type_spec_t, next), offsetof(tinypy_native_type_spec_t, next), TINYPY_NATIVE_WRAPPER_NEXT, TINYPY_COMPARE_LESS},
    {offsetof(tinypy_vm_t, internal_special_add_key), offsetof(tinypy_native_type_spec_t, add), offsetof(tinypy_native_type_spec_t, add), TINYPY_NATIVE_WRAPPER_NUMBER, TINYPY_COMPARE_LESS},
    {offsetof(tinypy_vm_t, internal_special_radd_key), offsetof(tinypy_native_type_spec_t, reflected_add), offsetof(tinypy_native_type_spec_t, reflected_add), TINYPY_NATIVE_WRAPPER_NUMBER, TINYPY_COMPARE_LESS},
    {offsetof(tinypy_vm_t, internal_special_sub_key), offsetof(tinypy_native_type_spec_t, subtract), offsetof(tinypy_native_type_spec_t, subtract), TINYPY_NATIVE_WRAPPER_NUMBER, TINYPY_COMPARE_LESS},
    {offsetof(tinypy_vm_t, internal_special_rsub_key), offsetof(tinypy_native_type_spec_t, reflected_subtract), offsetof(tinypy_native_type_spec_t, reflected_subtract), TINYPY_NATIVE_WRAPPER_NUMBER, TINYPY_COMPARE_LESS},
    {offsetof(tinypy_vm_t, internal_special_mul_key), offsetof(tinypy_native_type_spec_t, multiply), offsetof(tinypy_native_type_spec_t, multiply), TINYPY_NATIVE_WRAPPER_NUMBER, TINYPY_COMPARE_LESS},
    {offsetof(tinypy_vm_t, internal_special_rmul_key), offsetof(tinypy_native_type_spec_t, reflected_multiply), offsetof(tinypy_native_type_spec_t, reflected_multiply), TINYPY_NATIVE_WRAPPER_NUMBER, TINYPY_COMPARE_LESS},
    {offsetof(tinypy_vm_t, internal_special_div_key), offsetof(tinypy_native_type_spec_t, divide), offsetof(tinypy_native_type_spec_t, divide), TINYPY_NATIVE_WRAPPER_NUMBER, TINYPY_COMPARE_LESS},
    {offsetof(tinypy_vm_t, internal_special_rdiv_key), offsetof(tinypy_native_type_spec_t, reflected_divide), offsetof(tinypy_native_type_spec_t, reflected_divide), TINYPY_NATIVE_WRAPPER_NUMBER, TINYPY_COMPARE_LESS},
    {offsetof(tinypy_vm_t, internal_special_neg_key), offsetof(tinypy_native_type_spec_t, negative), offsetof(tinypy_native_type_spec_t, negative), TINYPY_NATIVE_WRAPPER_UNARY, TINYPY_COMPARE_LESS},
    {offsetof(tinypy_vm_t, internal_special_abs_key), offsetof(tinypy_native_type_spec_t, absolute), offsetof(tinypy_native_type_spec_t, absolute), TINYPY_NATIVE_WRAPPER_UNARY, TINYPY_COMPARE_LESS},
    {offsetof(tinypy_vm_t, internal_special_iadd_key), offsetof(tinypy_native_type_spec_t, inplace_add), offsetof(tinypy_native_type_spec_t, inplace_add), TINYPY_NATIVE_WRAPPER_NUMBER, TINYPY_COMPARE_LESS},
    {offsetof(tinypy_vm_t, internal_special_isub_key), offsetof(tinypy_native_type_spec_t, inplace_subtract), offsetof(tinypy_native_type_spec_t, inplace_subtract), TINYPY_NATIVE_WRAPPER_NUMBER, TINYPY_COMPARE_LESS},
    {offsetof(tinypy_vm_t, internal_special_imul_key), offsetof(tinypy_native_type_spec_t, inplace_multiply), offsetof(tinypy_native_type_spec_t, inplace_multiply), TINYPY_NATIVE_WRAPPER_NUMBER, TINYPY_COMPARE_LESS},
    {offsetof(tinypy_vm_t, internal_special_idiv_key), offsetof(tinypy_native_type_spec_t, inplace_divide), offsetof(tinypy_native_type_spec_t, inplace_divide), TINYPY_NATIVE_WRAPPER_NUMBER, TINYPY_COMPARE_LESS},
    {offsetof(tinypy_vm_t, internal_special_length_key), offsetof(tinypy_native_type_spec_t, mapping_length), offsetof(tinypy_native_type_spec_t, sequence_length), TINYPY_NATIVE_WRAPPER_LENGTH, TINYPY_COMPARE_LESS},
    {offsetof(tinypy_vm_t, internal_special_getitem_key), offsetof(tinypy_native_type_spec_t, mapping_get), offsetof(tinypy_native_type_spec_t, sequence_get), TINYPY_NATIVE_WRAPPER_GET_ITEM, TINYPY_COMPARE_LESS},
    {offsetof(tinypy_vm_t, internal_special_setitem_key), offsetof(tinypy_native_type_spec_t, mapping_set), offsetof(tinypy_native_type_spec_t, sequence_set), TINYPY_NATIVE_WRAPPER_SET_ITEM, TINYPY_COMPARE_LESS},
    {offsetof(tinypy_vm_t, internal_special_delitem_key), offsetof(tinypy_native_type_spec_t, mapping_set), offsetof(tinypy_native_type_spec_t, mapping_set), TINYPY_NATIVE_WRAPPER_DELETE_ITEM, TINYPY_COMPARE_LESS},
    {offsetof(tinypy_vm_t, internal_special_contains_key), offsetof(tinypy_native_type_spec_t, contains), offsetof(tinypy_native_type_spec_t, contains), TINYPY_NATIVE_WRAPPER_CONTAINS, TINYPY_COMPARE_LESS},
};
typedef char tinypy_native_slot_wrappers_must_fit_a_mask_t[sizeof(__tinypy_native_slot_wrappers) / sizeof(__tinypy_native_slot_wrappers[0]) <= 64U ? 1 : -1];
//////////////////////////////////////////////////////////////////////////
/* A callback of a spec as a pointer to a function of any type, which only
   compares. */
typedef void (*tinypy_native_callback_t)(void);
//////////////////////////////////////////////////////////////////////////
/* The callback in a field of a spec of the type of the callbacks of a slot
   wrapper. */
static tinypy_native_callback_t __tinypy_internal_native_field_callback(const uint8_t *field, tinypy_native_wrapper_kind_e kind) {
    switch (kind) {
    case TINYPY_NATIVE_WRAPPER_UNARY:
    case TINYPY_NATIVE_WRAPPER_NEXT:
        return (tinypy_native_callback_t)*(const tinypy_native_unary_t *)field;
    case TINYPY_NATIVE_WRAPPER_HASH:
        return (tinypy_native_callback_t)*(const tinypy_native_hash_t *)field;
    case TINYPY_NATIVE_WRAPPER_LENGTH:
        return (tinypy_native_callback_t)*(const tinypy_native_length_t *)field;
    case TINYPY_NATIVE_WRAPPER_NUMBER:
        return (tinypy_native_callback_t)*(const tinypy_native_binary_t *)field;
    case TINYPY_NATIVE_WRAPPER_GET_ITEM:
        return (tinypy_native_callback_t)*(const tinypy_native_get_item_t *)field;
    case TINYPY_NATIVE_WRAPPER_GET_ATTRIBUTE:
        return (tinypy_native_callback_t)*(const tinypy_native_get_attribute_t *)field;
    case TINYPY_NATIVE_WRAPPER_CONTAINS:
        return (tinypy_native_callback_t)*(const tinypy_native_contains_t *)field;
    case TINYPY_NATIVE_WRAPPER_COMPARE:
        return (tinypy_native_callback_t)*(const tinypy_native_compare_t *)field;
    case TINYPY_NATIVE_WRAPPER_CALL:
        return (tinypy_native_callback_t)*(const tinypy_native_call_t *)field;
    case TINYPY_NATIVE_WRAPPER_SET_ATTRIBUTE:
    case TINYPY_NATIVE_WRAPPER_DELETE_ATTRIBUTE:
        return (tinypy_native_callback_t)*(const tinypy_native_set_attribute_t *)field;
    default:
        return (tinypy_native_callback_t)*(const tinypy_native_set_item_t *)field;
    }
}
//////////////////////////////////////////////////////////////////////////
/* The field of the spec that holds the callback a slot wrapper runs. */
static const uint8_t *__tinypy_internal_native_wrapper_field(const tinypy_native_type_spec_t *spec, const tinypy_native_slot_wrapper_t *wrapper) {
    const uint8_t *field = (const uint8_t *)spec + wrapper->callback_offset;

    if (__tinypy_internal_native_field_callback(field, wrapper->kind) == NULL) {
        field = (const uint8_t *)spec + wrapper->sequence_offset;
    }
    return field;
}
//////////////////////////////////////////////////////////////////////////
/* The callback of the spec that a slot wrapper runs, NULL when the spec sets
   none. */
static tinypy_native_callback_t __tinypy_internal_native_wrapper_callback(const tinypy_native_type_spec_t *spec, const tinypy_native_slot_wrapper_t *wrapper) {
    const uint8_t *field = __tinypy_internal_native_wrapper_field(spec, wrapper);
    tinypy_native_callback_t callback = __tinypy_internal_native_field_callback(field, wrapper->kind);

    return callback;
}
//////////////////////////////////////////////////////////////////////////
/* The callbacks of the attribute wrappers take a str name, as the slots
   do: PyObject_GetAttr and PyObject_SetAttr encode a unicode one. */
static tinypy_value_t *__tinypy_internal_native_wrapper_name(tinypy_value_t *name, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(name);

    if (__tinypy_internal_object_check_attribute_name(vm, name, out_error) == TINYPY_FALSE) {
        return NULL;
    }
    if (TINYPY_VALUE_KIND(name) == TINYPY_VALUE_UNICODE) {
        tinypy_value_t *encoded = tinypy_internal_object_encode_attribute_name(name, out_error);
        return encoded;
    }
    return TINYPY_RET(name);
}
//////////////////////////////////////////////////////////////////////////
/* Runs the callback that the spec of the type publishing a slot wrapper
   holds, as the slot wrapper of a C type runs the slot function of that
   type, on the payload of self, whose liveness the call of the descriptor
   checks. A callback that tinypy_native_type_update_spec removed fails as
   its slot does. */
tinypy_value_t *tinypy_internal_native_slot_wrapper_call(tinypy_value_t *function, tinypy_value_t *self, tinypy_value_t *const *items, size_t count, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    const tinypy_native_function_object_t *source = __tinypy_native_function_source(function);
    const tinypy_native_slot_wrapper_t *wrapper = &__tinypy_native_slot_wrappers[(uintptr_t)source->user_data];
    tinypy_type_t *owner = tinypy_internal_native_function_owner(&source->base);

    if (owner == NULL || __tinypy_internal_native_wrapper_callback(&owner->native_spec, wrapper) == NULL) {
        if (wrapper->kind == TINYPY_NATIVE_WRAPPER_NUMBER || wrapper->kind == TINYPY_NATIVE_WRAPPER_COMPARE) {
            return TINYPY_RET_NOT_IMPLEMENTED(vm);
        }
        __tinypy_internal_native_slot_removed(self, out_error);
        return NULL;
    }
    const tinypy_native_type_spec_t *spec = &owner->native_spec;
    const uint8_t *field = __tinypy_internal_native_wrapper_field(spec, wrapper);
    void *native_payload = __tinypy_internal_native_payload(self);
    tinypy_value_t *previous_raised = vm->raised_value;

    switch (wrapper->kind) {
    case TINYPY_NATIVE_WRAPPER_UNARY:
    case TINYPY_NATIVE_WRAPPER_NEXT: {
        tinypy_value_t *result = (*(const tinypy_native_unary_t *)field)(self, native_payload, spec->user_data, out_error);

        /* wrap_next: an exhausted iterator raises StopIteration. */
        if (result == NULL && wrapper->kind == TINYPY_NATIVE_WRAPPER_NEXT && vm->raised_value == previous_raised && (out_error == NULL || *out_error == NULL)) {
            tinypy_internal_exception_raise_stop_iteration(vm, out_error);
        }
        return result;
    }
    case TINYPY_NATIVE_WRAPPER_HASH: {
        tinypy_hash_t hash = (*(const tinypy_native_hash_t *)field)(self, native_payload, spec->user_data, out_error);

        if (vm->raised_value != previous_raised || (out_error != NULL && *out_error != NULL)) {
            return NULL;
        }
        tinypy_value_t *result = tinypy_integer_from_i64(vm, (int64_t)(hash == (tinypy_hash_t)-1 ? (tinypy_hash_t)-2 : hash));
        return result;
    }
    case TINYPY_NATIVE_WRAPPER_LENGTH: {
        ptrdiff_t length = (*(const tinypy_native_length_t *)field)(self, native_payload, spec->user_data, out_error);
        tinypy_value_t *result = length >= 0 ? tinypy_integer_from_i64(vm, (int64_t)length) : NULL;
        return result;
    }
    case TINYPY_NATIVE_WRAPPER_NUMBER:
    case TINYPY_NATIVE_WRAPPER_GET_ITEM: {
        tinypy_value_t *result = (*(const tinypy_native_binary_t *)field)(self, native_payload, items[0], spec->user_data, out_error);
        return result;
    }
    case TINYPY_NATIVE_WRAPPER_CONTAINS: {
        int32_t contains = (*(const tinypy_native_contains_t *)field)(self, native_payload, items[0], spec->user_data, out_error);
        tinypy_value_t *result = contains >= 0 ? tinypy_bool_from_i32(vm, contains) : NULL;
        return result;
    }
    case TINYPY_NATIVE_WRAPPER_COMPARE: {
        tinypy_value_t *result = (*(const tinypy_native_compare_t *)field)(self, native_payload, items[0], wrapper->operation, spec->user_data, out_error);
        return result;
    }
    case TINYPY_NATIVE_WRAPPER_CALL: {
        tinypy_value_t *args = tinypy_internal_tuple_from_items_checked(vm, items, count, out_error);
        if (args == NULL) {
            return NULL;
        }
        tinypy_value_t *result = (*(const tinypy_native_call_t *)field)(self, native_payload, args, kwargs, spec->user_data, out_error);
        TINYPY_DECREF(args);
        return result;
    }
    case TINYPY_NATIVE_WRAPPER_DELETE_ITEM:
    case TINYPY_NATIVE_WRAPPER_SET_ITEM: {
        tinypy_value_t *value = wrapper->kind == TINYPY_NATIVE_WRAPPER_SET_ITEM ? items[1] : NULL;
        tinypy_bool_t stored = (*(const tinypy_native_set_item_t *)field)(self, native_payload, items[0], value, spec->user_data, out_error);
        tinypy_value_t *result = stored != TINYPY_FALSE ? TINYPY_RET_NONE(vm) : NULL;
        return result;
    }
    default:
        break;
    }
    tinypy_value_t *name = __tinypy_internal_native_wrapper_name(items[0], out_error);
    if (name == NULL) {
        return NULL;
    }
    tinypy_value_t *result = NULL;
    if (wrapper->kind == TINYPY_NATIVE_WRAPPER_GET_ATTRIBUTE) {
        result = (*(const tinypy_native_get_attribute_t *)field)(self, native_payload, name, spec->user_data, out_error);
    }
    else {
        tinypy_value_t *value = wrapper->kind == TINYPY_NATIVE_WRAPPER_SET_ATTRIBUTE ? items[1] : NULL;
        tinypy_bool_t stored = (*(const tinypy_native_set_attribute_t *)field)(self, native_payload, name, value, spec->user_data, out_error);

        result = stored != TINYPY_FALSE ? TINYPY_RET_NONE(vm) : NULL;
    }
    TINYPY_DECREF(name);
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* The nearest base of a host native type that is no host native type. */
static const tinypy_type_t *__tinypy_internal_native_generic_base(const tinypy_type_t *type) {
    const tinypy_type_t *generic = type->base_type;

    while (tinypy_internal_type_is_host_native(generic) != 0) {
        generic = generic->base_type;
    }
    return generic;
}
//////////////////////////////////////////////////////////////////////////
/* Whether the instances of type run the callback of the spec of owner that
   one of its slot wrappers runs, with the same user data: type is owner, or
   copied its spec as a Python class does. */
static tinypy_bool_t __tinypy_internal_native_wrapper_dispatched(const tinypy_type_t *type, const tinypy_type_t *owner, const tinypy_native_slot_wrapper_t *wrapper) {
    tinypy_bool_t dispatched = type == owner || (type->native_spec.user_data == owner->native_spec.user_data && __tinypy_internal_native_wrapper_callback(&type->native_spec, wrapper) == __tinypy_internal_native_wrapper_callback(&owner->native_spec, wrapper)) ? TINYPY_TRUE : TINYPY_FALSE;

    return dispatched;
}
//////////////////////////////////////////////////////////////////////////
/* update_one_slot reads a slot wrapper found under its own name that
   applies to the value as the slot it wraps. The slot wrapper of a callback
   that the type of the value dispatches stands for its own slot, which
   nothing overrides. The static base of the type, the type itself or the
   host native type a Python class copied its spec from, dispatches no other
   callback of its host native bases: their slot wrappers read as the
   attribute of its nearest base that is no host native type. Any other
   slot wrapper, as one of another base of a Python class or one stored
   under another name, is an attribute that the generic slots call. */
TINYPY_NOINLINE tinypy_value_t *tinypy_internal_native_special_attribute(tinypy_vm_t *vm, tinypy_value_t *value, tinypy_value_t *key, tinypy_bool_t *out_slot) {
    tinypy_value_t *attribute = tinypy_internal_type_lookup_key(vm, value->type, key);

    *out_slot = TINYPY_FALSE;
    if (attribute == NULL || attribute->type != vm->native_wrapper_descriptor_type || TINYPY_NATIVE_FUNCTION_OBJECT(attribute)->items_callback != tinypy_internal_native_slot_wrapper_call || TINYPY_NAME_EQ(TINYPY_NATIVE_FUNCTION_OBJECT(attribute)->name, key) == 0) {
        return attribute;
    }
    tinypy_type_t *owner = tinypy_internal_native_function_owner(attribute);
    if (owner == NULL || (owner != value->type && owner != value->type->base_type && tinypy_type_is_subtype(value->type, owner) == 0)) {
        return attribute;
    }
    const tinypy_native_slot_wrapper_t *wrapper = &__tinypy_native_slot_wrappers[(uintptr_t)TINYPY_NATIVE_FUNCTION_OBJECT(attribute)->user_data];
    if (__tinypy_internal_native_wrapper_dispatched(value->type, owner, wrapper) != 0) {
        *out_slot = TINYPY_TRUE;
        return attribute;
    }
    const tinypy_type_t *static_base = tinypy_internal_type_static_base(value->type);
    if (tinypy_type_is_subtype(static_base, owner) == 0) {
        return attribute;
    }
    const tinypy_type_t *generic = __tinypy_internal_native_generic_base(static_base);
    tinypy_value_t *generic_attribute = tinypy_internal_type_lookup_key(vm, generic, key);
    return generic_attribute;
}
//////////////////////////////////////////////////////////////////////////
/* The slot wrappers that a spec publishes, one bit per entry: those of the
   callbacks it sets. */
static uint64_t __tinypy_internal_native_wrapper_mask(const tinypy_native_type_spec_t *spec) {
    size_t wrapper_count = sizeof(__tinypy_native_slot_wrappers) / sizeof(__tinypy_native_slot_wrappers[0]);
    uint64_t mask = 0U;

    for (size_t index = 0U; index < wrapper_count; ++index) {
        tinypy_native_callback_t callback = __tinypy_internal_native_wrapper_callback(spec, &__tinypy_native_slot_wrappers[index]);

        mask |= callback != NULL ? UINT64_C(1) << index : 0U;
    }
    return mask;
}
//////////////////////////////////////////////////////////////////////////
/* Whether attribute is a slot wrapper that the type published under name,
   which leaves out one Python stored under another name. */
static tinypy_bool_t __tinypy_internal_native_published_wrapper(tinypy_type_t *type, tinypy_value_t *name, tinypy_value_t *attribute) {
    tinypy_bool_t published = attribute->type == type->vm->native_wrapper_descriptor_type && TINYPY_NATIVE_FUNCTION_OBJECT(attribute)->items_callback == tinypy_internal_native_slot_wrapper_call && tinypy_internal_native_function_owner(attribute) == type && TINYPY_NAME_EQ(TINYPY_NATIVE_FUNCTION_OBJECT(attribute)->name, name) != 0 ? TINYPY_TRUE : TINYPY_FALSE;

    return published;
}
//////////////////////////////////////////////////////////////////////////
/* add_operators: the type publishes the slot wrappers of the callbacks that
   its spec sets beyond those of the previous one, each under a name its
   dictionary does not define otherwise, and withdraws those of the
   callbacks its spec no longer sets. */
static void __tinypy_internal_native_publish_wrappers(tinypy_type_t *type, uint64_t previous) {
    tinypy_vm_t *vm = type->vm;
    uint64_t current = __tinypy_internal_native_wrapper_mask(&type->native_spec);
    uint64_t changed = previous ^ current;

    while (changed != 0U) {
        size_t index = __tinypy_internal_lowest_bit_index(changed);
        const tinypy_native_slot_wrapper_t *wrapper = &__tinypy_native_slot_wrappers[index];
        tinypy_value_t *name = *(tinypy_value_t **)((uint8_t *)vm + wrapper->name_offset);
        tinypy_value_t *existing = tinypy_dict_get_optional(type->dict, name);
        tinypy_bool_t published = existing != NULL ? __tinypy_internal_native_published_wrapper(type, name, existing) : TINYPY_FALSE;
        tinypy_bool_t wanted = (current & (UINT64_C(1) << index)) != 0U ? TINYPY_TRUE : TINYPY_FALSE;

        changed &= changed - 1U;
        if (wanted != TINYPY_FALSE && (existing == NULL || published != 0)) {
            size_t arguments = wrapper->kind < TINYPY_NATIVE_WRAPPER_NUMBER ? 0U : (wrapper->kind < TINYPY_NATIVE_WRAPPER_SET_ITEM ? 1U : 2U);
            tinypy_arity_style_e style = arguments == 2U ? TINYPY_ARITY_STYLE_UNPACK : TINYPY_ARITY_STYLE_WRAPPER;
            tinypy_value_t *function = wrapper->kind == TINYPY_NATIVE_WRAPPER_CALL
                                           ? tinypy_internal_native_items_function_new(name, tinypy_internal_native_slot_wrapper_call, (void *)(uintptr_t)index, 0U, SIZE_MAX, TINYPY_ARITY_STYLE_UNCHECKED)
                                           : tinypy_internal_native_items_function_new(name, tinypy_internal_native_slot_wrapper_call, (void *)(uintptr_t)index, arguments, arguments, style);

            tinypy_internal_native_function_set_descriptor_kind(function, TINYPY_NATIVE_DESCRIPTOR_WRAPPER);
            tinypy_type_set_attr_key(type, name, function);
            TINYPY_DECREF(function);
        }
        else if (wanted == TINYPY_FALSE && published != 0) {
            tinypy_dict_delete(type->dict, name);
        }
    }
}
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
/* The fields that an older struct_size predates read as
   tinypy_native_type_spec_init sets them; the caller's struct is read only
   within its size. */
static void __tinypy_internal_native_spec_copy(tinypy_native_type_spec_t *target, const tinypy_native_type_spec_t *spec) {
    size_t copied_size = spec->struct_size < sizeof(*target) ? spec->struct_size : sizeof(*target);

    tinypy_native_type_spec_init(target);
    (void)memcpy(target, spec, copied_size);
    target->abi_version = TINYPY_NATIVE_TYPE_ABI_VERSION;
    target->struct_size = (uint32_t)sizeof(*target);
}
//////////////////////////////////////////////////////////////////////////
/* A host native type dispatches the callbacks of its own spec only: a slot
   its spec leaves without a callback keeps the behaviour of the nearest base
   that is no host native type, since the callback of a native base would run
   on the user data of the subtype's spec. */
static void __tinypy_internal_native_configure_slots(tinypy_type_t *type) {
    tinypy_native_type_spec_t *spec = &type->native_spec;
    const tinypy_type_t *generic = __tinypy_internal_native_generic_base(type);

    (void)memset(&type->native_number_slots, 0, sizeof(type->native_number_slots));
    (void)memset(&type->native_sequence_slots, 0, sizeof(type->native_sequence_slots));
    (void)memset(&type->native_mapping_slots, 0, sizeof(type->native_mapping_slots));
    type->call = spec->call != NULL ? __tinypy_internal_native_call : generic->call;
    type->repr = spec->repr != NULL ? __tinypy_internal_native_repr : generic->repr;
    type->hash = spec->hash != NULL ? __tinypy_internal_native_hash : generic->hash;
    type->rich_compare = spec->compare != NULL ? __tinypy_internal_native_compare : generic->rich_compare;
    type->get_attribute = spec->get_attribute != NULL ? __tinypy_internal_native_get_attribute : generic->get_attribute;
    type->set_attribute = spec->set_attribute != NULL ? __tinypy_internal_native_set_attribute : generic->set_attribute;
    type->iter = spec->iter != NULL ? __tinypy_internal_native_iter : generic->iter;
    type->next = spec->next != NULL ? __tinypy_internal_native_next : generic->next;
    type->mapping_slots = generic->mapping_slots;
    type->sequence_slots = generic->sequence_slots;
    type->number_slots = generic->number_slots;
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
    tinypy_type_t *type = tinypy_internal_native_type_new_key(name, bases, base_count, namespace_dict, spec, out_error);

    if (type == NULL) {
        return NULL;
    }
    __tinypy_internal_native_publish_wrappers(type, 0U);
    return type;
}
//////////////////////////////////////////////////////////////////////////
/* A host native type without its slot wrappers, for the built-in types of
   that layout, which register their own. */
tinypy_type_t *tinypy_internal_native_type_new_key(tinypy_value_t *name, const tinypy_type_t *const *bases, size_t base_count, tinypy_value_t *namespace_dict, const tinypy_native_type_spec_t *spec, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(name);
    size_t basic_size;

    TINYPY_CLEAR_ERROR(out_error);
    if (__tinypy_internal_native_validate_spec(vm, spec, out_error) == 0) {
        return NULL;
    }
    for (size_t index = 0U; index < base_count; ++index) {
        tinypy_bool_t foreign = bases[index]->vm != vm ? TINYPY_TRUE : TINYPY_FALSE;

        if (foreign != 0 || tinypy_internal_type_is_native_base(bases[index]) == 0) {
            tinypy_message_part_t parts[] = {
                TINYPY_MESSAGE_PART_LITERAL("native type base '"),
                {bases[index]->name, bases[index]->name_size},
                foreign != 0 ? (tinypy_message_part_t)TINYPY_MESSAGE_PART_LITERAL("' belongs to another VM") : (tinypy_message_part_t)TINYPY_MESSAGE_PART_LITERAL("' has an incompatible instance layout"),
            };

            tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
            return NULL;
        }
    }
    tinypy_native_type_spec_t native_spec;
    __tinypy_internal_native_spec_copy(&native_spec, spec);
    tinypy_bool_t has_instance_dict = native_spec.has_instance_dict != 0 ? TINYPY_TRUE : TINYPY_FALSE;
    tinypy_bool_t has_weakrefs = native_spec.has_weakrefs != 0 ? TINYPY_TRUE : TINYPY_FALSE;
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
    if (type->layout_kind == TINYPY_VALUE_NATIVE_INSTANCE && (type->native_payload_size != native_spec.payload_size || type->native_payload_alignment != native_spec.payload_alignment)) {
        tinypy_value_t *type_value = tinypy_type_as_value(type);
        TINYPY_DECREF(type_value);
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "native subtype payload layout differs from its base", out_error);
        return NULL;
    }
    type->native_spec = native_spec;
    type->layout_kind = TINYPY_VALUE_NATIVE_INSTANCE;
    type->native_payload_offset = offsetof(tinypy_native_instance_object_t, payload);
    type->native_payload_size = native_spec.payload_size;
    type->native_payload_alignment = native_spec.payload_alignment;
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
    TINYPY_CLEAR_ERROR(out_error);
    if (type->layout_kind != TINYPY_VALUE_NATIVE_INSTANCE) {
        tinypy_internal_make_vm_error(type->vm, TINYPY_ERROR_TYPE, "type does not have a native layout", out_error);
        return TINYPY_FALSE;
    }
    if (__tinypy_internal_native_validate_spec(type->vm, spec, out_error) == 0) {
        return TINYPY_FALSE;
    }
    tinypy_native_type_spec_t native_spec;
    __tinypy_internal_native_spec_copy(&native_spec, spec);
    tinypy_bool_t has_instance_dict = native_spec.has_instance_dict != 0 ? TINYPY_TRUE : TINYPY_FALSE;
    tinypy_bool_t has_weakrefs = native_spec.has_weakrefs != 0 ? TINYPY_TRUE : TINYPY_FALSE;
    if (type->native_payload_size != native_spec.payload_size || type->native_payload_alignment != native_spec.payload_alignment || type->native_spec.construct != native_spec.construct || type->native_spec.finalize != native_spec.finalize || type->native_spec.user_data != native_spec.user_data || type->has_instance_dict != has_instance_dict || (type->weakref_offset != 0U) != (has_weakrefs != 0)) {
        tinypy_internal_make_vm_error(type->vm, TINYPY_ERROR_TYPE, "native type layout and lifetime callbacks are immutable", out_error);
        return TINYPY_FALSE;
    }
    /* The same spec again, as a host binding a method at a time passes it,
       changes nothing. */
    if (memcmp(&type->native_spec, &native_spec, sizeof(native_spec)) == 0) {
        return TINYPY_TRUE;
    }
    uint64_t previous = __tinypy_internal_native_wrapper_mask(&type->native_spec);
    type->native_spec = native_spec;
    __tinypy_internal_native_configure_slots(type);
    __tinypy_internal_native_publish_wrappers(type, previous);
    tinypy_internal_type_modified(type);
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
/* Instances come from tinypy_native_instance_new or from object.__new__,
   which constructs them; both count the ones tinypy_vm_destroy finalizes,
   those of host types with a finalizer. */
static void __tinypy_internal_native_instance_created(tinypy_type_t *type) {
    if (type->native_spec.finalize != NULL && tinypy_internal_type_is_host_native(type) != 0) {
        type->vm->native_finalizable_created += 1U;
    }
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_native_instance_new(tinypy_type_t *type) {
    tinypy_value_t *instance = tinypy_internal_object_allocate(type->vm, type, type->basic_size);

    __tinypy_internal_native_instance_created(type);
    return instance;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_native_instance_construct(tinypy_value_t *instance, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    TINYPY_CLEAR_ERROR(out_error);
    tinypy_native_type_spec_t *spec = &instance->type->native_spec;
    __tinypy_internal_native_instance_created(instance->type);
    if (spec->construct == NULL) {
        return TINYPY_TRUE;
    }
    tinypy_vm_t *vm = TINYPY_VALUE_VM(instance);
    tinypy_value_t *previous_raised = vm->raised_value;
    void *native_payload = __tinypy_internal_native_payload(instance);
    tinypy_bool_t constructed = spec->construct(instance, native_payload, args, kwargs, spec->user_data, out_error);

    if (constructed == TINYPY_FALSE) {
        __tinypy_internal_native_slot_failed(vm, previous_raised, out_error);
    }
    return constructed;
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

    if (instance->finalization != TINYPY_NATIVE_FINALIZATION_NONE) {
        return;
    }
    if (spec->finalize == NULL) {
        instance->finalization = TINYPY_NATIVE_FINALIZATION_DONE;
        return;
    }
    void *native_payload = __tinypy_internal_native_payload(value);
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_internal_exception_state_t state;
    tinypy_internal_exception_preserve_begin(vm, &state);
    instance->finalization = TINYPY_NATIVE_FINALIZATION_RUNNING;
    spec->finalize(value, native_payload, spec->user_data);
    instance->finalization = TINYPY_NATIVE_FINALIZATION_DONE;
    if (vm->state == TINYPY_VM_STATE_LIVE) {
        tinypy_internal_output_unraisable(vm, value);
    }
    tinypy_internal_exception_preserve_restore(vm, &state);
}
//////////////////////////////////////////////////////////////////////////
/* The memory sweep of tinypy_vm_destroy runs no finalizer. */
void tinypy_internal_native_instance_destroy(tinypy_value_t *value) {
    if (TINYPY_VALUE_VM(value)->state == TINYPY_VM_STATE_LIVE) {
        tinypy_internal_native_instance_finalize(value);
    }
}
