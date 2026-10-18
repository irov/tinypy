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
    tinypy_value_t *value = (tinypy_value_t *)__tinypy_internal_vm_allocate_checked(vm, allocation_size, out_error);

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
/* The room an in-place concatenation reserves for byte_count bytes: eight
   classes per power of two, so that a string grown by repeated concatenation
   is copied a total proportional to its final size, whatever the host
   reallocate does. */
static size_t __tinypy_internal_string_capacity(size_t byte_count) {
    size_t mask = byte_count - 1U;

    mask |= mask >> 1U;
    mask |= mask >> 2U;
    mask |= mask >> 4U;
    mask |= mask >> 8U;
    mask |= mask >> 16U;
    mask |= mask >> 16U >> 16U;
    size_t granule = (mask >> 4U) + 1U;
    if (byte_count > SIZE_MAX - (granule - 1U)) {
        return byte_count;
    }
    size_t capacity = (byte_count + granule - 1U) & ~(granule - 1U);
    return capacity;
}
//////////////////////////////////////////////////////////////////////////
size_t tinypy_internal_variable_builtin_payload_size(const tinypy_value_t *value) {
    size_t result;

    switch (TINYPY_VALUE_KIND(value)) {
    case TINYPY_VALUE_STRING:
        result = TINYPY_SIZED_SIZE(value) + 1U;
        if (TINYPY_STRING_OBJECT(value)->over_allocated != 0) {
            result = __tinypy_internal_string_capacity(result);
        }
        result += offsetof(tinypy_string_object_t, bytes);
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
            TINYPY_STRING_OBJECT(result)->internal_metadata = NULL;
        }
        else if (kind == TINYPY_VALUE_UNICODE) {
            TINYPY_UNICODE_OBJECT(result)->index_offsets = NULL;
            TINYPY_UNICODE_OBJECT(result)->native_buffer = NULL;
            TINYPY_UNICODE_OBJECT(result)->wchar_buffer = NULL;
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
        TINYPY_STRING_OBJECT(result)->internal_metadata = NULL;
    }
    else if (kind == TINYPY_VALUE_UNICODE) {
        TINYPY_UNICODE_OBJECT(result)->index_offsets = NULL;
        TINYPY_UNICODE_OBJECT(result)->native_buffer = NULL;
        TINYPY_UNICODE_OBJECT(result)->wchar_buffer = NULL;
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
/* A finalizer counts towards the trashcan of the releases it runs. */
static void __tinypy_internal_value_finalizer_enter(tinypy_vm_t *vm) {
    vm->finalizer_depth += 1U;
}
//////////////////////////////////////////////////////////////////////////
/* Py_TRASHCAN_SAFE_END: the outermost finalizer, before it lets its object
   go, releases the objects that nested ones left. Those count as nested in
   turn, so that what their releases leave joins the loop. */
static TINYPY_NOINLINE void __tinypy_internal_value_release_pending_finalizations(tinypy_vm_t *vm) {
    while (vm->pending_finalizations != NULL) {
        tinypy_value_t *pending = vm->pending_finalizations;

        vm->pending_finalizations = (tinypy_value_t *)(uintptr_t)pending->ref;
        pending->ref = 0;
        tinypy_internal_value_release_zero(pending);
    }
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_internal_value_finalizer_leave(tinypy_vm_t *vm) {
    if (vm->finalizer_depth == 1U && vm->pending_finalizations != NULL) {
        __tinypy_internal_value_release_pending_finalizations(vm);
    }
    vm->finalizer_depth -= 1U;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_internal_value_finalize(tinypy_value_t *value) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_internal_exception_state_t exception_state;
    tinypy_value_t *args;
    tinypy_value_t *result;
    tinypy_error_t *error = NULL;

    if (value->type->version != 0U && value->type->finalizer_version == value->type->version && value->type->has_finalizer == 0 && (TINYPY_VALUE_KIND(value) != TINYPY_VALUE_OLD_INSTANCE || tinypy_internal_old_instance_has_special_key(value, vm->internal_special_del_key) == 0)) {
        return TINYPY_FALSE;
    }
    value->ref = 1;
    tinypy_internal_exception_preserve_begin(vm, &exception_state);
    tinypy_type_t *type = value->type;
    uint64_t version = __tinypy_internal_type_version(type);
    /* The lookup may run key comparisons that release other instances of the
       type or reassign the class, so the type is held and the memo published
       only after the lookup, and only when it left the type unchanged. */
    TINYPY_INCREF(&type->base.base);
    tinypy_bool_t has_finalizer = tinypy_internal_type_lookup_key(vm, type, vm->internal_special_del_key) != NULL ? TINYPY_TRUE : TINYPY_FALSE;
    type->has_finalizer = has_finalizer;
    if (type->version == version) {
        type->finalizer_version = version;
    }
    TINYPY_DECREF(&type->base.base);
    if (has_finalizer == 0 && (TINYPY_VALUE_KIND(value) != TINYPY_VALUE_OLD_INSTANCE
        || tinypy_internal_old_instance_has_special_key(value, vm->internal_special_del_key) == 0)) {
        tinypy_internal_exception_preserve_end(vm, &exception_state);
        value->ref -= 1U;
        return value->ref != 0U ? TINYPY_TRUE : TINYPY_FALSE;
    }
    __tinypy_internal_value_finalizer_enter(vm);
    tinypy_value_t *method = tinypy_internal_object_get_special_key(value, vm->internal_special_del_key, &error);
    if (method != NULL) {
        args = TINYPY_RET_EMPTY_TUPLE(vm);
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
    /* slot_tp_del and instance_dealloc restore the exception they saved over
       an error of the lookup of __del__, which nothing reports. */
    if (error != NULL) {
        tinypy_error_release(error);
        tinypy_internal_exception_clear_raised(vm);
    }
    __tinypy_internal_value_finalizer_leave(vm);
    tinypy_internal_exception_preserve_end(vm, &exception_state);
    value->ref -= 1U;
    return value->ref != 0U ? TINYPY_TRUE : TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
/* A generator abandoned while suspended is closed first, so that try/finally
   and with blocks in its body still run, as gen_dealloc does in Python 2.7. */
static tinypy_bool_t __tinypy_internal_value_generator_suspended(tinypy_value_t *value) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_generator_object_t *generator = TINYPY_GENERATOR_OBJECT(value);

    tinypy_bool_t suspended = vm->state == TINYPY_VM_STATE_LIVE && generator->running == 0 && generator->finished == 0 && generator->frame != NULL ? TINYPY_TRUE : TINYPY_FALSE;
    return suspended;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_internal_value_finalize_generator(tinypy_value_t *value) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_internal_exception_state_t exception_state;
    tinypy_error_t *error = NULL;

    if (__tinypy_internal_value_generator_suspended(value) == 0) {
        return TINYPY_FALSE;
    }
    value->ref = 1;
    tinypy_internal_exception_preserve_begin(vm, &exception_state);
    __tinypy_internal_value_finalizer_enter(vm);
    (void)tinypy_generator_close(value, &error);
    if (error != NULL) {
        tinypy_internal_output_unraisable(vm, value);
        tinypy_error_release(error);
    }
    __tinypy_internal_value_finalizer_leave(vm);
    tinypy_internal_exception_preserve_end(vm, &exception_state);
    value->ref -= 1U;
    return value->ref != 0U ? TINYPY_TRUE : TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_internal_value_release_contents(tinypy_value_t *value) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_type_t *type = value->type;

    tinypy_bool_t cache_native = (type == &vm->types[TINYPY_VALUE_NATIVE_FUNCTION] || type == vm->native_method_wrapper_type)
        && TINYPY_NATIVE_FUNCTION_OBJECT(value)->self != NULL
        && TINYPY_NATIVE_FUNCTION_OBJECT(value)->finalize == NULL;
    vm->release_depth += 1U;
    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_TYPE) {
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
/* A native instance whose finalizer ran, as tinypy_vm_destroy runs it on a
   reachable instance, has no host object left for its __del__, which runs
   before the finalizer and never after it. */
static tinypy_bool_t __tinypy_internal_value_native_finalized(const tinypy_value_t *value) {
    tinypy_bool_t finalized = TINYPY_VALUE_KIND(value) == TINYPY_VALUE_NATIVE_INSTANCE && TINYPY_NATIVE_INSTANCE_OBJECT(value)->finalization != TINYPY_NATIVE_FINALIZATION_NONE ? TINYPY_TRUE : TINYPY_FALSE;

    return finalized;
}
//////////////////////////////////////////////////////////////////////////
/* Whether the __del__ of a dying object may have to run: the memo of its
   type says so or is out of date. */
static tinypy_bool_t __tinypy_internal_value_may_have_del(tinypy_value_t *value) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_type_t *type = value->type;

    tinypy_bool_t may_have_del = vm->internal_special_del_key != NULL && type->dict != NULL && vm->exception_types[TINYPY_EXCEPTION_BASE] != NULL && (type->finalizer_version != type->version || type->version == 0U || type->has_finalizer != 0 || TINYPY_VALUE_KIND(value) == TINYPY_VALUE_OLD_INSTANCE) && __tinypy_internal_value_native_finalized(value) == 0 ? TINYPY_TRUE : TINYPY_FALSE;
    return may_have_del;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_internal_value_has_native_finalizer(tinypy_value_t *value) {
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);

    tinypy_bool_t has_finalizer = (kind == TINYPY_VALUE_NATIVE_INSTANCE && value->type->native_spec.finalize != NULL)
        || (kind == TINYPY_VALUE_NATIVE_FUNCTION && TINYPY_NATIVE_FUNCTION_OBJECT(value)->finalize != NULL) ? TINYPY_TRUE : TINYPY_FALSE;
    return has_finalizer;
}
//////////////////////////////////////////////////////////////////////////
/* Py_TRASHCAN_SAFE_BEGIN: a finalizer nested too deep leaves a dying object
   whose generator close or __del__ is known to run, once no weak reference
   reaches it, to the outermost finalizer, linked through its ref word, so
   that a long chain of them does not exhaust the native stack. Whether a type
   has __del__ is taken from its last lookup, made by class creation, rebases
   and finalizers, rather than from a current memo, which a __del__ assigning
   a class attribute withdraws. A value with a native finalizer runs it at
   once: until then the host may hand the value back to Python. */
static TINYPY_NOINLINE tinypy_bool_t __tinypy_internal_value_defer_finalizers(tinypy_value_t *value) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);
    tinypy_value_t **weakrefs = tinypy_internal_weakref_head_slot(value);
    tinypy_bool_t known;

    if ((weakrefs != NULL && *weakrefs != NULL) || __tinypy_internal_value_has_native_finalizer(value) != 0) {
        return TINYPY_FALSE;
    }
    if (kind == TINYPY_VALUE_GENERATOR) {
        known = __tinypy_internal_value_generator_suspended(value);
    }
    else if (kind == TINYPY_VALUE_OLD_INSTANCE) {
        known = tinypy_internal_old_instance_has_special_key(value, vm->internal_special_del_key);
    }
    else {
        known = value->type->has_finalizer;
    }
    if (known == 0) {
        return TINYPY_FALSE;
    }
    value->ref = (tinypy_ref_t)(uintptr_t)vm->pending_finalizations;
    vm->pending_finalizations = value;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
/* Runs the generator close, __del__ and native finalizers of a dying object;
   returns TRUE when one of them resurrected it or the object waits for
   them. */
static tinypy_bool_t __tinypy_internal_value_run_finalizers(tinypy_value_t *value) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);

    if (TINYPY_VALUE_KIND(value) != TINYPY_VALUE_GENERATOR && __tinypy_internal_value_may_have_del(value) == 0 && __tinypy_internal_value_has_native_finalizer(value) == 0) {
        return TINYPY_FALSE;
    }
    if (vm->finalizer_depth >= TINYPY_FINALIZER_DEFER_DEPTH && __tinypy_internal_value_defer_finalizers(value) != 0) {
        return TINYPY_TRUE;
    }
    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_GENERATOR && __tinypy_internal_value_finalize_generator(value) != 0) {
        return TINYPY_TRUE;
    }
    if (__tinypy_internal_value_may_have_del(value) != 0 && __tinypy_internal_value_finalize(value) != 0) {
        /* A native instance resurrected before its finalizer still needs
           one, as a new instance does. */
        if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_NATIVE_INSTANCE && value->type->native_spec.finalize != NULL) {
            vm->native_finalizable_created += 1U;
        }
        return TINYPY_TRUE;
    }
    if (__tinypy_internal_value_has_native_finalizer(value) != 0) {
        value->ref = 1;
        __tinypy_internal_value_finalizer_enter(vm);
        if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_NATIVE_INSTANCE) {
            tinypy_internal_native_instance_finalize(value);
        }
        else {
            tinypy_internal_native_function_finalize(value);
        }
        __tinypy_internal_value_finalizer_leave(vm);
        value->ref -= 1U;
        if (value->ref != 0U) {
            return TINYPY_TRUE;
        }
    }
    return TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
