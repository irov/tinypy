#include "tinypy/class.h"

#include "internal.h"

#include <assert.h>
#include <string.h>

//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_class_name_equal(tinypy_value_t *name, const char *expected, size_t expected_size) {
    size_t name_size;
    const unsigned char *bytes = (const unsigned char *)tinypy_string_view(name, &name_size);

    return name_size == expected_size && (name_size == 0U || memcmp(bytes, expected, name_size) == 0) ? INT32_C(1) : INT32_C(0);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_class_lookup_key(tinypy_vm_t *vm, tinypy_value_t *class_value, tinypy_value_t *key) {
    tinypy_value_t *const *iterator;
    tinypy_value_t *const *iterator_end;

    assert(class_value != NULL);
    assert(TINYPY_VALUE_KIND(class_value) == TINYPY_VALUE_CLASS);
    tinypy_class_object_t *class_object = TINYPY_CLASS_OBJECT(class_value);
    tinypy_value_t *attribute = tinypy_internal_dict_get_optional(vm, class_object->dict, key);
    if (attribute != NULL) {
        return attribute;
    }
    iterator = TINYPY_TUPLE_ITERATOR_BEGIN(class_object->bases);
    iterator_end = TINYPY_TUPLE_ITERATOR_END(class_object->bases);
    for (; iterator != iterator_end; ++iterator) {
        tinypy_value_t *item = *iterator;
        attribute = __tinypy_class_lookup_key(vm, item, key);
        if (attribute != NULL) {
            return attribute;
        }
    }
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_class_lookup(tinypy_value_t *class_value, const char *name, size_t name_size) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(class_value);
    tinypy_value_t *key = tinypy_string_from_bytes(vm, name, name_size);
    tinypy_value_t *result = __tinypy_class_lookup_key(vm, class_value, key);

    TINYPY_DECREF(key);
    return result;
}
//////////////////////////////////////////////////////////////////////////
int32_t tinypy_class_is_subclass(const tinypy_value_t *class_value, const tinypy_value_t *candidate_base) {
    tinypy_value_t *const *iterator;
    tinypy_value_t *const *iterator_end;

    assert(class_value != NULL);
    assert(TINYPY_VALUE_KIND(class_value) == TINYPY_VALUE_CLASS);
    assert(candidate_base != NULL);
    assert(TINYPY_VALUE_KIND(candidate_base) == TINYPY_VALUE_CLASS);
    assert(TINYPY_VALUE_VM(class_value) == TINYPY_VALUE_VM(candidate_base));
    tinypy_class_object_t *class_object = TINYPY_CLASS_OBJECT((tinypy_value_t *)class_value);
    if (class_value == candidate_base) {
        return INT32_C(1);
    }
    iterator = TINYPY_TUPLE_ITERATOR_BEGIN(class_object->bases);
    iterator_end = TINYPY_TUPLE_ITERATOR_END(class_object->bases);
    for (; iterator != iterator_end; ++iterator) {
        tinypy_value_t *item = *iterator;
        if (tinypy_class_is_subclass(item, candidate_base) != 0) {
            return INT32_C(1);
        }
    }
    return INT32_C(0);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_class_bind(tinypy_value_t *class_value, tinypy_value_t *attribute, tinypy_value_t *instance) {
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(attribute);

    if (kind == TINYPY_VALUE_FUNCTION) {
        return tinypy_method_new(attribute, instance, class_value);
    }
    if (kind == TINYPY_VALUE_STATIC_METHOD) {
        tinypy_value_t *callable = tinypy_static_method_callable(attribute);

        TINYPY_INCREF(callable);
        return callable;
    }
    if (kind == TINYPY_VALUE_CLASS_METHOD) {
        tinypy_value_t *class_method_callable = tinypy_class_method_callable(attribute);
        return tinypy_method_new(class_method_callable, class_value, class_value);
    }
    TINYPY_INCREF(attribute);
    return attribute;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_class_new(const char *name, size_t name_size, tinypy_value_t *bases, tinypy_value_t *namespace_dict, tinypy_error_t **out_error) {
    tinypy_value_t *const *iterator;
    tinypy_value_t *const *iterator_end;

    assert(name != NULL || name_size == 0U);
    assert(bases != NULL);
    assert(namespace_dict != NULL);
    tinypy_vm_t *vm = TINYPY_VALUE_VM(bases);
    assert(tinypy_internal_vm_valid(vm));
    assert(tinypy_internal_value_belongs_to(vm, bases));
    assert(tinypy_internal_value_belongs_to(vm, namespace_dict));
    assert(TINYPY_VALUE_KIND(bases) == TINYPY_VALUE_TUPLE);
    assert(TINYPY_VALUE_KIND(namespace_dict) == TINYPY_VALUE_DICT);
    TINYPY_CLEAR_ERROR(out_error);
    iterator = TINYPY_TUPLE_ITERATOR_BEGIN(bases);
    iterator_end = TINYPY_TUPLE_ITERATOR_END(bases);
    for (; iterator != iterator_end; ++iterator) {
        tinypy_value_t *const *earlier;
        tinypy_value_t *base = *iterator;

        if (TINYPY_VALUE_KIND(base) != TINYPY_VALUE_CLASS) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "classic class base is not a class", out_error);
            return NULL;
        }
        for (earlier = TINYPY_TUPLE_ITERATOR_BEGIN(bases); earlier != iterator; ++earlier) {
            if (*earlier == base) {
                tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "duplicate class base", out_error);
                return NULL;
            }
        }
    }
    tinypy_class_object_t *class_object = (tinypy_class_object_t *)tinypy_internal_value_allocate(vm, TINYPY_VALUE_CLASS, sizeof(*class_object));
    class_object->name = tinypy_string_from_bytes(vm, name, name_size);
    class_object->bases = bases;
    class_object->dict = namespace_dict;
    TINYPY_INCREF(bases);
    TINYPY_INCREF(namespace_dict);
    return &class_object->base;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_class_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data) {
    assert(value != NULL);
    assert(TINYPY_VALUE_KIND(value) == TINYPY_VALUE_CLASS);
    tinypy_class_object_t *class_object = TINYPY_CLASS_OBJECT(value);
    visit(class_object->name, user_data);
    visit(class_object->bases, user_data);
    visit(class_object->dict, user_data);
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_old_instance_new(tinypy_value_t *class_value) {
    assert(class_value != NULL);
    assert(TINYPY_VALUE_KIND(class_value) == TINYPY_VALUE_CLASS);
    tinypy_vm_t *vm = TINYPY_VALUE_VM(class_value);
    tinypy_old_instance_object_t *instance = (tinypy_old_instance_object_t *)tinypy_internal_value_allocate(vm, TINYPY_VALUE_OLD_INSTANCE, sizeof(*instance));
    instance->class_object = class_value;
    instance->dict = tinypy_dict_new(vm);
    TINYPY_INCREF(class_value);
    return &instance->base;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_old_instance_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data) {
    assert(value != NULL);
    assert(TINYPY_VALUE_KIND(value) == TINYPY_VALUE_OLD_INSTANCE);
    tinypy_old_instance_object_t *instance = TINYPY_OLD_INSTANCE_OBJECT(value);
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
        TINYPY_INCREF(result);
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_class_get_attribute(tinypy_value_t *class_value, tinypy_value_t *name, tinypy_error_t **out_error) {
    assert(class_value != NULL);
    assert(TINYPY_VALUE_KIND(class_value) == TINYPY_VALUE_CLASS);
    tinypy_vm_t *vm = TINYPY_VALUE_VM(class_value);
    TINYPY_CLEAR_ERROR(out_error);
    tinypy_value_t *special = __tinypy_class_special_attribute(class_value, name);
    if (special != NULL) {
        return special;
    }
    tinypy_value_t *attribute = __tinypy_class_lookup_key(vm, class_value, name);
    return attribute != NULL ? __tinypy_class_bind(class_value, attribute, NULL) : NULL;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_old_instance_get_direct(tinypy_vm_t *vm, tinypy_value_t *instance_value, tinypy_value_t *name) {
    tinypy_old_instance_object_t *instance = TINYPY_OLD_INSTANCE_OBJECT(instance_value);

    if (__tinypy_class_name_equal(name, "__class__", 9U) != 0) {
        TINYPY_INCREF(instance->class_object);
        return instance->class_object;
    }
    if (__tinypy_class_name_equal(name, "__dict__", 8U) != 0) {
        TINYPY_INCREF(instance->dict);
        return instance->dict;
    }
    tinypy_value_t *attribute = tinypy_internal_dict_get_optional(vm, instance->dict, name);
    if (attribute != NULL) {
        TINYPY_INCREF(attribute);
        return attribute;
    }
    attribute = __tinypy_class_lookup_key(vm, instance->class_object, name);
    return attribute != NULL ? __tinypy_class_bind(instance->class_object, attribute, instance_value) : NULL;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_old_instance_get_attribute(tinypy_value_t *instance_value, tinypy_value_t *name, tinypy_error_t **out_error) {
    assert(instance_value != NULL);
    assert(TINYPY_VALUE_KIND(instance_value) == TINYPY_VALUE_OLD_INSTANCE);
    tinypy_vm_t *vm = TINYPY_VALUE_VM(instance_value);
    tinypy_old_instance_object_t *instance = TINYPY_OLD_INSTANCE_OBJECT(instance_value);
    TINYPY_CLEAR_ERROR(out_error);
    tinypy_value_t *result = __tinypy_old_instance_get_direct(vm, instance_value, name);
    if (result != NULL || __tinypy_class_name_equal(name, "__getattr__", 11U) != 0) {
        return result;
    } {
        tinypy_value_t *hook_attribute = __tinypy_class_lookup_key(vm, instance->class_object, vm->special_getattr_key);
        tinypy_value_t *hook;
        tinypy_value_t *args;

        if (hook_attribute == NULL) {
            return NULL;
        }
        hook = __tinypy_class_bind(instance->class_object, hook_attribute, instance_value);
        args = tinypy_tuple_from_items(vm, &name, 1U);
        result = tinypy_call(hook, args, NULL, out_error);
        TINYPY_DECREF(args);
        TINYPY_DECREF(hook);
        return result;
    }
}
//////////////////////////////////////////////////////////////////////////
int32_t tinypy_internal_class_set_attribute(tinypy_value_t *class_value, tinypy_value_t *name, tinypy_value_t *attribute_value, tinypy_error_t **out_error) {
    assert(class_value != NULL);
    assert(TINYPY_VALUE_KIND(class_value) == TINYPY_VALUE_CLASS);
    tinypy_class_object_t *class_object = TINYPY_CLASS_OBJECT(class_value);
    TINYPY_CLEAR_ERROR(out_error);
    tinypy_dict_set(class_object->dict, name, attribute_value);
    return INT32_C(1);
}
//////////////////////////////////////////////////////////////////////////
int32_t tinypy_internal_old_instance_set_attribute(tinypy_value_t *instance_value, tinypy_value_t *name, tinypy_value_t *attribute_value, tinypy_error_t **out_error) {
    assert(instance_value != NULL);
    assert(TINYPY_VALUE_KIND(instance_value) == TINYPY_VALUE_OLD_INSTANCE);
    tinypy_old_instance_object_t *instance = TINYPY_OLD_INSTANCE_OBJECT(instance_value);
    TINYPY_CLEAR_ERROR(out_error);
    tinypy_dict_set(instance->dict, name, attribute_value);
    return INT32_C(1);
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_class_delete_from_dict(tinypy_vm_t *vm, tinypy_value_t *dict, tinypy_value_t *name, tinypy_error_t **out_error) {
    if (tinypy_internal_dict_delete_optional(vm, dict, name) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_ATTRIBUTE, "attribute does not exist", out_error);
        return INT32_C(0);
    }
    return INT32_C(1);
}
//////////////////////////////////////////////////////////////////////////
int32_t tinypy_internal_class_delete_attribute(tinypy_value_t *value, tinypy_value_t *name, tinypy_error_t **out_error) {
    assert(value != NULL);
    assert(TINYPY_VALUE_KIND(value) == TINYPY_VALUE_CLASS);
    return __tinypy_class_delete_from_dict(TINYPY_VALUE_VM(value), TINYPY_CLASS_OBJECT(value)->dict, name, out_error);
}
//////////////////////////////////////////////////////////////////////////
int32_t tinypy_internal_old_instance_delete_attribute(tinypy_value_t *value, tinypy_value_t *name, tinypy_error_t **out_error) {
    assert(value != NULL);
    assert(TINYPY_VALUE_KIND(value) == TINYPY_VALUE_OLD_INSTANCE);
    return __tinypy_class_delete_from_dict(TINYPY_VALUE_VM(value), TINYPY_OLD_INSTANCE_OBJECT(value)->dict, name, out_error);
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_class_call(tinypy_value_t *callable, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(callable);
    tinypy_value_t *instance = tinypy_old_instance_new(callable);
    tinypy_value_t *initializer_attribute = tinypy_internal_class_lookup(callable, "__init__", 8U);

    if (initializer_attribute == NULL) {
        if (TINYPY_TUPLE_SIZE(args) != 0U || (kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U)) {
            TINYPY_DECREF(instance);
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "class constructor takes no arguments", out_error);
            return NULL;
        }
        return instance;
    }
    tinypy_value_t *initializer = __tinypy_class_bind(callable, initializer_attribute, instance);
    tinypy_value_t *result = tinypy_call(initializer, args, kwargs, out_error);
    TINYPY_DECREF(initializer);
    if (result == NULL) {
        TINYPY_DECREF(instance);
        return NULL;
    }
    if (TINYPY_VALUE_KIND(result) != TINYPY_VALUE_NONE) {
        TINYPY_DECREF(result);
        TINYPY_DECREF(instance);
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "__init__ must return None", out_error);
        return NULL;
    }
    TINYPY_DECREF(result);
    return instance;
}
//////////////////////////////////////////////////////////////////////////
int32_t tinypy_internal_old_instance_has_special(tinypy_value_t *value, const char *name, size_t name_size) {
    return TINYPY_VALUE_KIND(value) == TINYPY_VALUE_OLD_INSTANCE && tinypy_internal_class_lookup(TINYPY_OLD_INSTANCE_OBJECT(value)->class_object, name, name_size) != NULL ? INT32_C(1) : INT32_C(0);
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_class_name(const tinypy_value_t *class_value) {
    assert(class_value != NULL);
    assert(TINYPY_VALUE_KIND(class_value) == TINYPY_VALUE_CLASS);
    return TINYPY_CLASS_OBJECT((tinypy_value_t *)class_value)->name;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_class_bases(const tinypy_value_t *class_value) {
    assert(class_value != NULL);
    assert(TINYPY_VALUE_KIND(class_value) == TINYPY_VALUE_CLASS);
    return TINYPY_CLASS_OBJECT((tinypy_value_t *)class_value)->bases;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_class_dict(const tinypy_value_t *class_value) {
    assert(class_value != NULL);
    assert(TINYPY_VALUE_KIND(class_value) == TINYPY_VALUE_CLASS);
    return TINYPY_CLASS_OBJECT((tinypy_value_t *)class_value)->dict;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_old_instance_class(const tinypy_value_t *instance) {
    assert(instance != NULL);
    assert(TINYPY_VALUE_KIND(instance) == TINYPY_VALUE_OLD_INSTANCE);
    return TINYPY_OLD_INSTANCE_OBJECT((tinypy_value_t *)instance)->class_object;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_old_instance_dict(const tinypy_value_t *instance) {
    assert(instance != NULL);
    assert(TINYPY_VALUE_KIND(instance) == TINYPY_VALUE_OLD_INSTANCE);
    return TINYPY_OLD_INSTANCE_OBJECT((tinypy_value_t *)instance)->dict;
}
