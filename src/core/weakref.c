#include "tinypy/weakref.h"

#include "internal.h"

#include <assert.h>

tinypy_value_t **tinypy_internal_weakref_head_slot(tinypy_value_t *value)
{
    tinypy_value_type_e kind;

    assert(value != NULL);
    kind = tinypy_internal_value_kind(value);
    if (kind == TINYPY_VALUE_TYPE) return &((tinypy_type_t *)value)->weakrefs;
    if (kind == TINYPY_VALUE_CLASS) return &TINYPY_CLASS_OBJECT(value)->weakrefs;
    if (kind == TINYPY_VALUE_OLD_INSTANCE) return &TINYPY_OLD_INSTANCE_OBJECT(value)->weakrefs;
    if (value->type->weakref_offset != 0U) return (tinypy_value_t **)((unsigned char *)value + value->type->weakref_offset);
    return NULL;
}

static void __tinypy_weakref_unlink(tinypy_weakref_object_t *weakref)
{
    tinypy_value_t **head_slot;

    if (weakref->object == NULL) return;
    head_slot = tinypy_internal_weakref_head_slot(weakref->object);
    assert(head_slot != NULL);
    if (weakref->previous != NULL) TINYPY_WEAKREF_OBJECT(weakref->previous)->next = weakref->next;
    else {
        assert(*head_slot == &weakref->base);
        *head_slot = weakref->next;
    }
    if (weakref->next != NULL) TINYPY_WEAKREF_OBJECT(weakref->next)->previous = weakref->previous;
    weakref->previous = NULL;
    weakref->next = NULL;
    weakref->object = NULL;
}

static tinypy_value_t *__tinypy_weakref_new_with_type(tinypy_type_t *type, tinypy_value_t *object, tinypy_value_t *callback, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = type->vm;
    tinypy_value_t **head_slot = tinypy_internal_weakref_head_slot(object);
    tinypy_weakref_object_t *weakref;

    if (head_slot == NULL) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "cannot create weak reference to this object", out_error);
        return NULL;
    }
    if (callback != NULL && callback->type->call == NULL) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "weak reference callback must be callable", out_error);
        return NULL;
    }
    weakref = (tinypy_weakref_object_t *)tinypy_internal_object_allocate(vm, type, type->basic_size);
    weakref->object = object;
    weakref->callback = callback;
    weakref->next = *head_slot;
    if (callback != NULL) tinypy_retain(callback);
    if (*head_slot != NULL) TINYPY_WEAKREF_OBJECT(*head_slot)->previous = &weakref->base;
    *head_slot = &weakref->base;
    return &weakref->base;
}

tinypy_value_t *tinypy_weakref_new(tinypy_value_t *object, tinypy_value_t *callback, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm;

    assert(object != NULL);
    vm = tinypy_internal_value_vm(object);
    assert(tinypy_internal_vm_valid(vm));
    assert(callback == NULL || tinypy_internal_value_belongs_to(vm, callback));
    tinypy_internal_clear_error(out_error);
    return __tinypy_weakref_new_with_type(&vm->weakref_type, object, callback, out_error);
}

tinypy_value_t *tinypy_weakref_get(const tinypy_value_t *value)
{
    assert(value != NULL);
    assert(tinypy_internal_vm_valid(tinypy_internal_value_vm(value)));
    assert(tinypy_internal_value_kind(value) == TINYPY_VALUE_WEAKREF);
    return TINYPY_WEAKREF_OBJECT((tinypy_value_t *)value)->object;
}

void tinypy_internal_weakref_clear(tinypy_value_t *value)
{
    tinypy_value_t **head_slot = tinypy_internal_weakref_head_slot(value);
    tinypy_value_t *weakref_value;
    tinypy_value_t *current;

    if (head_slot == NULL || *head_slot == NULL) return;
    weakref_value = *head_slot;
    *head_slot = NULL;
    current = weakref_value;
    while (current != NULL) {
        tinypy_weakref_object_t *weakref = TINYPY_WEAKREF_OBJECT(current);

        tinypy_retain(current);
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
            tinypy_vm_t *vm = tinypy_internal_value_vm(current);
            tinypy_internal_exception_state_t exception_state;
            tinypy_value_t *args = tinypy_tuple_from_items(vm, &current, 1U);
            tinypy_error_t *error = NULL;
            tinypy_value_t *result;

            tinypy_internal_exception_preserve_begin(vm, &exception_state);
            result = tinypy_call(weakref->callback, args, NULL, &error);
            tinypy_release(args);
            if (result != NULL) tinypy_release(result);
            if (error != NULL) tinypy_error_release(error);
            tinypy_internal_exception_preserve_end(vm, &exception_state);
        }
        tinypy_release(current);
        current = next;
    }
}

