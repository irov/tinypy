#include "tinypy/marshal.h"

#include "internal.h"

#include <assert.h>
#include <limits.h>
#include <string.h>

#define TINYPY_MARSHAL_DUMP_DEFAULT_BYTES ((size_t)67108864U)
#define TINYPY_MARSHAL_DUMP_DEFAULT_DEPTH ((size_t)2000U)

typedef enum tinypy_marshal_dump_string_context_e {
    TINYPY_MARSHAL_DUMP_STRING_NORMAL = 0,
    TINYPY_MARSHAL_DUMP_STRING_LITERAL = 1,
    TINYPY_MARSHAL_DUMP_STRING_IDENTIFIER = 2,
    TINYPY_MARSHAL_DUMP_STRING_CODE_NAME = 3
} tinypy_marshal_dump_string_context_e;

typedef struct tinypy_marshal_dump_writer_t {
    tinypy_vm_t *vm;
    unsigned char *buffer;
    size_t capacity;
    size_t offset;
    size_t max_output_bytes;
    size_t max_depth;
    const tinypy_value_t **interns;
    size_t intern_count;
    size_t intern_capacity;
    tinypy_marshal_result_e result;
    tinypy_marshal_error_t *error;
} tinypy_marshal_dump_writer_t;

//////////////////////////////////////////////////////////////////////////
static size_t __tinypy_marshal_dump_message_size(const char *message) {
    return strlen(message);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_marshal_dump_fail(tinypy_marshal_dump_writer_t *writer, tinypy_marshal_result_e result, const char *message) {
    if (writer->result != TINYPY_MARSHAL_OK) {
        return;
    }
    writer->result = result;
    if (writer->error == NULL) {
        return;
    }
    (void)memset(writer->error, 0, sizeof(*writer->error));
    writer->error->abi_version = TINYPY_MARSHAL_ABI_VERSION;
    writer->error->struct_size = (uint32_t)sizeof(*writer->error);
    writer->error->code = result;
    writer->error->offset = writer->offset;
    writer->error->object_index = SIZE_MAX;
    writer->error->message = message;
    writer->error->message_size = __tinypy_marshal_dump_message_size(message);
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_marshal_dump_put(tinypy_marshal_dump_writer_t *writer, const void *bytes, size_t size) {
    if (size > writer->max_output_bytes - writer->offset) {
        __tinypy_marshal_dump_fail(writer, TINYPY_MARSHAL_OUTPUT_LIMIT, "marshal output exceeds configured byte limit");
        return INT32_C(0);
    }
    if (writer->buffer != NULL) {
        assert(writer->offset <= writer->capacity);
        assert(size <= writer->capacity - writer->offset);
        if (size != 0U) {
            (void)memcpy(writer->buffer + writer->offset, bytes, size);
        }
    }
    writer->offset += size;
    return INT32_C(1);
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_marshal_dump_u8(tinypy_marshal_dump_writer_t *writer, uint8_t value) {
    return __tinypy_marshal_dump_put(writer, &value, 1U);
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_marshal_dump_u16(tinypy_marshal_dump_writer_t *writer, uint16_t value) {
    unsigned char bytes[2];

    bytes[0] = (unsigned char)(value & UINT16_C(0xff));
    bytes[1] = (unsigned char)(value >> 8U);
    return __tinypy_marshal_dump_put(writer, bytes, sizeof(bytes));
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_marshal_dump_i32(tinypy_marshal_dump_writer_t *writer, int32_t value) {
    uint32_t bits = (uint32_t)value;
    unsigned char bytes[4];

    bytes[0] = (unsigned char)(bits & UINT32_C(0xff));
    bytes[1] = (unsigned char)((bits >> 8U) & UINT32_C(0xff));
    bytes[2] = (unsigned char)((bits >> 16U) & UINT32_C(0xff));
    bytes[3] = (unsigned char)(bits >> 24U);
    return __tinypy_marshal_dump_put(writer, bytes, sizeof(bytes));
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_marshal_dump_i64(tinypy_marshal_dump_writer_t *writer, int64_t value) {
    uint64_t bits = (uint64_t)value;
    unsigned char bytes[8];
    size_t index;

    for (index = 0U; index < sizeof(bytes); ++index) {
        bytes[index] = (unsigned char)(bits >> (index * 8U));
    }
    return __tinypy_marshal_dump_put(writer, bytes, sizeof(bytes));
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_marshal_dump_size32(tinypy_marshal_dump_writer_t *writer, size_t size) {
    if (size > (size_t)INT32_MAX) {
        __tinypy_marshal_dump_fail(writer, TINYPY_MARSHAL_INVALID_SIZE, "marshal object size exceeds signed 32-bit range");
        return INT32_C(0);
    }
    return __tinypy_marshal_dump_i32(writer, (int32_t)size);
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_marshal_dump_double(tinypy_marshal_dump_writer_t *writer, double value) {
    uint64_t bits;

    (void)memcpy(&bits, &value, sizeof(bits));
    return __tinypy_marshal_dump_i64(writer, (int64_t)bits);
}
//////////////////////////////////////////////////////////////////////////
static ptrdiff_t __tinypy_marshal_dump_find_intern(const tinypy_marshal_dump_writer_t *writer, const unsigned char *bytes, size_t size) {
    size_t index;

    for (index = 0U; index < writer->intern_count; ++index) {
        size_t candidate_size;
        const unsigned char *candidate = (const unsigned char *)tinypy_string_view(writer->interns[index], &candidate_size);

        if (candidate_size == size && (size == 0U || memcmp(candidate, bytes, size) == 0)) {
            return (ptrdiff_t)index;
        }
    }
    return -1;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_marshal_dump_add_intern(tinypy_marshal_dump_writer_t *writer, const tinypy_value_t *value) {
    if (writer->intern_count == writer->intern_capacity) {
        size_t old_size = writer->intern_capacity * sizeof(*writer->interns);
        size_t new_capacity = writer->intern_capacity == 0U ? 32U : writer->intern_capacity * 2U;
        size_t new_size;

        assert(new_capacity > writer->intern_capacity);
        assert(new_capacity <= SIZE_MAX / sizeof(*writer->interns));
        new_size = new_capacity * sizeof(*writer->interns);
        if (writer->interns == NULL) {
            writer->interns = (const tinypy_value_t **)tinypy_internal_vm_allocate(writer->vm, new_size, (uint32_t)TINYPY_ALLOC_TAG_MARSHAL_WRITE);
        }
        else {
            writer->interns = (const tinypy_value_t **)tinypy_internal_vm_reallocate(writer->vm, (void *)writer->interns, old_size, new_size, (uint32_t)TINYPY_ALLOC_TAG_MARSHAL_WRITE);
        }
        writer->intern_capacity = new_capacity;
    }
    writer->interns[writer->intern_count++] = value;
}

static int32_t __tinypy_marshal_dump_value(tinypy_marshal_dump_writer_t *writer, const tinypy_value_t *value, size_t depth, tinypy_marshal_dump_string_context_e string_context);

//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_marshal_dump_string(tinypy_marshal_dump_writer_t *writer, const tinypy_value_t *value, tinypy_marshal_dump_string_context_e context) {
    size_t size;
    const unsigned char *bytes = (const unsigned char *)tinypy_string_view(value, &size);
    int32_t interned = tinypy_internal_string_is_interned(value);

    (void)context;

    if (interned != 0) {
        ptrdiff_t index = __tinypy_marshal_dump_find_intern(writer, bytes, size);

        if (index >= 0) {
            return __tinypy_marshal_dump_u8(writer, (uint8_t)'R') && __tinypy_marshal_dump_i32(writer, (int32_t)index);
        }
        __tinypy_marshal_dump_add_intern(writer, value);
        if (__tinypy_marshal_dump_u8(writer, (uint8_t)'t') == 0) {
            return INT32_C(0);
        }
    }
    else if (__tinypy_marshal_dump_u8(writer, (uint8_t)'s') == 0) {
        return INT32_C(0);
    }
    return __tinypy_marshal_dump_size32(writer, size) && __tinypy_marshal_dump_put(writer, bytes, size);
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_marshal_dump_tuple(tinypy_marshal_dump_writer_t *writer, const tinypy_value_t *tuple, size_t depth, tinypy_marshal_dump_string_context_e item_context) {
    size_t count = TINYPY_TUPLE_SIZE(tuple);
    tinypy_value_t *const *iterator = TINYPY_TUPLE_ITERATOR_BEGIN(tuple);
    tinypy_value_t *const *iterator_end = TINYPY_TUPLE_ITERATOR_END(tuple);

    if (__tinypy_marshal_dump_u8(writer, (uint8_t)'(') == 0 || __tinypy_marshal_dump_size32(writer, count) == 0) {
        return INT32_C(0);
    }
    for (; iterator != iterator_end; ++iterator) {
        tinypy_value_t *item = *iterator;
        if (__tinypy_marshal_dump_value(writer, item, depth + 1U, item_context) == 0) {
            return INT32_C(0);
        }
    }
    return INT32_C(1);
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_marshal_dump_code(tinypy_marshal_dump_writer_t *writer, const tinypy_value_t *value, size_t depth) {
    const tinypy_code_object_t *code = TINYPY_CODE_OBJECT((tinypy_value_t *)value);

    return __tinypy_marshal_dump_u8(writer, (uint8_t)'c') && __tinypy_marshal_dump_i32(writer, code->arg_count) && __tinypy_marshal_dump_i32(writer, code->local_count) && __tinypy_marshal_dump_i32(writer, code->stack_size) && __tinypy_marshal_dump_i32(writer, code->flags) && __tinypy_marshal_dump_value(writer, code->bytecode, depth + 1U, TINYPY_MARSHAL_DUMP_STRING_NORMAL) && __tinypy_marshal_dump_tuple(writer, code->consts, depth + 1U, TINYPY_MARSHAL_DUMP_STRING_LITERAL) && __tinypy_marshal_dump_tuple(writer, code->names, depth + 1U, TINYPY_MARSHAL_DUMP_STRING_IDENTIFIER) && __tinypy_marshal_dump_tuple(writer, code->varnames, depth + 1U, TINYPY_MARSHAL_DUMP_STRING_IDENTIFIER) && __tinypy_marshal_dump_tuple(writer, code->freevars, depth + 1U, TINYPY_MARSHAL_DUMP_STRING_IDENTIFIER) && __tinypy_marshal_dump_tuple(writer, code->cellvars, depth + 1U, TINYPY_MARSHAL_DUMP_STRING_IDENTIFIER) && __tinypy_marshal_dump_value(writer, code->filename, depth + 1U, TINYPY_MARSHAL_DUMP_STRING_NORMAL) && __tinypy_marshal_dump_value(writer, code->name, depth + 1U, TINYPY_MARSHAL_DUMP_STRING_CODE_NAME) && __tinypy_marshal_dump_i32(writer, code->first_line_number) && __tinypy_marshal_dump_value(writer, code->lnotab, depth + 1U, TINYPY_MARSHAL_DUMP_STRING_NORMAL);
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_marshal_dump_set(tinypy_marshal_dump_writer_t *writer, const tinypy_value_t *value, size_t depth) {
    tinypy_value_t *dict = TINYPY_SET_OBJECT((tinypy_value_t *)value)->dict;
    const tinypy_dict_entry_t *iterator = TINYPY_DICT_ITERATOR_BEGIN(dict);
    const tinypy_dict_entry_t *iterator_end = TINYPY_DICT_ITERATOR_END(dict);

    tinypy_value_type_e type = tinypy_typeof(value);
    uint8_t marker = type == TINYPY_VALUE_FROZENSET ? (uint8_t)'>' : (uint8_t)'<';
    int condition = __tinypy_marshal_dump_u8(writer, marker) == 0;
    if (condition == 0) {
        condition = __tinypy_marshal_dump_size32(writer, TINYPY_DICT_SIZE(dict)) == 0;
    }
    if (condition) {
        return INT32_C(0);
    }
    for (; iterator != iterator_end; ++iterator) {
        if (iterator->state == TINYPY_DICT_ENTRY_ACTIVE && __tinypy_marshal_dump_value(writer, iterator->key, depth + 1U, TINYPY_MARSHAL_DUMP_STRING_LITERAL) == 0) {
            return INT32_C(0);
        }
    }
    return INT32_C(1);
}
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_marshal_dump_value(tinypy_marshal_dump_writer_t *writer, const tinypy_value_t *value, size_t depth, tinypy_marshal_dump_string_context_e string_context) {
    tinypy_value_type_e type;

    if (depth > writer->max_depth) {
        __tinypy_marshal_dump_fail(writer, TINYPY_MARSHAL_DEPTH_LIMIT, "marshal object graph exceeds configured depth limit");
        return INT32_C(0);
    }
    type = tinypy_typeof(value);
    switch (type) {
    case TINYPY_VALUE_NONE:
        return __tinypy_marshal_dump_u8(writer, (uint8_t)'N');
    case TINYPY_VALUE_BOOL: {
        int32_t bool_value = tinypy_bool_as_i32(value);
        uint8_t marker = bool_value != 0 ? (uint8_t)'T' : (uint8_t)'F';
        return __tinypy_marshal_dump_u8(writer, marker);
    }
    case TINYPY_VALUE_ELLIPSIS:
        return __tinypy_marshal_dump_u8(writer, (uint8_t)'.');
    case TINYPY_VALUE_INTEGER: {
        int64_t integer = tinypy_integer_as_i64(value);

        if (integer >= INT32_MIN && integer <= INT32_MAX) {
            return __tinypy_marshal_dump_u8(writer, (uint8_t)'i') && __tinypy_marshal_dump_i32(writer, (int32_t)integer);
        }
        return __tinypy_marshal_dump_u8(writer, (uint8_t)'I') && __tinypy_marshal_dump_i64(writer, integer);
    }
    case TINYPY_VALUE_LONG: {
        const uint16_t *digits;
        size_t count;
        int sign;
        size_t index;

        digits = tinypy_long_base15_view(value, &sign, &count);
        if (count > (size_t)INT32_MAX) {
            __tinypy_marshal_dump_fail(writer, TINYPY_MARSHAL_INVALID_LONG, "marshal long digit count exceeds signed 32-bit range");
            return INT32_C(0);
        }
        if (__tinypy_marshal_dump_u8(writer, (uint8_t)'l') == 0 || __tinypy_marshal_dump_i32(writer, (int32_t)count * sign) == 0) {
            return INT32_C(0);
        }
        for (index = 0U; index < count; ++index) {
            if (__tinypy_marshal_dump_u16(writer, digits[index]) == 0) {
                return INT32_C(0);
            }
        }
        return INT32_C(1);
    }
    case TINYPY_VALUE_FLOAT: {
        double float_value = tinypy_float_as_double(value);
        return __tinypy_marshal_dump_u8(writer, (uint8_t)'g') && __tinypy_marshal_dump_double(writer, float_value);
    }
    case TINYPY_VALUE_COMPLEX: {
        double real;
        double imaginary;

        tinypy_complex_as_doubles(value, &real, &imaginary);
        return __tinypy_marshal_dump_u8(writer, (uint8_t)'y') && __tinypy_marshal_dump_double(writer, real) && __tinypy_marshal_dump_double(writer, imaginary);
    }
    case TINYPY_VALUE_STRING:
        return __tinypy_marshal_dump_string(writer, value, string_context);
    case TINYPY_VALUE_UNICODE: {
        size_t size;
        size_t code_points;
        const char *utf8 = tinypy_unicode_utf8_view(value, &size, &code_points);

        (void)code_points;
        return __tinypy_marshal_dump_u8(writer, (uint8_t)'u') && __tinypy_marshal_dump_size32(writer, size) && __tinypy_marshal_dump_put(writer, utf8, size);
    }
    case TINYPY_VALUE_TUPLE:
        return __tinypy_marshal_dump_tuple(writer, value, depth, string_context);
    case TINYPY_VALUE_CODE:
        return __tinypy_marshal_dump_code(writer, value, depth);
    case TINYPY_VALUE_SET:
    case TINYPY_VALUE_FROZENSET:
        return __tinypy_marshal_dump_set(writer, value, depth);
    default:
        __tinypy_marshal_dump_fail(writer, TINYPY_MARSHAL_UNSUPPORTED_RUNTIME_TYPE, "runtime value is not supported by marshal v2 code writer");
        return INT32_C(0);
    }
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_marshal_dump_writer_destroy(tinypy_marshal_dump_writer_t *writer) {
    if (writer->interns != NULL) {
        tinypy_internal_vm_deallocate(writer->vm, (void *)writer->interns, writer->intern_capacity * sizeof(*writer->interns), (uint32_t)TINYPY_ALLOC_TAG_MARSHAL_WRITE);
    }
}
//////////////////////////////////////////////////////////////////////////
static tinypy_marshal_result_e __tinypy_marshal_dump_run(const tinypy_value_t *code, void *buffer, size_t capacity, const tinypy_marshal_write_options_t *options, size_t *out_size, tinypy_marshal_error_t *out_error) {
    tinypy_marshal_dump_writer_t writer;

    (void)memset(&writer, 0, sizeof(writer));
    writer.vm = TINYPY_VALUE_VM(code);
    writer.buffer = (unsigned char *)buffer;
    writer.capacity = capacity;
    writer.max_output_bytes = options->max_output_bytes;
    writer.max_depth = options->max_depth;
    writer.result = TINYPY_MARSHAL_OK;
    writer.error = out_error;
    (void)__tinypy_marshal_dump_code(&writer, code, 0U);
    *out_size = writer.offset;
    __tinypy_marshal_dump_writer_destroy(&writer);
    return writer.result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_marshal_result_e tinypy_marshal_dump_code_v2(const tinypy_value_t *code, void *buffer, size_t capacity, size_t *out_size, const tinypy_marshal_write_options_t *options, tinypy_marshal_error_t *out_error) {
    tinypy_marshal_write_options_t defaults;
    size_t required_size = 0U;
    tinypy_marshal_result_e result;

    assert(code != NULL);
    assert(tinypy_typeof(code) == TINYPY_VALUE_CODE);
    assert(buffer != NULL || capacity == 0U);
    assert(out_size != NULL);
    if (options == NULL) {
        (void)memset(&defaults, 0, sizeof(defaults));
        defaults.abi_version = TINYPY_MARSHAL_ABI_VERSION;
        defaults.struct_size = (uint32_t)sizeof(defaults);
        defaults.max_output_bytes = TINYPY_MARSHAL_DUMP_DEFAULT_BYTES;
        defaults.max_depth = TINYPY_MARSHAL_DUMP_DEFAULT_DEPTH;
        options = &defaults;
    }
    assert(options->abi_version == TINYPY_MARSHAL_ABI_VERSION);
    assert(options->struct_size >= (uint32_t)sizeof(*options));
    assert(options->max_output_bytes != 0U);
    assert(options->max_depth != 0U);
    if (out_error != NULL) {
        (void)memset(out_error, 0, sizeof(*out_error));
    }
    result = __tinypy_marshal_dump_run(code, NULL, 0U, options, &required_size, out_error);
    *out_size = required_size;
    if (result != TINYPY_MARSHAL_OK) {
        return result;
    }
    if (buffer == NULL) {
        return TINYPY_MARSHAL_OK;
    }
    if (capacity < required_size) {
        tinypy_marshal_dump_writer_t error_writer;

        (void)memset(&error_writer, 0, sizeof(error_writer));
        error_writer.result = TINYPY_MARSHAL_OK;
        error_writer.error = out_error;
        __tinypy_marshal_dump_fail(&error_writer, TINYPY_MARSHAL_BUFFER_TOO_SMALL, "caller marshal output buffer is too small");
        return TINYPY_MARSHAL_BUFFER_TOO_SMALL;
    }
    return __tinypy_marshal_dump_run(code, buffer, capacity, options, out_size, out_error);
}
