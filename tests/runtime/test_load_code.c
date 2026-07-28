#include "tinypy/tinypy.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef union test_allocation_header_t {
    struct {
        size_t size;
        size_t alignment;
    } fields;
    void *pointer_alignment;
    void (*function_alignment)(void);
    int64_t integer_alignment;
    long double floating_alignment;
} test_allocation_header_t;

typedef struct test_allocator_state_t {
    size_t outstanding_allocations;
    size_t outstanding_bytes;
} test_allocator_state_t;

typedef struct test_module_entry_t {
    const char *name;
    size_t name_size;
    uint8_t *bytes;
    size_t byte_size;
    tinypy_module_artifact_t artifact;
} test_module_entry_t;

typedef struct test_import_host_state_t {
    test_module_entry_t *entries;
    size_t entry_count;
    size_t request_count;
    size_t resolve_count;
    size_t release_count;
} test_import_host_state_t;

typedef struct test_output_state_t {
    uint8_t stdout_bytes[4096];
    size_t stdout_size;
    uint8_t stderr_bytes[4096];
    size_t stderr_size;
    int32_t overflow;
} test_output_state_t;

//////////////////////////////////////////////////////////////////////////
static void *__test_allocate(void *user_data, size_t size, size_t alignment) {
    test_allocator_state_t *state = (test_allocator_state_t *)user_data;
    test_allocation_header_t *header = (test_allocation_header_t *)malloc(sizeof(*header) + size);

    if (header == NULL) {
        return NULL;
    }
    header->fields.size = size;
    header->fields.alignment = alignment;
    state->outstanding_allocations += 1U;
    state->outstanding_bytes += size;
    return header + 1;
}
//////////////////////////////////////////////////////////////////////////
static void *__test_reallocate(void *user_data, void *memory, size_t old_size, size_t new_size, size_t alignment) {
    test_allocator_state_t *state = (test_allocator_state_t *)user_data;
    test_allocation_header_t *header = ((test_allocation_header_t *)memory) - 1;
    test_allocation_header_t *resized;

    if (header->fields.size != old_size || header->fields.alignment != alignment) {
        return NULL;
    }
    resized = (test_allocation_header_t *)realloc(header, sizeof(*header) + new_size);
    if (resized == NULL) {
        return NULL;
    }
    resized->fields.size = new_size;
    state->outstanding_bytes -= old_size;
    state->outstanding_bytes += new_size;
    return resized + 1;
}
//////////////////////////////////////////////////////////////////////////
static void __test_deallocate(void *user_data, void *memory, size_t size, size_t alignment) {
    test_allocator_state_t *state = (test_allocator_state_t *)user_data;
    test_allocation_header_t *header = ((test_allocation_header_t *)memory) - 1;

    if (header->fields.size != size || header->fields.alignment != alignment) {
        abort();
    }
    state->outstanding_allocations -= 1U;
    state->outstanding_bytes -= size;
    free(header);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_vm_t *__test_vm_create_with_host(test_allocator_state_t *state, const tinypy_host_t *host) {
    tinypy_allocator_t allocator;
    tinypy_vm_config_t config;

    (void)memset(&allocator, 0, sizeof(allocator));
    allocator.abi_version = TINYPY_ABI_VERSION;
    allocator.struct_size = (uint32_t)sizeof(allocator);
    allocator.user_data = state;
    allocator.allocate = __test_allocate;
    allocator.reallocate = __test_reallocate;
    allocator.deallocate = __test_deallocate;
    (void)memset(&config, 0, sizeof(config));
    config.abi_version = TINYPY_ABI_VERSION;
    config.struct_size = (uint32_t)sizeof(config);
    config.allocator = &allocator;
    config.host = host;
    tinypy_vm_t *return_value_1 = tinypy_vm_create(&config);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_vm_t *__test_vm_create(test_allocator_state_t *state) {
    tinypy_vm_t *return_value_1 = __test_vm_create_with_host(state, NULL);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static const tinypy_module_artifact_t *__test_resolve_module(void *user_data, const tinypy_module_request_t *request) {
    test_import_host_state_t *state = (test_import_host_state_t *)user_data;
    size_t index;

    state->request_count += 1U;
    for (index = 0U; index < state->entry_count; index += 1U) {
        test_module_entry_t *entry = &state->entries[index];

        if (entry->name_size == request->canonical_name_size && memcmp(entry->name, request->canonical_name, entry->name_size) == 0) {
            state->resolve_count += 1U;
            return &entry->artifact;
        }
    }
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
static void __test_release_module_artifact(void *user_data, const tinypy_module_artifact_t *artifact) {
    test_import_host_state_t *state = (test_import_host_state_t *)user_data;

    (void)artifact;
    state->release_count += 1U;
}
//////////////////////////////////////////////////////////////////////////
static void __test_emit_output(void *user_data, tinypy_output_channel_e channel, const void *bytes, size_t size) {
    test_output_state_t *state = (test_output_state_t *)user_data;
    uint8_t *target;
    size_t *target_size;

    if (channel == TINYPY_OUTPUT_STDOUT) {
        target = state->stdout_bytes;
        target_size = &state->stdout_size;
    }
    else {
        target = state->stderr_bytes;
        target_size = &state->stderr_size;
    }
    if (size > 4096U - *target_size) {
        state->overflow = 1;
        return;
    }
    if (size != 0U) {
        (void)memcpy(target + *target_size, bytes, size);
    }
    *target_size += size;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __test_load_bytes(tinypy_vm_t *vm, const void *bytes, size_t size, const char *label) {
    tinypy_marshal_error_t error;
    tinypy_value_t *code = NULL;
    tinypy_marshal_result_e result = tinypy_marshal_load_code_v2(vm, bytes, size, NULL, &code, &error);

    if (result != TINYPY_MARSHAL_OK) {
        const char *marshal_result_name = tinypy_marshal_result_name(result);
        (void)fprintf(stderr, "%s: %s at %zu: %.*s\n", label, marshal_result_name, error.offset, (int32_t)error.message_size, error.message);
        return 0;
    }
    tinypy_value_type_e code_type = code != NULL ? tinypy_typeof(code) : TINYPY_VALUE_NONE;
    tinypy_value_t *bytecode = code_type == TINYPY_VALUE_CODE ? tinypy_code_bytecode(code) : NULL;
    tinypy_value_t *consts = code_type == TINYPY_VALUE_CODE ? tinypy_code_consts(code) : NULL;
    if (code == NULL || code_type != TINYPY_VALUE_CODE || tinypy_typeof(bytecode) != TINYPY_VALUE_STRING || tinypy_typeof(consts) != TINYPY_VALUE_TUPLE) {
        (void)fprintf(stderr, "%s: invalid materialized code object\n", label);
        if (code != NULL) {
            tinypy_release(code);
        }
        return 0;
    }
    tinypy_release(code);
    return 1;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __test_fixture(tinypy_vm_t *vm) {
    static const uint8_t bytes[] = {
        0x63U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x01U, 0x00U, 0x00U, 0x00U, 0x40U, 0x00U, 0x00U,
        0x00U, 0x73U, 0x04U, 0x00U, 0x00U, 0x00U, 0x64U, 0x00U, 0x00U, 0x53U, 0x28U, 0x01U, 0x00U, 0x00U, 0x00U, 0x69U,
        0x7bU, 0x00U, 0x00U, 0x00U, 0x28U, 0x00U, 0x00U, 0x00U, 0x00U, 0x28U, 0x00U, 0x00U, 0x00U, 0x00U, 0x28U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x28U, 0x00U, 0x00U, 0x00U, 0x00U, 0x73U, 0x07U, 0x00U, 0x00U, 0x00U, 0x74U, 0x65U, 0x73U,
        0x74U, 0x2eU, 0x70U, 0x79U, 0x73U, 0x08U, 0x00U, 0x00U, 0x00U, 0x3cU, 0x6dU, 0x6fU, 0x64U, 0x75U, 0x6cU, 0x65U,
        0x3eU, 0x01U, 0x00U, 0x00U, 0x00U, 0x73U, 0x00U, 0x00U, 0x00U, 0x00U};

    int32_t return_value_1 = __test_load_bytes(vm, bytes, sizeof(bytes), "embedded fixture");
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __test_file(tinypy_vm_t *vm, const char *path) {
    FILE *stream;
    long file_size;
    uint8_t *bytes;
    size_t read_size;
    int32_t result;

    stream = fopen(path, "rb");
    if (stream == NULL || fseek(stream, 0L, SEEK_END) != 0) {
        (void)fprintf(stderr, "%s: unable to open file\n", path);
        if (stream != NULL) {
            (void)fclose(stream);
        }
        return 0;
    }
    file_size = ftell(stream);
    if (file_size < 0L || fseek(stream, 0L, SEEK_SET) != 0) {
        (void)fclose(stream);
        return 0;
    }
    bytes = (uint8_t *)malloc((size_t)file_size);
    if (bytes == NULL && file_size != 0L) {
        (void)fclose(stream);
        return 0;
    }
    read_size = fread(bytes, 1U, (size_t)file_size, stream);
    (void)fclose(stream);
    if (read_size != (size_t)file_size) {
        free(bytes);
        return 0;
    }
    result = __test_load_bytes(vm, bytes, read_size, path);
    free(bytes);
    return result;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __test_module_entry_load(test_module_entry_t *entry, const char *name, const char *path, uint32_t flags) {
    FILE *stream;
    long file_size;
    size_t read_size;

    (void)memset(entry, 0, sizeof(*entry));
    stream = fopen(path, "rb");
    if (stream == NULL || fseek(stream, 0L, SEEK_END) != 0) {
        if (stream != NULL) {
            (void)fclose(stream);
        }
        return 0;
    }
    file_size = ftell(stream);
    if (file_size < 0L || fseek(stream, 0L, SEEK_SET) != 0) {
        (void)fclose(stream);
        return 0;
    }
    entry->bytes = (uint8_t *)malloc((size_t)file_size);
    if (entry->bytes == NULL && file_size != 0L) {
        (void)fclose(stream);
        return 0;
    }
    read_size = fread(entry->bytes, 1U, (size_t)file_size, stream);
    (void)fclose(stream);
    if (read_size != (size_t)file_size) {
        free(entry->bytes);
        entry->bytes = NULL;
        return 0;
    }
    entry->name = name;
    entry->name_size = strlen(name);
    entry->byte_size = read_size;
    entry->artifact.abi_version = TINYPY_ABI_VERSION;
    entry->artifact.struct_size = (uint32_t)sizeof(entry->artifact);
    entry->artifact.content_kind = TINYPY_MODULE_CONTENT_MARSHAL_V2;
    entry->artifact.flags = flags;
    entry->artifact.data = entry->bytes;
    entry->artifact.data_size = entry->byte_size;
    entry->artifact.canonical_name = entry->name;
    entry->artifact.canonical_name_size = entry->name_size;
    entry->artifact.logical_filename = path;
    entry->artifact.logical_filename_size = strlen(path);
    return 1;
}
//////////////////////////////////////////////////////////////////////////
static void __test_module_entries_release(test_module_entry_t *entries, size_t count) {
    size_t index;

    for (index = 0U; index < count; index += 1U) {
        free(entries[index].bytes);
    }
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__test_global(tinypy_vm_t *vm, tinypy_value_t *globals, const char *name, size_t name_size) {
    tinypy_value_t *key = tinypy_string_from_bytes(vm, name, name_size);
    tinypy_value_t *value = tinypy_dict_contains(globals, key) != 0 ? tinypy_dict_get(globals, key) : NULL;

    tinypy_release(key);
    return value;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __test_global_integer(tinypy_vm_t *vm, tinypy_value_t *globals, const char *name, size_t name_size, int64_t expected) {
    tinypy_value_t *value = __test_global(vm, globals, name, name_size);

    int32_t return_value_1 = value != NULL && tinypy_typeof(value) == TINYPY_VALUE_INTEGER && tinypy_integer_as_i64(value) == expected;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __test_global_long(tinypy_vm_t *vm, tinypy_value_t *globals, const char *name, size_t name_size, int64_t expected) {
    tinypy_value_t *value = __test_global(vm, globals, name, name_size);

    int32_t return_value_1 = value != NULL && tinypy_typeof(value) == TINYPY_VALUE_LONG && tinypy_long_as_i64(value) == expected;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __test_global_bool(tinypy_vm_t *vm, tinypy_value_t *globals, const char *name, size_t name_size, int32_t expected) {
    tinypy_value_t *value = __test_global(vm, globals, name, name_size);

    int32_t return_value_1 = value != NULL && tinypy_typeof(value) == TINYPY_VALUE_BOOL && tinypy_bool_as_i32(value) == expected;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __test_global_sequence(tinypy_vm_t *vm, tinypy_value_t *globals, const char *name, size_t name_size, tinypy_value_type_e kind, const int64_t *expected, size_t expected_size) {
    tinypy_value_t *value = __test_global(vm, globals, name, name_size);
    size_t actual_size;
    size_t index;

    if (value == NULL || tinypy_typeof(value) != kind) {
        return 0;
    }
    actual_size = kind == TINYPY_VALUE_LIST ? tinypy_list_size(value) : tinypy_tuple_size(value);
    if (actual_size != expected_size) {
        return 0;
    }
    for (index = 0U; index < expected_size; index += 1U) {
        tinypy_value_t *item = kind == TINYPY_VALUE_LIST ? tinypy_list_get(value, index) : tinypy_tuple_get(value, index);

        if (tinypy_typeof(item) != TINYPY_VALUE_INTEGER || tinypy_integer_as_i64(item) != expected[index]) {
            return 0;
        }
    }
    return 1;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __test_global_string(tinypy_vm_t *vm, tinypy_value_t *globals, const char *name, size_t name_size, const char *expected, size_t expected_size) {
    tinypy_value_t *value = __test_global(vm, globals, name, name_size);
    const void *bytes;
    size_t size;

    if (value == NULL || tinypy_typeof(value) != TINYPY_VALUE_STRING) {
        return 0;
    }
    bytes = tinypy_string_view(value, &size);
    int32_t return_value_1 = size == expected_size && memcmp(bytes, expected, expected_size) == 0;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __test_import_files(const char *const *paths) {
    static const char *const names[] = {"main", "helper", "package", "package.child", "package.fallback", "package.failure", "package.implicit", "package.sibling", "package.broken_target", "absolute_only", "broken_target", "star_module"};
    test_allocator_state_t allocator_state;
    test_module_entry_t entries[12];
    test_import_host_state_t host_state;
    tinypy_host_t host;
    tinypy_vm_t *vm;
    tinypy_value_t *star;
    tinypy_value_t *fromlist;
    tinypy_value_t *module;
    tinypy_error_t *error = NULL;
    size_t loaded_count = 0U;
    int32_t success = 0;

    (void)memset(&allocator_state, 0, sizeof(allocator_state));
    (void)memset(entries, 0, sizeof(entries));
    while (loaded_count < 12U) {
        uint32_t flags = loaded_count == 2U ? (uint32_t)TINYPY_MODULE_ARTIFACT_PACKAGE : 0U;

        if (__test_module_entry_load(&entries[loaded_count], names[loaded_count], paths[loaded_count], flags) == 0) {
            goto cleanup_entries;
        }
        loaded_count += 1U;
    }
    (void)memset(&host_state, 0, sizeof(host_state));
    host_state.entries = entries;
    host_state.entry_count = 12U;
    (void)memset(&host, 0, sizeof(host));
    host.abi_version = TINYPY_ABI_VERSION;
    host.struct_size = (uint32_t)sizeof(host);
    host.user_data = &host_state;
    host.resolve_module = __test_resolve_module;
    host.release_module_artifact = __test_release_module_artifact;
    vm = __test_vm_create_with_host(&allocator_state, &host);
    star = tinypy_string_from_bytes(vm, "*", 1U);
    fromlist = tinypy_tuple_from_items(vm, &star, 1U);
    module = tinypy_import_module(vm, "main", 4U, NULL, fromlist, 0, &error);
    tinypy_release(fromlist);
    tinypy_release(star);
    if (module == NULL) {
        size_t error_size;
        const char *message = tinypy_error_message(error, &error_size);

        tinypy_error_kind_e error_kind = tinypy_error_kind(error);
        const char *error_kind_name = tinypy_error_kind_name(error_kind);
        (void)fprintf(stderr, "import fixture: %s: %.*s\n", error_kind_name, (int32_t)error_size, message);
        tinypy_error_release(error);
    }
    else {
        tinypy_value_t *module_dict = tinypy_module_dict(module);
        success = __test_global_integer(vm, module_dict, "import_result", 13U, 72);
        tinypy_release(module);
    }
    if (host_state.request_count != 12U || host_state.resolve_count != 11U || host_state.release_count != 11U) {
        success = 0;
    }
    tinypy_vm_destroy(vm);
    if (allocator_state.outstanding_allocations != 0U || allocator_state.outstanding_bytes != 0U) {
        success = 0;
    }
cleanup_entries:
    __test_module_entries_release(entries, loaded_count);
    return success;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __test_eval_file(tinypy_vm_t *vm, const char *path, int32_t check_fixture) {
    static const int64_t loop_expected[] = {2, 9, 6};
    static const int64_t delete_expected[] = {2, 3};
    static const int64_t slice_expected[] = {2, 3};
    static const int64_t slice_assign_expected[] = {0, 7, 8, 9, 3};
    static const int64_t slice_delete_expected[] = {0, 2, 4};
    static const int64_t range_expected[] = {1, 3, 5};
    FILE *stream;
    long file_size;
    uint8_t *bytes;
    size_t read_size;
    tinypy_marshal_error_t marshal_error;
    tinypy_error_t *eval_error = NULL;
    tinypy_value_t *code = NULL;
    tinypy_value_t *globals;
    tinypy_value_t *name_key;
    tinypy_value_t *name_value;
    tinypy_value_t *eval_result;
    tinypy_marshal_result_e load_result;
    int32_t success = 0;

    stream = fopen(path, "rb");
    if (stream == NULL || fseek(stream, 0L, SEEK_END) != 0) {
        if (stream != NULL) {
            (void)fclose(stream);
        }
        return 0;
    }
    file_size = ftell(stream);
    if (file_size < 0L || fseek(stream, 0L, SEEK_SET) != 0) {
        (void)fclose(stream);
        return 0;
    }
    bytes = (uint8_t *)malloc((size_t)file_size);
    if (bytes == NULL && file_size != 0L) {
        (void)fclose(stream);
        return 0;
    }
    read_size = fread(bytes, 1U, (size_t)file_size, stream);
    (void)fclose(stream);
    if (read_size != (size_t)file_size) {
        free(bytes);
        return 0;
    }
    load_result = tinypy_marshal_load_code_v2(vm, bytes, read_size, NULL, &code, &marshal_error);
    free(bytes);
    if (load_result != TINYPY_MARSHAL_OK) {
        const char *marshal_result_name = tinypy_marshal_result_name(load_result);
        (void)fprintf(stderr, "%s: %s: %.*s\n", path, marshal_result_name, (int32_t)marshal_error.message_size, marshal_error.message);
        return 0;
    }

    globals = tinypy_dict_new(vm);
    name_key = tinypy_string_from_bytes(vm, "__name__", 8U);
    name_value = tinypy_string_from_bytes(vm, "fixture", 7U);
    tinypy_dict_set(globals, name_key, name_value);
    tinypy_release(name_value);
    tinypy_release(name_key);
    eval_result = tinypy_eval_code(code, globals, NULL, &eval_error);
    if (eval_result == NULL) {
        size_t error_size;
        const char *message = tinypy_error_message(eval_error, &error_size);

        tinypy_error_kind_e error_kind = tinypy_error_kind(eval_error);
        const char *error_kind_name = tinypy_error_kind_name(error_kind);
        (void)fprintf(stderr, "%s: %s: %.*s\n", path, error_kind_name, (int32_t)error_size, message);
        tinypy_error_release(eval_error);
    }
    else {
        if (tinypy_typeof(eval_result) == TINYPY_VALUE_NONE && (check_fixture == 0 || (__test_global_integer(vm, globals, "result", 6U, 42) != 0 && __test_global_integer(vm, globals, "closure_result", 14U, 42) != 0 && __test_global_integer(vm, globals, "arithmetic_result", 17U, 32) != 0 && __test_global_sequence(vm, globals, "loop_result", 11U, TINYPY_VALUE_LIST, loop_expected, 3U) != 0 && __test_global_integer(vm, globals, "subscript_result", 16U, 6) != 0 && __test_global_integer(vm, globals, "mapping_result", 14U, 7) != 0 && __test_global_sequence(vm, globals, "delete_result", 13U, TINYPY_VALUE_LIST, delete_expected, 2U) != 0 && __test_global_sequence(vm, globals, "slice_result", 12U, TINYPY_VALUE_TUPLE, slice_expected, 2U) != 0 && __test_global_string(vm, globals, "extended_slice_result", 21U, "bdf", 3U) != 0 && __test_global_sequence(vm, globals, "slice_assign_result", 19U, TINYPY_VALUE_LIST, slice_assign_expected, 5U) != 0 && __test_global_sequence(vm, globals, "slice_delete_result", 19U, TINYPY_VALUE_LIST, slice_delete_expected, 3U) != 0 && __test_global_integer(vm, globals, "power_result", 12U, 1024) != 0 && __test_global_integer(vm, globals, "bitwise_result", 14U, 42) != 0 && __test_global_integer(vm, globals, "right_shift_result", 18U, -4) != 0 && __test_global_long(vm, globals, "long_shift_result", 17U, 2) != 0 && __test_global_long(vm, globals, "long_bitwise_result", 19U, 5) != 0 && __test_global_long(vm, globals, "invert_long_result", 18U, -2) != 0 && __test_global_long(vm, globals, "negative_long_shift_result", 26U, -4) != 0 && __test_global_long(vm, globals, "negative_bitwise_result", 23U, -5) != 0 && __test_global_long(vm, globals, "long_power_result", 17U, 8) != 0 && __test_global_bool(vm, globals, "comparison_result", 17U, 1) != 0 && __test_global_bool(vm, globals, "sequence_order_result", 21U, 1) != 0 && __test_global_integer(vm, globals, "call_var_result", 15U, 3) != 0 && __test_global_integer(vm, globals, "call_kw_result", 14U, 5) != 0 && __test_global_integer(vm, globals, "call_var_kw_result", 18U, 6) != 0 && __test_global_integer(vm, globals, "builtin_len_result", 18U, 3) != 0 && __test_global_sequence(vm, globals, "builtin_range_result", 20U, TINYPY_VALUE_LIST, range_expected, 3U) != 0 && __test_global_bool(vm, globals, "builtin_isinstance_result", 25U, 1) != 0 && __test_global_bool(vm, globals, "builtin_callable_result", 23U, 1) != 0 && __test_global_integer(vm, globals, "builtin_getattr_result", 22U, 7) != 0 && __test_global_bool(vm, globals, "builtin_hasattr_result", 22U, 1) != 0 && __test_global_integer(vm, globals, "builtin_abs_result", 18U, 7) != 0 && __test_global_integer(vm, globals, "builtin_ord_result", 18U, 65) != 0 && __test_global_bool(vm, globals, "builtin_id_result", 17U, 1) != 0 && __test_global_integer(vm, globals, "builtin_next_result", 19U, 9) != 0 && __test_global_integer(vm, globals, "builtin_setattr_result", 22U, 8) != 0 && __test_global_integer(vm, globals, "staticmethod_result", 19U, 42) != 0 && __test_global_bool(vm, globals, "classmethod_result", 18U, 1) != 0 && __test_global_integer(vm, globals, "property_read_result", 20U, 40) != 0 && __test_global_integer(vm, globals, "property_write_result", 21U, 50) != 0 && __test_global_integer(vm, globals, "super_result", 12U, 52) != 0 && __test_global_bool(vm, globals, "property_fields_result", 22U, 1) != 0 && __test_global_integer(vm, globals, "exception_result", 16U, 42) != 0 && __test_global_integer(vm, globals, "finally_return_result", 21U, 42) != 0 && __test_global_integer(vm, globals, "finally_marker", 14U, 1) != 0 && __test_global_integer(vm, globals, "nested_exception_result", 23U, 42) != 0 && __test_global_integer(vm, globals, "custom_exception_result", 23U, 42) != 0 && __test_global_integer(vm, globals, "reraised_exception_result", 25U, 42) != 0 && __test_global_integer(vm, globals, "finally_override_result", 23U, 42) != 0))) {
            success = 1;
        }
        tinypy_release(eval_result);
    }
    tinypy_dict_clear(globals);
    tinypy_release(globals);
    tinypy_release(code);
    return success;
}
//////////////////////////////////////////////////////////////////////////
static int32_t __test_eval_output(const char *path, const char *expected_stdout, const char *expected_stderr) {
    test_allocator_state_t allocator_state;
    test_output_state_t output_state;
    tinypy_host_t host;
    tinypy_vm_t *vm;
    int32_t success;

    (void)memset(&allocator_state, 0, sizeof(allocator_state));
    (void)memset(&output_state, 0, sizeof(output_state));
    (void)memset(&host, 0, sizeof(host));
    host.abi_version = TINYPY_ABI_VERSION;
    host.struct_size = (uint32_t)sizeof(host);
    host.user_data = &output_state;
    host.emit_output = __test_emit_output;
    vm = __test_vm_create_with_host(&allocator_state, &host);
    success = __test_eval_file(vm, path, 0);
    if (output_state.overflow != 0 || output_state.stdout_size != strlen(expected_stdout) || memcmp(output_state.stdout_bytes, expected_stdout, output_state.stdout_size) != 0 || output_state.stderr_size != strlen(expected_stderr) || memcmp(output_state.stderr_bytes, expected_stderr, output_state.stderr_size) != 0) {
        success = 0;
    }
    tinypy_vm_destroy(vm);
    if (allocator_state.outstanding_allocations != 0U || allocator_state.outstanding_bytes != 0U) {
        success = 0;
    }
    return success;
}
//////////////////////////////////////////////////////////////////////////
int main(int argc, char **argv) {
    test_allocator_state_t state;
    tinypy_vm_t *vm;
    size_t base_allocations;
    int32_t index;

    if (argc == 14 && strcmp(argv[1], "--eval-import") == 0) {
        int return_value_1 = __test_import_files((const char *const *)&argv[2]) != 0 ? 0 : 1;
        return return_value_1;
    }
    if (argc == 5 && strcmp(argv[1], "--eval-output") == 0) {
        int return_value_2 = __test_eval_output(argv[2], argv[3], argv[4]) != 0 ? 0 : 1;
        return return_value_2;
    }
    (void)memset(&state, 0, sizeof(state));
    vm = __test_vm_create(&state);
    base_allocations = state.outstanding_allocations;
    if (argc == 3 && (strcmp(argv[1], "--eval") == 0 || strcmp(argv[1], "--eval-any") == 0)) {
        int32_t compare = strcmp(argv[1], "--eval");
        if (__test_eval_file(vm, argv[2], compare == 0) == 0) {
            return 1;
        }
        if (state.outstanding_allocations != base_allocations) {
            tinypy_value_t *vm_raised_exception = tinypy_vm_raised_exception(vm);
            tinypy_value_t *vm_handled_exception = tinypy_vm_handled_exception(vm);
            (void)fprintf(stderr, "%s: evaluation leaked VM allocations: base=%zu actual=%zu bytes=%zu raised=%p handled=%p\n", argv[2], base_allocations, state.outstanding_allocations, state.outstanding_bytes, (void *)vm_raised_exception, (void *)vm_handled_exception);
            return 1;
        }
        tinypy_vm_destroy(vm);
        return state.outstanding_allocations == 0U && state.outstanding_bytes == 0U ? 0 : 1;
    }
    if (__test_fixture(vm) == 0) {
        return 1;
    }
    if (state.outstanding_allocations != base_allocations) {
        (void)fprintf(stderr, "embedded fixture leaked VM allocations\n");
        return 1;
    }
    for (index = 1; index < argc; index += 1) {
        if (__test_file(vm, argv[index]) == 0) {
            return 1;
        }
        if (state.outstanding_allocations != base_allocations) {
            (void)fprintf(stderr, "%s: materialized graph leaked VM allocations\n", argv[index]);
            return 1;
        }
    }
    tinypy_vm_destroy(vm);
    if (state.outstanding_allocations != 0U || state.outstanding_bytes != 0U) {
        (void)fprintf(stderr, "VM destruction leaked allocations\n");
        return 1;
    }
    return 0;
}
