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
    return value != NULL && value->type != NULL ? value->type->vm : NULL;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_type_e tinypy_internal_value_kind(const tinypy_value_t *value) {
    tinypy_type_t *type;
    tinypy_vm_t *vm;

    assert(value != NULL);
    assert(value->type != NULL);
    assert(value->type->vm != NULL);
    type = value->type;
    vm = type->vm;
    if (type == &vm->none_type) {
        return TINYPY_VALUE_NONE;
    }
    if (type == &vm->not_implemented_type) {
        return TINYPY_VALUE_NOT_IMPLEMENTED;
    }
    if (type == &vm->bool_type) {
        return TINYPY_VALUE_BOOL;
    }
    if (type == &vm->integer_type) {
        return TINYPY_VALUE_INTEGER;
    }
    if (type == &vm->string_type) {
        return TINYPY_VALUE_STRING;
    }
    if (type == &vm->unicode_type) {
        return TINYPY_VALUE_UNICODE;
    }
    if (type == &vm->long_type) {
        return TINYPY_VALUE_LONG;
    }
    if (type == &vm->float_type) {
        return TINYPY_VALUE_FLOAT;
    }
    if (type == &vm->complex_type) {
        return TINYPY_VALUE_COMPLEX;
    }
    if (type == &vm->tuple_type) {
        return TINYPY_VALUE_TUPLE;
    }
    if (type == &vm->list_type) {
        return TINYPY_VALUE_LIST;
    }
    if (type == &vm->dict_type) {
        return TINYPY_VALUE_DICT;
    }
    if (type == &vm->set_type) {
        return TINYPY_VALUE_SET;
    }
    if (type == &vm->frozenset_type) {
        return TINYPY_VALUE_FROZENSET;
    }
    if (type == &vm->output_stream_type) {
        return TINYPY_VALUE_OUTPUT_STREAM;
    }
    if (type == &vm->code_type) {
        return TINYPY_VALUE_CODE;
    }
    if (type == &vm->frame_type) {
        return TINYPY_VALUE_FRAME;
    }
    if (type == &vm->function_type) {
        return TINYPY_VALUE_FUNCTION;
    }
    if (type == &vm->iterator_type) {
        return TINYPY_VALUE_ITERATOR;
    }
    if (type == &vm->method_type) {
        return TINYPY_VALUE_METHOD;
    }
    if (type == &vm->cell_type) {
        return TINYPY_VALUE_CELL;
    }
    if (type == &vm->slice_type) {
        return TINYPY_VALUE_SLICE;
    }
    if (type == &vm->module_type) {
        return TINYPY_VALUE_MODULE;
    }
    if (type == &vm->native_function_type) {
        return TINYPY_VALUE_NATIVE_FUNCTION;
    }
    if (type == &vm->static_method_type) {
        return TINYPY_VALUE_STATIC_METHOD;
    }
    if (type == &vm->class_method_type) {
        return TINYPY_VALUE_CLASS_METHOD;
    }
    if (type == &vm->property_type) {
        return TINYPY_VALUE_PROPERTY;
    }
    if (type == &vm->super_type) {
        return TINYPY_VALUE_SUPER;
    }
    if (type == &vm->traceback_type) {
        return TINYPY_VALUE_TRACEBACK;
    }
    if (type == &vm->generator_type) {
        return TINYPY_VALUE_GENERATOR;
    }
    if (type == &vm->xrange_type) {
        return TINYPY_VALUE_XRANGE;
    }
    if (type == &vm->enumerate_type) {
        return TINYPY_VALUE_ENUMERATE;
    }
    if (type == &vm->reversed_type) {
        return TINYPY_VALUE_REVERSED;
    }
    if (type == &vm->buffer_type) {
        return TINYPY_VALUE_BUFFER;
    }
    if (type == &vm->bytearray_type) {
        return TINYPY_VALUE_BYTEARRAY;
    }
    if (type == &vm->weakref_type) {
        return TINYPY_VALUE_WEAKREF;
    }
    if (type == &vm->dict_keys_type) {
        return TINYPY_VALUE_DICT_KEYS;
    }
    if (type == &vm->dict_values_type) {
        return TINYPY_VALUE_DICT_VALUES;
    }
    if (type == &vm->dict_items_type) {
        return TINYPY_VALUE_DICT_ITEMS;
    }
    if (type == &vm->ellipsis_type) {
        return TINYPY_VALUE_ELLIPSIS;
    }
    if (type == &vm->file_type) {
        return TINYPY_VALUE_FILE;
    }
    if (type == &vm->getset_descriptor_type) {
        return TINYPY_VALUE_GETSET_DESCRIPTOR;
    }
    if (type == &vm->member_descriptor_type) {
        return TINYPY_VALUE_MEMBER_DESCRIPTOR;
    }
    if (type == &vm->class_type) {
        return TINYPY_VALUE_CLASS;
    }
    if (type == &vm->old_instance_type) {
        return TINYPY_VALUE_OLD_INSTANCE;
    }
    if (type == &vm->partial_type) {
        return TINYPY_VALUE_PARTIAL;
    }
    if (type == &vm->sre_pattern_type) {
        return TINYPY_VALUE_SRE_PATTERN;
    }
    if (type == &vm->sre_match_type) {
        return TINYPY_VALUE_SRE_MATCH;
    }
    if ((type->flags & TINYPY_TYPE_FLAG_HEAP) != 0U && type->layout_kind != TINYPY_VALUE_INVALID) {
        return type->layout_kind;
    }
    if ((type->flags & TINYPY_TYPE_FLAG_TYPE_SUBCLASS) != 0U) {
        return TINYPY_VALUE_TYPE;
    }
    return TINYPY_VALUE_INSTANCE;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_value_allocate(tinypy_vm_t *vm, tinypy_value_type_e type, size_t allocation_size) {
    tinypy_type_t *object_type = tinypy_internal_type_for_kind(vm, type);

    return tinypy_internal_object_allocate(vm, object_type, allocation_size);
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_object_allocate(tinypy_vm_t *vm, tinypy_type_t *object_type, size_t allocation_size) {
    tinypy_value_t *value;

    assert(object_type != NULL);
    assert(object_type->vm == vm);
    assert(allocation_size >= object_type->basic_size);

    value = (tinypy_value_t *)tinypy_internal_vm_allocate(
        vm,
        allocation_size,
        (uint32_t)TINYPY_ALLOC_TAG_VALUE);

    (void)memset(value, 0, allocation_size);
    value->ref = 1U;
    value->type = object_type;

    tinypy_retain(&object_type->base.base);
    return value;
}
//////////////////////////////////////////////////////////////////////////
size_t tinypy_internal_value_allocation_size(const tinypy_value_t *value) {
    tinypy_value_type_e kind = tinypy_internal_value_kind(value);

    switch (kind) {
    case TINYPY_VALUE_STRING:
        return offsetof(tinypy_string_object_t, bytes) + (size_t)TINYPY_SIZE(value) + 1U;
    case TINYPY_VALUE_UNICODE:
        return offsetof(tinypy_unicode_object_t, utf8) + TINYPY_UNICODE_OBJECT(value)->byte_size + 1U;
    case TINYPY_VALUE_LONG:
        return offsetof(tinypy_long_object_t, digits) + TINYPY_LONG_DIGIT_COUNT(value) * sizeof(uint16_t);
    case TINYPY_VALUE_TUPLE:
        if (value->type == &tinypy_internal_value_vm(value)->tuple_type) {
            return offsetof(tinypy_tuple_object_t, items) + (size_t)TINYPY_SIZE(value) * sizeof(tinypy_value_t *);
        }
        return value->type->basic_size;
    case TINYPY_VALUE_FRAME:
        return offsetof(tinypy_frame_object_t, locals_plus) + (size_t)TINYPY_SIZE(value) * sizeof(tinypy_value_t *);
    default:
        return value->type->basic_size;
    }
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_value_destroy(tinypy_vm_t *vm, tinypy_value_t *value) {
    size_t allocation_size;

    assert(value != NULL);
    assert(tinypy_internal_value_vm(value) == vm);

    allocation_size = tinypy_internal_value_allocation_size(value);
    if (value->type != NULL && value->type->destroy != NULL) {
        value->type->destroy(vm, value);
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
    tinypy_release(child);
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
         integer_index += 1U) {
        if (value == &vm->integer_constants[integer_index].base) {
            return 1;
        }
    }

    for (index = 0U; index < TINYPY_BUILTIN_TYPE_COUNT; index += 1U) {
        if (value == &types[index]->base.base || value == &vm->builtin_type_dicts[index].base) {
            return 1;
        }
    }

    return 0;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_internal_value_finalize(tinypy_value_t *value) {
    tinypy_vm_t *vm = tinypy_internal_value_vm(value);
    tinypy_internal_exception_state_t exception_state;
    tinypy_value_t *method;
    tinypy_value_t *args;
    tinypy_value_t *result;
    tinypy_error_t *error = NULL;

    if (value->type->has_finalizer == 0 && (tinypy_internal_value_kind(value) != TINYPY_VALUE_OLD_INSTANCE || tinypy_internal_old_instance_has_special(value, "__del__", 7U) == 0)) {
        return INT32_C(0);
    }
    value->ref = 1;
    tinypy_internal_exception_preserve_begin(vm, &exception_state);
    method = tinypy_object_get_attr(value, "__del__", 7U, &error);
    if (method != NULL) {
        args = tinypy_tuple_from_items(vm, NULL, 0U);
        result = tinypy_call(method, args, NULL, &error);
        tinypy_release(args);
        tinypy_release(method);
        if (result != NULL) {
            tinypy_release(result);
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
void tinypy_release(tinypy_value_t *value) {
    assert(value != NULL);
    assert(tinypy_internal_vm_valid(tinypy_internal_value_vm(value)));

    assert(value->ref != PTRDIFF_MAX);
    assert(value->ref > 0);
#ifndef NDEBUG
    tinypy_vm_t *vm_2 = tinypy_internal_value_vm(value);
    if (tinypy_internal_value_is_vm_embedded(
            vm_2, value)) {
        assert(value->ref > 1);
    }
#endif

    value->ref -= 1U;
    if (value->ref != 0U) {
        return;
    }

    tinypy_vm_t *vm = tinypy_internal_value_vm(value);
    tinypy_type_t *type = value->type;
    if (__tinypy_internal_value_finalize(value) != 0) {
        return;
    }
    tinypy_internal_weakref_clear(value);
    if (type != NULL && type->release_references != NULL) {
        type->release_references(value, __tinypy_internal_release_visit, NULL);
    }
    tinypy_internal_value_destroy(vm, value);
    if (type != NULL) {
        __tinypy_internal_release_visit(&type->base.base, NULL);
    }
}
//////////////////////////////////////////////////////////////////////////
static size_t __tinypy_internal_text_allocation_size(tinypy_value_type_e type, size_t byte_size) {
    size_t object_size = type == TINYPY_VALUE_STRING
                             ? offsetof(tinypy_string_object_t, bytes)
                             : offsetof(tinypy_unicode_object_t, utf8);

    assert(byte_size <= (size_t)PTRDIFF_MAX);
    assert(byte_size <= SIZE_MAX - object_size - 1U);
    return object_size + byte_size + 1U;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_text_from_bytes(tinypy_vm_t *vm, const unsigned char *bytes, size_t byte_size, size_t code_point_count, tinypy_value_type_e type) {
    size_t allocation_size;
    tinypy_value_t *value;
    unsigned char *payload;

    assert(code_point_count <= (size_t)PTRDIFF_MAX);
    allocation_size = __tinypy_internal_text_allocation_size(type, byte_size);

    value = tinypy_internal_value_allocate(vm, type, allocation_size);

    if (type == TINYPY_VALUE_STRING) {
        TINYPY_SIZE(value) = (ptrdiff_t)byte_size;
        TINYPY_STRING_OBJECT(value)->interned = byte_size <= 1U ? INT32_C(1) : INT32_C(0);
        payload = TINYPY_STRING_OBJECT(value)->bytes;
    }
    else {
        TINYPY_SIZE(value) = (ptrdiff_t)code_point_count;
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
    assert(tinypy_internal_value_kind(value) == TINYPY_VALUE_STRING || tinypy_internal_value_kind(value) == TINYPY_VALUE_UNICODE);
    return tinypy_internal_value_kind(value) == TINYPY_VALUE_STRING
               ? TINYPY_STRING_OBJECT(value)->bytes
               : TINYPY_UNICODE_OBJECT(value)->utf8;
}
//////////////////////////////////////////////////////////////////////////
size_t tinypy_internal_text_byte_size(const tinypy_value_t *value) {
    assert(tinypy_internal_value_kind(value) == TINYPY_VALUE_STRING || tinypy_internal_value_kind(value) == TINYPY_VALUE_UNICODE);
    return tinypy_internal_value_kind(value) == TINYPY_VALUE_STRING
               ? (size_t)TINYPY_SIZE(value)
               : TINYPY_UNICODE_OBJECT(value)->byte_size;
}
//////////////////////////////////////////////////////////////////////////
int32_t tinypy_internal_string_is_interned(const tinypy_value_t *value) {
    assert(tinypy_internal_value_kind(value) == TINYPY_VALUE_STRING);
    return TINYPY_STRING_OBJECT(value)->interned;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_string_set_interned(tinypy_value_t *value, int32_t interned) {
    assert(tinypy_internal_value_kind(value) == TINYPY_VALUE_STRING);
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
    return value != NULL && tinypy_internal_value_vm(value) == vm;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_none_get(tinypy_vm_t *vm) {
    tinypy_value_t *result;

    assert(tinypy_internal_vm_valid(vm));
    result = &vm->none_object.base;
    tinypy_retain(result);

    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_not_implemented_get(tinypy_vm_t *vm) {
    tinypy_value_t *result;

    assert(tinypy_internal_vm_valid(vm));
    result = &vm->not_implemented_object.base;
    tinypy_retain(result);
    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_ellipsis_get(tinypy_vm_t *vm) {
    tinypy_value_t *result;

    assert(tinypy_internal_vm_valid(vm));
    result = &vm->ellipsis_object.base;
    tinypy_retain(result);
    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_bool_from_i32(tinypy_vm_t *vm, int32_t value) {
    tinypy_value_t *result;

    assert(tinypy_internal_vm_valid(vm));
    result = value != 0
                 ? &vm->true_object.base
                 : &vm->false_object.base;
    tinypy_retain(result);

    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_integer_from_i64(tinypy_vm_t *vm, int64_t value) {
    tinypy_value_t *result;
    size_t index;

    assert(tinypy_internal_vm_valid(vm));
    if (value >= TINYPY_INTEGER_CONSTANT_MIN && value <= TINYPY_INTEGER_CONSTANT_MAX) {
        index = (size_t)(value - TINYPY_INTEGER_CONSTANT_MIN);
        result = &vm->integer_constants[index].base;
        tinypy_retain(result);
        return result;
    }
    result = tinypy_internal_value_allocate(
        vm,
        TINYPY_VALUE_INTEGER,
        sizeof(tinypy_integer_object_t));

    TINYPY_INTEGER_VALUE(result) = value;
    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_string_from_bytes(tinypy_vm_t *vm, const void *bytes, size_t size) {
    assert(tinypy_internal_vm_valid(vm));
    assert(bytes != NULL || size == 0U);

    if (size == 0U) {
        tinypy_value_t *result = &vm->empty_string_object.base.base;

        tinypy_retain(result);
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
    assert(tinypy_internal_vm_valid(tinypy_internal_value_vm(value)));
    assert(out_size != NULL);
    assert(tinypy_internal_value_kind(value) == TINYPY_VALUE_STRING);

    *out_size = (size_t)TINYPY_SIZE(value);
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
    assert(tinypy_internal_vm_valid(tinypy_internal_value_vm(value)));
    assert(out_size != NULL);
    assert(out_code_point_count != NULL);
    assert(tinypy_internal_value_kind(value) == TINYPY_VALUE_UNICODE);

    *out_size = TINYPY_UNICODE_OBJECT(value)->byte_size;
    *out_code_point_count = (size_t)TINYPY_SIZE(value);
    return (const char *)TINYPY_UNICODE_OBJECT(value)->utf8;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_type_e tinypy_typeof(const tinypy_value_t *value) {
    assert(value != NULL);
    assert(tinypy_internal_vm_valid(tinypy_internal_value_vm(value)));

    return tinypy_internal_value_kind(value);
}
//////////////////////////////////////////////////////////////////////////
tinypy_vm_t *tinypy_value_vm(const tinypy_value_t *value) {
    assert(value != NULL);
    assert(tinypy_internal_vm_valid(tinypy_internal_value_vm(value)));
    return tinypy_internal_value_vm(value);
}
//////////////////////////////////////////////////////////////////////////
tinypy_ref_t tinypy_refcount(const tinypy_value_t *value) {
    assert(value != NULL);
    assert(tinypy_internal_vm_valid(tinypy_internal_value_vm(value)));
    return value->ref;
}
//////////////////////////////////////////////////////////////////////////
int32_t tinypy_is_callable(const tinypy_value_t *value) {
    assert(value != NULL);
    assert(tinypy_internal_vm_valid(tinypy_internal_value_vm(value)));
    return value->type->call != NULL || tinypy_internal_object_has_special((tinypy_value_t *)value, "__call__", 8U) != 0 ? INT32_C(1) : INT32_C(0);
}
//////////////////////////////////////////////////////////////////////////
const tinypy_type_t *tinypy_object_type(const tinypy_value_t *value) {
    assert(value != NULL);
    assert(tinypy_internal_vm_valid(tinypy_internal_value_vm(value)));
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
void tinypy_internal_type_destroy(tinypy_vm_t *vm, tinypy_value_t *value) {
    tinypy_type_t *type = (tinypy_type_t *)value;

    if (type->mro != NULL) {
        tinypy_internal_value_destroy(vm, type->mro);
        type->mro = NULL;
        tinypy_release(&vm->tuple_type.base.base);
    }
}
//////////////////////////////////////////////////////////////////////////
int32_t tinypy_bool_as_i32(const tinypy_value_t *value) {
    assert(value != NULL);
    assert(tinypy_internal_vm_valid(tinypy_internal_value_vm(value)));
    assert(tinypy_internal_value_kind(value) == TINYPY_VALUE_BOOL);

    return (int32_t)TINYPY_INTEGER_VALUE(value);
}
//////////////////////////////////////////////////////////////////////////
int64_t tinypy_integer_as_i64(const tinypy_value_t *value) {
    tinypy_value_type_e kind;

    assert(value != NULL);
    assert(tinypy_internal_vm_valid(tinypy_internal_value_vm(value)));
    kind = tinypy_internal_value_kind(value);
    assert(kind == TINYPY_VALUE_INTEGER || kind == TINYPY_VALUE_BOOL);

    if (kind == TINYPY_VALUE_BOOL) {
        return TINYPY_INTEGER_VALUE(value) != 0 ? INT64_C(1) : INT64_C(0);
    }

    return TINYPY_INTEGER_VALUE(value);
}
//////////////////////////////////////////////////////////////////////////
void tinypy_retain(tinypy_value_t *value) {
    assert(value != NULL);
    assert(tinypy_internal_vm_valid(tinypy_internal_value_vm(value)));

    assert(value->ref > 0);
    assert(value->ref < PTRDIFF_MAX - 1);

    value->ref += 1U;
}
