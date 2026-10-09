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

/* PyTraceBack_LIMIT and the line buffer of tb_displayline. */
#define TINYPY_CLI_TRACEBACK_LIMIT INT64_C(1000)
#define TINYPY_CLI_LINE_BUFFER_SIZE ((size_t)2000U)

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
    uint32_t compile_flags;
} tinypy_cli_context_t;

//////////////////////////////////////////////////////////////////////////
typedef struct tinypy_cli_artifact_t {
    tinypy_module_artifact_t artifact;
    uint8_t *source;
    char *canonical_name;
    char *logical_filename;
    char *package_directory;
} tinypy_cli_artifact_t;

//////////////////////////////////////////////////////////////////////////
typedef struct tinypy_cli_buffer_t {
    uint8_t *data;
    size_t size;
    size_t capacity;
} tinypy_cli_buffer_t;

//////////////////////////////////////////////////////////////////////////
/* Writes to the sys.stderr object the way PyFile_WriteString does: the
   first failed write ends the report. */
typedef struct tinypy_cli_writer_t {
    tinypy_vm_t *vm;
    tinypy_value_t *stream;
    tinypy_bool_t failed;
} tinypy_cli_writer_t;

//////////////////////////////////////////////////////////////////////////
/* The attributes parse_syntax_error reads from a SyntaxError. */
typedef struct tinypy_cli_syntax_location_t {
    tinypy_value_t *message;
    tinypy_value_t *filename;
    tinypy_value_t *text;
    int32_t line_number;
    int32_t offset;
} tinypy_cli_syntax_location_t;

//////////////////////////////////////////////////////////////////////////
/* The exception state of PyErr_Fetch, with owned references. */
typedef struct tinypy_cli_exception_t {
    tinypy_value_t *type;
    tinypy_value_t *value;
    tinypy_value_t *traceback;
} tinypy_cli_exception_t;

