#include "tinypy/class.h"

#include "internal.h"

#include <assert.h>
#include <string.h>

//////////////////////////////////////////////////////////////////////////
static tinypy_class_object_t *__tinypy_class_validate(const tinypy_value_t *value) {
    assert(value != NULL);
    assert(tinypy_internal_value_kind(value) == TINYPY_VALUE_CLASS);
    return TINYPY_CLASS_OBJECT((tinypy_value_t *)value);
}

//////////////////////////////////////////////////////////////////////////
static tinypy_old_instance_object_t *__tinypy_old_instance_validate(const tinypy_value_t *value) {
    assert(value != NULL);
    assert(tinypy_internal_value_kind(value) == TINYPY_VALUE_OLD_INSTANCE);
    return TINYPY_OLD_INSTANCE_OBJECT((tinypy_value_t *)value);
}

//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_class_name_equal(tinypy_value_t *name, const char *expected, size_t expected_size) {
    size_t name_size;
    const unsigned char *bytes = (const unsigned char *)tinypy_string_view(name, &name_size);

    return name_size == expected_size && (name_size == 0U || memcmp(bytes, expected, name_size) == 0) ? INT32_C(1) : INT32_C(0);
}

//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_class_dict_lookup(tinypy_value_t *dict, const char *name, size_t name_size) {
    tinypy_vm_t *vm = tinypy_internal_value_vm(dict);
    tinypy_value_t *key = tinypy_string_from_bytes(vm, name, name_size);
    tinypy_value_t *result = tinypy_dict_contains(dict, key) != 0 ? tinypy_dict_get(dict, key) : NULL;

    tinypy_release(key);
    return result;
}

//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_class_lookup(tinypy_value_t *class_value, const char *name, size_t name_size) {
    tinypy_class_object_t *class_object = __tinypy_class_validate(class_value);
    tinypy_value_t *attribute = __tinypy_class_dict_lookup(class_object->dict, name, name_size);
    size_t index;

    if (attribute != NULL) {
        return attribute;
    }
    for (index = 0U; index < tinypy_tuple_size(class_object->bases); index += 1U) {
        tinypy_value_t *item = tinypy_tuple_get(class_object->bases, index);
        attribute = tinypy_internal_class_lookup(item, name, name_size);
        if (attribute != NULL) {
            return attribute;
        }
    }
    return NULL;
}

//////////////////////////////////////////////////////////////////////////
int32_t tinypy_class_is_subclass(const tinypy_value_t *class_value, const tinypy_value_t *candidate_base) {
    tinypy_class_object_t *class_object = __tinypy_class_validate(class_value);
    size_t index;

    (void)__tinypy_class_validate(candidate_base);
    assert(tinypy_internal_value_vm(class_value) == tinypy_internal_value_vm(candidate_base));
    if (class_value == candidate_base) {
        return INT32_C(1);
    }
    for (index = 0U; index < tinypy_tuple_size(class_object->bases); index += 1U) {
        tinypy_value_t *item = tinypy_tuple_get(class_object->bases, index);
        if (tinypy_class_is_subclass(item, candidate_base) != 0) {
            return INT32_C(1);
        }
    }
    return INT32_C(0);
}

//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_class_bind(tinypy_value_t *class_value, tinypy_value_t *attribute, tinypy_value_t *instance) {
    tinypy_value_type_e kind = tinypy_internal_value_kind(attribute);

    if (kind == TINYPY_VALUE_FUNCTION) {
        return tinypy_method_new(attribute, instance, class_value);
    }
    if (kind == TINYPY_VALUE_STATIC_METHOD) {
        tinypy_value_t *callable = tinypy_static_method_callable(attribute);

        tinypy_retain(callable);
        return callable;
    }
    if (kind == TINYPY_VALUE_CLASS_METHOD) {
        tinypy_value_t *class_method_callable = tinypy_class_method_callable(attribute);
        return tinypy_method_new(class_method_callable, class_value, class_value);
    }
    tinypy_retain(attribute);
    return attribute;
}

//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_class_new(const char *name, size_t name_size, tinypy_value_t *bases, tinypy_value_t *namespace_dict, tinypy_error_t **out_error) {
    tinypy_vm_t *vm;
    tinypy_class_object_t *class_object;
    size_t index;

    assert(name != NULL || name_size == 0U);
    assert(bases != NULL);
    assert(namespace_dict != NULL);
    vm = tinypy_internal_value_vm(bases);
    assert(tinypy_internal_vm_valid(vm));
    assert(tinypy_internal_value_belongs_to(vm, bases));
    assert(tinypy_internal_value_belongs_to(vm, namespace_dict));
    assert(tinypy_internal_value_kind(bases) == TINYPY_VALUE_TUPLE);
    assert(tinypy_internal_value_kind(namespace_dict) == TINYPY_VALUE_DICT);
    tinypy_internal_clear_error(out_error);
    for (index = 0U; index < tinypy_tuple_size(bases); index += 1U) {
        size_t earlier;
        tinypy_value_t *base = tinypy_tuple_get(bases, index);

        if (tinypy_internal_value_kind(base) != TINYPY_VALUE_CLASS) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "classic class base is not a class", out_error);
            return NULL;
        }
        for (earlier = 0U; earlier < index; earlier += 1U) {
            if (tinypy_tuple_get(bases, earlier) == base) {
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "duplicate class base", out_error);
                return NULL;
            }
        }
    }
    class_object = (tinypy_class_object_t *)tinypy_internal_value_allocate(vm, TINYPY_VALUE_CLASS, sizeof(*class_object));
    class_object->name = tinypy_string_from_bytes(vm, name, name_size);
    class_object->bases = bases;
    class_object->dict = namespace_dict;
    tinypy_retain(bases);
    tinypy_retain(namespace_dict);
    return &class_object->base;
}

