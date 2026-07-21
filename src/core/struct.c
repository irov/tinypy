#include "internal.h"

#include <assert.h>
#include <string.h>

typedef enum tinypy_struct_byte_order_e {
    TINYPY_STRUCT_NATIVE_ENDIAN,
    TINYPY_STRUCT_LITTLE_ENDIAN,
    TINYPY_STRUCT_BIG_ENDIAN
} tinypy_struct_byte_order_e;

typedef struct tinypy_struct_format_t {
    tinypy_struct_byte_order_e byte_order;
    size_t item_count;
    size_t byte_size;
} tinypy_struct_format_t;

//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_struct_arguments(tinypy_vm_t *vm, tinypy_value_t *args, tinypy_value_t *kwargs, size_t minimum, tinypy_error_t **out_error) {
    if ((kwargs != NULL && TINYPY_DICT_SIZE(kwargs) != 0U) || TINYPY_TUPLE_SIZE(args) < minimum) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "struct function received invalid arguments", out_error);
        return INT32_C(0);
    }
    return INT32_C(1);
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_struct_format_view(tinypy_vm_t *vm, tinypy_value_t *value, const unsigned char **out_bytes, size_t *out_size, tinypy_error_t **out_error) {
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);

    if (kind != TINYPY_VALUE_STRING && kind != TINYPY_VALUE_UNICODE) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "struct format must be a string", out_error);
        return INT32_C(0);
    }
    *out_bytes = TINYPY_TEXT_BYTES(value);
    *out_size = TINYPY_TEXT_BYTE_SIZE(value);
    return INT32_C(1);
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_struct_parse_format(tinypy_vm_t *vm, tinypy_value_t *value, tinypy_struct_format_t *out_format, tinypy_error_t **out_error) {
    const unsigned char *bytes;
    size_t size;
    size_t index = 0U;
    size_t count = 0U;
    int32_t have_count = INT32_C(0);

    if (__tinypy_struct_format_view(vm, value, &bytes, &size, out_error) == 0) {
        return INT32_C(0);
    }
    out_format->byte_order = TINYPY_STRUCT_NATIVE_ENDIAN;
    out_format->item_count = 0U;
    out_format->byte_size = 0U;
    if (index < size) {
        unsigned char prefix = bytes[index];

        if (prefix == (unsigned char)'<' || prefix == (unsigned char)'>' || prefix == (unsigned char)'!' || prefix == (unsigned char)'=' || prefix == (unsigned char)'@') {
            if (prefix == (unsigned char)'<') {
                out_format->byte_order = TINYPY_STRUCT_LITTLE_ENDIAN;
            }
            else if (prefix == (unsigned char)'>' || prefix == (unsigned char)'!') {
                out_format->byte_order = TINYPY_STRUCT_BIG_ENDIAN;
            }
            index += 1U;
        }
    }
    while (index < size) {
        unsigned char character = bytes[index++];

        if (character == (unsigned char)' ' || character == (unsigned char)'\t' || character == (unsigned char)'\r' || character == (unsigned char)'\n') {
            continue;
        }
        if (character >= (unsigned char)'0' && character <= (unsigned char)'9') {
            size_t digit = (size_t)(character - (unsigned char)'0');

            assert(count <= (SIZE_MAX - digit) / 10U);
            count = count * 10U + digit;
            have_count = INT32_C(1);
            continue;
        }
        if (character != (unsigned char)'d') {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "unsupported struct format character", out_error);
            return INT32_C(0);
        }
        if (have_count == 0) {
            count = 1U;
        }
        assert(count <= (SIZE_MAX - out_format->byte_size) / 8U);
        assert(count <= SIZE_MAX - out_format->item_count);
        out_format->byte_size += count * 8U;
        out_format->item_count += count;
        count = 0U;
        have_count = INT32_C(0);
    }
    if (have_count != 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "repeat count given without format specifier", out_error);
        return INT32_C(0);
    }
    return INT32_C(1);
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_struct_native_little_endian(void) {
    uint16_t probe = UINT16_C(1);

    return *((const unsigned char *)&probe) == (unsigned char)1 ? INT32_C(1) : INT32_C(0);
}
//////////////////////////////////////////////////////////////////////////
static uint64_t __tinypy_struct_read_u64(const unsigned char *bytes, tinypy_struct_byte_order_e byte_order) {
    int32_t little = byte_order == TINYPY_STRUCT_LITTLE_ENDIAN || (byte_order == TINYPY_STRUCT_NATIVE_ENDIAN && __tinypy_struct_native_little_endian() != 0);
    uint64_t bits = UINT64_C(0);
    size_t index;

    for (index = 0U; index < 8U; ++index) {
        size_t source_index = little != 0 ? 7U - index : index;

        bits = (bits << 8U) | bytes[source_index];
    }
    return bits;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_struct_write_u64(unsigned char *bytes, uint64_t bits, tinypy_struct_byte_order_e byte_order) {
    int32_t little = byte_order == TINYPY_STRUCT_LITTLE_ENDIAN || (byte_order == TINYPY_STRUCT_NATIVE_ENDIAN && __tinypy_struct_native_little_endian() != 0);
    size_t index;

    for (index = 0U; index < 8U; ++index) {
        size_t destination_index = little != 0 ? index : 7U - index;

        bytes[destination_index] = (unsigned char)(bits & UINT64_C(0xff));
        bits >>= 8U;
    }
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_struct_as_double(tinypy_vm_t *vm, tinypy_value_t *value, double *out_value, tinypy_error_t **out_error) {
    tinypy_value_type_e kind = TINYPY_VALUE_KIND(value);

    if (kind == TINYPY_VALUE_FLOAT) {
        *out_value = tinypy_float_as_double(value);
        return INT32_C(1);
    }
    if (kind == TINYPY_VALUE_BOOL || kind == TINYPY_VALUE_INTEGER) {
        *out_value = (double)TINYPY_INTEGER_VALUE(value);
        return INT32_C(1);
    }
    if (kind == TINYPY_VALUE_LONG && TINYPY_LONG_DIGIT_COUNT(value) <= 4U) {
        *out_value = (double)tinypy_long_as_i64(value);
        return INT32_C(1);
    }
    tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "required argument is not a float", out_error);
    return INT32_C(0);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_struct_calcsize(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_struct_format_t format;

    (void)user_data;
    if (__tinypy_struct_arguments(vm, args, kwargs, 1U, out_error) == 0 || TINYPY_TUPLE_SIZE(args) != 1U) {
        return NULL;
    }
    tinypy_value_t *item = TINYPY_TUPLE_GET(args, 0U);
    if (__tinypy_struct_parse_format(vm, item, &format, out_error) == 0) {
        return NULL;
    }
    assert(format.byte_size <= (size_t)INT64_MAX);
    return tinypy_integer_from_i64(vm, (int64_t)format.byte_size);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_struct_unpack(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_struct_format_t format;
    const unsigned char *bytes;
    size_t byte_size;
    tinypy_value_t **items;
    tinypy_value_t *result;
    size_t index;

    (void)user_data;
    if (__tinypy_struct_arguments(vm, args, kwargs, 2U, out_error) == 0 || TINYPY_TUPLE_SIZE(args) != 2U) {
        return NULL;
    }
    tinypy_value_t *item_2 = TINYPY_TUPLE_GET(args, 0U);
    if (__tinypy_struct_parse_format(vm, item_2, &format, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *item = TINYPY_TUPLE_GET(args, 1U);
    if (tinypy_internal_bytes_view(item, &bytes, &byte_size) == 0) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_TYPE, "unpack requires a string argument", out_error);
        return NULL;
    }
    if (byte_size != format.byte_size) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "unpack requires a string argument of the exact format size", out_error);
        return NULL;
    }
    if (format.item_count == 0U) {
        return tinypy_tuple_from_items(vm, NULL, 0U);
    }
    assert(format.item_count <= SIZE_MAX / sizeof(*items));
    items = (tinypy_value_t **)tinypy_internal_vm_allocate(vm, format.item_count * sizeof(*items), (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    for (index = 0U; index < format.item_count; ++index) {
        uint64_t bits = __tinypy_struct_read_u64(bytes + index * 8U, format.byte_order);
        double value;

        (void)memcpy(&value, &bits, sizeof(value));
        items[index] = tinypy_float_from_double(vm, value);
    }
    result = tinypy_tuple_from_items(vm, items, format.item_count);
    for (index = 0U; index < format.item_count; ++index) {
        TINYPY_DECREF(items[index]);
    }
    tinypy_internal_vm_deallocate(vm, items, format.item_count * sizeof(*items), (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_struct_pack(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);
    tinypy_struct_format_t format;
    unsigned char *bytes;
    tinypy_value_t *result;
    size_t index;

    (void)user_data;
    if (__tinypy_struct_arguments(vm, args, kwargs, 1U, out_error) == 0) {
        return NULL;
    }
    tinypy_value_t *item_2 = TINYPY_TUPLE_GET(args, 0U);
    if (__tinypy_struct_parse_format(vm, item_2, &format, out_error) == 0) {
        return NULL;
    }
    if (TINYPY_TUPLE_SIZE(args) != format.item_count + 1U) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_VALUE, "pack expected a different number of items", out_error);
        return NULL;
    }
    if (format.byte_size == 0U) {
        return tinypy_string_from_bytes(vm, NULL, 0U);
    }
    bytes = (unsigned char *)tinypy_internal_vm_allocate(vm, format.byte_size, (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    for (index = 0U; index < format.item_count; ++index) {
        double value;
        uint64_t bits;

        tinypy_value_t *item = TINYPY_TUPLE_GET(args, index + 1U);
        if (__tinypy_struct_as_double(vm, item, &value, out_error) == 0) {
            tinypy_internal_vm_deallocate(vm, bytes, format.byte_size, (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
            return NULL;
        }
        (void)memcpy(&bits, &value, sizeof(bits));
        __tinypy_struct_write_u64(bytes + index * 8U, bits, format.byte_order);
    }
    result = tinypy_string_from_bytes(vm, bytes, format.byte_size);
    tinypy_internal_vm_deallocate(vm, bytes, format.byte_size, (uint32_t)TINYPY_ALLOC_TAG_TEMPORARY);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_struct_clearcache(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(function);

    (void)user_data;
    if (__tinypy_struct_arguments(vm, args, kwargs, 0U, out_error) == 0 || TINYPY_TUPLE_SIZE(args) != 0U) {
        return NULL;
    }
    return tinypy_none_get(vm);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_struct_add_function(tinypy_vm_t *vm, tinypy_value_t *module, const char *name, size_t name_size, tinypy_native_function_callback_t callback) {
    tinypy_value_t *function = tinypy_native_function_new(vm, name, name_size, callback, NULL, NULL);

    tinypy_module_add_value(module, name, name_size, function);
    TINYPY_DECREF(function);
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_initialize_struct_module(tinypy_vm_t *vm) {
    tinypy_value_t *module = tinypy_module_new(vm, "_struct", 7U);
    tinypy_value_t *name = tinypy_string_from_bytes(vm, "_struct", 7U);
    tinypy_value_t *doc = tinypy_string_from_bytes(vm, "Functions to convert between Python values and C structs.", 57U);
    tinypy_value_t *version = tinypy_string_from_bytes(vm, "0.2", 3U);

    tinypy_module_add_value(module, "__name__", 8U, name);
    tinypy_module_add_value(module, "__doc__", 7U, doc);
    tinypy_module_add_value(module, "__version__", 11U, version);
    tinypy_module_add_value(module, "error", 5U, &vm->exception_types[TINYPY_EXCEPTION_VALUE_ERROR]->base.base);
    tinypy_module_add_value(module, "_PY_STRUCT_FLOAT_COERCE", 23U, &vm->true_object.base);
    tinypy_module_add_value(module, "_PY_STRUCT_RANGE_CHECKING", 25U, &vm->true_object.base);
    __tinypy_struct_add_function(vm, module, "calcsize", 8U, __tinypy_struct_calcsize);
    __tinypy_struct_add_function(vm, module, "pack", 4U, __tinypy_struct_pack);
    __tinypy_struct_add_function(vm, module, "unpack", 6U, __tinypy_struct_unpack);
    __tinypy_struct_add_function(vm, module, "_clearcache", 11U, __tinypy_struct_clearcache);
    TINYPY_DECREF(version);
    TINYPY_DECREF(doc);
    TINYPY_DECREF(name);
    tinypy_internal_register_module(vm, "_struct", 7U, module);
    TINYPY_DECREF(module);
}
