#ifndef TINYPY_PREPROCESSOR_H
#define TINYPY_PREPROCESSOR_H

#include "tinypy/compiler.h"

#define TINYPY_SOURCE_MAP_DIGEST_SIZE ((size_t)32U)

typedef struct tinypy_source_map_entry_t {
    uint32_t abi_version;
    uint32_t struct_size;
    int32_t generated_line;
    int32_t generated_column;
    int32_t template_line;
    int32_t template_column;
    int32_t expansion_line;
    int32_t expansion_column;
    const char *generated_symbol;
    size_t generated_symbol_size;
} tinypy_source_map_entry_t;

tinypy_preprocess_result_t *tinypy_preprocess_source(tinypy_vm_t *vm, const void *source, size_t source_size, const char *logical_filename, size_t filename_size, const tinypy_compile_options_t *options, tinypy_error_t **out_error);
void tinypy_preprocess_result_destroy(tinypy_preprocess_result_t *result);
const char *tinypy_preprocess_result_expanded_source(const tinypy_preprocess_result_t *result, size_t *out_size);
const void *tinypy_preprocess_result_source_map(const tinypy_preprocess_result_t *result, size_t *out_size);
const uint8_t *tinypy_preprocess_result_source_map_digest(const tinypy_preprocess_result_t *result);
size_t tinypy_preprocess_result_source_map_count(const tinypy_preprocess_result_t *result);
void tinypy_preprocess_result_source_map_at(const tinypy_preprocess_result_t *result, size_t index, tinypy_source_map_entry_t *out_entry);

#endif
