#include "tinypy/descriptor.h"

#include "internal.h"

#include <assert.h>

//////////////////////////////////////////////////////////////////////////
static tinypy_callable_descriptor_object_t *__tinypy_internal_callable_descriptor_validate(const tinypy_value_t *value, tinypy_value_type_e kind) {
    assert(value != NULL);
    assert(tinypy_internal_vm_valid(tinypy_internal_value_vm(value)));
    assert(tinypy_internal_value_kind(value) == kind);
    (void)kind;
    return TINYPY_CALLABLE_DESCRIPTOR_OBJECT((tinypy_value_t *)value);
}

//////////////////////////////////////////////////////////////////////////
static tinypy_property_object_t *__tinypy_internal_property_validate(const tinypy_value_t *value) {
    assert(value != NULL);
    assert(tinypy_internal_vm_valid(tinypy_internal_value_vm(value)));
    assert(tinypy_internal_value_kind(value) == TINYPY_VALUE_PROPERTY);
    return TINYPY_PROPERTY_OBJECT((tinypy_value_t *)value);
}

typedef enum tinypy_internal_c_descriptor_field_e {
    TINYPY_INTERNAL_C_DESCRIPTOR_FUNCTION_CODE = 1,
    TINYPY_INTERNAL_C_DESCRIPTOR_FUNCTION_GLOBALS = 2,
    TINYPY_INTERNAL_C_DESCRIPTOR_FUNCTION_DEFAULTS = 3,
    TINYPY_INTERNAL_C_DESCRIPTOR_FUNCTION_CLOSURE = 4,
    TINYPY_INTERNAL_C_DESCRIPTOR_FUNCTION_NAME = 5,
    TINYPY_INTERNAL_C_DESCRIPTOR_FUNCTION_DOC = 6,
    TINYPY_INTERNAL_C_DESCRIPTOR_FUNCTION_DICT = 7,
    TINYPY_INTERNAL_C_DESCRIPTOR_FUNCTION_MODULE = 8,
    TINYPY_INTERNAL_C_DESCRIPTOR_INSTANCE_SLOT = 9
} tinypy_internal_c_descriptor_field_e;

//////////////////////////////////////////////////////////////////////////
static tinypy_c_descriptor_object_t *__tinypy_internal_c_descriptor_validate(const tinypy_value_t *value) {
    assert(value != NULL);
    assert(tinypy_internal_value_kind(value) == TINYPY_VALUE_GETSET_DESCRIPTOR || tinypy_internal_value_kind(value) == TINYPY_VALUE_MEMBER_DESCRIPTOR);
    return TINYPY_C_DESCRIPTOR_OBJECT((tinypy_value_t *)value);
}

//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_c_descriptor_new(tinypy_vm_t *vm, tinypy_value_type_e kind, tinypy_type_t *owner, const char *name, size_t name_size, tinypy_internal_c_descriptor_field_e field, int32_t writable) {
    tinypy_c_descriptor_object_t *descriptor;

    assert(kind == TINYPY_VALUE_GETSET_DESCRIPTOR || kind == TINYPY_VALUE_MEMBER_DESCRIPTOR);
    assert(owner != NULL);
    assert(owner->vm == vm);
    assert(name != NULL || name_size == 0U);
    descriptor = (tinypy_c_descriptor_object_t *)tinypy_internal_value_allocate(vm, kind, sizeof(*descriptor));
    descriptor->owner = owner;
    descriptor->name = tinypy_string_from_bytes(vm, name, name_size);
    descriptor->field = (int32_t)field;
    descriptor->writable = writable;
    descriptor->owner_retained = INT32_C(1);
    tinypy_retain(&owner->base.base);
    return &descriptor->base;
}