//////////////////////////////////////////////////////////////////////////
typedef enum tinypy_cli_execute_result_e {
    TINYPY_CLI_EXECUTE_OK = 0,
    TINYPY_CLI_EXECUTE_ERROR = 1,
    TINYPY_CLI_EXECUTE_INCOMPLETE = 2,
    TINYPY_CLI_EXECUTE_NOT_EXPRESSION = 3,
    TINYPY_CLI_EXECUTE_EXIT = 4
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
    if (size > SIZE_MAX - sizeof(*header) - (alignment - 1U)) {
        return NULL;
    }
    allocation_size = sizeof(*header) + size + alignment - 1U;
    base = (uint8_t *)malloc(allocation_size);
    if (base == NULL) {
        return NULL;
    }
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

    (void)alignment;
    state->current_allocations -= 1U;
    state->current_bytes -= size;
    free(header->base);
}
//////////////////////////////////////////////////////////////////////////
static void *__tinypy_cli_reallocate(void *user_data, void *memory, size_t old_size, size_t new_size, size_t alignment) {
    void *resized = __tinypy_cli_allocate(user_data, new_size, alignment);

    if (resized == NULL) {
        return NULL;
    }
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
        if (result != 0) {
            free(*out_data);
            *out_data = NULL;
            *out_size = 0U;
        }
        result = TINYPY_FALSE;
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
/* PySys_SetArgv resolves the script path before taking its directory, so
   sys.path[0] is absolute; a path that cannot be resolved is used as given. */
static char *__tinypy_cli_resolved_path(const char *path) {
#if defined(_WIN32)
    char *resolved = _fullpath(NULL, path, 0U);
#else
    char *resolved = realpath(path, NULL);
#endif
    if (resolved == NULL) {
        size_t size = strlen(path);
        char *return_value_1 = __tinypy_cli_string_duplicate(path, size);
        return return_value_1;
    }
    return resolved;
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
    if (package != 0) {
        size_t directory_size = strlen(path) - (sizeof("/__init__.py") - 1U);

        entry->package_directory = __tinypy_cli_string_duplicate(path, directory_size);
        entry->artifact.package_token = entry->package_directory;
        entry->artifact.package_token_size = directory_size;
    }
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
    free(entry->package_directory);
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
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_cli_report_cycles(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = (tinypy_vm_t *)user_data;

    (void)function;
    (void)out_error;
    if (tinypy_tuple_size(args) != 0U || (kwargs != NULL && tinypy_dict_size(kwargs) != 0U)) {
        tinypy_vm_raise_error(vm, TINYPY_ERROR_TYPE, "cycle reporter takes no arguments");
        return NULL;
    }
    size_t count = tinypy_vm_report_cycles(vm, __tinypy_cli_diagnostic, NULL);
    tinypy_value_t *result = tinypy_integer_from_i64(vm, (int64_t)count);

    return result;
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
/* PyString_AsString: a byte string as is, a unicode string in the ASCII
   default encoding. */
static const char *__tinypy_cli_c_string(const tinypy_value_t *value, size_t *out_size) {
    const char *text = __tinypy_cli_text_view(value, out_size);

    if (text == NULL || tinypy_typeof(value) == TINYPY_VALUE_STRING) {
        return text;
    }
    for (size_t index = 0U; index < *out_size; ++index) {
        if ((uint8_t)text[index] >= 0x80U) {
            *out_size = 0U;
            return NULL;
        }
    }
    return text;
}
//////////////////////////////////////////////////////////////////////////
/* PyInt_AsLong: ints, bools and longs within the C range. */
static tinypy_bool_t __tinypy_cli_as_integer(const tinypy_value_t *value, int64_t *out_value) {
    tinypy_value_type_e kind = tinypy_typeof(value);

    if (kind == TINYPY_VALUE_INTEGER || kind == TINYPY_VALUE_BOOL) {
        *out_value = tinypy_integer_as_i64(value);
        return TINYPY_TRUE;
    }
    if (kind != TINYPY_VALUE_LONG) {
        return TINYPY_FALSE;
    }
    int32_t sign;
    size_t digit_count;
    const uint16_t *digits = tinypy_long_base15_view(value, &sign, &digit_count);
    uint64_t magnitude = 0U;
    uint64_t limit = sign < 0 ? (uint64_t)INT64_MAX + 1U : (uint64_t)INT64_MAX;

    while (digit_count != 0U) {
        uint64_t digit = digits[digit_count - 1U];

        if (magnitude > (limit - digit) / 32768U) {
            return TINYPY_FALSE;
        }
        magnitude = magnitude * 32768U + digit;
        digit_count -= 1U;
    }
    *out_value = sign < 0 ? (int64_t)(0U - magnitude) : (int64_t)magnitude;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_cli_module(tinypy_vm_t *vm, const char *name, size_t name_size) {
    tinypy_value_t *modules = tinypy_vm_modules(vm);
    tinypy_value_t *key = tinypy_string_from_bytes(vm, name, name_size);
    tinypy_value_t *module = tinypy_dict_get_optional(modules, key);

    tinypy_release(key);
    if (module == NULL || tinypy_typeof(module) != TINYPY_VALUE_MODULE) {
        return NULL;
    }
    return module;
}
//////////////////////////////////////////////////////////////////////////
/* PySys_GetObject: a borrowed sys attribute or NULL. */
static tinypy_value_t *__tinypy_cli_sys_value(tinypy_vm_t *vm, const char *name, size_t name_size) {
    tinypy_value_t *sys_module = __tinypy_cli_module(vm, "sys", 3U);

    if (sys_module == NULL) {
        return NULL;
    }
    tinypy_value_t *value = tinypy_module_get_value(sys_module, name, name_size);
    return value;
}
//////////////////////////////////////////////////////////////////////////
/* The sys.stderr object when one is set; None counts as lost. */
static tinypy_value_t *__tinypy_cli_stderr_object(tinypy_vm_t *vm) {
    tinypy_value_t *stream = __tinypy_cli_sys_value(vm, "stderr", 6U);

    if (stream == NULL || tinypy_typeof(stream) == TINYPY_VALUE_NONE) {
        return NULL;
    }
    return stream;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_cli_discard_error(tinypy_vm_t *vm, tinypy_error_t *error) {
    if (error != NULL) {
        tinypy_error_release(error);
    }
    tinypy_vm_clear_error(vm);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_cli_stream_write(tinypy_vm_t *vm, tinypy_value_t *stream, const void *bytes, size_t size) {
    tinypy_error_t *error = NULL;
    tinypy_value_t *method = tinypy_object_get_attr(stream, "write", 5U, &error);

    if (method == NULL) {
        __tinypy_cli_discard_error(vm, error);
        return TINYPY_FALSE;
    }
    tinypy_value_t *text = tinypy_string_from_bytes(vm, bytes, size);
    tinypy_value_t *args = tinypy_tuple_from_items(vm, &text, 1U);
    tinypy_value_t *result = tinypy_call(method, args, NULL, &error);

    tinypy_release(args);
    tinypy_release(text);
    tinypy_release(method);
    if (result == NULL) {
        __tinypy_cli_discard_error(vm, error);
        return TINYPY_FALSE;
    }
    tinypy_release(result);
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_cli_write(tinypy_cli_writer_t *writer, const void *bytes, size_t size) {
    if (writer->failed != 0) {
        return;
    }
    if (__tinypy_cli_stream_write(writer->vm, writer->stream, bytes, size) == 0) {
        writer->failed = TINYPY_TRUE;
    }
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_cli_write_text(tinypy_cli_writer_t *writer, const char *text) {
    size_t size = strlen(text);

    __tinypy_cli_write(writer, text, size);
}
//////////////////////////////////////////////////////////////////////////
/* PySys_WriteStderr: through the sys.stderr object when the program set
   one, to the process stderr when sys.stderr is missing, nowhere when it is
   None; a failed write is ignored. */
static void __tinypy_cli_write_stderr(tinypy_vm_t *vm, const char *text) {
    tinypy_value_t *stream = __tinypy_cli_sys_value(vm, "stderr", 6U);

    if (stream == NULL) {
        (void)fputs(text, stderr);
        (void)fflush(stderr);
        return;
    }
    if (tinypy_typeof(stream) == TINYPY_VALUE_NONE) {
        return;
    }
    tinypy_retain(stream);
    size_t size = strlen(text);
    (void)__tinypy_cli_stream_write(vm, stream, text, size);
    tinypy_release(stream);
}
//////////////////////////////////////////////////////////////////////////
/* Opens a source file by name, then by its tail under each sys.path entry,
   as tb_displayline does. */
static tinypy_bool_t __tinypy_cli_open_source(tinypy_vm_t *vm, const char *filename, size_t filename_size, uint8_t **out_data, size_t *out_size) {
    char *path = __tinypy_cli_string_duplicate(filename, filename_size);
    tinypy_bool_t opened = __tinypy_cli_read_file(path, out_data, out_size);

    free(path);
    if (opened != 0) {
        return TINYPY_TRUE;
    }
    const char *tail = filename;
    for (size_t index = 0U; index < filename_size; ++index) {
        if (filename[index] == '/') {
            tail = filename + index + 1U;
        }
    }
    size_t tail_size = filename_size - (size_t)(tail - filename);
    tinypy_value_t *sys_path = __tinypy_cli_sys_value(vm, "path", 4U);
    if (sys_path == NULL || tinypy_typeof(sys_path) != TINYPY_VALUE_LIST) {
        return TINYPY_FALSE;
    }
    size_t count = tinypy_list_size(sys_path);
    for (size_t index = 0U; index < count; ++index) {
        tinypy_value_t *entry = tinypy_list_get(sys_path, index);
        size_t entry_size;

        if (tinypy_typeof(entry) != TINYPY_VALUE_STRING) {
            continue;
        }
        const char *entry_text = (const char *)tinypy_string_view(entry, &entry_size);
        if (strlen(entry_text) != entry_size) {
            continue;
        }
        path = (char *)malloc(entry_size + tail_size + 2U);
        (void)memcpy(path, entry_text, entry_size);
        size_t cursor = entry_size;
        if (entry_size != 0U && entry_text[entry_size - 1U] != '/') {
            path[cursor++] = '/';
        }
        (void)memcpy(path + cursor, tail, tail_size);
        path[cursor + tail_size] = '\0';
        opened = __tinypy_cli_read_file(path, out_data, out_size);
        free(path);
        if (opened != 0) {
            return TINYPY_TRUE;
        }
    }
    return TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
/* _Py_DisplaySourceLine: a line of a file read with universal newlines,
   without its indentation, behind indent spaces. */
static void __tinypy_cli_write_source_line(tinypy_cli_writer_t *writer, const char *filename, size_t filename_size, int32_t line_number, const char *indent) {
    uint8_t *data;
    size_t size;
    size_t position = 0U;
    int32_t line = 1;

    if (line_number <= 0) {
        return;
    }
    if (__tinypy_cli_open_source(writer->vm, filename, filename_size, &data, &size) == 0) {
        return;
    }
    while (position < size && line < line_number) {
        if (data[position] == (uint8_t)'\n') {
            line += 1;
        }
        else if (data[position] == (uint8_t)'\r') {
            line += 1;
            if (position + 1U < size && data[position + 1U] == (uint8_t)'\n') {
                position += 1U;
            }
        }
        position += 1U;
    }
    if (line == line_number && position < size) {
        size_t end = position;
        char *text;

        while (end < size && data[end] != (uint8_t)'\n' && data[end] != (uint8_t)'\r') {
            end += 1U;
        }
        while (position < end && (data[position] == (uint8_t)' ' || data[position] == (uint8_t)'\t' || data[position] == (uint8_t)'\f')) {
            position += 1U;
        }
        text = (char *)malloc(end - position + 2U);
        (void)memcpy(text, data + position, end - position);
        text[end - position] = '\n';
        text[end - position + 1U] = '\0';
        __tinypy_cli_write_text(writer, indent);
        /* The line is written with its own newline, or followed by one. */
        if (end < size) {
            __tinypy_cli_write(writer, text, end - position + 1U);
        }
        else {
            __tinypy_cli_write(writer, text, end - position);
            __tinypy_cli_write_text(writer, "\n");
        }
        free(text);
    }
    free(data);
}
//////////////////////////////////////////////////////////////////////////
/* PyTraceBack_Print: the innermost sys.tracebacklimit frames. */
static void __tinypy_cli_write_traceback(tinypy_cli_writer_t *writer, tinypy_value_t *traceback) {
    tinypy_vm_t *vm = writer->vm;
    int64_t limit = TINYPY_CLI_TRACEBACK_LIMIT;
    int64_t depth = 0;
    tinypy_value_t *limit_value = __tinypy_cli_sys_value(vm, "tracebacklimit", 14U);
    tinypy_value_t *cursor;

    if (limit_value != NULL && (tinypy_typeof(limit_value) == TINYPY_VALUE_INTEGER || tinypy_typeof(limit_value) == TINYPY_VALUE_BOOL)) {
        limit = tinypy_integer_as_i64(limit_value);
        if (limit <= 0) {
            return;
        }
    }
    for (cursor = traceback; cursor != NULL; cursor = tinypy_traceback_next(cursor)) {
        depth += 1;
    }
    __tinypy_cli_write_text(writer, "Traceback (most recent call last):\n");
    for (cursor = traceback; cursor != NULL && writer->failed == 0; cursor = tinypy_traceback_next(cursor)) {
        if (depth <= limit) {
            tinypy_value_t *frame = tinypy_traceback_frame(cursor);
            tinypy_value_t *code = tinypy_frame_code(frame);
            int32_t line_number = tinypy_traceback_line_number(cursor);
            size_t filename_size;
            size_t name_size;
            const char *filename = __tinypy_cli_text_view(tinypy_code_filename(code), &filename_size);
            const char *name = __tinypy_cli_text_view(tinypy_code_name(code), &name_size);
            char line_buffer[TINYPY_CLI_LINE_BUFFER_SIZE];

            if (filename == NULL || name == NULL) {
                writer->failed = TINYPY_TRUE;
                break;
            }
            (void)snprintf(line_buffer, sizeof(line_buffer), "  File \"%.500s\", line %d, in %.500s\n", filename, (int)line_number, name);
            __tinypy_cli_write_text(writer, line_buffer);
            __tinypy_cli_write_source_line(writer, filename, filename_size, line_number, "    ");
        }
        depth -= 1;
    }
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_cli_release_syntax_location(tinypy_cli_syntax_location_t *location) {
    if (location->message != NULL) {
        tinypy_release(location->message);
    }
    if (location->filename != NULL) {
        tinypy_release(location->filename);
    }
    if (location->text != NULL) {
        tinypy_release(location->text);
    }
    (void)memset(location, 0, sizeof(*location));
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_cli_attribute_integer(tinypy_vm_t *vm, tinypy_value_t *value, const char *name, size_t name_size, int32_t *out_value, tinypy_bool_t none_allowed) {
    tinypy_error_t *error = NULL;
    tinypy_value_t *attribute = tinypy_object_get_attr(value, name, name_size, &error);
    int64_t number;

    if (attribute == NULL) {
        __tinypy_cli_discard_error(vm, error);
        return TINYPY_FALSE;
    }
    if (none_allowed != 0 && tinypy_typeof(attribute) == TINYPY_VALUE_NONE) {
        tinypy_release(attribute);
        *out_value = -1;
        return TINYPY_TRUE;
    }
    tinypy_bool_t converted = __tinypy_cli_as_integer(attribute, &number);
    tinypy_release(attribute);
    if (converted == 0) {
        return TINYPY_FALSE;
    }
    *out_value = (int32_t)number;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_cli_attribute_text(tinypy_vm_t *vm, tinypy_value_t *value, const char *name, size_t name_size, tinypy_value_t **out_value) {
    tinypy_error_t *error = NULL;
    tinypy_value_t *attribute = tinypy_object_get_attr(value, name, name_size, &error);
    size_t size;

    *out_value = NULL;
    if (attribute == NULL) {
        __tinypy_cli_discard_error(vm, error);
        return TINYPY_FALSE;
    }
    if (tinypy_typeof(attribute) == TINYPY_VALUE_NONE) {
        tinypy_release(attribute);
        return TINYPY_TRUE;
    }
    if (__tinypy_cli_c_string(attribute, &size) == NULL) {
        tinypy_release(attribute);
        return TINYPY_FALSE;
    }
    *out_value = attribute;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
/* parse_syntax_error: the location attributes of a SyntaxError, which any
   unreadable attribute cancels. */
static tinypy_bool_t __tinypy_cli_syntax_location(tinypy_vm_t *vm, tinypy_value_t *value, tinypy_cli_syntax_location_t *location) {
    tinypy_error_t *error = NULL;

    (void)memset(location, 0, sizeof(*location));
    location->message = tinypy_object_get_attr(value, "msg", 3U, &error);
    if (location->message == NULL) {
        __tinypy_cli_discard_error(vm, error);
        return TINYPY_FALSE;
    }
    if (__tinypy_cli_attribute_text(vm, value, "filename", 8U, &location->filename) == 0) {
        __tinypy_cli_release_syntax_location(location);
        return TINYPY_FALSE;
    }
    if (__tinypy_cli_attribute_integer(vm, value, "lineno", 6U, &location->line_number, TINYPY_FALSE) == 0) {
        __tinypy_cli_release_syntax_location(location);
        return TINYPY_FALSE;
    }
    if (__tinypy_cli_attribute_integer(vm, value, "offset", 6U, &location->offset, TINYPY_TRUE) == 0) {
        __tinypy_cli_release_syntax_location(location);
        return TINYPY_FALSE;
    }
    if (__tinypy_cli_attribute_text(vm, value, "text", 4U, &location->text) == 0) {
        __tinypy_cli_release_syntax_location(location);
        return TINYPY_FALSE;
    }
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
/* print_error_text: the line of the error without its indentation and a
   caret under the offset, which walks to its line of a multi-line text. */
static void __tinypy_cli_write_error_text(tinypy_cli_writer_t *writer, int32_t offset, const char *text, size_t text_size) {
    const char *terminator = (const char *)memchr(text, '\0', text_size);
    const char *end = terminator != NULL ? terminator : text + text_size;

    if (offset >= 0) {
        if (offset > 0 && (size_t)offset == (size_t)(end - text) && text[offset - 1] == '\n') {
            offset -= 1;
        }
        for (;;) {
            const char *newline = (const char *)memchr(text, '\n', (size_t)(end - text));

            if (newline == NULL || newline - text >= offset) {
                break;
            }
            offset -= (int32_t)(newline + 1 - text);
            text = newline + 1;
        }
        while (text != end && (*text == ' ' || *text == '\t')) {
            text += 1;
            offset -= 1;
        }
    }
    __tinypy_cli_write_text(writer, "    ");
    __tinypy_cli_write(writer, text, (size_t)(end - text));
    if (text == end || end[-1] != '\n') {
        __tinypy_cli_write_text(writer, "\n");
    }
    if (offset == -1) {
        return;
    }
    __tinypy_cli_write_text(writer, "    ");
    offset -= 1;
    while (offset > 0) {
        __tinypy_cli_write_text(writer, " ");
        offset -= 1;
    }
    __tinypy_cli_write_text(writer, "^\n");
}
//////////////////////////////////////////////////////////////////////////
/* PyExceptionClass_Name: the part of a type name after its last dot. */
static const char *__tinypy_cli_exception_type_name(tinypy_value_t *type, size_t *out_size) {
    if (tinypy_typeof(type) == TINYPY_VALUE_CLASS) {
        tinypy_value_t *name = tinypy_class_name(type);
        const char *return_value_1 = (const char *)tinypy_string_view(name, out_size);
        return return_value_1;
    }
    const char *type_name = tinypy_type_name(tinypy_value_as_const_type(type), out_size);
    size_t offset = 0U;

    for (size_t index = 0U; index < *out_size; ++index) {
        if (type_name[index] == '.') {
            offset = index + 1U;
        }
    }
    *out_size -= offset;
    return type_name + offset;
}
//////////////////////////////////////////////////////////////////////////
/* PyErr_Display writes the __module__ of the type before its name unless
   the type belongs to the exceptions module. */
static void __tinypy_cli_write_exception_type(tinypy_cli_writer_t *writer, tinypy_value_t *type) {
    tinypy_vm_t *vm = writer->vm;
    tinypy_error_t *error = NULL;
    tinypy_value_t *module = tinypy_object_get_attr(type, "__module__", 10U, &error);
    size_t module_size;
    size_t name_size;

    if (module == NULL) {
        __tinypy_cli_discard_error(vm, error);
        __tinypy_cli_write_text(writer, "<unknown>");
    }
    else {
        const char *module_name = __tinypy_cli_c_string(module, &module_size);

        if (module_name != NULL && (module_size != sizeof("exceptions") - 1U || memcmp(module_name, "exceptions", module_size) != 0)) {
            __tinypy_cli_write(writer, module_name, module_size);
            __tinypy_cli_write_text(writer, ".");
        }
        tinypy_release(module);
    }
    const char *name = __tinypy_cli_exception_type_name(type, &name_size);
    __tinypy_cli_write(writer, name, name_size);
}
//////////////////////////////////////////////////////////////////////////
/* PyErr_Display: the traceback, the location of a SyntaxError, then the
   type and str() of the exception, all through sys.stderr. */
static void __tinypy_cli_display(tinypy_vm_t *vm, tinypy_value_t *type, tinypy_value_t *value, tinypy_value_t *traceback) {
    tinypy_value_t *stream = __tinypy_cli_stderr_object(vm);
    tinypy_cli_writer_t writer;

    if (stream == NULL) {
        (void)fputs("lost sys.stderr\n", stderr);
        (void)fflush(stderr);
        return;
    }
    tinypy_retain(stream);
    tinypy_retain(value);
    writer.vm = vm;
    writer.stream = stream;
    writer.failed = TINYPY_FALSE;
    tinypy_output_flush_line(vm);
    (void)fflush(stdout);
    if (traceback != NULL && tinypy_typeof(traceback) != TINYPY_VALUE_NONE) {
        __tinypy_cli_write_traceback(&writer, traceback);
    }
    if (writer.failed == 0 && tinypy_object_has_attr(value, "print_file_and_line", 19U) != 0) {
        tinypy_cli_syntax_location_t location;

        if (__tinypy_cli_syntax_location(vm, value, &location) != 0) {
            char line_buffer[16];
            size_t filename_size;
            size_t text_size;
            const char *filename = location.filename != NULL ? __tinypy_cli_c_string(location.filename, &filename_size) : NULL;
            const char *text = location.text != NULL ? __tinypy_cli_c_string(location.text, &text_size) : NULL;

            __tinypy_cli_write_text(&writer, "  File \"");
            if (filename == NULL) {
                __tinypy_cli_write_text(&writer, "<string>");
            }
            else {
                __tinypy_cli_write(&writer, filename, filename_size);
            }
            __tinypy_cli_write_text(&writer, "\", line ");
            (void)snprintf(line_buffer, sizeof(line_buffer), "%d", (int)location.line_number);
            __tinypy_cli_write_text(&writer, line_buffer);
            __tinypy_cli_write_text(&writer, "\n");
            if (text != NULL) {
                __tinypy_cli_write_error_text(&writer, location.offset, text, text_size);
            }
            tinypy_release(value);
            value = location.message;
            location.message = NULL;
            __tinypy_cli_release_syntax_location(&location);
        }
    }
    if (writer.failed == 0) {
        __tinypy_cli_write_exception_type(&writer, type);
    }
    if (writer.failed == 0 && tinypy_typeof(value) != TINYPY_VALUE_NONE) {
        tinypy_error_t *error = NULL;
        tinypy_value_t *rendered = tinypy_object_str(value, &error);

        if (rendered == NULL) {
            __tinypy_cli_discard_error(vm, error);
            __tinypy_cli_write_text(&writer, ": <exception str() failed>");
        }
        else {
            size_t rendered_size;
            const char *rendered_text = __tinypy_cli_text_view(rendered, &rendered_size);

            if (rendered_text != NULL && (tinypy_typeof(rendered) != TINYPY_VALUE_STRING || rendered_size != 0U)) {
                __tinypy_cli_write_text(&writer, ": ");
            }
            if (rendered_text != NULL) {
                __tinypy_cli_write(&writer, rendered_text, rendered_size);
            }
            tinypy_release(rendered);
        }
    }
    __tinypy_cli_write_text(&writer, "\n");
    tinypy_release(value);
    tinypy_release(stream);
}
//////////////////////////////////////////////////////////////////////////
/* The sys.excepthook the host installs: PyErr_Display of its arguments. */
static tinypy_value_t *__tinypy_cli_excepthook(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = (tinypy_vm_t *)user_data;

    (void)function;
    (void)out_error;
    if (kwargs != NULL && tinypy_dict_size(kwargs) != 0U) {
        tinypy_vm_raise_error(vm, TINYPY_ERROR_TYPE, "excepthook() takes no keyword arguments");
        return NULL;
    }
    if (tinypy_tuple_size(args) != 3U) {
        tinypy_vm_raise_error(vm, TINYPY_ERROR_TYPE, "excepthook expected 3 arguments");
        return NULL;
    }
    __tinypy_cli_display(vm, tinypy_tuple_get(args, 0U), tinypy_tuple_get(args, 1U), tinypy_tuple_get(args, 2U));
    tinypy_value_t *result = tinypy_none_get(vm);
    return result;
}
//////////////////////////////////////////////////////////////////////////
/* PyErr_Fetch: takes the raised exception out of the VM. */
static void __tinypy_cli_fetch_exception(tinypy_vm_t *vm, tinypy_cli_exception_t *exception) {
    exception->type = tinypy_vm_raised_exception_type(vm);
    exception->value = tinypy_vm_raised_exception(vm);
    exception->traceback = tinypy_vm_raised_traceback(vm);
    if (exception->type != NULL) {
        tinypy_retain(exception->type);
    }
    if (exception->value != NULL) {
        tinypy_retain(exception->value);
    }
    if (exception->traceback != NULL) {
        tinypy_retain(exception->traceback);
    }
    tinypy_vm_clear_error(vm);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_cli_release_exception(tinypy_cli_exception_t *exception) {
    if (exception->type != NULL) {
        tinypy_release(exception->type);
    }
    if (exception->value != NULL) {
        tinypy_release(exception->value);
    }
    if (exception->traceback != NULL) {
        tinypy_release(exception->traceback);
    }
    (void)memset(exception, 0, sizeof(*exception));
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_cli_raised_system_exit(tinypy_vm_t *vm) {
    tinypy_value_t *exception = tinypy_vm_raised_exception(vm);
    tinypy_value_t *builtins = tinypy_vm_builtins(vm);

    if (exception == NULL || builtins == NULL) {
        return TINYPY_FALSE;
    }
    tinypy_value_t *key = tinypy_string_from_bytes(vm, "SystemExit", 10U);
    tinypy_value_t *system_exit = tinypy_dict_get_optional(builtins, key);
    tinypy_release(key);
    if (system_exit == NULL) {
        return TINYPY_FALSE;
    }
    tinypy_bool_t matches = tinypy_exception_matches(exception, system_exit, NULL) > 0 ? TINYPY_TRUE : TINYPY_FALSE;
    return matches;
}
//////////////////////////////////////////////////////////////////////////
/* handle_system_exit: the status of the code attribute, an int or a long,
   or 1 after writing any other code to sys.stderr. */
static int32_t __tinypy_cli_system_exit_status(tinypy_vm_t *vm) {
    tinypy_cli_exception_t exception;
    int32_t status = 0;

    __tinypy_cli_fetch_exception(vm, &exception);
    tinypy_output_flush_line(vm);
    (void)fflush(stdout);
    tinypy_value_t *value = exception.value;
    if (value == NULL || tinypy_typeof(value) == TINYPY_VALUE_NONE) {
        __tinypy_cli_release_exception(&exception);
        return 0;
    }
    tinypy_retain(value);
    tinypy_error_t *error = NULL;
    tinypy_value_t *code = tinypy_object_get_attr(value, "code", 4U, &error);
    if (code != NULL) {
        tinypy_release(value);
        value = code;
    }
    else {
        /* The exception itself reads as the code when it has none. */
        __tinypy_cli_discard_error(vm, error);
    }
    int64_t number;
    if (tinypy_typeof(value) == TINYPY_VALUE_NONE) {
        status = 0;
    }
    else if (tinypy_typeof(value) == TINYPY_VALUE_INTEGER || tinypy_typeof(value) == TINYPY_VALUE_BOOL || tinypy_typeof(value) == TINYPY_VALUE_LONG) {
        /* (int)PyInt_AsLong: a long outside the C range reads as -1. */
        status = __tinypy_cli_as_integer(value, &number) != 0 ? (int32_t)(uint32_t)(uint64_t)number : -1;
    }
    else {
        tinypy_value_t *stream = __tinypy_cli_sys_value(vm, "stderr", 6U);
        tinypy_value_t *rendered = tinypy_object_str(value, &error);
        size_t rendered_size;
        const char *rendered_text = rendered != NULL ? __tinypy_cli_text_view(rendered, &rendered_size) : NULL;

        if (rendered == NULL) {
            __tinypy_cli_discard_error(vm, error);
        }
        if (stream != NULL && tinypy_typeof(stream) != TINYPY_VALUE_NONE) {
            if (rendered_text != NULL) {
                tinypy_retain(stream);
                (void)__tinypy_cli_stream_write(vm, stream, rendered_text, rendered_size);
                tinypy_release(stream);
            }
        }
        else if (rendered_text != NULL) {
            (void)fwrite(rendered_text, 1U, rendered_size, stderr);
            (void)fflush(stderr);
        }
        if (rendered != NULL) {
            tinypy_release(rendered);
        }
        __tinypy_cli_write_stderr(vm, "\n");
        status = 1;
    }
    tinypy_release(value);
    __tinypy_cli_release_exception(&exception);
    return status;
}
//////////////////////////////////////////////////////////////////////////
/* PyErr_PrintEx: an uncaught SystemExit ends the program with its status;
   any other exception goes to sys.excepthook, whose own failure or absence
   PyErr_Display reports. Returns whether the program must exit. */
static tinypy_bool_t __tinypy_cli_report_exception(tinypy_vm_t *vm, int32_t *out_status) {
    tinypy_cli_exception_t exception;

    if (__tinypy_cli_raised_system_exit(vm) != 0) {
        *out_status = __tinypy_cli_system_exit_status(vm);
        return TINYPY_TRUE;
    }
    __tinypy_cli_fetch_exception(vm, &exception);
    if (exception.type == NULL || exception.value == NULL) {
        __tinypy_cli_release_exception(&exception);
        return TINYPY_FALSE;
    }
    tinypy_value_t *hook = __tinypy_cli_sys_value(vm, "excepthook", 10U);
    if (hook == NULL || tinypy_typeof(hook) == TINYPY_VALUE_NONE) {
        __tinypy_cli_write_stderr(vm, "sys.excepthook is missing\n");
        __tinypy_cli_display(vm, exception.type, exception.value, exception.traceback);
        __tinypy_cli_release_exception(&exception);
        return TINYPY_FALSE;
    }
    tinypy_retain(hook);
    tinypy_value_t *items[3];
    items[0] = exception.type;
    items[1] = exception.value;
    items[2] = exception.traceback != NULL ? exception.traceback : tinypy_none_get(vm);
    tinypy_value_t *args = tinypy_tuple_from_items(vm, items, 3U);
    if (exception.traceback == NULL) {
        tinypy_release(items[2]);
    }
    tinypy_error_t *error = NULL;
    tinypy_value_t *result = tinypy_call(hook, args, NULL, &error);
    tinypy_release(args);
    tinypy_release(hook);
    if (error != NULL) {
        tinypy_error_release(error);
    }
    if (result != NULL) {
        tinypy_release(result);
        __tinypy_cli_release_exception(&exception);
        return TINYPY_FALSE;
    }
    if (__tinypy_cli_raised_system_exit(vm) != 0) {
        *out_status = __tinypy_cli_system_exit_status(vm);
        __tinypy_cli_release_exception(&exception);
        return TINYPY_TRUE;
    }
    tinypy_cli_exception_t hook_exception;
    __tinypy_cli_fetch_exception(vm, &hook_exception);
    tinypy_output_flush_line(vm);
    (void)fflush(stdout);
    __tinypy_cli_write_stderr(vm, "Error in sys.excepthook:\n");
    if (hook_exception.type != NULL && hook_exception.value != NULL) {
        __tinypy_cli_display(vm, hook_exception.type, hook_exception.value, hook_exception.traceback);
    }
    __tinypy_cli_write_stderr(vm, "\nOriginal exception was:\n");
    __tinypy_cli_display(vm, exception.type, exception.value, exception.traceback);
    __tinypy_cli_release_exception(&hook_exception);
    __tinypy_cli_release_exception(&exception);
    return TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
/* A failure the VM did not raise as a Python exception, reported by its
   kind. */
static void __tinypy_cli_print_host_error(const tinypy_error_t *error) {
    size_t message_size;
    const char *message = tinypy_error_message(error, &message_size);
    const char *kind_name = tinypy_error_kind_name(tinypy_error_kind(error));

    (void)fprintf(stderr, "tinypy: %s", kind_name);
    if (message_size != 0U) {
        (void)fputs(": ", stderr);
        (void)fwrite(message, 1U, message_size, stderr);
    }
    (void)fputc('\n', stderr);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_cli_execute_result_e __tinypy_cli_report_failure(tinypy_vm_t *vm, tinypy_error_t *error, int32_t *out_status) {
    tinypy_cli_execute_result_e result = TINYPY_CLI_EXECUTE_ERROR;

    if (tinypy_vm_has_error(vm) != 0) {
        if (__tinypy_cli_report_exception(vm, out_status) != 0) {
            result = TINYPY_CLI_EXECUTE_EXIT;
        }
    }
    else if (error != NULL) {
        __tinypy_cli_print_host_error(error);
    }
    if (error != NULL) {
        tinypy_error_release(error);
    }
    tinypy_vm_clear_error(vm);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_cli_execute_result_e __tinypy_cli_execute(tinypy_vm_t *vm, tinypy_value_t *globals, const void *source, size_t source_size, const char *filename, tinypy_compile_mode_e mode, const tinypy_cli_context_t *context, uint32_t source_flags, tinypy_bool_t allow_incomplete, int32_t *out_status) {
    tinypy_compile_options_t options;
    tinypy_error_t *error = NULL;
    tinypy_value_t *result;
    size_t filename_size;

    tinypy_compile_options_init(&options, mode);
    options.flags = context->compile_flags | source_flags;
    options.optimize_level = context->optimize_level;
    filename_size = strlen(filename);
    result = tinypy_exec_source(vm, source, source_size, filename, filename_size, globals, NULL, &options, &error);
    tinypy_output_flush_line(vm);
    if (result != NULL) {
        tinypy_release(result);
        return TINYPY_CLI_EXECUTE_OK;
    }
    if (error != NULL && allow_incomplete != 0 && __tinypy_cli_error_is_incomplete(error) != 0) {
        tinypy_error_release(error);
        tinypy_vm_clear_error(vm);
        return TINYPY_CLI_EXECUTE_INCOMPLETE;
    }
    tinypy_cli_execute_result_e return_value_1 = __tinypy_cli_report_failure(vm, error, out_status);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_cli_execute_result_e __tinypy_cli_execute_expression(tinypy_vm_t *vm, tinypy_value_t *globals, const void *source, size_t source_size, const char *filename, const tinypy_cli_context_t *context, tinypy_bool_t allow_incomplete, int32_t *out_status) {
    tinypy_compile_options_t options;
    tinypy_error_t *error = NULL;
    tinypy_value_t *result;
    size_t filename_size;

    tinypy_compile_options_init(&options, TINYPY_COMPILE_EVAL);
    options.flags = context->compile_flags | (uint32_t)TINYPY_COMPILE_FLAG_STRING_SOURCE;
    options.optimize_level = context->optimize_level;
    filename_size = strlen(filename);
    result = tinypy_eval_source(vm, source, source_size, filename, filename_size, globals, NULL, &options, &error);
    tinypy_output_flush_line(vm);
    if (result != NULL) {
        if (tinypy_typeof(result) != TINYPY_VALUE_NONE) {
            tinypy_value_t *representation = tinypy_object_repr(result, &error);

            if (representation == NULL) {
                tinypy_release(result);
                tinypy_cli_execute_result_e return_value_1 = __tinypy_cli_report_failure(vm, error, out_status);
                return return_value_1;
            }
            size_t size;
            const char *bytes = __tinypy_cli_text_view(representation, &size);
            tinypy_output_emit(vm, TINYPY_OUTPUT_STDOUT, bytes, size);
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
    tinypy_cli_execute_result_e return_value_2 = __tinypy_cli_report_failure(vm, error, out_status);
    return return_value_2;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_cli_add_main_value(tinypy_vm_t *vm, tinypy_value_t *module, const char *name, const char *text) {
    tinypy_value_t *value;
    size_t name_size;
    size_t text_size;

    name_size = strlen(name);
    text_size = strlen(text);
    value = tinypy_string_from_bytes(vm, text, text_size);
    tinypy_module_add_value(module, name, name_size, value);
    tinypy_release(value);
}
//////////////////////////////////////////////////////////////////////////
/* __main__ as Python initializes it: __package__ None, __builtins__ the
   __builtin__ module and __file__ for a script. */
static tinypy_value_t *__tinypy_cli_create_main(tinypy_vm_t *vm, const char *filename) {
    tinypy_value_t *module = tinypy_module_new(vm, "__main__", 8U);
    tinypy_value_t *key = tinypy_string_from_bytes(vm, "__main__", 8U);
    tinypy_value_t *builtins_module = __tinypy_cli_module(vm, "__builtin__", 11U);
    tinypy_value_t *modules;

    __tinypy_cli_add_main_value(vm, module, "__name__", "__main__");
    if (filename != NULL) {
        __tinypy_cli_add_main_value(vm, module, "__file__", filename);
    }
    tinypy_module_add_value(module, "__builtins__", 12U, builtins_module != NULL ? builtins_module : tinypy_vm_builtins(vm));
    modules = tinypy_vm_modules(vm);
    tinypy_dict_set(modules, key, module);
    tinypy_release(key);
    return module;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_cli_set_sys_values(tinypy_vm_t *vm, int32_t argc, const char *const *argv, tinypy_cli_context_t *context, int32_t *out_status) {
    tinypy_error_t *error = NULL;
    tinypy_value_t *sys_module = tinypy_import_module(vm, "sys", 3U, NULL, NULL, 0, &error);
    tinypy_value_t **items;
    tinypy_value_t *list;
    size_t index;

    if (sys_module == NULL) {
        (void)__tinypy_cli_report_failure(vm, error, out_status);
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
    tinypy_value_t *hook = tinypy_native_function_new(vm, "excepthook", 10U, __tinypy_cli_excepthook, vm, NULL);
    tinypy_module_add_value(sys_module, "excepthook", 10U, hook);
    tinypy_module_add_value(sys_module, "__excepthook__", 14U, hook);
    tinypy_release(hook);
    tinypy_release(sys_module);
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
/* call_sys_exitfunc: sys.exitfunc runs once, and its failure is reported
   like an uncaught exception. */
static void __tinypy_cli_call_exitfunc(tinypy_vm_t *vm, int32_t *out_status, tinypy_bool_t *out_exit) {
    tinypy_value_t *sys_module = __tinypy_cli_module(vm, "sys", 3U);

    if (sys_module == NULL) {
        return;
    }
    tinypy_value_t *exitfunc = tinypy_module_get_value(sys_module, "exitfunc", 8U);
    if (exitfunc != NULL) {
        tinypy_value_t *key = tinypy_string_from_bytes(vm, "exitfunc", 8U);
        tinypy_error_t *error = NULL;

        tinypy_retain(exitfunc);
        tinypy_dict_delete(tinypy_module_dict(sys_module), key);
        tinypy_release(key);
        tinypy_value_t *args = tinypy_tuple_from_items(vm, NULL, 0U);
        tinypy_value_t *result = tinypy_call(exitfunc, args, NULL, &error);
        tinypy_release(args);
        tinypy_release(exitfunc);
        if (result == NULL) {
            if (__tinypy_cli_raised_system_exit(vm) == 0) {
                __tinypy_cli_write_stderr(vm, "Error in sys.exitfunc:\n");
            }
            if (__tinypy_cli_report_failure(vm, error, out_status) == TINYPY_CLI_EXECUTE_EXIT) {
                *out_exit = TINYPY_TRUE;
            }
        }
        else {
            tinypy_release(result);
        }
    }
    tinypy_output_flush_line(vm);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_cli_name_is(tinypy_value_t *key, const char *name) {
    size_t size;

    if (tinypy_typeof(key) != TINYPY_VALUE_STRING) {
        return TINYPY_FALSE;
    }
    const char *text = (const char *)tinypy_string_view(key, &size);
    tinypy_bool_t equal = size == strlen(name) && memcmp(text, name, size) == 0 ? TINYPY_TRUE : TINYPY_FALSE;
    return equal;
}
//////////////////////////////////////////////////////////////////////////
/* The survivors dictionary keys its values by identity. */
static tinypy_value_t *__tinypy_cli_survivor_key(tinypy_vm_t *vm, const tinypy_value_t *value) {
    tinypy_value_t *key = tinypy_integer_from_i64(vm, (int64_t)(intptr_t)value);
    return key;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_cli_survivor_anchored(tinypy_vm_t *vm, tinypy_value_t *survivors, const tinypy_value_t *value) {
    tinypy_value_t *key = __tinypy_cli_survivor_key(vm, value);
    tinypy_bool_t anchored = tinypy_dict_get_optional(survivors, key) != NULL ? TINYPY_TRUE : TINYPY_FALSE;

    tinypy_release(key);
    return anchored;
}
//////////////////////////////////////////////////////////////////////////
/* Classes outlive the teardown: their namespaces anchor the functions
   defined in them, whose globals may be those namespaces themselves. */
static tinypy_bool_t __tinypy_cli_survives_teardown(const tinypy_value_t *value) {
    tinypy_value_type_e kind = tinypy_typeof(value);
    tinypy_bool_t survives = kind == TINYPY_VALUE_CLASS || kind == TINYPY_VALUE_TYPE ? TINYPY_TRUE : TINYPY_FALSE;
    return survives;
}
//////////////////////////////////////////////////////////////////////////
/* Releases a value a namespace no longer holds. A value nothing else
   references dies now, running its finalizer; one other references keep
   alive joins the survivors, which keep it reachable for the sweep of the
   VM, since a cycle the namespace released would otherwise leak without a
   cyclic collector. */
static void __tinypy_cli_release_cleared(tinypy_vm_t *vm, tinypy_value_t *survivors, tinypy_value_t *value) {
    tinypy_value_t *key = __tinypy_cli_survivor_key(vm, value);
    tinypy_bool_t anchored = tinypy_dict_get_optional(survivors, key) != NULL ? TINYPY_TRUE : TINYPY_FALSE;
    tinypy_ref_t held = anchored != 0 ? 2 : 1;

    if (tinypy_refcount(value) == held && __tinypy_cli_survives_teardown(value) == 0) {
        if (anchored != 0) {
            tinypy_dict_delete(survivors, key);
        }
    }
    else if (anchored == 0) {
        tinypy_dict_set(survivors, key, value);
    }
    tinypy_release(key);
    tinypy_release(value);
}
//////////////////////////////////////////////////////////////////////////
/* Survivors that lost their other references since die in turn. */
static void __tinypy_cli_release_survivors(tinypy_value_t *survivors) {
    tinypy_bool_t progress = TINYPY_TRUE;

    while (progress != 0) {
        size_t position = 0U;
        tinypy_value_t *key;
        tinypy_value_t *value;

        progress = TINYPY_FALSE;
        while (tinypy_dict_next(survivors, &position, &key, &value) != 0) {
            if (tinypy_refcount(value) == 1 && __tinypy_cli_survives_teardown(value) == 0) {
                tinypy_retain(key);
                tinypy_dict_delete(survivors, key);
                tinypy_release(key);
                progress = TINYPY_TRUE;
                break;
            }
        }
    }
}
//////////////////////////////////////////////////////////////////////////
/* _PyModule_Clear: every value of the namespace becomes None, names of one
   leading underscore first and __builtins__ never. Names are pinned before
   values are replaced, as finalizers may mutate the namespace. */
static void __tinypy_cli_clear_namespace(tinypy_vm_t *vm, tinypy_value_t *dict, tinypy_value_t *survivors) {
    tinypy_value_t *none = tinypy_none_get(vm);

    for (size_t pass = 0U; pass < 2U; ++pass) {
        size_t capacity = tinypy_dict_size(dict);
        tinypy_value_t **keys = (tinypy_value_t **)malloc((capacity != 0U ? capacity : 1U) * sizeof(*keys));
        size_t count = 0U;
        size_t position = 0U;
        tinypy_value_t *key;
        tinypy_value_t *value;

        while (count < capacity && tinypy_dict_next(dict, &position, &key, &value) != 0) {
            size_t size;

            if (tinypy_typeof(key) != TINYPY_VALUE_STRING || tinypy_typeof(value) == TINYPY_VALUE_NONE) {
                continue;
            }
            const char *name = (const char *)tinypy_string_view(key, &size);
            tinypy_bool_t underscore = size != 0U && name[0] == '_' && (size == 1U || name[1] != '_') ? TINYPY_TRUE : TINYPY_FALSE;
            if ((pass == 0U && underscore == 0) || (pass == 1U && __tinypy_cli_name_is(key, "__builtins__") != 0)) {
                continue;
            }
            tinypy_retain(key);
            keys[count++] = key;
        }
        for (size_t index = 0U; index < count; ++index) {
            value = tinypy_dict_get_optional(dict, keys[index]);
            if (value != NULL && tinypy_typeof(value) != TINYPY_VALUE_NONE) {
                tinypy_retain(value);
                tinypy_dict_set(dict, keys[index], none);
                __tinypy_cli_release_cleared(vm, survivors, value);
            }
            tinypy_release(keys[index]);
        }
        free(keys);
    }
    tinypy_release(none);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_cli_clear_module(tinypy_vm_t *vm, tinypy_value_t *modules, tinypy_value_t *key, tinypy_value_t *module, tinypy_value_t *survivors) {
    tinypy_value_t *none = tinypy_none_get(vm);
    tinypy_value_t *dict = tinypy_module_dict(module);

    if (dict != NULL) {
        __tinypy_cli_clear_namespace(vm, dict, survivors);
    }
    tinypy_retain(module);
    tinypy_dict_set(modules, key, none);
    __tinypy_cli_release_cleared(vm, survivors, module);
    tinypy_release(none);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_cli_clear_named_module(tinypy_vm_t *vm, tinypy_value_t *modules, const char *name, size_t name_size, tinypy_value_t *survivors) {
    tinypy_value_t *key = tinypy_string_from_bytes(vm, name, name_size);
    tinypy_value_t *module = tinypy_dict_get_optional(modules, key);

    if (module != NULL && tinypy_typeof(module) == TINYPY_VALUE_MODULE) {
        __tinypy_cli_clear_module(vm, modules, key, module, survivors);
    }
    tinypy_release(key);
}
//////////////////////////////////////////////////////////////////////////
/* PyImport_Cleanup: sys forgets the program state and gets its original
   streams back, then the module namespaces become None, __main__ first,
   modules nothing else references next, every other module, then sys; the
   __builtin__ namespace stays intact for the VM. The survivors dictionary
   lives in sys.modules, a root of the VM. */
static void __tinypy_cli_cleanup_modules(tinypy_vm_t *vm) {
    static const char *const sys_deletes[] = {"path", "argv", "ps1", "ps2", "exitfunc", "exc_type", "exc_value", "exc_traceback", "last_type", "last_value", "last_traceback", "path_hooks", "path_importer_cache", "meta_path"};
    static const char *const sys_files[] = {"stdin", "__stdin__", "stdout", "__stdout__", "stderr", "__stderr__"};
    static const char survivors_name[] = "__tinypy_finalizing__";
    tinypy_value_t *modules = tinypy_vm_modules(vm);
    tinypy_value_t *none = tinypy_none_get(vm);
    tinypy_value_t *builtins_module = __tinypy_cli_module(vm, "__builtin__", 11U);
    tinypy_value_t *sys_module = __tinypy_cli_module(vm, "sys", 3U);
    tinypy_value_t *survivors = tinypy_dict_new(vm);
    tinypy_value_t *survivors_key = tinypy_string_from_bytes(vm, survivors_name, sizeof(survivors_name) - 1U);
    size_t index;

    tinypy_dict_set(modules, survivors_key, survivors);
    if (builtins_module != NULL) {
        tinypy_module_add_value(builtins_module, "_", 1U, none);
    }
    if (sys_module != NULL) {
        for (index = 0U; index < sizeof(sys_deletes) / sizeof(sys_deletes[0]); ++index) {
            size_t name_size = strlen(sys_deletes[index]);

            tinypy_module_add_value(sys_module, sys_deletes[index], name_size, none);
        }
        for (index = 0U; index < sizeof(sys_files) / sizeof(sys_files[0]); index += 2U) {
            size_t original_size = strlen(sys_files[index + 1U]);
            tinypy_value_t *original = tinypy_module_get_value(sys_module, sys_files[index + 1U], original_size);
            size_t name_size = strlen(sys_files[index]);

            tinypy_module_add_value(sys_module, sys_files[index], name_size, original != NULL ? original : none);
        }
    }
    __tinypy_cli_clear_named_module(vm, modules, "__main__", 8U, survivors);
    size_t cleared;
    do {
        size_t position = 0U;
        tinypy_value_t *key;
        tinypy_value_t *value;

        cleared = 0U;
        while (tinypy_dict_next(modules, &position, &key, &value) != 0) {
            if (tinypy_typeof(key) != TINYPY_VALUE_STRING || tinypy_typeof(value) != TINYPY_VALUE_MODULE) {
                continue;
            }
            tinypy_ref_t anchors = __tinypy_cli_survivor_anchored(vm, survivors, value) != 0 ? 2 : 1;
            if (tinypy_refcount(value) != anchors || __tinypy_cli_name_is(key, "__builtin__") != 0 || __tinypy_cli_name_is(key, "sys") != 0) {
                continue;
            }
            __tinypy_cli_clear_module(vm, modules, key, value, survivors);
            cleared += 1U;
        }
    } while (cleared != 0U);
    size_t position = 0U;
    tinypy_value_t *key;
    tinypy_value_t *value;
    while (tinypy_dict_next(modules, &position, &key, &value) != 0) {
        if (tinypy_typeof(key) != TINYPY_VALUE_STRING || tinypy_typeof(value) != TINYPY_VALUE_MODULE) {
            continue;
        }
        if (__tinypy_cli_name_is(key, "__builtin__") != 0 || __tinypy_cli_name_is(key, "sys") != 0) {
            continue;
        }
        __tinypy_cli_clear_module(vm, modules, key, value, survivors);
    }
    __tinypy_cli_clear_named_module(vm, modules, "sys", 3U, survivors);
    __tinypy_cli_release_survivors(survivors);
    tinypy_release(survivors_key);
    tinypy_release(survivors);
    tinypy_release(none);
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
static tinypy_bool_t __tinypy_cli_repl(tinypy_vm_t *vm, tinypy_value_t *globals, const tinypy_cli_context_t *context, int32_t *out_status) {
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
            if (source.size != 0U && __tinypy_cli_execute(vm, globals, source.data, source.size, "<stdin>", TINYPY_COMPILE_SINGLE, context, (uint32_t)TINYPY_COMPILE_FLAG_STRING_SOURCE, TINYPY_FALSE, out_status) == TINYPY_CLI_EXECUTE_ERROR) {
                result = TINYPY_FALSE;
            }
            break;
        }
        line_size = strlen(line);
        if (__tinypy_cli_buffer_append(&source, line, line_size) == 0) {
            (void)fputs("tinypy: input is too large\n", stderr);
            result = TINYPY_FALSE;
            break;
        }
        if (line_size == sizeof(line) - 1U && line[line_size - 1U] != '\n') {
            continue;
        }
        execute_result = __tinypy_cli_execute_expression(vm, globals, source.data, source.size, "<stdin>", context, TINYPY_TRUE, out_status);
        if (execute_result == TINYPY_CLI_EXECUTE_NOT_EXPRESSION) {
            execute_result = __tinypy_cli_execute(vm, globals, source.data, source.size, "<stdin>", TINYPY_COMPILE_SINGLE, context, (uint32_t)TINYPY_COMPILE_FLAG_STRING_SOURCE, TINYPY_TRUE, out_status);
        }
        if (execute_result == TINYPY_CLI_EXECUTE_INCOMPLETE) {
            continue;
        }
        if (execute_result == TINYPY_CLI_EXECUTE_EXIT) {
            break;
        }
        source.size = 0U;
    }
    free(source.data);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_cli_usage(FILE *stream) {
    (void)fputs("usage: tinypy [-O | -OO] [-t | -tt] [--stats] [--cycle-diagnostics] [-c command | script.py | -] [args]\n", stream);
    (void)fputs("       tinypy --version\n", stream);
    (void)fputs("       tinypy --build-info\n", stream);
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
    int32_t tab_check = 0;
    tinypy_bool_t cycle_diagnostics = TINYPY_FALSE;
    tinypy_bool_t interactive = TINYPY_FALSE;
    tinypy_bool_t success = TINYPY_TRUE;
    tinypy_bool_t exit_requested = TINYPY_FALSE;
    int32_t exit_status = 0;
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
        else if (strcmp(argv[argument], "-t") == 0) {
            tab_check += 1;
            argument += 1;
        }
        else if (strcmp(argv[argument], "-tt") == 0) {
            tab_check += 2;
            argument += 1;
        }
        else if (strcmp(argv[argument], "--stats") == 0) {
            show_stats = INT32_C(1);
            argument += 1;
        }
        else if (strcmp(argv[argument], "--build-info") == 0) {
#if defined(TINYPY_DEBUGGER)
            (void)fputs("{\"debug\":true,", stdout);
#else
            (void)fputs("{\"debug\":false,", stdout);
#endif
#if defined(TINYPY_CYCLE_DIAGNOSTICS)
            (void)fputs("\"cycle_diagnostics\":true}\n", stdout);
#else
            (void)fputs("\"cycle_diagnostics\":false}\n", stdout);
#endif
            return EXIT_SUCCESS;
        }
        else if (strcmp(argv[argument], "--cycle-diagnostics") == 0) {
#if defined(TINYPY_CYCLE_DIAGNOSTICS)
            cycle_diagnostics = TINYPY_TRUE;
            argument += 1;
#else
            (void)fputs("tinypy: cycle diagnostics require a Debug build with TINYPY_ENABLE_CYCLE_DIAGNOSTICS=ON\n", stderr);
            return EXIT_FAILURE;
#endif
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
    if (tab_check >= 2) {
        context.compile_flags = (uint32_t)TINYPY_COMPILE_FLAG_TAB_ERROR;
    }
    else if (tab_check == 1) {
        context.compile_flags = (uint32_t)TINYPY_COMPILE_FLAG_TAB_WARNING;
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
    /* sys.path[0] is the resolved directory of a script, and the empty
       string, the current directory, for a command or standard input. */
    if (script_argument >= 0 && strcmp(argv[script_argument], "-") != 0) {
        char *resolved = __tinypy_cli_resolved_path(argv[script_argument]);

        context.import_roots[context.import_root_count++] = __tinypy_cli_directory_name(resolved);
        free(resolved);
        context.import_roots[context.import_root_count] = __tinypy_cli_current_directory();
        if (context.import_roots[context.import_root_count] != NULL && strcmp(context.import_roots[0], context.import_roots[context.import_root_count]) != 0) {
            context.import_root_count += 1U;
        }
        else {
            free(context.import_roots[context.import_root_count]);
            context.import_roots[context.import_root_count] = NULL;
        }
    }
    else {
        context.import_roots[context.import_root_count++] = __tinypy_cli_string_duplicate("", 0U);
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
    config.cycle_diagnostics = cycle_diagnostics;
    begin = clock();
    vm = tinypy_vm_create(&config);
    main_module = __tinypy_cli_create_main(vm, interactive != 0 || command_argument >= 0 ? NULL : filename);
    globals = tinypy_module_dict(main_module);
#if defined(TINYPY_CYCLE_DIAGNOSTICS)
    if (cycle_diagnostics != 0) {
        static const char name[] = "__tinypy_report_cycles__";
        tinypy_value_t *key = tinypy_string_from_bytes(vm, name, sizeof(name) - 1U);
        tinypy_value_t *reporter = tinypy_native_function_new(vm, name, sizeof(name) - 1U, __tinypy_cli_report_cycles, vm, NULL);

        tinypy_dict_set(globals, key, reporter);
        tinypy_release(reporter);
        tinypy_release(key);
    }
#endif
    if (__tinypy_cli_set_sys_values(vm, python_argc, python_argv, &context, &exit_status) == 0) {
        success = INT32_C(0);
    }
    else if (interactive != 0) {
        success = __tinypy_cli_repl(vm, globals, &context, &exit_status);
    }
    else {
        uint32_t source_flags = command_argument >= 0 ? (uint32_t)TINYPY_COMPILE_FLAG_STRING_SOURCE : 0U;
        tinypy_cli_execute_result_e execute_result = __tinypy_cli_execute(vm, globals, source, source_size, filename, TINYPY_COMPILE_EXEC, &context, source_flags, TINYPY_FALSE, &exit_status);

        if (execute_result == TINYPY_CLI_EXECUTE_EXIT) {
            exit_requested = TINYPY_TRUE;
        }
        else if (execute_result != TINYPY_CLI_EXECUTE_OK) {
            success = INT32_C(0);
        }
    }
    /* Py_Finalize: the exit function, then the module namespaces, run while
       the VM is intact. */
    __tinypy_cli_call_exitfunc(vm, &exit_status, &exit_requested);
    __tinypy_cli_cleanup_modules(vm);
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
    if (success == 0) {
        return EXIT_FAILURE;
    }
    if (exit_requested != 0) {
        return exit_status;
    }
    return EXIT_SUCCESS;
}
