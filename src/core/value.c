#include "tinypy/value.h"

#include "internal.h"

#include <string.h>
//////////////////////////////////////////////////////////////////////////
tinypy_vm_t *tinypy_internal_value_vm(const tinypy_value_t *value) {
    tinypy_vm_t *return_value_1 = TINYPY_VALUE_VM(value);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_type_e tinypy_internal_value_kind(const tinypy_value_t *value) {
    tinypy_value_type_e return_value_1 = TINYPY_VALUE_KIND(value);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_value_allocate(tinypy_vm_t *vm, tinypy_value_type_e type, size_t allocation_size) {
    tinypy_type_t *object_type = &vm->types[type];

    tinypy_value_t *return_value_1 = tinypy_internal_object_allocate(vm, object_type, allocation_size);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_object_allocate(tinypy_vm_t *vm, tinypy_type_t *object_type, size_t allocation_size) {

    tinypy_value_t *value = (tinypy_value_t *)tinypy_internal_vm_allocate(
        vm,
        allocation_size);

    (void)memset(value, 0, allocation_size);
    value->ref = 1U;
    value->type = object_type;

    TINYPY_INCREF(&object_type->base.base);
#if defined(TINYPY_CYCLE_DIAGNOSTICS)
    __tinypy_internal_cycle_diagnostics_value_register(vm, value);
#endif
    return value;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_object_allocate_checked(tinypy_vm_t *vm, tinypy_type_t *object_type, size_t allocation_size, tinypy_error_t **out_error) {
    tinypy_value_t *value = (tinypy_value_t *)tinypy_internal_vm_allocate_checked(vm, allocation_size, out_error);

    if (value == NULL) {
        return NULL;
    }
    (void)memset(value, 0, allocation_size);
    value->ref = 1U;
    value->type = object_type;
    TINYPY_INCREF(&object_type->base.base);
#if defined(TINYPY_CYCLE_DIAGNOSTICS)
    __tinypy_internal_cycle_diagnostics_value_register(vm, value);
#endif
    return value;
}
//////////////////////////////////////////////////////////////////////////
size_t tinypy_internal_variable_builtin_payload_size(const tinypy_value_t *value) {
    size_t result;

    switch (TINYPY_VALUE_KIND(value)) {
    case TINYPY_VALUE_STRING:
        result = offsetof(tinypy_string_object_t, bytes) + TINYPY_SIZED_SIZE(value) + 1U;
        break;
    case TINYPY_VALUE_UNICODE:
        result = offsetof(tinypy_unicode_object_t, utf8) + TINYPY_UNICODE_OBJECT(value)->byte_size + 1U;
        break;
    case TINYPY_VALUE_LONG:
        result = offsetof(tinypy_long_object_t, digits) + TINYPY_LONG_OBJECT(value)->digit_capacity * sizeof(uint16_t);
        break;
    default:
        result = 0U;
        break;
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
size_t tinypy_internal_builtin_subclass_allocation_size(const tinypy_type_t *type, size_t payload_size) {
    const size_t alignment = sizeof(tinypy_value_t *);
    size_t aligned_payload;
    size_t pointer_count = type->slot_count + (type->dict_offset != 0U ? 1U : 0U) + (type->weakref_offset != 0U ? 1U : 0U);

    if (payload_size > SIZE_MAX - (alignment - 1U)) {
        return 0U;
    }
    aligned_payload = (payload_size + alignment - 1U) & ~(alignment - 1U);
    if (pointer_count > (SIZE_MAX - aligned_payload) / sizeof(tinypy_value_t *)) {
        return 0U;
    }
    return aligned_payload + pointer_count * sizeof(tinypy_value_t *);
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_immutable_subclass_copy(tinypy_type_t *type, tinypy_value_t *value, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = type->vm;
    tinypy_value_type_e kind;
    size_t payload_size;
    size_t allocation_size;
    tinypy_value_t *result;

    if (value == NULL || type == value->type) {
        return value;
    }
    kind = type->layout_kind;
    if (TINYPY_VALUE_KIND(value) != kind) {
        if (type == &vm->types[kind]) {
            return value;
        }
        TINYPY_DECREF(value);
        tinypy_internal_make_vm_error(vm, kind == TINYPY_VALUE_INTEGER ? TINYPY_ERROR_OVERFLOW : TINYPY_ERROR_TYPE, "immutable constructor produced an incompatible value", out_error);
        return NULL;
    }
    payload_size = kind == TINYPY_VALUE_STRING || kind == TINYPY_VALUE_UNICODE || kind == TINYPY_VALUE_LONG
                       ? tinypy_internal_variable_builtin_payload_size(value)
                       : vm->types[kind].basic_size;
    if (type == &vm->types[kind]) {
        result = tinypy_internal_object_allocate_checked(vm, &vm->types[kind], payload_size, out_error);
        if (result == NULL) {
            TINYPY_DECREF(value);
            return NULL;
        }
        (void)memcpy((uint8_t *)result + sizeof(tinypy_value_t), (const uint8_t *)value + sizeof(tinypy_value_t), payload_size - sizeof(tinypy_value_t));
        if (kind == TINYPY_VALUE_STRING) {
            TINYPY_STRING_OBJECT(result)->interned = TINYPY_FALSE;
        }
        else if (kind == TINYPY_VALUE_UNICODE) {
            TINYPY_UNICODE_OBJECT(result)->index_offsets = NULL;
        }
        TINYPY_DECREF(value);
        return result;
    }
    allocation_size = tinypy_internal_builtin_subclass_allocation_size(type, payload_size);
    if (allocation_size == 0U) {
        TINYPY_DECREF(value);
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_OVERFLOW, "immutable subtype is too large", out_error);
        return NULL;
    }
    result = tinypy_internal_object_allocate_checked(vm, type, allocation_size, out_error);
    if (result == NULL) {
        TINYPY_DECREF(value);
        return NULL;
    }
    (void)memcpy((uint8_t *)result + sizeof(tinypy_value_t), (const uint8_t *)value + sizeof(tinypy_value_t), payload_size - sizeof(tinypy_value_t));
    if (kind == TINYPY_VALUE_STRING) {
        TINYPY_STRING_OBJECT(result)->interned = TINYPY_FALSE;
    }
    else if (kind == TINYPY_VALUE_UNICODE) {
        TINYPY_UNICODE_OBJECT(result)->index_offsets = NULL;
    }
    TINYPY_DECREF(value);
    return result;
}
//////////////////////////////////////////////////////////////////////////
size_t tinypy_internal_value_allocation_size(const tinypy_value_t *value) {
    size_t function_result;
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);

    switch (kind) {
    case TINYPY_VALUE_STRING:
        function_result = tinypy_internal_variable_builtin_payload_size(value);
        if (value->type != &TINYPY_VALUE_VM(value)->types[kind]) {
            function_result = tinypy_internal_builtin_subclass_allocation_size(value->type, function_result);
        }
        return function_result;
    case TINYPY_VALUE_UNICODE:
        function_result = tinypy_internal_variable_builtin_payload_size(value);
        if (value->type != &TINYPY_VALUE_VM(value)->types[kind]) {
            function_result = tinypy_internal_builtin_subclass_allocation_size(value->type, function_result);
        }
        return function_result;
    case TINYPY_VALUE_LONG:
        function_result = tinypy_internal_variable_builtin_payload_size(value);
        if (value->type != &TINYPY_VALUE_VM(value)->types[kind]) {
            function_result = tinypy_internal_builtin_subclass_allocation_size(value->type, function_result);
        }
        return function_result;
    case TINYPY_VALUE_TUPLE:
        if (value->type == &TINYPY_VALUE_VM(value)->types[TINYPY_VALUE_TUPLE]) {
            size_t return_value_1 = offsetof(tinypy_tuple_object_t, items) + TINYPY_SIZED_SIZE(value) * sizeof(tinypy_value_t *);
            return return_value_1;
        }
        return value->type->basic_size;
    case TINYPY_VALUE_FRAME:
        function_result = offsetof(tinypy_frame_object_t, locals_plus) + TINYPY_SIZED_SIZE(value) * sizeof(tinypy_value_t *);
        return function_result;
    default:
        return value->type->basic_size;
    }
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_value_destroy(tinypy_value_t *value) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    size_t allocation_size = tinypy_internal_value_allocation_size(value);
    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_STRING) {
        tinypy_internal_string_unintern(value);
    }
#if defined(TINYPY_CYCLE_DIAGNOSTICS)
    __tinypy_internal_cycle_diagnostics_value_unregister(vm, value);
#endif
    if (value->type != NULL && value->type->destroy != NULL) {
        value->type->destroy(value);
    }
    tinypy_internal_vm_deallocate(
        vm,
        value,
        allocation_size);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_internal_release_visit(tinypy_value_t *child, void *user_data) {
    (void)user_data;
    TINYPY_DECREF(child);
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_integer_free_list_finalize(tinypy_vm_t *vm) {
    while (vm->integer_free_list != NULL) {
        tinypy_integer_object_t *value = vm->integer_free_list;

        vm->integer_free_list = __tinypy_internal_integer_free_next(value);
        vm->integer_free_count -= 1U;
        vm->types[TINYPY_VALUE_INTEGER].base.base.ref -= 1;
#if defined(TINYPY_CYCLE_DIAGNOSTICS)
        __tinypy_internal_cycle_diagnostics_value_unregister(vm, &value->base);
#endif
        tinypy_internal_vm_deallocate(vm, value, sizeof(*value));
    }
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_value_is_vm_embedded(const tinypy_vm_t *vm, const tinypy_value_t *value) {
    /* Singletons, cached integers, builtin types and their dictionaries all
       live inside the VM allocation, so one address-range test covers them. */
    uintptr_t offset = (uintptr_t)value - (uintptr_t)vm;

    tinypy_bool_t embedded = offset < sizeof(*vm) ? TINYPY_TRUE : TINYPY_FALSE;
    return embedded;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_internal_value_finalize(tinypy_value_t *value) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_internal_exception_state_t exception_state;
    tinypy_value_t *args;
    tinypy_value_t *result;
    tinypy_error_t *error = NULL;

    if (vm->type_lookup_cache_epoch != 0U && value->type->finalizer_epoch == vm->type_lookup_cache_epoch && value->type->has_finalizer == 0 && value->type->has_classic_mro == 0 && value->type->has_custom_mro == 0 && (TINYPY_VALUE_KIND(value) != TINYPY_VALUE_OLD_INSTANCE || tinypy_internal_old_instance_has_special(value, "__del__", 7U) == 0)) {
        return TINYPY_FALSE;
    }
    value->ref = 1;
    tinypy_internal_exception_preserve_begin(vm, &exception_state);
    tinypy_type_t *type = value->type;
    uint64_t epoch = vm->type_lookup_cache_epoch;
    type->finalizer_epoch = epoch;
    type->has_finalizer = tinypy_internal_type_lookup_key(vm, type, vm->special_del_key) != NULL ? TINYPY_TRUE : TINYPY_FALSE;
    if (type->has_finalizer == 0 && (TINYPY_VALUE_KIND(value) != TINYPY_VALUE_OLD_INSTANCE
        || tinypy_internal_old_instance_has_special(value, "__del__", 7U) == 0)) {
        tinypy_internal_exception_preserve_end(vm, &exception_state);
        value->ref -= 1U;
        return value->ref != 0U ? TINYPY_TRUE : TINYPY_FALSE;
    }
    tinypy_value_t *method = tinypy_internal_object_get_special_key(value, vm->special_del_key, &error);
    if (method != NULL) {
        args = tinypy_tuple_from_items(vm, NULL, 0U);
        result = tinypy_call(method, args, NULL, &error);
        TINYPY_DECREF(args);
        if (result != NULL) {
            TINYPY_DECREF(result);
        }
        else {
            tinypy_internal_output_unraisable(vm, method);
        }
        TINYPY_DECREF(method);
    }
    if (error != NULL) {
        tinypy_internal_output_unraisable(vm, value);
        tinypy_error_release(error);
    }
    tinypy_internal_exception_preserve_end(vm, &exception_state);
    value->ref -= 1U;
    return value->ref != 0U ? TINYPY_TRUE : TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
/* A generator abandoned while suspended is closed first, so that try/finally
   and with blocks in its body still run, as gen_dealloc does in Python 2.7. */
static tinypy_bool_t __tinypy_internal_value_finalize_generator(tinypy_value_t *value) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_generator_object_t *generator = TINYPY_GENERATOR_OBJECT(value);
    tinypy_internal_exception_state_t exception_state;
    tinypy_error_t *error = NULL;

    if (vm->state != TINYPY_VM_STATE_LIVE || generator->running != 0 || generator->finished != 0 || generator->frame == NULL) {
        return TINYPY_FALSE;
    }
    value->ref = 1;
    tinypy_internal_exception_preserve_begin(vm, &exception_state);
    (void)tinypy_generator_close(value, &error);
    if (error != NULL) {
        tinypy_internal_output_unraisable(vm, value);
        tinypy_error_release(error);
    }
    tinypy_internal_exception_preserve_end(vm, &exception_state);
    value->ref -= 1U;
    return value->ref != 0U ? TINYPY_TRUE : TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_internal_value_release_contents(tinypy_value_t *value) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_type_t *type = value->type;

    tinypy_bool_t cache_native = type == &vm->types[TINYPY_VALUE_NATIVE_FUNCTION]
        && TINYPY_NATIVE_FUNCTION_OBJECT(value)->self != NULL
        && TINYPY_NATIVE_FUNCTION_OBJECT(value)->finalize == NULL;
    vm->release_depth += 1U;
    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_TYPE) {
        tinypy_internal_type_lookup_cache_invalidate(vm);
        tinypy_internal_type_detach((tinypy_type_t *)value);
    }
    if (type != NULL && type->release_references != NULL) {
        type->release_references(value, __tinypy_internal_release_visit, NULL);
    }
    if (type == &vm->types[TINYPY_VALUE_FRAME] && vm->state == TINYPY_VM_STATE_LIVE && vm->frame_free_count < TINYPY_FRAME_FREE_LIST_MAX) {
        tinypy_internal_frame_free_list_push(vm, value);
        vm->release_depth -= 1U;
        return;
    }
    if (type == &vm->types[TINYPY_VALUE_METHOD] && vm->state == TINYPY_VM_STATE_LIVE && vm->method_free_count < TINYPY_METHOD_FREE_LIST_MAX) {
        tinypy_internal_method_free_list_push(vm, value);
        vm->release_depth -= 1U;
        return;
    }
    if (cache_native != 0 && vm->state == TINYPY_VM_STATE_LIVE && vm->native_method_free_count < TINYPY_NATIVE_METHOD_FREE_LIST_MAX) {
        tinypy_internal_native_method_free_list_push(vm, value);
        vm->release_depth -= 1U;
        return;
    }
    tinypy_internal_value_destroy(value);
    if (type != NULL) {
        __tinypy_internal_release_visit(&type->base.base, NULL);
    }
    vm->release_depth -= 1U;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_value_release_zero(tinypy_value_t *value) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_type_t *type = value->type;

    if (type == &vm->types[TINYPY_VALUE_INTEGER] && vm->state == TINYPY_VM_STATE_LIVE && vm->integer_free_count < TINYPY_INTEGER_FREE_LIST_MAX) {
        tinypy_integer_object_t *integer = TINYPY_INTEGER_OBJECT(value);

        __tinypy_internal_integer_set_free_next(integer, vm->integer_free_list);
        vm->integer_free_list = integer;
        vm->integer_free_count += 1U;
        return;
    }
    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_GENERATOR && __tinypy_internal_value_finalize_generator(value) != 0) {
        return;
    }
    if (type->weakref_offset != 0U) {
        tinypy_internal_weakref_clear(value);
    }
    if (vm->special_del_key != NULL && type->dict != NULL && vm->exception_types[TINYPY_EXCEPTION_BASE] != NULL && (type->finalizer_epoch != vm->type_lookup_cache_epoch || vm->type_lookup_cache_epoch == 0U || type->has_finalizer != 0 || type->has_classic_mro != 0 || type->has_custom_mro != 0 || TINYPY_VALUE_KIND(value) == TINYPY_VALUE_OLD_INSTANCE)) {
        if (__tinypy_internal_value_finalize(value) != 0) {
            return;
        }
    }
    if ((TINYPY_VALUE_KIND(value) == TINYPY_VALUE_NATIVE_INSTANCE && value->type->native_spec.finalize != NULL)
        || (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_NATIVE_FUNCTION && TINYPY_NATIVE_FUNCTION_OBJECT(value)->finalize != NULL)) {
        value->ref = 1;
        if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_NATIVE_INSTANCE) {
            tinypy_internal_native_instance_finalize(value);
        }
        else {
            tinypy_internal_native_function_finalize(value);
        }
        value->ref -= 1U;
        if (value->ref != 0U) {
            return;
        }
    }
    /* Finalizers may replace __class__ and create new weak references. */
    type = value->type;
    if (type->weakref_offset != 0U) {
        tinypy_internal_weakref_clear(value);
    }
    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_STRING) {
        tinypy_internal_string_unintern(value);
    }
    /* Only already-finalized zero-ref objects are deferred. Their ref word
       temporarily links the queue; no registry or extra allocation is needed. */
    if (vm->release_depth >= 32U) {
        value->ref = (tinypy_ref_t)(uintptr_t)vm->pending_releases;
        vm->pending_releases = value;
        return;
    }
    __tinypy_internal_value_release_contents(value);
    while (vm->release_depth == 0U && vm->pending_releases != NULL) {
        tinypy_value_t *pending = vm->pending_releases;

        vm->pending_releases = (tinypy_value_t *)(uintptr_t)pending->ref;
        pending->ref = 0;
        __tinypy_internal_value_release_contents(pending);
    }
}

//////////////////////////////////////////////////////////////////////////
void tinypy_release(tinypy_value_t *value) {
    TINYPY_DECREF(value);
}
//////////////////////////////////////////////////////////////////////////
static inline size_t __tinypy_internal_text_allocation_size(tinypy_value_type_e type, size_t byte_size) {
    size_t object_size = type == TINYPY_VALUE_STRING
                             ? offsetof(tinypy_string_object_t, bytes)
                             : offsetof(tinypy_unicode_object_t, utf8);

    if (byte_size > SIZE_MAX - object_size - 1U) {
        return 0U;
    }
    return object_size + byte_size + 1U;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_text_allocate_uninitialized(tinypy_vm_t *vm, tinypy_value_type_e type, size_t byte_size, size_t code_point_count, uint8_t **out_bytes, tinypy_bool_t checked, tinypy_error_t **out_error) {
    size_t allocation_size = __tinypy_internal_text_allocation_size(type, byte_size);
    uint8_t *payload;
    tinypy_value_t *value;

    if (allocation_size == 0U) {
        if (checked != 0) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_MEMORY, "text object is too large", out_error);
        }
        return NULL;
    }
    value = checked != 0
                ? tinypy_internal_object_allocate_checked(vm, &vm->types[type], allocation_size, out_error)
                : tinypy_internal_value_allocate(vm, type, allocation_size);
    if (value == NULL) {
        return NULL;
    }
    if (type == TINYPY_VALUE_STRING) {
        TINYPY_SIZED_SIZE(value) = byte_size;
        TINYPY_STRING_OBJECT(value)->interned = byte_size <= 1U ? INT32_C(1) : INT32_C(0);
        payload = TINYPY_STRING_OBJECT(value)->bytes;
    }
    else {
        TINYPY_SIZED_SIZE(value) = code_point_count;
        TINYPY_UNICODE_OBJECT(value)->byte_size = byte_size;
        TINYPY_UNICODE_OBJECT(value)->index_offsets = NULL;
        payload = TINYPY_UNICODE_OBJECT(value)->utf8;
    }
    payload[byte_size] = 0U;
    *out_bytes = payload;
    return value;
}
//////////////////////////////////////////////////////////////////////////
/* Grows a byte string in place, as string_concatenate does when the left
   operand is about to be rebound: the caller holds the only reference, so
   the object may move. NULL leaves the string untouched. */
tinypy_value_t *tinypy_internal_string_concat_in_place(tinypy_vm_t *vm, tinypy_value_t *left, const uint8_t *bytes, size_t size) {
    size_t old_size = TINYPY_SIZED_SIZE(left);
    size_t old_allocation = __tinypy_internal_text_allocation_size(TINYPY_VALUE_STRING, old_size);
    size_t new_allocation;
    tinypy_value_t *grown;

    if (size > SIZE_MAX - old_size) {
        return NULL;
    }
    new_allocation = __tinypy_internal_text_allocation_size(TINYPY_VALUE_STRING, old_size + size);
    if (new_allocation == 0U) {
        return NULL;
    }
#if defined(TINYPY_CYCLE_DIAGNOSTICS)
    __tinypy_internal_cycle_diagnostics_value_unregister(vm, left);
#endif
    grown = (tinypy_value_t *)tinypy_internal_vm_reallocate_checked(vm, left, old_allocation, new_allocation, NULL);
    if (grown == NULL) {
#if defined(TINYPY_CYCLE_DIAGNOSTICS)
        __tinypy_internal_cycle_diagnostics_value_register(vm, left);
#endif
        tinypy_internal_exception_clear_raised(vm);
        return NULL;
    }
#if defined(TINYPY_CYCLE_DIAGNOSTICS)
    __tinypy_internal_cycle_diagnostics_value_register(vm, grown);
#endif
    (void)memcpy(TINYPY_STRING_OBJECT(grown)->bytes + old_size, bytes, size);
    TINYPY_STRING_OBJECT(grown)->bytes[old_size + size] = 0U;
    TINYPY_SIZED_SIZE(grown) = old_size + size;
    TINYPY_STRING_OBJECT(grown)->hash_computed = INT32_C(0);
    TINYPY_STRING_OBJECT(grown)->interned = INT32_C(0);
    return grown;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_text_allocate_uninitialized(tinypy_vm_t *vm, tinypy_value_type_e type, size_t byte_size, size_t code_point_count, uint8_t **out_bytes) {
    tinypy_value_t *value = __tinypy_internal_text_allocate_uninitialized(vm, type, byte_size, code_point_count, out_bytes, TINYPY_FALSE, NULL);

    return value;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_text_allocate_uninitialized_checked(tinypy_vm_t *vm, tinypy_value_type_e type, size_t byte_size, size_t code_point_count, uint8_t **out_bytes, tinypy_error_t **out_error) {
    tinypy_value_t *value = __tinypy_internal_text_allocate_uninitialized(vm, type, byte_size, code_point_count, out_bytes, TINYPY_TRUE, out_error);

    return value;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_text_from_bytes(tinypy_vm_t *vm, const uint8_t *bytes, size_t byte_size, size_t code_point_count, tinypy_value_type_e type) {
    uint8_t *payload;
    tinypy_value_t *value = tinypy_internal_text_allocate_uninitialized(vm, type, byte_size, code_point_count, &payload);

    if (value != NULL && byte_size != 0U) {
        (void)memcpy(payload, bytes, byte_size);
    }
    return value;
}
//////////////////////////////////////////////////////////////////////////
const uint8_t *tinypy_internal_text_bytes(const tinypy_value_t *value) {
    const uint8_t *return_value_1 = TINYPY_VALUE_KIND(value) == TINYPY_VALUE_STRING
                   ? TINYPY_STRING_OBJECT(value)->bytes
                   : TINYPY_UNICODE_OBJECT(value)->utf8;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
size_t tinypy_internal_text_byte_size(const tinypy_value_t *value) {
    size_t return_value_1 = TINYPY_VALUE_KIND(value) == TINYPY_VALUE_STRING
                   ? TINYPY_SIZED_SIZE(value)
                   : TINYPY_UNICODE_OBJECT(value)->byte_size;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_string_is_interned(const tinypy_value_t *value) {
    tinypy_bool_t return_value_1 = TINYPY_STRING_OBJECT(value)->interned;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_string_set_interned(tinypy_value_t *value, tinypy_bool_t interned) {
    TINYPY_STRING_OBJECT(value)->interned = interned != 0 ? INT32_C(1) : INT32_C(0);
}

//////////////////////////////////////////////////////////////////////////
static size_t __tinypy_internal_utf8_code_point_count(const uint8_t *bytes, size_t size) {
    size_t offset = 0U;
    size_t code_point_count = 0U;

    while (offset < size) {
        uint8_t first = bytes[offset];
        size_t width;

        if (first <= 0x7fU) {
            width = 1U;
        }
        else if (first < 0xe0U) {
            width = 2U;
        }
        else if (first < 0xf0U) {
            width = 3U;
        }
        else {
            width = 4U;
        }

        offset += width;
        code_point_count += 1U;
    }

    return code_point_count;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_value_belongs_to(const tinypy_vm_t *vm, const tinypy_value_t *value) {
    tinypy_bool_t belongs = value != NULL && TINYPY_VALUE_VM(value) == vm;
    return belongs;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_none_get(tinypy_vm_t *vm) {
    tinypy_value_t *result = &vm->none_object.base;
    TINYPY_INCREF(result);

    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_not_implemented_get(tinypy_vm_t *vm) {
    tinypy_value_t *result = &vm->not_implemented_object.base;
    TINYPY_INCREF(result);
    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_ellipsis_get(tinypy_vm_t *vm) {
    tinypy_value_t *result = &vm->ellipsis_object.base;
    TINYPY_INCREF(result);
    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_bool_from_i32(tinypy_vm_t *vm, int32_t value) {
    tinypy_value_t *result = value != 0
                 ? &vm->true_object.base
                 : &vm->false_object.base;
    TINYPY_INCREF(result);

    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_integer_from_i64(tinypy_vm_t *vm, int64_t value) {
    tinypy_value_t *return_value_1 = __tinypy_internal_integer_from_i64_fast(vm, value);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_string_from_bytes(tinypy_vm_t *vm, const void *bytes, size_t size, tinypy_bool_t checked, tinypy_error_t **out_error) {
    if (size == 0U) {
        tinypy_value_t *result = &vm->empty_string_object.base.base;

        TINYPY_INCREF(result);
        return result;
    }
    if (size == 1U) {
        size_t cache_index = (size_t)*(const uint8_t *)bytes;
        tinypy_value_t *cached = vm->string_char_cache[cache_index];

        if (cached != NULL) {
            TINYPY_INCREF(cached);
            return cached;
        }
        tinypy_value_t *result;

        if (checked != 0) {
            uint8_t *output;
            result = tinypy_internal_text_allocate_uninitialized_checked(vm, TINYPY_VALUE_STRING, 1U, 0U, &output, out_error);

            if (result == NULL) {
                return NULL;
            }
            output[0] = *(const uint8_t *)bytes;
        }
        else {
            result = __tinypy_internal_text_from_bytes(vm, (const uint8_t *)bytes, 1U, 0U, TINYPY_VALUE_STRING);

            if (result == NULL) {
                return NULL;
            }
        }
        vm->string_char_cache[cache_index] = result;
        TINYPY_INCREF(result);
        return result;
    }
    if (checked != 0) {
        uint8_t *output;
        tinypy_value_t *result = tinypy_internal_text_allocate_uninitialized_checked(vm, TINYPY_VALUE_STRING, size, 0U, &output, out_error);

        if (result != NULL) {
            (void)memcpy(output, bytes, size);
        }
        return result;
    }
    tinypy_value_t *result = __tinypy_internal_text_from_bytes(vm, (const uint8_t *)bytes, size, 0U, TINYPY_VALUE_STRING);
    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_string_from_bytes(tinypy_vm_t *vm, const void *bytes, size_t size) {
    tinypy_value_t *result = __tinypy_internal_string_from_bytes(vm, bytes, size, TINYPY_FALSE, NULL);

    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_string_from_bytes_checked(tinypy_vm_t *vm, const void *bytes, size_t size, tinypy_error_t **out_error) {
    tinypy_value_t *result = __tinypy_internal_string_from_bytes(vm, bytes, size, TINYPY_TRUE, out_error);

    return result;
}
//////////////////////////////////////////////////////////////////////////
const void *tinypy_string_view(const tinypy_value_t *value, size_t *out_size) {
    *out_size = TINYPY_SIZED_SIZE(value);
    const void *bytes = TINYPY_STRING_OBJECT(value)->bytes;
    return bytes;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_unicode_from_utf8(tinypy_vm_t *vm, const char *utf8, size_t size) {
    size_t code_point_count;
    size_t cache_index = SIZE_MAX;

    if (__tinypy_internal_text_allocation_size(TINYPY_VALUE_UNICODE, size) == 0U) {
        return NULL;
    }
    if (size == 1U && (uint8_t)utf8[0] < 0x80U) {
        cache_index = (size_t)(uint8_t)utf8[0];
    }
    else if (size == 2U && ((uint8_t)utf8[0] == 0xc2U || (uint8_t)utf8[0] == 0xc3U) && ((uint8_t)utf8[1] & 0xc0U) == 0x80U) {
        cache_index = (size_t)((((uint32_t)(uint8_t)utf8[0] & 0x1fU) << 6U) | ((uint32_t)(uint8_t)utf8[1] & 0x3fU));
    }
    if (cache_index != SIZE_MAX && vm->unicode_char_cache[cache_index] != NULL) {
        tinypy_value_t *cached = vm->unicode_char_cache[cache_index];

        TINYPY_INCREF(cached);
        return cached;
    }

    code_point_count = __tinypy_internal_utf8_code_point_count(
        (const uint8_t *)utf8,
        size);

    tinypy_value_t *result = __tinypy_internal_text_from_bytes(
        vm,
        (const uint8_t *)utf8,
        size,
        code_point_count,
        TINYPY_VALUE_UNICODE);
    if (cache_index != SIZE_MAX) {
        vm->unicode_char_cache[cache_index] = result;
        TINYPY_INCREF(result);
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
const char *tinypy_unicode_utf8_view(const tinypy_value_t *value, size_t *out_size, size_t *out_code_point_count) {

    *out_size = TINYPY_UNICODE_OBJECT(value)->byte_size;
    *out_code_point_count = TINYPY_SIZED_SIZE(value);
    const char *return_value_1 = (const char *)TINYPY_UNICODE_OBJECT(value)->utf8;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_type_e tinypy_typeof(const tinypy_value_t *value) {

    tinypy_value_type_e return_value_1 = TINYPY_VALUE_KIND(value);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_vm_t *tinypy_value_vm(const tinypy_value_t *value) {
    tinypy_vm_t *return_value_1 = TINYPY_VALUE_VM(value);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_ref_t tinypy_refcount(const tinypy_value_t *value) {
    return value->ref;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_is_callable(const tinypy_value_t *value) {
    tinypy_bool_t return_value_1 = value->type->call != NULL || tinypy_internal_object_has_special((tinypy_value_t *)value, "__call__", 8U) != 0 ? TINYPY_TRUE : TINYPY_FALSE;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
const tinypy_type_t *tinypy_object_type(const tinypy_value_t *value) {
    return value->type;
}
//////////////////////////////////////////////////////////////////////////
const char *tinypy_type_name(const tinypy_type_t *type, size_t *out_size) {
    *out_size = type->name_size;
    return type->name;
}
//////////////////////////////////////////////////////////////////////////
const tinypy_type_t *tinypy_type_metaclass(const tinypy_type_t *type) {
    return type->base.base.type;
}
//////////////////////////////////////////////////////////////////////////
const tinypy_type_t *tinypy_type_base(const tinypy_type_t *type) {
    return type->base_type;
}
//////////////////////////////////////////////////////////////////////////
const tinypy_value_t *tinypy_type_dict(const tinypy_type_t *type) {
    return type->dict;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_type_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data) {
    tinypy_type_t *type = (tinypy_type_t *)value;

    if (type->name_object != NULL) {
        visit(type->name_object, user_data);
    }
    if (type->dict != NULL) {
        visit(type->dict, user_data);
    }
    if (type->bases != NULL) {
        visit(type->bases, user_data);
    }
    if (type->subclasses != NULL) {
        visit(type->subclasses, user_data);
    }
    if (type->mro != NULL && (type->flags & TINYPY_TYPE_FLAG_HEAP) != 0U) {
        for (size_t index = 0U; index < TINYPY_TUPLE_SIZE(type->mro); ++index) {
            tinypy_value_t *entry = TINYPY_TUPLE_GET(type->mro, index);

            if (entry != value) {
                visit(entry, user_data);
            }
        }
    }
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_type_destroy(tinypy_value_t *value) {
    tinypy_type_t *type = (tinypy_type_t *)value;

    if (type->mro != NULL && tinypy_internal_value_is_vm_embedded(type->vm, type->mro) == 0) {
        tinypy_internal_value_destroy(type->mro);
        TINYPY_DECREF(&type->vm->types[TINYPY_VALUE_TUPLE].base.base);
    }
}
//////////////////////////////////////////////////////////////////////////
int32_t tinypy_bool_as_i32(const tinypy_value_t *value) {

    int32_t return_value_1 = (int32_t)TINYPY_INTEGER_VALUE(value);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
int64_t tinypy_integer_as_i64(const tinypy_value_t *value) {
    tinypy_value_type_e kind;

    kind = TINYPY_VALUE_KIND(value);

    if (kind == TINYPY_VALUE_BOOL) {
        int64_t return_value_1 = TINYPY_INTEGER_VALUE(value) != 0 ? INT64_C(1) : INT64_C(0);
        return return_value_1;
    }

    int64_t return_value_2 = TINYPY_INTEGER_VALUE(value);
    return return_value_2;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_retain(tinypy_value_t *value) {
    TINYPY_INCREF(value);
}
