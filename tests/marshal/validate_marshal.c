#include "tinypy/marshal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void *__validator_allocate(
    void *user_data,
    size_t size,
    size_t alignment,
    uint32_t tag)
{
    (void)user_data;
    (void)alignment;
    (void)tag;
    return malloc(size);
}

static void *__validator_reallocate(
    void *user_data,
    void *memory,
    size_t old_size,
    size_t new_size,
    size_t alignment,
    uint32_t tag)
{
    (void)user_data;
    (void)old_size;
    (void)alignment;
    (void)tag;
    return realloc(memory, new_size);
}

static void __validator_deallocate(
    void *user_data,
    void *memory,
    size_t size,
    size_t alignment,
    uint32_t tag)
{
    (void)user_data;
    (void)size;
    (void)alignment;
    (void)tag;
    free(memory);
}

static tinypy_allocator_t __validator_allocator(void)
{
    tinypy_allocator_t allocator;
    allocator.abi_version = TINYPY_ABI_VERSION;
    allocator.struct_size = (uint32_t)sizeof(allocator);
    allocator.user_data = NULL;
    allocator.allocate = __validator_allocate;
    allocator.reallocate = __validator_reallocate;
    allocator.deallocate = __validator_deallocate;
    return allocator;
}

static int __validate_file(const char *filename, const tinypy_allocator_t *allocator)
{
    FILE *stream;
    long file_size;
    unsigned char *bytes;
    size_t size;
    tinypy_marshal_document_t *document = NULL;
    tinypy_marshal_error_t error;
    tinypy_marshal_result_e result;
    const tinypy_marshal_object_t *root;
    size_t output_size = 0U;
    unsigned char *output = NULL;
    tinypy_marshal_document_t *roundtrip_document = NULL;

    stream = fopen(filename, "rb");
    if (stream == NULL) {
        (void)fprintf(stderr, "%s: cannot open\n", filename);
        return 0;
    }
    if (fseek(stream, 0L, SEEK_END) != 0) {
        (void)fprintf(stderr, "%s: cannot seek\n", filename);
        (void)fclose(stream);
        return 0;
    }
    file_size = ftell(stream);
    if (file_size < 0L || fseek(stream, 0L, SEEK_SET) != 0) {
        (void)fprintf(stderr, "%s: cannot determine size\n", filename);
        (void)fclose(stream);
        return 0;
    }
    size = (size_t)file_size;
    if ((long)size != file_size) {
        (void)fprintf(stderr, "%s: size does not fit size_t\n", filename);
        (void)fclose(stream);
        return 0;
    }
    bytes = (unsigned char *)malloc(size == 0U ? 1U : size);
    if (bytes == NULL) {
        (void)fprintf(stderr, "%s: input allocation failed\n", filename);
        (void)fclose(stream);
        return 0;
    }
    if (size != 0U && fread(bytes, 1U, size, stream) != size) {
        (void)fprintf(stderr, "%s: cannot read complete input\n", filename);
        free(bytes);
        (void)fclose(stream);
        return 0;
    }
    if (fclose(stream) != 0) {
        (void)fprintf(stderr, "%s: cannot close input\n", filename);
        free(bytes);
        return 0;
    }

    result = tinypy_marshal_read_v2(
        bytes,
        size,
        allocator,
        NULL,
        &document,
        &error);
    if (result != TINYPY_MARSHAL_OK) {
        (void)fprintf(
            stderr,
            "%s:%zu: %s (wire=0x%02x, %.*s)\n",
            filename,
            error.offset,
            tinypy_marshal_result_name(result),
            (unsigned int)error.wire_type,
            (int)error.message_size,
            error.message);
        free(bytes);
        return 0;
    }

    root = tinypy_marshal_document_root(document);
    if (tinypy_marshal_object_type(root) != TINYPY_MARSHAL_TYPE_CODE) {
        (void)fprintf(stderr, "%s: root is not a code object\n", filename);
        tinypy_marshal_document_destroy(document);
        free(bytes);
        return 0;
    }

    result = tinypy_marshal_write_v2(
        document,
        NULL,
        0U,
        &output_size,
        NULL,
        &error);
    if (result != TINYPY_MARSHAL_OK) {
        (void)fprintf(
            stderr,
            "%s:%zu: size query failed: %s (%.*s)\n",
            filename,
            error.offset,
            tinypy_marshal_result_name(result),
            (int)error.message_size,
            error.message);
        tinypy_marshal_document_destroy(document);
        free(bytes);
        return 0;
    }
    output = (unsigned char *)malloc(output_size == 0U ? 1U : output_size);
    if (output == NULL) {
        (void)fprintf(stderr, "%s: output allocation failed\n", filename);
        tinypy_marshal_document_destroy(document);
        free(bytes);
        return 0;
    }
    result = tinypy_marshal_write_v2(
        document,
        output,
        output_size,
        &output_size,
        NULL,
        &error);
    if (result != TINYPY_MARSHAL_OK) {
        (void)fprintf(
            stderr,
            "%s:%zu: dump failed: %s (%.*s)\n",
            filename,
            error.offset,
            tinypy_marshal_result_name(result),
            (int)error.message_size,
            error.message);
        free(output);
        tinypy_marshal_document_destroy(document);
        free(bytes);
        return 0;
    }
    if (output_size != size || memcmp(output, bytes, size) != 0) {
        size_t mismatch = 0U;
        size_t common = output_size < size ? output_size : size;
        while (mismatch != common && output[mismatch] == bytes[mismatch]) {
            mismatch += 1U;
        }
        (void)fprintf(
            stderr,
            "%s:%zu: round-trip bytes differ (input=%zu output=%zu)\n",
            filename,
            mismatch,
            size,
            output_size);
        free(output);
        tinypy_marshal_document_destroy(document);
        free(bytes);
        return 0;
    }
    result = tinypy_marshal_read_v2(
        output,
        output_size,
        allocator,
        NULL,
        &roundtrip_document,
        &error);
    if (result != TINYPY_MARSHAL_OK ||
        tinypy_marshal_object_type(tinypy_marshal_document_root(roundtrip_document)) !=
            TINYPY_MARSHAL_TYPE_CODE) {
        (void)fprintf(stderr, "%s: dumped bytes failed semantic re-read\n", filename);
        tinypy_marshal_document_destroy(roundtrip_document);
        free(output);
        tinypy_marshal_document_destroy(document);
        free(bytes);
        return 0;
    }
    tinypy_marshal_document_destroy(roundtrip_document);
    free(output);
    tinypy_marshal_document_destroy(document);
    free(bytes);
    return 1;
}

int main(int argc, char **argv)
{
    tinypy_allocator_t allocator = __validator_allocator();
    size_t validated = 0U;
    int index;

    if (argc < 2) {
        (void)fprintf(stderr, "usage: %s FILE.marshal [...]\n", argv[0]);
        return 2;
    }
    for (index = 1; index != argc; ++index) {
        if (!__validate_file(argv[index], &allocator)) {
            return 1;
        }
        validated += 1U;
    }
    (void)printf("validated and exactly round-tripped %zu marshal-v2 code objects\n", validated);
    return 0;
}