void tinypy_internal_weakref_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data)
{
    tinypy_weakref_object_t *weakref = TINYPY_WEAKREF_OBJECT(value);
    size_t index;

    if (weakref->callback != NULL) visit(weakref->callback, user_data);
    if (weakref->dict != NULL) visit(weakref->dict, user_data);
    for (index = 0U; index < value->type->slot_count; index += 1U) {
        tinypy_value_t **slot = tinypy_internal_object_member_slot(value, index);

        if (*slot != NULL) visit(*slot, user_data);
    }
}

void tinypy_internal_weakref_destroy(tinypy_vm_t *vm, tinypy_value_t *value)
{
    (void)vm;
    __tinypy_weakref_unlink(TINYPY_WEAKREF_OBJECT(value));
}

static int32_t __tinypy_weakref_arguments(tinypy_vm_t *vm, tinypy_value_t *args, tinypy_value_t *kwargs, size_t minimum, size_t maximum, tinypy_error_t **out_error)
{
    size_t count = tinypy_tuple_size(args);

    if ((kwargs != NULL && tinypy_dict_size(kwargs) != 0U) || count < minimum || count > maximum) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "weakref function received invalid arguments", out_error);
        return INT32_C(0);
    }
    return INT32_C(1);
}

tinypy_value_t *tinypy_internal_weakref_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error)
{
    tinypy_value_t *callback = NULL;

    if (__tinypy_weakref_arguments(type->vm, args, kwargs, 1U, 2U, out_error) == 0) return NULL;
    if (tinypy_tuple_size(args) == 2U && tinypy_internal_value_kind(tinypy_tuple_get(args, 1U)) != TINYPY_VALUE_NONE) callback = tinypy_tuple_get(args, 1U);
    return __tinypy_weakref_new_with_type(type, tinypy_tuple_get(args, 0U), callback, out_error);
}

tinypy_value_t *tinypy_internal_weakref_call(tinypy_value_t *callable, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = tinypy_internal_value_vm(callable);
    tinypy_value_t *object;

    if (__tinypy_weakref_arguments(vm, args, kwargs, 0U, 0U, out_error) == 0) return NULL;
    object = TINYPY_WEAKREF_OBJECT(callable)->object;
    if (object == NULL) return tinypy_none_get(vm);
    tinypy_retain(object);
    return object;
}

static tinypy_value_t *__tinypy_weakref_new_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);
    tinypy_value_t *type_value;
    tinypy_value_t *callback = NULL;

    (void)user_data;
    if (__tinypy_weakref_arguments(vm, args, kwargs, 2U, 3U, out_error) == 0) return NULL;
    type_value = tinypy_tuple_get(args, 0U);
    if (tinypy_internal_value_kind(type_value) != TINYPY_VALUE_TYPE || tinypy_type_is_subtype((tinypy_type_t *)type_value, &vm->weakref_type) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "weakref.__new__ requires a weakref subtype", out_error);
        return NULL;
    }
    if (tinypy_tuple_size(args) == 3U && tinypy_internal_value_kind(tinypy_tuple_get(args, 2U)) != TINYPY_VALUE_NONE) callback = tinypy_tuple_get(args, 2U);
    return __tinypy_weakref_new_with_type((tinypy_type_t *)type_value, tinypy_tuple_get(args, 1U), callback, out_error);
}

static tinypy_value_t *__tinypy_weakref_init_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);

    (void)user_data;
    if (__tinypy_weakref_arguments(vm, args, kwargs, 2U, 3U, out_error) == 0) return NULL;
    return tinypy_none_get(vm);
}

static tinypy_value_t *__tinypy_weakref_hash_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);
    tinypy_weakref_object_t *weakref;

    (void)user_data;
    if (__tinypy_weakref_arguments(vm, args, kwargs, 1U, 1U, out_error) == 0) return NULL;
    weakref = TINYPY_WEAKREF_OBJECT(tinypy_tuple_get(args, 0U));
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

static void __tinypy_weakref_add_type_method(tinypy_vm_t *vm, const char *name, size_t name_size, tinypy_native_function_callback_t callback, int32_t static_method)
{
    tinypy_value_t *function = tinypy_native_function_new(vm, name, name_size, callback, NULL, NULL);
    tinypy_value_t *attribute = static_method != 0 ? tinypy_static_method_new(function) : function;
    tinypy_value_t *key = tinypy_string_from_bytes(vm, name, name_size);

    tinypy_dict_set(vm->weakref_type.dict, key, attribute);
    tinypy_release(key);
    if (attribute != function) tinypy_release(attribute);
    tinypy_release(function);
}

void tinypy_internal_initialize_weakref_type(tinypy_vm_t *vm)
{
    __tinypy_weakref_add_type_method(vm, "__new__", 7U, __tinypy_weakref_new_method, INT32_C(1));
    __tinypy_weakref_add_type_method(vm, "__init__", 8U, __tinypy_weakref_init_method, INT32_C(0));
    __tinypy_weakref_add_type_method(vm, "__hash__", 8U, __tinypy_weakref_hash_method, INT32_C(0));
}

