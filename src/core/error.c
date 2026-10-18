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
/* A diagnostic that keeps its exception is linked on the pending
   diagnostics, which tinypy_vm_destroy renders one step each, and any
   other error on the errors of its VM. */
static tinypy_error_t **__tinypy_internal_error_list(tinypy_error_t *error) {
    tinypy_vm_t *vm = error->vm;
    tinypy_error_t **list = error->exception != NULL ? &vm->pending_diagnostics : &vm->errors;

    return list;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_internal_error_link(tinypy_error_t *error) {
    tinypy_error_t **list = __tinypy_internal_error_list(error);

    error->previous = NULL;
    error->next = *list;
    if (*list != NULL) {
        (*list)->previous = error;
    }
    *list = error;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_internal_error_unlink(tinypy_error_t *error) {
    tinypy_error_t **list = __tinypy_internal_error_list(error);

    if (error->previous != NULL) {
        error->previous->next = error->next;
    }
    else {
        *list = error->next;
    }
    if (error->next != NULL) {
        error->next->previous = error->previous;
    }
}
//////////////////////////////////////////////////////////////////////////
/* An error is a direct allocation of its VM: it counts against the heap
   budget, with the message rendered into it, until the host releases it or
   the VM is destroyed, which detaches it. The count never refuses an error,
   so that raising never fails for want of its record. */
static inline void __tinypy_internal_make_error_location(tinypy_vm_t *vm, tinypy_error_kind_e error_kind, const char *message, const char *logical_filename, size_t filename_size, int32_t line_number, int32_t column_offset, const char *source_line, size_t source_line_size, tinypy_value_t *exception, tinypy_error_t **out_error) {
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

    tinypy_error_t *error = (tinypy_error_t *)vm->allocator.allocate(vm->allocator.user_data, allocation_size, TINYPY_INTERNAL_ALIGNMENT);

    if (error == NULL) {
        return;
    }
    error->allocator = vm->allocator;
    error->kind = error_kind;
    error->allocation_size = allocation_size;
    error->message_size = message_size;
    error->filename_size = filename_size;
    error->source_line_size = source_line_size;
    error->line_number = line_number;
    error->column_offset = column_offset;
    error->vm = vm;
    error->exception = exception != NULL ? TINYPY_RET(exception) : NULL;
    error->exception_type = NULL;
    error->rendered_message = NULL;
    error->rendering = TINYPY_FALSE;
    error->released = TINYPY_FALSE;

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
    __tinypy_internal_error_link(error);
    vm->allocated_bytes += allocation_size;
    *out_error = error;
}
//////////////////////////////////////////////////////////////////////////
/* A SyntaxError with a location is built from (msg, (filename, lineno,
   offset, text)), as err_input and compiler_error raise it, so its message
   is empty; PyErr_SyntaxLocation sets the location of one raised from the
   message alone. */
static void __tinypy_internal_raise_syntax_exception(tinypy_vm_t *vm, tinypy_error_kind_e error_kind, const char *message, const char *logical_filename, size_t filename_size, int32_t line_number, int32_t column_offset, const char *source_line, size_t source_line_size, tinypy_bool_t include_location) {
    tinypy_value_t *location_items[4];
    tinypy_value_t *args_items[2];

    size_t string_length = __tinypy_internal_string_length(message);
    tinypy_value_t *message_value = tinypy_string_from_bytes(vm, message, string_length);
    tinypy_value_t *filename_value = logical_filename != NULL ? tinypy_string_from_bytes(vm, logical_filename, filename_size) : TINYPY_RET_NONE(vm);
    tinypy_value_t *line_value = line_number >= 0 ? tinypy_integer_from_i64(vm, line_number) : TINYPY_RET_NONE(vm);
    tinypy_value_t *offset_value = column_offset >= 0 ? tinypy_integer_from_i64(vm, column_offset) : TINYPY_RET_NONE(vm);
    tinypy_value_t *text_value = source_line != NULL ? tinypy_string_from_bytes(vm, source_line, source_line_size) : TINYPY_RET_NONE(vm);
    location_items[0] = filename_value;
    location_items[1] = line_value;
    location_items[2] = offset_value;
    location_items[3] = text_value;
    tinypy_value_t *location = tinypy_tuple_from_items(vm, location_items, 4U);
    args_items[0] = message_value;
    args_items[1] = location;
    tinypy_value_t *args = tinypy_tuple_from_items(vm, args_items, include_location != 0 ? 2U : 1U);

    tinypy_internal_exception_raise_kind_args(vm, error_kind, args);
    tinypy_value_t *exception = vm->raised_value;
    if (include_location == 0 && exception != NULL) {
        /* The members of SyntaxError are descriptors of its type. */
        tinypy_value_t *const attribute_names[4] = {vm->internal_filename_key, vm->internal_lineno_key, vm->internal_offset_key, vm->internal_text_key};
        for (size_t index = 0U; index < 4U; ++index) {
            tinypy_error_t *attribute_error = NULL;

            (void)tinypy_object_set_attr_value(exception, attribute_names[index], location_items[index], &attribute_error);
            if (attribute_error != NULL) {
                tinypy_error_release(attribute_error);
            }
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
void tinypy_internal_make_vm_error_location(tinypy_vm_t *vm, tinypy_error_kind_e error_kind, const char *message, const char *logical_filename, size_t filename_size, int32_t line_number, int32_t column_offset, const char *source_line, size_t source_line_size, tinypy_bool_t include_location, tinypy_error_t **out_error) {
    if (error_kind == TINYPY_ERROR_SYNTAX || error_kind == TINYPY_ERROR_INDENTATION || error_kind == TINYPY_ERROR_TAB || error_kind == TINYPY_ERROR_SOURCE_DECODING) {
        __tinypy_internal_raise_syntax_exception(vm, error_kind, message, logical_filename, filename_size, line_number, column_offset, source_line, source_line_size, include_location);
    }
    else {
        tinypy_internal_exception_raise_kind(vm, error_kind, message);
    }
    __tinypy_internal_make_error_location(vm, error_kind, message, logical_filename, filename_size, line_number, column_offset, source_line, source_line_size, NULL, out_error);
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_make_vm_error(tinypy_vm_t *vm, tinypy_error_kind_e error_kind, const char *message, tinypy_error_t **out_error) {
    tinypy_internal_exception_raise_kind(vm, error_kind, message);
    __tinypy_internal_make_error_location(vm, error_kind, message, NULL, 0U, 0, 0, NULL, 0U, NULL, out_error);
}
//////////////////////////////////////////////////////////////////////////
/* Most diagnostics belong to exceptions that Python code handles, so the
   class they were raised with maps to their kind only when it is read. The
   reference to the class passes to the caller: releasing it may run code
   that releases the error. */
static tinypy_type_t *__tinypy_internal_error_resolve_kind(tinypy_error_t *error) {
    tinypy_type_t *exception_type = error->exception_type;

    if (exception_type != NULL) {
        error->exception_type = NULL;
        error->kind = tinypy_internal_exception_error_kind(error->vm, exception_type);
    }
    return exception_type;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_internal_error_free(tinypy_error_t *error) {
    tinypy_allocator_t allocator = error->allocator;
    tinypy_vm_t *vm = error->vm;

    if (vm != NULL) {
        __tinypy_internal_error_unlink(error);
        vm->allocated_bytes -= error->allocation_size;
        if (error->rendered_message != NULL) {
            vm->allocated_bytes -= error->message_size + 1U;
        }
    }
    if (error->rendered_message != NULL) {
        allocator.deallocate(allocator.user_data, error->rendered_message, error->message_size + 1U, TINYPY_INTERNAL_ALIGNMENT);
    }
    allocator.deallocate(allocator.user_data, error, error->allocation_size, TINYPY_INTERNAL_ALIGNMENT);
}
//////////////////////////////////////////////////////////////////////////
/* The message is str() of the exception up to its first NUL byte, rendered
   with the pending exception state of the VM set aside; a failed str() reads
   as PyErr_Display reports it. The error that the code it runs releases is
   freed once rendering ends; returns whether the error is still held. */
static tinypy_bool_t __tinypy_internal_error_render(tinypy_error_t *error) {
    static const char fallback_message[] = "<exception str() failed>";
    tinypy_vm_t *vm = error->vm;
    tinypy_value_t *exception = error->exception;
    tinypy_type_t *exception_type = __tinypy_internal_error_resolve_kind(error);

    __tinypy_internal_error_unlink(error);
    error->exception = NULL;
    __tinypy_internal_error_link(error);
    error->rendering = TINYPY_TRUE;
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
    char *buffer = NULL;

    if (error->released == 0) {
        buffer = (char *)error->allocator.allocate(error->allocator.user_data, message_size + 1U, TINYPY_INTERNAL_ALIGNMENT);
    }
    if (buffer != NULL) {
        if (message_size != 0U) {
            (void)memcpy(buffer, message, message_size);
        }
        buffer[message_size] = '\0';
        error->rendered_message = buffer;
        error->message_size = message_size;
        vm->allocated_bytes += message_size + 1U;
    }
    if (rendered != NULL) {
        TINYPY_DECREF(rendered);
    }
    TINYPY_DECREF(exception);
    if (exception_type != NULL) {
        TINYPY_DECREF(&exception_type->base.base);
    }
    error->rendering = TINYPY_FALSE;
    if (error->released != 0) {
        __tinypy_internal_error_free(error);
        return TINYPY_FALSE;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
/* Diagnostics of exceptions that Python code handles are released unread, so
   str() runs only for a diagnostic that is reported, as in CPython. */
void tinypy_internal_make_exception_error(tinypy_vm_t *vm, tinypy_type_t *exception_type, tinypy_value_t *exception, tinypy_error_t **out_error) {
    __tinypy_internal_make_error_location(vm, TINYPY_ERROR_RUNTIME, "", NULL, 0U, 0, 0, NULL, 0U, exception, out_error);
    if (out_error == NULL || *out_error == NULL) {
        return;
    }
    tinypy_error_t *error = *out_error;

    if (exception_type != NULL) {
        TINYPY_INCREF(&exception_type->base.base);
        error->exception_type = exception_type;
    }
    if (vm->state == TINYPY_VM_STATE_LIVE) {
        return;
    }
    if (__tinypy_internal_error_render(error) == 0) {
        *out_error = NULL;
    }
}
//////////////////////////////////////////////////////////////////////////
/* Rendering moves the diagnostic to the errors or frees it, and the code it
   runs may add diagnostics, which render in turn. */
void tinypy_internal_error_render_pending(tinypy_vm_t *vm) {
    while (vm->pending_diagnostics != NULL) {
        (void)__tinypy_internal_error_render(vm->pending_diagnostics);
    }
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_error_detach_all(tinypy_vm_t *vm) {
    for (tinypy_error_t *error = vm->errors; error != NULL; error = error->next) {
        error->vm = NULL;
    }
    vm->errors = NULL;
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
/* A part is formatted like the "%s" of a C string in PyErr_Format: it ends
   at its first NUL. */
static size_t __tinypy_internal_message_part_size(const tinypy_message_part_t *part) {
    const char *nul = part->size != 0U ? (const char *)memchr(part->bytes, 0, part->size) : NULL;
    size_t size = nul != NULL ? (size_t)(nul - part->bytes) : part->size;

    return size;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_message_parts_precision(tinypy_message_part_t *parts, size_t part_count, size_t precision) {
    size_t remaining = precision;

    for (size_t index = 0U; index < part_count; ++index) {
        parts[index].size = __tinypy_internal_message_precision(parts[index].size, remaining);
        remaining -= parts[index].size;
    }
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_make_vm_error_parts(tinypy_vm_t *vm, tinypy_error_kind_e error_kind, const tinypy_message_part_t *parts, size_t part_count, tinypy_error_t **out_error) {
    size_t message_size = 0U;
    size_t offset = 0U;
    size_t index;
    char *message;

    for (index = 0U; index < part_count; ++index) {
        message_size += __tinypy_internal_message_part_size(&parts[index]);
    }
    message = (char *)tinypy_internal_vm_allocate(vm, message_size + 1U);
    for (index = 0U; index < part_count; ++index) {
        size_t part_size = __tinypy_internal_message_part_size(&parts[index]);

        if (part_size != 0U) {
            (void)memcpy(message + offset, parts[index].bytes, part_size);
            offset += part_size;
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
    /* Resolving completes the kind of a logically immutable error. */
    tinypy_type_t *exception_type = __tinypy_internal_error_resolve_kind((tinypy_error_t *)error);
    tinypy_error_kind_e kind = error->kind;

    if (exception_type != NULL) {
        TINYPY_DECREF(&exception_type->base.base);
    }
    return kind;
}
//////////////////////////////////////////////////////////////////////////
const char *tinypy_error_message(const tinypy_error_t *error, size_t *out_size) {
    if (error->exception != NULL) {
        /* Rendering completes the message of a logically immutable error,
           unless the code it runs releases the error. */
        tinypy_bool_t held = __tinypy_internal_error_render((tinypy_error_t *)error);

        if (held == 0) {
            if (out_size != NULL) {
                *out_size = 0U;
            }
            return "";
        }
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
    if (error->rendering != 0) {
        error->released = TINYPY_TRUE;
        return;
    }
    tinypy_value_t *exception = error->exception;
    tinypy_type_t *exception_type = error->exception_type;

    __tinypy_internal_error_free(error);
    if (exception != NULL) {
        TINYPY_DECREF(exception);
    }
    if (exception_type != NULL) {
        TINYPY_DECREF(&exception_type->base.base);
    }
}