//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_member_descriptor_new(tinypy_type_t *owner, tinypy_value_t *name, size_t index) {
    tinypy_vm_t *vm;
    tinypy_c_descriptor_object_t *descriptor;

    assert(owner != NULL);
    vm = owner->vm;
    assert(tinypy_internal_value_belongs_to(vm, name));
    assert(tinypy_internal_value_kind(name) == TINYPY_VALUE_STRING);
    assert(index < owner->slot_count);
    descriptor = (tinypy_c_descriptor_object_t *)tinypy_internal_value_allocate(vm, TINYPY_VALUE_MEMBER_DESCRIPTOR, sizeof(*descriptor));
    descriptor->owner = owner;
    descriptor->name = name;
    descriptor->index = index;
    descriptor->field = (int32_t)TINYPY_INTERNAL_C_DESCRIPTOR_INSTANCE_SLOT;
    descriptor->writable = INT32_C(1);
    descriptor->owner_retained = INT32_C(0);
    tinypy_retain(name);
    return &descriptor->base;
}

//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_c_descriptor_optional(tinypy_vm_t *vm, tinypy_value_t *value) {
    if (value == NULL) {
        return tinypy_none_get(vm);
    }
    tinypy_retain(value);
    return value;
}

//////////////////////////////////////////////////////////////////////////
static void __tinypy_internal_c_descriptor_replace(tinypy_value_t **target, tinypy_value_t *value) {
    tinypy_value_t *previous = *target;

    if (value != NULL) {
        tinypy_retain(value);
    }
    *target = value;
    if (previous != NULL) {
        tinypy_release(previous);
    }
}

//////////////////////////////////////////////////////////////////////////
static void __tinypy_internal_c_descriptor_readonly(tinypy_vm_t *vm, tinypy_error_t **out_error) {
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "readonly attribute", out_error);
}

//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_callable_descriptor_new(tinypy_value_t *callable, tinypy_value_type_e kind) {
    tinypy_vm_t *vm;
    tinypy_callable_descriptor_object_t *descriptor;

    assert(callable != NULL);
    vm = tinypy_internal_value_vm(callable);
    assert(tinypy_internal_vm_valid(vm));
    assert(callable->type->call != NULL);
    descriptor = (tinypy_callable_descriptor_object_t *)tinypy_internal_value_allocate(vm, kind, sizeof(*descriptor));
    descriptor->callable = callable;
    tinypy_retain(callable);
    return &descriptor->base;
}

//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_static_method_new(tinypy_value_t *callable) {
    return __tinypy_internal_callable_descriptor_new(callable, TINYPY_VALUE_STATIC_METHOD);
}

//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_class_method_new(tinypy_value_t *callable) {
    return __tinypy_internal_callable_descriptor_new(callable, TINYPY_VALUE_CLASS_METHOD);
}

//////////////////////////////////////////////////////////////////////////
void tinypy_internal_callable_descriptor_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data) {
    visit(TINYPY_CALLABLE_DESCRIPTOR_OBJECT(value)->callable, user_data);
}

//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_static_method_get(tinypy_value_t *descriptor, tinypy_value_t *instance, tinypy_type_t *owner, tinypy_error_t **out_error) {
    tinypy_value_t *callable = TINYPY_CALLABLE_DESCRIPTOR_OBJECT(descriptor)->callable;

    (void)instance;
    (void)owner;
    tinypy_internal_clear_error(out_error);
    tinypy_retain(callable);
    return callable;
}

//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_class_method_get(tinypy_value_t *descriptor, tinypy_value_t *instance, tinypy_type_t *owner, tinypy_error_t **out_error) {
    (void)instance;
    tinypy_internal_clear_error(out_error);
    assert(owner != NULL);
    return tinypy_method_new(TINYPY_CALLABLE_DESCRIPTOR_OBJECT(descriptor)->callable, &owner->base.base, &owner->base.base);
}

//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_static_method_callable(const tinypy_value_t *descriptor) {
    return __tinypy_internal_callable_descriptor_validate(descriptor, TINYPY_VALUE_STATIC_METHOD)->callable;
}

//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_class_method_callable(const tinypy_value_t *descriptor) {
    return __tinypy_internal_callable_descriptor_validate(descriptor, TINYPY_VALUE_CLASS_METHOD)->callable;
}

