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
    TINYPY_INTERNAL_C_DESCRIPTOR_TYPE_DOC = 72
} tinypy_internal_c_descriptor_field_e;

//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_c_descriptor_new_with_owner(tinypy_vm_t *vm, tinypy_value_type_e kind, tinypy_type_t *owner, const char *name, size_t name_size, tinypy_internal_c_descriptor_field_e field, tinypy_bool_t writable, tinypy_bool_t retain_owner) {
    tinypy_c_descriptor_object_t *descriptor = (tinypy_c_descriptor_object_t *)tinypy_internal_value_allocate(vm, kind, sizeof(*descriptor));
    descriptor->owner = owner;
    descriptor->name = tinypy_string_from_bytes(vm, name, name_size);
    descriptor->field = (int32_t)field;
    descriptor->writable = writable;
    descriptor->owner_retained = retain_owner;
    if (retain_owner != 0) {
        TINYPY_INCREF(&owner->base.base);
    }
    return &descriptor->base;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_c_descriptor_new(tinypy_vm_t *vm, tinypy_value_type_e kind, tinypy_type_t *owner, const char *name, size_t name_size, tinypy_internal_c_descriptor_field_e field, tinypy_bool_t writable) {
    tinypy_value_t *return_value_1 = __tinypy_internal_c_descriptor_new_with_owner(vm, kind, owner, name, name_size, field, writable, TINYPY_TRUE);
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
    TINYPY_INCREF(name);
    return &descriptor->base;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_instance_dict_descriptor_new(tinypy_type_t *owner) {
    tinypy_value_t *return_value_1 = __tinypy_internal_c_descriptor_new_with_owner(owner->vm, TINYPY_VALUE_GETSET_DESCRIPTOR, owner, "__dict__", 8U, TINYPY_INTERNAL_C_DESCRIPTOR_INSTANCE_DICT, INT32_C(1), TINYPY_FALSE);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_instance_weakref_descriptor_new(tinypy_type_t *owner) {
    tinypy_value_t *return_value_1 = __tinypy_internal_c_descriptor_new_with_owner(owner->vm, TINYPY_VALUE_GETSET_DESCRIPTOR, owner, "__weakref__", 11U, TINYPY_INTERNAL_C_DESCRIPTOR_INSTANCE_WEAKREF, INT32_C(0), TINYPY_FALSE);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_c_descriptor_optional(tinypy_vm_t *vm, tinypy_value_t *value) {
    if (value == NULL) {
        tinypy_value_t *return_value_1 = tinypy_none_get(vm);
        return return_value_1;
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
    TINYPY_INCREF(callable);
    return callable;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_class_method_get(tinypy_value_t *descriptor, tinypy_value_t *instance, tinypy_type_t *owner, tinypy_error_t **out_error) {
    (void)instance;
    TINYPY_CLEAR_ERROR(out_error);
    tinypy_value_t *callable = TINYPY_CALLABLE_DESCRIPTOR_OBJECT(descriptor)->callable;
    if (callable == NULL) {
        tinypy_internal_make_vm_error(TINYPY_VALUE_VM(descriptor), TINYPY_ERROR_RUNTIME, "uninitialized classmethod object", out_error);
        return NULL;
    }
    /* A bound classmethod carries the class as self and the metaclass as its
       owner, so im_class matches Python 2.7. */
    tinypy_value_t *metaclass = &owner->base.base.type->base.base;
    tinypy_value_t *return_value_1 = tinypy_method_new(callable, &owner->base.base, metaclass);
    return return_value_1;
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
    key = tinypy_string_from_bytes(vm, "__doc__", 7U);
    found = tinypy_internal_object_get_optional_attr_key(getter, key, out_doc, out_error);
    TINYPY_DECREF(key);
    return found >= 0 ? TINYPY_TRUE : TINYPY_FALSE;
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
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_ATTRIBUTE, "unreadable property", out_error);
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
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_ATTRIBUTE, value != NULL ? "property has no setter" : "property has no deleter", out_error);
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

    if ((kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U) || TINYPY_TUPLE_SIZE(args) != 1U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "descriptor constructor requires one callable", out_error);
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
tinypy_value_t *tinypy_internal_property_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = type->vm;
    tinypy_value_t *values[4] = {NULL, NULL, NULL, NULL};
    tinypy_value_t *owned_doc = NULL;
    tinypy_bool_t getter_doc = TINYPY_FALSE;
    size_t count = TINYPY_TUPLE_SIZE(args);

    if (count > 4U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "property accepts at most four arguments", out_error);
        return NULL;
    }
    for (size_t index = 0U; index < count; ++index) {
        tinypy_value_t *value = TINYPY_TUPLE_GET(args, index);

        values[index] = TINYPY_VALUE_KIND(value) == TINYPY_VALUE_NONE ? NULL : value;
    }
    if (kwargs != NULL) {
        tinypy_dict_entry_t *iterator = TINYPY_DICT_ITERATOR_BEGIN(kwargs);
        tinypy_dict_entry_t *iterator_end = TINYPY_DICT_ITERATOR_END(kwargs);

        for (; iterator != iterator_end; ++iterator) {
            const uint8_t *key_bytes;
            size_t key_size;
            size_t parameter;

            if (!TINYPY_DICT_ENTRY_IS_ACTIVE(iterator)) {
                continue;
            }
            if (TINYPY_VALUE_KIND(iterator->key) != TINYPY_VALUE_STRING && TINYPY_VALUE_KIND(iterator->key) != TINYPY_VALUE_UNICODE) {
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "property received an unexpected keyword", out_error);
                return NULL;
            }
            key_bytes = TINYPY_TEXT_BYTES(iterator->key);
            key_size = TINYPY_TEXT_BYTE_SIZE(iterator->key);
            if (key_size == 4U && memcmp(key_bytes, "fget", 4U) == 0) {
                parameter = 0U;
            }
            else if (key_size == 4U && memcmp(key_bytes, "fset", 4U) == 0) {
                parameter = 1U;
            }
            else if (key_size == 4U && memcmp(key_bytes, "fdel", 4U) == 0) {
                parameter = 2U;
            }
            else if (key_size == 3U && memcmp(key_bytes, "doc", 3U) == 0) {
                parameter = 3U;
            }
            else {
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "property received an unexpected keyword", out_error);
                return NULL;
            }
            if (parameter < count) {
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "property received multiple values for an argument", out_error);
                return NULL;
            }
            values[parameter] = TINYPY_VALUE_KIND(iterator->value) == TINYPY_VALUE_NONE ? NULL : iterator->value;
        }
    }
    if (values[3] == NULL) {
        getter_doc = TINYPY_TRUE;
        if (values[0] != NULL && __tinypy_property_getter_doc(vm, values[0], &owned_doc, out_error) == 0) {
            return NULL;
        }
        values[3] = owned_doc;
    }
    tinypy_value_t *return_value_1 = __tinypy_property_new(vm, values[0], values[1], values[2], values[3], getter_doc);
    if (owned_doc != NULL) {
        TINYPY_DECREF(owned_doc);
    }
    return return_value_1;
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
    tinypy_value_t *result = tinypy_none_get(vm);
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
    tinypy_value_t *initialized = tinypy_internal_property_create(&vm->types[TINYPY_VALUE_PROPERTY], constructor_args, kwargs, out_error);

    TINYPY_DECREF(constructor_args);
    if (initialized == NULL) {
        return NULL;
    }
    tinypy_property_object_t *self_property = TINYPY_PROPERTY_OBJECT(self);
    tinypy_property_object_t *initialized_property = TINYPY_PROPERTY_OBJECT(initialized);
    tinypy_value_t *getter = self_property->getter;
    tinypy_value_t *setter = self_property->setter;
    tinypy_value_t *deleter = self_property->deleter;
    tinypy_value_t *doc = self_property->doc;
    tinypy_bool_t getter_doc = self_property->getter_doc;

    self_property->getter = initialized_property->getter;
    self_property->setter = initialized_property->setter;
    self_property->deleter = initialized_property->deleter;
    self_property->doc = initialized_property->doc;
    self_property->getter_doc = initialized_property->getter_doc;
    initialized_property->getter = getter;
    initialized_property->setter = setter;
    initialized_property->deleter = deleter;
    initialized_property->doc = doc;
    initialized_property->getter_doc = getter_doc;
    TINYPY_DECREF(initialized);
    tinypy_value_t *result = tinypy_none_get(vm);
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
    tinypy_value_t *getter = field == 0 ? replacement : property->getter;
    tinypy_value_t *setter = field == 1 ? replacement : property->setter;
    tinypy_value_t *deleter = field == 2 ? replacement : property->deleter;
    tinypy_value_t *doc = property->doc;
    tinypy_value_t *owned_doc = NULL;

    if (field == 0 && property->getter_doc != 0) {
        if (__tinypy_property_getter_doc(vm, getter, &owned_doc, out_error) == 0) {
            return NULL;
        }
        doc = owned_doc;
    }
    tinypy_value_t *constructor_items[4] = {
        getter != NULL ? getter : &vm->none_object.base,
        setter != NULL ? setter : &vm->none_object.base,
        deleter != NULL ? deleter : &vm->none_object.base,
        doc != NULL ? doc : &vm->none_object.base};
    tinypy_value_t *constructor_args = tinypy_tuple_from_items(vm, constructor_items, 4U);
    tinypy_value_t *return_value_1 = tinypy_call(&property->base.type->base.base, constructor_args, NULL, out_error);

    TINYPY_DECREF(constructor_args);
    if (owned_doc != NULL) {
        TINYPY_DECREF(owned_doc);
    }
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
static tinypy_value_t *__tinypy_internal_property_field(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, int32_t field, tinypy_error_t **out_error) {
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
        tinypy_value_t *return_value_1 = tinypy_none_get(vm);
        return return_value_1;
    }
    TINYPY_INCREF(result);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_property_fget(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    (void)user_data;
    tinypy_value_t *return_value_1 = __tinypy_internal_property_field(function, args, kwargs, 0, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_property_fset(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    (void)user_data;
    tinypy_value_t *return_value_1 = __tinypy_internal_property_field(function, args, kwargs, 1, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_property_fdel(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    (void)user_data;
    tinypy_value_t *return_value_1 = __tinypy_internal_property_field(function, args, kwargs, 2, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_property_doc_value(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    (void)user_data;
    tinypy_value_t *return_value_1 = __tinypy_internal_property_field(function, args, kwargs, 3, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_internal_type_dict_set(tinypy_vm_t *vm, tinypy_type_t *type, const char *name, size_t name_size, tinypy_value_t *value) {
    (void)vm;
    tinypy_type_set_attr(type, name, name_size, value);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_internal_property_method_set(tinypy_vm_t *vm, const char *name, size_t name_size, tinypy_native_function_callback_t callback) {
    tinypy_value_t *function = tinypy_native_function_new(vm, name, name_size, callback, NULL, NULL);

    __tinypy_internal_type_dict_set(vm, &vm->types[TINYPY_VALUE_PROPERTY], name, name_size, function);
    TINYPY_DECREF(function);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_internal_descriptor_method_set(tinypy_vm_t *vm, tinypy_type_t *type, const char *name, size_t name_size, tinypy_native_function_callback_t callback) {
    tinypy_value_t *function = tinypy_native_function_new(vm, name, name_size, callback, NULL, NULL);

    __tinypy_internal_type_dict_set(vm, type, name, name_size, function);
    TINYPY_DECREF(function);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_internal_descriptor_static_method_set(tinypy_vm_t *vm, tinypy_type_t *type, const char *name, size_t name_size, tinypy_native_function_callback_t callback) {
    tinypy_value_t *function = tinypy_native_function_new(vm, name, name_size, callback, NULL, NULL);
    tinypy_value_t *descriptor = tinypy_static_method_new(function);

    __tinypy_internal_type_dict_set(vm, type, name, name_size, descriptor);
    TINYPY_DECREF(descriptor);
    TINYPY_DECREF(function);
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
        owner = tinypy_none_get(vm);
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
    tinypy_type_t *owner;
    size_t count;

    (void)user_data;
    if (__tinypy_internal_descriptor_method_arguments(vm, args, kwargs, 2U, 3U, out_error) == 0) {
        return NULL;
    }
    count = TINYPY_TUPLE_SIZE(args);
    descriptor = TINYPY_TUPLE_GET(args, 0U);
    instance = TINYPY_TUPLE_GET(args, 1U);
    if (TINYPY_VALUE_KIND(instance) == TINYPY_VALUE_NONE) {
        instance = NULL;
    }
    if (count == 3U) {
        tinypy_value_t *owner_value = TINYPY_TUPLE_GET(args, 2U);

        if (TINYPY_VALUE_KIND(owner_value) != TINYPY_VALUE_TYPE) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "descriptor owner must be a type", out_error);
            return NULL;
        }
        owner = (tinypy_type_t *)owner_value;
    }
    else if (instance != NULL) {
        owner = instance->type;
    }
    else {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "descriptor requires an instance or owner", out_error);
        return NULL;
    }
    if (TINYPY_VALUE_KIND(descriptor) == TINYPY_VALUE_FUNCTION) {
        tinypy_value_t *return_value_1 = tinypy_internal_function_descriptor_get(descriptor, instance, owner, out_error);
        return return_value_1;
    }
    if (TINYPY_VALUE_KIND(descriptor) == TINYPY_VALUE_METHOD) {
        tinypy_value_t *return_value_2 = tinypy_internal_method_descriptor_get(descriptor, instance, owner, out_error);
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
        tinypy_value_t *return_value_5 = tinypy_internal_class_method_get(descriptor, instance, owner, out_error);
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
    if (__tinypy_internal_descriptor_method_arguments(vm, args, kwargs, 3U, 3U, out_error) == 0) {
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
    tinypy_value_t *result = tinypy_none_get(vm);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_c_descriptor_delete_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_internal_descriptor_method_arguments(vm, args, kwargs, 2U, 2U, out_error) == 0) {
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
    tinypy_value_t *result = tinypy_none_get(vm);
    return result;
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
    if (__tinypy_internal_descriptor_method_arguments(vm, args, kwargs, 1U, 1U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *value = TINYPY_TUPLE_GET(args, 0U);
    if (TINYPY_VALUE_KIND(value) != TINYPY_VALUE_GETSET_DESCRIPTOR && TINYPY_VALUE_KIND(value) != TINYPY_VALUE_MEMBER_DESCRIPTOR) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "__repr__ requires a C descriptor", out_error);
        return NULL;
    }
    descriptor = TINYPY_C_DESCRIPTOR_OBJECT(value);
    prefix = TINYPY_VALUE_KIND(value) == TINYPY_VALUE_MEMBER_DESCRIPTOR ? member_prefix : getset_prefix;
    prefix_size = TINYPY_VALUE_KIND(value) == TINYPY_VALUE_MEMBER_DESCRIPTOR ? sizeof(member_prefix) - 1U : sizeof(getset_prefix) - 1U;
    name_size = TINYPY_TEXT_BYTE_SIZE(descriptor->name);
    owner_size = descriptor->owner->name_size;
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
    (void)memcpy(bytes + offset, descriptor->owner->name, owner_size);
    offset += owner_size;
    (void)memcpy(bytes + offset, suffix, sizeof(suffix) - 1U);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_property_set_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_internal_descriptor_method_arguments(vm, args, kwargs, 3U, 3U, out_error) == 0) {
        return NULL;
    }
    if (tinypy_internal_property_set(TINYPY_TUPLE_GET(args, 0U), TINYPY_TUPLE_GET(args, 1U), TINYPY_TUPLE_GET(args, 2U), out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *result = tinypy_none_get(vm);

    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_property_delete_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_internal_descriptor_method_arguments(vm, args, kwargs, 2U, 2U, out_error) == 0) {
        return NULL;
    }
    if (tinypy_internal_property_set(TINYPY_TUPLE_GET(args, 0U), TINYPY_TUPLE_GET(args, 1U), NULL, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *result = tinypy_none_get(vm);

    return result;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_internal_property_field_set(tinypy_vm_t *vm, const char *name, size_t name_size, tinypy_native_function_callback_t callback) {
    tinypy_value_t *function = tinypy_native_function_new(vm, name, name_size, callback, NULL, NULL);
    tinypy_value_t *descriptor = tinypy_property_new(vm, function, NULL, NULL, NULL);

    __tinypy_internal_type_dict_set(vm, &vm->types[TINYPY_VALUE_PROPERTY], name, name_size, descriptor);
    TINYPY_DECREF(descriptor);
    TINYPY_DECREF(function);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_c_descriptor_metadata(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    if (__tinypy_internal_descriptor_method_arguments(vm, args, kwargs, 1U, 1U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *value = TINYPY_TUPLE_GET(args, 0U);
    if (TINYPY_VALUE_KIND(value) != TINYPY_VALUE_GETSET_DESCRIPTOR && TINYPY_VALUE_KIND(value) != TINYPY_VALUE_MEMBER_DESCRIPTOR) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "descriptor metadata requires a C descriptor", out_error);
        return NULL;
    }
    tinypy_c_descriptor_object_t *descriptor = TINYPY_C_DESCRIPTOR_OBJECT(value);
    tinypy_value_t *result = user_data == NULL ? descriptor->name : &descriptor->owner->base.base;

    TINYPY_INCREF(result);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_internal_c_descriptor_metadata_set(tinypy_vm_t *vm, tinypy_type_t *type, const char *name, size_t name_size, void *field) {
    tinypy_value_t *getter = tinypy_native_function_new(vm, name, name_size, __tinypy_internal_c_descriptor_metadata, field, NULL);
    tinypy_value_t *descriptor = tinypy_property_new(vm, getter, NULL, NULL, NULL);

    __tinypy_internal_type_dict_set(vm, type, name, name_size, descriptor);
    TINYPY_DECREF(descriptor);
    TINYPY_DECREF(getter);
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_c_descriptor_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data) {
    tinypy_c_descriptor_object_t *descriptor = TINYPY_C_DESCRIPTOR_OBJECT(value);
    if (descriptor->owner_retained != 0) {
        visit(&descriptor->owner->base.base, user_data);
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
    if (instance == NULL) {
        TINYPY_INCREF(descriptor_value);
        return descriptor_value;
    }
    if (tinypy_type_is_subtype(instance->type, descriptor->owner) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "descriptor does not apply to this object", out_error);
        return NULL;
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
            TINYPY_INCREF(value);
            return value;
        }
        tinypy_value_t *return_value_1 = tinypy_object_get_attr(instance, (const char *)TINYPY_TEXT_BYTES(descriptor->name), TINYPY_TEXT_BYTE_SIZE(descriptor->name), out_error);
        return return_value_1;
    }
    if (field == TINYPY_INTERNAL_C_DESCRIPTOR_INSTANCE_SLOT) {
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
    if (field == TINYPY_INTERNAL_C_DESCRIPTOR_INSTANCE_DICT) {
        tinypy_value_t **dict_slot = tinypy_internal_object_dict_slot(instance);

        if (dict_slot == NULL) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_ATTRIBUTE, "object has no __dict__", out_error);
            return NULL;
        }
        if (*dict_slot == NULL) {
            *dict_slot = tinypy_dict_new(vm);
        }
        TINYPY_INCREF(*dict_slot);
        return *dict_slot;
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
        TINYPY_INCREF(content);
        return content;
    }
    if (field == TINYPY_INTERNAL_C_DESCRIPTOR_MODULE_DICT) {
        tinypy_value_t *dict = TINYPY_MODULE_OBJECT(instance)->dict;

        TINYPY_INCREF(dict);
        return dict;
    }
    if (field >= TINYPY_INTERNAL_C_DESCRIPTOR_SUPER_THISCLASS && field <= TINYPY_INTERNAL_C_DESCRIPTOR_SUPER_SELF_CLASS) {
        tinypy_super_object_t *super_value = TINYPY_SUPER_OBJECT(instance);

        if (field == TINYPY_INTERNAL_C_DESCRIPTOR_SUPER_THISCLASS) {
            TINYPY_INCREF(&super_value->type->base.base);
            return &super_value->type->base.base;
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
        TINYPY_INCREF(function_result);
        return function_result;
    }
    if (field == TINYPY_INTERNAL_C_DESCRIPTOR_EXCEPTION_ARGS || field == TINYPY_INTERNAL_C_DESCRIPTOR_EXCEPTION_MESSAGE) {
        tinypy_internal_exception_payload_t *payload = (tinypy_internal_exception_payload_t *)tinypy_native_instance_payload(instance);

        if (field == TINYPY_INTERNAL_C_DESCRIPTOR_EXCEPTION_ARGS) {
            function_result = __tinypy_internal_c_descriptor_optional(vm, payload->args);
            return function_result;
        }
        tinypy_value_t **dict_slot = tinypy_internal_object_dict_slot(instance);
        if (dict_slot != NULL && *dict_slot != NULL) {
            tinypy_value_t *key = tinypy_string_from_bytes(vm, "message", 7U);
            tinypy_value_t *message = tinypy_dict_get_optional(*dict_slot, key);

            TINYPY_DECREF(key);
            if (message != NULL) {
                TINYPY_INCREF(message);
                return message;
            }
        }
        if (payload->message == NULL) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_ATTRIBUTE, "message attribute was deleted", out_error);
            return NULL;
        }
        TINYPY_INCREF(payload->message);
        return payload->message;
    }
    if (field >= TINYPY_INTERNAL_C_DESCRIPTOR_METHOD_FUNCTION && field <= TINYPY_INTERNAL_C_DESCRIPTOR_METHOD_OWNER) {
        tinypy_method_object_t *method = TINYPY_METHOD_OBJECT(instance);
        tinypy_value_t *value = field == TINYPY_INTERNAL_C_DESCRIPTOR_METHOD_FUNCTION ? method->function : (field == TINYPY_INTERNAL_C_DESCRIPTOR_METHOD_SELF ? method->self : method->owner);

        function_result = __tinypy_internal_c_descriptor_optional(vm, value);
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
        TINYPY_INCREF(function_result);
        return function_result;
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
            function_result = __tinypy_internal_c_descriptor_optional(vm, frame == vm->current_frame ? vm->handled_type : frame->previous_handled_type);
            return function_result;
        case TINYPY_INTERNAL_C_DESCRIPTOR_FRAME_EXCEPTION_VALUE:
            function_result = __tinypy_internal_c_descriptor_optional(vm, frame == vm->current_frame ? vm->handled_value : frame->previous_handled_value);
            return function_result;
        case TINYPY_INTERNAL_C_DESCRIPTOR_FRAME_EXCEPTION_TRACEBACK:
            function_result = __tinypy_internal_c_descriptor_optional(vm, frame == vm->current_frame ? vm->handled_traceback : frame->previous_handled_traceback);
            return function_result;
        case TINYPY_INTERNAL_C_DESCRIPTOR_FRAME_RESTRICTED:
            function_result = tinypy_bool_from_i32(vm, frame->builtins != vm->builtins);
            return function_result;
        default:
            return NULL;
        }
        TINYPY_INCREF(function_result);
        return function_result;
    }
    if (field >= TINYPY_INTERNAL_C_DESCRIPTOR_TRACEBACK_NEXT && field <= TINYPY_INTERNAL_C_DESCRIPTOR_TRACEBACK_LINE_NUMBER) {
        tinypy_traceback_object_t *traceback = TINYPY_TRACEBACK_OBJECT(instance);

        if (field == TINYPY_INTERNAL_C_DESCRIPTOR_TRACEBACK_NEXT) {
            function_result = __tinypy_internal_c_descriptor_optional(vm, traceback->next);
            return function_result;
        }
        if (field == TINYPY_INTERNAL_C_DESCRIPTOR_TRACEBACK_FRAME) {
            TINYPY_INCREF(traceback->frame);
            return traceback->frame;
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
        TINYPY_INCREF(function_result);
        return function_result;
    }
    if (TINYPY_VALUE_KIND(instance) != TINYPY_VALUE_FUNCTION) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "descriptor does not apply to this object", out_error);
        return NULL;
    }
    tinypy_function_object_t *function = TINYPY_FUNCTION_OBJECT(instance);
    switch (field) {
    case TINYPY_INTERNAL_C_DESCRIPTOR_FUNCTION_CODE:
        TINYPY_INCREF(function->code);
        return function->code;
    case TINYPY_INTERNAL_C_DESCRIPTOR_FUNCTION_GLOBALS:
        TINYPY_INCREF(function->globals);
        return function->globals;
    case TINYPY_INTERNAL_C_DESCRIPTOR_FUNCTION_DEFAULTS:
        function_result = __tinypy_internal_c_descriptor_optional(vm, function->defaults);
        return function_result;
    case TINYPY_INTERNAL_C_DESCRIPTOR_FUNCTION_CLOSURE:
        function_result = __tinypy_internal_c_descriptor_optional(vm, function->closure);
        return function_result;
    case TINYPY_INTERNAL_C_DESCRIPTOR_FUNCTION_NAME:
        TINYPY_INCREF(function->name);
        return function->name;
    case TINYPY_INTERNAL_C_DESCRIPTOR_FUNCTION_DOC:
        function_result = __tinypy_internal_c_descriptor_optional(vm, function->doc);
        return function_result;
    case TINYPY_INTERNAL_C_DESCRIPTOR_FUNCTION_DICT:
        if (function->dict == NULL) {
            function->dict = tinypy_dict_new(vm);
        }
        TINYPY_INCREF(function->dict);
        return function->dict;
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
    if (tinypy_type_is_subtype(instance->type, descriptor->owner) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "descriptor does not apply to this object", out_error);
        return TINYPY_FALSE;
    }
    if (descriptor->writable == 0) {
        if (field == TINYPY_INTERNAL_C_DESCRIPTOR_TYPE_DOC) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_ATTRIBUTE, "attribute '__doc__' of 'type' objects is not writable", out_error);
        }
        else {
            __tinypy_internal_c_descriptor_readonly(vm, out_error);
        }
        return TINYPY_FALSE;
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
        if (field == TINYPY_INTERNAL_C_DESCRIPTOR_TYPE_BASES) {
            if (value == NULL) {
                __tinypy_internal_c_descriptor_readonly(vm, out_error);
                return TINYPY_FALSE;
            }
            tinypy_bool_t result = tinypy_internal_type_set_bases(type, value, out_error);

            return result;
        }
        if (field == TINYPY_INTERNAL_C_DESCRIPTOR_TYPE_NAME) {
            tinypy_bool_t result = tinypy_internal_type_set_name(type, value, out_error);

            return result;
        }
        if (field == TINYPY_INTERNAL_C_DESCRIPTOR_TYPE_MODULE) {
            if (value == NULL) {
                __tinypy_internal_c_descriptor_readonly(vm, out_error);
                return TINYPY_FALSE;
            }
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
        tinypy_value_t **dict_slot;

        if (value == NULL || TINYPY_VALUE_KIND(value) != TINYPY_VALUE_DICT) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "__dict__ must be a dictionary", out_error);
            return TINYPY_FALSE;
        }
        dict_slot = tinypy_internal_object_dict_slot(instance);
        if (dict_slot == NULL) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_ATTRIBUTE, "object has no __dict__", out_error);
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
        tinypy_value_t *key = tinypy_string_from_bytes(vm, "message", 7U);

        if (value == NULL) {
            if (dict_slot != NULL && *dict_slot != NULL) {
                (void)tinypy_internal_dict_delete_optional(vm, *dict_slot, key);
            }
            __tinypy_internal_c_descriptor_replace(&payload->message, NULL);
            TINYPY_DECREF(key);
            return TINYPY_TRUE;
        }
        if (dict_slot == NULL) {
            TINYPY_DECREF(key);
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_ATTRIBUTE, "object has no __dict__", out_error);
            return TINYPY_FALSE;
        }
        if (*dict_slot == NULL) {
            *dict_slot = tinypy_dict_new(vm);
        }
        tinypy_bool_t stored = tinypy_internal_dict_set_checked(vm, *dict_slot, key, value, out_error);
        TINYPY_DECREF(key);
        return stored;
    }
    if (field == TINYPY_INTERNAL_C_DESCRIPTOR_FRAME_TRACE) {
        tinypy_frame_object_t *frame = TINYPY_FRAME_OBJECT(instance);

        __tinypy_internal_c_descriptor_replace(&frame->trace, value);
        return TINYPY_TRUE;
    }
    if (TINYPY_VALUE_KIND(instance) != TINYPY_VALUE_FUNCTION) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "descriptor does not apply to this object", out_error);
        return TINYPY_FALSE;
    }
    tinypy_function_object_t *function = TINYPY_FUNCTION_OBJECT(instance);
    switch (field) {
    case TINYPY_INTERNAL_C_DESCRIPTOR_FUNCTION_CODE:
        if (value == NULL) {
            __tinypy_internal_c_descriptor_readonly(vm, out_error);
            return TINYPY_FALSE;
        }
        if (TINYPY_VALUE_KIND(value) != TINYPY_VALUE_CODE) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "func_code must be a code object", out_error);
            return TINYPY_FALSE;
        }
        tinypy_bool_t condition = function->closure != NULL;
        if (condition != 0) {
            tinypy_value_t *freevars = TINYPY_CODE_FREEVARS(value);
            condition = TINYPY_TUPLE_SIZE(function->closure) != TINYPY_TUPLE_SIZE(freevars);
        }
        if (condition) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "func_code has incompatible free variables", out_error);
            return TINYPY_FALSE;
        }
        __tinypy_internal_c_descriptor_replace(&function->code, value);
        return TINYPY_TRUE;
    case TINYPY_INTERNAL_C_DESCRIPTOR_FUNCTION_DEFAULTS:
        if (value != NULL && TINYPY_VALUE_KIND(value) == TINYPY_VALUE_NONE) {
            value = NULL;
        }
        if (value != NULL && TINYPY_VALUE_KIND(value) != TINYPY_VALUE_TUPLE) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "func_defaults must be a tuple", out_error);
            return TINYPY_FALSE;
        }
        __tinypy_internal_c_descriptor_replace(&function->defaults, value);
        return TINYPY_TRUE;
    case TINYPY_INTERNAL_C_DESCRIPTOR_FUNCTION_NAME:
        if (value == NULL || TINYPY_VALUE_KIND(value) != TINYPY_VALUE_STRING) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "func_name must be a string", out_error);
            return TINYPY_FALSE;
        }
        __tinypy_internal_c_descriptor_replace(&function->name, value);
        return TINYPY_TRUE;
    case TINYPY_INTERNAL_C_DESCRIPTOR_FUNCTION_DOC:
        if (value == NULL) {
            value = tinypy_none_get(vm);
        }
        else {
            TINYPY_INCREF(value);
        }
        __tinypy_internal_c_descriptor_replace(&function->doc, value);
        TINYPY_DECREF(value);
        return TINYPY_TRUE;
    case TINYPY_INTERNAL_C_DESCRIPTOR_FUNCTION_DICT:
        if (value == NULL || TINYPY_VALUE_KIND(value) != TINYPY_VALUE_DICT) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "func_dict must be a dictionary", out_error);
            return TINYPY_FALSE;
        }
        __tinypy_internal_c_descriptor_replace(&function->dict, value);
        return TINYPY_TRUE;
    case TINYPY_INTERNAL_C_DESCRIPTOR_FUNCTION_MODULE:
        if (value == NULL) {
            value = tinypy_none_get(vm);
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
static void __tinypy_internal_function_descriptor_set(tinypy_vm_t *vm, tinypy_value_type_e kind, const char *name, size_t name_size, tinypy_internal_c_descriptor_field_e field, tinypy_bool_t writable) {
    tinypy_value_t *descriptor = __tinypy_internal_c_descriptor_new(vm, kind, &vm->types[TINYPY_VALUE_FUNCTION], name, name_size, field, writable);

    __tinypy_internal_type_dict_set(vm, &vm->types[TINYPY_VALUE_FUNCTION], name, name_size, descriptor);
    TINYPY_DECREF(descriptor);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_internal_builtin_descriptor_set(tinypy_vm_t *vm, tinypy_type_t *owner, tinypy_value_type_e kind, const char *name, size_t name_size, tinypy_internal_c_descriptor_field_e field, tinypy_bool_t writable) {
    tinypy_value_t *descriptor = __tinypy_internal_c_descriptor_new(vm, kind, owner, name, name_size, field, writable);

    __tinypy_internal_type_dict_set(vm, owner, name, name_size, descriptor);
    TINYPY_DECREF(descriptor);
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_initialize_descriptor_types(tinypy_vm_t *vm) {
    __tinypy_internal_descriptor_method_set(vm, &vm->types[TINYPY_VALUE_FUNCTION], "__get__", 7U, __tinypy_internal_descriptor_get_method);
    __tinypy_internal_descriptor_static_method_set(vm, &vm->types[TINYPY_VALUE_METHOD], "__new__", 7U, __tinypy_internal_method_new_method);
    __tinypy_internal_descriptor_method_set(vm, &vm->types[TINYPY_VALUE_METHOD], "__get__", 7U, __tinypy_internal_descriptor_get_method);
    __tinypy_internal_descriptor_method_set(vm, &vm->types[TINYPY_VALUE_METHOD], "__call__", 8U, __tinypy_internal_method_call_method);
    __tinypy_internal_descriptor_method_set(vm, &vm->types[TINYPY_VALUE_METHOD], "__repr__", 8U, __tinypy_internal_method_repr_method);
    __tinypy_internal_descriptor_method_set(vm, &vm->types[TINYPY_VALUE_METHOD], "__hash__", 8U, __tinypy_internal_method_hash_method);
    __tinypy_internal_descriptor_method_set(vm, &vm->types[TINYPY_VALUE_METHOD], "__cmp__", 7U, __tinypy_internal_method_cmp_method);
    __tinypy_internal_descriptor_method_set(vm, &vm->types[TINYPY_VALUE_PROPERTY], "__get__", 7U, __tinypy_internal_descriptor_get_method);
    __tinypy_internal_descriptor_method_set(vm, &vm->types[TINYPY_VALUE_STATIC_METHOD], "__get__", 7U, __tinypy_internal_descriptor_get_method);
    __tinypy_internal_descriptor_method_set(vm, &vm->types[TINYPY_VALUE_CLASS_METHOD], "__get__", 7U, __tinypy_internal_descriptor_get_method);
    __tinypy_internal_descriptor_method_set(vm, &vm->types[TINYPY_VALUE_PROPERTY], "__init__", 8U, __tinypy_internal_property_init_method);
    __tinypy_internal_descriptor_method_set(vm, &vm->types[TINYPY_VALUE_STATIC_METHOD], "__init__", 8U, __tinypy_internal_callable_descriptor_init_method);
    __tinypy_internal_descriptor_method_set(vm, &vm->types[TINYPY_VALUE_CLASS_METHOD], "__init__", 8U, __tinypy_internal_callable_descriptor_init_method);
    __tinypy_internal_descriptor_method_set(vm, &vm->types[TINYPY_VALUE_PROPERTY], "__set__", 7U, __tinypy_internal_property_set_method);
    __tinypy_internal_descriptor_method_set(vm, &vm->types[TINYPY_VALUE_PROPERTY], "__delete__", 10U, __tinypy_internal_property_delete_method);
    __tinypy_internal_descriptor_method_set(vm, &vm->types[TINYPY_VALUE_GETSET_DESCRIPTOR], "__get__", 7U, __tinypy_internal_descriptor_get_method);
    __tinypy_internal_descriptor_method_set(vm, &vm->types[TINYPY_VALUE_GETSET_DESCRIPTOR], "__set__", 7U, __tinypy_internal_c_descriptor_set_method);
    __tinypy_internal_descriptor_method_set(vm, &vm->types[TINYPY_VALUE_GETSET_DESCRIPTOR], "__delete__", 10U, __tinypy_internal_c_descriptor_delete_method);
    __tinypy_internal_descriptor_method_set(vm, &vm->types[TINYPY_VALUE_GETSET_DESCRIPTOR], "__repr__", 8U, __tinypy_internal_c_descriptor_repr_method);
    __tinypy_internal_descriptor_method_set(vm, &vm->types[TINYPY_VALUE_MEMBER_DESCRIPTOR], "__get__", 7U, __tinypy_internal_descriptor_get_method);
    __tinypy_internal_descriptor_method_set(vm, &vm->types[TINYPY_VALUE_MEMBER_DESCRIPTOR], "__set__", 7U, __tinypy_internal_c_descriptor_set_method);
    __tinypy_internal_descriptor_method_set(vm, &vm->types[TINYPY_VALUE_MEMBER_DESCRIPTOR], "__delete__", 10U, __tinypy_internal_c_descriptor_delete_method);
    __tinypy_internal_descriptor_method_set(vm, &vm->types[TINYPY_VALUE_MEMBER_DESCRIPTOR], "__repr__", 8U, __tinypy_internal_c_descriptor_repr_method);
    __tinypy_internal_c_descriptor_metadata_set(vm, &vm->types[TINYPY_VALUE_GETSET_DESCRIPTOR], "__name__", 8U, NULL);
    __tinypy_internal_c_descriptor_metadata_set(vm, &vm->types[TINYPY_VALUE_GETSET_DESCRIPTOR], "__objclass__", 12U, (void *)(intptr_t)1);
    __tinypy_internal_c_descriptor_metadata_set(vm, &vm->types[TINYPY_VALUE_MEMBER_DESCRIPTOR], "__name__", 8U, NULL);
    __tinypy_internal_c_descriptor_metadata_set(vm, &vm->types[TINYPY_VALUE_MEMBER_DESCRIPTOR], "__objclass__", 12U, (void *)(intptr_t)1);
    __tinypy_internal_property_method_set(vm, "getter", 6U, __tinypy_internal_property_getter_method);
    __tinypy_internal_property_method_set(vm, "setter", 6U, __tinypy_internal_property_setter_method);
    __tinypy_internal_property_method_set(vm, "deleter", 7U, __tinypy_internal_property_deleter_method);
    __tinypy_internal_property_field_set(vm, "fget", 4U, __tinypy_internal_property_fget);
    __tinypy_internal_property_field_set(vm, "fset", 4U, __tinypy_internal_property_fset);
    __tinypy_internal_property_field_set(vm, "fdel", 4U, __tinypy_internal_property_fdel);
    __tinypy_internal_property_field_set(vm, "__doc__", 7U, __tinypy_internal_property_doc_value);
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_TYPE], TINYPY_VALUE_GETSET_DESCRIPTOR, "__abstractmethods__", 19U, TINYPY_INTERNAL_C_DESCRIPTOR_TYPE_ABSTRACT_METHODS, TINYPY_TRUE);
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_TYPE], TINYPY_VALUE_MEMBER_DESCRIPTOR, "__base__", 8U, TINYPY_INTERNAL_C_DESCRIPTOR_TYPE_BASE, TINYPY_FALSE);
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_TYPE], TINYPY_VALUE_GETSET_DESCRIPTOR, "__bases__", 9U, TINYPY_INTERNAL_C_DESCRIPTOR_TYPE_BASES, TINYPY_TRUE);
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_TYPE], TINYPY_VALUE_MEMBER_DESCRIPTOR, "__basicsize__", 13U, TINYPY_INTERNAL_C_DESCRIPTOR_TYPE_BASIC_SIZE, TINYPY_FALSE);
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_TYPE], TINYPY_VALUE_GETSET_DESCRIPTOR, "__dict__", 8U, TINYPY_INTERNAL_C_DESCRIPTOR_TYPE_DICT, TINYPY_FALSE);
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_TYPE], TINYPY_VALUE_GETSET_DESCRIPTOR, "__doc__", 7U, TINYPY_INTERNAL_C_DESCRIPTOR_TYPE_DOC, TINYPY_FALSE);
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_TYPE], TINYPY_VALUE_MEMBER_DESCRIPTOR, "__dictoffset__", 14U, TINYPY_INTERNAL_C_DESCRIPTOR_TYPE_DICT_OFFSET, TINYPY_FALSE);
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_TYPE], TINYPY_VALUE_MEMBER_DESCRIPTOR, "__flags__", 9U, TINYPY_INTERNAL_C_DESCRIPTOR_TYPE_FLAGS, TINYPY_FALSE);
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_TYPE], TINYPY_VALUE_MEMBER_DESCRIPTOR, "__itemsize__", 12U, TINYPY_INTERNAL_C_DESCRIPTOR_TYPE_ITEM_SIZE, TINYPY_FALSE);
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_TYPE], TINYPY_VALUE_GETSET_DESCRIPTOR, "__module__", 10U, TINYPY_INTERNAL_C_DESCRIPTOR_TYPE_MODULE, TINYPY_TRUE);
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_TYPE], TINYPY_VALUE_MEMBER_DESCRIPTOR, "__mro__", 7U, TINYPY_INTERNAL_C_DESCRIPTOR_TYPE_MRO, TINYPY_FALSE);
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_TYPE], TINYPY_VALUE_GETSET_DESCRIPTOR, "__name__", 8U, TINYPY_INTERNAL_C_DESCRIPTOR_TYPE_NAME, TINYPY_TRUE);
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_TYPE], TINYPY_VALUE_MEMBER_DESCRIPTOR, "__weakrefoffset__", 17U, TINYPY_INTERNAL_C_DESCRIPTOR_TYPE_WEAKREF_OFFSET, TINYPY_FALSE);
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
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_METHOD], TINYPY_VALUE_MEMBER_DESCRIPTOR, "im_func", 7U, TINYPY_INTERNAL_C_DESCRIPTOR_METHOD_FUNCTION, INT32_C(0));
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_METHOD], TINYPY_VALUE_MEMBER_DESCRIPTOR, "im_self", 7U, TINYPY_INTERNAL_C_DESCRIPTOR_METHOD_SELF, INT32_C(0));
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_METHOD], TINYPY_VALUE_MEMBER_DESCRIPTOR, "im_class", 8U, TINYPY_INTERNAL_C_DESCRIPTOR_METHOD_OWNER, INT32_C(0));
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_METHOD], TINYPY_VALUE_MEMBER_DESCRIPTOR, "__func__", 8U, TINYPY_INTERNAL_C_DESCRIPTOR_METHOD_FUNCTION, INT32_C(0));
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_METHOD], TINYPY_VALUE_MEMBER_DESCRIPTOR, "__self__", 8U, TINYPY_INTERNAL_C_DESCRIPTOR_METHOD_SELF, INT32_C(0));
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_STATIC_METHOD], TINYPY_VALUE_MEMBER_DESCRIPTOR, "__func__", 8U, TINYPY_INTERNAL_C_DESCRIPTOR_CALLABLE_FUNCTION, INT32_C(0));
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_CLASS_METHOD], TINYPY_VALUE_MEMBER_DESCRIPTOR, "__func__", 8U, TINYPY_INTERNAL_C_DESCRIPTOR_CALLABLE_FUNCTION, INT32_C(0));
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_CODE], TINYPY_VALUE_MEMBER_DESCRIPTOR, "co_argcount", 11U, TINYPY_INTERNAL_C_DESCRIPTOR_CODE_ARG_COUNT, INT32_C(0));
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_CODE], TINYPY_VALUE_MEMBER_DESCRIPTOR, "co_nlocals", 10U, TINYPY_INTERNAL_C_DESCRIPTOR_CODE_LOCAL_COUNT, INT32_C(0));
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_CODE], TINYPY_VALUE_MEMBER_DESCRIPTOR, "co_stacksize", 12U, TINYPY_INTERNAL_C_DESCRIPTOR_CODE_STACK_SIZE, INT32_C(0));
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_CODE], TINYPY_VALUE_MEMBER_DESCRIPTOR, "co_flags", 8U, TINYPY_INTERNAL_C_DESCRIPTOR_CODE_FLAGS, INT32_C(0));
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_CODE], TINYPY_VALUE_MEMBER_DESCRIPTOR, "co_code", 7U, TINYPY_INTERNAL_C_DESCRIPTOR_CODE_BYTECODE, INT32_C(0));
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_CODE], TINYPY_VALUE_MEMBER_DESCRIPTOR, "co_consts", 9U, TINYPY_INTERNAL_C_DESCRIPTOR_CODE_CONSTS, INT32_C(0));
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_CODE], TINYPY_VALUE_MEMBER_DESCRIPTOR, "co_names", 8U, TINYPY_INTERNAL_C_DESCRIPTOR_CODE_NAMES, INT32_C(0));
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_CODE], TINYPY_VALUE_MEMBER_DESCRIPTOR, "co_varnames", 11U, TINYPY_INTERNAL_C_DESCRIPTOR_CODE_VARNAMES, INT32_C(0));
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_CODE], TINYPY_VALUE_MEMBER_DESCRIPTOR, "co_freevars", 11U, TINYPY_INTERNAL_C_DESCRIPTOR_CODE_FREEVARS, INT32_C(0));
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_CODE], TINYPY_VALUE_MEMBER_DESCRIPTOR, "co_cellvars", 11U, TINYPY_INTERNAL_C_DESCRIPTOR_CODE_CELLVARS, INT32_C(0));
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_CODE], TINYPY_VALUE_MEMBER_DESCRIPTOR, "co_filename", 11U, TINYPY_INTERNAL_C_DESCRIPTOR_CODE_FILENAME, INT32_C(0));
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_CODE], TINYPY_VALUE_MEMBER_DESCRIPTOR, "co_name", 7U, TINYPY_INTERNAL_C_DESCRIPTOR_CODE_NAME, INT32_C(0));
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_CODE], TINYPY_VALUE_MEMBER_DESCRIPTOR, "co_firstlineno", 14U, TINYPY_INTERNAL_C_DESCRIPTOR_CODE_FIRST_LINE, INT32_C(0));
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_CODE], TINYPY_VALUE_MEMBER_DESCRIPTOR, "co_lnotab", 9U, TINYPY_INTERNAL_C_DESCRIPTOR_CODE_LNOTAB, INT32_C(0));
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_CELL], TINYPY_VALUE_GETSET_DESCRIPTOR, "cell_contents", 13U, TINYPY_INTERNAL_C_DESCRIPTOR_CELL_CONTENT, INT32_C(0));
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_MODULE], TINYPY_VALUE_MEMBER_DESCRIPTOR, "__dict__", 8U, TINYPY_INTERNAL_C_DESCRIPTOR_MODULE_DICT, INT32_C(0));
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_SUPER], TINYPY_VALUE_MEMBER_DESCRIPTOR, "__thisclass__", 13U, TINYPY_INTERNAL_C_DESCRIPTOR_SUPER_THISCLASS, INT32_C(0));
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_SUPER], TINYPY_VALUE_MEMBER_DESCRIPTOR, "__self__", 8U, TINYPY_INTERNAL_C_DESCRIPTOR_SUPER_SELF, INT32_C(0));
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_SUPER], TINYPY_VALUE_MEMBER_DESCRIPTOR, "__self_class__", 14U, TINYPY_INTERNAL_C_DESCRIPTOR_SUPER_SELF_CLASS, INT32_C(0));
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_PARTIAL], TINYPY_VALUE_MEMBER_DESCRIPTOR, "func", 4U, TINYPY_INTERNAL_C_DESCRIPTOR_PARTIAL_FUNCTION, INT32_C(0));
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_PARTIAL], TINYPY_VALUE_MEMBER_DESCRIPTOR, "args", 4U, TINYPY_INTERNAL_C_DESCRIPTOR_PARTIAL_ARGS, INT32_C(0));
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_PARTIAL], TINYPY_VALUE_MEMBER_DESCRIPTOR, "keywords", 8U, TINYPY_INTERNAL_C_DESCRIPTOR_PARTIAL_KEYWORDS, INT32_C(0));
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_FRAME], TINYPY_VALUE_GETSET_DESCRIPTOR, "f_back", 6U, TINYPY_INTERNAL_C_DESCRIPTOR_FRAME_BACK, INT32_C(0));
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_FRAME], TINYPY_VALUE_GETSET_DESCRIPTOR, "f_code", 6U, TINYPY_INTERNAL_C_DESCRIPTOR_FRAME_CODE, INT32_C(0));
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_FRAME], TINYPY_VALUE_GETSET_DESCRIPTOR, "f_builtins", 10U, TINYPY_INTERNAL_C_DESCRIPTOR_FRAME_BUILTINS, INT32_C(0));
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_FRAME], TINYPY_VALUE_GETSET_DESCRIPTOR, "f_globals", 9U, TINYPY_INTERNAL_C_DESCRIPTOR_FRAME_GLOBALS, INT32_C(0));
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_FRAME], TINYPY_VALUE_GETSET_DESCRIPTOR, "f_locals", 8U, TINYPY_INTERNAL_C_DESCRIPTOR_FRAME_LOCALS, INT32_C(0));
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_FRAME], TINYPY_VALUE_GETSET_DESCRIPTOR, "f_lasti", 7U, TINYPY_INTERNAL_C_DESCRIPTOR_FRAME_LAST_INSTRUCTION, INT32_C(0));
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_FRAME], TINYPY_VALUE_GETSET_DESCRIPTOR, "f_lineno", 8U, TINYPY_INTERNAL_C_DESCRIPTOR_FRAME_LINE_NUMBER, INT32_C(0));
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_FRAME], TINYPY_VALUE_GETSET_DESCRIPTOR, "f_trace", 7U, TINYPY_INTERNAL_C_DESCRIPTOR_FRAME_TRACE, INT32_C(1));
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_FRAME], TINYPY_VALUE_GETSET_DESCRIPTOR, "f_exc_type", 10U, TINYPY_INTERNAL_C_DESCRIPTOR_FRAME_EXCEPTION_TYPE, INT32_C(0));
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_FRAME], TINYPY_VALUE_GETSET_DESCRIPTOR, "f_exc_value", 11U, TINYPY_INTERNAL_C_DESCRIPTOR_FRAME_EXCEPTION_VALUE, INT32_C(0));
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_FRAME], TINYPY_VALUE_GETSET_DESCRIPTOR, "f_exc_traceback", 15U, TINYPY_INTERNAL_C_DESCRIPTOR_FRAME_EXCEPTION_TRACEBACK, INT32_C(0));
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_FRAME], TINYPY_VALUE_GETSET_DESCRIPTOR, "f_restricted", 12U, TINYPY_INTERNAL_C_DESCRIPTOR_FRAME_RESTRICTED, INT32_C(0));
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_TRACEBACK], TINYPY_VALUE_MEMBER_DESCRIPTOR, "tb_next", 7U, TINYPY_INTERNAL_C_DESCRIPTOR_TRACEBACK_NEXT, INT32_C(0));
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_TRACEBACK], TINYPY_VALUE_MEMBER_DESCRIPTOR, "tb_frame", 8U, TINYPY_INTERNAL_C_DESCRIPTOR_TRACEBACK_FRAME, INT32_C(0));
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_TRACEBACK], TINYPY_VALUE_MEMBER_DESCRIPTOR, "tb_lasti", 8U, TINYPY_INTERNAL_C_DESCRIPTOR_TRACEBACK_LAST_INSTRUCTION, INT32_C(0));
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_TRACEBACK], TINYPY_VALUE_MEMBER_DESCRIPTOR, "tb_lineno", 9U, TINYPY_INTERNAL_C_DESCRIPTOR_TRACEBACK_LINE_NUMBER, INT32_C(0));
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_GENERATOR], TINYPY_VALUE_GETSET_DESCRIPTOR, "__name__", 8U, TINYPY_INTERNAL_C_DESCRIPTOR_GENERATOR_NAME, INT32_C(0));
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_GENERATOR], TINYPY_VALUE_MEMBER_DESCRIPTOR, "gi_code", 7U, TINYPY_INTERNAL_C_DESCRIPTOR_GENERATOR_CODE, INT32_C(0));
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_GENERATOR], TINYPY_VALUE_MEMBER_DESCRIPTOR, "gi_frame", 8U, TINYPY_INTERNAL_C_DESCRIPTOR_GENERATOR_FRAME, INT32_C(0));
    __tinypy_internal_builtin_descriptor_set(vm, &vm->types[TINYPY_VALUE_GENERATOR], TINYPY_VALUE_MEMBER_DESCRIPTOR, "gi_running", 10U, TINYPY_INTERNAL_C_DESCRIPTOR_GENERATOR_RUNNING, INT32_C(0));
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_initialize_exception_descriptors(tinypy_type_t *type) {
    tinypy_value_t *args = __tinypy_internal_c_descriptor_new_with_owner(type->vm, TINYPY_VALUE_GETSET_DESCRIPTOR, type, "args", 4U, TINYPY_INTERNAL_C_DESCRIPTOR_EXCEPTION_ARGS, TINYPY_TRUE, TINYPY_FALSE);
    tinypy_value_t *message = __tinypy_internal_c_descriptor_new_with_owner(type->vm, TINYPY_VALUE_GETSET_DESCRIPTOR, type, "message", 7U, TINYPY_INTERNAL_C_DESCRIPTOR_EXCEPTION_MESSAGE, TINYPY_TRUE, TINYPY_FALSE);

    tinypy_type_set_attr(type, "args", 4U, args);
    tinypy_type_set_attr(type, "message", 7U, message);
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
