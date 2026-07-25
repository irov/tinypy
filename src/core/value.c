#include "tinypy/value.h"

#include "internal.h"

#include <assert.h>
#include <string.h>
//////////////////////////////////////////////////////////////////////////
tinypy_type_t *tinypy_internal_type_for_kind(tinypy_vm_t *vm, tinypy_value_type_e kind) {
    switch (kind) {
    case TINYPY_VALUE_NONE:
        return &vm->none_type;
    case TINYPY_VALUE_NOT_IMPLEMENTED:
        return &vm->not_implemented_type;
    case TINYPY_VALUE_BOOL:
        return &vm->bool_type;
    case TINYPY_VALUE_INTEGER:
        return &vm->integer_type;
    case TINYPY_VALUE_STRING:
        return &vm->string_type;
    case TINYPY_VALUE_UNICODE:
        return &vm->unicode_type;
    case TINYPY_VALUE_LONG:
        return &vm->long_type;
    case TINYPY_VALUE_FLOAT:
        return &vm->float_type;
    case TINYPY_VALUE_COMPLEX:
        return &vm->complex_type;
    case TINYPY_VALUE_TUPLE:
        return &vm->tuple_type;
    case TINYPY_VALUE_LIST:
        return &vm->list_type;
    case TINYPY_VALUE_DICT:
        return &vm->dict_type;
    case TINYPY_VALUE_SET:
        return &vm->set_type;
    case TINYPY_VALUE_FROZENSET:
        return &vm->frozenset_type;
    case TINYPY_VALUE_OUTPUT_STREAM:
        return &vm->output_stream_type;
    case TINYPY_VALUE_CODE:
        return &vm->code_type;
    case TINYPY_VALUE_FRAME:
        return &vm->frame_type;
    case TINYPY_VALUE_FUNCTION:
        return &vm->function_type;
    case TINYPY_VALUE_ITERATOR:
        return &vm->iterator_type;
    case TINYPY_VALUE_METHOD:
        return &vm->method_type;
    case TINYPY_VALUE_CELL:
        return &vm->cell_type;
    case TINYPY_VALUE_SLICE:
        return &vm->slice_type;
    case TINYPY_VALUE_MODULE:
        return &vm->module_type;
    case TINYPY_VALUE_NATIVE_FUNCTION:
        return &vm->native_function_type;
    case TINYPY_VALUE_STATIC_METHOD:
        return &vm->static_method_type;
    case TINYPY_VALUE_CLASS_METHOD:
        return &vm->class_method_type;
    case TINYPY_VALUE_PROPERTY:
        return &vm->property_type;
    case TINYPY_VALUE_SUPER:
        return &vm->super_type;
    case TINYPY_VALUE_TRACEBACK:
        return &vm->traceback_type;
    case TINYPY_VALUE_GENERATOR:
        return &vm->generator_type;
    case TINYPY_VALUE_XRANGE:
        return &vm->xrange_type;
    case TINYPY_VALUE_ENUMERATE:
        return &vm->enumerate_type;
    case TINYPY_VALUE_REVERSED:
        return &vm->reversed_type;
    case TINYPY_VALUE_BUFFER:
        return &vm->buffer_type;
    case TINYPY_VALUE_BYTEARRAY:
        return &vm->bytearray_type;
    case TINYPY_VALUE_WEAKREF:
        return &vm->weakref_type;
    case TINYPY_VALUE_DICT_KEYS:
        return &vm->dict_keys_type;
    case TINYPY_VALUE_DICT_VALUES:
        return &vm->dict_values_type;
    case TINYPY_VALUE_DICT_ITEMS:
        return &vm->dict_items_type;
    case TINYPY_VALUE_ELLIPSIS:
        return &vm->ellipsis_type;
    case TINYPY_VALUE_FILE:
        return &vm->file_type;
    case TINYPY_VALUE_GETSET_DESCRIPTOR:
        return &vm->getset_descriptor_type;
    case TINYPY_VALUE_MEMBER_DESCRIPTOR:
        return &vm->member_descriptor_type;
    case TINYPY_VALUE_CLASS:
        return &vm->class_type;
    case TINYPY_VALUE_OLD_INSTANCE:
        return &vm->old_instance_type;
    case TINYPY_VALUE_PARTIAL:
        return &vm->partial_type;
    case TINYPY_VALUE_SRE_PATTERN:
        return &vm->sre_pattern_type;
    case TINYPY_VALUE_SRE_MATCH:
        return &vm->sre_match_type;
    default:
        assert(!"invalid builtin value kind");
        return NULL;
    }
}
//////////////////////////////////////////////////////////////////////////
tinypy_vm_t *tinypy_internal_value_vm(const tinypy_value_t *value) {
    return TINYPY_VALUE_VM(value);
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_type_e tinypy_internal_value_kind(const tinypy_value_t *value) {
    return TINYPY_VALUE_KIND(value);
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_value_allocate(tinypy_vm_t *vm, tinypy_value_type_e type, size_t allocation_size) {
    tinypy_type_t *object_type = tinypy_internal_type_for_kind(vm, type);

    return tinypy_internal_object_allocate(vm, object_type, allocation_size);
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_object_allocate(tinypy_vm_t *vm, tinypy_type_t *object_type, size_t allocation_size) {
    assert(object_type != NULL);
    assert(object_type->vm == vm);
    assert(allocation_size >= object_type->basic_size);

    tinypy_value_t *value = (tinypy_value_t *)tinypy_internal_vm_allocate(
        vm,
        allocation_size,
        (uint32_t)TINYPY_ALLOC_TAG_VALUE);

    (void)memset(value, 0, allocation_size);
    value->ref = 1U;
    value->type = object_type;

    TINYPY_INCREF(&object_type->base.base);
#if defined(TINYPY_CYCLE_DIAGNOSTICS)
    tinypy_internal_debug_value_register(vm, value);
#endif
    return value;
}
//////////////////////////////////////////////////////////////////////////
size_t tinypy_internal_value_allocation_size(const tinypy_value_t *value) {
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);

    switch (kind) {
    case TINYPY_VALUE_STRING:
        return offsetof(tinypy_string_object_t, bytes) + TINYPY_SIZED_SIZE(value) + 1U;
    case TINYPY_VALUE_UNICODE:
        return offsetof(tinypy_unicode_object_t, utf8) + TINYPY_UNICODE_OBJECT(value)->byte_size + 1U;
    case TINYPY_VALUE_LONG:
        return offsetof(tinypy_long_object_t, digits) + TINYPY_LONG_DIGIT_COUNT(value) * sizeof(uint16_t);
    case TINYPY_VALUE_TUPLE:
        if (value->type == &TINYPY_VALUE_VM(value)->tuple_type) {
            return offsetof(tinypy_tuple_object_t, items) + TINYPY_SIZED_SIZE(value) * sizeof(tinypy_value_t *);
        }
        return value->type->basic_size;
    case TINYPY_VALUE_FRAME:
        return offsetof(tinypy_frame_object_t, locals_plus) + TINYPY_SIZED_SIZE(value) * sizeof(tinypy_value_t *);
    default:
        return value->type->basic_size;
    }
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_value_destroy(tinypy_value_t *value) {
    assert(value != NULL);
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    size_t allocation_size = tinypy_internal_value_allocation_size(value);
#if defined(TINYPY_CYCLE_DIAGNOSTICS)
    tinypy_internal_debug_value_unregister(vm, value);
#endif
    if (value->type != NULL && value->type->destroy != NULL) {
        value->type->destroy(value);
    }
    tinypy_internal_vm_deallocate(
        vm,
        value,
        allocation_size,
        (uint32_t)TINYPY_ALLOC_TAG_VALUE);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_internal_release_visit(tinypy_value_t *child, void *user_data) {
    assert(child != NULL);
    (void)user_data;
    TINYPY_DECREF(child);
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_integer_free_list_finalize(tinypy_vm_t *vm) {
    while (vm->integer_free_list != NULL) {
        tinypy_integer_object_t *value = vm->integer_free_list;

        vm->integer_free_list = __tinypy_internal_integer_free_next(value);
        assert(vm->integer_free_count != 0U);
        vm->integer_free_count -= 1U;
        assert(vm->integer_type.base.base.ref > 1);
        vm->integer_type.base.base.ref -= 1;
#if defined(TINYPY_CYCLE_DIAGNOSTICS)
        tinypy_internal_debug_value_unregister(vm, &value->base);
#endif
        tinypy_internal_vm_deallocate(vm, value, sizeof(*value), (uint32_t)TINYPY_ALLOC_TAG_VALUE);
    }
}
//////////////////////////////////////////////////////////////////////////
int tinypy_internal_value_is_vm_embedded(const tinypy_vm_t *vm, const tinypy_value_t *value) {
    const tinypy_type_t *types[TINYPY_BUILTIN_TYPE_COUNT] = {
        &vm->type_type,
        &vm->object_type,
        &vm->none_type,
        &vm->not_implemented_type,
        &vm->basestring_type,
        &vm->bool_type,
        &vm->integer_type,
        &vm->string_type,
        &vm->unicode_type,
        &vm->long_type,
        &vm->float_type,
        &vm->complex_type,
        &vm->tuple_type,
        &vm->list_type,
        &vm->dict_type,
        &vm->set_type,
        &vm->frozenset_type,
        &vm->output_stream_type,
        &vm->code_type,
        &vm->frame_type,
        &vm->function_type,
        &vm->iterator_type,
        &vm->method_type,
        &vm->cell_type,
        &vm->slice_type,
        &vm->module_type,
        &vm->native_function_type,
        &vm->static_method_type,
        &vm->class_method_type,
        &vm->property_type,
        &vm->super_type,
        &vm->traceback_type,
        &vm->generator_type,
        &vm->xrange_type,
        &vm->enumerate_type,
        &vm->reversed_type,
        &vm->buffer_type,
        &vm->bytearray_type,
        &vm->weakref_type,
        &vm->dict_keys_type,
        &vm->dict_values_type,
        &vm->dict_items_type,
        &vm->ellipsis_type,
        &vm->file_type,
        &vm->getset_descriptor_type,
        &vm->member_descriptor_type,
        &vm->class_type,
        &vm->old_instance_type,
        &vm->partial_type,
        &vm->sre_pattern_type,
        &vm->sre_match_type};
    size_t index;
    size_t integer_index;

    if (value == &vm->none_object.base || value == &vm->not_implemented_object.base || value == &vm->ellipsis_object.base || value == &vm->false_object.base || value == &vm->true_object.base || value == &vm->float_zero_object.base || value == &vm->empty_string_object.base.base || value == &vm->empty_tuple_object.base.base) {
        return 1;
    }

    for (integer_index = 0U;
         integer_index < TINYPY_INTEGER_CONSTANT_COUNT;
         ++integer_index) {
        if (value == &vm->integer_constants[integer_index].base) {
            return 1;
        }
    }

    for (index = 0U; index < TINYPY_BUILTIN_TYPE_COUNT; ++index) {
        if (value == &types[index]->base.base || value == &vm->builtin_type_dicts[index].base) {
            return 1;
        }
    }

    return 0;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_internal_value_finalize(tinypy_value_t *value) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_internal_exception_state_t exception_state;
    tinypy_value_t *args;
    tinypy_value_t *result;
    tinypy_error_t *error = NULL;

    if (value->type->has_finalizer == 0 && (TINYPY_VALUE_KIND(value) != TINYPY_VALUE_OLD_INSTANCE || tinypy_internal_old_instance_has_special(value, "__del__", 7U) == 0)) {
        return INT32_C(0);
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
    assert(value->ref > 0);
    value->ref -= 1U;
    return value->ref != 0U ? INT32_C(1) : INT32_C(0);
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_value_release_zero(tinypy_value_t *value) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);
    tinypy_type_t *type = value->type;

    assert(value->ref == 0);
    if (type == &vm->integer_type && vm->state == TINYPY_VM_STATE_LIVE && vm->integer_free_count < TINYPY_INTEGER_FREE_LIST_MAX) {
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
    if (type == &vm->frame_type && vm->state == TINYPY_VM_STATE_LIVE && vm->frame_free_count < TINYPY_FRAME_FREE_LIST_MAX) {
        tinypy_internal_frame_free_list_push(vm, value);
        return;
    }
    if (type == &vm->method_type && vm->state == TINYPY_VM_STATE_LIVE && vm->method_free_count < TINYPY_METHOD_FREE_LIST_MAX) {
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
    assert(tinypy_internal_vm_valid(TINYPY_VALUE_VM(value)));
    TINYPY_DECREF(value);
}
//////////////////////////////////////////////////////////////////////////
static inline size_t __tinypy_internal_text_allocation_size(tinypy_value_type_e type, size_t byte_size) {
    size_t object_size = type == TINYPY_VALUE_STRING
                             ? offsetof(tinypy_string_object_t, bytes)
                             : offsetof(tinypy_unicode_object_t, utf8);

    assert(byte_size <= SIZE_MAX - object_size - 1U);
    return object_size + byte_size + 1U;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_text_from_bytes(tinypy_vm_t *vm, const unsigned char *bytes, size_t byte_size, size_t code_point_count, tinypy_value_type_e type) {
    size_t allocation_size;
    unsigned char *payload;

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
const unsigned char *tinypy_internal_text_bytes(const tinypy_value_t *value) {
    assert(TINYPY_VALUE_KIND(value) == TINYPY_VALUE_STRING || TINYPY_VALUE_KIND(value) == TINYPY_VALUE_UNICODE);
    return TINYPY_VALUE_KIND(value) == TINYPY_VALUE_STRING
               ? TINYPY_STRING_OBJECT(value)->bytes
               : TINYPY_UNICODE_OBJECT(value)->utf8;
}
//////////////////////////////////////////////////////////////////////////
size_t tinypy_internal_text_byte_size(const tinypy_value_t *value) {
    assert(TINYPY_VALUE_KIND(value) == TINYPY_VALUE_STRING || TINYPY_VALUE_KIND(value) == TINYPY_VALUE_UNICODE);
    return TINYPY_VALUE_KIND(value) == TINYPY_VALUE_STRING
               ? TINYPY_SIZED_SIZE(value)
               : TINYPY_UNICODE_OBJECT(value)->byte_size;
}
//////////////////////////////////////////////////////////////////////////
int32_t tinypy_internal_string_is_interned(const tinypy_value_t *value) {
    assert(TINYPY_VALUE_KIND(value) == TINYPY_VALUE_STRING);
    return TINYPY_STRING_OBJECT(value)->interned;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_string_set_interned(tinypy_value_t *value, int32_t interned) {
    assert(TINYPY_VALUE_KIND(value) == TINYPY_VALUE_STRING);
    TINYPY_STRING_OBJECT(value)->interned = interned != 0 ? INT32_C(1) : INT32_C(0);
}

#ifndef NDEBUG
//////////////////////////////////////////////////////////////////////////
static int __tinypy_internal_utf8_is_continuation(unsigned char byte) {
    return byte >= 0x80U && byte <= 0xbfU;
}
#endif
//////////////////////////////////////////////////////////////////////////
static size_t __tinypy_internal_utf8_code_point_count(const unsigned char *bytes, size_t size) {
    size_t offset = 0U;
    size_t code_point_count = 0U;

    while (offset < size) {
        unsigned char first = bytes[offset];
        size_t width;

        if (first <= 0x7fU) {
            width = 1U;
        }
        else if (first < 0xe0U) {
            assert(first >= 0xc2U);
            assert(size - offset >= 2U);
            assert(__tinypy_internal_utf8_is_continuation(bytes[offset + 1U]));
            width = 2U;
        }
        else if (first < 0xf0U) {
            assert(size - offset >= 3U);
            assert(__tinypy_internal_utf8_is_continuation(bytes[offset + 1U]));
            assert(__tinypy_internal_utf8_is_continuation(bytes[offset + 2U]));
            if (first == 0xe0U) {
                assert(bytes[offset + 1U] >= 0xa0U);
            }
            else if (first == 0xedU) {
                assert(bytes[offset + 1U] <= 0x9fU);
            }
            width = 3U;
        }
        else {
            assert(first <= 0xf4U);
            assert(size - offset >= 4U);
            assert(__tinypy_internal_utf8_is_continuation(bytes[offset + 1U]));
            assert(__tinypy_internal_utf8_is_continuation(bytes[offset + 2U]));
            assert(__tinypy_internal_utf8_is_continuation(bytes[offset + 3U]));
            if (first == 0xf0U) {
                assert(bytes[offset + 1U] >= 0x90U);
            }
            else if (first == 0xf4U) {
                assert(bytes[offset + 1U] <= 0x8fU);
            }
            width = 4U;
        }

        offset += width;
        code_point_count += 1U;
    }

    return code_point_count;
}
//////////////////////////////////////////////////////////////////////////
int tinypy_internal_value_belongs_to(const tinypy_vm_t *vm, const tinypy_value_t *value) {
    return value != NULL && TINYPY_VALUE_VM(value) == vm;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_none_get(tinypy_vm_t *vm) {
    assert(tinypy_internal_vm_valid(vm));
    tinypy_value_t *result = &vm->none_object.base;
    TINYPY_INCREF(result);

    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_not_implemented_get(tinypy_vm_t *vm) {
    assert(tinypy_internal_vm_valid(vm));
    tinypy_value_t *result = &vm->not_implemented_object.base;
    TINYPY_INCREF(result);
    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_ellipsis_get(tinypy_vm_t *vm) {
    assert(tinypy_internal_vm_valid(vm));
    tinypy_value_t *result = &vm->ellipsis_object.base;
    TINYPY_INCREF(result);
    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_bool_from_i32(tinypy_vm_t *vm, int32_t value) {
    assert(tinypy_internal_vm_valid(vm));
    tinypy_value_t *result = value != 0
                 ? &vm->true_object.base
                 : &vm->false_object.base;
    TINYPY_INCREF(result);

    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_integer_from_i64(tinypy_vm_t *vm, int64_t value) {
    assert(tinypy_internal_vm_valid(vm));
    return __tinypy_internal_integer_from_i64_fast(vm, value);
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_string_from_bytes(tinypy_vm_t *vm, const void *bytes, size_t size) {
    assert(tinypy_internal_vm_valid(vm));
    assert(bytes != NULL || size == 0U);

    if (size == 0U) {
        tinypy_value_t *result = &vm->empty_string_object.base.base;

        TINYPY_INCREF(result);
        return result;
    }

    return __tinypy_internal_text_from_bytes(
        vm,
        (const unsigned char *)bytes,
        size,
        0U,
        TINYPY_VALUE_STRING);
}
//////////////////////////////////////////////////////////////////////////
const void *tinypy_string_view(const tinypy_value_t *value, size_t *out_size) {
    assert(value != NULL);
    assert(tinypy_internal_vm_valid(TINYPY_VALUE_VM(value)));
    assert(out_size != NULL);
    assert(TINYPY_VALUE_KIND(value) == TINYPY_VALUE_STRING);

    *out_size = TINYPY_SIZED_SIZE(value);
    return TINYPY_STRING_OBJECT(value)->bytes;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_unicode_from_utf8(tinypy_vm_t *vm, const char *utf8, size_t size) {
    size_t code_point_count;

    assert(tinypy_internal_vm_valid(vm));
    assert(utf8 != NULL || size == 0U);
    code_point_count = __tinypy_internal_utf8_code_point_count(
        (const unsigned char *)utf8,
        size);

    return __tinypy_internal_text_from_bytes(
        vm,
        (const unsigned char *)utf8,
        size,
        code_point_count,
        TINYPY_VALUE_UNICODE);
}
//////////////////////////////////////////////////////////////////////////
const char *tinypy_unicode_utf8_view(const tinypy_value_t *value, size_t *out_size, size_t *out_code_point_count) {
    assert(value != NULL);
    assert(tinypy_internal_vm_valid(TINYPY_VALUE_VM(value)));
    assert(out_size != NULL);
    assert(out_code_point_count != NULL);
    assert(TINYPY_VALUE_KIND(value) == TINYPY_VALUE_UNICODE);

    *out_size = TINYPY_UNICODE_OBJECT(value)->byte_size;
    *out_code_point_count = TINYPY_SIZED_SIZE(value);
    return (const char *)TINYPY_UNICODE_OBJECT(value)->utf8;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_type_e tinypy_typeof(const tinypy_value_t *value) {
    assert(value != NULL);
    assert(tinypy_internal_vm_valid(TINYPY_VALUE_VM(value)));

    return TINYPY_VALUE_KIND(value);
}
//////////////////////////////////////////////////////////////////////////
tinypy_vm_t *tinypy_value_vm(const tinypy_value_t *value) {
    assert(value != NULL);
    assert(tinypy_internal_vm_valid(TINYPY_VALUE_VM(value)));
    return TINYPY_VALUE_VM(value);
}
//////////////////////////////////////////////////////////////////////////
tinypy_ref_t tinypy_refcount(const tinypy_value_t *value) {
    assert(value != NULL);
    assert(tinypy_internal_vm_valid(TINYPY_VALUE_VM(value)));
    return value->ref;
}
//////////////////////////////////////////////////////////////////////////
int32_t tinypy_is_callable(const tinypy_value_t *value) {
    assert(value != NULL);
    assert(tinypy_internal_vm_valid(TINYPY_VALUE_VM(value)));
    return value->type->call != NULL || tinypy_internal_object_has_special((tinypy_value_t *)value, "__call__", 8U) != 0 ? INT32_C(1) : INT32_C(0);
}
//////////////////////////////////////////////////////////////////////////
const tinypy_type_t *tinypy_object_type(const tinypy_value_t *value) {
    assert(value != NULL);
    assert(tinypy_internal_vm_valid(TINYPY_VALUE_VM(value)));
    return value->type;
}
//////////////////////////////////////////////////////////////////////////
const char *tinypy_type_name(const tinypy_type_t *type, size_t *out_size) {
    assert(type != NULL);
    assert(tinypy_internal_vm_valid(type->vm));
    assert(out_size != NULL);
    *out_size = type->name_size;
    return type->name;
}
//////////////////////////////////////////////////////////////////////////
const tinypy_type_t *tinypy_type_metaclass(const tinypy_type_t *type) {
    assert(type != NULL);
    assert(tinypy_internal_vm_valid(type->vm));
    return type->base.base.type;
}
//////////////////////////////////////////////////////////////////////////
const tinypy_type_t *tinypy_type_base(const tinypy_type_t *type) {
    assert(type != NULL);
    assert(tinypy_internal_vm_valid(type->vm));
    return type->base_type;
}
//////////////////////////////////////////////////////////////////////////
const tinypy_value_t *tinypy_type_dict(const tinypy_type_t *type) {
    assert(type != NULL);
    assert(tinypy_internal_vm_valid(type->vm));
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
        TINYPY_DECREF(&type->vm->tuple_type.base.base);
    }
}
//////////////////////////////////////////////////////////////////////////
int32_t tinypy_bool_as_i32(const tinypy_value_t *value) {
    assert(value != NULL);
    assert(tinypy_internal_vm_valid(TINYPY_VALUE_VM(value)));
    assert(TINYPY_VALUE_KIND(value) == TINYPY_VALUE_BOOL);

    return (int32_t)TINYPY_INTEGER_VALUE(value);
}
//////////////////////////////////////////////////////////////////////////
int64_t tinypy_integer_as_i64(const tinypy_value_t *value) {
    tinypy_value_type_e kind;

    assert(value != NULL);
    assert(tinypy_internal_vm_valid(TINYPY_VALUE_VM(value)));
    kind = TINYPY_VALUE_KIND(value);
    assert(kind == TINYPY_VALUE_INTEGER || kind == TINYPY_VALUE_BOOL);

    if (kind == TINYPY_VALUE_BOOL) {
        return TINYPY_INTEGER_VALUE(value) != 0 ? INT64_C(1) : INT64_C(0);
    }

    return TINYPY_INTEGER_VALUE(value);
}
//////////////////////////////////////////////////////////////////////////
void tinypy_retain(tinypy_value_t *value) {
    assert(tinypy_internal_vm_valid(TINYPY_VALUE_VM(value)));
    TINYPY_INCREF(value);
}