//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_property_new(tinypy_vm_t *vm, tinypy_value_t *getter, tinypy_value_t *setter, tinypy_value_t *deleter, tinypy_value_t *doc) {
    tinypy_property_object_t *property;

    assert(tinypy_internal_vm_valid(vm));
    assert(getter == NULL || tinypy_internal_value_belongs_to(vm, getter));
    assert(setter == NULL || tinypy_internal_value_belongs_to(vm, setter));
    assert(deleter == NULL || tinypy_internal_value_belongs_to(vm, deleter));
    assert(doc == NULL || tinypy_internal_value_belongs_to(vm, doc));
    property = (tinypy_property_object_t *)tinypy_internal_value_allocate(vm, TINYPY_VALUE_PROPERTY, sizeof(*property));
    property->getter = getter;
    property->setter = setter;
    property->deleter = deleter;
    property->doc = doc;
    if (getter != NULL) {
        tinypy_retain(getter);
    }
    if (setter != NULL) {
        tinypy_retain(setter);
    }
    if (deleter != NULL) {
        tinypy_retain(deleter);
    }
    if (doc != NULL) {
        tinypy_retain(doc);
    }
    return &property->base;
}

//////////////////////////////////////////////////////////////////////////
void tinypy_internal_property_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data) {
    tinypy_property_object_t *property = TINYPY_PROPERTY_OBJECT(value);

    if (property->getter != NULL) {
        visit(property->getter, user_data);
    }
    if (property->setter != NULL) {
        visit(property->setter, user_data);
    }
    if (property->deleter != NULL) {
        visit(property->deleter, user_data);
    }
    if (property->doc != NULL) {
        visit(property->doc, user_data);
    }
}

//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_property_get(tinypy_value_t *descriptor, tinypy_value_t *instance, tinypy_type_t *owner, tinypy_error_t **out_error) {
    tinypy_property_object_t *property = TINYPY_PROPERTY_OBJECT(descriptor);
    tinypy_vm_t *vm = tinypy_internal_value_vm(descriptor);
    tinypy_value_t *args;
    tinypy_value_t *result;

    (void)owner;
    tinypy_internal_clear_error(out_error);
    if (instance == NULL) {
        tinypy_retain(descriptor);
        return descriptor;
    }
    if (property->getter == NULL) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_RUNTIME, "unreadable property", out_error);
        return NULL;
    }
    args = tinypy_tuple_from_items(vm, &instance, 1U);
    result = tinypy_call(property->getter, args, NULL, out_error);
    tinypy_release(args);
    return result;
}

//////////////////////////////////////////////////////////////////////////
int32_t tinypy_internal_property_set(tinypy_value_t *descriptor, tinypy_value_t *instance, tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_property_object_t *property = TINYPY_PROPERTY_OBJECT(descriptor);
    tinypy_vm_t *vm = tinypy_internal_value_vm(descriptor);
    tinypy_value_t *callable = value != NULL ? property->setter : property->deleter;
    tinypy_value_t *items[2];
    tinypy_value_t *args;
    tinypy_value_t *result;
    size_t count = value != NULL ? 2U : 1U;

    tinypy_internal_clear_error(out_error);
    if (callable == NULL) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_RUNTIME, value != NULL ? "property has no setter" : "property has no deleter", out_error);
        return 0;
    }
    items[0] = instance;
    items[1] = value;
    args = tinypy_tuple_from_items(vm, items, count);
    result = tinypy_call(callable, args, NULL, out_error);
    tinypy_release(args);
    if (result == NULL) {
        return 0;
    }
    tinypy_release(result);
    return 1;
}

//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_descriptor_constructor(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error, int class_method) {
    tinypy_vm_t *vm = type->vm;

    if ((kwargs != NULL && tinypy_dict_size(kwargs) != 0U) || tinypy_tuple_size(args) != 1U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "descriptor constructor requires one callable", out_error);
        return NULL;
    }
    if (tinypy_tuple_get(args, 0U)->type->call == NULL) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "descriptor argument is not callable", out_error);
        return NULL;
    }
    tinypy_value_t *selected_value;
    if (class_method != 0) {
        tinypy_value_t *item = tinypy_tuple_get(args, 0U);
        selected_value = tinypy_class_method_new(item);
    }
    else {
        tinypy_value_t *item = tinypy_tuple_get(args, 0U);
        selected_value = tinypy_static_method_new(item);
    }
    return selected_value;
}

