#include "tinypy/descriptor.h"

#include "internal.h"

#include <string.h>

typedef enum tinypy_internal_c_descriptor_field_e {
    TINYPY_INTERNAL_C_DESCRIPTOR_FUNCTION_CODE = 1,
    TINYPY_INTERNAL_C_DESCRIPTOR_FUNCTION_GLOBALS = 2,
    TINYPY_INTERNAL_C_DESCRIPTOR_FUNCTION_DEFAULTS = 3,
    TINYPY_INTERNAL_C_DESCRIPTOR_FUNCTION_CLOSURE = 4,
    TINYPY_INTERNAL_C_DESCRIPTOR_FUNCTION_NAME = 5,
    TINYPY_INTERNAL_C_DESCRIPTOR_FUNCTION_DOC = 6,
    TINYPY_INTERNAL_C_DESCRIPTOR_FUNCTION_DICT = 7,
    TINYPY_INTERNAL_C_DESCRIPTOR_FUNCTION_MODULE = 8,
    TINYPY_INTERNAL_C_DESCRIPTOR_INSTANCE_SLOT = 9,
    TINYPY_INTERNAL_C_DESCRIPTOR_METHOD_FUNCTION = 10,
    TINYPY_INTERNAL_C_DESCRIPTOR_METHOD_SELF = 11,
    TINYPY_INTERNAL_C_DESCRIPTOR_METHOD_OWNER = 12,
    TINYPY_INTERNAL_C_DESCRIPTOR_CODE_ARG_COUNT = 13,
    TINYPY_INTERNAL_C_DESCRIPTOR_CODE_LOCAL_COUNT = 14,
    TINYPY_INTERNAL_C_DESCRIPTOR_CODE_STACK_SIZE = 15,
    TINYPY_INTERNAL_C_DESCRIPTOR_CODE_FLAGS = 16,
    TINYPY_INTERNAL_C_DESCRIPTOR_CODE_BYTECODE = 17,
    TINYPY_INTERNAL_C_DESCRIPTOR_CODE_CONSTS = 18,
    TINYPY_INTERNAL_C_DESCRIPTOR_CODE_NAMES = 19,
    TINYPY_INTERNAL_C_DESCRIPTOR_CODE_VARNAMES = 20,
    TINYPY_INTERNAL_C_DESCRIPTOR_CODE_FREEVARS = 21,
    TINYPY_INTERNAL_C_DESCRIPTOR_CODE_CELLVARS = 22,
    TINYPY_INTERNAL_C_DESCRIPTOR_CODE_FILENAME = 23,
    TINYPY_INTERNAL_C_DESCRIPTOR_CODE_NAME = 24,
    TINYPY_INTERNAL_C_DESCRIPTOR_CODE_FIRST_LINE = 25,
    TINYPY_INTERNAL_C_DESCRIPTOR_CODE_LNOTAB = 26,
    TINYPY_INTERNAL_C_DESCRIPTOR_FRAME_BACK = 27,
    TINYPY_INTERNAL_C_DESCRIPTOR_FRAME_CODE = 28,
    TINYPY_INTERNAL_C_DESCRIPTOR_FRAME_BUILTINS = 29,
    TINYPY_INTERNAL_C_DESCRIPTOR_FRAME_GLOBALS = 30,
    TINYPY_INTERNAL_C_DESCRIPTOR_FRAME_LOCALS = 31,
    TINYPY_INTERNAL_C_DESCRIPTOR_FRAME_LAST_INSTRUCTION = 32,
    TINYPY_INTERNAL_C_DESCRIPTOR_FRAME_LINE_NUMBER = 33,
    TINYPY_INTERNAL_C_DESCRIPTOR_FRAME_TRACE = 34,
    TINYPY_INTERNAL_C_DESCRIPTOR_TRACEBACK_NEXT = 35,
    TINYPY_INTERNAL_C_DESCRIPTOR_TRACEBACK_FRAME = 36,
    TINYPY_INTERNAL_C_DESCRIPTOR_TRACEBACK_LAST_INSTRUCTION = 37,
    TINYPY_INTERNAL_C_DESCRIPTOR_TRACEBACK_LINE_NUMBER = 38,
    TINYPY_INTERNAL_C_DESCRIPTOR_GENERATOR_NAME = 39,
    TINYPY_INTERNAL_C_DESCRIPTOR_GENERATOR_CODE = 40,
    TINYPY_INTERNAL_C_DESCRIPTOR_GENERATOR_FRAME = 41,
    TINYPY_INTERNAL_C_DESCRIPTOR_GENERATOR_RUNNING = 42,
    TINYPY_INTERNAL_C_DESCRIPTOR_INSTANCE_DICT = 43,
    TINYPY_INTERNAL_C_DESCRIPTOR_INSTANCE_WEAKREF = 44,
    TINYPY_INTERNAL_C_DESCRIPTOR_EXCEPTION_ARGS = 45,
    TINYPY_INTERNAL_C_DESCRIPTOR_EXCEPTION_MESSAGE = 46,
    TINYPY_INTERNAL_C_DESCRIPTOR_CALLABLE_FUNCTION = 47,
    TINYPY_INTERNAL_C_DESCRIPTOR_CELL_CONTENT = 48,
    TINYPY_INTERNAL_C_DESCRIPTOR_MODULE_DICT = 49,
    TINYPY_INTERNAL_C_DESCRIPTOR_SUPER_THISCLASS = 50,
    TINYPY_INTERNAL_C_DESCRIPTOR_SUPER_SELF = 51,
    TINYPY_INTERNAL_C_DESCRIPTOR_SUPER_SELF_CLASS = 52,
    TINYPY_INTERNAL_C_DESCRIPTOR_PARTIAL_FUNCTION = 53,
    TINYPY_INTERNAL_C_DESCRIPTOR_PARTIAL_ARGS = 54,
    TINYPY_INTERNAL_C_DESCRIPTOR_PARTIAL_KEYWORDS = 55,
    TINYPY_INTERNAL_C_DESCRIPTOR_FRAME_EXCEPTION_TYPE = 56,
    TINYPY_INTERNAL_C_DESCRIPTOR_FRAME_EXCEPTION_VALUE = 57,
    TINYPY_INTERNAL_C_DESCRIPTOR_FRAME_EXCEPTION_TRACEBACK = 58,
    TINYPY_INTERNAL_C_DESCRIPTOR_FRAME_RESTRICTED = 59,
    TINYPY_INTERNAL_C_DESCRIPTOR_TYPE_NAME = 60,
    TINYPY_INTERNAL_C_DESCRIPTOR_TYPE_BASES = 61,
    TINYPY_INTERNAL_C_DESCRIPTOR_TYPE_MRO = 62,
    TINYPY_INTERNAL_C_DESCRIPTOR_TYPE_BASE = 63,
    TINYPY_INTERNAL_C_DESCRIPTOR_TYPE_FLAGS = 64,
    TINYPY_INTERNAL_C_DESCRIPTOR_TYPE_BASIC_SIZE = 65,
    TINYPY_INTERNAL_C_DESCRIPTOR_TYPE_ITEM_SIZE = 66,
    TINYPY_INTERNAL_C_DESCRIPTOR_TYPE_DICT_OFFSET = 67,
    TINYPY_INTERNAL_C_DESCRIPTOR_TYPE_WEAKREF_OFFSET = 68,
    TINYPY_INTERNAL_C_DESCRIPTOR_TYPE_DICT = 69,
    TINYPY_INTERNAL_C_DESCRIPTOR_TYPE_MODULE = 70,
    TINYPY_INTERNAL_C_DESCRIPTOR_TYPE_ABSTRACT_METHODS = 71,
    TINYPY_INTERNAL_C_DESCRIPTOR_TYPE_DOC = 72,
    TINYPY_INTERNAL_C_DESCRIPTOR_UNICODE_ENCODING = 73,
    TINYPY_INTERNAL_C_DESCRIPTOR_UNICODE_OBJECT = 74,
    TINYPY_INTERNAL_C_DESCRIPTOR_UNICODE_REASON = 75,
    TINYPY_INTERNAL_C_DESCRIPTOR_UNICODE_START = 76,
    TINYPY_INTERNAL_C_DESCRIPTOR_UNICODE_END = 77,
    TINYPY_INTERNAL_C_DESCRIPTOR_NATIVE_NAME = 78,
    TINYPY_INTERNAL_C_DESCRIPTOR_NATIVE_SELF = 79,
    TINYPY_INTERNAL_C_DESCRIPTOR_NATIVE_MODULE = 80,
    TINYPY_INTERNAL_C_DESCRIPTOR_NATIVE_OWNER = 81,
    TINYPY_INTERNAL_C_DESCRIPTOR_NATIVE_DOC = 82,
    TINYPY_INTERNAL_C_DESCRIPTOR_STRUCT_FORMAT = 83,
    TINYPY_INTERNAL_C_DESCRIPTOR_STRUCT_SIZE = 84,
    TINYPY_INTERNAL_C_DESCRIPTOR_METADATA_NAME = 85,
    TINYPY_INTERNAL_C_DESCRIPTOR_METADATA_OWNER = 86,
    TINYPY_INTERNAL_C_DESCRIPTOR_METADATA_DOC = 87,
    TINYPY_INTERNAL_C_DESCRIPTOR_NUMERIC_REAL = 88,
    TINYPY_INTERNAL_C_DESCRIPTOR_NUMERIC_IMAG = 89,
    TINYPY_INTERNAL_C_DESCRIPTOR_NUMERIC_NUMERATOR = 90,
    TINYPY_INTERNAL_C_DESCRIPTOR_NUMERIC_DENOMINATOR = 91,
    TINYPY_INTERNAL_C_DESCRIPTOR_PROPERTY_GETTER = 92,
    TINYPY_INTERNAL_C_DESCRIPTOR_PROPERTY_SETTER = 93,
    TINYPY_INTERNAL_C_DESCRIPTOR_PROPERTY_DELETER = 94,
    TINYPY_INTERNAL_C_DESCRIPTOR_PROPERTY_DOC = 95,
    TINYPY_INTERNAL_C_DESCRIPTOR_METHOD_DOC = 96
} tinypy_internal_c_descriptor_field_e;

