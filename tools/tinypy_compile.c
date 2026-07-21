#include "tinypy/tinypy.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//////////////////////////////////////////////////////////////////////////
static void *__tool_allocate(void *user_data, size_t size, size_t alignment, uint32_t tag) {
    (void)user_data;
    (void)alignment;
    (void)tag;
    return malloc(size);
}

//////////////////////////////////////////////////////////////////////////
static void *__tool_reallocate(void *user_data, void *memory, size_t old_size, size_t new_size, size_t alignment, uint32_t tag) {
    (void)user_data;
    (void)old_size;
    (void)alignment;
    (void)tag;
    return realloc(memory, new_size);
}

//////////////////////////////////////////////////////////////////////////
static void __tool_deallocate(void *user_data, void *memory, size_t size, size_t alignment, uint32_t tag) {
    (void)user_data;
    (void)size;
    (void)alignment;
    (void)tag;
    free(memory);
}

//////////////////////////////////////////////////////////////////////////
static int32_t __tool_mode(const char *text, tinypy_compile_mode_e *out_mode) {
    if (strcmp(text, "exec") == 0) {
        *out_mode = TINYPY_COMPILE_EXEC;
    }
    else if (strcmp(text, "eval") == 0) {
        *out_mode = TINYPY_COMPILE_EVAL;
    }
    else if (strcmp(text, "single") == 0) {
        *out_mode = TINYPY_COMPILE_SINGLE;
    }
    else {
        return INT32_C(0);
    }
    return INT32_C(1);
}

//////////////////////////////////////////////////////////////////////////
static unsigned char *__tool_read(const char *path, size_t *out_size) {
    FILE *stream = fopen(path, "rb");
    long length;
    unsigned char *data;

    if (stream == NULL) {
        return NULL;
    }
    if (fseek(stream, 0L, SEEK_END) != 0) {
        (void)fclose(stream);
        return NULL;
    }
    length = ftell(stream);
    if (length < 0L || fseek(stream, 0L, SEEK_SET) != 0) {
        (void)fclose(stream);
        return NULL;
    }
    data = (unsigned char *)malloc((size_t)length == 0U ? 1U : (size_t)length);
    assert(data != NULL);
    if ((size_t)length != 0U && fread(data, 1U, (size_t)length, stream) != (size_t)length) {
        free(data);
        (void)fclose(stream);
        return NULL;
    }
    (void)fclose(stream);
    *out_size = (size_t)length;
    return data;
}

//////////////////////////////////////////////////////////////////////////
static int32_t __tool_write(const char *path, const void *data, size_t size) {
    FILE *stream = fopen(path, "wb");
    int32_t result;

    if (stream == NULL) {
        return INT32_C(0);
    }
    result = size == 0U || fwrite(data, 1U, size, stream) == size ? INT32_C(1) : INT32_C(0);
    if (fclose(stream) != 0) {
        result = INT32_C(0);
    }
    return result;
}

//////////////////////////////////////////////////////////////////////////
int main(int argc, char **argv) {
    tinypy_allocator_t allocator;
    tinypy_vm_config_t config;
    tinypy_compile_options_t options;
    tinypy_compile_mode_e mode;
    tinypy_vm_t *vm;
    tinypy_value_t *code;
    tinypy_error_t *error = NULL;
    unsigned char *source;
    unsigned char *marshal;
    size_t source_size;
    size_t marshal_size;
    int32_t optimize;
    int result = EXIT_FAILURE;

    if (argc != 6 || __tool_mode(argv[4], &mode) == 0) {
        return EXIT_FAILURE;
    }
    optimize = (int32_t)(argv[5][0] - '0');
    if (argv[5][1] != '\0' || optimize < 0 || optimize > 2) {
        return EXIT_FAILURE;
    }
    source = __tool_read(argv[1], &source_size);
    if (source == NULL) {
        return EXIT_FAILURE;
    }
    (void)memset(&allocator, 0, sizeof(allocator));
    allocator.abi_version = TINYPY_ABI_VERSION;
    allocator.struct_size = (uint32_t)sizeof(allocator);
    allocator.allocate = &__tool_allocate;
    allocator.reallocate = &__tool_reallocate;
    allocator.deallocate = &__tool_deallocate;
    (void)memset(&config, 0, sizeof(config));
    config.abi_version = TINYPY_ABI_VERSION;
    config.struct_size = (uint32_t)sizeof(config);
    config.allocator = &allocator;
    config.optimize_level = optimize;
    vm = tinypy_vm_create(&config);
    tinypy_compile_options_init(&options, mode);
    options.optimize_level = optimize;
    unsigned long size = strlen(argv[3]);
    code = tinypy_compile_source(vm, source, source_size, argv[3], size, &options, &error);
    if (code == NULL) {
        if (error != NULL) {
            size_t message_size;
            const char *message = tinypy_error_message(error, &message_size);

            int32_t error_line_number = tinypy_error_line_number(error);
            int32_t error_column_offset = tinypy_error_column_offset(error);
            (void)fprintf(stderr, "%s:%d:%d: %.*s\n", argv[3], error_line_number, error_column_offset, (int)message_size, message);
        }
        goto cleanup;
    }
    if (tinypy_marshal_dump_code_v2(code, NULL, 0U, &marshal_size, NULL, NULL) != TINYPY_MARSHAL_OK) {
        goto cleanup_code;
    }
    marshal = (unsigned char *)malloc(marshal_size == 0U ? 1U : marshal_size);
    assert(marshal != NULL);
    if (tinypy_marshal_dump_code_v2(code, marshal, marshal_size, &marshal_size, NULL, NULL) == TINYPY_MARSHAL_OK && __tool_write(argv[2], marshal, marshal_size) != 0) {
        result = EXIT_SUCCESS;
    }
    free(marshal);
cleanup_code:
    tinypy_release(code);
cleanup:
    if (error != NULL) {
        tinypy_error_release(error);
    }
    tinypy_vm_destroy(vm);
    free(source);
    return result;
}