//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_static_method_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    return __tinypy_internal_descriptor_constructor(type, args, kwargs, out_error, 0);
}

//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_class_method_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    return __tinypy_internal_descriptor_constructor(type, args, kwargs, out_error, 1);
}

//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_property_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = type->vm;
    tinypy_value_t *values[4] = {NULL, NULL, NULL, NULL};
    size_t count = tinypy_tuple_size(args);
    size_t index;

    if (kwargs != NULL && tinypy_dict_size(kwargs) != 0U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "property keyword arguments are not implemented", out_error);
        return NULL;
    }
    if (count > 4U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "property accepts at most four arguments", out_error);
        return NULL;
    }
    for (index = 0U; index < count; index += 1U) {
        tinypy_value_t *value = tinypy_tuple_get(args, index);

        values[index] = tinypy_internal_value_kind(value) == TINYPY_VALUE_NONE ? NULL : value;
    }
    if (values[3] == NULL && values[0] != NULL && tinypy_internal_value_kind(values[0]) == TINYPY_VALUE_FUNCTION) {
        values[3] = tinypy_function_doc(values[0]);
    }
    return tinypy_property_new(vm, values[0], values[1], values[2], values[3]);
}

//////////////////////////////////////////////////////////////////////////
static int __tinypy_internal_property_method_arguments(tinypy_vm_t *vm, tinypy_value_t *args, tinypy_value_t *kwargs, size_t count, tinypy_error_t **out_error) {
    if ((kwargs != NULL && tinypy_dict_size(kwargs) != 0U) || tinypy_tuple_size(args) != count) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "property method received invalid arguments", out_error);
        return 0;
    }
    tinypy_value_t *item = tinypy_tuple_get(args, 0U);
    if (tinypy_internal_value_kind(item) != TINYPY_VALUE_PROPERTY) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "property method requires a property object", out_error);
        return 0;
    }
    return 1;
}

//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_property_copy(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, int field, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);
    tinypy_property_object_t *property;
    tinypy_value_t *replacement;
    tinypy_value_t *getter;
    tinypy_value_t *setter;
    tinypy_value_t *deleter;

    if (__tinypy_internal_property_method_arguments(vm, args, kwargs, 2U, out_error) == 0) {
        return NULL;
    }
    property = TINYPY_PROPERTY_OBJECT(tinypy_tuple_get(args, 0U));
    replacement = tinypy_tuple_get(args, 1U);
    if (tinypy_internal_value_kind(replacement) == TINYPY_VALUE_NONE) {
        replacement = NULL;
    }
    if (replacement != NULL && replacement->type->call == NULL) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "property accessor must be callable", out_error);
        return NULL;
    }
    getter = field == 0 ? replacement : property->getter;
    setter = field == 1 ? replacement : property->setter;
    deleter = field == 2 ? replacement : property->deleter;
    return tinypy_property_new(vm, getter, setter, deleter, property->doc);
}

//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_property_getter_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    (void)user_data;
    return __tinypy_internal_property_copy(function, args, kwargs, 0, out_error);
}

//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_property_setter_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    (void)user_data;
    return __tinypy_internal_property_copy(function, args, kwargs, 1, out_error);
}

//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_property_deleter_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    (void)user_data;
    return __tinypy_internal_property_copy(function, args, kwargs, 2, out_error);
}

//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_property_field(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, int field, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);
    tinypy_property_object_t *property;
    tinypy_value_t *result;

    if (__tinypy_internal_property_method_arguments(vm, args, kwargs, 1U, out_error) == 0) {
        return NULL;
    }
    property = TINYPY_PROPERTY_OBJECT(tinypy_tuple_get(args, 0U));
    switch (field) {
    case 0:
        result = property->getter;
        break;
    case 1:
        result = property->setter;
        break;
    case 2:
        result = property->deleter;
        break;
    default:
        result = property->doc;
        break;
    }
    if (result == NULL) {
        return tinypy_none_get(vm);
    }
    tinypy_retain(result);
    return result;
}

