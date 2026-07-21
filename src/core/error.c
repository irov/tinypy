#include "tinypy/error.h"

#include "internal.h"

#include <string.h>

//////////////////////////////////////////////////////////////////////////
static size_t __tinypy_internal_string_length(const char *text) {
    size_t length = 0U;

    assert(text != NULL);

    while (text[length] != '\0') {
        length += 1U;
    }

    return length;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_internal_make_error_location(const tinypy_allocator_t *allocator, tinypy_error_kind_e error_kind, const char *message, const char *logical_filename, size_t filename_size, int32_t line_number, int32_t column_offset, const char *source_line, size_t source_line_size, tinypy_error_t **out_error) {
    size_t message_size;
    size_t allocation_size;
    char *cursor;

    if (out_error == NULL) {
        return;
    }
    assert(allocator != NULL);
    assert(allocator->abi_version == TINYPY_ABI_VERSION);
    assert(allocator->struct_size >= (uint32_t)sizeof(*allocator));
    assert(allocator->allocate != NULL);
    assert(allocator->deallocate != NULL);
    assert(message != NULL);
    assert(logical_filename != NULL || filename_size == 0U);
    assert(source_line != NULL || source_line_size == 0U);

    message_size = __tinypy_internal_string_length(message);
    assert(message_size <= SIZE_MAX - sizeof(tinypy_error_t) - 1U);
    allocation_size = sizeof(tinypy_error_t) + message_size + 1U;
    assert(filename_size <= SIZE_MAX - allocation_size - 1U);
    allocation_size += filename_size + 1U;
    assert(source_line_size <= SIZE_MAX - allocation_size - 1U);
    allocation_size += source_line_size + 1U;

    tinypy_error_t *error = (tinypy_error_t *)allocator->allocate(allocator->user_data, allocation_size, TINYPY_INTERNAL_ALIGNMENT, (uint32_t)TINYPY_ALLOC_TAG_ERROR);
    assert(error != NULL);

    error->allocator = *allocator;
    error->kind = error_kind;
    error->allocation_size = allocation_size;
    error->message_size = message_size;
    error->filename_size = filename_size;
    error->source_line_size = source_line_size;
    error->line_number = line_number;
    error->column_offset = column_offset;

    cursor = error->data;
    if (message_size != 0U) {
        (void)memcpy(cursor, message, message_size);
    }
    cursor[message_size] = '\0';
    cursor += message_size + 1U;
    if (filename_size != 0U) {
        (void)memcpy(cursor, logical_filename, filename_size);
    }
    cursor[filename_size] = '\0';
    cursor += filename_size + 1U;
    if (source_line_size != 0U) {
        (void)memcpy(cursor, source_line, source_line_size);
    }
    cursor[source_line_size] = '\0';
    *out_error = error;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_internal_set_syntax_exception_location(tinypy_vm_t *vm, const char *message, const char *logical_filename, size_t filename_size, int32_t line_number, int32_t column_offset, const char *source_line, size_t source_line_size) {
    tinypy_value_t *exception = vm->raised_value;
    tinypy_value_t *line_value;
    tinypy_value_t *offset_value;
    tinypy_value_t *text_value;
    tinypy_value_t *location_items[4];
    tinypy_value_t *location;
    tinypy_value_t *args_items[2];
    tinypy_value_t *args;

    if (exception == NULL) {
        return;
    }
    size_t string_length = __tinypy_internal_string_length(message);
    tinypy_value_t *message_value = tinypy_string_from_bytes(vm, message, string_length);
    tinypy_value_t *filename_value = logical_filename != NULL ? tinypy_string_from_bytes(vm, logical_filename, filename_size) : tinypy_none_get(vm);
    line_value = tinypy_integer_from_i64(vm, line_number);
    offset_value = tinypy_integer_from_i64(vm, column_offset);
    text_value = source_line != NULL ? tinypy_string_from_bytes(vm, source_line, source_line_size) : tinypy_none_get(vm);
    location_items[0] = filename_value;
    location_items[1] = line_value;
    location_items[2] = offset_value;
    location_items[3] = text_value;
    location = tinypy_tuple_from_items(vm, location_items, 4U);
    args_items[0] = message_value;
    args_items[1] = location;
    args = tinypy_tuple_from_items(vm, args_items, 2U);
    tinypy_instance_set_attr(exception, "args", 4U, args);
    tinypy_instance_set_attr(exception, "filename", 8U, filename_value);
    tinypy_instance_set_attr(exception, "lineno", 6U, line_value);
    tinypy_instance_set_attr(exception, "offset", 6U, offset_value);
    tinypy_instance_set_attr(exception, "text", 4U, text_value);
    TINYPY_DECREF(args);
    TINYPY_DECREF(location);
    TINYPY_DECREF(text_value);
    TINYPY_DECREF(offset_value);
    TINYPY_DECREF(line_value);
    TINYPY_DECREF(filename_value);
    TINYPY_DECREF(message_value);
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_make_error(const tinypy_allocator_t *allocator, tinypy_error_kind_e error_kind, const char *message, tinypy_error_t **out_error) {
    __tinypy_internal_make_error_location(allocator, error_kind, message, NULL, 0U, 0, 0, NULL, 0U, out_error);
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_make_vm_error_location(tinypy_vm_t *vm, tinypy_error_kind_e error_kind, const char *message, const char *logical_filename, size_t filename_size, int32_t line_number, int32_t column_offset, const char *source_line, size_t source_line_size, tinypy_error_t **out_error) {
    tinypy_internal_exception_raise_kind(vm, error_kind, message);
    if (error_kind == TINYPY_ERROR_SYNTAX || error_kind == TINYPY_ERROR_INDENTATION || error_kind == TINYPY_ERROR_TAB || error_kind == TINYPY_ERROR_SOURCE_DECODING) {
        __tinypy_internal_set_syntax_exception_location(vm, message, logical_filename, filename_size, line_number, column_offset, source_line, source_line_size);
    }
    __tinypy_internal_make_error_location(&vm->allocator, error_kind, message, logical_filename, filename_size, line_number, column_offset, source_line, source_line_size, out_error);
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_make_vm_error(tinypy_vm_t *vm, tinypy_error_kind_e error_kind, const char *message, tinypy_error_t **out_error) {
    tinypy_internal_exception_raise_kind(vm, error_kind, message);
    tinypy_internal_make_error(&vm->allocator, error_kind, message, out_error);
}
//////////////////////////////////////////////////////////////////////////
uint32_t tinypy_abi_version(void) {
    return TINYPY_ABI_VERSION;
}
//////////////////////////////////////////////////////////////////////////
const char *tinypy_error_kind_name(tinypy_error_kind_e error_kind) {
    switch (error_kind) {
    case TINYPY_ERROR_TYPE:
        return "type error";
    case TINYPY_ERROR_RUNTIME:
        return "runtime error";
    case TINYPY_ERROR_NAME:
        return "name error";
    case TINYPY_ERROR_UNBOUND_LOCAL:
        return "unbound local error";
    case TINYPY_ERROR_INTERRUPT:
        return "interrupt";
    case TINYPY_ERROR_ZERO_DIVISION:
        return "zero division error";
    case TINYPY_ERROR_VALUE:
        return "value error";
    case TINYPY_ERROR_INDEX:
        return "index error";
    case TINYPY_ERROR_KEY:
        return "key error";
    case TINYPY_ERROR_OVERFLOW:
        return "overflow error";
    case TINYPY_ERROR_IMPORT:
        return "import error";
    case TINYPY_ERROR_ATTRIBUTE:
        return "attribute error";
    case TINYPY_ERROR_LOOKUP:
        return "lookup error";
    case TINYPY_ERROR_SYNTAX:
        return "syntax error";
    case TINYPY_ERROR_INDENTATION:
        return "indentation error";
    case TINYPY_ERROR_TAB:
        return "tab error";
    case TINYPY_ERROR_SOURCE_DECODING:
        return "source decoding error";
    case TINYPY_ERROR_COMPILER_LIMIT:
        return "compiler limit";
    case TINYPY_ERROR_PREPROCESSOR:
        return "preprocessor error";
    case TINYPY_ERROR_META:
        return "meta error";
    default:
        return "unknown error kind";
    }
}
//////////////////////////////////////////////////////////////////////////
tinypy_error_kind_e tinypy_error_kind(const tinypy_error_t *error) {
    assert(error != NULL);

    return error->kind;
}
//////////////////////////////////////////////////////////////////////////
const char *tinypy_error_message(const tinypy_error_t *error, size_t *out_size) {
    assert(error != NULL);

    if (out_size != NULL) {
        *out_size = error->message_size;
    }

    return error->data;
}
//////////////////////////////////////////////////////////////////////////
const char *tinypy_error_logical_filename(const tinypy_error_t *error, size_t *out_size) {
    assert(error != NULL);
    if (out_size != NULL) {
        *out_size = error->filename_size;
    }
    return error->filename_size != 0U ? error->data + error->message_size + 1U : NULL;
}
//////////////////////////////////////////////////////////////////////////
const char *tinypy_error_source_line(const tinypy_error_t *error, size_t *out_size) {
    assert(error != NULL);
    if (out_size != NULL) {
        *out_size = error->source_line_size;
    }
    return error->source_line_size != 0U ? error->data + error->message_size + 1U + error->filename_size + 1U : NULL;
}
//////////////////////////////////////////////////////////////////////////
int32_t tinypy_error_line_number(const tinypy_error_t *error) {
    assert(error != NULL);
    return error->line_number;
}
//////////////////////////////////////////////////////////////////////////
int32_t tinypy_error_column_offset(const tinypy_error_t *error) {
    assert(error != NULL);
    return error->column_offset;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_error_release(tinypy_error_t *error) {
    tinypy_allocator_t allocator;
    size_t allocation_size;

    assert(error != NULL);

    allocator = error->allocator;
    allocation_size = error->allocation_size;
    allocator.deallocate(
        allocator.user_data,
        error,
        allocation_size,
        TINYPY_INTERNAL_ALIGNMENT,
        (uint32_t)TINYPY_ALLOC_TAG_ERROR);
}
