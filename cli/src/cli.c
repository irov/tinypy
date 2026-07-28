/* Optional command-line host built on the public TinyPy embedding API. */
#include "tinypy/tinypy.h"
#include "tinypy_cli/cli.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#include <direct.h>
#include <io.h>
#else
#include <unistd.h>
#endif

typedef struct tinypy_cli_allocator_state_t {
    size_t current_allocations;
    size_t peak_allocations;
    size_t current_bytes;
    size_t peak_bytes;
    size_t total_allocations;
} tinypy_cli_allocator_state_t;

//////////////////////////////////////////////////////////////////////////
typedef struct tinypy_cli_allocation_header_t {
    void *base;
    size_t size;
    size_t alignment;
} tinypy_cli_allocation_header_t;

//////////////////////////////////////////////////////////////////////////
typedef struct tinypy_cli_context_t {
    tinypy_cli_allocator_state_t allocator;
    char *import_roots[2];
    size_t import_root_count;
    int32_t optimize_level;
} tinypy_cli_context_t;

//////////////////////////////////////////////////////////////////////////
typedef struct tinypy_cli_artifact_t {
    tinypy_module_artifact_t artifact;
    uint8_t *source;
    char *canonical_name;
    char *logical_filename;
} tinypy_cli_artifact_t;

//////////////////////////////////////////////////////////////////////////
typedef struct tinypy_cli_buffer_t {
    uint8_t *data;
    size_t size;
    size_t capacity;
} tinypy_cli_buffer_t;

//////////////////////////////////////////////////////////////////////////
typedef enum tinypy_cli_execute_result_e {
    TINYPY_CLI_EXECUTE_OK = 0,
    TINYPY_CLI_EXECUTE_ERROR = 1,
    TINYPY_CLI_EXECUTE_INCOMPLETE = 2,
    TINYPY_CLI_EXECUTE_NOT_EXPRESSION = 3
} tinypy_cli_execute_result_e;