//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_property_fget(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    (void)user_data;
    return __tinypy_internal_property_field(function, args, kwargs, 0, out_error);
}

//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_property_fset(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    (void)user_data;
    return __tinypy_internal_property_field(function, args, kwargs, 1, out_error);
}

//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_property_fdel(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    (void)user_data;
    return __tinypy_internal_property_field(function, args, kwargs, 2, out_error);
}

//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_property_doc_value(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    (void)user_data;
    return __tinypy_internal_property_field(function, args, kwargs, 3, out_error);
}

//////////////////////////////////////////////////////////////////////////
static void __tinypy_internal_type_dict_set(tinypy_vm_t *vm, tinypy_type_t *type, const char *name, size_t name_size, tinypy_value_t *value) {
    tinypy_value_t *key = tinypy_string_from_bytes(vm, name, name_size);

    tinypy_dict_set(type->dict, key, value);
    tinypy_release(key);
}

//////////////////////////////////////////////////////////////////////////
static void __tinypy_internal_property_method_set(tinypy_vm_t *vm, const char *name, size_t name_size, tinypy_native_function_callback_t callback) {
    tinypy_value_t *function = tinypy_native_function_new(vm, name, name_size, callback, NULL, NULL);

    __tinypy_internal_type_dict_set(vm, &vm->property_type, name, name_size, function);
    tinypy_release(function);
}

//////////////////////////////////////////////////////////////////////////
static void __tinypy_internal_property_field_set(tinypy_vm_t *vm, const char *name, size_t name_size, tinypy_native_function_callback_t callback) {
    tinypy_value_t *function = tinypy_native_function_new(vm, name, name_size, callback, NULL, NULL);
    tinypy_value_t *descriptor = tinypy_property_new(vm, function, NULL, NULL, NULL);

    __tinypy_internal_type_dict_set(vm, &vm->property_type, name, name_size, descriptor);
    tinypy_release(descriptor);
    tinypy_release(function);
}

//////////////////////////////////////////////////////////////////////////
void tinypy_internal_c_descriptor_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data) {
    tinypy_c_descriptor_object_t *descriptor = __tinypy_internal_c_descriptor_validate(value);

    if (descriptor->owner_retained != 0) {
        visit(&descriptor->owner->base.base, user_data);
    }
    visit(descriptor->name, user_data);
}

//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_c_descriptor_get(tinypy_value_t *descriptor_value, tinypy_value_t *instance, tinypy_type_t *owner, tinypy_error_t **out_error) {
    tinypy_c_descriptor_object_t *descriptor = __tinypy_internal_c_descriptor_validate(descriptor_value);
    tinypy_vm_t *vm = tinypy_internal_value_vm(descriptor_value);
    tinypy_function_object_t *function;

    (void)owner;
    tinypy_internal_clear_error(out_error);
    if (instance == NULL) {
        tinypy_retain(descriptor_value);
        return descriptor_value;
    }
    if (tinypy_type_is_subtype(instance->type, descriptor->owner) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "descriptor does not apply to this object", out_error);
        return NULL;
    }
    if ((tinypy_internal_c_descriptor_field_e)descriptor->field == TINYPY_INTERNAL_C_DESCRIPTOR_INSTANCE_SLOT) {
        tinypy_value_t *slot_value;

        if (instance->type->slots_offset == 0U) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "slot descriptor requires an instance", out_error);
            return NULL;
        }
        slot_value = *tinypy_internal_object_member_slot(instance, descriptor->index);
        if (slot_value == NULL) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_ATTRIBUTE, "slot attribute is not set", out_error);
            return NULL;
        }
        tinypy_retain(slot_value);
        return slot_value;
    }
    if (tinypy_internal_value_kind(instance) != TINYPY_VALUE_FUNCTION) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "descriptor does not apply to this object", out_error);
        return NULL;
    }
    function = TINYPY_FUNCTION_OBJECT(instance);
    switch ((tinypy_internal_c_descriptor_field_e)descriptor->field) {
    case TINYPY_INTERNAL_C_DESCRIPTOR_FUNCTION_CODE:
        tinypy_retain(function->code);
        return function->code;
    case TINYPY_INTERNAL_C_DESCRIPTOR_FUNCTION_GLOBALS:
        tinypy_retain(function->globals);
        return function->globals;
    case TINYPY_INTERNAL_C_DESCRIPTOR_FUNCTION_DEFAULTS:
        return __tinypy_internal_c_descriptor_optional(vm, function->defaults);
    case TINYPY_INTERNAL_C_DESCRIPTOR_FUNCTION_CLOSURE:
        return __tinypy_internal_c_descriptor_optional(vm, function->closure);
    case TINYPY_INTERNAL_C_DESCRIPTOR_FUNCTION_NAME:
        tinypy_retain(function->name);
        return function->name;
    case TINYPY_INTERNAL_C_DESCRIPTOR_FUNCTION_DOC:
        return __tinypy_internal_c_descriptor_optional(vm, function->doc);
    case TINYPY_INTERNAL_C_DESCRIPTOR_FUNCTION_DICT:
        if (function->dict == NULL) {
            function->dict = tinypy_dict_new(vm);
        }
        tinypy_retain(function->dict);
        return function->dict;
    case TINYPY_INTERNAL_C_DESCRIPTOR_FUNCTION_MODULE:
        return __tinypy_internal_c_descriptor_optional(vm, function->module);
    default:
        assert(!"invalid C descriptor field");
        return NULL;
    }
}