//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_c_descriptor_new_with_owner(tinypy_vm_t *vm, tinypy_value_type_e kind, tinypy_type_t *owner, tinypy_value_t *name, tinypy_internal_c_descriptor_field_e field, tinypy_bool_t writable, tinypy_bool_t retain_owner) {
    tinypy_c_descriptor_object_t *descriptor = (tinypy_c_descriptor_object_t *)tinypy_internal_value_allocate(vm, kind, sizeof(*descriptor));
    descriptor->owner = owner;
    descriptor->name = TINYPY_RET(name);
    descriptor->field = (int32_t)field;
    descriptor->writable = writable;
    descriptor->owner_retained = retain_owner;
    if (retain_owner != 0) {
        TINYPY_INCREF(&owner->base.base);
    }
    else {
        descriptor->owner_reference = tinypy_weakref_new(&owner->base.base, NULL, NULL);
    }
    return &descriptor->base;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_c_descriptor_new(tinypy_vm_t *vm, tinypy_value_type_e kind, tinypy_type_t *owner, tinypy_value_t *name, tinypy_internal_c_descriptor_field_e field, tinypy_bool_t writable) {
    tinypy_value_t *return_value_1 = __tinypy_internal_c_descriptor_new_with_owner(vm, kind, owner, name, field, writable, TINYPY_TRUE);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_member_descriptor_new(tinypy_type_t *owner, tinypy_value_t *name, size_t index) {
    tinypy_vm_t *vm = owner->vm;
    tinypy_c_descriptor_object_t *descriptor = (tinypy_c_descriptor_object_t *)tinypy_internal_value_allocate(vm, TINYPY_VALUE_MEMBER_DESCRIPTOR, sizeof(*descriptor));
    descriptor->owner = owner;
    descriptor->name = name;
    descriptor->index = index;
    descriptor->field = (int32_t)TINYPY_INTERNAL_C_DESCRIPTOR_INSTANCE_SLOT;
    descriptor->writable = INT32_C(1);
    descriptor->owner_retained = INT32_C(0);
    descriptor->owner_reference = tinypy_weakref_new(&owner->base.base, NULL, NULL);
    TINYPY_INCREF(name);
    return &descriptor->base;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_instance_dict_descriptor_new(tinypy_type_t *owner) {
    tinypy_value_t *return_value_1 = __tinypy_internal_c_descriptor_new_with_owner(owner->vm, TINYPY_VALUE_GETSET_DESCRIPTOR, owner, owner->vm->internal_special_dict_key, TINYPY_INTERNAL_C_DESCRIPTOR_INSTANCE_DICT, INT32_C(1), TINYPY_FALSE);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_instance_weakref_descriptor_new(tinypy_type_t *owner) {
    tinypy_value_t *return_value_1 = __tinypy_internal_c_descriptor_new_with_owner(owner->vm, TINYPY_VALUE_GETSET_DESCRIPTOR, owner, owner->vm->internal_special_weakref_key, TINYPY_INTERNAL_C_DESCRIPTOR_INSTANCE_WEAKREF, INT32_C(0), TINYPY_FALSE);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_c_descriptor_optional(tinypy_vm_t *vm, tinypy_value_t *value) {
    if (value == NULL) {
        tinypy_value_t *return_value_1 = TINYPY_RET_NONE(vm);
        return return_value_1;
    }
    return TINYPY_RET(value);
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
static void __tinypy_internal_c_descriptor_receiver_error(tinypy_c_descriptor_object_t *descriptor, tinypy_value_t *instance, tinypy_bool_t setting, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(&descriptor->base);

    if (descriptor->owner != NULL) {
        size_t name_size = TINYPY_TEXT_BYTE_SIZE(descriptor->name);
        size_t owner_size = descriptor->owner->name_size;
        size_t instance_size = instance->type->name_size;

        if (setting != TINYPY_FALSE) {
            name_size = name_size < 200U ? name_size : 200U;
            owner_size = owner_size < 100U ? owner_size : 100U;
            instance_size = instance_size < 100U ? instance_size : 100U;
        }
        const char *name_bytes = (const char *)TINYPY_TEXT_BYTES(descriptor->name);
        const char *name_end = (const char *)memchr(name_bytes, 0, name_size);
        const char *owner_end = (const char *)memchr(descriptor->owner->name, 0, owner_size);
        const char *instance_end = (const char *)memchr(instance->type->name, 0, instance_size);

        if (name_end != NULL) {
            name_size = (size_t)(name_end - name_bytes);
        }
        if (owner_end != NULL) {
            owner_size = (size_t)(owner_end - descriptor->owner->name);
        }
        if (instance_end != NULL) {
            instance_size = (size_t)(instance_end - instance->type->name);
        }
        tinypy_message_part_t parts[] = {
            TINYPY_MESSAGE_PART_LITERAL("descriptor '"), {name_bytes, name_size},
            TINYPY_MESSAGE_PART_LITERAL("' for '"), {descriptor->owner->name, owner_size},
            TINYPY_MESSAGE_PART_LITERAL("' objects doesn't apply to '"), {instance->type->name, instance_size},
            TINYPY_MESSAGE_PART_LITERAL("' object")
        };

        tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, 7U, out_error);
        return;
    }
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "descriptor does not apply to this object", out_error);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_callable_descriptor_new(tinypy_value_t *callable, tinypy_value_type_e kind) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(callable);
    tinypy_callable_descriptor_object_t *descriptor = (tinypy_callable_descriptor_object_t *)tinypy_internal_value_allocate(vm, kind, sizeof(*descriptor));
    descriptor->callable = callable;
    TINYPY_INCREF(callable);
    return &descriptor->base;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_static_method_new(tinypy_value_t *callable) {
    tinypy_value_t *return_value_1 = __tinypy_internal_callable_descriptor_new(callable, TINYPY_VALUE_STATIC_METHOD);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_class_method_new(tinypy_value_t *callable) {
    tinypy_value_t *return_value_1 = __tinypy_internal_callable_descriptor_new(callable, TINYPY_VALUE_CLASS_METHOD);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_callable_descriptor_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data) {
    tinypy_value_t *callable = TINYPY_CALLABLE_DESCRIPTOR_OBJECT(value)->callable;

    if (callable != NULL) {
        visit(callable, user_data);
    }
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_static_method_get(tinypy_value_t *descriptor, tinypy_value_t *instance, tinypy_type_t *owner, tinypy_error_t **out_error) {
    tinypy_value_t *callable = TINYPY_CALLABLE_DESCRIPTOR_OBJECT(descriptor)->callable;

    (void)instance;
    (void)owner;
    TINYPY_CLEAR_ERROR(out_error);
    if (callable == NULL) {
        tinypy_internal_make_vm_error(TINYPY_VALUE_VM(descriptor), TINYPY_ERROR_RUNTIME, "uninitialized staticmethod object", out_error);
        return NULL;
    }
    return TINYPY_RET(callable);
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_class_method_get(tinypy_value_t *descriptor, tinypy_value_t *instance, tinypy_type_t *owner, tinypy_error_t **out_error) {
    (void)instance;
    tinypy_value_t *result = tinypy_internal_class_method_bind(descriptor, &owner->base.base, out_error);
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* A bound classmethod carries the class as self and the metaclass as its
   owner, so im_class matches Python 2.7. */
tinypy_value_t *tinypy_internal_class_method_bind(tinypy_value_t *descriptor, tinypy_value_t *owner, tinypy_error_t **out_error) {
    TINYPY_CLEAR_ERROR(out_error);
    tinypy_value_t *callable = TINYPY_CALLABLE_DESCRIPTOR_OBJECT(descriptor)->callable;
    if (callable == NULL) {
        tinypy_internal_make_vm_error(TINYPY_VALUE_VM(descriptor), TINYPY_ERROR_RUNTIME, "uninitialized classmethod object", out_error);
        return NULL;
    }
    tinypy_value_t *result = tinypy_method_new(callable, owner, &owner->type->base.base);
    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_static_method_callable(const tinypy_value_t *descriptor) {
    tinypy_value_t *return_value_1 = TINYPY_CALLABLE_DESCRIPTOR_OBJECT((tinypy_value_t *)descriptor)->callable;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_class_method_callable(const tinypy_value_t *descriptor) {
    tinypy_value_t *return_value_1 = TINYPY_CALLABLE_DESCRIPTOR_OBJECT((tinypy_value_t *)descriptor)->callable;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_property_new(tinypy_vm_t *vm, tinypy_value_t *getter, tinypy_value_t *setter, tinypy_value_t *deleter, tinypy_value_t *doc, tinypy_bool_t getter_doc) {
    tinypy_property_object_t *property = (tinypy_property_object_t *)tinypy_internal_value_allocate(vm, TINYPY_VALUE_PROPERTY, sizeof(*property));
    property->getter = getter;
    property->setter = setter;
    property->deleter = deleter;
    property->doc = doc;
    property->getter_doc = getter_doc;
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
tinypy_value_t *tinypy_property_new(tinypy_vm_t *vm, tinypy_value_t *getter, tinypy_value_t *setter, tinypy_value_t *deleter, tinypy_value_t *doc) {
    tinypy_value_t *return_value = __tinypy_property_new(vm, getter, setter, deleter, doc, TINYPY_FALSE);
    return return_value;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_property_getter_doc(tinypy_vm_t *vm, tinypy_value_t *getter, tinypy_value_t **out_doc, tinypy_error_t **out_error) {
    tinypy_value_t *key;
    int32_t found;

    *out_doc = NULL;
    if (getter == NULL) {
        return TINYPY_TRUE;
    }
    key = TINYPY_RET(vm->internal_special_doc_key);
    found = tinypy_internal_object_get_optional_attr_key(getter, key, out_doc, out_error);
    TINYPY_DECREF(key);
    if (found < 0 && tinypy_internal_exception_consume_kind(vm, TINYPY_EXCEPTION_EXCEPTION, out_error) == 0) {
        return TINYPY_FALSE;
    }
    return TINYPY_TRUE;
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
        return TINYPY_RET(descriptor);
    }
    if (property->getter == NULL) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_ATTRIBUTE, "unreadable attribute", out_error);
        return NULL;
    }
    if (property->getter->type == &vm->types[TINYPY_VALUE_FUNCTION]) {
        tinypy_value_t *return_value_1 = tinypy_internal_eval_function_items(property->getter, &instance, 1U, NULL, out_error);
        return return_value_1;
    }
    tinypy_value_t *args = tinypy_tuple_from_items(vm, &instance, 1U);
    tinypy_value_t *result = tinypy_call(property->getter, args, NULL, out_error);
    TINYPY_DECREF(args);
    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_property_set(tinypy_value_t *descriptor, tinypy_value_t *instance, tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_property_object_t *property = TINYPY_PROPERTY_OBJECT(descriptor);
    tinypy_vm_t *vm = TINYPY_VALUE_VM(descriptor);
    tinypy_value_t *callable = value != NULL ? property->setter : property->deleter;
    tinypy_value_t *items[2];
    size_t count = value != NULL ? 2U : 1U;

    TINYPY_CLEAR_ERROR(out_error);
    if (callable == NULL) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_ATTRIBUTE, value != NULL ? "can't set attribute" : "can't delete attribute", out_error);
        return TINYPY_FALSE;
    }
    items[0] = instance;
    items[1] = value;
    if (callable->type == &vm->types[TINYPY_VALUE_FUNCTION]) {
        tinypy_value_t *result = tinypy_internal_eval_function_items(callable, items, count, NULL, out_error);

        if (result == NULL) {
            return TINYPY_FALSE;
        }
        TINYPY_DECREF(result);
        return TINYPY_TRUE;
    }
    tinypy_value_t *args = tinypy_tuple_from_items(vm, items, count);
    tinypy_value_t *result = tinypy_call(callable, args, NULL, out_error);
    TINYPY_DECREF(args);
    if (result == NULL) {
        return TINYPY_FALSE;
    }
    TINYPY_DECREF(result);
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_descriptor_constructor(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error, int32_t class_method) {
    tinypy_vm_t *vm = type->vm;
    const char *name = class_method != 0 ? "classmethod" : "staticmethod";
    size_t name_size = class_method != 0 ? 11U : 12U;
    size_t count = TINYPY_TUPLE_SIZE(args);

    if (count != 1U) {
        tinypy_internal_make_arity_error(vm, name, name_size, count, 1U, 1U, TINYPY_ARITY_STYLE_UNPACK, out_error);
        return NULL;
    }
    if (kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U) {
        tinypy_message_part_t parts[] = {
            {name, name_size},
            TINYPY_MESSAGE_PART_LITERAL(" does not take keyword arguments"),
        };

        tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
        return NULL;
    }
    if (class_method != 0) {
        tinypy_value_t *item = TINYPY_TUPLE_GET(args, 0U);
        tinypy_value_t *return_value_1 = tinypy_class_method_new(item);
        return return_value_1;
    }
    tinypy_value_t *item = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *return_value_2 = tinypy_static_method_new(item);
    return return_value_2;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_static_method_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_value_t *return_value_1 = __tinypy_internal_descriptor_constructor(type, args, kwargs, out_error, 0);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_class_method_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_value_t *return_value_1 = __tinypy_internal_descriptor_constructor(type, args, kwargs, out_error, 1);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_internal_property_initialize(tinypy_value_t *self, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(self);
    tinypy_value_t *const names[4] = {vm->internal_fget_key, vm->internal_fset_key, vm->internal_fdel_key, vm->internal_doc_key};
    tinypy_value_t *values[4] = {NULL, NULL, NULL, NULL};
    tinypy_value_t *owned_doc = NULL;
    tinypy_bool_t success = TINYPY_FALSE;
    size_t count = TINYPY_TUPLE_SIZE(args);

    if (count > 4U || (kwargs != NULL && TINYPY_DICT_SIZE(kwargs) > 4U - count)) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "property accepts at most four arguments", out_error);
        return TINYPY_FALSE;
    }
    for (size_t index = 0U; index < 4U; ++index) {
        tinypy_value_t *keyword = NULL;

        if (kwargs != NULL) {
            keyword = tinypy_internal_constructor_keyword_optional(kwargs, names[index]);
        }
        if (index < count && keyword != NULL) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "property received multiple values for an argument", out_error);
            goto cleanup;
        }
        tinypy_value_t *value = index < count ? TINYPY_TUPLE_GET(args, index) : keyword;

        if (value != NULL && TINYPY_VALUE_KIND(value) != TINYPY_VALUE_NONE) {
            values[index] = value;
            TINYPY_INCREF(value);
        }
    }
    if (kwargs != NULL) {
        tinypy_dict_entry_t *iterator = TINYPY_DICT_ITERATOR_BEGIN(kwargs);
        tinypy_dict_entry_t *iterator_end = TINYPY_DICT_ITERATOR_END(kwargs);

        for (; iterator != iterator_end; ++iterator) {
            tinypy_bool_t recognized = TINYPY_FALSE;

            if (!TINYPY_DICT_ENTRY_IS_ACTIVE(iterator)) {
                continue;
            }
            if (TINYPY_VALUE_KIND(iterator->key) != TINYPY_VALUE_STRING && TINYPY_VALUE_KIND(iterator->key) != TINYPY_VALUE_UNICODE) {
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "property received an unexpected keyword", out_error);
                goto cleanup;
            }
            for (size_t index = 0U; index < 4U; ++index) {
                if (TINYPY_NAME_EQ(iterator->key, names[index]) != 0) {
                    recognized = TINYPY_TRUE;
                    break;
                }
            }
            if (recognized == 0) {
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "property received an unexpected keyword", out_error);
                goto cleanup;
            }
        }
    }
    tinypy_property_object_t *property = TINYPY_PROPERTY_OBJECT(self);
    tinypy_value_t *previous[4] = {property->getter, property->setter, property->deleter, property->doc};

    for (size_t index = 0U; index < 4U; ++index) {
        if (values[index] != NULL) {
            TINYPY_INCREF(values[index]);
        }
    }
    property->getter = values[0];
    property->setter = values[1];
    property->deleter = values[2];
    property->doc = values[3];
    property->getter_doc = TINYPY_FALSE;
    for (size_t index = 0U; index < 4U; ++index) {
        if (previous[index] != NULL) {
            TINYPY_DECREF(previous[index]);
        }
    }
    if (values[3] == NULL && values[0] != NULL) {
        if (__tinypy_property_getter_doc(vm, values[0], &owned_doc, out_error) == 0) {
            goto cleanup;
        }
        if (owned_doc != NULL) {
            if (self->type == &vm->types[TINYPY_VALUE_PROPERTY]) {
                tinypy_value_t *previous_doc = property->doc;

                property->doc = owned_doc;
                if (previous_doc != NULL) {
                    TINYPY_DECREF(previous_doc);
                }
            }
            else {
                tinypy_value_t *key = vm->internal_special_doc_key;
                tinypy_bool_t stored = tinypy_internal_object_set_attr_protocol_key(self, key, owned_doc, out_error);

                TINYPY_DECREF(owned_doc);
                if (stored == 0) {
                    goto cleanup;
                }
            }
            property->getter_doc = TINYPY_TRUE;
        }
    }
    success = TINYPY_TRUE;
cleanup:
    for (size_t index = 0U; index < 4U; ++index) {
        if (values[index] != NULL) {
            TINYPY_DECREF(values[index]);
        }
    }
    return success;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_property_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_value_t *result = tinypy_internal_object_allocate_checked(type->vm, type, type->basic_size, out_error);

    if (result == NULL) {
        return NULL;
    }
    if (__tinypy_internal_property_initialize(result, args, kwargs, out_error) == 0) {
        TINYPY_DECREF(result);
        return NULL;
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_descriptor_tail_args(tinypy_vm_t *vm, tinypy_value_t *args) {
    size_t count = TINYPY_TUPLE_SIZE(args);
    tinypy_value_t *const *items = tinypy_internal_tuple_items(args);
    tinypy_value_t *result = tinypy_tuple_from_items(vm, count > 1U ? items + 1U : NULL, count > 0U ? count - 1U : 0U);

    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_callable_descriptor_init_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (TINYPY_TUPLE_SIZE(args) == 0U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "descriptor __init__ requires an instance", out_error);
        return NULL;
    }
    tinypy_value_t *self = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(self);
    if (kind != TINYPY_VALUE_STATIC_METHOD && kind != TINYPY_VALUE_CLASS_METHOD) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "descriptor __init__ received an incompatible object", out_error);
        return NULL;
    }
    tinypy_value_t *constructor_args = __tinypy_internal_descriptor_tail_args(vm, args);
    tinypy_type_t *base_type = &vm->types[kind];
    tinypy_value_t *initialized = base_type->create(base_type, constructor_args, kwargs, out_error);

    TINYPY_DECREF(constructor_args);
    if (initialized == NULL) {
        return NULL;
    }
    tinypy_value_t *callable = TINYPY_CALLABLE_DESCRIPTOR_OBJECT(self)->callable;
    TINYPY_CALLABLE_DESCRIPTOR_OBJECT(self)->callable = TINYPY_CALLABLE_DESCRIPTOR_OBJECT(initialized)->callable;
    TINYPY_CALLABLE_DESCRIPTOR_OBJECT(initialized)->callable = callable;
    TINYPY_DECREF(initialized);
    tinypy_value_t *result = TINYPY_RET_NONE(vm);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_property_init_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (TINYPY_TUPLE_SIZE(args) == 0U || TINYPY_VALUE_KIND(TINYPY_TUPLE_GET(args, 0U)) != TINYPY_VALUE_PROPERTY) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "property.__init__ received an incompatible object", out_error);
        return NULL;
    }
    tinypy_value_t *self = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *constructor_args = __tinypy_internal_descriptor_tail_args(vm, args);
    tinypy_bool_t initialized = __tinypy_internal_property_initialize(self, constructor_args, kwargs, out_error);

    TINYPY_DECREF(constructor_args);
    if (initialized == 0) {
        return NULL;
    }
    tinypy_value_t *result = TINYPY_RET_NONE(vm);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_internal_property_method_arguments(tinypy_vm_t *vm, tinypy_value_t *args, tinypy_value_t *kwargs, size_t count, tinypy_error_t **out_error) {
    if ((kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U) || TINYPY_TUPLE_SIZE(args) != count) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "property method received invalid arguments", out_error);
        return TINYPY_FALSE;
    }
    tinypy_value_t *item = TINYPY_TUPLE_GET(args, 0U);
    if (TINYPY_VALUE_KIND(item) != TINYPY_VALUE_PROPERTY) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "property method requires a property object", out_error);
        return TINYPY_FALSE;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_property_copy(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, int32_t field, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    if (__tinypy_internal_property_method_arguments(vm, args, kwargs, 2U, out_error) == 0) {
        return NULL;
    }
    tinypy_property_object_t *property = TINYPY_PROPERTY_OBJECT(TINYPY_TUPLE_GET(args, 0U));
    tinypy_value_t *replacement = TINYPY_TUPLE_GET(args, 1U);
    if (TINYPY_VALUE_KIND(replacement) == TINYPY_VALUE_NONE) {
        replacement = NULL;
    }
    tinypy_value_t *getter = field == 0 && replacement != NULL ? replacement : property->getter;
    tinypy_value_t *setter = field == 1 && replacement != NULL ? replacement : property->setter;
    tinypy_value_t *deleter = field == 2 && replacement != NULL ? replacement : property->deleter;
    tinypy_value_t *doc = property->getter_doc != 0 ? NULL : property->doc;
    tinypy_value_t *constructor_items[4] = {
        getter != NULL ? getter : &vm->none_object.base,
        setter != NULL ? setter : &vm->none_object.base,
        deleter != NULL ? deleter : &vm->none_object.base,
        doc != NULL ? doc : &vm->none_object.base};
    tinypy_value_t *constructor_args = tinypy_tuple_from_items(vm, constructor_items, 4U);
    tinypy_value_t *return_value_1 = tinypy_call(&property->base.type->base.base, constructor_args, NULL, out_error);

    TINYPY_DECREF(constructor_args);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_property_getter_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    (void)user_data;
    tinypy_value_t *return_value_1 = __tinypy_internal_property_copy(function, args, kwargs, 0, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_property_setter_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    (void)user_data;
    tinypy_value_t *return_value_1 = __tinypy_internal_property_copy(function, args, kwargs, 1, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_property_deleter_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    (void)user_data;
    tinypy_value_t *return_value_1 = __tinypy_internal_property_copy(function, args, kwargs, 2, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_internal_descriptor_method_arguments(tinypy_vm_t *vm, tinypy_value_t *args, tinypy_value_t *kwargs, size_t minimum, size_t maximum, tinypy_error_t **out_error);
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_internal_method_slot_self(tinypy_vm_t *vm, tinypy_value_t *args, tinypy_value_t *kwargs, size_t minimum, size_t maximum, tinypy_error_t **out_error) {
    if (__tinypy_internal_descriptor_method_arguments(vm, args, kwargs, minimum, maximum, out_error) == 0) {
        return TINYPY_FALSE;
    }
    if (TINYPY_VALUE_KIND(TINYPY_TUPLE_GET(args, 0U)) != TINYPY_VALUE_METHOD) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "instancemethod operation requires a method", out_error);
        return TINYPY_FALSE;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_method_call_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (TINYPY_TUPLE_SIZE(args) == 0U || TINYPY_VALUE_KIND(TINYPY_TUPLE_GET(args, 0U)) != TINYPY_VALUE_METHOD) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "instancemethod operation requires a method", out_error);
        return NULL;
    }
    tinypy_value_t *self = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *const *items = tinypy_internal_tuple_items(args);
    tinypy_value_t *call_args = tinypy_tuple_from_items(vm, items + 1U, TINYPY_TUPLE_SIZE(args) - 1U);
    tinypy_value_t *result = tinypy_internal_method_call(self, call_args, kwargs, out_error);

    TINYPY_DECREF(call_args);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_method_new_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    size_t count;
    tinypy_value_t *method_type;
    tinypy_value_t *callable;
    tinypy_value_t *self;
    tinypy_value_t *owner;
    tinypy_bool_t owned_owner = TINYPY_FALSE;

    (void)user_data;
    if (__tinypy_internal_descriptor_method_arguments(vm, args, kwargs, 3U, 4U, out_error) == 0) {
        return NULL;
    }
    count = TINYPY_TUPLE_SIZE(args);
    method_type = TINYPY_TUPLE_GET(args, 0U);
    callable = TINYPY_TUPLE_GET(args, 1U);
    self = TINYPY_TUPLE_GET(args, 2U);
    if (method_type != &vm->types[TINYPY_VALUE_METHOD].base.base) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "instancemethod.__new__ requires the instancemethod type", out_error);
        return NULL;
    }
    if (tinypy_is_callable(callable) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "first instancemethod argument must be callable", out_error);
        return NULL;
    }
    if (TINYPY_VALUE_KIND(self) == TINYPY_VALUE_NONE) {
        self = NULL;
    }
    if (count == 3U) {
        if (self == NULL) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "unbound instancemethod requires an owner", out_error);
            return NULL;
        }
        owner = TINYPY_RET_NONE(vm);
        owned_owner = TINYPY_TRUE;
    }
    else {
        owner = TINYPY_TUPLE_GET(args, 3U);
    }
    tinypy_value_t *result = tinypy_method_new(callable, self, owner);
    if (owned_owner != 0) {
        TINYPY_DECREF(owner);
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_method_repr_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_internal_method_slot_self(vm, args, kwargs, 1U, 1U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *result = tinypy_internal_object_repr_builtin(TINYPY_TUPLE_GET(args, 0U), out_error);

    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_method_hash_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_internal_method_slot_self(vm, args, kwargs, 1U, 1U, out_error) == 0) {
        return NULL;
    }
    tinypy_hash_t hash = tinypy_internal_hash_builtin_value(TINYPY_TUPLE_GET(args, 0U), out_error);
    if (out_error != NULL && *out_error != NULL) {
        return NULL;
    }
    tinypy_value_t *result = tinypy_integer_from_i64(vm, hash);

    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_method_cmp_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_internal_method_slot_self(vm, args, kwargs, 2U, 2U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *left = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *right = TINYPY_TUPLE_GET(args, 1U);
    int64_t order;

    if (TINYPY_VALUE_KIND(right) == TINYPY_VALUE_METHOD) {
        int32_t equal = tinypy_compare_bool(left, right, TINYPY_COMPARE_EQUAL, out_error);

        if (equal < 0) {
            return NULL;
        }
        order = equal != 0 ? 0 : ((uintptr_t)left < (uintptr_t)right ? INT64_C(-1) : INT64_C(1));
    }
    else {
        order = (uintptr_t)left < (uintptr_t)right ? INT64_C(-1) : INT64_C(1);
    }
    tinypy_value_t *result = tinypy_integer_from_i64(vm, order);

    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_internal_descriptor_method_arguments(tinypy_vm_t *vm, tinypy_value_t *args, tinypy_value_t *kwargs, size_t minimum, size_t maximum, tinypy_error_t **out_error) {
    size_t count = TINYPY_TUPLE_SIZE(args);

    if ((kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U) || count < minimum || count > maximum) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "descriptor method received invalid arguments", out_error);
        return TINYPY_FALSE;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_descriptor_get_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *descriptor;
    tinypy_value_t *instance;
    tinypy_value_t *owner_value;
    tinypy_type_t *owner;
    size_t count;

    (void)user_data;
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 1U, 2U, TINYPY_ARITY_STYLE_UNPACK, out_error) == TINYPY_FALSE) {
        return NULL;
    }
    count = TINYPY_TUPLE_SIZE(args);
    descriptor = TINYPY_TUPLE_GET(args, 0U);
    instance = TINYPY_TUPLE_GET(args, 1U);
    if (TINYPY_VALUE_KIND(instance) == TINYPY_VALUE_NONE) {
        instance = NULL;
    }
    if (count == 3U) {
        owner_value = TINYPY_TUPLE_GET(args, 2U);
    }
    else {
        owner_value = &vm->none_object.base;
    }
    if (instance == NULL && TINYPY_VALUE_KIND(owner_value) == TINYPY_VALUE_NONE) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "__get__(None, None) is invalid", out_error);
        return NULL;
    }
    owner = TINYPY_VALUE_KIND(owner_value) == TINYPY_VALUE_TYPE ? (tinypy_type_t *)owner_value : (instance != NULL ? instance->type : &vm->types[TINYPY_VALUE_INSTANCE]);
    if (TINYPY_VALUE_KIND(descriptor) == TINYPY_VALUE_FUNCTION) {
        tinypy_value_t *return_value_1 = tinypy_method_new(descriptor, instance, owner_value);
        return return_value_1;
    }
    if (TINYPY_VALUE_KIND(descriptor) == TINYPY_VALUE_METHOD) {
        tinypy_value_t *return_value_2 = tinypy_internal_method_bind(descriptor, instance, owner_value, out_error);
        return return_value_2;
    }
    if (TINYPY_VALUE_KIND(descriptor) == TINYPY_VALUE_PROPERTY) {
        tinypy_value_t *return_value_3 = tinypy_internal_property_get(descriptor, instance, owner, out_error);
        return return_value_3;
    }
    if (TINYPY_VALUE_KIND(descriptor) == TINYPY_VALUE_STATIC_METHOD) {
        tinypy_value_t *return_value_4 = tinypy_internal_static_method_get(descriptor, instance, owner, out_error);
        return return_value_4;
    }
    if (TINYPY_VALUE_KIND(descriptor) == TINYPY_VALUE_CLASS_METHOD) {
        if (TINYPY_VALUE_KIND(owner_value) == TINYPY_VALUE_NONE) {
            owner_value = &instance->type->base.base;
        }
        tinypy_value_t *return_value_5 = tinypy_internal_class_method_bind(descriptor, owner_value, out_error);
        return return_value_5;
    }
    if (TINYPY_VALUE_KIND(descriptor) == TINYPY_VALUE_GETSET_DESCRIPTOR || TINYPY_VALUE_KIND(descriptor) == TINYPY_VALUE_MEMBER_DESCRIPTOR) {
        tinypy_value_t *return_value_6 = tinypy_internal_c_descriptor_get(descriptor, instance, owner, out_error);
        return return_value_6;
    }
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "__get__ requires a descriptor", out_error);
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_c_descriptor_set_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 2U, 2U, TINYPY_ARITY_STYLE_UNPACK, out_error) == TINYPY_FALSE) {
        return NULL;
    }
    tinypy_value_t *descriptor = TINYPY_TUPLE_GET(args, 0U);
    if (TINYPY_VALUE_KIND(descriptor) != TINYPY_VALUE_GETSET_DESCRIPTOR && TINYPY_VALUE_KIND(descriptor) != TINYPY_VALUE_MEMBER_DESCRIPTOR) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "__set__ requires a C descriptor", out_error);
        return NULL;
    }
    if (tinypy_internal_c_descriptor_set(descriptor, TINYPY_TUPLE_GET(args, 1U), TINYPY_TUPLE_GET(args, 2U), out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *result = TINYPY_RET_NONE(vm);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_c_descriptor_delete_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 1U, 1U, TINYPY_ARITY_STYLE_WRAPPER, out_error) == TINYPY_FALSE) {
        return NULL;
    }
    tinypy_value_t *descriptor = TINYPY_TUPLE_GET(args, 0U);
    if (TINYPY_VALUE_KIND(descriptor) != TINYPY_VALUE_GETSET_DESCRIPTOR && TINYPY_VALUE_KIND(descriptor) != TINYPY_VALUE_MEMBER_DESCRIPTOR) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "__delete__ requires a C descriptor", out_error);
        return NULL;
    }
    if (tinypy_internal_c_descriptor_set(descriptor, TINYPY_TUPLE_GET(args, 1U), NULL, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *result = TINYPY_RET_NONE(vm);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_internal_c_descriptor_refresh_owner(tinypy_c_descriptor_object_t *descriptor) {
    if (descriptor->owner_reference != NULL) {
        descriptor->owner = (tinypy_type_t *)tinypy_weakref_get(descriptor->owner_reference);
    }
}
//////////////////////////////////////////////////////////////////////////
/* Reports the slot that a writable __slots__ member addresses in instances of
   type, so that attribute caches can access it directly. */
tinypy_bool_t tinypy_internal_member_descriptor_slot(tinypy_value_t *descriptor_value, const tinypy_type_t *type, size_t *out_index) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(descriptor_value);

    if (descriptor_value->type != &vm->types[TINYPY_VALUE_MEMBER_DESCRIPTOR]) {
        return TINYPY_FALSE;
    }
    tinypy_c_descriptor_object_t *descriptor = TINYPY_C_DESCRIPTOR_OBJECT(descriptor_value);
    __tinypy_internal_c_descriptor_refresh_owner(descriptor);
    if (descriptor->field != (int32_t)TINYPY_INTERNAL_C_DESCRIPTOR_INSTANCE_SLOT || descriptor->writable == 0 || descriptor->owner == NULL || type->slots_offset == 0U || tinypy_type_is_subtype(type, descriptor->owner) == 0) {
        return TINYPY_FALSE;
    }
    *out_index = descriptor->index;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_c_descriptor_repr_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    static const char member_prefix[] = "<member '";
    static const char getset_prefix[] = "<attribute '";
    static const char owner_separator[] = "' of '";
    static const char suffix[] = "' objects>";
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_c_descriptor_object_t *descriptor;
    const char *prefix;
    size_t prefix_size;
    size_t name_size;
    size_t owner_size;
    size_t total_size;
    size_t offset = 0U;
    uint8_t *bytes;

    (void)user_data;
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 0U, 0U, TINYPY_ARITY_STYLE_WRAPPER, out_error) == TINYPY_FALSE) {
        return NULL;
    }
    tinypy_value_t *value = TINYPY_TUPLE_GET(args, 0U);
    if (TINYPY_VALUE_KIND(value) != TINYPY_VALUE_GETSET_DESCRIPTOR && TINYPY_VALUE_KIND(value) != TINYPY_VALUE_MEMBER_DESCRIPTOR) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "__repr__ requires a C descriptor", out_error);
        return NULL;
    }
    descriptor = TINYPY_C_DESCRIPTOR_OBJECT(value);
    __tinypy_internal_c_descriptor_refresh_owner(descriptor);
    prefix = TINYPY_VALUE_KIND(value) == TINYPY_VALUE_MEMBER_DESCRIPTOR ? member_prefix : getset_prefix;
    prefix_size = TINYPY_VALUE_KIND(value) == TINYPY_VALUE_MEMBER_DESCRIPTOR ? sizeof(member_prefix) - 1U : sizeof(getset_prefix) - 1U;
    static const char detached_owner[] = "<deleted type>";
    tinypy_message_part_t owner[3] = {{"", 0U}, {"", 0U}, {detached_owner, sizeof(detached_owner) - 1U}};

    if (descriptor->owner != NULL) {
        tinypy_internal_type_message_name(descriptor->owner, owner);
    }
    name_size = TINYPY_TEXT_BYTE_SIZE(descriptor->name);
    owner_size = owner[0].size + owner[1].size + owner[2].size;
    if (name_size > SIZE_MAX - prefix_size - (sizeof(owner_separator) - 1U) - (sizeof(suffix) - 1U) || owner_size > SIZE_MAX - prefix_size - name_size - (sizeof(owner_separator) - 1U) - (sizeof(suffix) - 1U)) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_MEMORY, "descriptor representation is too large", out_error);
        return NULL;
    }
    total_size = prefix_size + name_size + (sizeof(owner_separator) - 1U) + owner_size + (sizeof(suffix) - 1U);
    tinypy_value_t *result = tinypy_internal_text_allocate_uninitialized_checked(vm, TINYPY_VALUE_STRING, total_size, total_size, &bytes, out_error);
    if (result == NULL) {
        return NULL;
    }
    (void)memcpy(bytes + offset, prefix, prefix_size);
    offset += prefix_size;
    (void)memcpy(bytes + offset, TINYPY_TEXT_BYTES(descriptor->name), name_size);
    offset += name_size;
    (void)memcpy(bytes + offset, owner_separator, sizeof(owner_separator) - 1U);
    offset += sizeof(owner_separator) - 1U;
    for (size_t index = 0U; index < sizeof(owner) / sizeof(owner[0]); ++index) {
        (void)memcpy(bytes + offset, owner[index].bytes, owner[index].size);
        offset += owner[index].size;
    }
    (void)memcpy(bytes + offset, suffix, sizeof(suffix) - 1U);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_property_set_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 2U, 2U, TINYPY_ARITY_STYLE_UNPACK, out_error) == TINYPY_FALSE) {
        return NULL;
    }
    if (tinypy_internal_property_set(TINYPY_TUPLE_GET(args, 0U), TINYPY_TUPLE_GET(args, 1U), TINYPY_TUPLE_GET(args, 2U), out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *result = TINYPY_RET_NONE(vm);

    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_property_delete_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (tinypy_internal_native_method_arguments(function, args, kwargs, 1U, 1U, TINYPY_ARITY_STYLE_WRAPPER, out_error) == TINYPY_FALSE) {
        return NULL;
    }
    if (tinypy_internal_property_set(TINYPY_TUPLE_GET(args, 0U), TINYPY_TUPLE_GET(args, 1U), NULL, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *result = TINYPY_RET_NONE(vm);

    return result;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_c_descriptor_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data) {
    tinypy_c_descriptor_object_t *descriptor = TINYPY_C_DESCRIPTOR_OBJECT(value);
    if (descriptor->owner_retained != 0) {
        visit(&descriptor->owner->base.base, user_data);
    }
    if (descriptor->owner_reference != NULL) {
        visit(descriptor->owner_reference, user_data);
    }
    visit(descriptor->name, user_data);
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_c_descriptor_get(tinypy_value_t *descriptor_value, tinypy_value_t *instance, tinypy_type_t *owner, tinypy_error_t **out_error) {
    tinypy_value_t * function_result;
    tinypy_c_descriptor_object_t *descriptor = TINYPY_C_DESCRIPTOR_OBJECT(descriptor_value);
    tinypy_internal_c_descriptor_field_e field = (tinypy_internal_c_descriptor_field_e)descriptor->field;
    tinypy_vm_t *vm = TINYPY_VALUE_VM(descriptor_value);
    (void)owner;
    TINYPY_CLEAR_ERROR(out_error);
    __tinypy_internal_c_descriptor_refresh_owner(descriptor);
    if (instance == NULL) {
        return TINYPY_RET(descriptor_value);
    }
    if (descriptor->owner == NULL || tinypy_type_is_subtype(instance->type, descriptor->owner) == 0) {
        __tinypy_internal_c_descriptor_receiver_error(descriptor, instance, TINYPY_FALSE, out_error);
        return NULL;
    }
    if (field >= TINYPY_INTERNAL_C_DESCRIPTOR_NUMERIC_REAL && field <= TINYPY_INTERNAL_C_DESCRIPTOR_NUMERIC_DENOMINATOR) {
        tinypy_value_t *result = tinypy_internal_numeric_field(instance, (int32_t)field - (int32_t)TINYPY_INTERNAL_C_DESCRIPTOR_NUMERIC_REAL, out_error);

        return result;
    }
    if (field >= TINYPY_INTERNAL_C_DESCRIPTOR_METADATA_NAME && field <= TINYPY_INTERNAL_C_DESCRIPTOR_METADATA_DOC) {
        tinypy_c_descriptor_object_t *source = TINYPY_C_DESCRIPTOR_OBJECT(instance);

        if (field == TINYPY_INTERNAL_C_DESCRIPTOR_METADATA_DOC) {
            return TINYPY_RET_NONE(vm);
        }
        __tinypy_internal_c_descriptor_refresh_owner(source);
        if (field == TINYPY_INTERNAL_C_DESCRIPTOR_METADATA_OWNER && source->owner == NULL) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_ATTRIBUTE, "descriptor owner type no longer exists", out_error);
            return NULL;
        }
        tinypy_value_t *result = field == TINYPY_INTERNAL_C_DESCRIPTOR_METADATA_NAME ? source->name : &source->owner->base.base;

        return TINYPY_RET(result);
    }
    if (field >= TINYPY_INTERNAL_C_DESCRIPTOR_NATIVE_NAME && field <= TINYPY_INTERNAL_C_DESCRIPTOR_NATIVE_DOC) {
        tinypy_native_function_object_t *native = TINYPY_NATIVE_FUNCTION_OBJECT(instance);
        tinypy_value_t *value = NULL;

        if (field == TINYPY_INTERNAL_C_DESCRIPTOR_NATIVE_NAME) {
            value = native->name;
        }
        else if (field == TINYPY_INTERNAL_C_DESCRIPTOR_NATIVE_SELF) {
            value = native->self;
        }
        else if (field == TINYPY_INTERNAL_C_DESCRIPTOR_NATIVE_MODULE) {
            value = native->module;
        }
        else if (field == TINYPY_INTERNAL_C_DESCRIPTOR_NATIVE_OWNER) {
            tinypy_native_function_object_t *source = native->function != NULL ? TINYPY_NATIVE_FUNCTION_OBJECT(native->function) : native;
            value = source->owner != NULL ? &source->owner->base.base : NULL;
        }
        function_result = __tinypy_internal_c_descriptor_optional(vm, value);
        return function_result;
    }
    if (field == TINYPY_INTERNAL_C_DESCRIPTOR_STRUCT_FORMAT || field == TINYPY_INTERNAL_C_DESCRIPTOR_STRUCT_SIZE) {
        function_result = tinypy_internal_struct_get_field(instance, descriptor->name, out_error);
        return function_result;
    }
    if (field >= TINYPY_INTERNAL_C_DESCRIPTOR_TYPE_NAME && field <= TINYPY_INTERNAL_C_DESCRIPTOR_TYPE_DOC) {
        if (TINYPY_VALUE_KIND(instance) != TINYPY_VALUE_TYPE) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "type descriptor requires a type object", out_error);
            return NULL;
        }
        if (field == TINYPY_INTERNAL_C_DESCRIPTOR_TYPE_ABSTRACT_METHODS) {
            tinypy_value_t *value = tinypy_internal_dict_get_optional(vm, ((tinypy_type_t *)instance)->dict, descriptor->name);

            if (value == NULL) {
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_ATTRIBUTE, "type has no __abstractmethods__ attribute", out_error);
                return NULL;
            }
            return TINYPY_RET(value);
        }
        tinypy_value_t *return_value_1 = tinypy_internal_object_builtin_attribute(instance, descriptor->name);
        return return_value_1;
    }
    if (field == TINYPY_INTERNAL_C_DESCRIPTOR_INSTANCE_SLOT) {
        if (instance->type->slots_offset == 0U) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "slot descriptor requires an instance", out_error);
            return NULL;
        }
        tinypy_value_t *slot_value = *tinypy_internal_object_member_slot(instance, descriptor->index);
        if (slot_value == NULL) {
            tinypy_internal_make_attribute_name_error(vm, descriptor->name, out_error);
            return NULL;
        }
        return TINYPY_RET(slot_value);
    }
    if (field == TINYPY_INTERNAL_C_DESCRIPTOR_INSTANCE_DICT) {
        tinypy_value_t **dict_slot = tinypy_internal_object_dict_slot(instance);

        if (dict_slot == NULL) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_ATTRIBUTE, "object has no __dict__", out_error);
            return NULL;
        }
        if (*dict_slot == NULL) {
            *dict_slot = tinypy_dict_new(vm);
        }
        return TINYPY_RET(*dict_slot);
    }
    if (field == TINYPY_INTERNAL_C_DESCRIPTOR_INSTANCE_WEAKREF) {
        tinypy_value_t **weakref_slot = tinypy_internal_weakref_head_slot(instance);

        function_result = __tinypy_internal_c_descriptor_optional(vm, weakref_slot != NULL ? *weakref_slot : NULL);
        return function_result;
    }
    if (field == TINYPY_INTERNAL_C_DESCRIPTOR_CALLABLE_FUNCTION) {
        tinypy_value_t *callable = TINYPY_CALLABLE_DESCRIPTOR_OBJECT(instance)->callable;

        function_result = __tinypy_internal_c_descriptor_optional(vm, callable);
        return function_result;
    }
    if (field == TINYPY_INTERNAL_C_DESCRIPTOR_CELL_CONTENT) {
        tinypy_value_t *content = TINYPY_CELL_OBJECT(instance)->content;

        if (content == NULL) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "cell is empty", out_error);
            return NULL;
        }
        return TINYPY_RET(content);
    }
    if (field == TINYPY_INTERNAL_C_DESCRIPTOR_MODULE_DICT) {
        tinypy_value_t *dict = TINYPY_MODULE_OBJECT(instance)->dict;

        if (dict == NULL) {
            tinypy_value_t *result = TINYPY_RET_NONE(vm);

            return result;
        }
        return TINYPY_RET(dict);
    }
    if (field >= TINYPY_INTERNAL_C_DESCRIPTOR_SUPER_THISCLASS && field <= TINYPY_INTERNAL_C_DESCRIPTOR_SUPER_SELF_CLASS) {
        tinypy_super_object_t *super_value = TINYPY_SUPER_OBJECT(instance);

        if (field == TINYPY_INTERNAL_C_DESCRIPTOR_SUPER_THISCLASS) {
            return TINYPY_RET(&super_value->type->base.base);
        }
        function_result = field == TINYPY_INTERNAL_C_DESCRIPTOR_SUPER_SELF ? super_value->object : (super_value->object_type != NULL ? &super_value->object_type->base.base : NULL);
        tinypy_value_t *return_value_1 = __tinypy_internal_c_descriptor_optional(vm, function_result);
        return return_value_1;
    }
    if (field >= TINYPY_INTERNAL_C_DESCRIPTOR_PARTIAL_FUNCTION && field <= TINYPY_INTERNAL_C_DESCRIPTOR_PARTIAL_KEYWORDS) {
        tinypy_partial_object_t *partial = TINYPY_PARTIAL_OBJECT(instance);

        if (field == TINYPY_INTERNAL_C_DESCRIPTOR_PARTIAL_FUNCTION) {
            function_result = partial->callable;
        }
        else if (field == TINYPY_INTERNAL_C_DESCRIPTOR_PARTIAL_ARGS) {
            function_result = partial->args;
        }
        else {
            function_result = partial->keywords;
        }
        return TINYPY_RET(function_result);
    }
    if (field == TINYPY_INTERNAL_C_DESCRIPTOR_EXCEPTION_ARGS || field == TINYPY_INTERNAL_C_DESCRIPTOR_EXCEPTION_MESSAGE) {
        tinypy_internal_exception_payload_t *payload = (tinypy_internal_exception_payload_t *)tinypy_native_instance_payload(instance);

        if (field == TINYPY_INTERNAL_C_DESCRIPTOR_EXCEPTION_ARGS) {
            function_result = __tinypy_internal_c_descriptor_optional(vm, payload->args);
            return function_result;
        }
        tinypy_value_t **dict_slot = tinypy_internal_object_dict_slot(instance);
        if (dict_slot != NULL && *dict_slot != NULL) {
            tinypy_value_t *key = vm->internal_message_key;
            tinypy_value_t *message = tinypy_dict_get_optional(*dict_slot, key);

            if (message != NULL) {
                return TINYPY_RET(message);
            }
        }
        if (payload->message == NULL) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_ATTRIBUTE, "message attribute was deleted", out_error);
            return NULL;
        }
        return TINYPY_RET(payload->message);
    }
    if (field >= TINYPY_INTERNAL_C_DESCRIPTOR_UNICODE_ENCODING && field <= TINYPY_INTERNAL_C_DESCRIPTOR_UNICODE_END) {
        tinypy_internal_unicode_error_payload_t *payload = (tinypy_internal_unicode_error_payload_t *)tinypy_native_instance_payload(instance);

        if (field == TINYPY_INTERNAL_C_DESCRIPTOR_UNICODE_START || field == TINYPY_INTERNAL_C_DESCRIPTOR_UNICODE_END) {
            tinypy_value_t *result = tinypy_integer_from_i64(vm, field == TINYPY_INTERNAL_C_DESCRIPTOR_UNICODE_START ? payload->start : payload->end);
            return result;
        }
        tinypy_value_t *value = field == TINYPY_INTERNAL_C_DESCRIPTOR_UNICODE_ENCODING ? payload->encoding : (field == TINYPY_INTERNAL_C_DESCRIPTOR_UNICODE_OBJECT ? payload->object : payload->reason);
        tinypy_value_t *result = __tinypy_internal_c_descriptor_optional(vm, value);
        return result;
    }
    if (field >= TINYPY_INTERNAL_C_DESCRIPTOR_METHOD_FUNCTION && field <= TINYPY_INTERNAL_C_DESCRIPTOR_METHOD_OWNER) {
        tinypy_method_object_t *method = TINYPY_METHOD_OBJECT(instance);
        tinypy_value_t *value = field == TINYPY_INTERNAL_C_DESCRIPTOR_METHOD_FUNCTION ? method->function : (field == TINYPY_INTERNAL_C_DESCRIPTOR_METHOD_SELF ? method->self : method->owner);

        function_result = __tinypy_internal_c_descriptor_optional(vm, value);
        return function_result;
    }
    /* instancemethod_get_doc */
    if (field == TINYPY_INTERNAL_C_DESCRIPTOR_METHOD_DOC) {
        function_result = tinypy_internal_object_get_attr_key(TINYPY_METHOD_OBJECT(instance)->function, vm->internal_special_doc_key, out_error);
        return function_result;
    }
    if (field >= TINYPY_INTERNAL_C_DESCRIPTOR_PROPERTY_GETTER && field <= TINYPY_INTERNAL_C_DESCRIPTOR_PROPERTY_DOC) {
        tinypy_property_object_t *property = TINYPY_PROPERTY_OBJECT(instance);
        tinypy_value_t *const values[4] = {property->getter, property->setter, property->deleter, property->doc};

        function_result = __tinypy_internal_c_descriptor_optional(vm, values[(int32_t)field - (int32_t)TINYPY_INTERNAL_C_DESCRIPTOR_PROPERTY_GETTER]);
        return function_result;
    }
    if (field >= TINYPY_INTERNAL_C_DESCRIPTOR_CODE_ARG_COUNT && field <= TINYPY_INTERNAL_C_DESCRIPTOR_CODE_LNOTAB) {
        tinypy_code_object_t *code = TINYPY_CODE_OBJECT(instance);

        switch (field) {
        case TINYPY_INTERNAL_C_DESCRIPTOR_CODE_ARG_COUNT:
            function_result = tinypy_integer_from_i64(vm, code->arg_count);
            return function_result;
        case TINYPY_INTERNAL_C_DESCRIPTOR_CODE_LOCAL_COUNT:
            function_result = tinypy_integer_from_i64(vm, code->local_count);
            return function_result;
        case TINYPY_INTERNAL_C_DESCRIPTOR_CODE_STACK_SIZE:
            function_result = tinypy_integer_from_i64(vm, code->stack_size);
            return function_result;
        case TINYPY_INTERNAL_C_DESCRIPTOR_CODE_FLAGS:
            function_result = tinypy_integer_from_i64(vm, code->flags);
            return function_result;
        case TINYPY_INTERNAL_C_DESCRIPTOR_CODE_BYTECODE:
            function_result = code->bytecode;
            break;
        case TINYPY_INTERNAL_C_DESCRIPTOR_CODE_CONSTS:
            function_result = code->consts;
            break;
        case TINYPY_INTERNAL_C_DESCRIPTOR_CODE_NAMES:
            function_result = code->names;
            break;
        case TINYPY_INTERNAL_C_DESCRIPTOR_CODE_VARNAMES:
            function_result = code->varnames;
            break;
        case TINYPY_INTERNAL_C_DESCRIPTOR_CODE_FREEVARS:
            function_result = code->freevars;
            break;
        case TINYPY_INTERNAL_C_DESCRIPTOR_CODE_CELLVARS:
            function_result = code->cellvars;
            break;
        case TINYPY_INTERNAL_C_DESCRIPTOR_CODE_FILENAME:
            function_result = code->filename;
            break;
        case TINYPY_INTERNAL_C_DESCRIPTOR_CODE_NAME:
            function_result = code->name;
            break;
        case TINYPY_INTERNAL_C_DESCRIPTOR_CODE_FIRST_LINE:
            function_result = tinypy_integer_from_i64(vm, code->first_line_number);
            return function_result;
        case TINYPY_INTERNAL_C_DESCRIPTOR_CODE_LNOTAB:
            function_result = code->lnotab;
            break;
        default:
            return NULL;
        }
        return TINYPY_RET(function_result);
    }
    if ((field >= TINYPY_INTERNAL_C_DESCRIPTOR_FRAME_BACK && field <= TINYPY_INTERNAL_C_DESCRIPTOR_FRAME_TRACE) || (field >= TINYPY_INTERNAL_C_DESCRIPTOR_FRAME_EXCEPTION_TYPE && field <= TINYPY_INTERNAL_C_DESCRIPTOR_FRAME_RESTRICTED)) {
        tinypy_frame_object_t *frame = TINYPY_FRAME_OBJECT(instance);

        switch (field) {
        case TINYPY_INTERNAL_C_DESCRIPTOR_FRAME_BACK:
            function_result = __tinypy_internal_c_descriptor_optional(vm, frame->back);
            return function_result;
        case TINYPY_INTERNAL_C_DESCRIPTOR_FRAME_CODE:
            function_result = frame->code;
            break;
        case TINYPY_INTERNAL_C_DESCRIPTOR_FRAME_BUILTINS:
            function_result = frame->builtins;
            break;
        case TINYPY_INTERNAL_C_DESCRIPTOR_FRAME_GLOBALS:
            function_result = frame->globals;
            break;
        case TINYPY_INTERNAL_C_DESCRIPTOR_FRAME_LOCALS:
            function_result = tinypy_internal_frame_locals(frame);
            break;
        case TINYPY_INTERNAL_C_DESCRIPTOR_FRAME_LAST_INSTRUCTION:
            function_result = tinypy_integer_from_i64(vm, frame->last_instruction);
            return function_result;
        case TINYPY_INTERNAL_C_DESCRIPTOR_FRAME_LINE_NUMBER:
            function_result = tinypy_integer_from_i64(vm, tinypy_frame_line_number(instance));
            return function_result;
        case TINYPY_INTERNAL_C_DESCRIPTOR_FRAME_TRACE:
            function_result = __tinypy_internal_c_descriptor_optional(vm, frame->trace);
            return function_result;
        case TINYPY_INTERNAL_C_DESCRIPTOR_FRAME_EXCEPTION_TYPE:
            function_result = __tinypy_internal_c_descriptor_optional(vm, frame->previous_handled_type);
            return function_result;
        case TINYPY_INTERNAL_C_DESCRIPTOR_FRAME_EXCEPTION_VALUE:
            function_result = __tinypy_internal_c_descriptor_optional(vm, frame->previous_handled_value);
            return function_result;
        case TINYPY_INTERNAL_C_DESCRIPTOR_FRAME_EXCEPTION_TRACEBACK:
            function_result = __tinypy_internal_c_descriptor_optional(vm, frame->previous_handled_traceback);
            return function_result;
        case TINYPY_INTERNAL_C_DESCRIPTOR_FRAME_RESTRICTED:
            function_result = tinypy_bool_from_i32(vm, frame->builtins != vm->builtins);
            return function_result;
        default:
            return NULL;
        }
        return TINYPY_RET(function_result);
    }
    if (field >= TINYPY_INTERNAL_C_DESCRIPTOR_TRACEBACK_NEXT && field <= TINYPY_INTERNAL_C_DESCRIPTOR_TRACEBACK_LINE_NUMBER) {
        tinypy_traceback_object_t *traceback = TINYPY_TRACEBACK_OBJECT(instance);

        if (field == TINYPY_INTERNAL_C_DESCRIPTOR_TRACEBACK_NEXT) {
            function_result = __tinypy_internal_c_descriptor_optional(vm, traceback->next);
            return function_result;
        }
        if (field == TINYPY_INTERNAL_C_DESCRIPTOR_TRACEBACK_FRAME) {
            return TINYPY_RET(traceback->frame);
        }
        function_result = tinypy_integer_from_i64(vm, field == TINYPY_INTERNAL_C_DESCRIPTOR_TRACEBACK_LAST_INSTRUCTION ? traceback->last_instruction : traceback->line_number);
        return function_result;
    }
    if (field >= TINYPY_INTERNAL_C_DESCRIPTOR_GENERATOR_NAME && field <= TINYPY_INTERNAL_C_DESCRIPTOR_GENERATOR_RUNNING) {
        tinypy_generator_object_t *generator = TINYPY_GENERATOR_OBJECT(instance);

        if (field == TINYPY_INTERNAL_C_DESCRIPTOR_GENERATOR_NAME) {
            function_result = TINYPY_CODE_OBJECT(generator->code)->name;
        }
        else if (field == TINYPY_INTERNAL_C_DESCRIPTOR_GENERATOR_CODE) {
            function_result = generator->code;
        }
        else if (field == TINYPY_INTERNAL_C_DESCRIPTOR_GENERATOR_FRAME) {
            function_result = __tinypy_internal_c_descriptor_optional(vm, generator->frame);
            return function_result;
        }
        else {
            function_result = tinypy_integer_from_i64(vm, generator->running != 0 ? INT64_C(1) : INT64_C(0));
            return function_result;
        }
        return TINYPY_RET(function_result);
    }
    if (TINYPY_VALUE_KIND(instance) != TINYPY_VALUE_FUNCTION) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "descriptor does not apply to this object", out_error);
        return NULL;
    }
    tinypy_function_object_t *function = TINYPY_FUNCTION_OBJECT(instance);
    switch (field) {
    case TINYPY_INTERNAL_C_DESCRIPTOR_FUNCTION_CODE:
        return TINYPY_RET(function->code);
    case TINYPY_INTERNAL_C_DESCRIPTOR_FUNCTION_GLOBALS:
        return TINYPY_RET(function->globals);
    case TINYPY_INTERNAL_C_DESCRIPTOR_FUNCTION_DEFAULTS:
        function_result = __tinypy_internal_c_descriptor_optional(vm, function->defaults);
        return function_result;
    case TINYPY_INTERNAL_C_DESCRIPTOR_FUNCTION_CLOSURE:
        function_result = __tinypy_internal_c_descriptor_optional(vm, function->closure);
        return function_result;
    case TINYPY_INTERNAL_C_DESCRIPTOR_FUNCTION_NAME:
        return TINYPY_RET(function->name);
    case TINYPY_INTERNAL_C_DESCRIPTOR_FUNCTION_DOC:
        function_result = __tinypy_internal_c_descriptor_optional(vm, function->doc);
        return function_result;
    case TINYPY_INTERNAL_C_DESCRIPTOR_FUNCTION_DICT:
        if (function->dict == NULL) {
            function->dict = tinypy_dict_new(vm);
        }
        return TINYPY_RET(function->dict);
    case TINYPY_INTERNAL_C_DESCRIPTOR_FUNCTION_MODULE:
        function_result = __tinypy_internal_c_descriptor_optional(vm, function->module);
        return function_result;
    default:
        return NULL;
    }
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_c_descriptor_set(tinypy_value_t *descriptor_value, tinypy_value_t *instance, tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_c_descriptor_object_t *descriptor = TINYPY_C_DESCRIPTOR_OBJECT(descriptor_value);
    tinypy_vm_t *vm = TINYPY_VALUE_VM(descriptor_value);
    tinypy_internal_c_descriptor_field_e field = (tinypy_internal_c_descriptor_field_e)descriptor->field;
    TINYPY_CLEAR_ERROR(out_error);
    __tinypy_internal_c_descriptor_refresh_owner(descriptor);
    if (descriptor->owner == NULL || tinypy_type_is_subtype(instance->type, descriptor->owner) == 0) {
        __tinypy_internal_c_descriptor_receiver_error(descriptor, instance, TINYPY_TRUE, out_error);
        return TINYPY_FALSE;
    }
    if (descriptor->writable == 0) {
        if (field == TINYPY_INTERNAL_C_DESCRIPTOR_TYPE_DOC || field == TINYPY_INTERNAL_C_DESCRIPTOR_TYPE_DICT || field == TINYPY_INTERNAL_C_DESCRIPTOR_FRAME_LOCALS || field == TINYPY_INTERNAL_C_DESCRIPTOR_FRAME_RESTRICTED || field == TINYPY_INTERNAL_C_DESCRIPTOR_GENERATOR_NAME || field == TINYPY_INTERNAL_C_DESCRIPTOR_METADATA_DOC || (TINYPY_VALUE_KIND(descriptor_value) == TINYPY_VALUE_GETSET_DESCRIPTOR && field >= TINYPY_INTERNAL_C_DESCRIPTOR_NATIVE_NAME && field <= TINYPY_INTERNAL_C_DESCRIPTOR_METHOD_DOC)) {
            tinypy_message_part_t parts[] = {
                TINYPY_MESSAGE_PART_LITERAL("attribute '"), {(const char *)TINYPY_TEXT_BYTES(descriptor->name), TINYPY_TEXT_BYTE_SIZE(descriptor->name)},
                TINYPY_MESSAGE_PART_LITERAL("' of '"), {descriptor->owner->name, descriptor->owner->name_size},
                TINYPY_MESSAGE_PART_LITERAL("' objects is not writable")
            };

            tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_ATTRIBUTE, parts, 5U, out_error);
        }
        else {
            __tinypy_internal_c_descriptor_readonly(vm, out_error);
        }
        return TINYPY_FALSE;
    }
    if (field == TINYPY_INTERNAL_C_DESCRIPTOR_NATIVE_MODULE) {
        __tinypy_internal_c_descriptor_replace(&TINYPY_NATIVE_FUNCTION_OBJECT(instance)->module, value);
        return TINYPY_TRUE;
    }
    if (field >= TINYPY_INTERNAL_C_DESCRIPTOR_TYPE_NAME && field <= TINYPY_INTERNAL_C_DESCRIPTOR_TYPE_DOC) {
        tinypy_type_t *type;

        if (TINYPY_VALUE_KIND(instance) != TINYPY_VALUE_TYPE) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "type descriptor requires a type object", out_error);
            return TINYPY_FALSE;
        }
        type = (tinypy_type_t *)instance;
        if ((type->flags & TINYPY_TYPE_FLAG_IMMUTABLE) != 0U) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "type attributes are read-only", out_error);
            return TINYPY_FALSE;
        }
        if (value == NULL && (field == TINYPY_INTERNAL_C_DESCRIPTOR_TYPE_BASES || field == TINYPY_INTERNAL_C_DESCRIPTOR_TYPE_MODULE)) {
            tinypy_message_part_t parts[] = {
                TINYPY_MESSAGE_PART_LITERAL("can't delete "), {type->name, type->name_size}, TINYPY_MESSAGE_PART_LITERAL("."), TINYPY_MESSAGE_PART_TEXT(descriptor->name)
            };

            tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
            return TINYPY_FALSE;
        }
        if (field == TINYPY_INTERNAL_C_DESCRIPTOR_TYPE_BASES) {
            tinypy_bool_t result = tinypy_internal_type_set_bases(type, value, out_error);

            return result;
        }
        if (field == TINYPY_INTERNAL_C_DESCRIPTOR_TYPE_NAME) {
            tinypy_bool_t result = tinypy_internal_type_set_name(type, value, out_error);

            return result;
        }
        if (field == TINYPY_INTERNAL_C_DESCRIPTOR_TYPE_MODULE) {
            tinypy_internal_type_set_attr_key(type, descriptor->name, value);
            return TINYPY_TRUE;
        }
        if (field == TINYPY_INTERNAL_C_DESCRIPTOR_TYPE_ABSTRACT_METHODS) {
            if (value == NULL) {
                if (tinypy_internal_dict_delete_optional(vm, type->dict, descriptor->name) == 0) {
                    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_ATTRIBUTE, "__abstractmethods__", out_error);
                    return TINYPY_FALSE;
                }
                type->flags &= ~TINYPY_TYPE_FLAG_ABSTRACT;
                return TINYPY_TRUE;
            }
            int32_t abstract = tinypy_truth(value, out_error);
            if (abstract < 0) {
                return TINYPY_FALSE;
            }
            tinypy_internal_type_set_attr_key(type, descriptor->name, value);
            if (abstract != 0) {
                type->flags |= TINYPY_TYPE_FLAG_ABSTRACT;
            }
            else {
                type->flags &= ~TINYPY_TYPE_FLAG_ABSTRACT;
            }
            return TINYPY_TRUE;
        }
        __tinypy_internal_c_descriptor_readonly(vm, out_error);
        return TINYPY_FALSE;
    }
    if (field == TINYPY_INTERNAL_C_DESCRIPTOR_INSTANCE_SLOT) {
        if (instance->type->slots_offset == 0U) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "slot descriptor requires an instance", out_error);
            return TINYPY_FALSE;
        }
        tinypy_value_t **slot = tinypy_internal_object_member_slot(instance, descriptor->index);
        tinypy_value_t *previous = *slot;
        if (value == NULL && previous == NULL) {
            tinypy_internal_make_attribute_name_error(vm, descriptor->name, out_error);
            return TINYPY_FALSE;
        }
        if (value != NULL) {
            TINYPY_INCREF(value);
        }
        *slot = value;
        if (previous != NULL) {
            TINYPY_DECREF(previous);
        }
        return TINYPY_TRUE;
    }
    if (field == TINYPY_INTERNAL_C_DESCRIPTOR_INSTANCE_DICT) {
        /* subtype_setdict: deleting leaves the dictionary to be recreated. */
        tinypy_value_t **dict_slot = tinypy_internal_object_dict_slot(instance);

        if (dict_slot == NULL) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_ATTRIBUTE, "This object has no __dict__", out_error);
            return TINYPY_FALSE;
        }
        if (TINYPY_VALUE_KIND(instance) == TINYPY_VALUE_PARTIAL && (value == NULL || TINYPY_VALUE_KIND(value) != TINYPY_VALUE_DICT)) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, value == NULL ? "a partial object's dictionary may not be deleted" : "setting partial object's dictionary to a non-dict", out_error);
            return TINYPY_FALSE;
        }
        if (value != NULL && TINYPY_VALUE_KIND(value) != TINYPY_VALUE_DICT) {
            tinypy_message_part_t parts[] = {
                TINYPY_MESSAGE_PART_LITERAL("__dict__ must be set to a dictionary, not a '"),
                TINYPY_MESSAGE_PART_TYPE_NAME(value),
                TINYPY_MESSAGE_PART_LITERAL("'"),
            };

            tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
            return TINYPY_FALSE;
        }
        __tinypy_internal_c_descriptor_replace(dict_slot, value);
        return TINYPY_TRUE;
    }
    if (field == TINYPY_INTERNAL_C_DESCRIPTOR_EXCEPTION_ARGS) {
        tinypy_internal_exception_payload_t *payload = (tinypy_internal_exception_payload_t *)tinypy_native_instance_payload(instance);

        if (value == NULL) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "args may not be deleted", out_error);
            return TINYPY_FALSE;
        }
        tinypy_value_t *constructor_args = tinypy_tuple_from_items(vm, &value, 1U);
        tinypy_value_t *converted = tinypy_internal_tuple_create(&vm->types[TINYPY_VALUE_TUPLE], constructor_args, NULL, out_error);

        TINYPY_DECREF(constructor_args);
        if (converted == NULL) {
            return TINYPY_FALSE;
        }
        __tinypy_internal_c_descriptor_replace(&payload->args, converted);
        TINYPY_DECREF(converted);
        return TINYPY_TRUE;
    }
    if (field == TINYPY_INTERNAL_C_DESCRIPTOR_EXCEPTION_MESSAGE) {
        tinypy_internal_exception_payload_t *payload = (tinypy_internal_exception_payload_t *)tinypy_native_instance_payload(instance);
        tinypy_value_t **dict_slot = tinypy_internal_object_dict_slot(instance);
        tinypy_value_t *key = vm->internal_message_key;

        if (value == NULL) {
            if (dict_slot != NULL && *dict_slot != NULL) {
                (void)tinypy_internal_dict_delete_optional(vm, *dict_slot, key);
            }
            __tinypy_internal_c_descriptor_replace(&payload->message, NULL);
            return TINYPY_TRUE;
        }
        if (dict_slot == NULL) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_ATTRIBUTE, "object has no __dict__", out_error);
            return TINYPY_FALSE;
        }
        if (*dict_slot == NULL) {
            *dict_slot = tinypy_dict_new(vm);
        }
        tinypy_bool_t stored = tinypy_internal_dict_set_checked(vm, *dict_slot, key, value, out_error);
        return stored;
    }
    if (field >= TINYPY_INTERNAL_C_DESCRIPTOR_UNICODE_ENCODING && field <= TINYPY_INTERNAL_C_DESCRIPTOR_UNICODE_END) {
        tinypy_internal_unicode_error_payload_t *payload = (tinypy_internal_unicode_error_payload_t *)tinypy_native_instance_payload(instance);

        if (field == TINYPY_INTERNAL_C_DESCRIPTOR_UNICODE_START || field == TINYPY_INTERNAL_C_DESCRIPTOR_UNICODE_END) {
            int64_t *position = field == TINYPY_INTERNAL_C_DESCRIPTOR_UNICODE_START ? &payload->start : &payload->end;
            int64_t integer;
            tinypy_bool_t converted;

            if (value == NULL) {
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "can't delete numeric attribute", out_error);
                return TINYPY_FALSE;
            }
            tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);
            if (kind == TINYPY_VALUE_BOOL || kind == TINYPY_VALUE_INTEGER || kind == TINYPY_VALUE_LONG) {
                converted = tinypy_internal_index_as_i64(value, &integer, TINYPY_FALSE, out_error);
            }
            else {
                tinypy_bool_t handled;
                tinypy_value_t *number = tinypy_internal_call_conversion(value, vm->internal_special_int_key, &handled, out_error);

                converted = TINYPY_FALSE;
                if (handled == 0) {
                    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "an integer is required", out_error);
                }
                else if (number != NULL) {
                    kind = TINYPY_VALUE_KIND(number);
                    if (kind == TINYPY_VALUE_BOOL || kind == TINYPY_VALUE_INTEGER || kind == TINYPY_VALUE_LONG) {
                        converted = tinypy_internal_index_as_i64(number, &integer, TINYPY_FALSE, out_error);
                    }
                    else {
                        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "__int__ returned a non-integer", out_error);
                    }
                    TINYPY_DECREF(number);
                }
            }
            *position = converted != 0 ? integer : INT64_C(-1);
            return converted;
        }
        tinypy_value_t **slot = field == TINYPY_INTERNAL_C_DESCRIPTOR_UNICODE_ENCODING ? &payload->encoding : (field == TINYPY_INTERNAL_C_DESCRIPTOR_UNICODE_OBJECT ? &payload->object : &payload->reason);

        __tinypy_internal_c_descriptor_replace(slot, value);
        return TINYPY_TRUE;
    }
    if (field == TINYPY_INTERNAL_C_DESCRIPTOR_FRAME_TRACE) {
        tinypy_frame_object_t *frame = TINYPY_FRAME_OBJECT(instance);

        __tinypy_internal_c_descriptor_replace(&frame->trace, value);
        return TINYPY_TRUE;
    }
    if (field >= TINYPY_INTERNAL_C_DESCRIPTOR_FRAME_EXCEPTION_TYPE && field <= TINYPY_INTERNAL_C_DESCRIPTOR_FRAME_EXCEPTION_TRACEBACK) {
        tinypy_frame_object_t *frame = TINYPY_FRAME_OBJECT(instance);
        tinypy_value_t **slot = field == TINYPY_INTERNAL_C_DESCRIPTOR_FRAME_EXCEPTION_TYPE ? &frame->previous_handled_type : (field == TINYPY_INTERNAL_C_DESCRIPTOR_FRAME_EXCEPTION_VALUE ? &frame->previous_handled_value : &frame->previous_handled_traceback);
        tinypy_value_t *incoming = value != NULL && TINYPY_VALUE_KIND(value) == TINYPY_VALUE_NONE ? NULL : value;
        tinypy_value_t *previous = *slot;

        if (incoming != NULL) {
            TINYPY_INCREF(incoming);
        }
        *slot = NULL;
        if (previous != NULL) {
            TINYPY_DECREF(previous);
        }
        previous = *slot;
        *slot = incoming;
        if (previous != NULL) {
            TINYPY_DECREF(previous);
        }
        return TINYPY_TRUE;
    }
    if (TINYPY_VALUE_KIND(instance) != TINYPY_VALUE_FUNCTION) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "descriptor does not apply to this object", out_error);
        return TINYPY_FALSE;
    }
    tinypy_function_object_t *function = TINYPY_FUNCTION_OBJECT(instance);
    switch (field) {
    case TINYPY_INTERNAL_C_DESCRIPTOR_FUNCTION_CODE:
        if (value == NULL || TINYPY_VALUE_KIND(value) != TINYPY_VALUE_CODE) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "__code__ must be set to a code object", out_error);
            return TINYPY_FALSE;
        }
        size_t closure_size = function->closure != NULL ? TINYPY_TUPLE_SIZE(function->closure) : 0U;
        size_t free_count = TINYPY_TUPLE_SIZE(TINYPY_CODE_FREEVARS(value));
        tinypy_bool_t condition = closure_size != free_count;
        if (condition) {
            const char *name = (const char *)TINYPY_TEXT_BYTES(function->name);
            size_t name_size = TINYPY_TEXT_BYTE_SIZE(function->name);
            const char *terminator = (const char *)memchr(name, '\0', name_size);
            char closure_buffer[TINYPY_MESSAGE_SIZE_BUFFER];
            char free_buffer[TINYPY_MESSAGE_SIZE_BUFFER];
            size_t closure_text_size = tinypy_internal_format_size(closure_buffer, closure_size);
            size_t free_text_size = tinypy_internal_format_size(free_buffer, free_count);
            tinypy_message_part_t parts[] = {
                {name, terminator != NULL ? (size_t)(terminator - name) : name_size},
                TINYPY_MESSAGE_PART_LITERAL("() requires a code object with "), {closure_buffer, closure_text_size},
                TINYPY_MESSAGE_PART_LITERAL(" free vars, not "), {free_buffer, free_text_size}
            };

            tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_VALUE, parts, 5U, out_error);
            return TINYPY_FALSE;
        }
        __tinypy_internal_c_descriptor_replace(&function->code, value);
        return TINYPY_TRUE;
    case TINYPY_INTERNAL_C_DESCRIPTOR_FUNCTION_DEFAULTS:
        if (value != NULL && TINYPY_VALUE_KIND(value) == TINYPY_VALUE_NONE) {
            value = NULL;
        }
        if (value != NULL && TINYPY_VALUE_KIND(value) != TINYPY_VALUE_TUPLE) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "__defaults__ must be set to a tuple object", out_error);
            return TINYPY_FALSE;
        }
        __tinypy_internal_c_descriptor_replace(&function->defaults, value);
        return TINYPY_TRUE;
    case TINYPY_INTERNAL_C_DESCRIPTOR_FUNCTION_NAME:
        if (value == NULL || TINYPY_VALUE_KIND(value) != TINYPY_VALUE_STRING) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "__name__ must be set to a string object", out_error);
            return TINYPY_FALSE;
        }
        __tinypy_internal_c_descriptor_replace(&function->name, value);
        return TINYPY_TRUE;
    case TINYPY_INTERNAL_C_DESCRIPTOR_FUNCTION_DOC:
        if (value == NULL) {
            value = TINYPY_RET_NONE(vm);
        }
        else {
            TINYPY_INCREF(value);
        }
        __tinypy_internal_c_descriptor_replace(&function->doc, value);
        TINYPY_DECREF(value);
        return TINYPY_TRUE;
    case TINYPY_INTERNAL_C_DESCRIPTOR_FUNCTION_DICT:
        if (value == NULL) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "function's dictionary may not be deleted", out_error);
            return TINYPY_FALSE;
        }
        if (TINYPY_VALUE_KIND(value) != TINYPY_VALUE_DICT) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "setting function's dictionary to a non-dict", out_error);
            return TINYPY_FALSE;
        }
        __tinypy_internal_c_descriptor_replace(&function->dict, value);
        return TINYPY_TRUE;
    case TINYPY_INTERNAL_C_DESCRIPTOR_FUNCTION_MODULE:
        if (value == NULL) {
            value = TINYPY_RET_NONE(vm);
        }
        else {
            TINYPY_INCREF(value);
        }
        __tinypy_internal_c_descriptor_replace(&function->module, value);
        TINYPY_DECREF(value);
        return TINYPY_TRUE;
    case TINYPY_INTERNAL_C_DESCRIPTOR_FUNCTION_GLOBALS:
    case TINYPY_INTERNAL_C_DESCRIPTOR_FUNCTION_CLOSURE:
        __tinypy_internal_c_descriptor_readonly(vm, out_error);
        return TINYPY_FALSE;
    default:
        return TINYPY_FALSE;
    }
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_internal_function_descriptor_set(tinypy_vm_t *vm, tinypy_value_type_e kind, tinypy_value_t *name, tinypy_internal_c_descriptor_field_e field, tinypy_bool_t writable) {
    tinypy_value_t *descriptor = __tinypy_internal_c_descriptor_new(vm, kind, &vm->types[TINYPY_VALUE_FUNCTION], name, field, writable);

    tinypy_type_set_attr_key(&vm->types[TINYPY_VALUE_FUNCTION], TINYPY_C_DESCRIPTOR_OBJECT(descriptor)->name, descriptor);
    TINYPY_DECREF(descriptor);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_internal_builtin_descriptor_set(tinypy_vm_t *vm, tinypy_type_t *owner, tinypy_value_type_e kind, tinypy_value_t *name, tinypy_internal_c_descriptor_field_e field, tinypy_bool_t writable) {
    tinypy_value_t *descriptor = __tinypy_internal_c_descriptor_new(vm, kind, owner, name, field, writable);

    tinypy_type_set_attr_key(owner, TINYPY_C_DESCRIPTOR_OBJECT(descriptor)->name, descriptor);
    TINYPY_DECREF(descriptor);
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_initialize_descriptor_types(tinypy_vm_t *vm) {
    tinypy_internal_type_add_method((&vm->types[TINYPY_VALUE_FUNCTION]), vm->internal_special_get_key, __tinypy_internal_descriptor_get_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_static_method((&vm->types[TINYPY_VALUE_METHOD]), vm->internal_special_new_key, __tinypy_internal_method_new_method, NULL, NULL);
    tinypy_internal_type_add_method((&vm->types[TINYPY_VALUE_METHOD]), vm->internal_special_get_key, __tinypy_internal_descriptor_get_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method((&vm->types[TINYPY_VALUE_METHOD]), vm->internal_special_call_key, __tinypy_internal_method_call_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method((&vm->types[TINYPY_VALUE_METHOD]), vm->internal_special_repr_key, __tinypy_internal_method_repr_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method((&vm->types[TINYPY_VALUE_METHOD]), vm->internal_special_hash_key, __tinypy_internal_method_hash_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method((&vm->types[TINYPY_VALUE_METHOD]), vm->internal_special_cmp_key, __tinypy_internal_method_cmp_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method((&vm->types[TINYPY_VALUE_PROPERTY]), vm->internal_special_get_key, __tinypy_internal_descriptor_get_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method((&vm->types[TINYPY_VALUE_STATIC_METHOD]), vm->internal_special_get_key, __tinypy_internal_descriptor_get_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method((&vm->types[TINYPY_VALUE_CLASS_METHOD]), vm->internal_special_get_key, __tinypy_internal_descriptor_get_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method((&vm->types[TINYPY_VALUE_PROPERTY]), vm->internal_special_init_key, __tinypy_internal_property_init_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method((&vm->types[TINYPY_VALUE_STATIC_METHOD]), vm->internal_special_init_key, __tinypy_internal_callable_descriptor_init_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method((&vm->types[TINYPY_VALUE_CLASS_METHOD]), vm->internal_special_init_key, __tinypy_internal_callable_descriptor_init_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method((&vm->types[TINYPY_VALUE_PROPERTY]), vm->internal_special_set_key, __tinypy_internal_property_set_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method((&vm->types[TINYPY_VALUE_PROPERTY]), vm->internal_special_delete_key, __tinypy_internal_property_delete_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method((&vm->types[TINYPY_VALUE_GETSET_DESCRIPTOR]), vm->internal_special_get_key, __tinypy_internal_descriptor_get_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method((&vm->types[TINYPY_VALUE_GETSET_DESCRIPTOR]), vm->internal_special_set_key, __tinypy_internal_c_descriptor_set_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method((&vm->types[TINYPY_VALUE_GETSET_DESCRIPTOR]), vm->internal_special_delete_key, __tinypy_internal_c_descriptor_delete_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method((&vm->types[TINYPY_VALUE_GETSET_DESCRIPTOR]), vm->internal_special_repr_key, __tinypy_internal_c_descriptor_repr_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method((&vm->types[TINYPY_VALUE_MEMBER_DESCRIPTOR]), vm->internal_special_get_key, __tinypy_internal_descriptor_get_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method((&vm->types[TINYPY_VALUE_MEMBER_DESCRIPTOR]), vm->internal_special_set_key, __tinypy_internal_c_descriptor_set_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method((&vm->types[TINYPY_VALUE_MEMBER_DESCRIPTOR]), vm->internal_special_delete_key, __tinypy_internal_c_descriptor_delete_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method((&vm->types[TINYPY_VALUE_MEMBER_DESCRIPTOR]), vm->internal_special_repr_key, __tinypy_internal_c_descriptor_repr_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_type_t *descriptor_types[] = {&vm->types[TINYPY_VALUE_GETSET_DESCRIPTOR], &vm->types[TINYPY_VALUE_MEMBER_DESCRIPTOR]};
    for (size_t index = 0U; index < sizeof(descriptor_types) / sizeof(descriptor_types[0]); ++index) {
        __tinypy_internal_builtin_descriptor_set(vm, descriptor_types[index], TINYPY_VALUE_MEMBER_DESCRIPTOR, vm->internal_special_name_key, TINYPY_INTERNAL_C_DESCRIPTOR_METADATA_NAME, TINYPY_FALSE);
        __tinypy_internal_builtin_descriptor_set(vm, descriptor_types[index], TINYPY_VALUE_MEMBER_DESCRIPTOR, vm->internal_special_objclass_key, TINYPY_INTERNAL_C_DESCRIPTOR_METADATA_OWNER, TINYPY_FALSE);
        __tinypy_internal_builtin_descriptor_set(vm, descriptor_types[index], TINYPY_VALUE_GETSET_DESCRIPTOR, vm->internal_special_doc_key, TINYPY_INTERNAL_C_DESCRIPTOR_METADATA_DOC, TINYPY_FALSE);
    }
    tinypy_internal_type_add_method(&vm->types[TINYPY_VALUE_PROPERTY], vm->internal_getter_key, __tinypy_internal_property_getter_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(&vm->types[TINYPY_VALUE_PROPERTY], vm->internal_setter_key, __tinypy_internal_property_setter_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    tinypy_internal_type_add_method(&vm->types[TINYPY_VALUE_PROPERTY], vm->internal_deleter_key, __tinypy_internal_property_deleter_method, NULL, NULL, TINYPY_NATIVE_DESCRIPTOR_AUTO);
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_PROPERTY], TINYPY_VALUE_MEMBER_DESCRIPTOR, vm->internal_fget_key, TINYPY_INTERNAL_C_DESCRIPTOR_PROPERTY_GETTER, TINYPY_FALSE);
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_PROPERTY], TINYPY_VALUE_MEMBER_DESCRIPTOR, vm->internal_fset_key, TINYPY_INTERNAL_C_DESCRIPTOR_PROPERTY_SETTER, TINYPY_FALSE);
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_PROPERTY], TINYPY_VALUE_MEMBER_DESCRIPTOR, vm->internal_fdel_key, TINYPY_INTERNAL_C_DESCRIPTOR_PROPERTY_DELETER, TINYPY_FALSE);
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_PROPERTY], TINYPY_VALUE_MEMBER_DESCRIPTOR, vm->internal_special_doc_key, TINYPY_INTERNAL_C_DESCRIPTOR_PROPERTY_DOC, TINYPY_FALSE);
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_TYPE], TINYPY_VALUE_GETSET_DESCRIPTOR, vm->internal_special_abstractmethods_key, TINYPY_INTERNAL_C_DESCRIPTOR_TYPE_ABSTRACT_METHODS, TINYPY_TRUE);
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_TYPE], TINYPY_VALUE_MEMBER_DESCRIPTOR, vm->internal_special_base_key, TINYPY_INTERNAL_C_DESCRIPTOR_TYPE_BASE, TINYPY_FALSE);
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_TYPE], TINYPY_VALUE_GETSET_DESCRIPTOR, vm->internal_special_bases_key, TINYPY_INTERNAL_C_DESCRIPTOR_TYPE_BASES, TINYPY_TRUE);
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_TYPE], TINYPY_VALUE_MEMBER_DESCRIPTOR, vm->internal_special_basicsize_key, TINYPY_INTERNAL_C_DESCRIPTOR_TYPE_BASIC_SIZE, TINYPY_FALSE);
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_TYPE], TINYPY_VALUE_GETSET_DESCRIPTOR, vm->internal_special_dict_key, TINYPY_INTERNAL_C_DESCRIPTOR_TYPE_DICT, TINYPY_FALSE);
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_TYPE], TINYPY_VALUE_GETSET_DESCRIPTOR, vm->internal_special_doc_key, TINYPY_INTERNAL_C_DESCRIPTOR_TYPE_DOC, TINYPY_FALSE);
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_TYPE], TINYPY_VALUE_MEMBER_DESCRIPTOR, vm->internal_special_dictoffset_key, TINYPY_INTERNAL_C_DESCRIPTOR_TYPE_DICT_OFFSET, TINYPY_FALSE);
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_TYPE], TINYPY_VALUE_MEMBER_DESCRIPTOR, vm->internal_special_flags_key, TINYPY_INTERNAL_C_DESCRIPTOR_TYPE_FLAGS, TINYPY_FALSE);
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_TYPE], TINYPY_VALUE_MEMBER_DESCRIPTOR, vm->internal_special_itemsize_key, TINYPY_INTERNAL_C_DESCRIPTOR_TYPE_ITEM_SIZE, TINYPY_FALSE);
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_TYPE], TINYPY_VALUE_GETSET_DESCRIPTOR, vm->internal_special_module_key, TINYPY_INTERNAL_C_DESCRIPTOR_TYPE_MODULE, TINYPY_TRUE);
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_TYPE], TINYPY_VALUE_MEMBER_DESCRIPTOR, vm->internal_special_mro_key, TINYPY_INTERNAL_C_DESCRIPTOR_TYPE_MRO, TINYPY_FALSE);
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_TYPE], TINYPY_VALUE_GETSET_DESCRIPTOR, vm->internal_special_name_key, TINYPY_INTERNAL_C_DESCRIPTOR_TYPE_NAME, TINYPY_TRUE);
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_TYPE], TINYPY_VALUE_MEMBER_DESCRIPTOR, vm->internal_special_weakrefoffset_key, TINYPY_INTERNAL_C_DESCRIPTOR_TYPE_WEAKREF_OFFSET, TINYPY_FALSE);
    __tinypy_internal_function_descriptor_set(vm, TINYPY_VALUE_GETSET_DESCRIPTOR, vm->internal_func_code_key, TINYPY_INTERNAL_C_DESCRIPTOR_FUNCTION_CODE, INT32_C(1));
    __tinypy_internal_function_descriptor_set(vm, TINYPY_VALUE_MEMBER_DESCRIPTOR, vm->internal_func_globals_key, TINYPY_INTERNAL_C_DESCRIPTOR_FUNCTION_GLOBALS, INT32_C(0));
    __tinypy_internal_function_descriptor_set(vm, TINYPY_VALUE_GETSET_DESCRIPTOR, vm->internal_func_defaults_key, TINYPY_INTERNAL_C_DESCRIPTOR_FUNCTION_DEFAULTS, INT32_C(1));
    __tinypy_internal_function_descriptor_set(vm, TINYPY_VALUE_GETSET_DESCRIPTOR, vm->internal_func_closure_key, TINYPY_INTERNAL_C_DESCRIPTOR_FUNCTION_CLOSURE, INT32_C(0));
    __tinypy_internal_function_descriptor_set(vm, TINYPY_VALUE_GETSET_DESCRIPTOR, vm->internal_func_name_key, TINYPY_INTERNAL_C_DESCRIPTOR_FUNCTION_NAME, INT32_C(1));
    __tinypy_internal_function_descriptor_set(vm, TINYPY_VALUE_GETSET_DESCRIPTOR, vm->internal_func_doc_key, TINYPY_INTERNAL_C_DESCRIPTOR_FUNCTION_DOC, INT32_C(1));
    __tinypy_internal_function_descriptor_set(vm, TINYPY_VALUE_GETSET_DESCRIPTOR, vm->internal_func_dict_key, TINYPY_INTERNAL_C_DESCRIPTOR_FUNCTION_DICT, INT32_C(1));
    __tinypy_internal_function_descriptor_set(vm, TINYPY_VALUE_GETSET_DESCRIPTOR, vm->internal_special_module_key, TINYPY_INTERNAL_C_DESCRIPTOR_FUNCTION_MODULE, INT32_C(1));
    __tinypy_internal_function_descriptor_set(vm, TINYPY_VALUE_GETSET_DESCRIPTOR, vm->internal_special_code_key, TINYPY_INTERNAL_C_DESCRIPTOR_FUNCTION_CODE, INT32_C(1));
    __tinypy_internal_function_descriptor_set(vm, TINYPY_VALUE_MEMBER_DESCRIPTOR, vm->internal_special_globals_key, TINYPY_INTERNAL_C_DESCRIPTOR_FUNCTION_GLOBALS, INT32_C(0));
    __tinypy_internal_function_descriptor_set(vm, TINYPY_VALUE_GETSET_DESCRIPTOR, vm->internal_special_defaults_key, TINYPY_INTERNAL_C_DESCRIPTOR_FUNCTION_DEFAULTS, INT32_C(1));
    __tinypy_internal_function_descriptor_set(vm, TINYPY_VALUE_GETSET_DESCRIPTOR, vm->internal_special_closure_key, TINYPY_INTERNAL_C_DESCRIPTOR_FUNCTION_CLOSURE, INT32_C(0));
    __tinypy_internal_function_descriptor_set(vm, TINYPY_VALUE_GETSET_DESCRIPTOR, vm->internal_special_name_key, TINYPY_INTERNAL_C_DESCRIPTOR_FUNCTION_NAME, INT32_C(1));
    __tinypy_internal_function_descriptor_set(vm, TINYPY_VALUE_GETSET_DESCRIPTOR, vm->internal_special_doc_key, TINYPY_INTERNAL_C_DESCRIPTOR_FUNCTION_DOC, INT32_C(1));
    __tinypy_internal_function_descriptor_set(vm, TINYPY_VALUE_GETSET_DESCRIPTOR, vm->internal_special_dict_key, TINYPY_INTERNAL_C_DESCRIPTOR_FUNCTION_DICT, INT32_C(1));
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_METHOD], TINYPY_VALUE_MEMBER_DESCRIPTOR, vm->internal_im_func_key, TINYPY_INTERNAL_C_DESCRIPTOR_METHOD_FUNCTION, INT32_C(0));
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_METHOD], TINYPY_VALUE_MEMBER_DESCRIPTOR, vm->internal_im_self_key, TINYPY_INTERNAL_C_DESCRIPTOR_METHOD_SELF, INT32_C(0));
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_METHOD], TINYPY_VALUE_MEMBER_DESCRIPTOR, vm->internal_im_class_key, TINYPY_INTERNAL_C_DESCRIPTOR_METHOD_OWNER, INT32_C(0));
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_METHOD], TINYPY_VALUE_MEMBER_DESCRIPTOR, vm->internal_special_func_key, TINYPY_INTERNAL_C_DESCRIPTOR_METHOD_FUNCTION, INT32_C(0));
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_METHOD], TINYPY_VALUE_MEMBER_DESCRIPTOR, vm->internal_special_self_key, TINYPY_INTERNAL_C_DESCRIPTOR_METHOD_SELF, INT32_C(0));
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_METHOD], TINYPY_VALUE_GETSET_DESCRIPTOR, vm->internal_special_doc_key, TINYPY_INTERNAL_C_DESCRIPTOR_METHOD_DOC, TINYPY_FALSE);
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_STATIC_METHOD], TINYPY_VALUE_MEMBER_DESCRIPTOR, vm->internal_special_func_key, TINYPY_INTERNAL_C_DESCRIPTOR_CALLABLE_FUNCTION, INT32_C(0));
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_CLASS_METHOD], TINYPY_VALUE_MEMBER_DESCRIPTOR, vm->internal_special_func_key, TINYPY_INTERNAL_C_DESCRIPTOR_CALLABLE_FUNCTION, INT32_C(0));
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_CODE], TINYPY_VALUE_MEMBER_DESCRIPTOR, vm->internal_co_argcount_key, TINYPY_INTERNAL_C_DESCRIPTOR_CODE_ARG_COUNT, INT32_C(0));
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_CODE], TINYPY_VALUE_MEMBER_DESCRIPTOR, vm->internal_co_nlocals_key, TINYPY_INTERNAL_C_DESCRIPTOR_CODE_LOCAL_COUNT, INT32_C(0));
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_CODE], TINYPY_VALUE_MEMBER_DESCRIPTOR, vm->internal_co_stacksize_key, TINYPY_INTERNAL_C_DESCRIPTOR_CODE_STACK_SIZE, INT32_C(0));
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_CODE], TINYPY_VALUE_MEMBER_DESCRIPTOR, vm->internal_co_flags_key, TINYPY_INTERNAL_C_DESCRIPTOR_CODE_FLAGS, INT32_C(0));
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_CODE], TINYPY_VALUE_MEMBER_DESCRIPTOR, vm->internal_co_code_key, TINYPY_INTERNAL_C_DESCRIPTOR_CODE_BYTECODE, INT32_C(0));
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_CODE], TINYPY_VALUE_MEMBER_DESCRIPTOR, vm->internal_co_consts_key, TINYPY_INTERNAL_C_DESCRIPTOR_CODE_CONSTS, INT32_C(0));
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_CODE], TINYPY_VALUE_MEMBER_DESCRIPTOR, vm->internal_co_names_key, TINYPY_INTERNAL_C_DESCRIPTOR_CODE_NAMES, INT32_C(0));
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_CODE], TINYPY_VALUE_MEMBER_DESCRIPTOR, vm->internal_co_varnames_key, TINYPY_INTERNAL_C_DESCRIPTOR_CODE_VARNAMES, INT32_C(0));
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_CODE], TINYPY_VALUE_MEMBER_DESCRIPTOR, vm->internal_co_freevars_key, TINYPY_INTERNAL_C_DESCRIPTOR_CODE_FREEVARS, INT32_C(0));
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_CODE], TINYPY_VALUE_MEMBER_DESCRIPTOR, vm->internal_co_cellvars_key, TINYPY_INTERNAL_C_DESCRIPTOR_CODE_CELLVARS, INT32_C(0));
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_CODE], TINYPY_VALUE_MEMBER_DESCRIPTOR, vm->internal_co_filename_key, TINYPY_INTERNAL_C_DESCRIPTOR_CODE_FILENAME, INT32_C(0));
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_CODE], TINYPY_VALUE_MEMBER_DESCRIPTOR, vm->internal_co_name_key, TINYPY_INTERNAL_C_DESCRIPTOR_CODE_NAME, INT32_C(0));
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_CODE], TINYPY_VALUE_MEMBER_DESCRIPTOR, vm->internal_co_firstlineno_key, TINYPY_INTERNAL_C_DESCRIPTOR_CODE_FIRST_LINE, INT32_C(0));
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_CODE], TINYPY_VALUE_MEMBER_DESCRIPTOR, vm->internal_co_lnotab_key, TINYPY_INTERNAL_C_DESCRIPTOR_CODE_LNOTAB, INT32_C(0));
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_CELL], TINYPY_VALUE_GETSET_DESCRIPTOR, vm->internal_cell_contents_key, TINYPY_INTERNAL_C_DESCRIPTOR_CELL_CONTENT, INT32_C(0));
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_MODULE], TINYPY_VALUE_MEMBER_DESCRIPTOR, vm->internal_special_dict_key, TINYPY_INTERNAL_C_DESCRIPTOR_MODULE_DICT, INT32_C(0));
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_SUPER], TINYPY_VALUE_MEMBER_DESCRIPTOR, vm->internal_special_thisclass_key, TINYPY_INTERNAL_C_DESCRIPTOR_SUPER_THISCLASS, INT32_C(0));
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_SUPER], TINYPY_VALUE_MEMBER_DESCRIPTOR, vm->internal_special_self_key, TINYPY_INTERNAL_C_DESCRIPTOR_SUPER_SELF, INT32_C(0));
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_SUPER], TINYPY_VALUE_MEMBER_DESCRIPTOR, vm->internal_special_self_class_key, TINYPY_INTERNAL_C_DESCRIPTOR_SUPER_SELF_CLASS, INT32_C(0));
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_PARTIAL], TINYPY_VALUE_MEMBER_DESCRIPTOR, vm->internal_func_key, TINYPY_INTERNAL_C_DESCRIPTOR_PARTIAL_FUNCTION, INT32_C(0));
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_PARTIAL], TINYPY_VALUE_MEMBER_DESCRIPTOR, vm->internal_args_key, TINYPY_INTERNAL_C_DESCRIPTOR_PARTIAL_ARGS, INT32_C(0));
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_PARTIAL], TINYPY_VALUE_MEMBER_DESCRIPTOR, vm->internal_keywords_key, TINYPY_INTERNAL_C_DESCRIPTOR_PARTIAL_KEYWORDS, INT32_C(0));
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_FRAME], TINYPY_VALUE_GETSET_DESCRIPTOR, vm->internal_f_back_key, TINYPY_INTERNAL_C_DESCRIPTOR_FRAME_BACK, INT32_C(0));
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_FRAME], TINYPY_VALUE_GETSET_DESCRIPTOR, vm->internal_f_code_key, TINYPY_INTERNAL_C_DESCRIPTOR_FRAME_CODE, INT32_C(0));
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_FRAME], TINYPY_VALUE_GETSET_DESCRIPTOR, vm->internal_f_builtins_key, TINYPY_INTERNAL_C_DESCRIPTOR_FRAME_BUILTINS, INT32_C(0));
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_FRAME], TINYPY_VALUE_GETSET_DESCRIPTOR, vm->internal_f_globals_key, TINYPY_INTERNAL_C_DESCRIPTOR_FRAME_GLOBALS, INT32_C(0));
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_FRAME], TINYPY_VALUE_GETSET_DESCRIPTOR, vm->internal_f_locals_key, TINYPY_INTERNAL_C_DESCRIPTOR_FRAME_LOCALS, INT32_C(0));
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_FRAME], TINYPY_VALUE_GETSET_DESCRIPTOR, vm->internal_f_lasti_key, TINYPY_INTERNAL_C_DESCRIPTOR_FRAME_LAST_INSTRUCTION, INT32_C(0));
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_FRAME], TINYPY_VALUE_GETSET_DESCRIPTOR, vm->internal_f_lineno_key, TINYPY_INTERNAL_C_DESCRIPTOR_FRAME_LINE_NUMBER, INT32_C(0));
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_FRAME], TINYPY_VALUE_GETSET_DESCRIPTOR, vm->internal_f_trace_key, TINYPY_INTERNAL_C_DESCRIPTOR_FRAME_TRACE, INT32_C(1));
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_FRAME], TINYPY_VALUE_GETSET_DESCRIPTOR, vm->internal_f_exc_type_key, TINYPY_INTERNAL_C_DESCRIPTOR_FRAME_EXCEPTION_TYPE, INT32_C(1));
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_FRAME], TINYPY_VALUE_GETSET_DESCRIPTOR, vm->internal_f_exc_value_key, TINYPY_INTERNAL_C_DESCRIPTOR_FRAME_EXCEPTION_VALUE, INT32_C(1));
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_FRAME], TINYPY_VALUE_GETSET_DESCRIPTOR, vm->internal_f_exc_traceback_key, TINYPY_INTERNAL_C_DESCRIPTOR_FRAME_EXCEPTION_TRACEBACK, INT32_C(1));
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_FRAME], TINYPY_VALUE_GETSET_DESCRIPTOR, vm->internal_f_restricted_key, TINYPY_INTERNAL_C_DESCRIPTOR_FRAME_RESTRICTED, INT32_C(0));
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_TRACEBACK], TINYPY_VALUE_MEMBER_DESCRIPTOR, vm->internal_tb_next_key, TINYPY_INTERNAL_C_DESCRIPTOR_TRACEBACK_NEXT, INT32_C(0));
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_TRACEBACK], TINYPY_VALUE_MEMBER_DESCRIPTOR, vm->internal_tb_frame_key, TINYPY_INTERNAL_C_DESCRIPTOR_TRACEBACK_FRAME, INT32_C(0));
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_TRACEBACK], TINYPY_VALUE_MEMBER_DESCRIPTOR, vm->internal_tb_lasti_key, TINYPY_INTERNAL_C_DESCRIPTOR_TRACEBACK_LAST_INSTRUCTION, INT32_C(0));
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_TRACEBACK], TINYPY_VALUE_MEMBER_DESCRIPTOR, vm->internal_tb_lineno_key, TINYPY_INTERNAL_C_DESCRIPTOR_TRACEBACK_LINE_NUMBER, INT32_C(0));
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_GENERATOR], TINYPY_VALUE_GETSET_DESCRIPTOR, vm->internal_special_name_key, TINYPY_INTERNAL_C_DESCRIPTOR_GENERATOR_NAME, INT32_C(0));
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_GENERATOR], TINYPY_VALUE_MEMBER_DESCRIPTOR, vm->internal_gi_code_key, TINYPY_INTERNAL_C_DESCRIPTOR_GENERATOR_CODE, INT32_C(0));
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_GENERATOR], TINYPY_VALUE_MEMBER_DESCRIPTOR, vm->internal_gi_frame_key, TINYPY_INTERNAL_C_DESCRIPTOR_GENERATOR_FRAME, INT32_C(0));
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_GENERATOR], TINYPY_VALUE_MEMBER_DESCRIPTOR, vm->internal_gi_running_key, TINYPY_INTERNAL_C_DESCRIPTOR_GENERATOR_RUNNING, INT32_C(0));
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_initialize_native_function_descriptors(tinypy_vm_t *vm) {
    tinypy_type_t *function_type = &vm->types[TINYPY_VALUE_NATIVE_FUNCTION];
    tinypy_type_t *wrapper_type = vm->native_method_wrapper_type;
    tinypy_type_t *descriptor_types[] = {vm->native_method_descriptor_type, vm->native_wrapper_descriptor_type, vm->native_class_method_descriptor_type};

    __tinypy_internal_builtin_descriptor_set(vm, function_type, TINYPY_VALUE_GETSET_DESCRIPTOR, vm->internal_special_name_key, TINYPY_INTERNAL_C_DESCRIPTOR_NATIVE_NAME, TINYPY_FALSE);
    __tinypy_internal_builtin_descriptor_set(vm, function_type, TINYPY_VALUE_GETSET_DESCRIPTOR, vm->internal_special_self_key, TINYPY_INTERNAL_C_DESCRIPTOR_NATIVE_SELF, TINYPY_FALSE);
    __tinypy_internal_builtin_descriptor_set(vm, function_type, TINYPY_VALUE_GETSET_DESCRIPTOR, vm->internal_special_doc_key, TINYPY_INTERNAL_C_DESCRIPTOR_NATIVE_DOC, TINYPY_FALSE);
    __tinypy_internal_builtin_descriptor_set(vm, function_type, TINYPY_VALUE_MEMBER_DESCRIPTOR, vm->internal_special_module_key, TINYPY_INTERNAL_C_DESCRIPTOR_NATIVE_MODULE, TINYPY_TRUE);
    __tinypy_internal_builtin_descriptor_set(vm, wrapper_type, TINYPY_VALUE_GETSET_DESCRIPTOR, vm->internal_special_name_key, TINYPY_INTERNAL_C_DESCRIPTOR_NATIVE_NAME, TINYPY_FALSE);
    __tinypy_internal_builtin_descriptor_set(vm, wrapper_type, TINYPY_VALUE_MEMBER_DESCRIPTOR, vm->internal_special_self_key, TINYPY_INTERNAL_C_DESCRIPTOR_NATIVE_SELF, TINYPY_FALSE);
    __tinypy_internal_builtin_descriptor_set(vm, wrapper_type, TINYPY_VALUE_GETSET_DESCRIPTOR, vm->internal_special_objclass_key, TINYPY_INTERNAL_C_DESCRIPTOR_NATIVE_OWNER, TINYPY_FALSE);
    __tinypy_internal_builtin_descriptor_set(vm, wrapper_type, TINYPY_VALUE_GETSET_DESCRIPTOR, vm->internal_special_doc_key, TINYPY_INTERNAL_C_DESCRIPTOR_NATIVE_DOC, TINYPY_FALSE);
    for (size_t index = 0U; index < sizeof(descriptor_types) / sizeof(descriptor_types[0]); ++index) {
        __tinypy_internal_builtin_descriptor_set(vm, descriptor_types[index], TINYPY_VALUE_MEMBER_DESCRIPTOR, vm->internal_special_name_key, TINYPY_INTERNAL_C_DESCRIPTOR_NATIVE_NAME, TINYPY_FALSE);
        __tinypy_internal_builtin_descriptor_set(vm, descriptor_types[index], TINYPY_VALUE_MEMBER_DESCRIPTOR, vm->internal_special_objclass_key, TINYPY_INTERNAL_C_DESCRIPTOR_NATIVE_OWNER, TINYPY_FALSE);
        __tinypy_internal_builtin_descriptor_set(vm, descriptor_types[index], TINYPY_VALUE_GETSET_DESCRIPTOR, vm->internal_special_doc_key, TINYPY_INTERNAL_C_DESCRIPTOR_NATIVE_DOC, TINYPY_FALSE);
    }
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_initialize_struct_descriptors(tinypy_type_t *type) {
    tinypy_vm_t *vm = type->vm;

    __tinypy_internal_builtin_descriptor_set(vm, type, TINYPY_VALUE_GETSET_DESCRIPTOR, vm->internal_format_key, TINYPY_INTERNAL_C_DESCRIPTOR_STRUCT_FORMAT, TINYPY_FALSE);
    __tinypy_internal_builtin_descriptor_set(vm, type, TINYPY_VALUE_GETSET_DESCRIPTOR, vm->internal_size_key, TINYPY_INTERNAL_C_DESCRIPTOR_STRUCT_SIZE, TINYPY_FALSE);
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_initialize_numeric_descriptors(tinypy_vm_t *vm) {
    tinypy_value_t *names[] = {vm->internal_real_key, vm->internal_imag_key, vm->internal_numerator_key, vm->internal_denominator_key};
    tinypy_type_t *types[] = {&vm->types[TINYPY_VALUE_INTEGER], &vm->types[TINYPY_VALUE_LONG], &vm->types[TINYPY_VALUE_FLOAT], &vm->types[TINYPY_VALUE_COMPLEX]};

    for (size_t index = 0U; index < sizeof(types) / sizeof(types[0]); ++index) {
        tinypy_value_type_e kind = index == 3U ? TINYPY_VALUE_MEMBER_DESCRIPTOR : TINYPY_VALUE_GETSET_DESCRIPTOR;
        size_t count = index < 2U ? 4U : 2U;

        for (size_t field = 0U; field < count; ++field) {
            __tinypy_internal_builtin_descriptor_set(vm, types[index], kind, names[field], (tinypy_internal_c_descriptor_field_e)((int32_t)TINYPY_INTERNAL_C_DESCRIPTOR_NUMERIC_REAL + (int32_t)field), TINYPY_FALSE);
        }
    }
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_initialize_exception_descriptors(tinypy_type_t *type) {
    if (type->native_payload_size == sizeof(tinypy_internal_unicode_error_payload_t)) {
        tinypy_vm_t *vm = type->vm;
        tinypy_value_t *const names[5] = {vm->internal_encoding_key, vm->internal_object_key, vm->internal_reason_key, vm->internal_start_key, vm->internal_end_key};
        for (size_t index = 0U; index < 5U; ++index) {
            tinypy_value_t *descriptor = __tinypy_internal_c_descriptor_new_with_owner(vm, TINYPY_VALUE_MEMBER_DESCRIPTOR, type, names[index], (tinypy_internal_c_descriptor_field_e)((size_t)TINYPY_INTERNAL_C_DESCRIPTOR_UNICODE_ENCODING + index), TINYPY_TRUE, TINYPY_FALSE);

            tinypy_type_set_attr_key(type, names[index], descriptor);
            TINYPY_DECREF(descriptor);
        }
        return;
    }
    tinypy_value_t *args = __tinypy_internal_c_descriptor_new_with_owner(type->vm, TINYPY_VALUE_GETSET_DESCRIPTOR, type, type->vm->internal_args_key, TINYPY_INTERNAL_C_DESCRIPTOR_EXCEPTION_ARGS, TINYPY_TRUE, TINYPY_FALSE);
    tinypy_value_t *message = __tinypy_internal_c_descriptor_new_with_owner(type->vm, TINYPY_VALUE_GETSET_DESCRIPTOR, type, type->vm->internal_message_key, TINYPY_INTERNAL_C_DESCRIPTOR_EXCEPTION_MESSAGE, TINYPY_TRUE, TINYPY_FALSE);

    tinypy_type_set_attr_key(type, type->vm->internal_args_key, args);
    tinypy_type_set_attr_key(type, type->vm->internal_message_key, message);
    TINYPY_DECREF(message);
    TINYPY_DECREF(args);
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_property_getter(const tinypy_value_t *property) {
    tinypy_value_t *return_value_1 = TINYPY_PROPERTY_OBJECT((tinypy_value_t *)property)->getter;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_property_setter(const tinypy_value_t *property) {
    tinypy_value_t *return_value_1 = TINYPY_PROPERTY_OBJECT((tinypy_value_t *)property)->setter;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_property_deleter(const tinypy_value_t *property) {
    tinypy_value_t *return_value_1 = TINYPY_PROPERTY_OBJECT((tinypy_value_t *)property)->deleter;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_property_doc(const tinypy_value_t *property) {
    tinypy_value_t *return_value_1 = TINYPY_PROPERTY_OBJECT((tinypy_value_t *)property)->doc;
    return return_value_1;
}
