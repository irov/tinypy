#include "tinypy/error.h"

#include "internal.h"

#include <string.h>

//////////////////////////////////////////////////////////////////////////
static size_t __tinypy_internal_string_length(const char *text) {
    size_t length = 0U;

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
    *out_error = NULL;

    message_size = __tinypy_internal_string_length(message);
    if (message_size > SIZE_MAX - sizeof(tinypy_error_t) - 1U) {
        return;
    }
    allocation_size = sizeof(tinypy_error_t) + message_size + 1U;
    if (filename_size > SIZE_MAX - allocation_size - 1U) {
        return;
    }
    allocation_size += filename_size + 1U;
    if (source_line_size > SIZE_MAX - allocation_size - 1U) {
        return;
    }
    allocation_size += source_line_size + 1U;

    tinypy_error_t *error = (tinypy_error_t *)allocator->allocate(allocator->user_data, allocation_size, TINYPY_INTERNAL_ALIGNMENT);

    if (error == NULL) {
        return;
    }
    error->allocator = *allocator;
    error->kind = error_kind;
    error->allocation_size = allocation_size;
    error->message_size = message_size;
    error->filename_size = filename_size;
    error->source_line_size = source_line_size;
    error->line_number = line_number;
    error->column_offset = column_offset;
    error->vm = NULL;
    error->exception = NULL;
    error->previous = NULL;
    error->next = NULL;
    error->rendered_message = NULL;

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
static void __tinypy_internal_set_syntax_exception_location(tinypy_vm_t *vm, const char *message, const char *logical_filename, size_t filename_size, int32_t line_number, int32_t column_offset, const char *source_line, size_t source_line_size, tinypy_bool_t include_location) {
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
    tinypy_value_t *filename_value = logical_filename != NULL ? tinypy_string_from_bytes(vm, logical_filename, filename_size) : TINYPY_RET_NONE(vm);
    line_value = line_number >= 0 ? tinypy_integer_from_i64(vm, line_number) : TINYPY_RET_NONE(vm);
    offset_value = column_offset >= 0 ? tinypy_integer_from_i64(vm, column_offset) : TINYPY_RET_NONE(vm);
    text_value = source_line != NULL ? tinypy_string_from_bytes(vm, source_line, source_line_size) : TINYPY_RET_NONE(vm);
    location_items[0] = filename_value;
    location_items[1] = line_value;
    location_items[2] = offset_value;
    location_items[3] = text_value;
    location = tinypy_tuple_from_items(vm, location_items, 4U);
    args_items[0] = message_value;
    args_items[1] = location;
    args = tinypy_tuple_from_items(vm, args_items, include_location != 0 ? 2U : 1U);
    /* The members of SyntaxError are descriptors of its type. */
    tinypy_value_t *const attribute_names[6] = {vm->internal_args_key, vm->internal_msg_key, vm->internal_filename_key, vm->internal_lineno_key, vm->internal_offset_key, vm->internal_text_key};
    tinypy_value_t *const attribute_values[6] = {args, message_value, filename_value, line_value, offset_value, text_value};
    for (size_t index = 0U; index < 6U; ++index) {
        tinypy_error_t *attribute_error = NULL;

        (void)tinypy_object_set_attr_value(exception, attribute_names[index], attribute_values[index], &attribute_error);
        if (attribute_error != NULL) {
            tinypy_error_release(attribute_error);
        }
    }
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
void tinypy_internal_make_vm_error_location(tinypy_vm_t *vm, tinypy_error_kind_e error_kind, const char *message, const char *logical_filename, size_t filename_size, int32_t line_number, int32_t column_offset, const char *source_line, size_t source_line_size, tinypy_bool_t include_location, tinypy_error_t **out_error) {
    tinypy_internal_exception_raise_kind(vm, error_kind, message);
    if (error_kind == TINYPY_ERROR_SYNTAX || error_kind == TINYPY_ERROR_INDENTATION || error_kind == TINYPY_ERROR_TAB || error_kind == TINYPY_ERROR_SOURCE_DECODING) {
        __tinypy_internal_set_syntax_exception_location(vm, message, logical_filename, filename_size, line_number, column_offset, source_line, source_line_size, include_location);
    }
    __tinypy_internal_make_error_location(&vm->allocator, error_kind, message, logical_filename, filename_size, line_number, column_offset, source_line, source_line_size, out_error);
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_make_vm_error(tinypy_vm_t *vm, tinypy_error_kind_e error_kind, const char *message, tinypy_error_t **out_error) {
    tinypy_internal_exception_raise_kind(vm, error_kind, message);
    tinypy_internal_make_error(&vm->allocator, error_kind, message, out_error);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_internal_error_unlink(tinypy_error_t *error) {
    if (error->previous != NULL) {
        error->previous->next = error->next;
    }
    else {
        error->vm->pending_diagnostics = error->next;
    }
    if (error->next != NULL) {
        error->next->previous = error->previous;
    }
    error->vm = NULL;
    error->previous = NULL;
    error->next = NULL;
}
//////////////////////////////////////////////////////////////////////////
/* The message is str() of the exception up to its first NUL byte, rendered
   with the pending exception state of the VM set aside; a failed str() reads
   as PyErr_Display reports it. */
static void __tinypy_internal_error_render(tinypy_error_t *error) {
    static const char fallback_message[] = "<exception str() failed>";
    tinypy_vm_t *vm = error->vm;
    tinypy_value_t *exception = error->exception;

    __tinypy_internal_error_unlink(error);
    error->exception = NULL;
    tinypy_internal_exception_state_t state;
    tinypy_internal_exception_preserve_begin(vm, &state);
    tinypy_value_t *rendered = tinypy_object_str(exception, NULL);
    tinypy_internal_exception_preserve_end(vm, &state);
    const char *message = fallback_message;
    size_t message_size = sizeof(fallback_message) - 1U;

    if (rendered != NULL && TINYPY_VALUE_KIND(rendered) == TINYPY_VALUE_STRING) {
        const char *bytes = (const char *)TINYPY_TEXT_BYTES(rendered);
        const char *terminator = (const char *)memchr(bytes, '\0', TINYPY_TEXT_BYTE_SIZE(rendered));

        message = bytes;
        message_size = terminator != NULL ? (size_t)(terminator - bytes) : TINYPY_TEXT_BYTE_SIZE(rendered);
    }
    char *buffer = (char *)error->allocator.allocate(error->allocator.user_data, message_size + 1U, TINYPY_INTERNAL_ALIGNMENT);

    if (buffer != NULL) {
        if (message_size != 0U) {
            (void)memcpy(buffer, message, message_size);
        }
        buffer[message_size] = '\0';
        error->rendered_message = buffer;
        error->message_size = message_size;
    }
    if (rendered != NULL) {
        TINYPY_DECREF(rendered);
    }
    TINYPY_DECREF(exception);
}
//////////////////////////////////////////////////////////////////////////
/* Diagnostics of exceptions that Python code handles are released unread, so
   str() runs only for a diagnostic that is reported, as in CPython. */
void tinypy_internal_make_exception_error(tinypy_vm_t *vm, tinypy_error_kind_e error_kind, tinypy_value_t *exception, tinypy_error_t **out_error) {
    __tinypy_internal_make_error_location(&vm->allocator, error_kind, "", NULL, 0U, 0, 0, NULL, 0U, out_error);
    if (out_error == NULL || *out_error == NULL) {
        return;
    }
    tinypy_error_t *error = *out_error;

    error->vm = vm;
    error->exception = TINYPY_RET(exception);
    error->next = vm->pending_diagnostics;
    if (vm->pending_diagnostics != NULL) {
        vm->pending_diagnostics->previous = error;
    }
    vm->pending_diagnostics = error;
    if (vm->state != TINYPY_VM_STATE_LIVE) {
        __tinypy_internal_error_render(error);
    }
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_error_render_pending(tinypy_vm_t *vm) {
    while (vm->pending_diagnostics != NULL) {
        __tinypy_internal_error_render(vm->pending_diagnostics);
    }
}
//////////////////////////////////////////////////////////////////////////
size_t tinypy_internal_format_size(char *buffer, size_t value) {
    char reversed[TINYPY_MESSAGE_SIZE_BUFFER];
    size_t count = 0U;
    size_t index;

    do {
        reversed[count] = (char)('0' + (char)(value % 10U));
        value /= 10U;
        count += 1U;
    } while (value != 0U);
    for (index = 0U; index < count; ++index) {
        buffer[index] = reversed[count - 1U - index];
    }
    return count;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_make_vm_error_parts(tinypy_vm_t *vm, tinypy_error_kind_e error_kind, const tinypy_message_part_t *parts, size_t part_count, tinypy_error_t **out_error) {
    size_t message_size = 0U;
    size_t offset = 0U;
    size_t index;
    char *message;

    for (index = 0U; index < part_count; ++index) {
        message_size += parts[index].size;
    }
    message = (char *)tinypy_internal_vm_allocate(vm, message_size + 1U);
    for (index = 0U; index < part_count; ++index) {
        if (parts[index].size != 0U) {
            (void)memcpy(message + offset, parts[index].bytes, parts[index].size);
            offset += parts[index].size;
        }
    }
    message[message_size] = '\0';
    tinypy_internal_make_vm_error(vm, error_kind, message, out_error);
    tinypy_internal_vm_deallocate(vm, message, message_size + 1U);
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_make_arity_error(tinypy_vm_t *vm, const char *name, size_t name_size, size_t count, size_t minimum, size_t maximum, tinypy_arity_style_e style, tinypy_error_t **out_error) {
    char expected_buffer[TINYPY_MESSAGE_SIZE_BUFFER];
    char count_buffer[TINYPY_MESSAGE_SIZE_BUFFER];
    size_t expected = count < minimum ? minimum : maximum;
    size_t expected_size = tinypy_internal_format_size(expected_buffer, expected);
    size_t count_size = tinypy_internal_format_size(count_buffer, count);

    if (style == TINYPY_ARITY_STYLE_WRAPPER) {
        char minimum_buffer[TINYPY_MESSAGE_SIZE_BUFFER];
        size_t minimum_size = tinypy_internal_format_size(minimum_buffer, minimum);
        tinypy_message_part_t parts[] = {
            TINYPY_MESSAGE_PART_LITERAL("expected "),
            {minimum_buffer, minimum_size},
            TINYPY_MESSAGE_PART_LITERAL(" or "),
            {expected_buffer, expected_size},
            TINYPY_MESSAGE_PART_LITERAL(" arguments, got "),
            {count_buffer, count_size},
        };

        if (minimum == maximum) {
            parts[2].size = 0U;
            parts[3].size = 0U;
        }
        else {
            parts[3].size = tinypy_internal_format_size(expected_buffer, maximum);
        }
        tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
        return;
    }
    if (style == TINYPY_ARITY_STYLE_SINGLE) {
        tinypy_message_part_t parts[] = {
            {name, name_size},
            TINYPY_MESSAGE_PART_LITERAL("() takes exactly one argument ("),
            {count_buffer, count_size},
            TINYPY_MESSAGE_PART_LITERAL(" given)"),
        };

        tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
        return;
    }
    if (style == TINYPY_ARITY_STYLE_UNPACK) {
        tinypy_message_part_t parts[] = {
            {name, name_size},
            TINYPY_MESSAGE_PART_LITERAL(" expected "),
            TINYPY_MESSAGE_PART_LITERAL("at least "),
            {expected_buffer, expected_size},
            TINYPY_MESSAGE_PART_LITERAL(" arguments, got "),
            {count_buffer, count_size},
        };

        if (minimum == maximum) {
            parts[2].size = 0U;
        }
        else if (count > maximum) {
            parts[2].bytes = "at most ";
            parts[2].size = 8U;
        }
        tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
        return;
    }
    if (maximum == 0U) {
        tinypy_message_part_t parts[] = {
            {name, name_size},
            TINYPY_MESSAGE_PART_LITERAL("() takes no arguments ("),
            {count_buffer, count_size},
            TINYPY_MESSAGE_PART_LITERAL(" given)"),
        };

        tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
        return;
    }
    /* A NULL name represents an anonymous PyArg_ParseTuple invocation. */
    tinypy_message_part_t parts[] = {
        {name != NULL ? name : "function", name != NULL ? name_size : 8U},
        {name != NULL ? "() takes " : " takes ", name != NULL ? 9U : 7U},
        TINYPY_MESSAGE_PART_LITERAL("exactly "),
        {expected_buffer, expected_size},
        TINYPY_MESSAGE_PART_LITERAL(" argument"),
        TINYPY_MESSAGE_PART_LITERAL("s"),
        TINYPY_MESSAGE_PART_LITERAL(" ("),
        {count_buffer, count_size},
        TINYPY_MESSAGE_PART_LITERAL(" given)"),
    };

    if (minimum != maximum) {
        parts[2].bytes = count < minimum ? "at least " : "at most ";
        parts[2].size = count < minimum ? 9U : 8U;
    }
    if (expected == 1U) {
        parts[5].size = 0U;
    }
    tinypy_internal_make_vm_error_parts(vm, TINYPY_ERROR_TYPE, parts, sizeof(parts) / sizeof(parts[0]), out_error);
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
    case TINYPY_ERROR_UNICODE_DECODE:
        return "unicode decode error";
    case TINYPY_ERROR_UNICODE_ENCODE:
        return "unicode encode error";
    case TINYPY_ERROR_BUFFER:
        return "buffer error";
    case TINYPY_ERROR_MEMORY:
        return "memory error";
    default:
        return "unknown error kind";
    }
}
//////////////////////////////////////////////////////////////////////////
tinypy_error_kind_e tinypy_error_kind(const tinypy_error_t *error) {

    return error->kind;
}
//////////////////////////////////////////////////////////////////////////
const char *tinypy_error_message(const tinypy_error_t *error, size_t *out_size) {
    if (error->vm != NULL) {
        /* Rendering completes the message of a logically immutable error. */
        __tinypy_internal_error_render((tinypy_error_t *)error);
    }
    if (out_size != NULL) {
        *out_size = error->message_size;
    }

    return error->rendered_message != NULL ? error->rendered_message : error->data;
}
//////////////////////////////////////////////////////////////////////////
const char *tinypy_error_logical_filename(const tinypy_error_t *error, size_t *out_size) {
    if (out_size != NULL) {
        *out_size = error->filename_size;
    }
    return error->filename_size != 0U ? error->data + error->message_size + 1U : NULL;
}
//////////////////////////////////////////////////////////////////////////
const char *tinypy_error_source_line(const tinypy_error_t *error, size_t *out_size) {
    if (out_size != NULL) {
        *out_size = error->source_line_size;
    }
    return error->source_line_size != 0U ? error->data + error->message_size + 1U + error->filename_size + 1U : NULL;
}
//////////////////////////////////////////////////////////////////////////
int32_t tinypy_error_line_number(const tinypy_error_t *error) {
    return error->line_number;
}
//////////////////////////////////////////////////////////////////////////
int32_t tinypy_error_column_offset(const tinypy_error_t *error) {
    return error->column_offset;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_error_release(tinypy_error_t *error) {
    tinypy_allocator_t allocator;
    size_t allocation_size;

    if (error->vm != NULL) {
        __tinypy_internal_error_unlink(error);
        TINYPY_DECREF(error->exception);
    }
    if (error->rendered_message != NULL) {
        error->allocator.deallocate(error->allocator.user_data, error->rendered_message, error->message_size + 1U, TINYPY_INTERNAL_ALIGNMENT);
    }
    allocator = error->allocator;
    allocation_size = error->allocation_size;
    allocator.deallocate(
        allocator.user_data,
        error,
        allocation_size,
        TINYPY_INTERNAL_ALIGNMENT);
}