//////////////////////////////////////////////////////////////////////////
int32_t tinypy_internal_c_descriptor_set(tinypy_value_t *descriptor_value, tinypy_value_t *instance, tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_c_descriptor_object_t *descriptor = __tinypy_internal_c_descriptor_validate(descriptor_value);
    tinypy_vm_t *vm = tinypy_internal_value_vm(descriptor_value);
    tinypy_function_object_t *function;
    tinypy_internal_c_descriptor_field_e field = (tinypy_internal_c_descriptor_field_e)descriptor->field;

    tinypy_internal_clear_error(out_error);
    if (tinypy_type_is_subtype(instance->type, descriptor->owner) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "descriptor does not apply to this object", out_error);
        return INT32_C(0);
    }
    if (descriptor->writable == 0) {
        __tinypy_internal_c_descriptor_readonly(vm, out_error);
        return INT32_C(0);
    }
    if (field == TINYPY_INTERNAL_C_DESCRIPTOR_INSTANCE_SLOT) {
        tinypy_value_t **slot;
        tinypy_value_t *previous;

        if (instance->type->slots_offset == 0U) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "slot descriptor requires an instance", out_error);
            return INT32_C(0);
        }
        slot = tinypy_internal_object_member_slot(instance, descriptor->index);
        previous = *slot;
        if (value != NULL) {
            tinypy_retain(value);
        }
        *slot = value;
        if (previous != NULL) {
            tinypy_release(previous);
        }
        return INT32_C(1);
    }
    if (tinypy_internal_value_kind(instance) != TINYPY_VALUE_FUNCTION) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "descriptor does not apply to this object", out_error);
        return INT32_C(0);
    }
    function = TINYPY_FUNCTION_OBJECT(instance);
    switch (field) {
    case TINYPY_INTERNAL_C_DESCRIPTOR_FUNCTION_CODE:
        if (value == NULL) {
            __tinypy_internal_c_descriptor_readonly(vm, out_error);
            return INT32_C(0);
        }
        if (tinypy_internal_value_kind(value) != TINYPY_VALUE_CODE) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "func_code must be a code object", out_error);
            return INT32_C(0);
        }
        int condition = function->closure != NULL;
        if (condition != 0) {
            tinypy_value_t *freevars = tinypy_code_freevars(value);
            condition = tinypy_tuple_size(function->closure) != tinypy_tuple_size(freevars);
        }
        if (condition) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "func_code has incompatible free variables", out_error);
            return INT32_C(0);
        }
        __tinypy_internal_c_descriptor_replace(&function->code, value);
        return INT32_C(1);
    case TINYPY_INTERNAL_C_DESCRIPTOR_FUNCTION_DEFAULTS:
        if (value != NULL && tinypy_internal_value_kind(value) == TINYPY_VALUE_NONE) {
            value = NULL;
        }
        if (value != NULL && tinypy_internal_value_kind(value) != TINYPY_VALUE_TUPLE) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "func_defaults must be a tuple", out_error);
            return INT32_C(0);
        }
        __tinypy_internal_c_descriptor_replace(&function->defaults, value);
        return INT32_C(1);
    case TINYPY_INTERNAL_C_DESCRIPTOR_FUNCTION_NAME:
        if (value == NULL || tinypy_internal_value_kind(value) != TINYPY_VALUE_STRING) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "func_name must be a string", out_error);
            return INT32_C(0);
        }
        __tinypy_internal_c_descriptor_replace(&function->name, value);
        return INT32_C(1);
    case TINYPY_INTERNAL_C_DESCRIPTOR_FUNCTION_DOC:
        if (value == NULL) {
            value = tinypy_none_get(vm);
        }
        else {
            tinypy_retain(value);
        }
        __tinypy_internal_c_descriptor_replace(&function->doc, value);
        tinypy_release(value);
        return INT32_C(1);
    case TINYPY_INTERNAL_C_DESCRIPTOR_FUNCTION_DICT:
        if (value == NULL || tinypy_internal_value_kind(value) != TINYPY_VALUE_DICT) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "func_dict must be a dictionary", out_error);
            return INT32_C(0);
        }
        __tinypy_internal_c_descriptor_replace(&function->dict, value);
        return INT32_C(1);
    case TINYPY_INTERNAL_C_DESCRIPTOR_FUNCTION_MODULE:
        if (value == NULL) {
            value = tinypy_none_get(vm);
        }
        else {
            tinypy_retain(value);
        }
        __tinypy_internal_c_descriptor_replace(&function->module, value);
        tinypy_release(value);
        return INT32_C(1);
    case TINYPY_INTERNAL_C_DESCRIPTOR_FUNCTION_GLOBALS:
    case TINYPY_INTERNAL_C_DESCRIPTOR_FUNCTION_CLOSURE:
        __tinypy_internal_c_descriptor_readonly(vm, out_error);
        return INT32_C(0);
    default:
        assert(!"invalid C descriptor field");
        return INT32_C(0);
    }
}