/* Clears the weak references of a dying object and runs its finalizers;
   returns TRUE when one of them resurrected it or the object waits for
   them. */
static tinypy_bool_t __tinypy_internal_value_release_finalize(tinypy_value_t *value) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_type_t *type = value->type;

    if (type->weakref_offset != 0U) {
        tinypy_internal_weakref_clear(value, TINYPY_FALSE);
    }
    /* The garbage a finalizer releases directly is not deferred: a deferred
       frame or bound method would still refer to the object and make it look
       resurrected. subtype_dealloc lowers the trashcan nesting the same way. */
    size_t release_depth = vm->release_depth;
    if (release_depth >= TINYPY_RELEASE_DEFER_DEPTH) {
        vm->release_depth = TINYPY_RELEASE_DEFER_DEPTH - 1U;
    }
    tinypy_bool_t resurrected = __tinypy_internal_value_run_finalizers(value);
    vm->release_depth = release_depth;
    if (resurrected != 0) {
        return TINYPY_TRUE;
    }
    /* Finalizers may replace __class__ and create new weak references. */
    type = value->type;
    if (type->weakref_offset != 0U) {
        tinypy_internal_weakref_clear(value, TINYPY_TRUE);
    }
    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_STRING) {
        tinypy_internal_string_unintern(value);
    }
    /* A dying weak reference leaves its referent's list before its dict and
       slots are released or deferred: they may hold the last reference to
       the referent, whose callbacks must not see it (CPython bug #3110). */
    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_WEAKREF) {
        tinypy_internal_weakref_destroy(value);
    }
    return TINYPY_FALSE;
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
    /* tupledealloc, list_dealloc and the like release the contents at once. */
    tinypy_bool_t plain = (type->flags & TINYPY_TYPE_FLAG_PLAIN_RELEASE) != 0U && (type != &vm->types[TINYPY_VALUE_STRING] || TINYPY_STRING_OBJECT(value)->interned == 0) ? TINYPY_TRUE : TINYPY_FALSE;
    if (plain == 0) {
        tinypy_bool_t resurrected = __tinypy_internal_value_release_finalize(value);

        if (resurrected != 0) {
            return;
        }
    }
    /* Only already-finalized zero-ref objects are deferred. Their ref word
       temporarily links the queue; no registry or extra allocation is needed. */
    if (vm->release_depth >= TINYPY_RELEASE_DEFER_DEPTH) {
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
    if (type == TINYPY_VALUE_UNICODE && byte_size == 0U && vm->empty_unicode != NULL) {
        *out_bytes = TINYPY_UNICODE_OBJECT(vm->empty_unicode)->utf8;
        tinypy_value_t *empty = TINYPY_RET_EMPTY_UNICODE(vm);
        return empty;
    }
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
        TINYPY_UNICODE_OBJECT(value)->native_buffer = NULL;
        TINYPY_UNICODE_OBJECT(value)->wchar_buffer = NULL;
        payload = TINYPY_UNICODE_OBJECT(value)->utf8;
    }
    payload[byte_size] = 0U;
    *out_bytes = payload;
    return value;
}
//////////////////////////////////////////////////////////////////////////
/* Grows a byte string in place, as string_concatenate does when the left
   operand is about to be rebound: the caller holds the only reference, so
   the object may move. The string keeps the room its size class leaves for
   the next concatenation. NULL leaves the string untouched. */