//////////////////////////////////////////////////////////////////////////
void tinypy_internal_class_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data) {
    tinypy_class_object_t *class_object = __tinypy_class_validate(value);

    visit(class_object->name, user_data);
    visit(class_object->bases, user_data);
    visit(class_object->dict, user_data);
}

//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_old_instance_new(tinypy_value_t *class_value) {
    tinypy_vm_t *vm;
    tinypy_old_instance_object_t *instance;

    (void)__tinypy_class_validate(class_value);
    vm = tinypy_internal_value_vm(class_value);
    instance = (tinypy_old_instance_object_t *)tinypy_internal_value_allocate(vm, TINYPY_VALUE_OLD_INSTANCE, sizeof(*instance));
    instance->class_object = class_value;
    instance->dict = tinypy_dict_new(vm);
    tinypy_retain(class_value);
    return &instance->base;
}

//////////////////////////////////////////////////////////////////////////
void tinypy_internal_old_instance_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data) {
    tinypy_old_instance_object_t *instance = __tinypy_old_instance_validate(value);

    visit(instance->class_object, user_data);
    visit(instance->dict, user_data);
}

//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_class_special_attribute(tinypy_value_t *class_value, tinypy_value_t *name) {
    tinypy_class_object_t *class_object = TINYPY_CLASS_OBJECT(class_value);
    tinypy_value_t *result = NULL;

    if (__tinypy_class_name_equal(name, "__name__", 8U) != 0) {
        result = class_object->name;
    }
    else if (__tinypy_class_name_equal(name, "__bases__", 9U) != 0) {
        result = class_object->bases;
    }
    else if (__tinypy_class_name_equal(name, "__dict__", 8U) != 0) {
        result = class_object->dict;
    }
    if (result != NULL) {
        tinypy_retain(result);
    }
    return result;
}

//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_class_get_attribute(tinypy_value_t *class_value, tinypy_value_t *name, tinypy_error_t **out_error) {
    size_t name_size;
    const char *name_bytes = (const char *)tinypy_string_view(name, &name_size);
    tinypy_value_t *attribute;
    tinypy_value_t *special;

    (void)__tinypy_class_validate(class_value);
    tinypy_internal_clear_error(out_error);
    special = __tinypy_class_special_attribute(class_value, name);
    if (special != NULL) {
        return special;
    }
    attribute = tinypy_internal_class_lookup(class_value, name_bytes, name_size);
    return attribute != NULL ? __tinypy_class_bind(class_value, attribute, NULL) : NULL;
}

//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_old_instance_get_direct(tinypy_value_t *instance_value, tinypy_value_t *name) {
    tinypy_old_instance_object_t *instance = TINYPY_OLD_INSTANCE_OBJECT(instance_value);
    size_t name_size;
    const char *name_bytes = (const char *)tinypy_string_view(name, &name_size);
    tinypy_value_t *attribute;

    if (__tinypy_class_name_equal(name, "__class__", 9U) != 0) {
        tinypy_retain(instance->class_object);
        return instance->class_object;
    }
    if (__tinypy_class_name_equal(name, "__dict__", 8U) != 0) {
        tinypy_retain(instance->dict);
        return instance->dict;
    }
    attribute = __tinypy_class_dict_lookup(instance->dict, name_bytes, name_size);
    if (attribute != NULL) {
        tinypy_retain(attribute);
        return attribute;
    }
    attribute = tinypy_internal_class_lookup(instance->class_object, name_bytes, name_size);
    return attribute != NULL ? __tinypy_class_bind(instance->class_object, attribute, instance_value) : NULL;
}

//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_old_instance_get_attribute(tinypy_value_t *instance_value, tinypy_value_t *name, tinypy_error_t **out_error) {
    tinypy_old_instance_object_t *instance = __tinypy_old_instance_validate(instance_value);
    tinypy_vm_t *vm = tinypy_internal_value_vm(instance_value);
    tinypy_value_t *result;

    tinypy_internal_clear_error(out_error);
    result = __tinypy_old_instance_get_direct(instance_value, name);
    if (result != NULL || __tinypy_class_name_equal(name, "__getattr__", 11U) != 0) {
        return result;
    } {
        tinypy_value_t *hook_attribute = tinypy_internal_class_lookup(instance->class_object, "__getattr__", 11U);
        tinypy_value_t *hook;
        tinypy_value_t *args;

        if (hook_attribute == NULL) {
            return NULL;
        }
        hook = __tinypy_class_bind(instance->class_object, hook_attribute, instance_value);
        args = tinypy_tuple_from_items(vm, &name, 1U);
        result = tinypy_call(hook, args, NULL, out_error);
        tinypy_release(args);
        tinypy_release(hook);
        return result;
    }
}

