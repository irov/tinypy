#include "tinypy/weakref.h"

#include "internal.h"

#include <assert.h>

//////////////////////////////////////////////////////////////////////////
tinypy_value_t **tinypy_internal_weakref_head_slot(tinypy_value_t *value) {
    assert(value != NULL);
    if (value->type->weakref_offset != 0U) {
        return (tinypy_value_t **)((unsigned char *)value + value->type->weakref_offset);
    }
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_weakref_unlink(tinypy_weakref_object_t *weakref) {
    if (weakref->object == NULL) {
        return;
    }
    tinypy_value_t **head_slot = tinypy_internal_weakref_head_slot(weakref->object);
    assert(head_slot != NULL);
    if (weakref->previous != NULL) {
        TINYPY_WEAKREF_OBJECT(weakref->previous)->next = weakref->next;
    }
    else {
        assert(*head_slot == &weakref->base);
        *head_slot = weakref->next;
    }
    if (weakref->next != NULL) {
        TINYPY_WEAKREF_OBJECT(weakref->next)->previous = weakref->previous;
    }
    weakref->previous = NULL;
    weakref->next = NULL;
    weakref->object = NULL;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_weakref_new_with_type(tinypy_type_t *type, tinypy_value_t *object, tinypy_value_t *callback, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = type->vm;
    tinypy_value_t **head_slot = tinypy_internal_weakref_head_slot(object);

    if (head_slot == NULL) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "cannot create weak reference to this object", out_error);
        return NULL;
    }
    if (callback != NULL && callback->type->call == NULL) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "weak reference callback must be callable", out_error);
        return NULL;
    }
    tinypy_weakref_object_t *weakref = (tinypy_weakref_object_t *)tinypy_internal_object_allocate(vm, type, type->basic_size);
    weakref->object = object;
    weakref->callback = callback;
    weakref->next = *head_slot;
    if (callback != NULL) {
        TINYPY_INCREF(callback);
    }
    if (*head_slot != NULL) {
        TINYPY_WEAKREF_OBJECT(*head_slot)->previous = &weakref->base;
    }
    *head_slot = &weakref->base;
    return &weakref->base;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_weakref_new(tinypy_value_t *object, tinypy_value_t *callback, tinypy_error_t **out_error) {
    assert(object != NULL);
    tinypy_vm_t *vm = TINYPY_VALUE_VM(object);
    assert(tinypy_internal_vm_valid(vm));
    assert(callback == NULL || tinypy_internal_value_belongs_to(vm, callback));
    TINYPY_CLEAR_ERROR(out_error);
    return __tinypy_weakref_new_with_type(&vm->weakref_type, object, callback, out_error);
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_weakref_get(const tinypy_value_t *value) {
    assert(value != NULL);
    assert(tinypy_internal_vm_valid(TINYPY_VALUE_VM(value)));
    assert(TINYPY_VALUE_KIND(value) == TINYPY_VALUE_WEAKREF);
    return TINYPY_WEAKREF_OBJECT((tinypy_value_t *)value)->object;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_weakref_clear(tinypy_value_t *value) {
    tinypy_value_t **head_slot = tinypy_internal_weakref_head_slot(value);

    if (head_slot == NULL || *head_slot == NULL) {
        return;
    }
    tinypy_value_t *weakref_value = *head_slot;
    *head_slot = NULL;
    tinypy_value_t *current = weakref_value;
    while (current != NULL) {
        tinypy_weakref_object_t *weakref = TINYPY_WEAKREF_OBJECT(current);

        TINYPY_INCREF(current);
        weakref->object = NULL;
        weakref->previous = NULL;
        current = weakref->next;
    }
    current = weakref_value;
    while (current != NULL) {
        tinypy_weakref_object_t *weakref = TINYPY_WEAKREF_OBJECT(current);
        tinypy_value_t *next = weakref->next;

        weakref->next = NULL;
        if (weakref->callback != NULL) {
            tinypy_vm_t *vm = TINYPY_VALUE_VM(current);
            tinypy_internal_exception_state_t exception_state;
            tinypy_value_t *args = tinypy_tuple_from_items(vm, &current, 1U);
            tinypy_error_t *error = NULL;
            tinypy_value_t *result;

            tinypy_internal_exception_preserve_begin(vm, &exception_state);
            result = tinypy_call(weakref->callback, args, NULL, &error);
            TINYPY_DECREF(args);
            if (result != NULL) {
                TINYPY_DECREF(result);
            }
            if (error != NULL) {
                tinypy_error_release(error);
            }
            tinypy_internal_exception_preserve_end(vm, &exception_state);
        }
        TINYPY_DECREF(current);
        current = next;
    }
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_weakref_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data) {
    tinypy_weakref_object_t *weakref = TINYPY_WEAKREF_OBJECT(value);

    if (weakref->callback != NULL) {
        visit(weakref->callback, user_data);
    }
    if (weakref->dict != NULL) {
        visit(weakref->dict, user_data);
    }
    for (size_t index = 0U; index < value->type->slot_count; ++index) {
        tinypy_value_t **slot = tinypy_internal_object_member_slot(value, index);

        if (*slot != NULL) {
            visit(*slot, user_data);
        }
    }
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_weakref_destroy(tinypy_value_t *value) {
    __tinypy_weakref_unlink(TINYPY_WEAKREF_OBJECT(value));
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_weakref_arguments(tinypy_vm_t *vm, tinypy_value_t *args, tinypy_value_t *kwargs, size_t minimum, size_t maximum, tinypy_error_t **out_error) {
    size_t count = TINYPY_TUPLE_SIZE(args);

    if ((kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U) || count < minimum || count > maximum) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "weakref function received invalid arguments", out_error);
        return INT32_C(0);
    }
    return INT32_C(1);
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_weakref_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_value_t *callback = NULL;

    if (__tinypy_weakref_arguments(type->vm, args, kwargs, 1U, 2U, out_error) == 0) {
        return NULL;
    }
    int condition = TINYPY_TUPLE_SIZE(args) == 2U;
    if (condition != 0) {
        tinypy_value_t *item_2 = TINYPY_TUPLE_GET(args, 1U);
        condition = TINYPY_VALUE_KIND(item_2) != TINYPY_VALUE_NONE;
    }
    if (condition) {
        callback = TINYPY_TUPLE_GET(args, 1U);
    }
    tinypy_value_t *item = TINYPY_TUPLE_GET(args, 0U);
    return __tinypy_weakref_new_with_type(type, item, callback, out_error);
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_weakref_call(tinypy_value_t *callable, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(callable);

    if (__tinypy_weakref_arguments(vm, args, kwargs, 0U, 0U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *object = TINYPY_WEAKREF_OBJECT(callable)->object;
    if (object == NULL) {
        return tinypy_none_get(vm);
    }
    TINYPY_INCREF(object);
    return object;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_weakref_new_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *callback = NULL;

    (void)user_data;
    if (__tinypy_weakref_arguments(vm, args, kwargs, 2U, 3U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *type_value = TINYPY_TUPLE_GET(args, 0U);
    if (TINYPY_VALUE_KIND(type_value) != TINYPY_VALUE_TYPE || tinypy_type_is_subtype((tinypy_type_t *)type_value, &vm->weakref_type) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "weakref.__new__ requires a weakref subtype", out_error);
        return NULL;
    }
    int condition_2 = TINYPY_TUPLE_SIZE(args) == 3U;
    if (condition_2 != 0) {
        tinypy_value_t *item_2 = TINYPY_TUPLE_GET(args, 2U);
        condition_2 = TINYPY_VALUE_KIND(item_2) != TINYPY_VALUE_NONE;
    }
    if (condition_2) {
        callback = TINYPY_TUPLE_GET(args, 2U);
    }
    tinypy_value_t *item = TINYPY_TUPLE_GET(args, 1U);
    return __tinypy_weakref_new_with_type((tinypy_type_t *)type_value, item, callback, out_error);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_weakref_init_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_weakref_arguments(vm, args, kwargs, 2U, 3U, out_error) == 0) {
        return NULL;
    }
    return tinypy_none_get(vm);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_weakref_hash_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_weakref_arguments(vm, args, kwargs, 1U, 1U, out_error) == 0) {
        return NULL;
    }
    tinypy_weakref_object_t *weakref = TINYPY_WEAKREF_OBJECT(TINYPY_TUPLE_GET(args, 0U));
    if (weakref->hash_computed == 0) {
        if (weakref->object == NULL) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "weak object has gone away", out_error);
            return NULL;
        }
        weakref->hash = tinypy_hash(weakref->object);
        weakref->hash_computed = INT32_C(1);
    }
    return tinypy_integer_from_i64(vm, (int64_t)weakref->hash);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_weakref_add_type_method(tinypy_vm_t *vm, const char *name, size_t name_size, tinypy_native_function_callback_t callback, int32_t static_method) {
    tinypy_value_t *function = tinypy_native_function_new(vm, name, name_size, callback, NULL, NULL);
    tinypy_value_t *attribute = static_method != 0 ? tinypy_static_method_new(function) : function;
    tinypy_value_t *key = tinypy_string_from_bytes(vm, name, name_size);

    tinypy_dict_set(vm->weakref_type.dict, key, attribute);
    TINYPY_DECREF(key);
    if (attribute != function) {
        TINYPY_DECREF(attribute);
    }
    TINYPY_DECREF(function);
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_initialize_weakref_type(tinypy_vm_t *vm) {
    __tinypy_weakref_add_type_method(vm, "__new__", 7U, __tinypy_weakref_new_method, INT32_C(1));
    __tinypy_weakref_add_type_method(vm, "__init__", 8U, __tinypy_weakref_init_method, INT32_C(0));
    __tinypy_weakref_add_type_method(vm, "__hash__", 8U, __tinypy_weakref_hash_method, INT32_C(0));
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_weakref_count_function(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    int64_t count = INT64_C(0);

    (void)user_data;
    if (__tinypy_weakref_arguments(vm, args, kwargs, 1U, 1U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *item = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t **head_slot = tinypy_internal_weakref_head_slot(item);
    tinypy_value_t *current = head_slot != NULL ? *head_slot : NULL;
    while (current != NULL) {
        count += INT64_C(1);
        current = TINYPY_WEAKREF_OBJECT(current)->next;
    }
    return tinypy_integer_from_i64(vm, count);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_weakref_list_function(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_weakref_arguments(vm, args, kwargs, 1U, 1U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *result = tinypy_list_from_items(vm, NULL, 0U);
    tinypy_value_t *item = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t **head_slot = tinypy_internal_weakref_head_slot(item);
    tinypy_value_t *current = head_slot != NULL ? *head_slot : NULL;
    while (current != NULL) {
        tinypy_list_append(result, current);
        current = TINYPY_WEAKREF_OBJECT(current)->next;
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_weakref_proxy_function(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_value_t *callback = NULL;

    (void)user_data;
    if (__tinypy_weakref_arguments(vm, args, kwargs, 1U, 2U, out_error) == 0) {
        return NULL;
    }
    int condition_3 = TINYPY_TUPLE_SIZE(args) == 2U;
    if (condition_3 != 0) {
        tinypy_value_t *item_2 = TINYPY_TUPLE_GET(args, 1U);
        condition_3 = TINYPY_VALUE_KIND(item_2) != TINYPY_VALUE_NONE;
    }
    if (condition_3) {
        callback = TINYPY_TUPLE_GET(args, 1U);
    }
    tinypy_value_t *item = TINYPY_TUPLE_GET(args, 0U);
    return tinypy_weakref_new(item, callback, out_error);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_weakref_remove_function(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_weakref_arguments(vm, args, kwargs, 2U, 2U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *dict = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *key = TINYPY_TUPLE_GET(args, 1U);
    if (TINYPY_VALUE_KIND(dict) != TINYPY_VALUE_DICT) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "weakref removal requires a dictionary", out_error);
        return NULL;
    }
    tinypy_value_t *candidate = tinypy_dict_get_optional(dict, key);

    if (candidate != NULL) {
        if (TINYPY_VALUE_KIND(candidate) == TINYPY_VALUE_WEAKREF && TINYPY_WEAKREF_OBJECT(candidate)->object == NULL) {
            tinypy_dict_delete(dict, key);
        }
    }
    return tinypy_none_get(vm);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_weakref_add_module_function(tinypy_vm_t *vm, tinypy_value_t *module, const char *name, size_t name_size, tinypy_native_function_callback_t callback) {
    tinypy_value_t *function = tinypy_native_function_new(vm, name, name_size, callback, NULL, NULL);

    tinypy_module_add_value(module, name, name_size, function);
    TINYPY_DECREF(function);
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_initialize_weakref_module(tinypy_vm_t *vm) {
    tinypy_value_t *module = tinypy_module_new(vm, "_weakref", 8U);
    tinypy_value_t *name = tinypy_string_from_bytes(vm, "_weakref", 8U);

    tinypy_module_add_value(module, "__name__", 8U, name);
    TINYPY_DECREF(name);
    tinypy_module_add_value(module, "ReferenceType", 13U, &vm->weakref_type.base.base);
    tinypy_module_add_value(module, "ProxyType", 9U, &vm->weakref_type.base.base);
    tinypy_module_add_value(module, "CallableProxyType", 17U, &vm->weakref_type.base.base);
    tinypy_module_add_value(module, "ref", 3U, &vm->weakref_type.base.base);
    __tinypy_weakref_add_module_function(vm, module, "proxy", 5U, __tinypy_weakref_proxy_function);
    __tinypy_weakref_add_module_function(vm, module, "getweakrefcount", 15U, __tinypy_weakref_count_function);
    __tinypy_weakref_add_module_function(vm, module, "getweakrefs", 11U, __tinypy_weakref_list_function);
    __tinypy_weakref_add_module_function(vm, module, "_remove_dead_weakref", 20U, __tinypy_weakref_remove_function);
    tinypy_internal_register_module(vm, "_weakref", 8U, module);
    TINYPY_DECREF(module);
}