tinypy_value_t *tinypy_internal_string_concat_in_place(tinypy_vm_t *vm, tinypy_value_t *left, const uint8_t *bytes, size_t size) {
    size_t old_size = TINYPY_SIZED_SIZE(left);
    size_t old_allocation = tinypy_internal_variable_builtin_payload_size(left);
    tinypy_value_t *grown = left;

    if (size > SIZE_MAX - old_size - 1U) {
        return NULL;
    }
    size_t capacity = __tinypy_internal_string_capacity(old_size + size + 1U);
    if (capacity > SIZE_MAX - offsetof(tinypy_string_object_t, bytes)) {
        return NULL;
    }
    size_t new_allocation = offsetof(tinypy_string_object_t, bytes) + capacity;
    if (new_allocation != old_allocation) {
#if defined(TINYPY_CYCLE_DIAGNOSTICS)
        __tinypy_internal_cycle_diagnostics_value_unregister(vm, left);
#endif
        grown = (tinypy_value_t *)__tinypy_internal_vm_reallocate_checked(vm, left, old_allocation, new_allocation, NULL);
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
    }
    (void)memcpy(TINYPY_STRING_OBJECT(grown)->bytes + old_size, bytes, size);
    TINYPY_STRING_OBJECT(grown)->bytes[old_size + size] = 0U;
    TINYPY_SIZED_SIZE(grown) = old_size + size;
    TINYPY_STRING_OBJECT(grown)->hash_computed = INT32_C(0);
    TINYPY_STRING_OBJECT(grown)->interned = INT32_C(0);
    TINYPY_STRING_OBJECT(grown)->over_allocated = TINYPY_TRUE;
    TINYPY_STRING_OBJECT(grown)->internal_metadata = NULL;
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
    return TINYPY_RET_NONE(vm);
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_not_implemented_get(tinypy_vm_t *vm) {
    return TINYPY_RET_NOT_IMPLEMENTED(vm);
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_ellipsis_get(tinypy_vm_t *vm) {
    return TINYPY_RET_ELLIPSIS(vm);
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_bool_from_i32(tinypy_vm_t *vm, int32_t value) {
    return value != 0 ? TINYPY_RET_TRUE(vm) : TINYPY_RET_FALSE(vm);
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_integer_from_i64(tinypy_vm_t *vm, int64_t value) {
    tinypy_value_t *return_value_1 = __tinypy_internal_integer_from_i64_fast(vm, value);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_integer_from_i64_checked(tinypy_vm_t *vm, int64_t value, tinypy_error_t **out_error) {
    if ((value >= TINYPY_INTEGER_CONSTANT_MIN && value <= TINYPY_INTEGER_CONSTANT_MAX) || vm->integer_free_list != NULL) {
        tinypy_value_t *result = __tinypy_internal_integer_from_i64_fast(vm, value);
        return result;
    }
    tinypy_value_t *result = tinypy_internal_object_allocate_checked(vm, &vm->types[TINYPY_VALUE_INTEGER], sizeof(tinypy_integer_object_t), out_error);
    if (result != NULL) {
        TINYPY_INTEGER_VALUE(result) = value;
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_string_from_bytes(tinypy_vm_t *vm, const void *bytes, size_t size, tinypy_bool_t reuse_interned, tinypy_bool_t checked, tinypy_error_t **out_error) {
    if (size == 0U) {
        return TINYPY_RET_EMPTY_STRING(vm);
    }
    if (size == 1U) {
        size_t cache_index = (size_t)*(const uint8_t *)bytes;
        tinypy_value_t *cached = vm->string_char_cache[cache_index];

        if (cached != NULL) {
            return TINYPY_RET(cached);
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
        return TINYPY_RET(result);
    }
    if (reuse_interned != 0 && __tinypy_internal_text_allocation_size(TINYPY_VALUE_STRING, size) != 0U) {
        tinypy_value_t *cached = tinypy_internal_string_find(vm, bytes, size);
        if (cached != NULL) {
            return TINYPY_RET(cached);
        }
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
    tinypy_value_t *result = __tinypy_internal_string_from_bytes(vm, bytes, size, TINYPY_TRUE, TINYPY_FALSE, NULL);

    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_string_from_bytes_checked(tinypy_vm_t *vm, const void *bytes, size_t size, tinypy_error_t **out_error) {
    tinypy_value_t *result = __tinypy_internal_string_from_bytes(vm, bytes, size, TINYPY_TRUE, TINYPY_TRUE, out_error);

    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_string_from_bytes_uninterned(tinypy_vm_t *vm, const void *bytes, size_t size) {
    tinypy_value_t *result = __tinypy_internal_string_from_bytes(vm, bytes, size, TINYPY_FALSE, TINYPY_FALSE, NULL);
    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_string_from_bytes_uninterned_checked(tinypy_vm_t *vm, const void *bytes, size_t size, tinypy_error_t **out_error) {
    tinypy_value_t *result = __tinypy_internal_string_from_bytes(vm, bytes, size, TINYPY_FALSE, TINYPY_TRUE, out_error);
    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_string_from_bytes_distinct_checked(tinypy_vm_t *vm, const void *bytes, size_t size, tinypy_error_t **out_error) {
    if (size == 0U) {
        return TINYPY_RET_EMPTY_STRING(vm);
    }
    uint8_t *output;
    tinypy_value_t *result = tinypy_internal_text_allocate_uninitialized_checked(vm, TINYPY_VALUE_STRING, size, 0U, &output, out_error);
    if (result == NULL) {
        return NULL;
    }
    (void)memcpy(output, bytes, size);
    TINYPY_STRING_OBJECT(result)->interned = TINYPY_FALSE;
    return result;
}
//////////////////////////////////////////////////////////////////////////
const void *tinypy_string_view(const tinypy_value_t *value, size_t *out_size) {
    *out_size = TINYPY_SIZED_SIZE(value);
    const void *bytes = TINYPY_STRING_OBJECT(value)->bytes;
    return bytes;
}
//////////////////////////////////////////////////////////////////////////
static TINYPY_ALWAYS_INLINE tinypy_value_t *__tinypy_internal_unicode_from_utf8(tinypy_vm_t *vm, const char *utf8, size_t size, tinypy_bool_t checked, tinypy_error_t **out_error) {
    size_t code_point_count;
    size_t cache_index = SIZE_MAX;

    if (__tinypy_internal_text_allocation_size(TINYPY_VALUE_UNICODE, size) == 0U) {
        if (checked != 0) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_MEMORY, "text object is too large", out_error);
        }
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

        return TINYPY_RET(cached);
    }

    code_point_count = __tinypy_internal_utf8_code_point_count(
        (const uint8_t *)utf8,
        size);

    uint8_t *payload;
    tinypy_value_t *result = checked != 0
        ? tinypy_internal_text_allocate_uninitialized_checked(vm, TINYPY_VALUE_UNICODE, size, code_point_count, &payload, out_error)
        : tinypy_internal_text_allocate_uninitialized(vm, TINYPY_VALUE_UNICODE, size, code_point_count, &payload);
    if (result == NULL) {
        return NULL;
    }
    if (size != 0U) {
        (void)memcpy(payload, utf8, size);
    }
    if (cache_index != SIZE_MAX) {
        vm->unicode_char_cache[cache_index] = result;
        TINYPY_INCREF(result);
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_unicode_from_utf8(tinypy_vm_t *vm, const char *utf8, size_t size) {
    tinypy_value_t *result = __tinypy_internal_unicode_from_utf8(vm, utf8, size, TINYPY_FALSE, NULL);

    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_unicode_from_utf8_checked(tinypy_vm_t *vm, const char *utf8, size_t size, tinypy_error_t **out_error) {
    tinypy_value_t *result = __tinypy_internal_unicode_from_utf8(vm, utf8, size, TINYPY_TRUE, out_error);

    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_unicode_decode_utf8(tinypy_vm_t *vm, const char *utf8, size_t size, tinypy_error_t **out_error) {
    const uint8_t *bytes = (const uint8_t *)utf8;
    size_t offset = 0U;

    TINYPY_CLEAR_ERROR(out_error);
    while (offset < size) {
        uint32_t code_point;
        size_t width = tinypy_internal_utf8_decode(bytes + offset, size - offset, &code_point);

        if (width == 0U) {
            break;
        }
        offset += width;
    }
    if (offset == size) {
        tinypy_value_t *unicode = tinypy_internal_unicode_from_utf8_checked(vm, utf8, size, out_error);
        return unicode;
    }
    /* The codec raises the UnicodeDecodeError that str.decode reports. */
    tinypy_value_t *text = tinypy_internal_string_from_bytes_checked(vm, utf8, size, out_error);
    if (text == NULL) {
        return NULL;
    }
    tinypy_value_t *decoded = tinypy_internal_text_codec(vm, text, vm->internal_codec_utf8_name, NULL, TINYPY_TRUE, TINYPY_TRUE, NULL, out_error);
    TINYPY_DECREF(text);
    return decoded;
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
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_bool_t return_value_1 = value->type->call != NULL || tinypy_internal_object_has_special_key((tinypy_value_t *)value, vm->internal_special_call_key) != 0 ? TINYPY_TRUE : TINYPY_FALSE;
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
    if (type->own_slot_names != NULL) {
        visit(type->own_slot_names, user_data);
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
