#include "tinypy/native.h"

#include "internal.h"

#include <string.h>
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_native_function_new(tinypy_vm_t *vm, const char *name, size_t name_size, tinypy_native_function_callback_t callback, void *user_data, tinypy_native_function_finalize_t finalize) {
    tinypy_native_function_object_t *function = (tinypy_native_function_object_t *)tinypy_internal_value_allocate(vm, TINYPY_VALUE_NATIVE_FUNCTION, sizeof(*function));
    function->name = tinypy_string_from_bytes(vm, name, name_size);
    function->function = NULL;
    function->self = NULL;
    function->callback = callback;
    function->user_data = user_data;
    function->finalize = finalize;
    return &function->base;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_native_function_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data) {
    tinypy_native_function_object_t *function = TINYPY_NATIVE_FUNCTION_OBJECT(value);

    visit(function->name, user_data);
    if (function->function != NULL) {
        visit(function->function, user_data);
    }
    if (function->self != NULL) {
        visit(function->self, user_data);
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
        size_t argument_count = TINYPY_TUPLE_SIZE(args);
        size_t output_count;
        tinypy_value_t **items;
        size_t index;

        output_count = argument_count + 1U;
        items = (tinypy_value_t **)tinypy_internal_vm_allocate(vm, output_count * sizeof(*items));
        items[0] = function->self;
        for (index = 0U; index < argument_count; ++index) {
            items[index + 1U] = TINYPY_TUPLE_GET(args, index);
        }
        call_args = tinypy_tuple_from_items(vm, items, output_count);
        tinypy_internal_vm_deallocate(vm, items, output_count * sizeof(*items));
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

    (void)owner;
    TINYPY_CLEAR_ERROR(out_error);
    if (instance == NULL || function->self != NULL) {
        TINYPY_INCREF(descriptor);
        return descriptor;
    }

    tinypy_vm_t *vm = TINYPY_VALUE_VM(descriptor);
    tinypy_native_function_object_t *method = (tinypy_native_function_object_t *)tinypy_internal_value_allocate(vm, TINYPY_VALUE_NATIVE_FUNCTION, sizeof(*method));

    method->name = function->name;
    method->function = descriptor;
    method->self = instance;
    method->callback = function->callback;
    method->user_data = function->user_data;
    method->finalize = NULL;
    TINYPY_INCREF(method->name);
    TINYPY_INCREF(method->function);
    TINYPY_INCREF(method->self);
    return &method->base;
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
    return (uint8_t *)instance + instance->type->native_payload_offset;
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

    TINYPY_CLEAR_ERROR(out_error);
    if (__tinypy_internal_native_validate_spec(vm, spec, out_error) == 0) {
        return NULL;
    }
    tinypy_type_t *type = tinypy_type_new(vm, name, name_size, bases, base_count, NULL, namespace_dict, out_error);
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
    (void)memset(&type->native_spec, 0, sizeof(type->native_spec));
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
    type->dict_offset = offsetof(tinypy_native_instance_object_t, dict);
    type->weakref_offset = offsetof(tinypy_native_instance_object_t, weakrefs);
    type->has_instance_dict = INT32_C(1);
    type->release_references = tinypy_internal_native_instance_release_references;
    type->destroy = tinypy_internal_native_instance_destroy;
    __tinypy_internal_native_configure_slots(type);
    return type;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_native_type_update_spec(tinypy_type_t *type, const tinypy_native_type_spec_t *spec, tinypy_error_t **out_error) {
    size_t copied_size;

    TINYPY_CLEAR_ERROR(out_error);
    if (type->layout_kind != TINYPY_VALUE_NATIVE_INSTANCE) {
        tinypy_internal_make_vm_error(type->vm, TINYPY_ERROR_TYPE, "type does not have a native layout", out_error);
        return TINYPY_FALSE;
    }
    if (__tinypy_internal_native_validate_spec(type->vm, spec, out_error) == 0) {
        return TINYPY_FALSE;
    }
    if (type->native_payload_size != spec->payload_size || type->native_payload_alignment != spec->payload_alignment || type->native_spec.construct != spec->construct || type->native_spec.finalize != spec->finalize || type->native_spec.user_data != spec->user_data) {
        tinypy_internal_make_vm_error(type->vm, TINYPY_ERROR_TYPE, "native type layout and lifetime callbacks are immutable", out_error);
        return TINYPY_FALSE;
    }
    copied_size = spec->struct_size < sizeof(type->native_spec) ? spec->struct_size : sizeof(type->native_spec);
    (void)memcpy(&type->native_spec, spec, copied_size);
    if (copied_size < sizeof(type->native_spec)) {
        (void)memset((uint8_t *)&type->native_spec + copied_size, 0, sizeof(type->native_spec) - copied_size);
    }
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
