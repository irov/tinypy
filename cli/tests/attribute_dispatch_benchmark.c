#include "tinypy/tinypy.h"

/* Standalone benchmark for direct dispatch and complete C attribute lookup.
 * Compile against a Release archive. CPU timings exclude VM creation and
 * compilation; this file is not part of the embedding runtime. */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#ifndef TINYPY_ATTRIBUTE_BENCHMARK_SCALE
#define TINYPY_ATTRIBUTE_BENCHMARK_SCALE 1U
#endif

extern tinypy_value_t *tinypy_internal_object_builtin_attribute(tinypy_value_t *value, tinypy_value_t *key);
static void *__benchmark_allocate(void *data, size_t size, size_t alignment) {
    (void)data;
    (void)alignment;
    void *result = malloc(size);
    return result;
}
static void *__benchmark_reallocate(void *data, void *memory, size_t old_size, size_t size, size_t alignment) {
    (void)data;
    (void)old_size;
    (void)alignment;
    void *result = realloc(memory, size);
    return result;
}
static void __benchmark_deallocate(void *data, void *memory, size_t size, size_t alignment) {
    (void)data;
    (void)size;
    (void)alignment;
    free(memory);
}
int main(void) {
    tinypy_allocator_t allocator = {0};
    allocator.abi_version = TINYPY_ABI_VERSION;
    allocator.struct_size = (uint32_t)sizeof(allocator);
    allocator.allocate = __benchmark_allocate;
    allocator.reallocate = __benchmark_reallocate;
    allocator.deallocate = __benchmark_deallocate;
    tinypy_vm_config_t config = {0};
    config.abi_version = TINYPY_ABI_VERSION;
    config.struct_size = (uint32_t)sizeof(config);
    config.allocator = &allocator;
    tinypy_vm_t *vm = tinypy_vm_create(&config);
    tinypy_value_t *globals = tinypy_dict_new(vm);
    tinypy_compile_options_t options;
    tinypy_compile_options_init(&options, TINYPY_COMPILE_EXEC);
    static const char source[] = "import sys\ndef f(x):\n    return x\ncode = f.func_code\nitems = []\ntype_object = type\n";
    tinypy_error_t *error = NULL;
    tinypy_value_t *result = tinypy_exec_source(vm, source, sizeof(source) - 1U, "<bench>", 7U, globals, globals, &options, &error);
    if (result == NULL || error != NULL) {
        return 2;
    }
    tinypy_release(result);
    static const struct {
        const char *object;
        const char *attribute;
    } cases[] = {
        {"items", "append"}, {"sys", "stdout"}, {"f", "func_code"}, {"f", "func_dict"}, {"code", "co_varnames"}, {"code", "co_lnotab"}, {"type_object", "__weakrefoffset__"}, {"items", "__class__"}, {"code", "unknown_attribute"}, {"f", "__module__"}};
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        tinypy_value_t *object_key = tinypy_string_from_bytes(vm, cases[i].object, strlen(cases[i].object));
        tinypy_value_t *object = tinypy_dict_get(globals, object_key);
        tinypy_value_t *key = tinypy_string_from_bytes(vm, cases[i].attribute, strlen(cases[i].attribute));
        tinypy_value_t *expected = tinypy_internal_object_builtin_attribute(object, key);
        tinypy_bool_t expect_dispatch = i == 0U || i == 1U || i == 8U ? TINYPY_FALSE : TINYPY_TRUE;
        if ((expected != NULL) != (expect_dispatch != TINYPY_FALSE)) {
            return 4;
        }
        if (expected != NULL) {
            tinypy_release(expected);
        }
        for (int32_t full = 0; full < 2; ++full) {
            if (full != 0 && expected == NULL && strcmp(cases[i].attribute, "unknown_attribute") == 0) {
                continue;
            }
            clock_t begin = clock();
            size_t hits = 0;
            size_t count = (full ? 500000U : 5000000U) * (size_t)TINYPY_ATTRIBUTE_BENCHMARK_SCALE;
            for (size_t j = 0; j < count; ++j) {
                tinypy_value_t *value = full ? tinypy_object_get_attr_value(object, key, &error) : tinypy_internal_object_builtin_attribute(object, key);
                if (error != NULL) {
                    return 3;
                }
                if (value != NULL) {
                    ++hits;
                    tinypy_release(value);
                }
            }
            if (hits != ((full != 0 || expect_dispatch != TINYPY_FALSE) ? count : 0U)) {
                return 5;
            }
            clock_t finish = clock();
            (void)printf("%s.%s/%s\t%.9f\t%zu\n", cases[i].object, cases[i].attribute, full ? "full" : "dispatch", (double)(finish - begin) / (double)CLOCKS_PER_SEC, hits);
        }
        tinypy_release(key);
        tinypy_release(object_key);
    }
    tinypy_dict_clear(globals);
    tinypy_release(globals);
    tinypy_vm_destroy(vm);
    return 0;
}