//////////////////////////////////////////////////////////////////////////
static void __tinypy_internal_function_descriptor_set(tinypy_vm_t *vm, tinypy_value_type_e kind, const char *name, size_t name_size, tinypy_internal_c_descriptor_field_e field, int32_t writable) {
    tinypy_value_t *descriptor = __tinypy_internal_c_descriptor_new(vm, kind, &vm->function_type, name, name_size, field, writable);

    __tinypy_internal_type_dict_set(vm, &vm->function_type, name, name_size, descriptor);
    tinypy_release(descriptor);
}

//////////////////////////////////////////////////////////////////////////
void tinypy_internal_initialize_descriptor_types(tinypy_vm_t *vm) {
    __tinypy_internal_property_method_set(vm, "getter", 6U, __tinypy_internal_property_getter_method);
    __tinypy_internal_property_method_set(vm, "setter", 6U, __tinypy_internal_property_setter_method);
    __tinypy_internal_property_method_set(vm, "deleter", 7U, __tinypy_internal_property_deleter_method);
    __tinypy_internal_property_field_set(vm, "fget", 4U, __tinypy_internal_property_fget);
    __tinypy_internal_property_field_set(vm, "fset", 4U, __tinypy_internal_property_fset);
    __tinypy_internal_property_field_set(vm, "fdel", 4U, __tinypy_internal_property_fdel);
    __tinypy_internal_property_field_set(vm, "__doc__", 7U, __tinypy_internal_property_doc_value);
    __tinypy_internal_function_descriptor_set(vm, TINYPY_VALUE_GETSET_DESCRIPTOR, "func_code", 9U, TINYPY_INTERNAL_C_DESCRIPTOR_FUNCTION_CODE, INT32_C(1));
    __tinypy_internal_function_descriptor_set(vm, TINYPY_VALUE_MEMBER_DESCRIPTOR, "func_globals", 12U, TINYPY_INTERNAL_C_DESCRIPTOR_FUNCTION_GLOBALS, INT32_C(0));
    __tinypy_internal_function_descriptor_set(vm, TINYPY_VALUE_GETSET_DESCRIPTOR, "func_defaults", 13U, TINYPY_INTERNAL_C_DESCRIPTOR_FUNCTION_DEFAULTS, INT32_C(1));
    __tinypy_internal_function_descriptor_set(vm, TINYPY_VALUE_GETSET_DESCRIPTOR, "func_closure", 12U, TINYPY_INTERNAL_C_DESCRIPTOR_FUNCTION_CLOSURE, INT32_C(0));
    __tinypy_internal_function_descriptor_set(vm, TINYPY_VALUE_GETSET_DESCRIPTOR, "func_name", 9U, TINYPY_INTERNAL_C_DESCRIPTOR_FUNCTION_NAME, INT32_C(1));
    __tinypy_internal_function_descriptor_set(vm, TINYPY_VALUE_GETSET_DESCRIPTOR, "func_doc", 8U, TINYPY_INTERNAL_C_DESCRIPTOR_FUNCTION_DOC, INT32_C(1));
    __tinypy_internal_function_descriptor_set(vm, TINYPY_VALUE_GETSET_DESCRIPTOR, "func_dict", 9U, TINYPY_INTERNAL_C_DESCRIPTOR_FUNCTION_DICT, INT32_C(1));
    __tinypy_internal_function_descriptor_set(vm, TINYPY_VALUE_GETSET_DESCRIPTOR, "__module__", 10U, TINYPY_INTERNAL_C_DESCRIPTOR_FUNCTION_MODULE, INT32_C(1));
    __tinypy_internal_function_descriptor_set(vm, TINYPY_VALUE_GETSET_DESCRIPTOR, "__code__", 8U, TINYPY_INTERNAL_C_DESCRIPTOR_FUNCTION_CODE, INT32_C(1));
    __tinypy_internal_function_descriptor_set(vm, TINYPY_VALUE_MEMBER_DESCRIPTOR, "__globals__", 11U, TINYPY_INTERNAL_C_DESCRIPTOR_FUNCTION_GLOBALS, INT32_C(0));
    __tinypy_internal_function_descriptor_set(vm, TINYPY_VALUE_GETSET_DESCRIPTOR, "__defaults__", 12U, TINYPY_INTERNAL_C_DESCRIPTOR_FUNCTION_DEFAULTS, INT32_C(1));
    __tinypy_internal_function_descriptor_set(vm, TINYPY_VALUE_GETSET_DESCRIPTOR, "__closure__", 11U, TINYPY_INTERNAL_C_DESCRIPTOR_FUNCTION_CLOSURE, INT32_C(0));
    __tinypy_internal_function_descriptor_set(vm, TINYPY_VALUE_GETSET_DESCRIPTOR, "__name__", 8U, TINYPY_INTERNAL_C_DESCRIPTOR_FUNCTION_NAME, INT32_C(1));
    __tinypy_internal_function_descriptor_set(vm, TINYPY_VALUE_GETSET_DESCRIPTOR, "__doc__", 7U, TINYPY_INTERNAL_C_DESCRIPTOR_FUNCTION_DOC, INT32_C(1));
    __tinypy_internal_function_descriptor_set(vm, TINYPY_VALUE_GETSET_DESCRIPTOR, "__dict__", 8U, TINYPY_INTERNAL_C_DESCRIPTOR_FUNCTION_DICT, INT32_C(1));
}

//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_property_getter(const tinypy_value_t *property) {
    return __tinypy_internal_property_validate(property)->getter;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_property_setter(const tinypy_value_t *property) {
    return __tinypy_internal_property_validate(property)->setter;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_property_deleter(const tinypy_value_t *property) {
    return __tinypy_internal_property_validate(property)->deleter;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_property_doc(const tinypy_value_t *property) {
    return __tinypy_internal_property_validate(property)->doc;
}
