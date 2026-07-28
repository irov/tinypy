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
size_t tinypy_internal_value_allocation_size(const tinypy_value_t *value) {
    size_t function_result;
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);

    switch (kind) {
    case TINYPY_VALUE_STRING:
        function_result = offsetof(tinypy_string_object_t, bytes) + TINYPY_SIZED_SIZE(value) + 1U;
        return function_result;
    case TINYPY_VALUE_UNICODE:
        function_result = offsetof(tinypy_unicode_object_t, utf8) + TINYPY_UNICODE_OBJECT(value)->byte_size + 1U;
        return function_result;
    case TINYPY_VALUE_LONG:
        function_result = offsetof(tinypy_long_object_t, digits) + TINYPY_LONG_DIGIT_COUNT(value) * sizeof(uint16_t);
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
    size_t index;
    size_t integer_index;

    if (value == &vm->none_object.base || value == &vm->not_implemented_object.base || value == &vm->ellipsis_object.base || value == &vm->false_object.base || value == &vm->true_object.base || value == &vm->float_zero_object.base || value == &vm->empty_string_object.base.base || value == &vm->empty_tuple_object.base.base) {
        return TINYPY_TRUE;
    }

    for (integer_index = 0U;
         integer_index < TINYPY_INTEGER_CONSTANT_COUNT;
         ++integer_index) {
        if (value == &vm->integer_constants[integer_index].base) {
            return TINYPY_TRUE;
        }
    }

    for (index = 0U; index < TINYPY_BUILTIN_TYPE_COUNT; ++index) {
        if (value == &vm->types[index].base.base || value == &vm->builtin_type_dicts[index].base) {
            return TINYPY_TRUE;
        }
    }

    return TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_internal_value_finalize(tinypy_value_t *value) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_internal_exception_state_t exception_state;
    tinypy_value_t *args;
    tinypy_value_t *result;
    tinypy_error_t *error = NULL;

    if (value->type->has_finalizer == 0 && (TINYPY_VALUE_KIND(value) != TINYPY_VALUE_OLD_INSTANCE || tinypy_internal_old_instance_has_special(value, "__del__", 7U) == 0)) {
        return TINYPY_FALSE;
    }
    value->ref = 1;
    tinypy_internal_exception_preserve_begin(vm, &exception_state);
    tinypy_value_t *method = tinypy_object_get_attr(value, "__del__", 7U, &error);
    if (method != NULL) {
        args = tinypy_tuple_from_items(vm, NULL, 0U);
        result = tinypy_call(method, args, NULL, &error);
        TINYPY_DECREF(args);
        TINYPY_DECREF(method);
        if (result != NULL) {
            TINYPY_DECREF(result);
        }
    }
    if (error != NULL) {
        tinypy_error_release(error);
    }
    tinypy_internal_exception_preserve_end(vm, &exception_state);
    value->ref -= 1U;
    return value->ref != 0U ? TINYPY_TRUE : TINYPY_FALSE;
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
    if (type->has_finalizer != 0 || TINYPY_VALUE_KIND(value) == TINYPY_VALUE_OLD_INSTANCE) {
        if (__tinypy_internal_value_finalize(value) != 0) {
            return;
        }
    }
    if (type->weakref_offset != 0U) {
        tinypy_internal_weakref_clear(value);
    }
    if (TINYPY_VALUE_KIND(value) == TINYPY_VALUE_TYPE) {
        tinypy_internal_type_lookup_cache_invalidate(vm);
    }
    if (type != NULL && type->release_references != NULL) {
        type->release_references(value, __tinypy_internal_release_visit, NULL);
    }
    if (type == &vm->types[TINYPY_VALUE_FRAME] && vm->state == TINYPY_VM_STATE_LIVE && vm->frame_free_count < TINYPY_FRAME_FREE_LIST_MAX) {
        tinypy_internal_frame_free_list_push(vm, value);
        return;
    }
    if (type == &vm->types[TINYPY_VALUE_METHOD] && vm->state == TINYPY_VM_STATE_LIVE && vm->method_free_count < TINYPY_METHOD_FREE_LIST_MAX) {
        tinypy_internal_method_free_list_push(vm, value);
        return;
    }
    tinypy_internal_value_destroy(value);
    if (type != NULL) {
        __tinypy_internal_release_visit(&type->base.base, NULL);
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

    return object_size + byte_size + 1U;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_text_from_bytes(tinypy_vm_t *vm, const uint8_t *bytes, size_t byte_size, size_t code_point_count, tinypy_value_type_e type) {
    size_t allocation_size;
    uint8_t *payload;

    allocation_size = __tinypy_internal_text_allocation_size(type, byte_size);

    tinypy_value_t *value = tinypy_internal_value_allocate(vm, type, allocation_size);

    if (type == TINYPY_VALUE_STRING) {
        TINYPY_SIZED_SIZE(value) = byte_size;
        TINYPY_STRING_OBJECT(value)->interned = byte_size <= 1U ? INT32_C(1) : INT32_C(0);
        payload = TINYPY_STRING_OBJECT(value)->bytes;
    }
    else {
        TINYPY_SIZED_SIZE(value) = code_point_count;
        TINYPY_UNICODE_OBJECT(value)->byte_size = byte_size;
        payload = TINYPY_UNICODE_OBJECT(value)->utf8;
    }
    if (byte_size != 0U) {
        (void)memcpy(payload, bytes, byte_size);
    }
    payload[byte_size] = 0U;
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
tinypy_value_t *tinypy_string_from_bytes(tinypy_vm_t *vm, const void *bytes, size_t size) {

    if (size == 0U) {
        tinypy_value_t *result = &vm->empty_string_object.base.base;

        TINYPY_INCREF(result);
        return result;
    }

    tinypy_value_t *return_value_1 = __tinypy_internal_text_from_bytes(
        vm,
        (const uint8_t *)bytes,
        size,
        0U,
        TINYPY_VALUE_STRING);
    return return_value_1;
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

    code_point_count = __tinypy_internal_utf8_code_point_count(
        (const uint8_t *)utf8,
        size);

    tinypy_value_t *return_value_1 = __tinypy_internal_text_from_bytes(
        vm,
        (const uint8_t *)utf8,
        size,
        code_point_count,
        TINYPY_VALUE_UNICODE);
    return return_value_1;
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
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_type_destroy(tinypy_value_t *value) {
    tinypy_type_t *type = (tinypy_type_t *)value;

    if (type->mro != NULL) {
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