static tinypy_value_t *__tinypy_weakref_count_function(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);
    tinypy_value_t **head_slot;
    tinypy_value_t *current;
    int64_t count = INT64_C(0);

    (void)user_data;
    if (__tinypy_weakref_arguments(vm, args, kwargs, 1U, 1U, out_error) == 0) return NULL;
    head_slot = tinypy_internal_weakref_head_slot(tinypy_tuple_get(args, 0U));
    current = head_slot != NULL ? *head_slot : NULL;
    while (current != NULL) {
        count += INT64_C(1);
        current = TINYPY_WEAKREF_OBJECT(current)->next;
    }
    return tinypy_integer_from_i64(vm, count);
}

static tinypy_value_t *__tinypy_weakref_list_function(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);
    tinypy_value_t *result;
    tinypy_value_t **head_slot;
    tinypy_value_t *current;

    (void)user_data;
    if (__tinypy_weakref_arguments(vm, args, kwargs, 1U, 1U, out_error) == 0) return NULL;
    result = tinypy_list_from_items(vm, NULL, 0U);
    head_slot = tinypy_internal_weakref_head_slot(tinypy_tuple_get(args, 0U));
    current = head_slot != NULL ? *head_slot : NULL;
    while (current != NULL) {
        tinypy_list_append(result, current);
        current = TINYPY_WEAKREF_OBJECT(current)->next;
    }
    return result;
}

static tinypy_value_t *__tinypy_weakref_proxy_function(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);
    tinypy_value_t *callback = NULL;

    (void)user_data;
    if (__tinypy_weakref_arguments(vm, args, kwargs, 1U, 2U, out_error) == 0) return NULL;
    if (tinypy_tuple_size(args) == 2U && tinypy_internal_value_kind(tinypy_tuple_get(args, 1U)) != TINYPY_VALUE_NONE) callback = tinypy_tuple_get(args, 1U);
    return tinypy_weakref_new(tinypy_tuple_get(args, 0U), callback, out_error);
}

static tinypy_value_t *__tinypy_weakref_remove_function(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error)
{
    tinypy_vm_t *vm = tinypy_internal_value_vm(function);
    tinypy_value_t *dict;
    tinypy_value_t *key;

    (void)user_data;
    if (__tinypy_weakref_arguments(vm, args, kwargs, 2U, 2U, out_error) == 0) return NULL;
    dict = tinypy_tuple_get(args, 0U);
    key = tinypy_tuple_get(args, 1U);
    if (tinypy_internal_value_kind(dict) != TINYPY_VALUE_DICT) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "weakref removal requires a dictionary", out_error);
        return NULL;
    }
    if (tinypy_dict_contains(dict, key) != 0) {
        tinypy_value_t *candidate = tinypy_dict_get(dict, key);

        if (tinypy_internal_value_kind(candidate) == TINYPY_VALUE_WEAKREF && TINYPY_WEAKREF_OBJECT(candidate)->object == NULL) tinypy_dict_delete(dict, key);
    }
    return tinypy_none_get(vm);
}

static void __tinypy_weakref_add_module_function(tinypy_vm_t *vm, tinypy_value_t *module, const char *name, size_t name_size, tinypy_native_function_callback_t callback)
{
    tinypy_value_t *function = tinypy_native_function_new(vm, name, name_size, callback, NULL, NULL);

    tinypy_module_add_value(module, name, name_size, function);
    tinypy_release(function);
}

void tinypy_internal_initialize_weakref_module(tinypy_vm_t *vm)
{
    tinypy_value_t *module = tinypy_module_new(vm, "_weakref", 8U);
    tinypy_value_t *name = tinypy_string_from_bytes(vm, "_weakref", 8U);

    tinypy_module_add_value(module, "__name__", 8U, name);
    tinypy_release(name);
    tinypy_module_add_value(module, "ReferenceType", 13U, &vm->weakref_type.base.base);
    tinypy_module_add_value(module, "ProxyType", 9U, &vm->weakref_type.base.base);
    tinypy_module_add_value(module, "CallableProxyType", 17U, &vm->weakref_type.base.base);
    tinypy_module_add_value(module, "ref", 3U, &vm->weakref_type.base.base);
    __tinypy_weakref_add_module_function(vm, module, "proxy", 5U, __tinypy_weakref_proxy_function);
    __tinypy_weakref_add_module_function(vm, module, "getweakrefcount", 15U, __tinypy_weakref_count_function);
    __tinypy_weakref_add_module_function(vm, module, "getweakrefs", 11U, __tinypy_weakref_list_function);
    __tinypy_weakref_add_module_function(vm, module, "_remove_dead_weakref", 20U, __tinypy_weakref_remove_function);
    tinypy_internal_register_module(vm, "_weakref", 8U, module);
    tinypy_release(module);
}
