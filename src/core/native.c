#include "tinypy/native.h"

#include "internal.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_native_function_new(tinypy_vm_t *vm, const char *name, size_t name_size, tinypy_native_function_callback_t callback, void *user_data, tinypy_native_function_finalize_t finalize) {
    tinypy_native_function_object_t *function = (tinypy_native_function_object_t *)tinypy_internal_value_allocate(vm, TINYPY_VALUE_NATIVE_FUNCTION, sizeof(*function));
    function->name = tinypy_string_from_bytes(vm, name, name_size);
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
    tinypy_native_function_object_t *function = TINYPY_NATIVE_FUNCTION_OBJECT(value);

    if (function->finalize != NULL) {
        tinypy_native_function_finalize_t finalize = function->finalize;
        void *user_data = function->user_data;

        function->finalize = NULL;
        function->user_data = NULL;
        finalize(user_data);
    }
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_native_function_call(tinypy_value_t *callable, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_native_function_object_t *function = TINYPY_NATIVE_FUNCTION_OBJECT(callable);
    tinypy_value_t *call_args = args;

    if (function->self != NULL) {
        tinypy_vm_t *vm = TINYPY_VALUE_VM(callable);

        call_args = tinypy_internal_tuple_prepend_checked(vm, function->self, args, out_error);
        if (call_args == NULL) {
            return NULL;
        }
    }

    tinypy_value_t *result = function->callback(callable, call_args, kwargs, function->user_data, out_error);

    if (call_args != args) {
        TINYPY_DECREF(call_args);
    }

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
tinypy_value_t *tinypy_internal_native_function_descriptor_get(tinypy_value_t *descriptor, tinypy_value_t *instance, tinypy_type_t *owner, tinypy_error_t **out_error) {
    tinypy_native_function_object_t *function = TINYPY_NATIVE_FUNCTION_OBJECT(descriptor);

    TINYPY_CLEAR_ERROR(out_error);
    if (instance == NULL || function->self != NULL) {
        TINYPY_INCREF(descriptor);
        return descriptor;
    }
    if (function->owner != NULL && tinypy_type_is_subtype(instance->type, function->owner) == 0) {
        tinypy_internal_make_vm_error(TINYPY_VALUE_VM(descriptor), TINYPY_ERROR_TYPE, "descriptor requires an instance of its owner", out_error);
        return NULL;
    }

    (void)owner;

    tinypy_vm_t *vm = TINYPY_VALUE_VM(descriptor);
    tinypy_native_function_object_t *method = (tinypy_native_function_object_t *)tinypy_internal_value_allocate(vm, TINYPY_VALUE_NATIVE_FUNCTION, sizeof(*method));

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
static tinypy_bool_t __tinypy_native_function_method_arguments(tinypy_vm_t *vm, tinypy_value_t *args, tinypy_value_t *kwargs, size_t minimum, size_t maximum, tinypy_error_t **out_error) {
    size_t count = TINYPY_TUPLE_SIZE(args);

    if ((kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U) || count < minimum || count > maximum || TINYPY_VALUE_KIND(TINYPY_TUPLE_GET(args, 0U)) != TINYPY_VALUE_NATIVE_FUNCTION) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "builtin function method received invalid arguments", out_error);
        return TINYPY_FALSE;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_native_function_attribute_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    if (__tinypy_native_function_method_arguments(vm, args, kwargs, 1U, 1U, out_error) == 0) {
        return NULL;
    }
    tinypy_native_function_object_t *native = TINYPY_NATIVE_FUNCTION_OBJECT(TINYPY_TUPLE_GET(args, 0U));
    intptr_t field = (intptr_t)user_data;
    tinypy_value_t *result;

    if (field == 0) {
        result = native->name;
    }
    else if (field == 1) {
        result = native->self;
    }
    else if (field == 2) {
        result = native->self == NULL ? native->module : NULL;
        if (result == NULL && native->self == NULL) {
            result = tinypy_string_from_bytes(vm, "__builtin__", 11U);
            return result;
        }
    }
    else if (field == 3) {
        result = native->owner != NULL ? &native->owner->base.base : NULL;
    }
    else {
        tinypy_value_t *return_value_1 = tinypy_none_get(vm);
        return return_value_1;
    }
    if (result == NULL) {
        result = tinypy_none_get(vm);
        return result;
    }
    TINYPY_INCREF(result);
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
    tinypy_value_t *result = tinypy_internal_native_function_call(self, call_args, kwargs, out_error);
    TINYPY_DECREF(call_args);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_native_function_repr_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    static const char function_prefix[] = "<built-in function ";
    static const char method_prefix[] = "<built-in method ";
    static const char method_middle[] = " of ";
    static const char object_middle[] = " object at ";
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_native_function_method_arguments(vm, args, kwargs, 1U, 1U, out_error) == 0) {
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
        size_t fixed_size = prefix_size + (sizeof(method_middle) - 1U) + (sizeof(object_middle) - 1U) + pointer_size + 1U;
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
static tinypy_value_t *__tinypy_native_function_hash_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_native_function_method_arguments(vm, args, kwargs, 1U, 1U, out_error) == 0) {
        return NULL;
    }
    tinypy_hash_t hash = (tinypy_hash_t)((uintptr_t)TINYPY_TUPLE_GET(args, 0U) >> 4U);
    if (hash == (tinypy_hash_t)-1) {
        hash = (tinypy_hash_t)-2;
    }
    tinypy_value_t *result = tinypy_integer_from_i64(vm, hash);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_hash_t __tinypy_native_function_hash_slot(tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_hash_t hash = (tinypy_hash_t)((uintptr_t)value >> 4U);

    (void)out_error;
    return hash == (tinypy_hash_t)-1 ? (tinypy_hash_t)-2 : hash;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_initialize_native_descriptor_types(tinypy_vm_t *vm) {
    tinypy_type_t **types[] = {&vm->native_method_descriptor_type, &vm->native_wrapper_descriptor_type};
    static const char *const names[] = {"method_descriptor", "wrapper_descriptor"};
    static const size_t name_sizes[] = {17U, 18U};

    for (size_t index = 0U; index < sizeof(types) / sizeof(types[0]); ++index) {
        tinypy_type_t *type = tinypy_internal_type_new_configured(vm, names[index], name_sizes[index], NULL, 0U, NULL, NULL, TINYPY_FALSE, TINYPY_FALSE, NULL);

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
        type->descriptor_get = tinypy_internal_native_function_descriptor_get;
        type->hash = __tinypy_native_function_hash_slot;
        type->create = NULL;
        type->flags = (type->flags | TINYPY_TYPE_FLAG_IMMUTABLE) & ~TINYPY_TYPE_FLAG_BASE_TYPE;
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
    if (__tinypy_native_function_method_arguments(vm, args, kwargs, 2U, 3U, out_error) == 0) {
        return NULL;
    }
    descriptor = TINYPY_TUPLE_GET(args, 0U);
    instance = TINYPY_TUPLE_GET(args, 1U);
    owner = TINYPY_NATIVE_FUNCTION_OBJECT(descriptor)->owner;
    if (TINYPY_VALUE_KIND(instance) == TINYPY_VALUE_NONE) {
        instance = NULL;
    }
    if (count == 3U && TINYPY_VALUE_KIND(TINYPY_TUPLE_GET(args, 2U)) != TINYPY_VALUE_NONE) {
        if (TINYPY_VALUE_KIND(TINYPY_TUPLE_GET(args, 2U)) != TINYPY_VALUE_TYPE) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "descriptor owner must be a type", out_error);
            return NULL;
        }
        owner = (tinypy_type_t *)TINYPY_TUPLE_GET(args, 2U);
    }
    tinypy_value_t *return_value_1 = tinypy_internal_native_function_descriptor_get(descriptor, instance, owner, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_native_function_compare_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    if (__tinypy_native_function_method_arguments(vm, args, kwargs, 2U, 2U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *result = tinypy_internal_compare_builtin_value(TINYPY_TUPLE_GET(args, 0U), TINYPY_TUPLE_GET(args, 1U), (tinypy_compare_operation_e)(intptr_t)user_data, out_error);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_native_function_cmp_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_native_function_method_arguments(vm, args, kwargs, 2U, 2U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *left = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *right = TINYPY_TUPLE_GET(args, 1U);
    int64_t order = left == right ? INT64_C(0) : ((uintptr_t)left < (uintptr_t)right ? INT64_C(-1) : INT64_C(1));
    tinypy_value_t *result = tinypy_integer_from_i64(vm, order);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_native_function_add_type_method(tinypy_vm_t *vm, tinypy_type_t *type, const char *name, size_t name_size, tinypy_native_function_callback_t callback, void *user_data) {
    tinypy_value_t *function = tinypy_native_function_new(vm, name, name_size, callback, user_data, NULL);

    tinypy_type_set_attr(type, name, name_size, function);
    TINYPY_DECREF(function);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_native_function_add_type_property(tinypy_vm_t *vm, tinypy_type_t *type, const char *name, size_t name_size, intptr_t field) {
    tinypy_value_t *getter = tinypy_native_function_new(vm, name, name_size, __tinypy_native_function_attribute_method, (void *)field, NULL);
    tinypy_value_t *property = tinypy_property_new(vm, getter, NULL, NULL, NULL);

    tinypy_type_set_attr(type, name, name_size, property);
    TINYPY_DECREF(property);
    TINYPY_DECREF(getter);
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_initialize_native_function_type(tinypy_vm_t *vm) {
    static const struct {
        const char *name;
        size_t name_size;
        tinypy_compare_operation_e operation;
    } comparisons[] = {
        {"__lt__", 6U, TINYPY_COMPARE_LESS},
        {"__le__", 6U, TINYPY_COMPARE_LESS_EQUAL},
        {"__eq__", 6U, TINYPY_COMPARE_EQUAL},
        {"__ne__", 6U, TINYPY_COMPARE_NOT_EQUAL},
        {"__gt__", 6U, TINYPY_COMPARE_GREATER},
        {"__ge__", 6U, TINYPY_COMPARE_GREATER_EQUAL}};

    tinypy_type_t *function_type = &vm->types[TINYPY_VALUE_NATIVE_FUNCTION];
    tinypy_type_t *descriptor_types[] = {vm->native_method_descriptor_type, vm->native_wrapper_descriptor_type};

    __tinypy_native_function_add_type_property(vm, function_type, "__name__", 8U, 0);
    __tinypy_native_function_add_type_property(vm, function_type, "__self__", 8U, 1);
    __tinypy_native_function_add_type_property(vm, function_type, "__module__", 10U, 2);
    __tinypy_native_function_add_type_method(vm, function_type, "__call__", 8U, __tinypy_native_function_call_method, NULL);
    __tinypy_native_function_add_type_method(vm, function_type, "__repr__", 8U, __tinypy_native_function_repr_method, NULL);
    __tinypy_native_function_add_type_method(vm, function_type, "__hash__", 8U, __tinypy_native_function_hash_method, NULL);
    __tinypy_native_function_add_type_method(vm, function_type, "__cmp__", 7U, __tinypy_native_function_cmp_method, NULL);
    for (size_t index = 0U; index < sizeof(comparisons) / sizeof(comparisons[0]); ++index) {
        __tinypy_native_function_add_type_method(vm, function_type, comparisons[index].name, comparisons[index].name_size, __tinypy_native_function_compare_method, (void *)(intptr_t)comparisons[index].operation);
    }
    for (size_t type_index = 0U; type_index < sizeof(descriptor_types) / sizeof(descriptor_types[0]); ++type_index) {
        tinypy_type_t *type = descriptor_types[type_index];

        __tinypy_native_function_add_type_property(vm, type, "__name__", 8U, 0);
        __tinypy_native_function_add_type_property(vm, type, "__objclass__", 12U, 3);
        __tinypy_native_function_add_type_property(vm, type, "__doc__", 7U, 4);
        __tinypy_native_function_add_type_method(vm, type, "__call__", 8U, __tinypy_native_function_call_method, NULL);
        __tinypy_native_function_add_type_method(vm, type, "__get__", 7U, __tinypy_native_descriptor_get_method, NULL);
        __tinypy_native_function_add_type_method(vm, type, "__repr__", 8U, __tinypy_native_function_repr_method, NULL);
        __tinypy_native_function_add_type_method(vm, type, "__hash__", 8U, __tinypy_native_function_hash_method, NULL);
        __tinypy_native_function_add_type_method(vm, type, "__cmp__", 7U, __tinypy_native_function_cmp_method, NULL);
        for (size_t index = 0U; index < sizeof(comparisons) / sizeof(comparisons[0]); ++index) {
            __tinypy_native_function_add_type_method(vm, type, comparisons[index].name, comparisons[index].name_size, __tinypy_native_function_compare_method, (void *)(intptr_t)comparisons[index].operation);
        }
    }
    vm->types[TINYPY_VALUE_NATIVE_FUNCTION].hash = __tinypy_native_function_hash_slot;
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
    tinypy_type_t *type = tinypy_internal_type_new_configured(vm, name, name_size, bases, base_count, NULL, namespace_dict, has_instance_dict, has_weakrefs, out_error);
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
void tinypy_internal_native_instance_destroy(tinypy_value_t *value) {
    tinypy_native_type_spec_t *spec = &value->type->native_spec;

    if (spec->finalize != NULL) {
        void *native_payload = __tinypy_internal_native_payload(value);
        spec->finalize(value, native_payload, spec->user_data);
    }
}
