#include "tinypy/marshal.h"

#include "internal.h"

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
    uint8_t *buffer;
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
    size_t return_value_1 = strlen(message);
    return return_value_1;
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
static tinypy_bool_t __tinypy_marshal_dump_put(tinypy_marshal_dump_writer_t *writer, const void *bytes, size_t size) {
    if (size > writer->max_output_bytes - writer->offset) {
        __tinypy_marshal_dump_fail(writer, TINYPY_MARSHAL_OUTPUT_LIMIT, "marshal output exceeds configured byte limit");
        return TINYPY_FALSE;
    }
    if (writer->buffer != NULL) {
        if (size != 0U) {
            (void)memcpy(writer->buffer + writer->offset, bytes, size);
        }
    }
    writer->offset += size;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_marshal_dump_u8(tinypy_marshal_dump_writer_t *writer, uint8_t value) {
    tinypy_bool_t return_value_1 = __tinypy_marshal_dump_put(writer, &value, 1U);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_marshal_dump_u16(tinypy_marshal_dump_writer_t *writer, uint16_t value) {
    uint8_t bytes[2];

    bytes[0] = (uint8_t)(value & UINT16_C(0xff));
    bytes[1] = (uint8_t)(value >> 8U);
    tinypy_bool_t return_value_1 = __tinypy_marshal_dump_put(writer, bytes, sizeof(bytes));
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_marshal_dump_i32(tinypy_marshal_dump_writer_t *writer, int32_t value) {
    uint32_t bits = (uint32_t)value;
    uint8_t bytes[4];

    bytes[0] = (uint8_t)(bits & UINT32_C(0xff));
    bytes[1] = (uint8_t)((bits >> 8U) & UINT32_C(0xff));
    bytes[2] = (uint8_t)((bits >> 16U) & UINT32_C(0xff));
    bytes[3] = (uint8_t)(bits >> 24U);
    tinypy_bool_t return_value_1 = __tinypy_marshal_dump_put(writer, bytes, sizeof(bytes));
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_marshal_dump_i64(tinypy_marshal_dump_writer_t *writer, int64_t value) {
    uint64_t bits = (uint64_t)value;
    uint8_t bytes[8];
    size_t index;

    for (index = 0U; index < sizeof(bytes); ++index) {
        bytes[index] = (uint8_t)(bits >> (index * 8U));
    }
    tinypy_bool_t return_value_1 = __tinypy_marshal_dump_put(writer, bytes, sizeof(bytes));
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_marshal_dump_size32(tinypy_marshal_dump_writer_t *writer, size_t size) {
    if (size > (size_t)INT32_MAX) {
        __tinypy_marshal_dump_fail(writer, TINYPY_MARSHAL_INVALID_SIZE, "marshal object size exceeds signed 32-bit range");
        return TINYPY_FALSE;
    }
    tinypy_bool_t return_value_1 = __tinypy_marshal_dump_i32(writer, (int32_t)size);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_marshal_dump_double(tinypy_marshal_dump_writer_t *writer, double value) {
    uint64_t bits;

    (void)memcpy(&bits, &value, sizeof(bits));
    tinypy_bool_t return_value_1 = __tinypy_marshal_dump_i64(writer, (int64_t)bits);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static ptrdiff_t __tinypy_marshal_dump_find_intern(const tinypy_marshal_dump_writer_t *writer, const uint8_t *bytes, size_t size) {
    size_t index;

    for (index = 0U; index < writer->intern_count; ++index) {
        size_t candidate_size;
        const uint8_t *candidate = (const uint8_t *)tinypy_string_view(writer->interns[index], &candidate_size);

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

        new_size = new_capacity * sizeof(*writer->interns);
        if (writer->interns == NULL) {
            writer->interns = (const tinypy_value_t **)tinypy_internal_vm_allocate(writer->vm, new_size);
        }
        else {
            writer->interns = (const tinypy_value_t **)tinypy_internal_vm_reallocate(writer->vm, (void *)writer->interns, old_size, new_size);
        }
        writer->intern_capacity = new_capacity;
    }
    writer->interns[writer->intern_count++] = value;
}

static tinypy_bool_t __tinypy_marshal_dump_value(tinypy_marshal_dump_writer_t *writer, const tinypy_value_t *value, size_t depth, tinypy_marshal_dump_string_context_e string_context);

//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_marshal_dump_string(tinypy_marshal_dump_writer_t *writer, const tinypy_value_t *value, tinypy_marshal_dump_string_context_e context) {
    size_t size;
    const uint8_t *bytes = (const uint8_t *)tinypy_string_view(value, &size);
    tinypy_bool_t interned = tinypy_internal_string_is_interned(value);

    (void)context;

    if (interned != 0) {
        ptrdiff_t index = __tinypy_marshal_dump_find_intern(writer, bytes, size);

        if (index >= 0) {
            tinypy_bool_t return_value_1 = __tinypy_marshal_dump_u8(writer, (uint8_t)'R') && __tinypy_marshal_dump_i32(writer, (int32_t)index);
            return return_value_1;
        }
        __tinypy_marshal_dump_add_intern(writer, value);
        if (__tinypy_marshal_dump_u8(writer, (uint8_t)'t') == 0) {
            return TINYPY_FALSE;
        }
    }
    else if (__tinypy_marshal_dump_u8(writer, (uint8_t)'s') == 0) {
        return TINYPY_FALSE;
    }
    tinypy_bool_t return_value_2 = __tinypy_marshal_dump_size32(writer, size) && __tinypy_marshal_dump_put(writer, bytes, size);
    return return_value_2;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_marshal_dump_tuple(tinypy_marshal_dump_writer_t *writer, const tinypy_value_t *tuple, size_t depth, tinypy_marshal_dump_string_context_e item_context) {
    size_t count = TINYPY_TUPLE_SIZE(tuple);
    tinypy_value_t *const *iterator = TINYPY_TUPLE_ITERATOR_BEGIN(tuple);
    tinypy_value_t *const *iterator_end = TINYPY_TUPLE_ITERATOR_END(tuple);

    if (__tinypy_marshal_dump_u8(writer, (uint8_t)'(') == 0 || __tinypy_marshal_dump_size32(writer, count) == 0) {
        return TINYPY_FALSE;
    }
    for (; iterator != iterator_end; ++iterator) {
        tinypy_value_t *item = *iterator;
        if (__tinypy_marshal_dump_value(writer, item, depth + 1U, item_context) == 0) {
            return TINYPY_FALSE;
        }
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_marshal_dump_code(tinypy_marshal_dump_writer_t *writer, const tinypy_value_t *value, size_t depth) {
    const tinypy_code_object_t *code = TINYPY_CODE_OBJECT((tinypy_value_t *)value);

    tinypy_bool_t return_value_1 = __tinypy_marshal_dump_u8(writer, (uint8_t)'c') && __tinypy_marshal_dump_i32(writer, code->arg_count) && __tinypy_marshal_dump_i32(writer, code->local_count) && __tinypy_marshal_dump_i32(writer, code->stack_size) && __tinypy_marshal_dump_i32(writer, code->flags) && __tinypy_marshal_dump_value(writer, code->bytecode, depth + 1U, TINYPY_MARSHAL_DUMP_STRING_NORMAL) && __tinypy_marshal_dump_tuple(writer, code->consts, depth + 1U, TINYPY_MARSHAL_DUMP_STRING_LITERAL) && __tinypy_marshal_dump_tuple(writer, code->names, depth + 1U, TINYPY_MARSHAL_DUMP_STRING_IDENTIFIER) && __tinypy_marshal_dump_tuple(writer, code->varnames, depth + 1U, TINYPY_MARSHAL_DUMP_STRING_IDENTIFIER) && __tinypy_marshal_dump_tuple(writer, code->freevars, depth + 1U, TINYPY_MARSHAL_DUMP_STRING_IDENTIFIER) && __tinypy_marshal_dump_tuple(writer, code->cellvars, depth + 1U, TINYPY_MARSHAL_DUMP_STRING_IDENTIFIER) && __tinypy_marshal_dump_value(writer, code->filename, depth + 1U, TINYPY_MARSHAL_DUMP_STRING_NORMAL) && __tinypy_marshal_dump_value(writer, code->name, depth + 1U, TINYPY_MARSHAL_DUMP_STRING_CODE_NAME) && __tinypy_marshal_dump_i32(writer, code->first_line_number) && __tinypy_marshal_dump_value(writer, code->lnotab, depth + 1U, TINYPY_MARSHAL_DUMP_STRING_NORMAL);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_marshal_dump_set(tinypy_marshal_dump_writer_t *writer, const tinypy_value_t *value, size_t depth) {
    tinypy_value_t *dict = TINYPY_SET_OBJECT((tinypy_value_t *)value)->dict;
    const tinypy_dict_entry_t *iterator = TINYPY_DICT_ITERATOR_BEGIN(dict);
    const tinypy_dict_entry_t *iterator_end = TINYPY_DICT_ITERATOR_END(dict);

    tinypy_value_type_e type = tinypy_typeof(value);
    uint8_t marker = type == TINYPY_VALUE_FROZENSET ? (uint8_t)'>' : (uint8_t)'<';
    tinypy_bool_t condition = __tinypy_marshal_dump_u8(writer, marker) == 0;
    if (condition == 0) {
        condition = __tinypy_marshal_dump_size32(writer, TINYPY_DICT_SIZE(dict)) == 0;
    }
    if (condition) {
        return TINYPY_FALSE;
    }
    for (; iterator != iterator_end; ++iterator) {
        if (iterator->state == TINYPY_DICT_ENTRY_ACTIVE && __tinypy_marshal_dump_value(writer, iterator->key, depth + 1U, TINYPY_MARSHAL_DUMP_STRING_LITERAL) == 0) {
            return TINYPY_FALSE;
        }
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_marshal_dump_value(tinypy_marshal_dump_writer_t *writer, const tinypy_value_t *value, size_t depth, tinypy_marshal_dump_string_context_e string_context) {
    tinypy_bool_t function_result;
    tinypy_value_type_e type;

    if (depth > writer->max_depth) {
        __tinypy_marshal_dump_fail(writer, TINYPY_MARSHAL_DEPTH_LIMIT, "marshal object graph exceeds configured depth limit");
        return TINYPY_FALSE;
    }
    type = tinypy_typeof(value);
    switch (type) {
    case TINYPY_VALUE_NONE:
        function_result = __tinypy_marshal_dump_u8(writer, (uint8_t)'N');
        return function_result;
    case TINYPY_VALUE_BOOL: {
        int32_t bool_value = tinypy_bool_as_i32(value);
        uint8_t marker = bool_value != 0 ? (uint8_t)'T' : (uint8_t)'F';
        tinypy_bool_t return_value_1 = __tinypy_marshal_dump_u8(writer, marker);
        return return_value_1;
    }
    case TINYPY_VALUE_ELLIPSIS:
        function_result = __tinypy_marshal_dump_u8(writer, (uint8_t)'.');
        return function_result;
    case TINYPY_VALUE_INTEGER: {
        int64_t integer = tinypy_integer_as_i64(value);

        if (integer >= INT32_MIN && integer <= INT32_MAX) {
            tinypy_bool_t return_value_2 = __tinypy_marshal_dump_u8(writer, (uint8_t)'i') && __tinypy_marshal_dump_i32(writer, (int32_t)integer);
            return return_value_2;
        }
        tinypy_bool_t return_value_3 = __tinypy_marshal_dump_u8(writer, (uint8_t)'I') && __tinypy_marshal_dump_i64(writer, integer);
        return return_value_3;
    }
    case TINYPY_VALUE_LONG: {
        const uint16_t *digits;
        size_t count;
        int32_t sign;
        size_t index;

        digits = tinypy_long_base15_view(value, &sign, &count);
        if (count > (size_t)INT32_MAX) {
            __tinypy_marshal_dump_fail(writer, TINYPY_MARSHAL_INVALID_LONG, "marshal long digit count exceeds signed 32-bit range");
            return TINYPY_FALSE;
        }
        if (__tinypy_marshal_dump_u8(writer, (uint8_t)'l') == 0 || __tinypy_marshal_dump_i32(writer, (int32_t)count * sign) == 0) {
            return TINYPY_FALSE;
        }
        for (index = 0U; index < count; ++index) {
            if (__tinypy_marshal_dump_u16(writer, digits[index]) == 0) {
                return TINYPY_FALSE;
            }
        }
        return TINYPY_TRUE;
    }
    case TINYPY_VALUE_FLOAT: {
        double float_value = tinypy_float_as_double(value);
        tinypy_bool_t return_value_4 = __tinypy_marshal_dump_u8(writer, (uint8_t)'g') && __tinypy_marshal_dump_double(writer, float_value);
        return return_value_4;
    }
    case TINYPY_VALUE_COMPLEX: {
        double real;
        double imaginary;

        tinypy_complex_as_doubles(value, &real, &imaginary);
        tinypy_bool_t return_value_5 = __tinypy_marshal_dump_u8(writer, (uint8_t)'y') && __tinypy_marshal_dump_double(writer, real) && __tinypy_marshal_dump_double(writer, imaginary);
        return return_value_5;
    }
    case TINYPY_VALUE_STRING:
        function_result = __tinypy_marshal_dump_string(writer, value, string_context);
        return function_result;
    case TINYPY_VALUE_UNICODE: {
        size_t size;
        size_t code_points;
        const char *utf8 = tinypy_unicode_utf8_view(value, &size, &code_points);

        (void)code_points;
        tinypy_bool_t return_value_6 = __tinypy_marshal_dump_u8(writer, (uint8_t)'u') && __tinypy_marshal_dump_size32(writer, size) && __tinypy_marshal_dump_put(writer, utf8, size);
        return return_value_6;
    }
    case TINYPY_VALUE_TUPLE:
        function_result = __tinypy_marshal_dump_tuple(writer, value, depth, string_context);
        return function_result;
    case TINYPY_VALUE_CODE:
        function_result = __tinypy_marshal_dump_code(writer, value, depth);
        return function_result;
    case TINYPY_VALUE_SET:
    case TINYPY_VALUE_FROZENSET:
        function_result = __tinypy_marshal_dump_set(writer, value, depth);
        return function_result;
    default:
        __tinypy_marshal_dump_fail(writer, TINYPY_MARSHAL_UNSUPPORTED_RUNTIME_TYPE, "runtime value is not supported by marshal v2 code writer");
        return TINYPY_FALSE;
    }
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_marshal_dump_writer_destroy(tinypy_marshal_dump_writer_t *writer) {
    if (writer->interns != NULL) {
        tinypy_internal_vm_deallocate(writer->vm, (void *)writer->interns, writer->intern_capacity * sizeof(*writer->interns));
    }
}
//////////////////////////////////////////////////////////////////////////
static tinypy_marshal_result_e __tinypy_marshal_dump_run(const tinypy_value_t *code, void *buffer, size_t capacity, const tinypy_marshal_write_options_t *options, size_t *out_size, tinypy_marshal_error_t *out_error) {
    tinypy_marshal_dump_writer_t writer;

    (void)memset(&writer, 0, sizeof(writer));
    writer.vm = TINYPY_VALUE_VM(code);
    writer.buffer = (uint8_t *)buffer;
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

    if (options == NULL) {
        (void)memset(&defaults, 0, sizeof(defaults));
        defaults.abi_version = TINYPY_MARSHAL_ABI_VERSION;
        defaults.struct_size = (uint32_t)sizeof(defaults);
        defaults.max_output_bytes = TINYPY_MARSHAL_DUMP_DEFAULT_BYTES;
        defaults.max_depth = TINYPY_MARSHAL_DUMP_DEFAULT_DEPTH;
        options = &defaults;
    }
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
    tinypy_marshal_result_e return_value_1 = __tinypy_marshal_dump_run(code, buffer, capacity, options, out_size, out_error);
    return return_value_1;
}
