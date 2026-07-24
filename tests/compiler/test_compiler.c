#include "tinypy/tinypy.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <pthread.h>
#endif

typedef struct test_allocator_state_t {
    size_t allocations;
    size_t bytes;
} test_allocator_state_t;

typedef struct test_source_host_t {
    tinypy_module_artifact_t artifact;
    size_t resolve_count;
    size_t release_count;
} test_source_host_t;

typedef struct test_source_multi_host_t {
    tinypy_module_artifact_t *artifacts;
    size_t artifact_count;
    size_t resolve_count;
    size_t release_count;
} test_source_multi_host_t;

typedef struct test_reentrant_state_t {
    tinypy_vm_t *vm;
    int32_t inherited_future;
} test_reentrant_state_t;

typedef struct test_compile_thread_state_t {
    int32_t passed;
} test_compile_thread_state_t;

typedef struct test_interrupt_host_t {
    size_t polls;
    size_t interrupt_after;
} test_interrupt_host_t;

//////////////////////////////////////////////////////////////////////////
static void *__test_allocate(void *user_data, size_t size, size_t alignment, uint32_t tag) {
    test_allocator_state_t *state = (test_allocator_state_t *)user_data;
    void *memory;

    (void)alignment;
    (void)tag;
    memory = malloc(size);
    assert(memory != NULL);
    state->allocations += 1U;
    state->bytes += size;
    return memory;
}
//////////////////////////////////////////////////////////////////////////
static void *__test_reallocate(void *user_data, void *memory, size_t old_size, size_t new_size, size_t alignment, uint32_t tag) {
    test_allocator_state_t *state = (test_allocator_state_t *)user_data;
    void *resized;

    (void)alignment;
    (void)tag;
    resized = realloc(memory, new_size);
    assert(resized != NULL);
    state->bytes -= old_size;
    state->bytes += new_size;
    return resized;
}
//////////////////////////////////////////////////////////////////////////
static void __test_deallocate(void *user_data, void *memory, size_t size, size_t alignment, uint32_t tag) {
    test_allocator_state_t *state = (test_allocator_state_t *)user_data;

    (void)alignment;
    (void)tag;
    assert(state->allocations != 0U);
    assert(state->bytes >= size);
    state->allocations -= 1U;
    state->bytes -= size;
    free(memory);
}
//////////////////////////////////////////////////////////////////////////
static int __test_poll_interrupt(void *user_data) {
    test_interrupt_host_t *host = (test_interrupt_host_t *)user_data;

    host->polls += 1U;
    return host->polls >= host->interrupt_after ? 1 : 0;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_vm_t *__test_vm_create(test_allocator_state_t *state, int32_t optimize_level) {
    tinypy_allocator_t allocator;
    tinypy_vm_config_t config;

    (void)memset(&allocator, 0, sizeof(allocator));
    allocator.abi_version = TINYPY_ABI_VERSION;
    allocator.struct_size = (uint32_t)sizeof(allocator);
    allocator.user_data = state;
    allocator.allocate = &__test_allocate;
    allocator.reallocate = &__test_reallocate;
    allocator.deallocate = &__test_deallocate;
    (void)memset(&config, 0, sizeof(config));
    config.abi_version = TINYPY_ABI_VERSION;
    config.struct_size = (uint32_t)sizeof(config);
    config.allocator = &allocator;
    config.optimize_level = optimize_level;
    return tinypy_vm_create(&config);
}
//////////////////////////////////////////////////////////////////////////
static const tinypy_module_artifact_t *__test_source_resolve(void *user_data, const tinypy_module_request_t *request) {
    test_source_host_t *host = (test_source_host_t *)user_data;

    if (request->canonical_name_size != host->artifact.canonical_name_size || memcmp(request->canonical_name, host->artifact.canonical_name, request->canonical_name_size) != 0) {
        return NULL;
    }
    host->resolve_count += 1U;
    return &host->artifact;
}
//////////////////////////////////////////////////////////////////////////
static void __test_source_release(void *user_data, const tinypy_module_artifact_t *artifact) {
    test_source_host_t *host = (test_source_host_t *)user_data;

    assert(artifact == &host->artifact);
    host->release_count += 1U;
}
//////////////////////////////////////////////////////////////////////////
static const tinypy_module_artifact_t *__test_source_multi_resolve(void *user_data, const tinypy_module_request_t *request) {
    test_source_multi_host_t *host = (test_source_multi_host_t *)user_data;
    size_t index;

    for (index = 0U; index < host->artifact_count; index += 1U) {
        tinypy_module_artifact_t *artifact = &host->artifacts[index];

        if (request->canonical_name_size == artifact->canonical_name_size && memcmp(request->canonical_name, artifact->canonical_name, request->canonical_name_size) == 0) {
            host->resolve_count += 1U;
            return artifact;
        }
    }
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
static void __test_source_multi_release(void *user_data, const tinypy_module_artifact_t *artifact) {
    test_source_multi_host_t *host = (test_source_multi_host_t *)user_data;

    assert(artifact >= host->artifacts && artifact < host->artifacts + host->artifact_count);
    host->release_count += 1U;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_vm_t *__test_vm_create_with_source_host(test_allocator_state_t *state, test_source_host_t *source_host) {
    tinypy_allocator_t allocator;
    tinypy_host_t host;
    tinypy_vm_config_t config;

    (void)memset(&allocator, 0, sizeof(allocator));
    allocator.abi_version = TINYPY_ABI_VERSION;
    allocator.struct_size = (uint32_t)sizeof(allocator);
    allocator.user_data = state;
    allocator.allocate = &__test_allocate;
    allocator.reallocate = &__test_reallocate;
    allocator.deallocate = &__test_deallocate;
    (void)memset(&host, 0, sizeof(host));
    host.abi_version = TINYPY_ABI_VERSION;
    host.struct_size = (uint32_t)sizeof(host);
    host.user_data = source_host;
    host.resolve_module = &__test_source_resolve;
    host.release_module_artifact = &__test_source_release;
    (void)memset(&config, 0, sizeof(config));
    config.abi_version = TINYPY_ABI_VERSION;
    config.struct_size = (uint32_t)sizeof(config);
    config.allocator = &allocator;
    config.host = &host;
    return tinypy_vm_create(&config);
}
//////////////////////////////////////////////////////////////////////////
static tinypy_vm_t *__test_vm_create_with_source_multi_host(test_allocator_state_t *state, test_source_multi_host_t *source_host) {
    tinypy_allocator_t allocator;
    tinypy_host_t host;
    tinypy_vm_config_t config;

    (void)memset(&allocator, 0, sizeof(allocator));
    allocator.abi_version = TINYPY_ABI_VERSION;
    allocator.struct_size = (uint32_t)sizeof(allocator);
    allocator.user_data = state;
    allocator.allocate = &__test_allocate;
    allocator.reallocate = &__test_reallocate;
    allocator.deallocate = &__test_deallocate;
    (void)memset(&host, 0, sizeof(host));
    host.abi_version = TINYPY_ABI_VERSION;
    host.struct_size = (uint32_t)sizeof(host);
    host.user_data = source_host;
    host.resolve_module = &__test_source_multi_resolve;
    host.release_module_artifact = &__test_source_multi_release;
    (void)memset(&config, 0, sizeof(config));
    config.abi_version = TINYPY_ABI_VERSION;
    config.struct_size = (uint32_t)sizeof(config);
    config.allocator = &allocator;
    config.host = &host;
    return tinypy_vm_create(&config);
}
//////////////////////////////////////////////////////////////////////////
static int __test_empty_exec(void) {
    static const char logical_filename[] = {'e', 'm', 'p', 't', 'y', '.', 'p', 'y'};
    test_allocator_state_t state = {0U, 0U};
    tinypy_vm_t *vm = __test_vm_create(&state, 2);
    tinypy_compile_options_t options;
    tinypy_value_t *code;
    tinypy_value_t *bytecode;
    tinypy_value_t *consts;
    size_t bytecode_size;
    const unsigned char expected[] = {100U, 0U, 0U, 83U};

    tinypy_compile_options_init(&options, TINYPY_COMPILE_EXEC);
    options.optimize_level = 2;
    code = tinypy_compile_source(vm, NULL, 0U, logical_filename, sizeof(logical_filename), &options, NULL);
    assert(code != NULL);
    assert(tinypy_code_arg_count(code) == 0);
    assert(tinypy_code_local_count(code) == 0);
    assert(tinypy_code_stack_size(code) == 1);
    assert(tinypy_code_flags(code) == TINYPY_CODE_NO_FREE);
    bytecode = tinypy_code_bytecode(code);
    assert(memcmp(tinypy_string_view(bytecode, &bytecode_size), expected, sizeof(expected)) == 0);
    assert(bytecode_size == sizeof(expected));
    consts = tinypy_code_consts(code);
    assert(tinypy_tuple_size(consts) == 1U);
    assert(tinypy_typeof(tinypy_tuple_get(consts, 0U)) == TINYPY_VALUE_NONE);
    assert(tinypy_typeof(tinypy_code_filename(code)) == TINYPY_VALUE_STRING);
    assert(tinypy_string_view(tinypy_code_filename(code), &bytecode_size) != NULL);
    assert(bytecode_size == sizeof(logical_filename));
    assert(memcmp(tinypy_string_view(tinypy_code_filename(code), &bytecode_size), logical_filename, sizeof(logical_filename)) == 0);
    tinypy_release(code);
    tinypy_vm_destroy(vm);
    assert(state.allocations == 0U);
    assert(state.bytes == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
static int __test_source_diagnostic(void) {
    test_allocator_state_t state = {0U, 0U};
    tinypy_vm_t *vm = __test_vm_create(&state, 0);
    tinypy_compile_options_t options;
    tinypy_error_t *error = NULL;
    tinypy_value_t *code;
    const char source[] = "value =\r\nnext = 2\r";
    const char *filename;
    const char *line;
    size_t filename_size;
    size_t line_size;

    tinypy_compile_options_init(&options, TINYPY_COMPILE_EXEC);
    code = tinypy_compile_source(vm, source, sizeof(source) - 1U, "diagnostic.py", 13U, &options, &error);
    assert(code == NULL);
    assert(error != NULL);
    assert(tinypy_error_kind(error) == TINYPY_ERROR_SYNTAX);
    assert(tinypy_error_line_number(error) == 1);
    assert(tinypy_error_column_offset(error) == 8);
    filename = tinypy_error_logical_filename(error, &filename_size);
    assert(filename_size == 13U && memcmp(filename, "diagnostic.py", 13U) == 0);
    line = tinypy_error_source_line(error, &line_size);
    assert(line_size == 8U && memcmp(line, "value =\n", 8U) == 0);
    tinypy_error_release(error);
    tinypy_vm_destroy(vm);
    assert(state.allocations == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
static int __test_source_decoding(void) {
    static const unsigned char latin1_source[] = "#!/usr/bin/env python\n# coding: latin-1\nvalue = u\"\xe9\"\n";
    static const unsigned char ascii_invalid[] = "# coding: ascii\nvalue = u\"\xc3\xa9\"\n";
    static const unsigned char bom_utf8[] = "\xef\xbb\xbf# coding: utf-8\nvalue = u\"\xc3\xa9\"\n";
    static const unsigned char bom_latin1[] = "\xef\xbb\xbf# coding: latin-1\nvalue = 1\n";
    static const unsigned char invalid_utf8[] = "value = u\"\xff\"\n";
    static const unsigned char embedded_nul[] = {'v', 'a', 'l', 'u', 'e', '=', '1', '\0', '\n'};
    static const char coding_in_code[] = "value = 'coding: koala'\n";
    static const char second_cookie_after_code[] = "value = 1\n# coding: koala\n";
    static const char unknown_cookie[] = "# comment\n# coding: koala\nvalue = 1\n";
    test_allocator_state_t state = {0U, 0U};
    tinypy_vm_t *vm = __test_vm_create(&state, 0);
    tinypy_compile_options_t options;
    tinypy_error_t *error = NULL;
    tinypy_value_t *code;

    tinypy_compile_options_init(&options, TINYPY_COMPILE_EXEC);
    code = tinypy_compile_source(vm, latin1_source, sizeof(latin1_source) - 1U, "latin1.py", 9U, &options, NULL);
    assert(code != NULL);
    tinypy_release(code);
    code = tinypy_compile_source(vm, bom_utf8, sizeof(bom_utf8) - 1U, "bom.py", 6U, &options, NULL);
    assert(code != NULL);
    tinypy_release(code);
    code = tinypy_compile_source(vm, coding_in_code, sizeof(coding_in_code) - 1U, "inline.py", 9U, &options, NULL);
    assert(code != NULL);
    tinypy_release(code);
    code = tinypy_compile_source(vm, second_cookie_after_code, sizeof(second_cookie_after_code) - 1U, "second.py", 9U, &options, NULL);
    assert(code != NULL);
    tinypy_release(code);

    code = tinypy_compile_source(vm, ascii_invalid, sizeof(ascii_invalid) - 1U, "ascii.py", 8U, &options, &error);
    assert(code == NULL && error != NULL && tinypy_error_kind(error) == TINYPY_ERROR_SOURCE_DECODING);
    tinypy_error_release(error);
    error = NULL;
    code = tinypy_compile_source(vm, bom_latin1, sizeof(bom_latin1) - 1U, "bom_latin.py", 12U, &options, &error);
    assert(code == NULL && error != NULL && tinypy_error_kind(error) == TINYPY_ERROR_SOURCE_DECODING);
    tinypy_error_release(error);
    error = NULL;
    code = tinypy_compile_source(vm, invalid_utf8, sizeof(invalid_utf8) - 1U, "utf8.py", 7U, &options, &error);
    assert(code == NULL && error != NULL && tinypy_error_kind(error) == TINYPY_ERROR_SOURCE_DECODING);
    tinypy_error_release(error);
    error = NULL;
    code = tinypy_compile_source(vm, unknown_cookie, sizeof(unknown_cookie) - 1U, "unknown.py", 10U, &options, &error);
    assert(code == NULL && error != NULL && tinypy_error_kind(error) == TINYPY_ERROR_SYNTAX);
    assert(tinypy_error_line_number(error) == 2);
    tinypy_error_release(error);
    error = NULL;
    code = tinypy_compile_source(vm, embedded_nul, sizeof(embedded_nul), "nul.py", 6U, &options, &error);
    assert(code == NULL && error != NULL && tinypy_error_kind(error) == TINYPY_ERROR_SYNTAX);
    tinypy_error_release(error);

    tinypy_vm_destroy(vm);
    assert(state.allocations == 0U && state.bytes == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
static void __test_unicode_value(tinypy_value_t *value, const unsigned char *expected, size_t expected_size) {
    const char *bytes;
    size_t byte_size;
    size_t code_point_count;

    assert(tinypy_typeof(value) == TINYPY_VALUE_UNICODE);
    bytes = tinypy_unicode_utf8_view(value, &byte_size, &code_point_count);
    assert(byte_size == expected_size);
    assert(memcmp(bytes, expected, expected_size) == 0);
}
//////////////////////////////////////////////////////////////////////////
static int __test_named_unicode_escapes(void) {
    static const char source[] = "(u'\\N{LATIN SMALL LETTER A}', u'\\N{GREEK SMALL LETTER PI}', u'\\N{HANGUL SYLLABLE GA}', u'\\N{CJK UNIFIED IDEOGRAPH-4E00}')";
    static const char invalid_source[] = "u'\\N{NOT A CHARACTER}'";
    static const char surrogate_source[] = "u'\\ud800'";
    static const unsigned char latin_a[] = {0x61U};
    static const unsigned char greek_pi[] = {0xcfU, 0x80U};
    static const unsigned char hangul_ga[] = {0xeaU, 0xb0U, 0x80U};
    static const unsigned char cjk_one[] = {0xe4U, 0xb8U, 0x80U};
    test_allocator_state_t state = {0U, 0U};
    tinypy_vm_t *vm = __test_vm_create(&state, 0);
    tinypy_compile_options_t options;
    tinypy_value_t *globals = tinypy_dict_new(vm);
    tinypy_value_t *result;
    tinypy_error_t *error = NULL;

    tinypy_compile_options_init(&options, TINYPY_COMPILE_EVAL);
    result = tinypy_eval_source(vm, source, sizeof(source) - 1U, "unicode.py", 10U, globals, NULL, &options, NULL);
    assert(result != NULL && tinypy_tuple_size(result) == 4U);
    tinypy_value_t *item = tinypy_tuple_get(result, 0U);
    __test_unicode_value(item, latin_a, sizeof(latin_a));
    tinypy_value_t *item_2 = tinypy_tuple_get(result, 1U);
    __test_unicode_value(item_2, greek_pi, sizeof(greek_pi));
    tinypy_value_t *item_3 = tinypy_tuple_get(result, 2U);
    __test_unicode_value(item_3, hangul_ga, sizeof(hangul_ga));
    tinypy_value_t *item_4 = tinypy_tuple_get(result, 3U);
    __test_unicode_value(item_4, cjk_one, sizeof(cjk_one));
    tinypy_release(result);
    result = tinypy_eval_source(vm, invalid_source, sizeof(invalid_source) - 1U, "unicode.py", 10U, globals, NULL, &options, &error);
    assert(result == NULL && error != NULL && tinypy_error_kind(error) == TINYPY_ERROR_SYNTAX);
    tinypy_error_release(error);
    error = NULL;
    result = tinypy_eval_source(vm, surrogate_source, sizeof(surrogate_source) - 1U, "unicode.py", 10U, globals, NULL, &options, &error);
    assert(result == NULL && error != NULL && tinypy_error_kind(error) == TINYPY_ERROR_SYNTAX);
    tinypy_error_release(error);
    tinypy_release(globals);
    tinypy_vm_destroy(vm);
    assert(state.allocations == 0U && state.bytes == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__test_dict_get(tinypy_vm_t *vm, tinypy_value_t *dict, const char *name, size_t name_size) {
    tinypy_value_t *key = tinypy_string_from_bytes(vm, name, name_size);
    tinypy_value_t *value;

    assert(tinypy_dict_contains(dict, key) != 0);
    value = tinypy_dict_get(dict, key);
    tinypy_release(key);
    return value;
}
//////////////////////////////////////////////////////////////////////////
static int __test_legacy_vm_config_optimize_default(void) {
    test_allocator_state_t state = {0U, 0U};
    tinypy_allocator_t allocator;
    tinypy_vm_config_t config;
    tinypy_vm_t *vm;
    tinypy_value_t *debug_value;

    (void)memset(&allocator, 0, sizeof(allocator));
    allocator.abi_version = TINYPY_ABI_VERSION;
    allocator.struct_size = (uint32_t)sizeof(allocator);
    allocator.user_data = &state;
    allocator.allocate = &__test_allocate;
    allocator.reallocate = &__test_reallocate;
    allocator.deallocate = &__test_deallocate;
    (void)memset(&config, 0, sizeof(config));
    config.abi_version = TINYPY_ABI_VERSION;
    config.struct_size = (uint32_t)offsetof(tinypy_vm_config_t, optimize_level);
    config.allocator = &allocator;
    vm = tinypy_vm_create(&config);
    tinypy_value_t *vm_builtins = tinypy_vm_builtins(vm);
    debug_value = __test_dict_get(vm, vm_builtins, "__debug__", 9U);
    assert(tinypy_typeof(debug_value) == TINYPY_VALUE_BOOL && tinypy_bool_as_i32(debug_value) != 0);
    tinypy_vm_destroy(vm);
    assert(state.allocations == 0U && state.bytes == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__test_find_nested_code(tinypy_value_t *code, const char *name, size_t name_size) {
    tinypy_value_t *consts = tinypy_code_consts(code);
    size_t index;

    for (index = 0U; index < tinypy_tuple_size(consts); index += 1U) {
        tinypy_value_t *nested = tinypy_tuple_get(consts, index);

        if (tinypy_typeof(nested) == TINYPY_VALUE_CODE) {
            tinypy_value_t *code_name = tinypy_code_name(nested);
            size_t code_name_size;
            const char *code_name_bytes = tinypy_string_view(code_name, &code_name_size);
            tinypy_value_t *found;

            if (code_name_size == name_size && memcmp(code_name_bytes, name, name_size) == 0) {
                return nested;
            }
            found = __test_find_nested_code(nested, name, name_size);
            if (found != NULL) {
                return found;
            }
        }
    }
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
static int __test_private_name_mangling(void) {
    static const char source[] = "class AnalyticUnit(object):\n    callback = lambda *_, **__: None\n";
    test_allocator_state_t state = {0U, 0U};
    tinypy_vm_t *vm = __test_vm_create(&state, 0);
    tinypy_compile_options_t options;
    tinypy_value_t *code;
    tinypy_value_t *lambda_code;
    tinypy_value_t *varnames;
    size_t name_size;
    const char *name;

    tinypy_compile_options_init(&options, TINYPY_COMPILE_EXEC);
    code = tinypy_compile_source(vm, source, sizeof(source) - 1U, "mangle.py", 9U, &options, NULL);
    assert(code != NULL);
    lambda_code = __test_find_nested_code(code, "<lambda>", 8U);
    assert(lambda_code != NULL);
    varnames = tinypy_code_varnames(lambda_code);
    assert(tinypy_tuple_size(varnames) == 2U);
    tinypy_value_t *item = tinypy_tuple_get(varnames, 1U);
    name = tinypy_string_view(item, &name_size);
    assert(name_size == 2U && memcmp(name, "__", 2U) == 0);
    tinypy_release(code);
    tinypy_vm_destroy(vm);
    assert(state.allocations == 0U && state.bytes == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
static int __test_compile_marshal_parity(void) {
    static const unsigned char expected[] = {
        0x63, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00,
        0x00, 0x73, 0x0a, 0x00, 0x00, 0x00, 0x64, 0x00, 0x00, 0x5a, 0x00, 0x00, 0x64, 0x01, 0x00, 0x53,
        0x28, 0x02, 0x00, 0x00, 0x00, 0x69, 0x2a, 0x00, 0x00, 0x00, 0x4e, 0x28, 0x01, 0x00, 0x00, 0x00,
        0x74, 0x06, 0x00, 0x00, 0x00, 0x72, 0x65, 0x73, 0x75, 0x6c, 0x74, 0x28, 0x00, 0x00, 0x00, 0x00,
        0x28, 0x00, 0x00, 0x00, 0x00, 0x28, 0x00, 0x00, 0x00, 0x00, 0x73, 0x08, 0x00, 0x00, 0x00, 0x73,
        0x6d, 0x61, 0x6c, 0x6c, 0x2e, 0x70, 0x79, 0x74, 0x08, 0x00, 0x00, 0x00, 0x3c, 0x6d, 0x6f, 0x64,
        0x75, 0x6c, 0x65, 0x3e, 0x01, 0x00, 0x00, 0x00, 0x74, 0x00, 0x00, 0x00, 0x00};
    test_allocator_state_t state = {0U, 0U};
    tinypy_vm_t *vm = __test_vm_create(&state, 0);
    tinypy_compile_options_t options;
    tinypy_value_t *code;
    unsigned char output[sizeof(expected)];
    size_t size;

    tinypy_compile_options_init(&options, TINYPY_COMPILE_EXEC);
    code = tinypy_compile_source(vm, "result = 42\n", 12U, "small.py", 8U, &options, NULL);
    assert(code != NULL);
    assert(tinypy_marshal_dump_code_v2(code, NULL, 0U, &size, NULL, NULL) == TINYPY_MARSHAL_OK);
    assert(size == sizeof(expected));
    assert(tinypy_marshal_dump_code_v2(code, output, sizeof(output), &size, NULL, NULL) == TINYPY_MARSHAL_OK);
    assert(memcmp(output, expected, sizeof(expected)) == 0);
    tinypy_release(code);
    tinypy_vm_destroy(vm);
    assert(state.allocations == 0U && state.bytes == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
static int __test_exec_eval_and_dynamic_compile(void) {
    static const char source[] =
        "from __future__ import division\n"
        "direct = eval('1 / 2')\n"
        "inherited = eval(compile('1 / 2', 'future.py', 'eval'))\n"
        "isolated = eval(compile('1 / 2', 'future.py', 'eval', 0, 1))\n"
        "unicode_eval = eval(u'40 + 2')\n"
        "unicode_cookie_rejected = False\n"
        "try:\n"
        "    compile(u'# coding: utf-8\\npass\\n', 'unicode.py', 'exec')\n"
        "except SyntaxError:\n"
        "    unicode_cookie_rejected = True\n"
        "unicode_exec_cookie_rejected = False\n"
        "try:\n"
        "    exec u'# coding: utf-8\\npass\\n'\n"
        "except SyntaxError:\n"
        "    unicode_exec_cookie_rejected = True\n"
        "closure_result = (lambda x: (lambda y: x + y))(40)(2)\n"
        "exception_message = str(RuntimeError('runtime failure'))\n"
        "payload = 'dynamic = 6 * 7'\n"
        "exec payload\n"
        "class InPlaceValue(object):\n"
        "    def __init__(self, value):\n"
        "        self.value = value\n"
        "    def __add__(self, other):\n"
        "        raise AssertionError('__add__ used instead of __iadd__')\n"
        "    def __sub__(self, other):\n"
        "        raise AssertionError('__sub__ used instead of __isub__')\n"
        "    def __iadd__(self, other):\n"
        "        self.value += other\n"
        "        return self\n"
        "    def __isub__(self, other):\n"
        "        self.value -= other\n"
        "        return self\n"
        "inplace_value = InPlaceValue(40)\n"
        "inplace_identity = inplace_value\n"
        "inplace_value += 5\n"
        "inplace_value -= 3\n"
        "inplace_is_same = inplace_value is inplace_identity\n"
        "inplace_result = inplace_value.value\n"
        "class InPlaceFallback(object):\n"
        "    def __init__(self, value):\n"
        "        self.value = value\n"
        "    def __iadd__(self, other):\n"
        "        return NotImplemented\n"
        "    def __add__(self, other):\n"
        "        return self.value + other\n"
        "inplace_fallback = InPlaceFallback(40)\n"
        "inplace_fallback += 2\n"
        "class EmptyLayoutMixin(object):\n"
        "    __slots__ = ()\n"
        "class LaterSlottedBase(object):\n"
        "    __slots__ = ('later_value',)\n"
        "class EmptyLayoutMixinFirst(EmptyLayoutMixin, LaterSlottedBase):\n"
        "    __slots__ = ()\n"
        "combined_layout = EmptyLayoutMixinFirst()\n"
        "combined_layout.later_value = 42\n"
        "combined_layout_result = combined_layout.later_value\n"
        "class MethodOwner(object):\n"
        "    def callback(self):\n"
        "        return 42\n"
        "method_owner = MethodOwner()\n"
        "saved_bound_method = method_owner.callback\n"
        "bound_methods_equal = saved_bound_method == method_owner.callback\n"
        "bound_method_list = [saved_bound_method]\n"
        "bound_method_contained = method_owner.callback in bound_method_list\n"
        "bound_method_list.remove(method_owner.callback)\n"
        "bound_method_removed = len(bound_method_list) == 0\n"
        "bound_method_dict = {saved_bound_method: 42}\n"
        "bound_method_dict_result = bound_method_dict[method_owner.callback]\n";
    test_allocator_state_t state = {0U, 0U};
    tinypy_vm_t *vm = __test_vm_create(&state, 0);
    tinypy_compile_options_t options;
    tinypy_value_t *globals = tinypy_dict_new(vm);
    tinypy_value_t *result;
    tinypy_error_t *error = NULL;

    tinypy_compile_options_init(&options, TINYPY_COMPILE_EXEC);
    result = tinypy_exec_source(vm, source, sizeof(source) - 1U, "dynamic.py", 10U, globals, NULL, &options, &error);
    assert(result != NULL);
    assert(error == NULL);
    tinypy_release(result);
    assert(tinypy_typeof(__test_dict_get(vm, globals, "direct", 6U)) == TINYPY_VALUE_FLOAT);
    assert(tinypy_float_as_double(__test_dict_get(vm, globals, "direct", 6U)) == 0.5);
    assert(tinypy_float_as_double(__test_dict_get(vm, globals, "inherited", 9U)) == 0.5);
    assert(tinypy_integer_as_i64(__test_dict_get(vm, globals, "isolated", 8U)) == 0);
    assert(tinypy_integer_as_i64(__test_dict_get(vm, globals, "unicode_eval", 12U)) == 42);
    assert(tinypy_bool_as_i32(__test_dict_get(vm, globals, "unicode_cookie_rejected", 23U)) != 0);
    assert(tinypy_bool_as_i32(__test_dict_get(vm, globals, "unicode_exec_cookie_rejected", 28U)) != 0);
    assert(tinypy_integer_as_i64(__test_dict_get(vm, globals, "closure_result", 14U)) == 42);
    {
        size_t exception_message_size;
        const char *exception_message = tinypy_string_view(__test_dict_get(vm, globals, "exception_message", 17U), &exception_message_size);

        assert(exception_message_size == 15U);
        assert(memcmp(exception_message, "runtime failure", 15U) == 0);
    }
    assert(tinypy_integer_as_i64(__test_dict_get(vm, globals, "dynamic", 7U)) == 42);
    assert(tinypy_bool_as_i32(__test_dict_get(vm, globals, "inplace_is_same", 15U)) != 0);
    assert(tinypy_integer_as_i64(__test_dict_get(vm, globals, "inplace_result", 14U)) == 42);
    assert(tinypy_integer_as_i64(__test_dict_get(vm, globals, "inplace_fallback", 16U)) == 42);
    assert(tinypy_integer_as_i64(__test_dict_get(vm, globals, "combined_layout_result", 22U)) == 42);
    assert(tinypy_bool_as_i32(__test_dict_get(vm, globals, "bound_methods_equal", 19U)) != 0);
    assert(tinypy_bool_as_i32(__test_dict_get(vm, globals, "bound_method_contained", 22U)) != 0);
    assert(tinypy_bool_as_i32(__test_dict_get(vm, globals, "bound_method_removed", 20U)) != 0);
    assert(tinypy_integer_as_i64(__test_dict_get(vm, globals, "bound_method_dict_result", 24U)) == 42);
    tinypy_release(globals);
    tinypy_vm_destroy(vm);
    assert(state.allocations == 0U && state.bytes == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
static int __test_runtime_cache_invalidation(void) {
    static const char source[] =
        "import sys\n"
        "len = 7\n"
        "def mutate_global():\n"
        "    global len\n"
        "    before = len\n"
        "    len = 8\n"
        "    after_store = len\n"
        "    del len\n"
        "    after_delete = len((1, 2, 3))\n"
        "    len = 9\n"
        "    after_restore = len\n"
        "    return (before, after_store, after_delete, after_restore)\n"
        "global_cache_result = mutate_global()\n"
        "class CacheBase(object):\n"
        "    value = 11\n"
        "class CacheChild(CacheBase):\n"
        "    pass\n"
        "cache_instance = CacheChild()\n"
        "cache_before = cache_instance.value\n"
        "CacheBase.value = 12\n"
        "cache_after_set = cache_instance.value\n"
        "del CacheBase.value\n"
        "try:\n"
        "    cache_instance.value\n"
        "    cache_after_delete = False\n"
        "except AttributeError:\n"
        "    cache_after_delete = True\n"
        "CacheBase.value = 14\n"
        "cache_after_restore = cache_instance.value\n"
        "class AttributeFallback(object):\n"
        "    @property\n"
        "    def missing(self):\n"
        "        raise AttributeError('missing')\n"
        "    def __getattr__(self, name):\n"
        "        return 42\n"
        "attribute_fallback = AttributeFallback().missing\n"
        "class AttributeValueError(object):\n"
        "    @property\n"
        "    def missing(self):\n"
        "        raise ValueError('missing')\n"
        "    def __getattr__(self, name):\n"
        "        return 43\n"
        "try:\n"
        "    AttributeValueError().missing\n"
        "    attribute_value_error = False\n"
        "except ValueError:\n"
        "    attribute_value_error = True\n"
        "class GetattributeFallback(object):\n"
        "    def __getattribute__(self, name):\n"
        "        raise AttributeError(name)\n"
        "    def __getattr__(self, name):\n"
        "        return 44\n"
        "getattribute_fallback = GetattributeFallback().missing\n"
        "class DynamicGetattr(object):\n"
        "    pass\n"
        "dynamic_getattr_owner = DynamicGetattr()\n"
        "try:\n"
        "    dynamic_getattr_owner.missing\n"
        "except AttributeError:\n"
        "    pass\n"
        "def dynamic_getattr(self, name):\n"
        "    return 45\n"
        "DynamicGetattr.__getattr__ = dynamic_getattr\n"
        "dynamic_getattr_result = dynamic_getattr_owner.missing\n"
        "class DynamicGetattribute(object):\n"
        "    value = 46\n"
        "dynamic_getattribute_owner = DynamicGetattribute()\n"
        "dynamic_getattribute_before = dynamic_getattribute_owner.value\n"
        "def dynamic_getattribute(self, name):\n"
        "    return 47\n"
        "DynamicGetattribute.__getattribute__ = dynamic_getattribute\n"
        "dynamic_getattribute_after = dynamic_getattribute_owner.value\n"
        "class InstanceHint(object):\n"
        "    pass\n"
        "instance_hint = InstanceHint()\n"
        "instance_hint.value = 48\n"
        "instance_hint_before = instance_hint.value\n"
        "instance_hint.value = 49\n"
        "instance_hint_after_set = instance_hint.value\n"
        "del instance_hint.value\n"
        "try:\n"
        "    instance_hint.value\n"
        "    instance_hint_after_delete = False\n"
        "except AttributeError:\n"
        "    instance_hint_after_delete = True\n"
        "instance_hint.value = 50\n"
        "instance_hint_after_restore = instance_hint.value\n"
        "class MethodOwner(object):\n"
        "    def add(self, value):\n"
        "        return value + 1\n"
        "method_owner = MethodOwner()\n"
        "saved_method = method_owner.add\n"
        "method_total = 0\n"
        "for method_index in xrange(1024):\n"
        "    method_total += method_owner.add(method_index)\n"
        "saved_method_result = saved_method(41)\n"
        "def retain_current_frame():\n"
        "    current_frame = sys._getframe()\n"
        "def retain_current_exception():\n"
        "    try:\n"
        "        raise ValueError('frame cycle')\n"
        "    except ValueError:\n"
        "        current_exception = sys.exc_info()\n"
        "for frame_index in xrange(64):\n"
        "    retain_current_frame()\n"
        "    retain_current_exception()\n";
    test_allocator_state_t state = {0U, 0U};
    tinypy_vm_t *vm = __test_vm_create(&state, 0);
    tinypy_compile_options_t options;
    tinypy_value_t *globals = tinypy_dict_new(vm);
    tinypy_value_t *global_cache_result;
    tinypy_value_t *result;
    tinypy_error_t *error = NULL;

    tinypy_compile_options_init(&options, TINYPY_COMPILE_EXEC);
    result = tinypy_exec_source(vm, source, sizeof(source) - 1U, "runtime_cache.py", 15U, globals, NULL, &options, &error);
    assert(result != NULL && error == NULL);
    tinypy_release(result);
    global_cache_result = __test_dict_get(vm, globals, "global_cache_result", 19U);
    assert(tinypy_tuple_size(global_cache_result) == 4U);
    assert(tinypy_integer_as_i64(tinypy_tuple_get(global_cache_result, 0U)) == 7);
    assert(tinypy_integer_as_i64(tinypy_tuple_get(global_cache_result, 1U)) == 8);
    assert(tinypy_integer_as_i64(tinypy_tuple_get(global_cache_result, 2U)) == 3);
    assert(tinypy_integer_as_i64(tinypy_tuple_get(global_cache_result, 3U)) == 9);
    assert(tinypy_integer_as_i64(__test_dict_get(vm, globals, "cache_before", 12U)) == 11);
    assert(tinypy_integer_as_i64(__test_dict_get(vm, globals, "cache_after_set", 15U)) == 12);
    assert(tinypy_bool_as_i32(__test_dict_get(vm, globals, "cache_after_delete", 18U)) != 0);
    assert(tinypy_integer_as_i64(__test_dict_get(vm, globals, "cache_after_restore", 19U)) == 14);
    assert(tinypy_integer_as_i64(__test_dict_get(vm, globals, "attribute_fallback", 18U)) == 42);
    assert(tinypy_bool_as_i32(__test_dict_get(vm, globals, "attribute_value_error", 21U)) != 0);
    assert(tinypy_integer_as_i64(__test_dict_get(vm, globals, "getattribute_fallback", 21U)) == 44);
    assert(tinypy_integer_as_i64(__test_dict_get(vm, globals, "dynamic_getattr_result", 22U)) == 45);
    assert(tinypy_integer_as_i64(__test_dict_get(vm, globals, "dynamic_getattribute_before", 27U)) == 46);
    assert(tinypy_integer_as_i64(__test_dict_get(vm, globals, "dynamic_getattribute_after", 26U)) == 47);
    assert(tinypy_integer_as_i64(__test_dict_get(vm, globals, "instance_hint_before", 20U)) == 48);
    assert(tinypy_integer_as_i64(__test_dict_get(vm, globals, "instance_hint_after_set", 23U)) == 49);
    assert(tinypy_bool_as_i32(__test_dict_get(vm, globals, "instance_hint_after_delete", 26U)) != 0);
    assert(tinypy_integer_as_i64(__test_dict_get(vm, globals, "instance_hint_after_restore", 27U)) == 50);
    assert(tinypy_integer_as_i64(__test_dict_get(vm, globals, "method_total", 12U)) == 524800);
    assert(tinypy_integer_as_i64(__test_dict_get(vm, globals, "saved_method_result", 19U)) == 42);
    tinypy_release(globals);
    tinypy_vm_destroy(vm);
    assert(state.allocations == 0U && state.bytes == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
static int __test_dictionary_iteration_semantics(void) {
    static const char source[] =
        "mapping = {'a': 1, 'b': 2, 'c': 3}\n"
        "for key in mapping:\n"
        "    mapping[key] = 0\n"
        "value_update_succeeded = mapping == {'a': 0, 'b': 0, 'c': 0}\n"
        "size_change_detected = False\n"
        "try:\n"
        "    for key in mapping:\n"
        "        mapping['d'] = 4\n"
        "except RuntimeError as error:\n"
        "    size_change_detected = str(error) == 'dictionary changed size during iteration'\n";
    test_allocator_state_t state = {0U, 0U};
    tinypy_vm_t *vm = __test_vm_create(&state, 0);
    tinypy_compile_options_t options;
    tinypy_value_t *globals = tinypy_dict_new(vm);
    tinypy_value_t *result;
    tinypy_error_t *error = NULL;

    tinypy_compile_options_init(&options, TINYPY_COMPILE_EXEC);
    result = tinypy_exec_source(vm, source, sizeof(source) - 1U, "dict_iteration.py", 17U, globals, NULL, &options, &error);
    assert(result != NULL);
    assert(error == NULL);
    tinypy_release(result);
    assert(tinypy_bool_as_i32(__test_dict_get(vm, globals, "value_update_succeeded", 22U)) != 0);
    assert(tinypy_bool_as_i32(__test_dict_get(vm, globals, "size_change_detected", 20U)) != 0);
    tinypy_release(globals);
    tinypy_vm_destroy(vm);
    assert(state.allocations == 0U && state.bytes == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
static int __test_runtime_interrupt_polling(void) {
    static const char source[] = "while True:\n    pass\n";
    test_allocator_state_t state = {0U, 0U};
    test_interrupt_host_t interrupt_host = {0U, 2U};
    tinypy_allocator_t allocator;
    tinypy_host_t host;
    tinypy_vm_config_t config;
    tinypy_compile_options_t options;
    tinypy_vm_t *vm;
    tinypy_value_t *globals;
    tinypy_value_t *result;
    tinypy_error_t *error = NULL;

    (void)memset(&allocator, 0, sizeof(allocator));
    allocator.abi_version = TINYPY_ABI_VERSION;
    allocator.struct_size = (uint32_t)sizeof(allocator);
    allocator.user_data = &state;
    allocator.allocate = &__test_allocate;
    allocator.reallocate = &__test_reallocate;
    allocator.deallocate = &__test_deallocate;
    (void)memset(&host, 0, sizeof(host));
    host.abi_version = TINYPY_ABI_VERSION;
    host.struct_size = (uint32_t)sizeof(host);
    host.user_data = &interrupt_host;
    host.poll_interrupt = &__test_poll_interrupt;
    (void)memset(&config, 0, sizeof(config));
    config.abi_version = TINYPY_ABI_VERSION;
    config.struct_size = (uint32_t)sizeof(config);
    config.allocator = &allocator;
    config.host = &host;
    vm = tinypy_vm_create(&config);
    globals = tinypy_dict_new(vm);
    tinypy_compile_options_init(&options, TINYPY_COMPILE_EXEC);
    result = tinypy_exec_source(vm, source, sizeof(source) - 1U, "interrupt.py", 12U, globals, NULL, &options, &error);
    assert(result == NULL);
    assert(error != NULL && tinypy_error_kind(error) == TINYPY_ERROR_INTERRUPT);
    assert(interrupt_host.polls == 2U);
    tinypy_error_release(error);
    tinypy_release(globals);
    tinypy_vm_destroy(vm);
    assert(state.allocations == 0U && state.bytes == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
static int __test_compile_modes_and_optimize(void) {
    test_allocator_state_t state = {0U, 0U};
    tinypy_vm_t *vm = __test_vm_create(&state, 0);
    tinypy_compile_options_t options;
    tinypy_value_t *globals = tinypy_dict_new(vm);
    tinypy_value_t *code;
    tinypy_value_t *result;

    tinypy_compile_options_init(&options, TINYPY_COMPILE_EVAL);
    result = tinypy_eval_source(vm, "40 + 2", 6U, "eval.py", 7U, globals, NULL, &options, NULL);
    assert(result != NULL && tinypy_integer_as_i64(result) == 42);
    tinypy_release(result);

    tinypy_compile_options_init(&options, TINYPY_COMPILE_SINGLE);
    code = tinypy_compile_source(vm, "single_value = 9\n", 17U, "single.py", 9U, &options, NULL);
    assert(code != NULL);
    result = tinypy_exec_code(code, globals, NULL, NULL);
    assert(result != NULL && tinypy_typeof(result) == TINYPY_VALUE_NONE);
    tinypy_release(result);
    tinypy_release(code);
    assert(tinypy_integer_as_i64(__test_dict_get(vm, globals, "single_value", 12U)) == 9);

    tinypy_compile_options_init(&options, TINYPY_COMPILE_EVAL);
    options.optimize_level = 0;
    result = tinypy_eval_source(vm, "__debug__", 9U, "debug.py", 8U, globals, NULL, &options, NULL);
    assert(result != NULL && tinypy_bool_as_i32(result) != 0);
    tinypy_release(result);
    options.optimize_level = 2;
    result = tinypy_eval_source(vm, "eval('__debug__')", 17U, "debug.py", 8U, globals, NULL, &options, NULL);
    assert(result != NULL && tinypy_bool_as_i32(result) == 0);
    tinypy_release(result);
    tinypy_release(globals);
    tinypy_vm_destroy(vm);
    assert(state.allocations == 0U && state.bytes == 0U);
    vm = __test_vm_create(&state, 2);
    globals = tinypy_dict_new(vm);
    tinypy_compile_options_init(&options, TINYPY_COMPILE_EVAL);
    options.optimize_level = 2;
    result = tinypy_eval_source(vm, "__debug__", 9U, "debug.py", 8U, globals, NULL, &options, NULL);
    assert(result != NULL && tinypy_bool_as_i32(result) == 0);
    tinypy_release(result);
    tinypy_release(globals);
    tinypy_vm_destroy(vm);
    assert(state.allocations == 0U && state.bytes == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
static int __test_host_call_eval_without_frame(void) {
    test_allocator_state_t state = {0U, 0U};
    tinypy_vm_t *vm = __test_vm_create(&state, 0);
    tinypy_value_t *builtins = tinypy_vm_builtins(vm);
    tinypy_value_t *eval_function = __test_dict_get(vm, builtins, "eval", 4U);
    tinypy_value_t *source = tinypy_string_from_bytes(vm, "40 + 2", 6U);
    tinypy_value_t *args = tinypy_tuple_from_items(vm, &source, 1U);
    tinypy_value_t *result = tinypy_call(eval_function, args, NULL, NULL);

    assert(result != NULL && tinypy_integer_as_i64(result) == 42);
    tinypy_release(result);
    tinypy_release(args);
    tinypy_release(source);
    tinypy_vm_destroy(vm);
    assert(state.allocations == 0U && state.bytes == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
static int __test_indentation_diagnostics(void) {
    static const char unexpected_indent[] = "  value = 1\n";
    static const char missing_indent[] = "if True:\npass\n";
    static const char mixed_tabs[] = "if True:\n\tvalue = 1\n        value = 2\n";
    test_allocator_state_t state = {0U, 0U};
    tinypy_vm_t *vm = __test_vm_create(&state, 0);
    tinypy_compile_options_t options;
    tinypy_error_t *error = NULL;
    tinypy_value_t *code;

    tinypy_compile_options_init(&options, TINYPY_COMPILE_EXEC);
    code = tinypy_compile_source(vm, unexpected_indent, sizeof(unexpected_indent) - 1U, "indent.py", 9U, &options, &error);
    assert(code == NULL && error != NULL && tinypy_error_kind(error) == TINYPY_ERROR_INDENTATION);
    assert(tinypy_error_line_number(error) == 1 && tinypy_error_column_offset(error) == 2);
    tinypy_error_release(error);
    error = NULL;
    code = tinypy_compile_source(vm, missing_indent, sizeof(missing_indent) - 1U, "indent.py", 9U, &options, &error);
    assert(code == NULL && error != NULL && tinypy_error_kind(error) == TINYPY_ERROR_INDENTATION);
    assert(tinypy_error_line_number(error) == 2 && tinypy_error_column_offset(error) == 4);
    tinypy_error_release(error);
    error = NULL;
    code = tinypy_compile_source(vm, mixed_tabs, sizeof(mixed_tabs) - 1U, "tabs.py", 7U, &options, &error);
    assert(code == NULL && error != NULL && tinypy_error_kind(error) == TINYPY_ERROR_TAB);
    tinypy_error_release(error);

    tinypy_vm_destroy(vm);
    assert(state.allocations == 0U && state.bytes == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
static int __test_dont_imply_dedent(void) {
    static const char incomplete[] = "if True:\n    pass";
    static const char complete[] = "if True:\n    pass\n";
    test_allocator_state_t state = {0U, 0U};
    tinypy_vm_t *vm = __test_vm_create(&state, 0);
    tinypy_compile_options_t options;
    tinypy_error_t *error = NULL;
    tinypy_value_t *code;

    tinypy_compile_options_init(&options, TINYPY_COMPILE_SINGLE);
    options.flags = (uint32_t)TINYPY_COMPILE_FLAG_DONT_IMPLY_DEDENT;
    options.dont_inherit = 1;
    code = tinypy_compile_source(vm, incomplete, sizeof(incomplete) - 1U, "codeop.py", 9U, &options, &error);
    assert(code == NULL && error != NULL && tinypy_error_kind(error) == TINYPY_ERROR_SYNTAX);
    assert(tinypy_error_line_number(error) == 2 && tinypy_error_column_offset(error) == 8);
    tinypy_error_release(error);
    code = tinypy_compile_source(vm, complete, sizeof(complete) - 1U, "codeop.py", 9U, &options, NULL);
    assert(code != NULL);
    assert((tinypy_code_flags(code) & (int32_t)TINYPY_COMPILE_FLAG_DONT_IMPLY_DEDENT) == 0);
    tinypy_release(code);
    tinypy_vm_destroy(vm);
    assert(state.allocations == 0U && state.bytes == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
static int __test_future_and_reserved_debug(void) {
    static const char misplaced_future[] = "value = 1\nfrom __future__ import division\n";
    static const char braces_future[] = "from __future__ import braces\n";
    static const char unknown_future[] = "from __future__ import koala\n";
    static const char unknown_message[] = "future feature koala is not defined";
    static const char debug_assignment[] = "__debug__ = False\n";
    static const char valid_future[] = "from __future__ import division, absolute_import\nvalue = 1 / 2\n";
    static const char print_function_future[] = "from __future__ import print_function\nprint = 1\n";
    const char *invalid_sources[] = {misplaced_future, braces_future, unknown_future, debug_assignment};
    const size_t invalid_sizes[] = {sizeof(misplaced_future) - 1U, sizeof(braces_future) - 1U, sizeof(unknown_future) - 1U, sizeof(debug_assignment) - 1U};
    test_allocator_state_t state = {0U, 0U};
    tinypy_vm_t *vm = __test_vm_create(&state, 0);
    tinypy_compile_options_t options;
    size_t index;
    tinypy_value_t *code;

    tinypy_compile_options_init(&options, TINYPY_COMPILE_EXEC);
    for (index = 0U; index < sizeof(invalid_sources) / sizeof(invalid_sources[0]); index += 1U) {
        tinypy_error_t *error = NULL;

        code = tinypy_compile_source(vm, invalid_sources[index], invalid_sizes[index], "future.py", 9U, &options, &error);
        assert(code == NULL && error != NULL && tinypy_error_kind(error) == TINYPY_ERROR_SYNTAX);
        if (index == 2U) {
            size_t message_size;
            const char *message = tinypy_error_message(error, &message_size);

            assert(message_size == sizeof(unknown_message) - 1U && memcmp(message, unknown_message, message_size) == 0);
        }
        tinypy_error_release(error);
    }
    code = tinypy_compile_source(vm, valid_future, sizeof(valid_future) - 1U, "future.py", 9U, &options, NULL);
    assert(code != NULL);
    assert((tinypy_code_flags(code) & TINYPY_CODE_FUTURE_DIVISION) != 0);
    assert((tinypy_code_flags(code) & TINYPY_CODE_FUTURE_ABSOLUTE_IMPORT) != 0);
    tinypy_release(code);
    code = tinypy_compile_source(vm, print_function_future, sizeof(print_function_future) - 1U, "future.py", 9U, &options, NULL);
    assert(code != NULL);
    assert((tinypy_code_flags(code) & TINYPY_CODE_FUTURE_PRINT_FUNCTION) != 0);
    tinypy_release(code);
    tinypy_vm_destroy(vm);
    assert(state.allocations == 0U && state.bytes == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
static int __test_symtable_and_ast_error_messages(void) {
    static const char duplicate_argument[] = "def f(value, value): pass\n";
    static const char local_and_global[] = "def f(value):\n    global value\n";
    static const char assign_literal[] = "1 = value\n";
    static const char delete_literal[] = "del 1\n";
    static const char duplicate_message[] = "duplicate argument 'value' in function definition";
    static const char global_message[] = "name 'value' is local and global";
    static const char assign_message[] = "can't assign to literal";
    static const char delete_message[] = "can't delete literal";
    const char *sources[] = {duplicate_argument, local_and_global, assign_literal, delete_literal};
    const size_t source_sizes[] = {sizeof(duplicate_argument) - 1U, sizeof(local_and_global) - 1U, sizeof(assign_literal) - 1U, sizeof(delete_literal) - 1U};
    const char *messages[] = {duplicate_message, global_message, assign_message, delete_message};
    const size_t message_sizes[] = {sizeof(duplicate_message) - 1U, sizeof(global_message) - 1U, sizeof(assign_message) - 1U, sizeof(delete_message) - 1U};
    test_allocator_state_t state = {0U, 0U};
    tinypy_vm_t *vm = __test_vm_create(&state, 0);
    tinypy_compile_options_t options;
    size_t index;

    tinypy_compile_options_init(&options, TINYPY_COMPILE_EXEC);
    for (index = 0U; index < sizeof(sources) / sizeof(sources[0]); index += 1U) {
        tinypy_error_t *error = NULL;
        tinypy_value_t *code = tinypy_compile_source(vm, sources[index], source_sizes[index], "semantic.py", 11U, &options, &error);
        const char *message;
        size_t message_size;

        assert(code == NULL && error != NULL && tinypy_error_kind(error) == TINYPY_ERROR_SYNTAX);
        message = tinypy_error_message(error, &message_size);
        assert(message_size == message_sizes[index] && memcmp(message, messages[index], message_size) == 0);
        tinypy_error_release(error);
    }
    tinypy_vm_destroy(vm);
    assert(state.allocations == 0U && state.bytes == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
static int __test_codegen_syntax_errors(void) {
    static const char delete_free[] = "def outer():\n    value = 1\n    def inner(): return value\n    del value\n";
    static const char delete_message[] = "can not delete variable 'value' referenced in nested scope";
    static const char block_message[] = "too many statically nested blocks";
    test_allocator_state_t state = {0U, 0U};
    tinypy_vm_t *vm = __test_vm_create(&state, 0);
    tinypy_compile_options_t options;
    tinypy_error_t *error = NULL;
    tinypy_value_t *code;
    char nested_blocks[4096];
    size_t source_size = 0U;
    size_t depth;
    size_t index;
    const char *message;
    size_t message_size;

    tinypy_compile_options_init(&options, TINYPY_COMPILE_EXEC);
    code = tinypy_compile_source(vm, delete_free, sizeof(delete_free) - 1U, "closure.py", 10U, &options, &error);
    assert(code == NULL && error != NULL && tinypy_error_kind(error) == TINYPY_ERROR_SYNTAX);
    message = tinypy_error_message(error, &message_size);
    assert(message_size == sizeof(delete_message) - 1U && memcmp(message, delete_message, message_size) == 0);
    assert(tinypy_error_line_number(error) == 4);
    tinypy_error_release(error);
    error = NULL;

    for (depth = 0U; depth < 25U; depth += 1U) {
        for (index = 0U; index < depth * 4U; index += 1U) {
            nested_blocks[source_size++] = ' ';
        }
        (void)memcpy(nested_blocks + source_size, "while 1:\n", 9U);
        source_size += 9U;
    }
    for (index = 0U; index < 25U * 4U; index += 1U) {
        nested_blocks[source_size++] = ' ';
    }
    (void)memcpy(nested_blocks + source_size, "pass\n", 5U);
    source_size += 5U;
    code = tinypy_compile_source(vm, nested_blocks, source_size, "blocks.py", 9U, &options, &error);
    assert(code == NULL && error != NULL && tinypy_error_kind(error) == TINYPY_ERROR_SYNTAX);
    message = tinypy_error_message(error, &message_size);
    assert(message_size == sizeof(block_message) - 1U && memcmp(message, block_message, message_size) == 0);
    assert(tinypy_error_line_number(error) > 0);
    tinypy_error_release(error);

    tinypy_vm_destroy(vm);
    assert(state.allocations == 0U && state.bytes == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__test_reentrant_compile_callback(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error) {
    test_reentrant_state_t *state = (test_reentrant_state_t *)user_data;
    tinypy_compile_options_t options;
    tinypy_value_t *code;

    (void)function;
    (void)args;
    (void)kwargs;
    tinypy_compile_options_init(&options, TINYPY_COMPILE_EVAL);
    code = tinypy_compile_source(state->vm, "1 / 2", 5U, "reentrant.py", 12U, &options, out_error);
    if (code == NULL) {
        return NULL;
    }
    state->inherited_future = (tinypy_code_flags(code) & TINYPY_CODE_FUTURE_DIVISION) != 0 ? 1 : 0;
    tinypy_release(code);
    return tinypy_none_get(state->vm);
}
//////////////////////////////////////////////////////////////////////////
static int __test_reentrant_compile(void) {
    static const char source[] = "from __future__ import division\nnested_compile()\n";
    test_allocator_state_t allocator_state = {0U, 0U};
    tinypy_vm_t *vm = __test_vm_create(&allocator_state, 0);
    test_reentrant_state_t reentrant_state;
    tinypy_compile_options_t options;
    tinypy_value_t *globals = tinypy_dict_new(vm);
    tinypy_value_t *function;
    tinypy_value_t *key;
    tinypy_value_t *result;

    reentrant_state.vm = vm;
    reentrant_state.inherited_future = 0;
    function = tinypy_native_function_new(vm, "nested_compile", 14U, &__test_reentrant_compile_callback, &reentrant_state, NULL);
    key = tinypy_string_from_bytes(vm, "nested_compile", 14U);
    tinypy_dict_set(globals, key, function);
    tinypy_release(key);
    tinypy_release(function);
    tinypy_compile_options_init(&options, TINYPY_COMPILE_EXEC);
    result = tinypy_exec_source(vm, source, sizeof(source) - 1U, "outer.py", 8U, globals, NULL, &options, NULL);
    assert(result != NULL);
    assert(reentrant_state.inherited_future != 0);
    tinypy_release(result);
    tinypy_release(globals);
    tinypy_vm_destroy(vm);
    assert(allocator_state.allocations == 0U && allocator_state.bytes == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
static void __test_compile_thread_run(test_compile_thread_state_t *thread_state) {
    test_allocator_state_t allocator_state = {0U, 0U};
    tinypy_vm_t *vm = __test_vm_create(&allocator_state, 0);
    tinypy_compile_options_t options;
    tinypy_value_t *globals = tinypy_dict_new(vm);
    size_t iteration;

    thread_state->passed = 0;
    tinypy_compile_options_init(&options, TINYPY_COMPILE_EVAL);
    for (iteration = 0U; iteration < 32U; iteration += 1U) {
        tinypy_value_t *result = tinypy_eval_source(vm, "6 * 7", 5U, "thread.py", 9U, globals, NULL, &options, NULL);

        if (result == NULL || tinypy_integer_as_i64(result) != 42) {
            if (result != NULL) {
                tinypy_release(result);
            }
            tinypy_release(globals);
            tinypy_vm_destroy(vm);
            return;
        }
        tinypy_release(result);
    }
    tinypy_release(globals);
    tinypy_vm_destroy(vm);
    if (allocator_state.allocations == 0U && allocator_state.bytes == 0U) {
        thread_state->passed = 1;
    }
}

#if defined(_WIN32)
static DWORD WINAPI __test_compile_thread_entry(LPVOID user_data) {
    __test_compile_thread_run((test_compile_thread_state_t *)user_data);
    return 0;
}
#else
//////////////////////////////////////////////////////////////////////////
static void *__test_compile_thread_entry(void *user_data) {
    __test_compile_thread_run((test_compile_thread_state_t *)user_data);
    return NULL;
}
#endif

//////////////////////////////////////////////////////////////////////////
static int __test_parallel_compile(void) {
    test_compile_thread_state_t states[4];
    size_t index;

#if defined(_WIN32)
    HANDLE threads[4];

    for (index = 0U; index < 4U; index += 1U) {
        states[index].passed = 0;
        threads[index] = CreateThread(NULL, 0U, &__test_compile_thread_entry, &states[index], 0U, NULL);
        assert(threads[index] != NULL);
    }
    assert(WaitForMultipleObjects(4U, threads, TRUE, INFINITE) == WAIT_OBJECT_0);
    for (index = 0U; index < 4U; index += 1U) {
        CloseHandle(threads[index]);
    }
#else
    pthread_t threads[4];

    for (index = 0U; index < 4U; index += 1U) {
        states[index].passed = 0;
        assert(pthread_create(&threads[index], NULL, &__test_compile_thread_entry, &states[index]) == 0);
    }
    for (index = 0U; index < 4U; index += 1U) {
        assert(pthread_join(threads[index], NULL) == 0);
    }
#endif
    for (index = 0U; index < 4U; index += 1U) {
        assert(states[index].passed != 0);
    }
    return 0;
}
//////////////////////////////////////////////////////////////////////////
static int __test_compiler_limits(void) {
    static const char source[] = "def f(a):\n    return (a + 123, 'constant payload')\nvalue = f(2)\n";
    test_allocator_state_t state = {0U, 0U};
    tinypy_vm_t *vm = __test_vm_create(&state, 0);
    tinypy_compile_options_t options;
    tinypy_compile_limits_t limits;
    size_t *limit_fields[] = {
        &limits.max_tokens,
        &limits.max_cst_nodes,
        &limits.max_ast_nodes,
        &limits.max_nesting,
        &limits.max_symbols,
        &limits.max_blocks,
        &limits.max_instructions,
        &limits.max_constants,
        &limits.max_constant_bytes,
        &limits.max_arena_bytes};
    size_t index;

    for (index = 0U; index < sizeof(limit_fields) / sizeof(limit_fields[0]); index += 1U) {
        tinypy_error_t *error = NULL;
        tinypy_value_t *code;

        tinypy_compile_limits_init(&limits);
        *limit_fields[index] = 1U;
        tinypy_compile_options_init(&options, TINYPY_COMPILE_EXEC);
        options.limits = &limits;
        code = tinypy_compile_source(vm, source, sizeof(source) - 1U, "limits.py", 9U, &options, &error);
        assert(code == NULL);
        assert(error != NULL && tinypy_error_kind(error) == TINYPY_ERROR_COMPILER_LIMIT);
        tinypy_error_release(error);
    }
    tinypy_vm_destroy(vm);
    assert(state.allocations == 0U && state.bytes == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
static int __test_source_import_and_rollback(void) {
    static const char valid_source[] = "value = 42\n";
    static const char invalid_source[] = "value =\n";
    test_allocator_state_t state = {0U, 0U};
    test_source_host_t host;
    tinypy_vm_t *vm;
    tinypy_value_t *module;
    tinypy_value_t *modules;
    tinypy_value_t *key;
    tinypy_error_t *error = NULL;

    (void)memset(&host, 0, sizeof(host));
    host.artifact.abi_version = TINYPY_ABI_VERSION;
    host.artifact.struct_size = (uint32_t)sizeof(host.artifact);
    host.artifact.content_kind = TINYPY_MODULE_CONTENT_SOURCE;
    host.artifact.data = valid_source;
    host.artifact.data_size = sizeof(valid_source) - 1U;
    host.artifact.canonical_name = "source_module";
    host.artifact.canonical_name_size = 13U;
    host.artifact.logical_filename = "source_module.py";
    host.artifact.logical_filename_size = 16U;
    vm = __test_vm_create_with_source_host(&state, &host);
    module = tinypy_import_module(vm, "source_module", 13U, NULL, NULL, 0, &error);
    assert(module != NULL && error == NULL);
    assert(tinypy_integer_as_i64(__test_dict_get(vm, tinypy_module_dict(module), "value", 5U)) == 42);
    tinypy_release(module);
    assert(host.resolve_count == 1U && host.release_count == 1U);
    host.artifact.canonical_name = "broken_module";
    host.artifact.canonical_name_size = 13U;
    host.artifact.logical_filename = "broken_module.py";
    host.artifact.logical_filename_size = 16U;
    host.artifact.data = invalid_source;
    host.artifact.data_size = sizeof(invalid_source) - 1U;
    module = tinypy_import_module(vm, "broken_module", 13U, NULL, NULL, 0, &error);
    assert(module == NULL && error != NULL && tinypy_error_kind(error) == TINYPY_ERROR_SYNTAX);
    tinypy_error_release(error);
    modules = tinypy_vm_modules(vm);
    key = tinypy_string_from_bytes(vm, "broken_module", 13U);
    assert(tinypy_dict_contains(modules, key) == 0);
    tinypy_release(key);
    tinypy_vm_destroy(vm);
    assert(state.allocations == 0U && state.bytes == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
static int __test_source_package_and_circular_imports(void) {
    static const char cycle_a_source[] = "import cycle_b\nvalue = cycle_b.value + 1\n";
    static const char cycle_b_source[] = "import cycle_a\nvalue = 41\n";
    static const char package_source[] = "from . import child\nvalue = child.value\n";
    static const char child_source[] = "value = 42\n";
    static const char keyword_import_source[] = "module = __import__('source_pkg.child', fromlist=['source_pkg'])\nvalue = module.value\n";
    test_allocator_state_t state = {0U, 0U};
    tinypy_module_artifact_t artifacts[5];
    test_source_multi_host_t host;
    tinypy_vm_t *vm;
    tinypy_value_t *module;
    tinypy_error_t *error = NULL;

    (void)memset(artifacts, 0, sizeof(artifacts));
    artifacts[0].abi_version = TINYPY_ABI_VERSION;
    artifacts[0].struct_size = (uint32_t)sizeof(artifacts[0]);
    artifacts[0].content_kind = TINYPY_MODULE_CONTENT_SOURCE;
    artifacts[0].data = cycle_a_source;
    artifacts[0].data_size = sizeof(cycle_a_source) - 1U;
    artifacts[0].canonical_name = "cycle_a";
    artifacts[0].canonical_name_size = 7U;
    artifacts[0].logical_filename = "cycle_a.py";
    artifacts[0].logical_filename_size = 10U;
    artifacts[1] = artifacts[0];
    artifacts[1].data = cycle_b_source;
    artifacts[1].data_size = sizeof(cycle_b_source) - 1U;
    artifacts[1].canonical_name = "cycle_b";
    artifacts[1].logical_filename = "cycle_b.py";
    artifacts[2] = artifacts[0];
    artifacts[2].data = package_source;
    artifacts[2].data_size = sizeof(package_source) - 1U;
    artifacts[2].canonical_name = "source_pkg";
    artifacts[2].canonical_name_size = 10U;
    artifacts[2].logical_filename = "source_pkg/__init__.py";
    artifacts[2].logical_filename_size = 22U;
    artifacts[2].flags = TINYPY_MODULE_ARTIFACT_PACKAGE;
    artifacts[3] = artifacts[0];
    artifacts[3].data = child_source;
    artifacts[3].data_size = sizeof(child_source) - 1U;
    artifacts[3].canonical_name = "source_pkg.child";
    artifacts[3].canonical_name_size = 16U;
    artifacts[3].logical_filename = "source_pkg/child.py";
    artifacts[3].logical_filename_size = 19U;
    artifacts[4] = artifacts[0];
    artifacts[4].data = keyword_import_source;
    artifacts[4].data_size = sizeof(keyword_import_source) - 1U;
    artifacts[4].canonical_name = "keyword_import";
    artifacts[4].canonical_name_size = 14U;
    artifacts[4].logical_filename = "keyword_import.py";
    artifacts[4].logical_filename_size = 17U;
    (void)memset(&host, 0, sizeof(host));
    host.artifacts = artifacts;
    host.artifact_count = sizeof(artifacts) / sizeof(artifacts[0]);
    vm = __test_vm_create_with_source_multi_host(&state, &host);

    module = tinypy_import_module(vm, "cycle_a", 7U, NULL, NULL, 0, &error);
    assert(module != NULL && error == NULL);
    assert(tinypy_integer_as_i64(__test_dict_get(vm, tinypy_module_dict(module), "value", 5U)) == 42);
    tinypy_release(module);
    module = tinypy_import_module(vm, "source_pkg", 10U, NULL, NULL, 0, &error);
    assert(module != NULL && error == NULL);
    assert(tinypy_integer_as_i64(__test_dict_get(vm, tinypy_module_dict(module), "value", 5U)) == 42);
    tinypy_release(module);
    module = tinypy_import_module(vm, "keyword_import", 14U, NULL, NULL, 0, &error);
    assert(module != NULL && error == NULL);
    assert(tinypy_integer_as_i64(__test_dict_get(vm, tinypy_module_dict(module), "value", 5U)) == 42);
    tinypy_release(module);
    assert(host.resolve_count == 5U && host.release_count == 5U);

    tinypy_vm_destroy(vm);
    assert(state.allocations == 0U && state.bytes == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
static int __test_source_limit(void) {
    test_allocator_state_t state = {0U, 0U};
    tinypy_vm_t *vm = __test_vm_create(&state, 0);
    tinypy_compile_options_t options;
    tinypy_compile_limits_t limits;
    tinypy_error_t *error = NULL;
    tinypy_value_t *code;

    tinypy_compile_limits_init(&limits);
    limits.max_source_bytes = 2U;
    tinypy_compile_options_init(&options, TINYPY_COMPILE_EXEC);
    options.limits = &limits;
    code = tinypy_compile_source(vm, "pass", 4U, "limit.py", 8U, &options, &error);
    assert(code == NULL);
    assert(error != NULL && tinypy_error_kind(error) == TINYPY_ERROR_COMPILER_LIMIT);
    tinypy_error_release(error);
    tinypy_vm_destroy(vm);
    assert(state.allocations == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
static int __test_build_preprocessor(void) {
    static const char source[] = "if __FEATURE__ and (1 + 1 == 2):\n    result = __VALUE__\nelse:\n    missing_runtime_name\nif __NDEBUG__:\n    ndebug = 1\nelse:\n    ndebug = 0\nif True:\n    literal_true = 1\nif False:\n    dead_runtime_name\nelse:\n    literal_false = 0\nruntime_flag = True\nif __FEATURE__ and runtime_flag:\n    mixed = __VALUE__\nprofile_values = (__TEXT__, __FLOAT__, __TUPLE__, __LONG__)\ndynamic_eval = eval('__VALUE__ + 1')\ndynamic_code = compile('__VALUE__ + 2', '<dynamic>', 'eval')\ndynamic_compile = eval(dynamic_code)\nexec 'dynamic_exec = __VALUE__ + 3'\n";
    static const char missing_source[] = "value = __MISSING__\n";
    static const char rebound_source[] = "if False:\n    __FEATURE__ = False\n";
    static const char unicode_text[] = "profile text";
    static const char tuple_text[] = "tuple item";
    static const uint16_t long_digits[] = {UINT16_C(7232), UINT16_C(1)};
    test_allocator_state_t state = {0U, 0U};
    tinypy_allocator_t allocator;
    tinypy_build_value_t tuple_items[2];
    tinypy_build_value_t values[6];
    tinypy_build_constant_t constants[6];
    tinypy_build_constant_t reordered[6];
    tinypy_build_profile_t *profile = NULL;
    tinypy_build_profile_t *reordered_profile = NULL;
    tinypy_build_profile_error_t profile_error;
    tinypy_vm_t *vm;
    tinypy_compile_options_t options;
    tinypy_compile_limits_t limits;
    tinypy_value_t *globals;
    tinypy_value_t *result;
    tinypy_error_t *error = NULL;

    (void)memset(&allocator, 0, sizeof(allocator));
    allocator.abi_version = TINYPY_ABI_VERSION;
    allocator.struct_size = (uint32_t)sizeof(allocator);
    allocator.user_data = &state;
    allocator.allocate = &__test_allocate;
    allocator.reallocate = &__test_reallocate;
    allocator.deallocate = &__test_deallocate;
    tinypy_build_value_init(&values[0], TINYPY_BUILD_VALUE_BOOL);
    values[0].integer_value = 1;
    tinypy_build_value_init(&values[1], TINYPY_BUILD_VALUE_INTEGER);
    values[1].integer_value = 42;
    tinypy_build_value_init(&values[2], TINYPY_BUILD_VALUE_UNICODE);
    values[2].data = unicode_text;
    values[2].data_size = sizeof(unicode_text) - 1U;
    tinypy_build_value_init(&values[3], TINYPY_BUILD_VALUE_FLOAT);
    values[3].float_value = 0.5;
    tinypy_build_value_init(&tuple_items[0], TINYPY_BUILD_VALUE_NONE);
    tinypy_build_value_init(&tuple_items[1], TINYPY_BUILD_VALUE_STRING);
    tuple_items[1].data = tuple_text;
    tuple_items[1].data_size = sizeof(tuple_text) - 1U;
    tinypy_build_value_init(&values[4], TINYPY_BUILD_VALUE_TUPLE);
    values[4].items = tuple_items;
    values[4].item_count = 2U;
    tinypy_build_value_init(&values[5], TINYPY_BUILD_VALUE_LONG);
    values[5].long_digits = long_digits;
    values[5].long_digit_count = 2U;
    values[5].long_sign = 1;
    (void)memset(constants, 0, sizeof(constants));
    constants[0].abi_version = TINYPY_COMPILER_ABI_VERSION;
    constants[0].struct_size = (uint32_t)sizeof(constants[0]);
    constants[0].name = "__FEATURE__";
    constants[0].name_size = 11U;
    constants[0].value = values[0];
    constants[1].abi_version = TINYPY_COMPILER_ABI_VERSION;
    constants[1].struct_size = (uint32_t)sizeof(constants[1]);
    constants[1].name = "__VALUE__";
    constants[1].name_size = 9U;
    constants[1].value = values[1];
    constants[2].abi_version = TINYPY_COMPILER_ABI_VERSION;
    constants[2].struct_size = (uint32_t)sizeof(constants[2]);
    constants[2].name = "__TEXT__";
    constants[2].name_size = 8U;
    constants[2].value = values[2];
    constants[3].abi_version = TINYPY_COMPILER_ABI_VERSION;
    constants[3].struct_size = (uint32_t)sizeof(constants[3]);
    constants[3].name = "__FLOAT__";
    constants[3].name_size = 9U;
    constants[3].value = values[3];
    constants[4].abi_version = TINYPY_COMPILER_ABI_VERSION;
    constants[4].struct_size = (uint32_t)sizeof(constants[4]);
    constants[4].name = "__TUPLE__";
    constants[4].name_size = 9U;
    constants[4].value = values[4];
    constants[5].abi_version = TINYPY_COMPILER_ABI_VERSION;
    constants[5].struct_size = (uint32_t)sizeof(constants[5]);
    constants[5].name = "__LONG__";
    constants[5].name_size = 8U;
    constants[5].value = values[5];
    assert(tinypy_build_profile_create(&allocator, 0, constants, 6U, NULL, &profile, &profile_error) == TINYPY_BUILD_PROFILE_OK); {
        size_t index;

        for (index = 0U; index < 6U; index += 1U) {
            reordered[index] = constants[5U - index];
        }
    }
    assert(tinypy_build_profile_create(&allocator, 0, reordered, 6U, NULL, &reordered_profile, &profile_error) == TINYPY_BUILD_PROFILE_OK);
    assert(tinypy_build_profile_constant_count(profile) == 7U);
    assert(memcmp(tinypy_build_profile_digest(profile), tinypy_build_profile_digest(reordered_profile), TINYPY_BUILD_PROFILE_DIGEST_SIZE) == 0);
    tinypy_build_profile_destroy(reordered_profile);
    reordered_profile = NULL;
    vm = __test_vm_create(&state, 0);
    globals = tinypy_dict_new(vm);
    tinypy_compile_options_init(&options, TINYPY_COMPILE_EXEC);
    options.feature_flags = (uint32_t)TINYPY_COMPILE_FEATURE_PREPROCESSOR;
    options.build_profile = profile;
    result = tinypy_compile_source(vm, missing_source, sizeof(missing_source) - 1U, "missing.py", 10U, &options, &error);
    assert(result == NULL && error != NULL && tinypy_error_kind(error) == TINYPY_ERROR_PREPROCESSOR);
    tinypy_error_release(error);
    error = NULL;
    result = tinypy_compile_source(vm, rebound_source, sizeof(rebound_source) - 1U, "rebound.py", 10U, &options, &error);
    assert(result == NULL && error != NULL && tinypy_error_kind(error) == TINYPY_ERROR_PREPROCESSOR);
    tinypy_error_release(error);
    error = NULL;
    tinypy_compile_limits_init(&limits);
    limits.max_preprocessor_bytes = 1U;
    options.limits = &limits;
    result = tinypy_compile_source(vm, source, sizeof(source) - 1U, "preprocessor.py", 15U, &options, &error);
    assert(result == NULL && error != NULL && tinypy_error_kind(error) == TINYPY_ERROR_COMPILER_LIMIT);
    tinypy_error_release(error);
    error = NULL;
    options.limits = NULL;
    result = tinypy_compile_source(vm, source, sizeof(source) - 1U, "preprocessor.py", 15U, &options, &error);
    assert(result != NULL && error == NULL);
    tinypy_build_profile_destroy(profile);
    profile = NULL; {
        tinypy_value_t *execution_result = tinypy_exec_code(result, globals, NULL, &error);

        assert(execution_result != NULL && error == NULL);
        tinypy_release(execution_result);
    }
    tinypy_release(result);
    assert(tinypy_integer_as_i64(__test_dict_get(vm, globals, "result", 6U)) == 42);
    assert(tinypy_integer_as_i64(__test_dict_get(vm, globals, "ndebug", 6U)) == 0);
    assert(tinypy_integer_as_i64(__test_dict_get(vm, globals, "literal_true", 12U)) == 1);
    assert(tinypy_integer_as_i64(__test_dict_get(vm, globals, "literal_false", 13U)) == 0);
    assert(tinypy_integer_as_i64(__test_dict_get(vm, globals, "mixed", 5U)) == 42); {
        tinypy_value_t *profile_values = __test_dict_get(vm, globals, "profile_values", 14U);
        tinypy_value_t *profile_tuple;
        size_t text_size;
        size_t code_points;
        const char *text;

        assert(tinypy_typeof(profile_values) == TINYPY_VALUE_TUPLE && tinypy_tuple_size(profile_values) == 4U);
        tinypy_value_t *item = tinypy_tuple_get(profile_values, 0U);
        text = tinypy_unicode_utf8_view(item, &text_size, &code_points);
        assert(text_size == sizeof(unicode_text) - 1U && memcmp(text, unicode_text, text_size) == 0);
        assert(tinypy_float_as_double(tinypy_tuple_get(profile_values, 1U)) == 0.5);
        profile_tuple = tinypy_tuple_get(profile_values, 2U);
        assert(tinypy_typeof(profile_tuple) == TINYPY_VALUE_TUPLE && tinypy_tuple_size(profile_tuple) == 2U);
        assert(tinypy_typeof(tinypy_tuple_get(profile_tuple, 0U)) == TINYPY_VALUE_NONE);
        assert(tinypy_typeof(tinypy_tuple_get(profile_tuple, 1U)) == TINYPY_VALUE_STRING);
        assert(tinypy_typeof(tinypy_tuple_get(profile_values, 3U)) == TINYPY_VALUE_LONG && tinypy_long_as_i64(tinypy_tuple_get(profile_values, 3U)) == 40000);
    }
    assert(tinypy_integer_as_i64(__test_dict_get(vm, globals, "dynamic_eval", 12U)) == 43);
    assert(tinypy_integer_as_i64(__test_dict_get(vm, globals, "dynamic_compile", 15U)) == 44);
    assert(tinypy_integer_as_i64(__test_dict_get(vm, globals, "dynamic_exec", 12U)) == 45);
    tinypy_release(globals);
    tinypy_vm_destroy(vm);
    if (profile != NULL) {
        tinypy_build_profile_destroy(profile);
    }
    if (reordered_profile != NULL) {
        tinypy_build_profile_destroy(reordered_profile);
    }
    assert(state.allocations == 0U && state.bytes == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
static int __test_preprocess_renderer(void) {
    static const char source[] = "from __future__ import division, with_statement\nimport package.module as module\nfrom package import value as imported\n\nglobal_value = 1\nunicode_value = u'\\u041f\\u0440\\u0438\\u0432\\u0435\\u0442'\ninfinite = 1e400\ncomplex_infinite = 1e400j\n\ndef generator(argument, optional=2, *args, **kwargs):\n    global global_value\n    assert argument, 'argument'\n    target = lambda item=1: item + optional\n    sequence = [item for item in (1, 2, 3) if item]\n    mapping = {item: item * 2 for item in sequence}\n    unique = {item for item in sequence}\n    sliced = sequence[0:2:1]\n    extended = sequence[0:1, ...]\n    representation = `mapping`\n    print >>kwargs['stream'], representation,\n    exec kwargs['code'] in kwargs, mapping\n    try:\n        with kwargs['context'] as context_value:\n            yield context_value\n    except ValueError as error:\n        raise TypeError, error, None\n    else:\n        pass\n    finally:\n        global_value += 1\n    del mapping[argument]\n\n@decorator\nclass Example(object):\n    @staticmethod\n    def method():\n        return u'value'\n";
    test_allocator_state_t state = {0U, 0U};
    tinypy_vm_t *vm = __test_vm_create(&state, 0);
    tinypy_compile_options_t options;
    tinypy_compile_limits_t limits;
    tinypy_preprocess_result_t *preprocessed;
    tinypy_value_t *code;
    tinypy_error_t *error = NULL;
    const char *expanded;
    size_t expanded_size;

    tinypy_compile_options_init(&options, TINYPY_COMPILE_EXEC);
    tinypy_compile_limits_init(&limits);
    limits.max_generated_source_bytes = 1U;
    options.limits = &limits;
    preprocessed = tinypy_preprocess_source(vm, source, sizeof(source) - 1U, "renderer.py", 11U, &options, &error);
    assert(preprocessed == NULL && error != NULL && tinypy_error_kind(error) == TINYPY_ERROR_COMPILER_LIMIT);
    tinypy_error_release(error);
    error = NULL;
    options.limits = NULL;
    preprocessed = tinypy_preprocess_source(vm, source, sizeof(source) - 1U, "renderer.py", 11U, &options, &error);
    assert(preprocessed != NULL && error == NULL);
    assert(tinypy_preprocess_result_source_map_count(preprocessed) == 0U);
    expanded = tinypy_preprocess_result_expanded_source(preprocessed, &expanded_size);
    assert(strstr(expanded, "u'\\u041f\\u0440\\u0438\\u0432\\u0435\\u0442'") != NULL);
    code = tinypy_compile_source(vm, expanded, expanded_size, "renderer.expanded.py", 20U, &options, &error);
    assert(code != NULL && error == NULL);
    tinypy_release(code);
    tinypy_preprocess_result_destroy(preprocessed);
    tinypy_vm_destroy(vm);
    assert(state.allocations == 0U && state.bytes == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
static int __test_meta_template(void) {
    static const char source[] = "class Base(object):\n    def initialize(self):\n        self.base = 1\n\n@meta.template\ndef ObjectTemplate(TypeName):\n    ClassName = meta.concat('Mixin', TypeName)\n\n    @meta.emit(name=ClassName)\n    class Generated(Base):\n        def initialize(self):\n            super(meta.current_class(), self).initialize()\n            meta.setattr(self, TypeName, 42)\n\n@meta.template\ndef PairTemplate(Prefix):\n    @meta.emit(name=meta.concat(Prefix, 'Class'))\n    class GeneratedClass(object):\n        values = [value for value in (1, 2, 3)]\n\n    @meta.emit(name=meta.concat(Prefix, 'Function'))\n    def generated_function():\n        return 7\n\nMixinItem = meta.expand(ObjectTemplate, 'Item')\nPairClass, PairFunction = meta.expand(PairTemplate, 'Pair')\nobj = MixinItem()\nobj.initialize()\ndynamic = eval('40 + 2')\ndynamic_debug = eval('__debug__')\nresult = (MixinItem.__name__, obj.Item, obj.base, dynamic, PairClass.values, PairFunction(), dynamic_debug)\n";
    static const char bare_meta_source[] = "@meta\ndef Template():\n    pass\n";
    static const char invalid_source[] = "value = meta.unknown()\n";
    test_allocator_state_t state = {0U, 0U};
    tinypy_vm_t *vm;
    tinypy_compile_options_t options;
    tinypy_compile_options_t expanded_options;
    tinypy_compile_limits_t limits;
    tinypy_preprocess_result_t *preprocessed;
    tinypy_value_t *globals;
    tinypy_value_t *execution_result;
    tinypy_value_t *result;
    tinypy_error_t *error = NULL;
    size_t name_size;
    size_t expanded_size;
    size_t source_map_size;
    size_t source_map_count;
    const char *name;
    const char *expanded_source;
    const char *source_map;
    tinypy_source_map_entry_t source_map_entry;

    vm = __test_vm_create(&state, 0);
    globals = tinypy_dict_new(vm);
    tinypy_compile_options_init(&options, TINYPY_COMPILE_EXEC);
    options.feature_flags = (uint32_t)TINYPY_COMPILE_FEATURE_META;
    options.optimize_level = 1;
    tinypy_compile_limits_init(&limits);
    limits.max_template_expansions = 1U;
    options.limits = &limits;
    execution_result = tinypy_compile_source(vm, source, sizeof(source) - 1U, "meta.py", 7U, &options, &error);
    assert(execution_result == NULL && error != NULL && tinypy_error_kind(error) == TINYPY_ERROR_COMPILER_LIMIT);
    tinypy_error_release(error);
    error = NULL;
    options.limits = NULL;
    preprocessed = tinypy_preprocess_source(vm, source, sizeof(source) - 1U, "meta.py", 7U, &options, &error);
    assert(preprocessed != NULL && error == NULL);
    expanded_source = tinypy_preprocess_result_expanded_source(preprocessed, &expanded_size);
    source_map = (const char *)tinypy_preprocess_result_source_map(preprocessed, &source_map_size);
    source_map_count = tinypy_preprocess_result_source_map_count(preprocessed);
    assert(expanded_size != 0U && strstr(expanded_source, "class MixinItem(Base):") != NULL);
    assert(strstr(expanded_source, "@meta") == NULL && strstr(expanded_source, "meta.") == NULL);
    assert(source_map_size > 21U && memcmp(source_map, "tinypy-source-map-v1\n", 21U) == 0);
    assert(source_map_count == 3U);
    tinypy_preprocess_result_source_map_at(preprocessed, 0U, &source_map_entry);
    assert(source_map_entry.generated_line > 0 && source_map_entry.template_line > 0 && source_map_entry.expansion_line > 0);
    assert(source_map_entry.generated_symbol_size != 0U);
    tinypy_compile_options_init(&expanded_options, TINYPY_COMPILE_EXEC);
    execution_result = tinypy_exec_source(vm, expanded_source, expanded_size, "meta.expanded.py", 16U, globals, NULL, &expanded_options, &error);
    assert(execution_result != NULL && error == NULL);
    tinypy_release(execution_result);
    tinypy_preprocess_result_destroy(preprocessed);
    execution_result = tinypy_exec_source(vm, source, sizeof(source) - 1U, "meta.py", 7U, globals, NULL, &options, &error);
    assert(execution_result != NULL && error == NULL);
    tinypy_release(execution_result);
    result = __test_dict_get(vm, globals, "result", 6U);
    assert(tinypy_typeof(result) == TINYPY_VALUE_TUPLE && tinypy_tuple_size(result) == 7U);
    tinypy_value_t *item = tinypy_tuple_get(result, 0U);
    name = (const char *)tinypy_string_view(item, &name_size);
    assert(name_size == 9U && memcmp(name, "MixinItem", 9U) == 0);
    assert(tinypy_integer_as_i64(tinypy_tuple_get(result, 1U)) == 42);
    assert(tinypy_integer_as_i64(tinypy_tuple_get(result, 2U)) == 1);
    assert(tinypy_integer_as_i64(tinypy_tuple_get(result, 3U)) == 42);
    assert(tinypy_typeof(tinypy_tuple_get(result, 4U)) == TINYPY_VALUE_LIST && tinypy_list_size(tinypy_tuple_get(result, 4U)) == 3U);
    assert(tinypy_integer_as_i64(tinypy_tuple_get(result, 5U)) == 7);
    assert(tinypy_bool_as_i32(tinypy_tuple_get(result, 6U)) == 0);
    execution_result = tinypy_compile_source(vm, bare_meta_source, sizeof(bare_meta_source) - 1U, "bare_meta.py", 12U, &options, &error);
    assert(execution_result == NULL && error != NULL && tinypy_error_kind(error) == TINYPY_ERROR_META);
    tinypy_error_release(error);
    error = NULL;
    execution_result = tinypy_compile_source(vm, invalid_source, sizeof(invalid_source) - 1U, "invalid_meta.py", 15U, &options, &error);
    assert(execution_result == NULL && error != NULL && tinypy_error_kind(error) == TINYPY_ERROR_META);
    tinypy_error_release(error);
    tinypy_release(globals);
    tinypy_vm_destroy(vm);
    assert(state.allocations == 0U && state.bytes == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
static int __test_compile_environment_imports(void) {
    static const char source[] = "value = __IMPORT_VALUE__\ndynamic = eval('__IMPORT_VALUE__ + 1')\n";
    test_allocator_state_t state = {0U, 0U};
    tinypy_allocator_t allocator;
    test_source_host_t host;
    tinypy_build_value_t build_value;
    tinypy_build_constant_t constant;
    tinypy_build_profile_t *profile = NULL;
    tinypy_vm_t *vm;
    tinypy_compile_options_t options;
    tinypy_value_t *code;
    tinypy_value_t *module;
    tinypy_error_t *error = NULL;
    unsigned char *marshal_data;
    size_t marshal_size;

    (void)memset(&allocator, 0, sizeof(allocator));
    allocator.abi_version = TINYPY_ABI_VERSION;
    allocator.struct_size = (uint32_t)sizeof(allocator);
    allocator.user_data = &state;
    allocator.allocate = &__test_allocate;
    allocator.reallocate = &__test_reallocate;
    allocator.deallocate = &__test_deallocate;
    tinypy_build_value_init(&build_value, TINYPY_BUILD_VALUE_INTEGER);
    build_value.integer_value = 41;
    (void)memset(&constant, 0, sizeof(constant));
    constant.abi_version = TINYPY_COMPILER_ABI_VERSION;
    constant.struct_size = (uint32_t)sizeof(constant);
    constant.name = "__IMPORT_VALUE__";
    constant.name_size = 16U;
    constant.value = build_value;
    assert(tinypy_build_profile_create(&allocator, 0, &constant, 1U, NULL, &profile, NULL) == TINYPY_BUILD_PROFILE_OK);
    (void)memset(&host, 0, sizeof(host));
    host.artifact.abi_version = TINYPY_ABI_VERSION;
    host.artifact.struct_size = (uint32_t)sizeof(host.artifact);
    host.artifact.content_kind = TINYPY_MODULE_CONTENT_SOURCE;
    host.artifact.data = source;
    host.artifact.data_size = sizeof(source) - 1U;
    host.artifact.canonical_name = "profiled_source";
    host.artifact.canonical_name_size = 15U;
    host.artifact.logical_filename = "profiled_source.py";
    host.artifact.logical_filename_size = 18U;
    host.artifact.compile_feature_flags = (uint32_t)TINYPY_COMPILE_FEATURE_PREPROCESSOR;
    host.artifact.compile_optimize_level = 0;
    host.artifact.build_profile = profile;
    vm = __test_vm_create_with_source_host(&state, &host);
    module = tinypy_import_module(vm, "profiled_source", 15U, NULL, NULL, 0, &error);
    assert(module != NULL && error == NULL);
    assert(tinypy_integer_as_i64(__test_dict_get(vm, tinypy_module_dict(module), "value", 5U)) == 41);
    assert(tinypy_integer_as_i64(__test_dict_get(vm, tinypy_module_dict(module), "dynamic", 7U)) == 42);
    tinypy_release(module);

    tinypy_compile_options_init(&options, TINYPY_COMPILE_EXEC);
    options.feature_flags = (uint32_t)TINYPY_COMPILE_FEATURE_PREPROCESSOR;
    options.build_profile = profile;
    code = tinypy_compile_source(vm, source, sizeof(source) - 1U, "profiled_marshal.py", 19U, &options, &error);
    assert(code != NULL && error == NULL);
    assert(tinypy_marshal_dump_code_v2(code, NULL, 0U, &marshal_size, NULL, NULL) == TINYPY_MARSHAL_OK);
    marshal_data = (unsigned char *)malloc(marshal_size);
    assert(marshal_data != NULL);
    assert(tinypy_marshal_dump_code_v2(code, marshal_data, marshal_size, &marshal_size, NULL, NULL) == TINYPY_MARSHAL_OK);
    tinypy_release(code);
    host.artifact.content_kind = TINYPY_MODULE_CONTENT_MARSHAL_V2;
    host.artifact.data = marshal_data;
    host.artifact.data_size = marshal_size;
    host.artifact.canonical_name = "profiled_marshal";
    host.artifact.canonical_name_size = 16U;
    host.artifact.logical_filename = "profiled_marshal.py";
    host.artifact.logical_filename_size = 19U;
    module = tinypy_import_module(vm, "profiled_marshal", 16U, NULL, NULL, 0, &error);
    assert(module != NULL && error == NULL);
    tinypy_build_profile_destroy(profile);
    profile = NULL;
    assert(tinypy_integer_as_i64(__test_dict_get(vm, tinypy_module_dict(module), "value", 5U)) == 41);
    assert(tinypy_integer_as_i64(__test_dict_get(vm, tinypy_module_dict(module), "dynamic", 7U)) == 42);
    tinypy_release(module);
    assert(host.resolve_count == 2U && host.release_count == 2U);
    free(marshal_data);
    tinypy_vm_destroy(vm);
    if (profile != NULL) {
        tinypy_build_profile_destroy(profile);
    }
    assert(state.allocations == 0U && state.bytes == 0U);
    return 0;
}
//////////////////////////////////////////////////////////////////////////
int main(void) {
    if (__test_empty_exec() != 0) {
        return EXIT_FAILURE;
    }
    if (__test_source_diagnostic() != 0) {
        return EXIT_FAILURE;
    }
    if (__test_source_decoding() != 0) {
        return EXIT_FAILURE;
    }
    if (__test_named_unicode_escapes() != 0) {
        return EXIT_FAILURE;
    }
    if (__test_source_limit() != 0) {
        return EXIT_FAILURE;
    }
    if (__test_private_name_mangling() != 0) {
        return EXIT_FAILURE;
    }
    if (__test_legacy_vm_config_optimize_default() != 0) {
        return EXIT_FAILURE;
    }
    if (__test_compile_marshal_parity() != 0) {
        return EXIT_FAILURE;
    }
    if (__test_exec_eval_and_dynamic_compile() != 0) {
        return EXIT_FAILURE;
    }
    if (__test_runtime_cache_invalidation() != 0) {
        return EXIT_FAILURE;
    }
    if (__test_dictionary_iteration_semantics() != 0) {
        return EXIT_FAILURE;
    }
    if (__test_runtime_interrupt_polling() != 0) {
        return EXIT_FAILURE;
    }
    if (__test_compile_modes_and_optimize() != 0) {
        return EXIT_FAILURE;
    }
    if (__test_host_call_eval_without_frame() != 0) {
        return EXIT_FAILURE;
    }
    if (__test_indentation_diagnostics() != 0) {
        return EXIT_FAILURE;
    }
    if (__test_dont_imply_dedent() != 0) {
        return EXIT_FAILURE;
    }
    if (__test_future_and_reserved_debug() != 0) {
        return EXIT_FAILURE;
    }
    if (__test_symtable_and_ast_error_messages() != 0) {
        return EXIT_FAILURE;
    }
    if (__test_codegen_syntax_errors() != 0) {
        return EXIT_FAILURE;
    }
    if (__test_reentrant_compile() != 0) {
        return EXIT_FAILURE;
    }
    if (__test_parallel_compile() != 0) {
        return EXIT_FAILURE;
    }
    if (__test_compiler_limits() != 0) {
        return EXIT_FAILURE;
    }
    if (__test_source_import_and_rollback() != 0) {
        return EXIT_FAILURE;
    }
    if (__test_source_package_and_circular_imports() != 0) {
        return EXIT_FAILURE;
    }
    if (__test_build_preprocessor() != 0) {
        return EXIT_FAILURE;
    }
    if (__test_preprocess_renderer() != 0) {
        return EXIT_FAILURE;
    }
    if (__test_meta_template() != 0) {
        return EXIT_FAILURE;
    }
    if (__test_compile_environment_imports() != 0) {
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
