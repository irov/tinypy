#include "tinypy/descriptor.h"

#include "internal.h"

#include <assert.h>

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
static tinypy_value_t *__tinypy_internal_c_descriptor_new(tinypy_vm_t *vm, tinypy_value_type_e kind, tinypy_type_t *owner, const char *name, size_t name_size, tinypy_internal_c_descriptor_field_e field, int32_t writable) {
    assert(kind == TINYPY_VALUE_GETSET_DESCRIPTOR || kind == TINYPY_VALUE_MEMBER_DESCRIPTOR);
    assert(owner != NULL);
    assert(owner->vm == vm);
    assert(name != NULL || name_size == 0U);
    tinypy_c_descriptor_object_t *descriptor = (tinypy_c_descriptor_object_t *)tinypy_internal_value_allocate(vm, kind, sizeof(*descriptor));
    descriptor->owner = owner;
    descriptor->name = tinypy_string_from_bytes(vm, name, name_size);
    descriptor->field = (int32_t)field;
    descriptor->writable = writable;
    descriptor->owner_retained = INT32_C(1);
    TINYPY_INCREF(&owner->base.base);
    return &descriptor->base;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_member_descriptor_new(tinypy_type_t *owner, tinypy_value_t *name, size_t index) {
    assert(owner != NULL);
    tinypy_vm_t *vm = owner->vm;
    assert(tinypy_internal_value_belongs_to(vm, name));
    assert(TINYPY_VALUE_KIND(name) == TINYPY_VALUE_STRING);
    assert(index < owner->slot_count);
    tinypy_c_descriptor_object_t *descriptor = (tinypy_c_descriptor_object_t *)tinypy_internal_value_allocate(vm, TINYPY_VALUE_MEMBER_DESCRIPTOR, sizeof(*descriptor));
    descriptor->owner = owner;
    descriptor->name = name;
    descriptor->index = index;
    descriptor->field = (int32_t)TINYPY_INTERNAL_C_DESCRIPTOR_INSTANCE_SLOT;
    descriptor->writable = INT32_C(1);
    descriptor->owner_retained = INT32_C(0);
    TINYPY_INCREF(name);
    return &descriptor->base;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_c_descriptor_optional(tinypy_vm_t *vm, tinypy_value_t *value) {
    if (value == NULL) {
        return tinypy_none_get(vm);
    }
    TINYPY_INCREF(value);
    return value;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_internal_c_descriptor_replace(tinypy_value_t **target, tinypy_value_t *value) {
    tinypy_value_t *previous = *target;

    if (value != NULL) {
        TINYPY_INCREF(value);
    }
    *target = value;
    if (previous != NULL) {
        TINYPY_DECREF(previous);
    }
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_internal_c_descriptor_readonly(tinypy_vm_t *vm, tinypy_error_t **out_error) {
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "readonly attribute", out_error);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_callable_descriptor_new(tinypy_value_t *callable, tinypy_value_type_e kind) {
    assert(callable != NULL);
    tinypy_vm_t *vm = TINYPY_VALUE_VM(callable);
    assert(tinypy_internal_vm_valid(vm));
    assert(callable->type->call != NULL);
    tinypy_callable_descriptor_object_t *descriptor = (tinypy_callable_descriptor_object_t *)tinypy_internal_value_allocate(vm, kind, sizeof(*descriptor));
    descriptor->callable = callable;
    TINYPY_INCREF(callable);
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
    TINYPY_CLEAR_ERROR(out_error);
    TINYPY_INCREF(callable);
    return callable;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_class_method_get(tinypy_value_t *descriptor, tinypy_value_t *instance, tinypy_type_t *owner, tinypy_error_t **out_error) {
    (void)instance;
    TINYPY_CLEAR_ERROR(out_error);
    assert(owner != NULL);
    return tinypy_method_new(TINYPY_CALLABLE_DESCRIPTOR_OBJECT(descriptor)->callable, &owner->base.base, &owner->base.base);
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_static_method_callable(const tinypy_value_t *descriptor) {
    assert(descriptor != NULL);
    assert(tinypy_internal_vm_valid(TINYPY_VALUE_VM(descriptor)));
    assert(TINYPY_VALUE_KIND(descriptor) == TINYPY_VALUE_STATIC_METHOD);
    return TINYPY_CALLABLE_DESCRIPTOR_OBJECT((tinypy_value_t *)descriptor)->callable;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_class_method_callable(const tinypy_value_t *descriptor) {
    assert(descriptor != NULL);
    assert(tinypy_internal_vm_valid(TINYPY_VALUE_VM(descriptor)));
    assert(TINYPY_VALUE_KIND(descriptor) == TINYPY_VALUE_CLASS_METHOD);
    return TINYPY_CALLABLE_DESCRIPTOR_OBJECT((tinypy_value_t *)descriptor)->callable;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_property_new(tinypy_vm_t *vm, tinypy_value_t *getter, tinypy_value_t *setter, tinypy_value_t *deleter, tinypy_value_t *doc) {
    assert(tinypy_internal_vm_valid(vm));
    assert(getter == NULL || tinypy_internal_value_belongs_to(vm, getter));
    assert(setter == NULL || tinypy_internal_value_belongs_to(vm, setter));
    assert(deleter == NULL || tinypy_internal_value_belongs_to(vm, deleter));
    assert(doc == NULL || tinypy_internal_value_belongs_to(vm, doc));
    tinypy_property_object_t *property = (tinypy_property_object_t *)tinypy_internal_value_allocate(vm, TINYPY_VALUE_PROPERTY, sizeof(*property));
    property->getter = getter;
    property->setter = setter;
    property->deleter = deleter;
    property->doc = doc;
    if (getter != NULL) {
        TINYPY_INCREF(getter);
    }
    if (setter != NULL) {
        TINYPY_INCREF(setter);
    }
    if (deleter != NULL) {
        TINYPY_INCREF(deleter);
    }
    if (doc != NULL) {
        TINYPY_INCREF(doc);
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
    tinypy_vm_t *vm = TINYPY_VALUE_VM(descriptor);

    (void)owner;
    TINYPY_CLEAR_ERROR(out_error);
    if (instance == NULL) {
        TINYPY_INCREF(descriptor);
        return descriptor;
    }
    if (property->getter == NULL) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_RUNTIME, "unreadable property", out_error);
        return NULL;
    }
    if (property->getter->type == &vm->function_type) {
        return tinypy_internal_eval_function_items(property->getter, &instance, 1U, NULL, out_error);
    }
    tinypy_value_t *args = tinypy_tuple_from_items(vm, &instance, 1U);
    tinypy_value_t *result = tinypy_call(property->getter, args, NULL, out_error);
    TINYPY_DECREF(args);
    return result;
}
//////////////////////////////////////////////////////////////////////////
int32_t tinypy_internal_property_set(tinypy_value_t *descriptor, tinypy_value_t *instance, tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_property_object_t *property = TINYPY_PROPERTY_OBJECT(descriptor);
    tinypy_vm_t *vm = TINYPY_VALUE_VM(descriptor);
    tinypy_value_t *callable = value != NULL ? property->setter : property->deleter;
    tinypy_value_t *items[2];
    size_t count = value != NULL ? 2U : 1U;

    TINYPY_CLEAR_ERROR(out_error);
    if (callable == NULL) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_RUNTIME, value != NULL ? "property has no setter" : "property has no deleter", out_error);
        return 0;
    }
    items[0] = instance;
    items[1] = value;
    if (callable->type == &vm->function_type) {
        tinypy_value_t *result = tinypy_internal_eval_function_items(callable, items, count, NULL, out_error);

        if (result == NULL) {
            return 0;
        }
        TINYPY_DECREF(result);
        return 1;
    }
    tinypy_value_t *args = tinypy_tuple_from_items(vm, items, count);
    tinypy_value_t *result = tinypy_call(callable, args, NULL, out_error);
    TINYPY_DECREF(args);
    if (result == NULL) {
        return 0;
    }
    TINYPY_DECREF(result);
    return 1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_descriptor_constructor(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error, int class_method) {
    tinypy_vm_t *vm = type->vm;

    if ((kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U) || TINYPY_TUPLE_SIZE(args) != 1U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "descriptor constructor requires one callable", out_error);
        return NULL;
    }
    if (TINYPY_TUPLE_GET(args, 0U)->type->call == NULL) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "descriptor argument is not callable", out_error);
        return NULL;
    }
    if (class_method != 0) {
        tinypy_value_t *item = TINYPY_TUPLE_GET(args, 0U);
        return tinypy_class_method_new(item);
    }
    tinypy_value_t *item = TINYPY_TUPLE_GET(args, 0U);
    return tinypy_static_method_new(item);
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
    size_t count = TINYPY_TUPLE_SIZE(args);

    if (kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "property keyword arguments are not implemented", out_error);
        return NULL;
    }
    if (count > 4U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "property accepts at most four arguments", out_error);
        return NULL;
    }
    for (size_t index = 0U; index < count; ++index) {
        tinypy_value_t *value = TINYPY_TUPLE_GET(args, index);

        values[index] = TINYPY_VALUE_KIND(value) == TINYPY_VALUE_NONE ? NULL : value;
    }
    if (values[3] == NULL && values[0] != NULL && TINYPY_VALUE_KIND(values[0]) == TINYPY_VALUE_FUNCTION) {
        values[3] = tinypy_function_doc(values[0]);
    }
    return tinypy_property_new(vm, values[0], values[1], values[2], values[3]);
}
//////////////////////////////////////////////////////////////////////////
static int __tinypy_internal_property_method_arguments(tinypy_vm_t *vm, tinypy_value_t *args, tinypy_value_t *kwargs, size_t count, tinypy_error_t **out_error) {
    if ((kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U) || TINYPY_TUPLE_SIZE(args) != count) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "property method received invalid arguments", out_error);
        return 0;
    }
    tinypy_value_t *item = TINYPY_TUPLE_GET(args, 0U);
    if (TINYPY_VALUE_KIND(item) != TINYPY_VALUE_PROPERTY) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "property method requires a property object", out_error);
        return 0;
    }
    return 1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_property_copy(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, int field, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    if (__tinypy_internal_property_method_arguments(vm, args, kwargs, 2U, out_error) == 0) {
        return NULL;
    }
    tinypy_property_object_t *property = TINYPY_PROPERTY_OBJECT(TINYPY_TUPLE_GET(args, 0U));
    tinypy_value_t *replacement = TINYPY_TUPLE_GET(args, 1U);
    if (TINYPY_VALUE_KIND(replacement) == TINYPY_VALUE_NONE) {
        replacement = NULL;
    }
    if (replacement != NULL && replacement->type->call == NULL) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "property accessor must be callable", out_error);
        return NULL;
    }
    tinypy_value_t *getter = field == 0 ? replacement : property->getter;
    tinypy_value_t *setter = field == 1 ? replacement : property->setter;
    tinypy_value_t *deleter = field == 2 ? replacement : property->deleter;
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
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    if (__tinypy_internal_property_method_arguments(vm, args, kwargs, 1U, out_error) == 0) {
        return NULL;
    }
    tinypy_property_object_t *property = TINYPY_PROPERTY_OBJECT(TINYPY_TUPLE_GET(args, 0U));
    tinypy_value_t *result;
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
    TINYPY_INCREF(result);
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
    TINYPY_DECREF(key);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_internal_property_method_set(tinypy_vm_t *vm, const char *name, size_t name_size, tinypy_native_function_callback_t callback) {
    tinypy_value_t *function = tinypy_native_function_new(vm, name, name_size, callback, NULL, NULL);

    __tinypy_internal_type_dict_set(vm, &vm->property_type, name, name_size, function);
    TINYPY_DECREF(function);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_internal_property_field_set(tinypy_vm_t *vm, const char *name, size_t name_size, tinypy_native_function_callback_t callback) {
    tinypy_value_t *function = tinypy_native_function_new(vm, name, name_size, callback, NULL, NULL);
    tinypy_value_t *descriptor = tinypy_property_new(vm, function, NULL, NULL, NULL);

    __tinypy_internal_type_dict_set(vm, &vm->property_type, name, name_size, descriptor);
    TINYPY_DECREF(descriptor);
    TINYPY_DECREF(function);
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_c_descriptor_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data) {
    assert(value != NULL);
    assert(TINYPY_VALUE_KIND(value) == TINYPY_VALUE_GETSET_DESCRIPTOR || TINYPY_VALUE_KIND(value) == TINYPY_VALUE_MEMBER_DESCRIPTOR);
    tinypy_c_descriptor_object_t *descriptor = TINYPY_C_DESCRIPTOR_OBJECT(value);
    if (descriptor->owner_retained != 0) {
        visit(&descriptor->owner->base.base, user_data);
    }
    visit(descriptor->name, user_data);
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_c_descriptor_get(tinypy_value_t *descriptor_value, tinypy_value_t *instance, tinypy_type_t *owner, tinypy_error_t **out_error) {
    assert(descriptor_value != NULL);
    assert(TINYPY_VALUE_KIND(descriptor_value) == TINYPY_VALUE_GETSET_DESCRIPTOR || TINYPY_VALUE_KIND(descriptor_value) == TINYPY_VALUE_MEMBER_DESCRIPTOR);
    tinypy_c_descriptor_object_t *descriptor = TINYPY_C_DESCRIPTOR_OBJECT(descriptor_value);
    tinypy_vm_t *vm = TINYPY_VALUE_VM(descriptor_value);
    (void)owner;
    TINYPY_CLEAR_ERROR(out_error);
    if (instance == NULL) {
        TINYPY_INCREF(descriptor_value);
        return descriptor_value;
    }
    if (tinypy_type_is_subtype(instance->type, descriptor->owner) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "descriptor does not apply to this object", out_error);
        return NULL;
    }
    if ((tinypy_internal_c_descriptor_field_e)descriptor->field == TINYPY_INTERNAL_C_DESCRIPTOR_INSTANCE_SLOT) {
        if (instance->type->slots_offset == 0U) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "slot descriptor requires an instance", out_error);
            return NULL;
        }
        tinypy_value_t *slot_value = *tinypy_internal_object_member_slot(instance, descriptor->index);
        if (slot_value == NULL) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_ATTRIBUTE, "slot attribute is not set", out_error);
            return NULL;
        }
        TINYPY_INCREF(slot_value);
        return slot_value;
    }
    if (TINYPY_VALUE_KIND(instance) != TINYPY_VALUE_FUNCTION) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "descriptor does not apply to this object", out_error);
        return NULL;
    }
    tinypy_function_object_t *function = TINYPY_FUNCTION_OBJECT(instance);
    switch ((tinypy_internal_c_descriptor_field_e)descriptor->field) {
    case TINYPY_INTERNAL_C_DESCRIPTOR_FUNCTION_CODE:
        TINYPY_INCREF(function->code);
        return function->code;
    case TINYPY_INTERNAL_C_DESCRIPTOR_FUNCTION_GLOBALS:
        TINYPY_INCREF(function->globals);
        return function->globals;
    case TINYPY_INTERNAL_C_DESCRIPTOR_FUNCTION_DEFAULTS:
        return __tinypy_internal_c_descriptor_optional(vm, function->defaults);
    case TINYPY_INTERNAL_C_DESCRIPTOR_FUNCTION_CLOSURE:
        return __tinypy_internal_c_descriptor_optional(vm, function->closure);
    case TINYPY_INTERNAL_C_DESCRIPTOR_FUNCTION_NAME:
        TINYPY_INCREF(function->name);
        return function->name;
    case TINYPY_INTERNAL_C_DESCRIPTOR_FUNCTION_DOC:
        return __tinypy_internal_c_descriptor_optional(vm, function->doc);
    case TINYPY_INTERNAL_C_DESCRIPTOR_FUNCTION_DICT:
        if (function->dict == NULL) {
            function->dict = tinypy_dict_new(vm);
        }
        TINYPY_INCREF(function->dict);
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
    assert(descriptor_value != NULL);
    assert(TINYPY_VALUE_KIND(descriptor_value) == TINYPY_VALUE_GETSET_DESCRIPTOR || TINYPY_VALUE_KIND(descriptor_value) == TINYPY_VALUE_MEMBER_DESCRIPTOR);
    tinypy_c_descriptor_object_t *descriptor = TINYPY_C_DESCRIPTOR_OBJECT(descriptor_value);
    tinypy_vm_t *vm = TINYPY_VALUE_VM(descriptor_value);
    tinypy_internal_c_descriptor_field_e field = (tinypy_internal_c_descriptor_field_e)descriptor->field;
    TINYPY_CLEAR_ERROR(out_error);
    if (tinypy_type_is_subtype(instance->type, descriptor->owner) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "descriptor does not apply to this object", out_error);
        return INT32_C(0);
    }
    if (descriptor->writable == 0) {
        __tinypy_internal_c_descriptor_readonly(vm, out_error);
        return INT32_C(0);
    }
    if (field == TINYPY_INTERNAL_C_DESCRIPTOR_INSTANCE_SLOT) {
        if (instance->type->slots_offset == 0U) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "slot descriptor requires an instance", out_error);
            return INT32_C(0);
        }
        tinypy_value_t **slot = tinypy_internal_object_member_slot(instance, descriptor->index);
        tinypy_value_t *previous = *slot;
        if (value != NULL) {
            TINYPY_INCREF(value);
        }
        *slot = value;
        if (previous != NULL) {
            TINYPY_DECREF(previous);
        }
        return INT32_C(1);
    }
    if (TINYPY_VALUE_KIND(instance) != TINYPY_VALUE_FUNCTION) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "descriptor does not apply to this object", out_error);
        return INT32_C(0);
    }
    tinypy_function_object_t *function = TINYPY_FUNCTION_OBJECT(instance);
    switch (field) {
    case TINYPY_INTERNAL_C_DESCRIPTOR_FUNCTION_CODE:
        if (value == NULL) {
            __tinypy_internal_c_descriptor_readonly(vm, out_error);
            return INT32_C(0);
        }
        if (TINYPY_VALUE_KIND(value) != TINYPY_VALUE_CODE) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "func_code must be a code object", out_error);
            return INT32_C(0);
        }
        int condition = function->closure != NULL;
        if (condition != 0) {
            tinypy_value_t *freevars = TINYPY_CODE_FREEVARS(value);
            condition = TINYPY_TUPLE_SIZE(function->closure) != TINYPY_TUPLE_SIZE(freevars);
        }
        if (condition) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "func_code has incompatible free variables", out_error);
            return INT32_C(0);
        }
        __tinypy_internal_c_descriptor_replace(&function->code, value);
        return INT32_C(1);
    case TINYPY_INTERNAL_C_DESCRIPTOR_FUNCTION_DEFAULTS:
        if (value != NULL && TINYPY_VALUE_KIND(value) == TINYPY_VALUE_NONE) {
            value = NULL;
        }
        if (value != NULL && TINYPY_VALUE_KIND(value) != TINYPY_VALUE_TUPLE) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "func_defaults must be a tuple", out_error);
            return INT32_C(0);
        }
        __tinypy_internal_c_descriptor_replace(&function->defaults, value);
        return INT32_C(1);
    case TINYPY_INTERNAL_C_DESCRIPTOR_FUNCTION_NAME:
        if (value == NULL || TINYPY_VALUE_KIND(value) != TINYPY_VALUE_STRING) {
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
            TINYPY_INCREF(value);
        }
        __tinypy_internal_c_descriptor_replace(&function->doc, value);
        TINYPY_DECREF(value);
        return INT32_C(1);
    case TINYPY_INTERNAL_C_DESCRIPTOR_FUNCTION_DICT:
        if (value == NULL || TINYPY_VALUE_KIND(value) != TINYPY_VALUE_DICT) {
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
            TINYPY_INCREF(value);
        }
        __tinypy_internal_c_descriptor_replace(&function->module, value);
        TINYPY_DECREF(value);
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
    TINYPY_DECREF(descriptor);
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
    assert(property != NULL);
    assert(tinypy_internal_vm_valid(TINYPY_VALUE_VM(property)));
    assert(TINYPY_VALUE_KIND(property) == TINYPY_VALUE_PROPERTY);
    return TINYPY_PROPERTY_OBJECT((tinypy_value_t *)property)->getter;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_property_setter(const tinypy_value_t *property) {
    assert(property != NULL);
    assert(tinypy_internal_vm_valid(TINYPY_VALUE_VM(property)));
    assert(TINYPY_VALUE_KIND(property) == TINYPY_VALUE_PROPERTY);
    return TINYPY_PROPERTY_OBJECT((tinypy_value_t *)property)->setter;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_property_deleter(const tinypy_value_t *property) {
    assert(property != NULL);
    assert(tinypy_internal_vm_valid(TINYPY_VALUE_VM(property)));
    assert(TINYPY_VALUE_KIND(property) == TINYPY_VALUE_PROPERTY);
    return TINYPY_PROPERTY_OBJECT((tinypy_value_t *)property)->deleter;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_property_doc(const tinypy_value_t *property) {
    assert(property != NULL);
    assert(tinypy_internal_vm_valid(TINYPY_VALUE_VM(property)));
    assert(TINYPY_VALUE_KIND(property) == TINYPY_VALUE_PROPERTY);
    return TINYPY_PROPERTY_OBJECT((tinypy_value_t *)property)->doc;
}
