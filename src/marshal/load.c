#include "tinypy/marshal.h"

#include "internal.h"

#include "assertion.h"
#include <string.h>

typedef struct tinypy_marshal_runtime_cache_entry_t {
    const tinypy_marshal_object_t *source;
    tinypy_value_t *value;
} tinypy_marshal_runtime_cache_entry_t;

typedef struct tinypy_marshal_materializer_t {
    tinypy_vm_t *vm;
    tinypy_marshal_runtime_cache_entry_t *cache;
    size_t cache_size;
    size_t cache_capacity;
    tinypy_marshal_error_t *error;
} tinypy_marshal_materializer_t;

//////////////////////////////////////////////////////////////////////////
static size_t __tinypy_marshal_load_cstring_size(const char *text) {
    size_t size = 0U;

    while (text[size] != '\0') {
        size += 1U;
    }
    return size;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_marshal_load_set_error(tinypy_marshal_error_t *error, tinypy_marshal_result_e result, uint8_t wire_type, const char *message) {
    if (error == NULL) {
        return;
    }

    (void)memset(error, 0, sizeof(*error));
    error->abi_version = TINYPY_MARSHAL_ABI_VERSION;
    error->struct_size = (uint32_t)sizeof(*error);
    error->code = result;
    error->wire_type = wire_type;
    error->object_index = SIZE_MAX;
    error->message = message;
    error->message_size = __tinypy_marshal_load_cstring_size(message);
}
//////////////////////////////////////////////////////////////////////////
static void *__tinypy_marshal_vm_allocate(void *user_data, size_t size, size_t alignment, uint32_t tag) {
    tinypy_vm_t *vm = (tinypy_vm_t *)user_data;

    TINYPY_ASSERT(alignment <= TINYPY_INTERNAL_ALIGNMENT);
    (void)alignment;
    return tinypy_internal_vm_allocate(vm, size, tag);
}
//////////////////////////////////////////////////////////////////////////
static void *__tinypy_marshal_vm_reallocate(void *user_data, void *memory, size_t old_size, size_t new_size, size_t alignment, uint32_t tag) {
    tinypy_vm_t *vm = (tinypy_vm_t *)user_data;

    TINYPY_ASSERT(alignment <= TINYPY_INTERNAL_ALIGNMENT);
    (void)alignment;
    return tinypy_internal_vm_reallocate(vm, memory, old_size, new_size, tag);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_marshal_vm_deallocate(void *user_data, void *memory, size_t size, size_t alignment, uint32_t tag) {
    tinypy_vm_t *vm = (tinypy_vm_t *)user_data;

    TINYPY_ASSERT(alignment <= TINYPY_INTERNAL_ALIGNMENT);
    (void)alignment;
    tinypy_internal_vm_deallocate(vm, memory, size, tag);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_allocator_t __tinypy_marshal_vm_allocator(tinypy_vm_t *vm) {
    tinypy_allocator_t allocator;

    (void)memset(&allocator, 0, sizeof(allocator));
    allocator.abi_version = TINYPY_ABI_VERSION;
    allocator.struct_size = (uint32_t)sizeof(allocator);
    allocator.user_data = vm;
    allocator.allocate = __tinypy_marshal_vm_allocate;
    allocator.reallocate = __tinypy_marshal_vm_reallocate;
    allocator.deallocate = __tinypy_marshal_vm_deallocate;
    return allocator;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_marshal_cache_find(const tinypy_marshal_materializer_t *materializer, const tinypy_marshal_object_t *source) {
    size_t index;

    for (index = 0U; index < materializer->cache_size; ++index) {
        if (materializer->cache[index].source == source) {
            return materializer->cache[index].value;
        }
    }
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_marshal_cache_append(tinypy_marshal_materializer_t *materializer, const tinypy_marshal_object_t *source, tinypy_value_t *value) {
    size_t old_size;
    size_t new_capacity;
    size_t new_size;

    if (materializer->cache_size == materializer->cache_capacity) {
        old_size = materializer->cache_capacity * sizeof(*materializer->cache);
        new_capacity = materializer->cache_capacity == 0U ? 16U : materializer->cache_capacity * 2U;
        TINYPY_ASSERT(new_capacity > materializer->cache_capacity);
        TINYPY_ASSERT(new_capacity <= SIZE_MAX / sizeof(*materializer->cache));
        new_size = new_capacity * sizeof(*materializer->cache);
        if (materializer->cache == NULL) {
            materializer->cache = (tinypy_marshal_runtime_cache_entry_t *)tinypy_internal_vm_allocate(materializer->vm, new_size, (uint32_t)TINYPY_ALLOC_TAG_MARSHAL_CACHE);
        }
        else {
            materializer->cache = (tinypy_marshal_runtime_cache_entry_t *)tinypy_internal_vm_reallocate(materializer->vm, materializer->cache, old_size, new_size, (uint32_t)TINYPY_ALLOC_TAG_MARSHAL_CACHE);
        }
        materializer->cache_capacity = new_capacity;
    }

    materializer->cache[materializer->cache_size].source = source;
    materializer->cache[materializer->cache_size].value = value;
    materializer->cache_size += 1U;
    TINYPY_INCREF(value);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_marshal_cache_destroy(tinypy_marshal_materializer_t *materializer) {
    size_t index;

    for (index = 0U; index < materializer->cache_size; ++index) {
        TINYPY_DECREF(materializer->cache[index].value);
    }
    if (materializer->cache != NULL) {
        tinypy_internal_vm_deallocate(materializer->vm, materializer->cache, materializer->cache_capacity * sizeof(*materializer->cache), (uint32_t)TINYPY_ALLOC_TAG_MARSHAL_CACHE);
    }
}

static tinypy_marshal_result_e __tinypy_marshal_materialize_object(tinypy_marshal_materializer_t *materializer, const tinypy_marshal_object_t *source, tinypy_value_t **out_value);

//////////////////////////////////////////////////////////////////////////
static tinypy_marshal_result_e __tinypy_marshal_materialize_sequence(tinypy_marshal_materializer_t *materializer, const tinypy_marshal_object_t *source, int32_t as_list, tinypy_value_t **out_value) {
    tinypy_value_t **items = NULL;
    size_t count = tinypy_marshal_sequence_size(source);
    size_t index;
    tinypy_marshal_result_e result = TINYPY_MARSHAL_OK;

    if (count != 0U) {
        TINYPY_ASSERT(count <= SIZE_MAX / sizeof(*items));
        items = (tinypy_value_t **)tinypy_internal_vm_allocate(materializer->vm, count * sizeof(*items), (uint32_t)TINYPY_ALLOC_TAG_MARSHAL_CACHE);
    }
    for (index = 0U; index < count; ++index) {
        const tinypy_marshal_object_t *marshal_sequence_item = tinypy_marshal_sequence_item(source, index);
        result = __tinypy_marshal_materialize_object(materializer, marshal_sequence_item, &items[index]);
        if (result != TINYPY_MARSHAL_OK) {
            break;
        }
    }
    if (result == TINYPY_MARSHAL_OK) {
        *out_value = as_list != 0 ? tinypy_list_from_items(materializer->vm, items, count) : tinypy_tuple_from_items(materializer->vm, items, count);
    }
    while (index != 0U) {
        index -= 1U;
        TINYPY_DECREF(items[index]);
    }
    if (items != NULL) {
        tinypy_internal_vm_deallocate(materializer->vm, items, count * sizeof(*items), (uint32_t)TINYPY_ALLOC_TAG_MARSHAL_CACHE);
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_marshal_result_e __tinypy_marshal_materialize_dict(tinypy_marshal_materializer_t *materializer, const tinypy_marshal_object_t *source, tinypy_value_t **out_value) {
    tinypy_value_t *dict = tinypy_dict_new(materializer->vm);
    size_t count = tinypy_marshal_dict_size(source);
    size_t index;

    for (index = 0U; index < count; ++index) {
        tinypy_value_t *key = NULL;
        tinypy_value_t *value = NULL;
        tinypy_marshal_result_e result;

        const tinypy_marshal_object_t *marshal_dict_key = tinypy_marshal_dict_key(source, index);
        result = __tinypy_marshal_materialize_object(materializer, marshal_dict_key, &key);
        if (result == TINYPY_MARSHAL_OK) {
            const tinypy_marshal_object_t *marshal_dict_value = tinypy_marshal_dict_value(source, index);
            result = __tinypy_marshal_materialize_object(materializer, marshal_dict_value, &value);
        }
        if (result != TINYPY_MARSHAL_OK) {
            if (key != NULL) {
                TINYPY_DECREF(key);
            }
            TINYPY_DECREF(dict);
            return result;
        }
        tinypy_dict_set(dict, key, value);
        TINYPY_DECREF(value);
        TINYPY_DECREF(key);
    }
    *out_value = dict;
    return TINYPY_MARSHAL_OK;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_marshal_result_e __tinypy_marshal_materialize_code(tinypy_marshal_materializer_t *materializer, const tinypy_marshal_object_t *source, tinypy_value_t **out_value) {
    const tinypy_marshal_code_t *source_code = tinypy_marshal_code_view(source);
    const tinypy_marshal_object_t *source_fields[9];
    tinypy_value_t *fields[9] = {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};
    tinypy_marshal_result_e result = TINYPY_MARSHAL_OK;
    size_t index;

    source_fields[0] = source_code->bytecode;
    source_fields[1] = source_code->consts;
    source_fields[2] = source_code->names;
    source_fields[3] = source_code->varnames;
    source_fields[4] = source_code->freevars;
    source_fields[5] = source_code->cellvars;
    source_fields[6] = source_code->filename;
    source_fields[7] = source_code->name;
    source_fields[8] = source_code->lnotab;

    for (index = 0U; index < 9U; ++index) {
        result = __tinypy_marshal_materialize_object(materializer, source_fields[index], &fields[index]);
        if (result != TINYPY_MARSHAL_OK) {
            break;
        }
    }
    if (result == TINYPY_MARSHAL_OK) {
        *out_value = tinypy_code_new(source_code->argcount, source_code->nlocals, source_code->stacksize, source_code->flags, fields[0], fields[1], fields[2], fields[3], fields[4], fields[5], fields[6], fields[7], source_code->firstlineno, fields[8]);
    }
    while (index != 0U) {
        index -= 1U;
        TINYPY_DECREF(fields[index]);
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_marshal_result_e __tinypy_marshal_materialize_object(tinypy_marshal_materializer_t *materializer, const tinypy_marshal_object_t *source, tinypy_value_t **out_value) {
    tinypy_marshal_type_e type = tinypy_marshal_object_type(source);

    *out_value = NULL;
    switch (type) {
    case TINYPY_MARSHAL_TYPE_NONE:
        *out_value = tinypy_none_get(materializer->vm);
        return TINYPY_MARSHAL_OK;
    case TINYPY_MARSHAL_TYPE_BOOL: {
        int32_t bool_value = (int32_t)tinypy_marshal_bool_value(source);
        *out_value = tinypy_bool_from_i32(materializer->vm, bool_value);
        return TINYPY_MARSHAL_OK;
    }
    case TINYPY_MARSHAL_TYPE_INTEGER: {
        int64_t integer = tinypy_marshal_integer_value(source);
        *out_value = tinypy_integer_from_i64(materializer->vm, integer);
        return TINYPY_MARSHAL_OK;
    }
    case TINYPY_MARSHAL_TYPE_LONG: {
        const uint16_t *digits;
        size_t digit_count;
        int32_t sign;

        tinypy_marshal_long_view(source, &sign, &digits, &digit_count);
        *out_value = tinypy_long_from_base15_digits(materializer->vm, sign, digits, digit_count);
    }
        return TINYPY_MARSHAL_OK;
    case TINYPY_MARSHAL_TYPE_FLOAT: {
        double float_value = tinypy_marshal_float_value(source);
        *out_value = tinypy_float_from_double(materializer->vm, float_value);
        return TINYPY_MARSHAL_OK;
    }
    case TINYPY_MARSHAL_TYPE_COMPLEX: {
        double real;
        double imaginary;

        tinypy_marshal_complex_value(source, &real, &imaginary);
        *out_value = tinypy_complex_from_doubles(materializer->vm, real, imaginary);
    }
        return TINYPY_MARSHAL_OK;
    case TINYPY_MARSHAL_TYPE_BYTES: {
        const void *bytes;
        size_t size;
        int32_t interned;

        tinypy_marshal_bytes_view(source, &bytes, &size, &interned);
        if (interned != 0) {
            *out_value = __tinypy_marshal_cache_find(materializer, source);
            if (*out_value != NULL) {
                TINYPY_INCREF(*out_value);
                return TINYPY_MARSHAL_OK;
            }
        }
        *out_value = tinypy_string_from_bytes(materializer->vm, bytes, size);
        if (interned != 0) {
            tinypy_internal_string_set_interned(*out_value, 1);
            __tinypy_marshal_cache_append(materializer, source, *out_value);
        }
        else if (size > 1U) {
            tinypy_internal_string_set_interned(*out_value, 0);
        }
    }
        return TINYPY_MARSHAL_OK;
    case TINYPY_MARSHAL_TYPE_UNICODE: {
        const char *utf8;
        size_t size;
        size_t code_point_count;

        tinypy_marshal_unicode_view(source, &utf8, &size, &code_point_count);
        (void)code_point_count;
        *out_value = tinypy_unicode_from_utf8(materializer->vm, utf8, size);
    }
        return TINYPY_MARSHAL_OK;
    case TINYPY_MARSHAL_TYPE_TUPLE:
        return __tinypy_marshal_materialize_sequence(materializer, source, 0, out_value);
    case TINYPY_MARSHAL_TYPE_LIST:
        return __tinypy_marshal_materialize_sequence(materializer, source, 1, out_value);
    case TINYPY_MARSHAL_TYPE_DICT:
        return __tinypy_marshal_materialize_dict(materializer, source, out_value);
    case TINYPY_MARSHAL_TYPE_CODE:
        return __tinypy_marshal_materialize_code(materializer, source, out_value);
    case TINYPY_MARSHAL_TYPE_STOP_ITERATION:
    case TINYPY_MARSHAL_TYPE_ELLIPSIS:
    case TINYPY_MARSHAL_TYPE_SET:
    case TINYPY_MARSHAL_TYPE_FROZENSET: {
        uint8_t wire_type = tinypy_marshal_object_wire_type(source);
        __tinypy_marshal_load_set_error(materializer->error, TINYPY_MARSHAL_UNSUPPORTED_RUNTIME_TYPE, wire_type, "marshal object type is not implemented by the runtime");
        return TINYPY_MARSHAL_UNSUPPORTED_RUNTIME_TYPE;
    }
    default:
        TINYPY_ASSERT(!"invalid marshal object type");
        return TINYPY_MARSHAL_INVALID_GRAPH;
    }
}
//////////////////////////////////////////////////////////////////////////
tinypy_marshal_result_e tinypy_marshal_load_code_v2(tinypy_vm_t *vm, const void *bytes, size_t size, const tinypy_marshal_limits_t *limits, tinypy_value_t **out_code, tinypy_marshal_error_t *out_error) {
    tinypy_allocator_t allocator;
    tinypy_marshal_document_t *document = NULL;
    tinypy_marshal_materializer_t materializer;
    tinypy_marshal_result_e result;

    TINYPY_ASSERT(tinypy_internal_vm_valid(vm));
    TINYPY_ASSERT(bytes != NULL || size == 0U);
    TINYPY_ASSERT(out_code != NULL);
    *out_code = NULL;
    allocator = __tinypy_marshal_vm_allocator(vm);
    result = tinypy_marshal_read_v2(bytes, size, &allocator, limits, &document, out_error);
    if (result != TINYPY_MARSHAL_OK) {
        return result;
    }

    const tinypy_marshal_object_t *root = tinypy_marshal_document_root(document);
    if (tinypy_marshal_object_type(root) != TINYPY_MARSHAL_TYPE_CODE) {
        uint8_t marshal_object_wire_type = tinypy_marshal_object_wire_type(root);
        __tinypy_marshal_load_set_error(out_error, TINYPY_MARSHAL_ROOT_NOT_CODE, marshal_object_wire_type, "top-level marshal object is not a code object");
        tinypy_marshal_document_destroy(document);
        return TINYPY_MARSHAL_ROOT_NOT_CODE;
    }

    (void)memset(&materializer, 0, sizeof(materializer));
    materializer.vm = vm;
    materializer.error = out_error;
    result = __tinypy_marshal_materialize_object(&materializer, root, out_code);
    __tinypy_marshal_cache_destroy(&materializer);
    tinypy_marshal_document_destroy(document);
    return result;
}
