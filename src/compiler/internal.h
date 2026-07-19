#ifndef TINYPY_COMPILER_INTERNAL_H
#define TINYPY_COMPILER_INTERNAL_H

#include "tinypy/compiler.h"

#include "../core/internal.h"

typedef struct tinypy_compiler_arena_block_t {
    struct tinypy_compiler_arena_block_t *next;
    size_t allocation_size;
    size_t used;
    unsigned char data[];
} tinypy_compiler_arena_block_t;

typedef struct tinypy_source_view_t {
    const unsigned char *bytes;
    size_t size;
} tinypy_source_view_t;

typedef struct tinypy_compiler_value_ref_t {
    struct tinypy_compiler_value_ref_t *next;
    tinypy_value_t *value;
} tinypy_compiler_value_ref_t;

typedef struct tinypy_compile_ctx_t {
    tinypy_vm_t *vm;
    tinypy_compile_options_t options;
    tinypy_compile_limits_t limits;
    tinypy_source_view_t source;
    const char *logical_filename;
    size_t filename_size;
    tinypy_compiler_arena_block_t *arena_blocks;
    tinypy_compiler_value_ref_t *arena_values;
    size_t arena_bytes;
    size_t token_count;
    size_t cst_node_count;
    size_t ast_node_count;
    size_t symbol_count;
    size_t block_count;
    size_t instruction_count;
    size_t constant_count;
    size_t constant_bytes;
    int32_t source_is_unicode;
    int32_t failed;
    tinypy_error_t **out_error;
} tinypy_compile_ctx_t;

void *tinypy_internal_compiler_arena_allocate(tinypy_compile_ctx_t *ctx, size_t size);
void *tinypy_internal_compiler_ast_allocate(tinypy_compile_ctx_t *ctx, size_t size);
void tinypy_internal_compiler_arena_destroy(tinypy_compile_ctx_t *ctx);
int32_t tinypy_internal_compiler_arena_add_value(tinypy_compile_ctx_t *ctx, tinypy_value_t *value);
int32_t tinypy_internal_compiler_source_prepare(tinypy_compile_ctx_t *ctx, const void *source, size_t source_size, tinypy_error_t **out_error);
void tinypy_internal_compiler_error(tinypy_compile_ctx_t *ctx, tinypy_error_kind_e error_kind, const char *message, int32_t line_number, int32_t column_offset, tinypy_error_t **out_error);
void tinypy_internal_compiler_error_parts(tinypy_compile_ctx_t *ctx, tinypy_error_kind_e error_kind, const char *const *parts, const size_t *part_sizes, size_t part_count, int32_t line_number, int32_t column_offset);
tinypy_value_t *tinypy_internal_compiler_compile(tinypy_compile_ctx_t *ctx, tinypy_error_t **out_error);
tinypy_value_t *tinypy_internal_compiler_parse_number(tinypy_compile_ctx_t *ctx, const char *text, int32_t line_number, int32_t column_offset);
int32_t tinypy_internal_compiler_decimal_double(tinypy_compile_ctx_t *ctx, const char *text, size_t size, double *out_value, int32_t line_number, int32_t column_offset);
int32_t tinypy_internal_compiler_unicode_name(const char *name, size_t name_size, uint32_t *out_code_point);
tinypy_value_t *tinypy_internal_compiler_parse_string(tinypy_compile_ctx_t *ctx, const char *text, int32_t future_unicode, int32_t line_number, int32_t column_offset);
tinypy_value_t *tinypy_internal_compiler_concat_strings(tinypy_compile_ctx_t *ctx, tinypy_value_t *left, tinypy_value_t *right, int32_t line_number, int32_t column_offset);

#endif