//////////////////////////////////////////////////////////////////////////
int32_t tinypy_internal_class_set_attribute(tinypy_value_t *class_value, tinypy_value_t *name, tinypy_value_t *attribute_value, tinypy_error_t **out_error) {
    tinypy_class_object_t *class_object = __tinypy_class_validate(class_value);

    tinypy_internal_clear_error(out_error);
    tinypy_dict_set(class_object->dict, name, attribute_value);
    return INT32_C(1);
}

//////////////////////////////////////////////////////////////////////////
int32_t tinypy_internal_old_instance_set_attribute(tinypy_value_t *instance_value, tinypy_value_t *name, tinypy_value_t *attribute_value, tinypy_error_t **out_error) {
    tinypy_old_instance_object_t *instance = __tinypy_old_instance_validate(instance_value);

    tinypy_internal_clear_error(out_error);
    tinypy_dict_set(instance->dict, name, attribute_value);
    return INT32_C(1);
}

//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_class_delete_from_dict(tinypy_value_t *dict, const char *name, size_t name_size, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = tinypy_internal_value_vm(dict);
    tinypy_value_t *key = tinypy_string_from_bytes(vm, name, name_size);

    if (tinypy_dict_contains(dict, key) == 0) {
        tinypy_release(key);
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_ATTRIBUTE, "attribute does not exist", out_error);
        return INT32_C(0);
    }
    tinypy_dict_delete(dict, key);
    tinypy_release(key);
    return INT32_C(1);
}

//////////////////////////////////////////////////////////////////////////
int32_t tinypy_internal_class_delete_attribute(tinypy_value_t *value, const char *name, size_t name_size, tinypy_error_t **out_error) {
    tinypy_class_object_t *class_validate = __tinypy_class_validate(value);
    return __tinypy_class_delete_from_dict(class_validate->dict, name, name_size, out_error);
}

//////////////////////////////////////////////////////////////////////////
int32_t tinypy_internal_old_instance_delete_attribute(tinypy_value_t *value, const char *name, size_t name_size, tinypy_error_t **out_error) {
    tinypy_old_instance_object_t *old_instance_validate = __tinypy_old_instance_validate(value);
    return __tinypy_class_delete_from_dict(old_instance_validate->dict, name, name_size, out_error);
}

//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_class_call(tinypy_value_t *callable, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = tinypy_internal_value_vm(callable);
    tinypy_value_t *instance = tinypy_old_instance_new(callable);
    tinypy_value_t *initializer_attribute = tinypy_internal_class_lookup(callable, "__init__", 8U);
    tinypy_value_t *initializer;
    tinypy_value_t *result;

    if (initializer_attribute == NULL) {
        if (tinypy_tuple_size(args) != 0U || (kwargs != NULL && tinypy_dict_size(kwargs) != 0U)) {
            tinypy_release(instance);
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "class constructor takes no arguments", out_error);
            return NULL;
        }
        return instance;
    }
    initializer = __tinypy_class_bind(callable, initializer_attribute, instance);
    result = tinypy_call(initializer, args, kwargs, out_error);
    tinypy_release(initializer);
    if (result == NULL) {
        tinypy_release(instance);
        return NULL;
    }
    if (tinypy_internal_value_kind(result) != TINYPY_VALUE_NONE) {
        tinypy_release(result);
        tinypy_release(instance);
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "__init__ must return None", out_error);
        return NULL;
    }
    tinypy_release(result);
    return instance;
}

//////////////////////////////////////////////////////////////////////////
int32_t tinypy_internal_old_instance_has_special(tinypy_value_t *value, const char *name, size_t name_size) {
    return tinypy_internal_value_kind(value) == TINYPY_VALUE_OLD_INSTANCE && tinypy_internal_class_lookup(TINYPY_OLD_INSTANCE_OBJECT(value)->class_object, name, name_size) != NULL ? INT32_C(1) : INT32_C(0);
}

//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_class_name(const tinypy_value_t *class_value) {
    return __tinypy_class_validate(class_value)->name;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_class_bases(const tinypy_value_t *class_value) {
    return __tinypy_class_validate(class_value)->bases;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_class_dict(const tinypy_value_t *class_value) {
    return __tinypy_class_validate(class_value)->dict;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_old_instance_class(const tinypy_value_t *instance) {
    return __tinypy_old_instance_validate(instance)->class_object;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_old_instance_dict(const tinypy_value_t *instance) {
    return __tinypy_old_instance_validate(instance)->dict;
}