//////////////////////////////////////////////////////////////////////////
static void *__tinypy_cli_allocate(void *user_data, size_t size, size_t alignment) {
    tinypy_cli_allocator_state_t *state = (tinypy_cli_allocator_state_t *)user_data;
    tinypy_cli_allocation_header_t *header;
    uint8_t *base;
    uintptr_t address;
    size_t allocation_size;

    if (alignment < sizeof(void *)) {
        alignment = sizeof(void *);
    }
    allocation_size = sizeof(*header) + size + alignment - 1U;
    base = (uint8_t *)malloc(allocation_size);
    address = ((uintptr_t)(base + sizeof(*header)) + (uintptr_t)alignment - 1U) & ~((uintptr_t)alignment - 1U);
    header = (tinypy_cli_allocation_header_t *)(address - sizeof(*header));
    header->base = base;
    header->size = size;
    header->alignment = alignment;
    state->current_allocations += 1U;
    state->total_allocations += 1U;
    state->current_bytes += size;
    if (state->current_allocations > state->peak_allocations) {
        state->peak_allocations = state->current_allocations;
    }
    if (state->current_bytes > state->peak_bytes) {
        state->peak_bytes = state->current_bytes;
    }
    return (void *)address;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_cli_deallocate(void *user_data, void *memory, size_t size, size_t alignment) {
    tinypy_cli_allocator_state_t *state = (tinypy_cli_allocator_state_t *)user_data;
    tinypy_cli_allocation_header_t *header = (tinypy_cli_allocation_header_t *)((uint8_t *)memory - sizeof(*header));

    if (alignment < sizeof(void *)) {
        alignment = sizeof(void *);
    }
    state->current_allocations -= 1U;
    state->current_bytes -= size;
    free(header->base);
}
//////////////////////////////////////////////////////////////////////////
static void *__tinypy_cli_reallocate(void *user_data, void *memory, size_t old_size, size_t new_size, size_t alignment) {
    void *resized = __tinypy_cli_allocate(user_data, new_size, alignment);

    (void)memcpy(resized, memory, old_size < new_size ? old_size : new_size);
    __tinypy_cli_deallocate(user_data, memory, old_size, alignment);
    return resized;
}
//////////////////////////////////////////////////////////////////////////
static char *__tinypy_cli_string_duplicate(const char *text, size_t size) {
    char *copy = (char *)malloc(size + 1U);

    if (size != 0U) {
        (void)memcpy(copy, text, size);
    }
    copy[size] = '\0';
    return copy;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_cli_buffer_reserve(tinypy_cli_buffer_t *buffer, size_t required) {
    size_t capacity;
    uint8_t *data;

    if (required <= buffer->capacity) {
        return TINYPY_TRUE;
    }
    capacity = buffer->capacity == 0U ? 4096U : buffer->capacity;
    while (capacity < required) {
        if (capacity > SIZE_MAX / 2U) {
            capacity = required;
            break;
        }
        capacity *= 2U;
    }
    data = (uint8_t *)realloc(buffer->data, capacity);
    if (data == NULL) {
        return TINYPY_FALSE;
    }
    buffer->data = data;
    buffer->capacity = capacity;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_cli_buffer_append(tinypy_cli_buffer_t *buffer, const void *data, size_t size) {
    if (size > SIZE_MAX - buffer->size || __tinypy_cli_buffer_reserve(buffer, buffer->size + size) == 0) {
        return TINYPY_FALSE;
    }
    if (size != 0U) {
        (void)memcpy(buffer->data + buffer->size, data, size);
    }
    buffer->size += size;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_cli_read_stream(FILE *stream, uint8_t **out_data, size_t *out_size) {
    tinypy_cli_buffer_t buffer = {NULL, 0U, 0U};
    uint8_t chunk[16384];

    for (;;) {
        size_t read_size = fread(chunk, 1U, sizeof(chunk), stream);

        if (read_size != 0U && __tinypy_cli_buffer_append(&buffer, chunk, read_size) == 0) {
            free(buffer.data);
            return TINYPY_FALSE;
        }
        if (read_size != sizeof(chunk)) {
            if (ferror(stream) != 0) {
                free(buffer.data);
                return TINYPY_FALSE;
            }
            break;
        }
    }
    if (buffer.data == NULL) {
        buffer.data = (uint8_t *)malloc(1U);
    }
    *out_data = buffer.data;
    *out_size = buffer.size;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_cli_read_file(const char *path, uint8_t **out_data, size_t *out_size) {
    FILE *stream = fopen(path, "rb");
    tinypy_bool_t result;

    if (stream == NULL) {
        return TINYPY_FALSE;
    }
    result = __tinypy_cli_read_stream(stream, out_data, out_size);
    if (fclose(stream) != 0) {
        result = INT32_C(0);
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
static char *__tinypy_cli_current_directory(void) {
#if defined(_WIN32)
    char *return_value = _getcwd(NULL, 0);
    return return_value;
#else
    char *return_value = getcwd(NULL, 0U);
    return return_value;
#endif
}
//////////////////////////////////////////////////////////////////////////
static char *__tinypy_cli_directory_name(const char *path) {
    size_t size = strlen(path);

    while (size != 0U && path[size - 1U] != '/' && path[size - 1U] != '\\') {
        size -= 1U;
    }
    if (size == 0U) {
        char *return_value_1 = __tinypy_cli_string_duplicate(".", 1U);
        return return_value_1;
    }
    while (size > 1U && (path[size - 1U] == '/' || path[size - 1U] == '\\')) {
        size -= 1U;
    }
    char *return_value_2 = __tinypy_cli_string_duplicate(path, size);
    return return_value_2;
}
//////////////////////////////////////////////////////////////////////////
static char *__tinypy_cli_module_path(const char *root, const char *name, size_t name_size, int32_t package) {
    static const char module_suffix[] = ".py";
    static const char package_suffix[] = "/__init__.py";
    const char *suffix = package != 0 ? package_suffix : module_suffix;
    size_t root_size = strlen(root);
    size_t suffix_size = strlen(suffix);
    size_t path_size;
    char *path;
    size_t index;
    size_t cursor;
    int32_t separator = root_size != 0U && root[root_size - 1U] != '/' && root[root_size - 1U] != '\\';

    if (root_size > SIZE_MAX - (size_t)separator - name_size - suffix_size) {
        return NULL;
    }
    path_size = root_size + (size_t)separator + name_size + suffix_size;
    path = (char *)malloc(path_size + 1U);
    cursor = 0U;
    if (root_size != 0U) {
        (void)memcpy(path, root, root_size);
        cursor = root_size;
    }
    if (separator != 0) {
        path[cursor++] = '/';
    }
    for (index = 0U; index < name_size; index += 1U) {
        path[cursor++] = name[index] == '.' ? '/' : name[index];
    }
    (void)memcpy(path + cursor, suffix, suffix_size);
    cursor += suffix_size;
    path[cursor] = '\0';
    return path;
}
//////////////////////////////////////////////////////////////////////////
static const tinypy_module_artifact_t *__tinypy_cli_try_module(tinypy_cli_context_t *context, const tinypy_module_request_t *request, const char *root, int32_t package) {
    tinypy_cli_artifact_t *entry;
    char *path = __tinypy_cli_module_path(root, request->canonical_name, request->canonical_name_size, package);
    uint8_t *source;
    size_t source_size;

    if (path == NULL || __tinypy_cli_read_file(path, &source, &source_size) == 0) {
        free(path);
        return NULL;
    }
    entry = (tinypy_cli_artifact_t *)calloc(1U, sizeof(*entry));
    entry->source = source;
    entry->canonical_name = __tinypy_cli_string_duplicate(request->canonical_name, request->canonical_name_size);
    entry->logical_filename = path;
    entry->artifact.abi_version = TINYPY_ABI_VERSION;
    entry->artifact.struct_size = (uint32_t)sizeof(entry->artifact);
    entry->artifact.content_kind = TINYPY_MODULE_CONTENT_SOURCE;
    entry->artifact.flags = package != 0 ? (uint32_t)TINYPY_MODULE_ARTIFACT_PACKAGE : 0U;
    entry->artifact.data = entry->source;
    entry->artifact.data_size = source_size;
    entry->artifact.canonical_name = entry->canonical_name;
    entry->artifact.canonical_name_size = request->canonical_name_size;
    entry->artifact.logical_filename = entry->logical_filename;
    entry->artifact.logical_filename_size = strlen(entry->logical_filename);
    entry->artifact.compile_feature_flags = 0U;
    entry->artifact.compile_optimize_level = context->optimize_level;
    return &entry->artifact;
}
//////////////////////////////////////////////////////////////////////////
static const tinypy_module_artifact_t *__tinypy_cli_resolve_module(void *user_data, const tinypy_module_request_t *request) {
    tinypy_cli_context_t *context = (tinypy_cli_context_t *)user_data;
    size_t index;

    for (index = 0U; index < context->import_root_count; index += 1U) {
        const tinypy_module_artifact_t *artifact = __tinypy_cli_try_module(context, request, context->import_roots[index], INT32_C(0));

        if (artifact != NULL) {
            return artifact;
        }
        artifact = __tinypy_cli_try_module(context, request, context->import_roots[index], INT32_C(1));
        if (artifact != NULL) {
            return artifact;
        }
    }
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_cli_release_module(void *user_data, const tinypy_module_artifact_t *artifact) {
    tinypy_cli_artifact_t *entry = (tinypy_cli_artifact_t *)artifact;

    (void)user_data;
    free(entry->source);
    free(entry->canonical_name);
    free(entry->logical_filename);
    free(entry);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_cli_emit_output(void *user_data, tinypy_output_channel_e channel, const void *bytes, size_t size) {
    FILE *stream = channel == TINYPY_OUTPUT_STDOUT ? stdout : stderr;

    (void)user_data;
    if (size != 0U) {
        (void)fwrite(bytes, 1U, size, stream);
    }
    (void)fflush(stream);
}
//////////////////////////////////////////////////////////////////////////
#if defined(TINYPY_CYCLE_DIAGNOSTICS)
static void __tinypy_cli_diagnostic(void *user_data, const tinypy_diagnostic_t *diagnostic) {
    (void)user_data;
    if (diagnostic == NULL) {
        return;
    }
    if (diagnostic->message_size != 0U) {
        (void)fwrite(diagnostic->message, 1U, diagnostic->message_size, stderr);
    }
    (void)fputc('\n', stderr);
    (void)fflush(stderr);
}
#endif
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_cli_message_contains(const char *message, size_t message_size, const char *needle) {
    size_t needle_size = strlen(needle);
    size_t index;

    if (needle_size > message_size) {
        return TINYPY_FALSE;
    }
    for (index = 0U; index <= message_size - needle_size; index += 1U) {
        if (memcmp(message + index, needle, needle_size) == 0) {
            return TINYPY_TRUE;
        }
    }
    return TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_cli_error_is_incomplete(const tinypy_error_t *error) {
    const char *message;
    size_t message_size;
    tinypy_error_kind_e kind = tinypy_error_kind(error);

    if (kind != TINYPY_ERROR_SYNTAX && kind != TINYPY_ERROR_INDENTATION) {
        return TINYPY_FALSE;
    }
    message = tinypy_error_message(error, &message_size);
    tinypy_bool_t return_value_1 = __tinypy_cli_message_contains(message, message_size, "unexpected EOF") != 0 || __tinypy_cli_message_contains(message, message_size, "unexpected end of file") != 0 || __tinypy_cli_message_contains(message, message_size, "EOF while scanning") != 0 || __tinypy_cli_message_contains(message, message_size, "expected an indented block") != 0;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_cli_error_has_syntax_location(tinypy_error_kind_e kind) {
    return kind == TINYPY_ERROR_SYNTAX || kind == TINYPY_ERROR_INDENTATION || kind == TINYPY_ERROR_TAB || kind == TINYPY_ERROR_SOURCE_DECODING || kind == TINYPY_ERROR_PREPROCESSOR || kind == TINYPY_ERROR_META;
}
//////////////////////////////////////////////////////////////////////////
static const char *__tinypy_cli_text_view(const tinypy_value_t *value, size_t *out_size) {
    if (value == NULL) {
        *out_size = 0U;
        return NULL;
    }
    if (tinypy_typeof(value) == TINYPY_VALUE_STRING) {
        const char *return_value_1 = (const char *)tinypy_string_view(value, out_size);
        return return_value_1;
    }
    if (tinypy_typeof(value) == TINYPY_VALUE_UNICODE) {
        size_t code_point_count;

        const char *return_value_2 = tinypy_unicode_utf8_view(value, out_size, &code_point_count);
        return return_value_2;
    }
    *out_size = 0U;
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
static const char *__tinypy_cli_error_type_name(tinypy_error_kind_e kind, size_t *out_size) {
    const char *name;

    switch (kind) {
    case TINYPY_ERROR_TYPE:
        name = "TypeError";
        break;
    case TINYPY_ERROR_NAME:
        name = "NameError";
        break;
    case TINYPY_ERROR_UNBOUND_LOCAL:
        name = "UnboundLocalError";
        break;
    case TINYPY_ERROR_INTERRUPT:
        name = "KeyboardInterrupt";
        break;
    case TINYPY_ERROR_ZERO_DIVISION:
        name = "ZeroDivisionError";
        break;
    case TINYPY_ERROR_VALUE:
        name = "ValueError";
        break;
    case TINYPY_ERROR_INDEX:
        name = "IndexError";
        break;
    case TINYPY_ERROR_KEY:
        name = "KeyError";
        break;
    case TINYPY_ERROR_OVERFLOW:
        name = "OverflowError";
        break;
    case TINYPY_ERROR_IMPORT:
        name = "ImportError";
        break;
    case TINYPY_ERROR_ATTRIBUTE:
        name = "AttributeError";
        break;
    case TINYPY_ERROR_LOOKUP:
        name = "LookupError";
        break;
    case TINYPY_ERROR_SYNTAX:
    case TINYPY_ERROR_SOURCE_DECODING:
    case TINYPY_ERROR_PREPROCESSOR:
    case TINYPY_ERROR_META:
        name = "SyntaxError";
        break;
    case TINYPY_ERROR_INDENTATION:
        name = "IndentationError";
        break;
    case TINYPY_ERROR_TAB:
        name = "TabError";
        break;
    case TINYPY_ERROR_RUNTIME:
    case TINYPY_ERROR_COMPILER_LIMIT:
    default:
        name = "RuntimeError";
        break;
    }
    *out_size = strlen(name);
    return name;
}
//////////////////////////////////////////////////////////////////////////
static const char *__tinypy_cli_exception_type_name(const tinypy_vm_t *vm, const tinypy_error_t *error, size_t *out_size) {
    tinypy_value_t *raised_type = tinypy_vm_raised_exception_type(vm);

    if (raised_type != NULL) {
        const char *return_value_1 = tinypy_type_name(tinypy_value_as_const_type(raised_type), out_size);
        return return_value_1;
    }
    const char *return_value_2 = __tinypy_cli_error_type_name(tinypy_error_kind(error), out_size);
    return return_value_2;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_cli_print_exception(const tinypy_vm_t *vm, const tinypy_error_t *error) {
    const char *message;
    const char *type_name;
    size_t message_size;
    size_t type_name_size;

    message = tinypy_error_message(error, &message_size);
    type_name = __tinypy_cli_exception_type_name(vm, error, &type_name_size);
    (void)fwrite(type_name, 1U, type_name_size, stderr);
    if (message_size != 0U) {
        (void)fputs(": ", stderr);
        (void)fwrite(message, 1U, message_size, stderr);
    }
    (void)fputc('\n', stderr);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_cli_print_syntax_error(const tinypy_vm_t *vm, const tinypy_error_t *error, const char *fallback_filename) {
    const char *filename;
    const char *source_line;
    size_t filename_size;
    size_t source_line_size;
    size_t source_begin = 0U;
    size_t source_end;
    int32_t line_number = tinypy_error_line_number(error);
    int32_t column_offset = tinypy_error_column_offset(error);

    filename = tinypy_error_logical_filename(error, &filename_size);
    source_line = tinypy_error_source_line(error, &source_line_size);
    if (filename_size == 0U) {
        filename = fallback_filename;
        filename_size = strlen(fallback_filename);
    }
    (void)fputs("  File \"", stderr);
    (void)fwrite(filename, 1U, filename_size, stderr);
    (void)fprintf(stderr, "\", line %d\n", line_number > 0 ? line_number : 1);
    if (source_line_size != 0U) {
        source_end = source_line_size;
        while (source_end != 0U && (source_line[source_end - 1U] == '\n' || source_line[source_end - 1U] == '\r')) {
            --source_end;
        }
        while (source_begin < source_end && (source_line[source_begin] == ' ' || source_line[source_begin] == '\t' || source_line[source_begin] == '\f')) {
            ++source_begin;
        }
        (void)fputs("    ", stderr);
        (void)fwrite(source_line + source_begin, 1U, source_end - source_begin, stderr);
        (void)fputc('\n', stderr);
        if (column_offset > 0) {
            size_t caret_offset = (size_t)(column_offset - 1);
            size_t index;

            if (caret_offset < source_begin) {
                caret_offset = source_begin;
            }
            if (caret_offset > source_end) {
                caret_offset = source_end;
            }
            (void)fputs("    ", stderr);
            for (index = source_begin; index < caret_offset; ++index) {
                (void)fputc(source_line[index] == '\t' ? '\t' : ' ', stderr);
            }
            (void)fputs("^\n", stderr);
        }
    }
    __tinypy_cli_print_exception(vm, error);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_cli_print_traceback(const tinypy_vm_t *vm) {
    const tinypy_value_t *traceback = tinypy_vm_raised_traceback(vm);

    if (traceback == NULL) {
        return;
    }
    (void)fputs("Traceback (most recent call last):\n", stderr);
    while (traceback != NULL) {
        tinypy_value_t *frame = tinypy_traceback_frame(traceback);
        tinypy_value_t *code = tinypy_frame_code(frame);
        const char *filename;
        const char *function_name;
        size_t filename_size;
        size_t function_name_size;

        filename = __tinypy_cli_text_view(tinypy_code_filename(code), &filename_size);
        function_name = __tinypy_cli_text_view(tinypy_code_name(code), &function_name_size);
        if (filename == NULL) {
            filename = "<unknown>";
            filename_size = sizeof("<unknown>") - 1U;
        }
        if (function_name == NULL) {
            function_name = "<unknown>";
            function_name_size = sizeof("<unknown>") - 1U;
        }
        (void)fputs("  File \"", stderr);
        (void)fwrite(filename, 1U, filename_size, stderr);
        (void)fprintf(stderr, "\", line %d, in ", tinypy_traceback_line_number(traceback));
        (void)fwrite(function_name, 1U, function_name_size, stderr);
        (void)fputc('\n', stderr);
        traceback = tinypy_traceback_next(traceback);
    }
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_cli_print_error(const tinypy_vm_t *vm, const tinypy_error_t *error, const char *fallback_filename) {
    if (__tinypy_cli_error_has_syntax_location(tinypy_error_kind(error)) != 0) {
        __tinypy_cli_print_syntax_error(vm, error, fallback_filename);
        return;
    }
    __tinypy_cli_print_traceback(vm);
    __tinypy_cli_print_exception(vm, error);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_cli_execute_result_e __tinypy_cli_execute(tinypy_vm_t *vm, tinypy_value_t *globals, const void *source, size_t source_size, const char *filename, tinypy_compile_mode_e mode, int32_t optimize_level, tinypy_bool_t allow_incomplete) {
    tinypy_compile_options_t options;
    tinypy_error_t *error = NULL;
    tinypy_value_t *result;
    size_t filename_size;

    tinypy_compile_options_init(&options, mode);
    options.optimize_level = optimize_level;
    filename_size = strlen(filename);
    result = tinypy_exec_source(vm, source, source_size, filename, filename_size, globals, NULL, &options, &error);
    if (result != NULL) {
        tinypy_release(result);
        return TINYPY_CLI_EXECUTE_OK;
    }
    if (error != NULL && allow_incomplete != 0 && __tinypy_cli_error_is_incomplete(error) != 0) {
        tinypy_error_release(error);
        tinypy_vm_clear_error(vm);
        return TINYPY_CLI_EXECUTE_INCOMPLETE;
    }
    if (error != NULL) {
        __tinypy_cli_print_error(vm, error, filename);
        tinypy_error_release(error);
    }
    else {
        (void)fprintf(stderr, "%s: execution failed\n", filename);
    }
    tinypy_vm_clear_error(vm);
    return TINYPY_CLI_EXECUTE_ERROR;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_cli_execute_result_e __tinypy_cli_execute_expression(tinypy_vm_t *vm, tinypy_value_t *globals, const void *source, size_t source_size, const char *filename, int32_t optimize_level, tinypy_bool_t allow_incomplete) {
    tinypy_compile_options_t options;
    tinypy_error_t *error = NULL;
    tinypy_value_t *result;
    size_t filename_size;

    tinypy_compile_options_init(&options, TINYPY_COMPILE_EVAL);
    options.optimize_level = optimize_level;
    filename_size = strlen(filename);
    result = tinypy_eval_source(vm, source, source_size, filename, filename_size, globals, NULL, &options, &error);
    if (result != NULL) {
        if (tinypy_typeof(result) != TINYPY_VALUE_NONE) {
            tinypy_value_t *representation = tinypy_object_repr(result, &error);

            if (representation == NULL) {
                tinypy_release(result);
                if (error != NULL) {
                    __tinypy_cli_print_error(vm, error, filename);
                    tinypy_error_release(error);
                }
                tinypy_vm_clear_error(vm);
                return TINYPY_CLI_EXECUTE_ERROR;
            }
            if (tinypy_typeof(representation) == TINYPY_VALUE_STRING) {
                size_t size;
                const void *bytes = tinypy_string_view(representation, &size);

                tinypy_output_emit(vm, TINYPY_OUTPUT_STDOUT, bytes, size);
            }
            else {
                size_t size;
                size_t code_point_count;
                const char *bytes;

                bytes = tinypy_unicode_utf8_view(representation, &size, &code_point_count);
                (void)code_point_count;
                tinypy_output_emit(vm, TINYPY_OUTPUT_STDOUT, bytes, size);
            }
            tinypy_output_emit(vm, TINYPY_OUTPUT_STDOUT, "\n", 1U);
            tinypy_release(representation);
        }
        tinypy_release(result);
        return TINYPY_CLI_EXECUTE_OK;
    }
    if (error != NULL && allow_incomplete != 0 && __tinypy_cli_error_is_incomplete(error) != 0) {
        tinypy_error_release(error);
        tinypy_vm_clear_error(vm);
        return TINYPY_CLI_EXECUTE_INCOMPLETE;
    }
    if (error != NULL && (tinypy_error_kind(error) == TINYPY_ERROR_SYNTAX || tinypy_error_kind(error) == TINYPY_ERROR_INDENTATION || tinypy_error_kind(error) == TINYPY_ERROR_TAB)) {
        tinypy_error_release(error);
        tinypy_vm_clear_error(vm);
        return TINYPY_CLI_EXECUTE_NOT_EXPRESSION;
    }
    if (error != NULL) {
        __tinypy_cli_print_error(vm, error, filename);
        tinypy_error_release(error);
    }
    else {
        (void)fprintf(stderr, "%s: execution failed\n", filename);
    }
    tinypy_vm_clear_error(vm);
    return TINYPY_CLI_EXECUTE_ERROR;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_cli_add_main_value(tinypy_vm_t *vm, tinypy_value_t *module, const char *name, const char *text) {
    tinypy_value_t *value;
    size_t name_size;
    size_t text_size;

    name_size = strlen(name);
    text_size = strlen(text);
    value = tinypy_string_from_bytes(vm, text, text_size);
    tinypy_module_add_value(module, name, name_size, value);
    tinypy_release(value);
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_cli_create_main(tinypy_vm_t *vm, const char *filename) {
    tinypy_value_t *module = tinypy_module_new(vm, "__main__", 8U);
    tinypy_value_t *key = tinypy_string_from_bytes(vm, "__main__", 8U);
    tinypy_value_t *builtins;
    tinypy_value_t *modules;

    (void)__tinypy_cli_add_main_value(vm, module, "__name__", "__main__");
    (void)__tinypy_cli_add_main_value(vm, module, "__package__", "");
    if (filename != NULL) {
        (void)__tinypy_cli_add_main_value(vm, module, "__file__", filename);
    }
    builtins = tinypy_vm_builtins(vm);
    tinypy_module_add_value(module, "__builtins__", 12U, builtins);
    modules = tinypy_vm_modules(vm);
    tinypy_dict_set(modules, key, module);
    tinypy_release(key);
    return module;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_cli_set_sys_values(tinypy_vm_t *vm, int32_t argc, const char *const *argv, tinypy_cli_context_t *context) {
    tinypy_error_t *error = NULL;
    tinypy_value_t *sys_module = tinypy_import_module(vm, "sys", 3U, NULL, NULL, 0, &error);
    tinypy_value_t **items;
    tinypy_value_t *list;
    size_t index;

    if (sys_module == NULL) {
        if (error != NULL) {
            __tinypy_cli_print_error(vm, error, "<startup>");
            tinypy_error_release(error);
        }
        return TINYPY_FALSE;
    }
    items = (tinypy_value_t **)malloc((size_t)(argc > 0 ? argc : 1) * sizeof(*items));
    for (index = 0U; index < (size_t)argc; index += 1U) {
        size_t argument_size;

        argument_size = strlen(argv[index]);
        items[index] = tinypy_string_from_bytes(vm, argv[index], argument_size);
    }
    list = tinypy_list_from_items(vm, items, (size_t)argc);
    tinypy_module_add_value(sys_module, "argv", 4U, list);
    tinypy_release(list);
    for (index = 0U; index < (size_t)argc; index += 1U) {
        tinypy_release(items[index]);
    }
    free(items);
    items = (tinypy_value_t **)malloc((context->import_root_count != 0U ? context->import_root_count : 1U) * sizeof(*items));
    for (index = 0U; index < context->import_root_count; index += 1U) {
        size_t import_root_size;

        import_root_size = strlen(context->import_roots[index]);
        items[index] = tinypy_string_from_bytes(vm, context->import_roots[index], import_root_size);
    }
    list = tinypy_list_from_items(vm, items, context->import_root_count);
    tinypy_module_add_value(sys_module, "path", 4U, list);
    tinypy_release(list);
    for (index = 0U; index < context->import_root_count; index += 1U) {
        tinypy_release(items[index]);
    }
    free(items);
    tinypy_release(sys_module);
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_cli_stdin_is_terminal(void) {
#if defined(_WIN32)
    int32_t file_descriptor;

    file_descriptor = _fileno(stdin);
    tinypy_bool_t return_value_1 = _isatty(file_descriptor) != 0 ? TINYPY_TRUE : TINYPY_FALSE;
    return return_value_1;
#else
    tinypy_bool_t return_value_2 = isatty(STDIN_FILENO) != 0 ? TINYPY_TRUE : TINYPY_FALSE;
    return return_value_2;
#endif
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_cli_repl(tinypy_vm_t *vm, tinypy_value_t *globals, int32_t optimize_level) {
    tinypy_cli_buffer_t source = {NULL, 0U, 0U};
    char line[4096];
    tinypy_bool_t result = TINYPY_TRUE;

    (void)fputs("TinyPy 0.1.0 (Python 2.7 compatible)\n", stdout);
    for (;;) {
        tinypy_cli_execute_result_e execute_result;
        size_t line_size;

        (void)fputs(source.size == 0U ? ">>> " : "... ", stdout);
        (void)fflush(stdout);
        if (fgets(line, (int32_t)sizeof(line), stdin) == NULL) {
            (void)fputc('\n', stdout);
            if (source.size != 0U && __tinypy_cli_execute(vm, globals, source.data, source.size, "<stdin>", TINYPY_COMPILE_SINGLE, optimize_level, INT32_C(0)) == TINYPY_CLI_EXECUTE_ERROR) {
                result = INT32_C(0);
            }
            break;
        }
        line_size = strlen(line);
        if (__tinypy_cli_buffer_append(&source, line, line_size) == 0) {
            (void)fputs("tinypy: input is too large\n", stderr);
            result = INT32_C(0);
            break;
        }
        if (line_size == sizeof(line) - 1U && line[line_size - 1U] != '\n') {
            continue;
        }
        execute_result = __tinypy_cli_execute_expression(vm, globals, source.data, source.size, "<stdin>", optimize_level, INT32_C(1));
        if (execute_result == TINYPY_CLI_EXECUTE_NOT_EXPRESSION) {
            execute_result = __tinypy_cli_execute(vm, globals, source.data, source.size, "<stdin>", TINYPY_COMPILE_SINGLE, optimize_level, INT32_C(1));
        }
        if (execute_result == TINYPY_CLI_EXECUTE_INCOMPLETE) {
            continue;
        }
        source.size = 0U;
    }
    free(source.data);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_cli_usage(FILE *stream) {
    (void)fputs("usage: tinypy [-O | -OO] [--stats] [-c command | script.py | -] [args]\n", stream);
    (void)fputs("       tinypy --version\n", stream);
}
//////////////////////////////////////////////////////////////////////////
int32_t tinypy_cli_run(int32_t argc, char **argv) {
    tinypy_cli_context_t context;
    tinypy_allocator_t allocator;
    tinypy_host_t host;
    tinypy_vm_config_t config;
    tinypy_vm_t *vm;
    tinypy_value_t *main_module;
    tinypy_value_t *globals;
    uint8_t *owned_source = NULL;
    const void *source = NULL;
    size_t source_size = 0U;
    const char *filename = NULL;
    const char **python_argv;
    int32_t python_argc;
    int32_t argument = 1;
    int32_t command_argument = -1;
    int32_t script_argument = -1;
    int32_t show_stats = INT32_C(0);
    tinypy_bool_t interactive = TINYPY_FALSE;
    tinypy_bool_t success = TINYPY_TRUE;
    clock_t begin;
    clock_t end;

    (void)memset(&context, 0, sizeof(context));
    while (argument < argc) {
        if (strcmp(argv[argument], "-O") == 0) {
            context.optimize_level = 1;
            argument += 1;
        }
        else if (strcmp(argv[argument], "-OO") == 0) {
            context.optimize_level = 2;
            argument += 1;
        }
        else if (strcmp(argv[argument], "--stats") == 0) {
            show_stats = INT32_C(1);
            argument += 1;
        }
        else if (strcmp(argv[argument], "--version") == 0 || strcmp(argv[argument], "-V") == 0) {
            (void)fputs("TinyPy 0.1.0 (Python 2.7 compatible)\n", stdout);
            return EXIT_SUCCESS;
        }
        else if (strcmp(argv[argument], "--help") == 0 || strcmp(argv[argument], "-h") == 0) {
            __tinypy_cli_usage(stdout);
            return EXIT_SUCCESS;
        }
        else if (strcmp(argv[argument], "-c") == 0) {
            if (argument + 1 >= argc) {
                __tinypy_cli_usage(stderr);
                return EXIT_FAILURE;
            }
            command_argument = argument + 1;
            break;
        }
        else if (strcmp(argv[argument], "--") == 0) {
            argument += 1;
            if (argument < argc) {
                script_argument = argument;
            }
            break;
        }
        else {
            script_argument = argument;
            break;
        }
    }
    if (command_argument >= 0) {
        source = argv[command_argument];
        source_size = strlen(argv[command_argument]);
        filename = "<string>";
        python_argc = argc - command_argument;
        python_argv = (const char **)malloc((size_t)python_argc * sizeof(*python_argv));
        python_argv[0] = "-c";
        for (argument = 1; argument < python_argc; argument += 1) {
            python_argv[argument] = argv[command_argument + argument];
        }
    }
    else if (script_argument >= 0) {
        filename = strcmp(argv[script_argument], "-") == 0 ? "<stdin>" : argv[script_argument];
        if (strcmp(argv[script_argument], "-") == 0) {
            success = __tinypy_cli_read_stream(stdin, &owned_source, &source_size);
        }
        else {
            success = __tinypy_cli_read_file(argv[script_argument], &owned_source, &source_size);
        }
        if (success == 0) {
            (void)fprintf(stderr, "tinypy: unable to read '%s'\n", argv[script_argument]);
            return EXIT_FAILURE;
        }
        source = owned_source;
        python_argc = argc - script_argument;
        python_argv = (const char **)(argv + script_argument);
    }
    else {
        filename = "<stdin>";
        python_argc = 1;
        python_argv = (const char **)malloc(sizeof(*python_argv));
        python_argv[0] = "";
        if (__tinypy_cli_stdin_is_terminal() != 0) {
            interactive = INT32_C(1);
        }
        else {
            success = __tinypy_cli_read_stream(stdin, &owned_source, &source_size);
            if (success == 0) {
                (void)fputs("tinypy: unable to read standard input\n", stderr);
                free(python_argv);
                return EXIT_FAILURE;
            }
            source = owned_source;
        }
    }
    context.import_roots[context.import_root_count++] = script_argument >= 0 && strcmp(argv[script_argument], "-") != 0 ? __tinypy_cli_directory_name(argv[script_argument]) : __tinypy_cli_current_directory();
    if (context.import_roots[0] == NULL) {
        (void)fputs("tinypy: unable to resolve current directory\n", stderr);
        free(owned_source);
        if (command_argument >= 0 || script_argument < 0) {
            free(python_argv);
        }
        return EXIT_FAILURE;
    }
    if (script_argument >= 0 && strcmp(context.import_roots[0], ".") != 0) {
        context.import_roots[context.import_root_count] = __tinypy_cli_current_directory();
        if (context.import_roots[context.import_root_count] != NULL && strcmp(context.import_roots[0], context.import_roots[context.import_root_count]) != 0) {
            context.import_root_count += 1U;
        }
        else {
            free(context.import_roots[context.import_root_count]);
            context.import_roots[context.import_root_count] = NULL;
        }
    }
    (void)memset(&allocator, 0, sizeof(allocator));
    allocator.abi_version = TINYPY_ABI_VERSION;
    allocator.struct_size = (uint32_t)sizeof(allocator);
    allocator.user_data = &context.allocator;
    allocator.allocate = &__tinypy_cli_allocate;
    allocator.reallocate = &__tinypy_cli_reallocate;
    allocator.deallocate = &__tinypy_cli_deallocate;
    (void)memset(&host, 0, sizeof(host));
    host.abi_version = TINYPY_ABI_VERSION;
    host.struct_size = (uint32_t)sizeof(host);
    host.user_data = &context;
    host.resolve_module = &__tinypy_cli_resolve_module;
    host.release_module_artifact = &__tinypy_cli_release_module;
    host.emit_output = &__tinypy_cli_emit_output;
#if defined(TINYPY_CYCLE_DIAGNOSTICS)
    host.diagnostic = &__tinypy_cli_diagnostic;
#endif
    (void)memset(&config, 0, sizeof(config));
    config.abi_version = TINYPY_ABI_VERSION;
    config.struct_size = (uint32_t)sizeof(config);
    config.allocator = &allocator;
    config.host = &host;
    config.optimize_level = context.optimize_level;
    begin = clock();
    vm = tinypy_vm_create(&config);
    main_module = __tinypy_cli_create_main(vm, interactive != 0 ? NULL : filename);
    globals = tinypy_module_dict(main_module);
    if (__tinypy_cli_set_sys_values(vm, python_argc, python_argv, &context) == 0) {
        success = INT32_C(0);
    }
    else if (interactive != 0) {
        success = __tinypy_cli_repl(vm, globals, context.optimize_level);
    }
    else if (__tinypy_cli_execute(vm, globals, source, source_size, filename, TINYPY_COMPILE_EXEC, context.optimize_level, INT32_C(0)) != TINYPY_CLI_EXECUTE_OK) {
        success = INT32_C(0);
    }
    tinypy_release(main_module);
    tinypy_vm_destroy(vm);
    end = clock();
    if (context.allocator.current_allocations != 0U || context.allocator.current_bytes != 0U) {
        (void)fprintf(stderr, "tinypy: allocator leak: %zu allocations, %zu bytes\n", context.allocator.current_allocations, context.allocator.current_bytes);
        success = INT32_C(0);
    }
    if (show_stats != 0) {
        double cpu_seconds = (double)(end - begin) / (double)CLOCKS_PER_SEC;

        (void)fprintf(stderr, "tinypy stats: cpu_seconds=%.6f peak_heap_bytes=%zu peak_allocations=%zu total_allocations=%zu outstanding_bytes=%zu outstanding_allocations=%zu\n", cpu_seconds, context.allocator.peak_bytes, context.allocator.peak_allocations, context.allocator.total_allocations, context.allocator.current_bytes, context.allocator.current_allocations);
    }
    free(context.import_roots[0]);
    free(context.import_roots[1]);
    free(owned_source);
    if (command_argument >= 0 || script_argument < 0) {
        free(python_argv);
    }
    return success != 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
