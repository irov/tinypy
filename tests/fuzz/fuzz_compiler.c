#include "tinypy/tinypy.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

typedef struct fuzz_allocator_state_t {
    size_t allocations;
    size_t bytes;
} fuzz_allocator_state_t;

//////////////////////////////////////////////////////////////////////////
static void *__fuzz_allocate(void *user_data, size_t size, size_t alignment, tinypy_allocation_tag_e tag) {
    fuzz_allocator_state_t *state = (fuzz_allocator_state_t *)user_data;
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
static void *__fuzz_reallocate(void *user_data, void *memory, size_t old_size, size_t new_size, size_t alignment, tinypy_allocation_tag_e tag) {
    fuzz_allocator_state_t *state = (fuzz_allocator_state_t *)user_data;
    void *resized;

    (void)alignment;
    (void)tag;
    resized = realloc(memory, new_size);
    assert(resized != NULL);
    assert(state->bytes >= old_size);
    state->bytes -= old_size;
    state->bytes += new_size;
    return resized;
}
//////////////////////////////////////////////////////////////////////////
static void __fuzz_deallocate(void *user_data, void *memory, size_t size, size_t alignment, tinypy_allocation_tag_e tag) {
    fuzz_allocator_state_t *state = (fuzz_allocator_state_t *)user_data;

    (void)alignment;
    (void)tag;
    assert(state->allocations != 0U && state->bytes >= size);
    state->allocations -= 1U;
    state->bytes -= size;
    free(memory);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

//////////////////////////////////////////////////////////////////////////
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    fuzz_allocator_state_t allocator_state = {0U, 0U};
    tinypy_allocator_t allocator;
    tinypy_vm_config_t config;
    tinypy_compile_limits_t limits;
    tinypy_compile_options_t options;
    tinypy_compile_mode_e mode = TINYPY_COMPILE_EXEC;
    tinypy_build_profile_t *profile = NULL;
    tinypy_vm_t *vm;
    tinypy_value_t *code;
    tinypy_preprocess_result_t *preprocessed;
    tinypy_error_t *error = NULL;
    uint8_t selector = size != 0U ? data[0] : 0U;

    (void)memset(&allocator, 0, sizeof(allocator));
    allocator.abi_version = TINYPY_ABI_VERSION;
    allocator.struct_size = (uint32_t)sizeof(allocator);
    allocator.user_data = &allocator_state;
    allocator.allocate = &__fuzz_allocate;
    allocator.reallocate = &__fuzz_reallocate;
    allocator.deallocate = &__fuzz_deallocate;
    (void)memset(&config, 0, sizeof(config));
    config.abi_version = TINYPY_ABI_VERSION;
    config.struct_size = (uint32_t)sizeof(config);
    config.allocator = &allocator;
    vm = tinypy_vm_create(&config);
    tinypy_compile_limits_init(&limits);
    limits.max_source_bytes = 64U * 1024U;
    limits.max_tokens = 4096U;
    limits.max_cst_nodes = 4096U;
    limits.max_ast_nodes = 4096U;
    limits.max_nesting = 128U;
    limits.max_symbols = 4096U;
    limits.max_blocks = 4096U;
    limits.max_instructions = 16384U;
    limits.max_constants = 4096U;
    limits.max_constant_bytes = 64U * 1024U;
    limits.max_arena_bytes = 8U * 1024U * 1024U;
    limits.max_preprocessor_operations = 4096U;
    limits.max_preprocessor_value_nodes = 4096U;
    limits.max_preprocessor_bytes = 64U * 1024U;
    limits.max_template_expansions = 256U;
    limits.max_template_depth = 32U;
    limits.max_generated_ast_nodes = 4096U;
    limits.max_generated_source_bytes = 64U * 1024U;
    limits.max_source_map_entries = 256U;
    if (size != 0U) {
        mode = (tinypy_compile_mode_e)(TINYPY_COMPILE_EXEC + selector % 3U);
    }
    tinypy_compile_options_init(&options, mode);
    options.limits = &limits;
    options.feature_flags = (uint32_t)((selector / 3U) % 4U);
    if ((options.feature_flags & (uint32_t)TINYPY_COMPILE_FEATURE_PREPROCESSOR) != 0U) {
        assert(tinypy_build_profile_create(&allocator, 0, NULL, 0U, NULL, &profile, NULL) == TINYPY_BUILD_PROFILE_OK);
        options.build_profile = profile;
    }
    if ((selector & UINT8_C(0x80)) != 0U) {
        preprocessed = tinypy_preprocess_source(vm, size != 0U ? data + 1U : data, size != 0U ? size - 1U : 0U, "fuzz.py", 7U, &options, &error);
        if (preprocessed != NULL) {
            tinypy_preprocess_result_destroy(preprocessed);
        }
    }
    else {
        code = tinypy_compile_source(vm, size != 0U ? data + 1U : data, size != 0U ? size - 1U : 0U, "fuzz.py", 7U, &options, &error);
        if (code != NULL) {
            tinypy_release(code);
        }
    }
    if (error != NULL) {
        tinypy_error_release(error);
    }
    if (profile != NULL) {
        tinypy_build_profile_destroy(profile);
    }
    tinypy_vm_destroy(vm);
    assert(allocator_state.allocations == 0U && allocator_state.bytes == 0U);
    return 0;
}

#if defined(TINYPY_FUZZ_STANDALONE)
static uint64_t __fuzz_random(uint64_t *state) {
    uint64_t value = *state;

    value ^= value << 13U;
    value ^= value >> 7U;
    value ^= value << 17U;
    *state = value;
    return value;
}

int main(void) {
    static const uint8_t seeds[][48] = { {0U},
        {0U, 'p', 'a', 's', 's', '\n'}, {1U, '1', '+', '2'},
        {2U, 'i', 'f', ' ', 'T', 'r', 'u', 'e', ':', '\n'}, {0U, '#', ' ', 'c', 'o', 'd', 'i', 'n', 'g', ':', ' ', 'u', 't', 'f', '-', '8', '\n'},
        {0U, 0xefU, 0xbbU, 0xbfU, 'v', 'a', 'l', 'u', 'e', '=', '1', '\n'}, {0U, 'u', '\'', '\\', 'N', '{', 'L', 'A', 'T', 'I', 'N', ' ', 'S', 'M', 'A', 'L', 'L', ' ', 'L', 'E', 'T', 'T', 'E', 'R', ' ', 'A', '}', '\'', '\n'},
        {0U, 'd', 'e', 'f', ' ', 'f', '(', 'x', ')', ':', '\n', ' ', ' ', ' ', ' ', 'r', 'e', 't', 'u', 'r', 'n', ' ', 'x', '+', '1', '\n'}};
    static const size_t seed_sizes[] = {1U, 6U, 4U, 10U, 18U, 12U, 29U, 26U};
    uint8_t buffer[513];
    uint64_t random_state = UINT64_C(0x6a09e667f3bcc909);
    size_t index;

    for (index = 0U; index < sizeof(seeds) / sizeof(seeds[0]); index += 1U) {
        (void)LLVMFuzzerTestOneInput(seeds[index], seed_sizes[index]);
    }
    for (index = 0U; index < 512U; index += 1U) {
        size_t size = (size_t)(__fuzz_random(&random_state) % sizeof(buffer));
        size_t byte_index;

        for (byte_index = 0U; byte_index < size; ++byte_index) {
            buffer[byte_index] = (uint8_t)__fuzz_random(&random_state);
        }
        (void)LLVMFuzzerTestOneInput(buffer, size);
    }
    return EXIT_SUCCESS;
}
#endif
