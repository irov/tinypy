#include "tinypy/weakref.h"

#include "internal.h"

//////////////////////////////////////////////////////////////////////////
tinypy_value_t **tinypy_internal_weakref_head_slot(tinypy_value_t *value) {
    if (value->type->weakref_offset != 0U) {
        tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);

        if (kind == TINYPY_VALUE_STRING || kind == TINYPY_VALUE_UNICODE || kind == TINYPY_VALUE_LONG) {
            size_t payload_size = tinypy_internal_variable_builtin_payload_size(value);
            size_t aligned_payload = (payload_size + sizeof(tinypy_value_t *) - 1U) & ~(sizeof(tinypy_value_t *) - 1U);
            size_t offset = value->type->slot_count + (value->type->dict_offset != 0U ? 1U : 0U);

            return (tinypy_value_t **)((uint8_t *)value + aligned_payload) + offset;
        }
        return (tinypy_value_t **)((uint8_t *)value + value->type->weakref_offset);
    }
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_weakref_unlink(tinypy_weakref_object_t *weakref) {
    if (weakref->object == NULL) {
        return;
    }
    tinypy_value_t **head_slot = tinypy_internal_weakref_head_slot(weakref->object);
    if (weakref->previous != NULL) {
        TINYPY_WEAKREF_OBJECT(weakref->previous)->next = weakref->next;
    }
    else {
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
    if (callback != NULL && tinypy_is_callable(callback) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "weak reference callback must be callable", out_error);
        return NULL;
    }
    if (callback == NULL && (type == &vm->types[TINYPY_VALUE_WEAKREF] || type == vm->weak_proxy_type || type == vm->callable_weak_proxy_type)) {
        tinypy_value_t *current = *head_slot;

        while (current != NULL) {
            tinypy_weakref_object_t *candidate = TINYPY_WEAKREF_OBJECT(current);

            if (current->type == type && candidate->callback == NULL) {
                TINYPY_INCREF(current);
                return current;
            }
            current = candidate->next;
        }
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
    tinypy_vm_t *vm = TINYPY_VALUE_VM(object);
    TINYPY_CLEAR_ERROR(out_error);
    tinypy_value_t *return_value_1 = __tinypy_weakref_new_with_type(&vm->types[TINYPY_VALUE_WEAKREF], object, callback, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_weakref_get(const tinypy_value_t *value) {
    tinypy_value_t *return_value_1 = TINYPY_WEAKREF_OBJECT((tinypy_value_t *)value)->object;
    return return_value_1;
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
static tinypy_bool_t __tinypy_weakref_arguments(tinypy_vm_t *vm, tinypy_value_t *args, tinypy_value_t *kwargs, size_t minimum, size_t maximum, tinypy_error_t **out_error) {
    size_t count = TINYPY_TUPLE_SIZE(args);

    if ((kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U) || count < minimum || count > maximum) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "weakref function received invalid arguments", out_error);
        return TINYPY_FALSE;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_weakref_create(tinypy_type_t *type, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_value_t *callback = NULL;

    if (__tinypy_weakref_arguments(type->vm, args, kwargs, 1U, 2U, out_error) == 0) {
        return NULL;
    }
    tinypy_bool_t condition = TINYPY_TUPLE_SIZE(args) == 2U;
    if (condition != 0) {
        tinypy_value_t *item_2 = TINYPY_TUPLE_GET(args, 1U);
        condition = TINYPY_VALUE_KIND(item_2) != TINYPY_VALUE_NONE;
    }
    if (condition) {
        callback = TINYPY_TUPLE_GET(args, 1U);
    }
    tinypy_value_t *item = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *return_value_1 = __tinypy_weakref_new_with_type(type, item, callback, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_weakref_call(tinypy_value_t *callable, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(callable);

    if (__tinypy_weakref_arguments(vm, args, kwargs, 0U, 0U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *object = TINYPY_WEAKREF_OBJECT(callable)->object;
    if (object == NULL) {
        tinypy_value_t *return_value_1 = tinypy_none_get(vm);
        return return_value_1;
    }
    TINYPY_INCREF(object);
    return object;
}
//////////////////////////////////////////////////////////////////////////
tinypy_hash_t tinypy_internal_weakref_hash(tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_weakref_object_t *weakref = TINYPY_WEAKREF_OBJECT(value);
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);

    if (weakref->hash_computed == 0) {
        tinypy_value_t *previous_raised;

        if (weakref->object == NULL) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "weak object has gone away", out_error);
            return (tinypy_hash_t)0;
        }
        previous_raised = vm->raised_value;
        weakref->hash = tinypy_internal_hash_value(weakref->object, out_error);
        if ((out_error != NULL && *out_error != NULL) || vm->raised_value != previous_raised) {
            return (tinypy_hash_t)0;
        }
        weakref->hash_computed = INT32_C(1);
    }
    return weakref->hash;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_weakref_compare(tinypy_value_t *left, tinypy_value_t *right, int32_t operation, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(left);
    tinypy_value_t *left_object;
    tinypy_value_t *right_object;

    if (right->type != left->type) {
        tinypy_value_t *return_value_1 = tinypy_not_implemented_get(vm);
        return return_value_1;
    }
    if (operation != TINYPY_COMPARE_EQUAL && operation != TINYPY_COMPARE_NOT_EQUAL) {
        tinypy_value_t *return_value_2 = tinypy_not_implemented_get(vm);
        return return_value_2;
    }
    left_object = TINYPY_WEAKREF_OBJECT(left)->object;
    right_object = TINYPY_WEAKREF_OBJECT(right)->object;
    if (left_object == NULL || right_object == NULL) {
        tinypy_bool_t equal = left == right ? TINYPY_TRUE : TINYPY_FALSE;

        tinypy_value_t *return_value_3 = tinypy_bool_from_i32(vm, operation == TINYPY_COMPARE_EQUAL ? equal : equal == 0);
        return return_value_3;
    }
    tinypy_value_t *return_value_4 = tinypy_compare_value(left_object, right_object, (tinypy_compare_operation_e)operation, out_error);
    return return_value_4;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_weakref_is_proxy(const tinypy_vm_t *vm, const tinypy_value_t *value) {
    return value->type == vm->weak_proxy_type || value->type == vm->callable_weak_proxy_type ? TINYPY_TRUE : TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_weakref_raise_reference_error(tinypy_vm_t *vm, tinypy_error_t **out_error) {
    static const char message[] = "weakly-referenced object no longer exists";

    if (vm->exception_types[TINYPY_EXCEPTION_REFERENCE_ERROR] == NULL) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_RUNTIME, message, out_error);
        return;
    }
    tinypy_value_t *text = tinypy_string_from_bytes(vm, message, sizeof(message) - 1U);
    tinypy_value_t *args = tinypy_tuple_from_items(vm, &text, 1U);
    tinypy_value_t *exception = tinypy_internal_exception_instantiate(vm->exception_types[TINYPY_EXCEPTION_REFERENCE_ERROR], args, NULL, out_error);

    TINYPY_DECREF(args);
    TINYPY_DECREF(text);
    if (exception == NULL) {
        return;
    }
    tinypy_internal_exception_set_raised(vm, exception, NULL);
    TINYPY_DECREF(exception);
    tinypy_internal_exception_make_diagnostic(vm, out_error);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_weakref_proxy_referent(tinypy_value_t *proxy, tinypy_error_t **out_error) {
    tinypy_value_t *object = TINYPY_WEAKREF_OBJECT(proxy)->object;

    if (object == NULL) {
        __tinypy_weakref_raise_reference_error(TINYPY_VALUE_VM(proxy), out_error);
        return NULL;
    }
    return object;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_weakref_proxy_operand(tinypy_vm_t *vm, tinypy_value_t *value, tinypy_error_t **out_error) {
    if (__tinypy_weakref_is_proxy(vm, value) != 0) {
        tinypy_value_t *return_value_1 = __tinypy_weakref_proxy_referent(value, out_error);
        return return_value_1;
    }
    return value;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_weakref_proxy_get_attribute(tinypy_value_t *proxy, tinypy_value_t *name, tinypy_error_t **out_error) {
    tinypy_value_t *object = __tinypy_weakref_proxy_referent(proxy, out_error);

    tinypy_value_t *return_value_1 = object != NULL ? tinypy_internal_object_get_attr_key(object, name, out_error) : NULL;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_weakref_proxy_set_attribute(tinypy_value_t *proxy, tinypy_value_t *name, tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_value_t *object = __tinypy_weakref_proxy_referent(proxy, out_error);

    tinypy_bool_t return_value_1 = object != NULL ? tinypy_internal_object_set_attr_protocol_key(object, name, value, out_error) : TINYPY_FALSE;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_weakref_proxy_call(tinypy_value_t *proxy, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error) {
    tinypy_value_t *object = __tinypy_weakref_proxy_referent(proxy, out_error);

    tinypy_value_t *return_value_1 = object != NULL ? tinypy_call(object, args, kwargs, out_error) : NULL;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_weakref_proxy_string(tinypy_value_t *proxy, tinypy_error_t **out_error) {
    tinypy_value_t *object = __tinypy_weakref_proxy_referent(proxy, out_error);

    tinypy_value_t *return_value_1 = object != NULL ? tinypy_object_str(object, out_error) : NULL;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_hash_t __tinypy_weakref_proxy_hash(tinypy_value_t *proxy, tinypy_error_t **out_error) {
    tinypy_internal_make_vm_error(TINYPY_VALUE_VM(proxy), TINYPY_ERROR_TYPE, "unhashable type", out_error);
    return (tinypy_hash_t)0;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_weakref_proxy_compare(tinypy_value_t *left, tinypy_value_t *right, int32_t operation, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(left);
    tinypy_value_t *left_object = __tinypy_weakref_proxy_referent(left, out_error);
    tinypy_value_t *right_object;

    if (left_object == NULL) {
        return NULL;
    }
    right_object = __tinypy_weakref_proxy_operand(vm, right, out_error);
    if (right_object == NULL) {
        return NULL;
    }
    tinypy_value_t *return_value_1 = tinypy_compare_value(left_object, right_object, (tinypy_compare_operation_e)operation, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_weakref_proxy_nonzero(tinypy_value_t *proxy, tinypy_error_t **out_error) {
    tinypy_value_t *object = __tinypy_weakref_proxy_referent(proxy, out_error);

    int32_t return_value_1 = object != NULL ? tinypy_truth(object, out_error) : -1;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_weakref_proxy_iter(tinypy_value_t *proxy, tinypy_error_t **out_error) {
    tinypy_value_t *object = __tinypy_weakref_proxy_referent(proxy, out_error);

    tinypy_value_t *return_value_1 = object != NULL ? tinypy_iter(object, out_error) : NULL;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_weakref_proxy_next(tinypy_value_t *proxy, tinypy_error_t **out_error) {
    tinypy_value_t *object = __tinypy_weakref_proxy_referent(proxy, out_error);

    tinypy_value_t *return_value_1 = object != NULL ? tinypy_next(object, out_error) : NULL;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static ptrdiff_t __tinypy_weakref_proxy_length(tinypy_value_t *proxy, tinypy_error_t **out_error) {
    tinypy_value_t *object = __tinypy_weakref_proxy_referent(proxy, out_error);
    tinypy_length_slot_t length;

    if (object == NULL) {
        return (ptrdiff_t)-1;
    }
    length = object->type->mapping_slots != NULL && object->type->mapping_slots->length != NULL
                 ? object->type->mapping_slots->length
                 : (object->type->sequence_slots != NULL ? object->type->sequence_slots->length : NULL);
    if (length != NULL) {
        ptrdiff_t return_value_1 = length(object, out_error);
        return return_value_1;
    }
    tinypy_value_t *method = tinypy_internal_object_get_special(object, "__len__", 7U, out_error);
    tinypy_value_t *empty;
    tinypy_value_t *result;
    int64_t value;

    if (method == NULL) {
        return (ptrdiff_t)-1;
    }
    empty = tinypy_tuple_from_items(TINYPY_VALUE_VM(proxy), NULL, 0U);
    result = tinypy_call(method, empty, NULL, out_error);
    TINYPY_DECREF(empty);
    TINYPY_DECREF(method);
    if (result == NULL) {
        return (ptrdiff_t)-1;
    }
    if (tinypy_internal_index_as_i64(result, &value, TINYPY_FALSE, out_error) == 0) {
        TINYPY_DECREF(result);
        return (ptrdiff_t)-1;
    }
    TINYPY_DECREF(result);
    if (value < 0 || (uint64_t)value > (uint64_t)PTRDIFF_MAX) {
        tinypy_internal_make_vm_error(TINYPY_VALUE_VM(proxy), value < 0 ? TINYPY_ERROR_VALUE : TINYPY_ERROR_OVERFLOW, "invalid object length", out_error);
        return (ptrdiff_t)-1;
    }
    return (ptrdiff_t)value;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_weakref_proxy_get_item(tinypy_value_t *proxy, tinypy_value_t *key, tinypy_error_t **out_error) {
    tinypy_value_t *object = __tinypy_weakref_proxy_referent(proxy, out_error);

    tinypy_value_t *return_value_1 = object != NULL ? tinypy_get_item(object, key, out_error) : NULL;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_weakref_proxy_set_item(tinypy_value_t *proxy, tinypy_value_t *key, tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_value_t *object = __tinypy_weakref_proxy_referent(proxy, out_error);

    if (object == NULL) {
        return TINYPY_FALSE;
    }
    tinypy_bool_t return_value_1 = value != NULL ? tinypy_set_item(object, key, value, out_error) : tinypy_delete_item(object, key, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_weakref_proxy_contains(tinypy_value_t *proxy, tinypy_value_t *item, tinypy_error_t **out_error) {
    tinypy_value_t *object = __tinypy_weakref_proxy_referent(proxy, out_error);

    int32_t return_value_1 = object != NULL ? tinypy_contains(object, item, out_error) : -1;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_weakref_proxy_positive(tinypy_value_t *proxy, tinypy_error_t **out_error) {
    tinypy_value_t *object = __tinypy_weakref_proxy_referent(proxy, out_error);

    tinypy_value_t *return_value_1 = object != NULL ? tinypy_positive(object, out_error) : NULL;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_weakref_proxy_negative(tinypy_value_t *proxy, tinypy_error_t **out_error) {
    tinypy_value_t *object = __tinypy_weakref_proxy_referent(proxy, out_error);

    tinypy_value_t *return_value_1 = object != NULL ? tinypy_negative(object, out_error) : NULL;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_weakref_proxy_invert(tinypy_value_t *proxy, tinypy_error_t **out_error) {
    tinypy_value_t *object = __tinypy_weakref_proxy_referent(proxy, out_error);

    tinypy_value_t *return_value_1 = object != NULL ? tinypy_invert(object, out_error) : NULL;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_weakref_proxy_absolute(tinypy_value_t *proxy, tinypy_error_t **out_error) {
    tinypy_value_t *object = __tinypy_weakref_proxy_referent(proxy, out_error);

    if (object == NULL) {
        return NULL;
    }
    if (object->type->number_slots != NULL && object->type->number_slots->absolute != NULL) {
        tinypy_value_t *return_value_1 = object->type->number_slots->absolute(object, out_error);
        return return_value_1;
    }
    if (tinypy_internal_object_has_special(object, "__abs__", 7U) != 0) {
        tinypy_value_t *method = tinypy_internal_object_get_special(object, "__abs__", 7U, out_error);
        tinypy_value_t *empty;
        tinypy_value_t *result;

        if (method == NULL) {
            return NULL;
        }
        empty = tinypy_tuple_from_items(TINYPY_VALUE_VM(proxy), NULL, 0U);
        result = tinypy_call(method, empty, NULL, out_error);
        TINYPY_DECREF(empty);
        TINYPY_DECREF(method);
        return result;
    }
    tinypy_internal_make_vm_error(TINYPY_VALUE_VM(proxy), TINYPY_ERROR_TYPE, "bad operand for abs", out_error);
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_weakref_proxy_index(tinypy_value_t *proxy, tinypy_error_t **out_error) {
    tinypy_value_t *object = __tinypy_weakref_proxy_referent(proxy, out_error);

    tinypy_value_t *return_value_1 = object != NULL ? tinypy_internal_index_value(object, out_error) : NULL;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
#define TINYPY_WEAKREF_PROXY_BINARY(name, operation)                                                                                  \
    static tinypy_value_t *__tinypy_weakref_proxy_##name(tinypy_value_t *left, tinypy_value_t *right, tinypy_error_t **out_error) { \
        tinypy_vm_t *vm = TINYPY_VALUE_VM(left);                                                                                     \
        tinypy_value_t *left_object = __tinypy_weakref_proxy_referent(left, out_error);                                              \
        tinypy_value_t *right_object = __tinypy_weakref_proxy_operand(vm, right, out_error);                                         \
        tinypy_value_t *return_value_1 = left_object != NULL && right_object != NULL ? operation(left_object, right_object, out_error) : NULL; \
        return return_value_1;                                                                                                        \
    }

#define TINYPY_WEAKREF_PROXY_REFLECTED(name, operation)                                                                               \
    static tinypy_value_t *__tinypy_weakref_proxy_reflected_##name(tinypy_value_t *right, tinypy_value_t *left, tinypy_error_t **out_error) { \
        tinypy_vm_t *vm = TINYPY_VALUE_VM(right);                                                                                     \
        tinypy_value_t *right_object = __tinypy_weakref_proxy_referent(right, out_error);                                            \
        tinypy_value_t *left_object = __tinypy_weakref_proxy_operand(vm, left, out_error);                                           \
        tinypy_value_t *return_value_1 = left_object != NULL && right_object != NULL ? operation(left_object, right_object, out_error) : NULL; \
        return return_value_1;                                                                                                        \
    }

TINYPY_WEAKREF_PROXY_BINARY(add, tinypy_add)
TINYPY_WEAKREF_PROXY_BINARY(subtract, tinypy_subtract)
TINYPY_WEAKREF_PROXY_BINARY(multiply, tinypy_multiply)
TINYPY_WEAKREF_PROXY_BINARY(divide, tinypy_divide)
TINYPY_WEAKREF_PROXY_BINARY(remainder, tinypy_remainder)
TINYPY_WEAKREF_PROXY_BINARY(floor_divide, tinypy_floor_divide)
TINYPY_WEAKREF_PROXY_BINARY(true_divide, tinypy_true_divide)
TINYPY_WEAKREF_PROXY_BINARY(left_shift, tinypy_left_shift)
TINYPY_WEAKREF_PROXY_BINARY(right_shift, tinypy_right_shift)
TINYPY_WEAKREF_PROXY_BINARY(bit_and, tinypy_bit_and)
TINYPY_WEAKREF_PROXY_BINARY(bit_xor, tinypy_bit_xor)
TINYPY_WEAKREF_PROXY_BINARY(bit_or, tinypy_bit_or)
TINYPY_WEAKREF_PROXY_BINARY(inplace_add, tinypy_inplace_add)
TINYPY_WEAKREF_PROXY_BINARY(inplace_subtract, tinypy_inplace_subtract)
TINYPY_WEAKREF_PROXY_BINARY(inplace_multiply, tinypy_inplace_multiply)
TINYPY_WEAKREF_PROXY_BINARY(inplace_divide, tinypy_inplace_divide)
TINYPY_WEAKREF_PROXY_REFLECTED(add, tinypy_add)
TINYPY_WEAKREF_PROXY_REFLECTED(subtract, tinypy_subtract)
TINYPY_WEAKREF_PROXY_REFLECTED(multiply, tinypy_multiply)
TINYPY_WEAKREF_PROXY_REFLECTED(divide, tinypy_divide)

#undef TINYPY_WEAKREF_PROXY_REFLECTED
#undef TINYPY_WEAKREF_PROXY_BINARY
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_weakref_proxy_power(tinypy_value_t *base, tinypy_value_t *exponent, tinypy_value_t *modulus, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(base);
    tinypy_value_t *object = __tinypy_weakref_proxy_referent(base, out_error);
    tinypy_value_t *exponent_object = __tinypy_weakref_proxy_operand(vm, exponent, out_error);

    if (object == NULL || exponent_object == NULL) {
        return NULL;
    }
    tinypy_value_t *return_value_1 = modulus == NULL || TINYPY_VALUE_KIND(modulus) == TINYPY_VALUE_NONE
                                         ? tinypy_power(object, exponent_object, out_error)
                                         : tinypy_internal_power_modulo(object, exponent_object, modulus, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
typedef enum tinypy_weakref_proxy_binary_operation_e {
    TINYPY_WEAKREF_PROXY_ADD = 0,
    TINYPY_WEAKREF_PROXY_SUBTRACT = 1,
    TINYPY_WEAKREF_PROXY_MULTIPLY = 2,
    TINYPY_WEAKREF_PROXY_DIVIDE = 3,
    TINYPY_WEAKREF_PROXY_FLOOR_DIVIDE = 4,
    TINYPY_WEAKREF_PROXY_TRUE_DIVIDE = 5,
    TINYPY_WEAKREF_PROXY_REMAINDER = 6,
    TINYPY_WEAKREF_PROXY_DIVMOD = 7,
    TINYPY_WEAKREF_PROXY_POWER = 8,
    TINYPY_WEAKREF_PROXY_LEFT_SHIFT = 9,
    TINYPY_WEAKREF_PROXY_RIGHT_SHIFT = 10,
    TINYPY_WEAKREF_PROXY_BIT_AND = 11,
    TINYPY_WEAKREF_PROXY_BIT_XOR = 12,
    TINYPY_WEAKREF_PROXY_BIT_OR = 13,
    TINYPY_WEAKREF_PROXY_INPLACE_ADD = 20,
    TINYPY_WEAKREF_PROXY_INPLACE_SUBTRACT = 21,
    TINYPY_WEAKREF_PROXY_INPLACE_MULTIPLY = 22,
    TINYPY_WEAKREF_PROXY_INPLACE_DIVIDE = 23,
    TINYPY_WEAKREF_PROXY_INPLACE_FLOOR_DIVIDE = 24,
    TINYPY_WEAKREF_PROXY_INPLACE_TRUE_DIVIDE = 25,
    TINYPY_WEAKREF_PROXY_INPLACE_REMAINDER = 26,
    TINYPY_WEAKREF_PROXY_INPLACE_POWER = 28,
    TINYPY_WEAKREF_PROXY_INPLACE_LEFT_SHIFT = 29,
    TINYPY_WEAKREF_PROXY_INPLACE_RIGHT_SHIFT = 30,
    TINYPY_WEAKREF_PROXY_INPLACE_BIT_AND = 31,
    TINYPY_WEAKREF_PROXY_INPLACE_BIT_XOR = 32,
    TINYPY_WEAKREF_PROXY_INPLACE_BIT_OR = 33,
    TINYPY_WEAKREF_PROXY_REFLECTED_OFFSET = 100
} tinypy_weakref_proxy_binary_operation_e;
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_weakref_proxy_binary_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    intptr_t operation = (intptr_t)user_data;
    size_t maximum = operation == TINYPY_WEAKREF_PROXY_POWER || operation == TINYPY_WEAKREF_PROXY_POWER + TINYPY_WEAKREF_PROXY_REFLECTED_OFFSET ? 3U : 2U;

    if (__tinypy_weakref_arguments(vm, args, kwargs, 2U, maximum, out_error) == 0 || __tinypy_weakref_is_proxy(vm, TINYPY_TUPLE_GET(args, 0U)) == 0) {
        if (out_error == NULL || *out_error == NULL) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "weak proxy operation requires a proxy", out_error);
        }
        return NULL;
    }
    tinypy_value_t *self_object = __tinypy_weakref_proxy_referent(TINYPY_TUPLE_GET(args, 0U), out_error);
    tinypy_value_t *argument_object = __tinypy_weakref_proxy_operand(vm, TINYPY_TUPLE_GET(args, 1U), out_error);
    tinypy_bool_t reflected = operation >= TINYPY_WEAKREF_PROXY_REFLECTED_OFFSET ? TINYPY_TRUE : TINYPY_FALSE;
    tinypy_value_t *left = reflected != 0 ? argument_object : self_object;
    tinypy_value_t *right = reflected != 0 ? self_object : argument_object;

    if (self_object == NULL || argument_object == NULL) {
        return NULL;
    }
    if (reflected != 0) {
        operation -= TINYPY_WEAKREF_PROXY_REFLECTED_OFFSET;
    }
    switch ((tinypy_weakref_proxy_binary_operation_e)operation) {
        case TINYPY_WEAKREF_PROXY_ADD: return tinypy_add(left, right, out_error);
        case TINYPY_WEAKREF_PROXY_SUBTRACT: return tinypy_subtract(left, right, out_error);
        case TINYPY_WEAKREF_PROXY_MULTIPLY: return tinypy_multiply(left, right, out_error);
        case TINYPY_WEAKREF_PROXY_DIVIDE: return tinypy_divide(left, right, out_error);
        case TINYPY_WEAKREF_PROXY_FLOOR_DIVIDE: return tinypy_floor_divide(left, right, out_error);
        case TINYPY_WEAKREF_PROXY_TRUE_DIVIDE: return tinypy_true_divide(left, right, out_error);
        case TINYPY_WEAKREF_PROXY_REMAINDER: return tinypy_remainder(left, right, out_error);
        case TINYPY_WEAKREF_PROXY_DIVMOD: return tinypy_divmod(left, right, out_error);
        case TINYPY_WEAKREF_PROXY_POWER:
            return TINYPY_TUPLE_SIZE(args) == 3U
                       ? tinypy_internal_power_modulo(left, right, TINYPY_TUPLE_GET(args, 2U), out_error)
                       : tinypy_power(left, right, out_error);
        case TINYPY_WEAKREF_PROXY_LEFT_SHIFT: return tinypy_left_shift(left, right, out_error);
        case TINYPY_WEAKREF_PROXY_RIGHT_SHIFT: return tinypy_right_shift(left, right, out_error);
        case TINYPY_WEAKREF_PROXY_BIT_AND: return tinypy_bit_and(left, right, out_error);
        case TINYPY_WEAKREF_PROXY_BIT_XOR: return tinypy_bit_xor(left, right, out_error);
        case TINYPY_WEAKREF_PROXY_BIT_OR: return tinypy_bit_or(left, right, out_error);
        case TINYPY_WEAKREF_PROXY_INPLACE_ADD: return tinypy_inplace_add(left, right, out_error);
        case TINYPY_WEAKREF_PROXY_INPLACE_SUBTRACT: return tinypy_inplace_subtract(left, right, out_error);
        case TINYPY_WEAKREF_PROXY_INPLACE_MULTIPLY: return tinypy_inplace_multiply(left, right, out_error);
        case TINYPY_WEAKREF_PROXY_INPLACE_DIVIDE: return tinypy_inplace_divide(left, right, out_error);
        case TINYPY_WEAKREF_PROXY_INPLACE_FLOOR_DIVIDE: return tinypy_inplace_floor_divide(left, right, out_error);
        case TINYPY_WEAKREF_PROXY_INPLACE_TRUE_DIVIDE: return tinypy_inplace_true_divide(left, right, out_error);
        case TINYPY_WEAKREF_PROXY_INPLACE_REMAINDER: return tinypy_inplace_remainder(left, right, out_error);
        case TINYPY_WEAKREF_PROXY_INPLACE_POWER: return tinypy_inplace_power(left, right, out_error);
        case TINYPY_WEAKREF_PROXY_INPLACE_LEFT_SHIFT: return tinypy_inplace_left_shift(left, right, out_error);
        case TINYPY_WEAKREF_PROXY_INPLACE_RIGHT_SHIFT: return tinypy_inplace_right_shift(left, right, out_error);
        case TINYPY_WEAKREF_PROXY_INPLACE_BIT_AND: return tinypy_inplace_bit_and(left, right, out_error);
        case TINYPY_WEAKREF_PROXY_INPLACE_BIT_XOR: return tinypy_inplace_bit_xor(left, right, out_error);
        case TINYPY_WEAKREF_PROXY_INPLACE_BIT_OR: return tinypy_inplace_bit_or(left, right, out_error);
        default: break;
    }
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_RUNTIME, "invalid weak proxy operation", out_error);
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_weakref_proxy_unary_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    if (__tinypy_weakref_arguments(vm, args, kwargs, 1U, 1U, out_error) == 0 || __tinypy_weakref_is_proxy(vm, TINYPY_TUPLE_GET(args, 0U)) == 0) {
        if (out_error == NULL || *out_error == NULL) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "weak proxy operation requires a proxy", out_error);
        }
        return NULL;
    }
    tinypy_value_t *proxy = TINYPY_TUPLE_GET(args, 0U);
    tinypy_value_t *object = __tinypy_weakref_proxy_referent(proxy, out_error);
    tinypy_value_t *result;

    if (object == NULL) {
        return NULL;
    }
    switch ((intptr_t)user_data) {
        case 0: result = tinypy_positive(object, out_error); break;
        case 1: result = tinypy_negative(object, out_error); break;
        case 2: result = tinypy_invert(object, out_error); break;
        case 3: result = __tinypy_weakref_proxy_absolute(proxy, out_error); break;
        case 7: result = tinypy_internal_index_value(object, out_error); break;
        case 8: {
            int32_t truth = tinypy_truth(object, out_error);

            result = truth >= 0 ? tinypy_bool_from_i32(vm, truth) : NULL;
            break;
        }
        default: {
            tinypy_value_t *constructor_args = tinypy_tuple_from_items(vm, &object, 1U);

            if ((intptr_t)user_data == 4) {
                result = tinypy_internal_integer_create(&vm->types[TINYPY_VALUE_INTEGER], constructor_args, NULL, out_error);
            }
            else if ((intptr_t)user_data == 5) {
                result = tinypy_internal_long_create(&vm->types[TINYPY_VALUE_LONG], constructor_args, NULL, out_error);
            }
            else {
                result = tinypy_internal_float_create(&vm->types[TINYPY_VALUE_FLOAT], constructor_args, NULL, out_error);
            }
            TINYPY_DECREF(constructor_args);
            break;
        }
    }
    return result;
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
    if (TINYPY_VALUE_KIND(type_value) != TINYPY_VALUE_TYPE || tinypy_type_is_subtype((tinypy_type_t *)type_value, &vm->types[TINYPY_VALUE_WEAKREF]) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "weakref.__new__ requires a weakref subtype", out_error);
        return NULL;
    }
    tinypy_bool_t condition_2 = TINYPY_TUPLE_SIZE(args) == 3U;
    if (condition_2 != 0) {
        tinypy_value_t *item_2 = TINYPY_TUPLE_GET(args, 2U);
        condition_2 = TINYPY_VALUE_KIND(item_2) != TINYPY_VALUE_NONE;
    }
    if (condition_2) {
        callback = TINYPY_TUPLE_GET(args, 2U);
    }
    tinypy_value_t *item = TINYPY_TUPLE_GET(args, 1U);
    tinypy_value_t *return_value_1 = __tinypy_weakref_new_with_type((tinypy_type_t *)type_value, item, callback, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_weakref_init_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_weakref_arguments(vm, args, kwargs, 2U, 3U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *return_value_1 = tinypy_none_get(vm);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_weakref_hash_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_weakref_arguments(vm, args, kwargs, 1U, 1U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *self = TINYPY_TUPLE_GET(args, 0U);
    if (tinypy_type_is_subtype(self->type, &vm->types[TINYPY_VALUE_WEAKREF]) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "weakref.__hash__ requires a weak reference", out_error);
        return NULL;
    }
    tinypy_hash_t hash = tinypy_internal_weakref_hash(self, out_error);
    if (out_error != NULL && *out_error != NULL) {
        return NULL;
    }
    tinypy_value_t *return_value_1 = tinypy_integer_from_i64(vm, (int64_t)hash);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_weakref_call_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_weakref_arguments(vm, args, kwargs, 1U, 1U, out_error) == 0 || tinypy_type_is_subtype(TINYPY_TUPLE_GET(args, 0U)->type, &vm->types[TINYPY_VALUE_WEAKREF]) == 0) {
        if (out_error == NULL || *out_error == NULL) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "weakref.__call__ requires a weak reference", out_error);
        }
        return NULL;
    }
    tinypy_value_t *empty = tinypy_tuple_from_items(vm, NULL, 0U);
    tinypy_value_t *result = tinypy_internal_weakref_call(TINYPY_TUPLE_GET(args, 0U), empty, NULL, out_error);

    TINYPY_DECREF(empty);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_weakref_compare_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    if (__tinypy_weakref_arguments(vm, args, kwargs, 2U, 2U, out_error) == 0 || tinypy_type_is_subtype(TINYPY_TUPLE_GET(args, 0U)->type, &vm->types[TINYPY_VALUE_WEAKREF]) == 0) {
        if (out_error == NULL || *out_error == NULL) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "weakref comparison requires a weak reference", out_error);
        }
        return NULL;
    }
    tinypy_value_t *return_value_1 = tinypy_internal_weakref_compare(TINYPY_TUPLE_GET(args, 0U), TINYPY_TUPLE_GET(args, 1U), (int32_t)(intptr_t)user_data, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_weakref_proxy_delete_attribute_method(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_weakref_arguments(vm, args, kwargs, 2U, 2U, out_error) == 0 || __tinypy_weakref_is_proxy(vm, TINYPY_TUPLE_GET(args, 0U)) == 0) {
        if (out_error == NULL || *out_error == NULL) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "weak proxy deletion requires a proxy", out_error);
        }
        return NULL;
    }
    tinypy_value_t *object = __tinypy_weakref_proxy_referent(TINYPY_TUPLE_GET(args, 0U), out_error);
    if (object == NULL || tinypy_internal_object_delete_attr_protocol_key(object, TINYPY_TUPLE_GET(args, 1U), out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *return_value_1 = tinypy_none_get(vm);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_weakref_add_type_method(tinypy_vm_t *vm, const char *name, size_t name_size, tinypy_native_function_callback_t callback, int32_t static_method) {
    tinypy_value_t *function = tinypy_native_function_new(vm, name, name_size, callback, NULL, NULL);
    tinypy_value_t *attribute = static_method != 0 ? tinypy_static_method_new(function) : function;

    tinypy_type_set_attr(&vm->types[TINYPY_VALUE_WEAKREF], name, name_size, attribute);
    if (attribute != function) {
        TINYPY_DECREF(attribute);
    }
    TINYPY_DECREF(function);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_weakref_proxy_add_type_method(tinypy_vm_t *vm, tinypy_type_t *type, const char *name, size_t name_size, tinypy_native_function_callback_t callback, void *user_data) {
    tinypy_value_t *function = tinypy_native_function_new(vm, name, name_size, callback, user_data, NULL);

    tinypy_internal_native_function_set_descriptor_kind(function, TINYPY_NATIVE_DESCRIPTOR_WRAPPER);
    tinypy_type_set_attr(type, name, name_size, function);
    TINYPY_DECREF(function);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_type_t *__tinypy_weakref_proxy_type_new(tinypy_vm_t *vm, const char *name, size_t name_size, tinypy_bool_t callable) {
    static const struct {
        const char *name;
        size_t size;
        intptr_t operation;
    } binary_methods[] = {
        {"__add__", 7U, TINYPY_WEAKREF_PROXY_ADD}, {"__radd__", 8U, TINYPY_WEAKREF_PROXY_ADD + TINYPY_WEAKREF_PROXY_REFLECTED_OFFSET},
        {"__sub__", 7U, TINYPY_WEAKREF_PROXY_SUBTRACT}, {"__rsub__", 8U, TINYPY_WEAKREF_PROXY_SUBTRACT + TINYPY_WEAKREF_PROXY_REFLECTED_OFFSET},
        {"__mul__", 7U, TINYPY_WEAKREF_PROXY_MULTIPLY}, {"__rmul__", 8U, TINYPY_WEAKREF_PROXY_MULTIPLY + TINYPY_WEAKREF_PROXY_REFLECTED_OFFSET},
        {"__div__", 7U, TINYPY_WEAKREF_PROXY_DIVIDE}, {"__rdiv__", 8U, TINYPY_WEAKREF_PROXY_DIVIDE + TINYPY_WEAKREF_PROXY_REFLECTED_OFFSET},
        {"__floordiv__", 12U, TINYPY_WEAKREF_PROXY_FLOOR_DIVIDE}, {"__rfloordiv__", 13U, TINYPY_WEAKREF_PROXY_FLOOR_DIVIDE + TINYPY_WEAKREF_PROXY_REFLECTED_OFFSET},
        {"__truediv__", 11U, TINYPY_WEAKREF_PROXY_TRUE_DIVIDE}, {"__rtruediv__", 12U, TINYPY_WEAKREF_PROXY_TRUE_DIVIDE + TINYPY_WEAKREF_PROXY_REFLECTED_OFFSET},
        {"__mod__", 7U, TINYPY_WEAKREF_PROXY_REMAINDER}, {"__rmod__", 8U, TINYPY_WEAKREF_PROXY_REMAINDER + TINYPY_WEAKREF_PROXY_REFLECTED_OFFSET},
        {"__divmod__", 10U, TINYPY_WEAKREF_PROXY_DIVMOD}, {"__rdivmod__", 11U, TINYPY_WEAKREF_PROXY_DIVMOD + TINYPY_WEAKREF_PROXY_REFLECTED_OFFSET},
        {"__pow__", 7U, TINYPY_WEAKREF_PROXY_POWER}, {"__rpow__", 8U, TINYPY_WEAKREF_PROXY_POWER + TINYPY_WEAKREF_PROXY_REFLECTED_OFFSET},
        {"__lshift__", 10U, TINYPY_WEAKREF_PROXY_LEFT_SHIFT}, {"__rlshift__", 11U, TINYPY_WEAKREF_PROXY_LEFT_SHIFT + TINYPY_WEAKREF_PROXY_REFLECTED_OFFSET},
        {"__rshift__", 10U, TINYPY_WEAKREF_PROXY_RIGHT_SHIFT}, {"__rrshift__", 11U, TINYPY_WEAKREF_PROXY_RIGHT_SHIFT + TINYPY_WEAKREF_PROXY_REFLECTED_OFFSET},
        {"__and__", 7U, TINYPY_WEAKREF_PROXY_BIT_AND}, {"__rand__", 8U, TINYPY_WEAKREF_PROXY_BIT_AND + TINYPY_WEAKREF_PROXY_REFLECTED_OFFSET},
        {"__xor__", 7U, TINYPY_WEAKREF_PROXY_BIT_XOR}, {"__rxor__", 8U, TINYPY_WEAKREF_PROXY_BIT_XOR + TINYPY_WEAKREF_PROXY_REFLECTED_OFFSET},
        {"__or__", 6U, TINYPY_WEAKREF_PROXY_BIT_OR}, {"__ror__", 7U, TINYPY_WEAKREF_PROXY_BIT_OR + TINYPY_WEAKREF_PROXY_REFLECTED_OFFSET},
        {"__iadd__", 8U, TINYPY_WEAKREF_PROXY_INPLACE_ADD}, {"__isub__", 8U, TINYPY_WEAKREF_PROXY_INPLACE_SUBTRACT},
        {"__imul__", 8U, TINYPY_WEAKREF_PROXY_INPLACE_MULTIPLY}, {"__idiv__", 8U, TINYPY_WEAKREF_PROXY_INPLACE_DIVIDE},
        {"__ifloordiv__", 13U, TINYPY_WEAKREF_PROXY_INPLACE_FLOOR_DIVIDE}, {"__itruediv__", 12U, TINYPY_WEAKREF_PROXY_INPLACE_TRUE_DIVIDE},
        {"__imod__", 8U, TINYPY_WEAKREF_PROXY_INPLACE_REMAINDER}, {"__ipow__", 8U, TINYPY_WEAKREF_PROXY_INPLACE_POWER},
        {"__ilshift__", 11U, TINYPY_WEAKREF_PROXY_INPLACE_LEFT_SHIFT}, {"__irshift__", 11U, TINYPY_WEAKREF_PROXY_INPLACE_RIGHT_SHIFT},
        {"__iand__", 8U, TINYPY_WEAKREF_PROXY_INPLACE_BIT_AND}, {"__ixor__", 8U, TINYPY_WEAKREF_PROXY_INPLACE_BIT_XOR}, {"__ior__", 7U, TINYPY_WEAKREF_PROXY_INPLACE_BIT_OR}};
    static const struct {
        const char *name;
        size_t size;
        intptr_t operation;
    } unary_methods[] = {
        {"__pos__", 7U, 0}, {"__neg__", 7U, 1}, {"__invert__", 10U, 2}, {"__abs__", 7U, 3},
        {"__int__", 7U, 4}, {"__long__", 8U, 5}, {"__float__", 9U, 6}, {"__index__", 9U, 7}, {"__nonzero__", 11U, 8}};
    tinypy_type_t *type = tinypy_internal_type_new_configured(vm, name, name_size, NULL, 0U, NULL, NULL, TINYPY_FALSE, TINYPY_FALSE, NULL);

    type->layout_kind = TINYPY_VALUE_WEAKREF;
    type->basic_size = sizeof(tinypy_weakref_object_t);
    type->slots_offset = 0U;
    type->dict_offset = 0U;
    type->weakref_offset = 0U;
    type->has_instance_dict = TINYPY_FALSE;
    type->release_references = tinypy_internal_weakref_release_references;
    type->traverse_references = tinypy_internal_weakref_release_references;
    type->destroy = tinypy_internal_weakref_destroy;
    type->number_slots = &vm->weak_proxy_number_slots;
    type->sequence_slots = &vm->weak_proxy_sequence_slots;
    type->mapping_slots = &vm->weak_proxy_mapping_slots;
    type->string = __tinypy_weakref_proxy_string;
    type->hash = __tinypy_weakref_proxy_hash;
    type->call = callable != 0 ? __tinypy_weakref_proxy_call : NULL;
    type->get_attribute = __tinypy_weakref_proxy_get_attribute;
    type->set_attribute = __tinypy_weakref_proxy_set_attribute;
    type->rich_compare = __tinypy_weakref_proxy_compare;
    type->iter = __tinypy_weakref_proxy_iter;
    type->next = __tinypy_weakref_proxy_next;
    type->create = NULL;
    type->flags = (type->flags | TINYPY_TYPE_FLAG_IMMUTABLE) & ~TINYPY_TYPE_FLAG_BASE_TYPE;
    tinypy_type_set_attr(type, "__hash__", 8U, &vm->none_object.base);
    __tinypy_weakref_proxy_add_type_method(vm, type, "__delattr__", 11U, __tinypy_weakref_proxy_delete_attribute_method, NULL);
    for (size_t index = 0U; index < sizeof(binary_methods) / sizeof(binary_methods[0]); ++index) {
        __tinypy_weakref_proxy_add_type_method(vm, type, binary_methods[index].name, binary_methods[index].size, __tinypy_weakref_proxy_binary_method, (void *)binary_methods[index].operation);
    }
    for (size_t index = 0U; index < sizeof(unary_methods) / sizeof(unary_methods[0]); ++index) {
        __tinypy_weakref_proxy_add_type_method(vm, type, unary_methods[index].name, unary_methods[index].size, __tinypy_weakref_proxy_unary_method, (void *)unary_methods[index].operation);
    }
    return type;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_initialize_weakref_type(tinypy_vm_t *vm) {
    static const char *const comparison_names[] = {"__lt__", "__le__", "__eq__", "__ne__", "__gt__", "__ge__"};

    (void)memset(&vm->weak_proxy_number_slots, 0, sizeof(vm->weak_proxy_number_slots));
    vm->weak_proxy_number_slots.positive = __tinypy_weakref_proxy_positive;
    vm->weak_proxy_number_slots.negative = __tinypy_weakref_proxy_negative;
    vm->weak_proxy_number_slots.absolute = __tinypy_weakref_proxy_absolute;
    vm->weak_proxy_number_slots.nonzero = __tinypy_weakref_proxy_nonzero;
    vm->weak_proxy_number_slots.invert = __tinypy_weakref_proxy_invert;
    vm->weak_proxy_number_slots.add = __tinypy_weakref_proxy_add;
    vm->weak_proxy_number_slots.subtract = __tinypy_weakref_proxy_subtract;
    vm->weak_proxy_number_slots.multiply = __tinypy_weakref_proxy_multiply;
    vm->weak_proxy_number_slots.divide = __tinypy_weakref_proxy_divide;
    vm->weak_proxy_number_slots.remainder = __tinypy_weakref_proxy_remainder;
    vm->weak_proxy_number_slots.power = __tinypy_weakref_proxy_power;
    vm->weak_proxy_number_slots.left_shift = __tinypy_weakref_proxy_left_shift;
    vm->weak_proxy_number_slots.right_shift = __tinypy_weakref_proxy_right_shift;
    vm->weak_proxy_number_slots.bit_and = __tinypy_weakref_proxy_bit_and;
    vm->weak_proxy_number_slots.bit_xor = __tinypy_weakref_proxy_bit_xor;
    vm->weak_proxy_number_slots.bit_or = __tinypy_weakref_proxy_bit_or;
    vm->weak_proxy_number_slots.floor_divide = __tinypy_weakref_proxy_floor_divide;
    vm->weak_proxy_number_slots.true_divide = __tinypy_weakref_proxy_true_divide;
    vm->weak_proxy_number_slots.index = __tinypy_weakref_proxy_index;
    vm->weak_proxy_number_slots.inplace_add = __tinypy_weakref_proxy_inplace_add;
    vm->weak_proxy_number_slots.inplace_subtract = __tinypy_weakref_proxy_inplace_subtract;
    vm->weak_proxy_number_slots.inplace_multiply = __tinypy_weakref_proxy_inplace_multiply;
    vm->weak_proxy_number_slots.inplace_divide = __tinypy_weakref_proxy_inplace_divide;
    vm->weak_proxy_number_slots.reflected_add = __tinypy_weakref_proxy_reflected_add;
    vm->weak_proxy_number_slots.reflected_subtract = __tinypy_weakref_proxy_reflected_subtract;
    vm->weak_proxy_number_slots.reflected_multiply = __tinypy_weakref_proxy_reflected_multiply;
    vm->weak_proxy_number_slots.reflected_divide = __tinypy_weakref_proxy_reflected_divide;
    (void)memset(&vm->weak_proxy_sequence_slots, 0, sizeof(vm->weak_proxy_sequence_slots));
    vm->weak_proxy_sequence_slots.length = __tinypy_weakref_proxy_length;
    vm->weak_proxy_sequence_slots.get_item = __tinypy_weakref_proxy_get_item;
    vm->weak_proxy_sequence_slots.set_item = __tinypy_weakref_proxy_set_item;
    vm->weak_proxy_sequence_slots.contains = __tinypy_weakref_proxy_contains;
    (void)memset(&vm->weak_proxy_mapping_slots, 0, sizeof(vm->weak_proxy_mapping_slots));
    vm->weak_proxy_mapping_slots.length = __tinypy_weakref_proxy_length;
    vm->weak_proxy_mapping_slots.get_item = __tinypy_weakref_proxy_get_item;
    vm->weak_proxy_mapping_slots.set_item = __tinypy_weakref_proxy_set_item;
    vm->weak_proxy_type = __tinypy_weakref_proxy_type_new(vm, "weakproxy", 9U, TINYPY_FALSE);
    vm->callable_weak_proxy_type = __tinypy_weakref_proxy_type_new(vm, "weakcallableproxy", 17U, TINYPY_TRUE);

    __tinypy_weakref_add_type_method(vm, "__new__", 7U, __tinypy_weakref_new_method, INT32_C(1));
    __tinypy_weakref_add_type_method(vm, "__init__", 8U, __tinypy_weakref_init_method, INT32_C(0));
    __tinypy_weakref_add_type_method(vm, "__call__", 8U, __tinypy_weakref_call_method, INT32_C(0));
    __tinypy_weakref_add_type_method(vm, "__hash__", 8U, __tinypy_weakref_hash_method, INT32_C(0));
    for (size_t index = 0U; index < sizeof(comparison_names) / sizeof(comparison_names[0]); ++index) {
        tinypy_value_t *function = tinypy_native_function_new(vm, comparison_names[index], 6U, __tinypy_weakref_compare_method, (void *)(intptr_t)index, NULL);

        tinypy_type_set_attr(&vm->types[TINYPY_VALUE_WEAKREF], comparison_names[index], 6U, function);
        TINYPY_DECREF(function);
    }
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
    tinypy_value_t *return_value_1 = tinypy_integer_from_i64(vm, count);
    return return_value_1;
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
        if (tinypy_internal_list_append_checked(result, current, out_error) == 0) {
            TINYPY_DECREF(result);
            return NULL;
        }
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
    tinypy_bool_t condition_3 = TINYPY_TUPLE_SIZE(args) == 2U;
    if (condition_3 != 0) {
        tinypy_value_t *item_2 = TINYPY_TUPLE_GET(args, 1U);
        condition_3 = TINYPY_VALUE_KIND(item_2) != TINYPY_VALUE_NONE;
    }
    if (condition_3) {
        callback = TINYPY_TUPLE_GET(args, 1U);
    }
    tinypy_value_t *item = TINYPY_TUPLE_GET(args, 0U);
    tinypy_type_t *type = tinypy_is_callable(item) != 0 ? vm->callable_weak_proxy_type : vm->weak_proxy_type;
    tinypy_value_t *return_value_1 = __tinypy_weakref_new_with_type(type, item, callback, out_error);
    return return_value_1;
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
    tinypy_value_t *return_value_1 = tinypy_none_get(vm);
    return return_value_1;
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
    tinypy_module_add_value(module, "ReferenceType", 13U, &vm->types[TINYPY_VALUE_WEAKREF].base.base);
    tinypy_module_add_value(module, "ProxyType", 9U, &vm->weak_proxy_type->base.base);
    tinypy_module_add_value(module, "CallableProxyType", 17U, &vm->callable_weak_proxy_type->base.base);
    tinypy_module_add_value(module, "ref", 3U, &vm->types[TINYPY_VALUE_WEAKREF].base.base);
    __tinypy_weakref_add_module_function(vm, module, "proxy", 5U, __tinypy_weakref_proxy_function);
    __tinypy_weakref_add_module_function(vm, module, "getweakrefcount", 15U, __tinypy_weakref_count_function);
    __tinypy_weakref_add_module_function(vm, module, "getweakrefs", 11U, __tinypy_weakref_list_function);
    __tinypy_weakref_add_module_function(vm, module, "_remove_dead_weakref", 20U, __tinypy_weakref_remove_function);
    tinypy_internal_register_module(vm, "_weakref", 8U, module);
    TINYPY_DECREF(module);
}
