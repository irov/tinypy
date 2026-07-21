#include "internal.h"

#include <string.h>

#define TINYPY_COMPILER_ARENA_BLOCK_SIZE ((size_t)4096U)

//////////////////////////////////////////////////////////////////////////
void *tinypy_internal_compiler_arena_allocate(tinypy_compile_ctx_t *ctx, size_t size) {
    tinypy_compiler_arena_block_t *block;
    size_t aligned_size;
    size_t header_size;
    size_t payload_size;
    size_t allocation_size;
    void *memory;

    assert(ctx != NULL);
    assert(size != 0U);
    assert(size <= SIZE_MAX - (TINYPY_INTERNAL_ALIGNMENT - 1U));
    aligned_size = (size + (TINYPY_INTERNAL_ALIGNMENT - 1U)) & ~(TINYPY_INTERNAL_ALIGNMENT - 1U);
    block = ctx->arena_blocks;
    if (block != NULL && aligned_size <= block->allocation_size - offsetof(tinypy_compiler_arena_block_t, data) - block->used) {
        memory = block->data + block->used;
        block->used += aligned_size;
        (void)memset(memory, 0, size);
        return memory;
    }

    header_size = offsetof(tinypy_compiler_arena_block_t, data);
    payload_size = aligned_size > TINYPY_COMPILER_ARENA_BLOCK_SIZE ? aligned_size : TINYPY_COMPILER_ARENA_BLOCK_SIZE;
    assert(payload_size <= SIZE_MAX - header_size);
    allocation_size = header_size + payload_size;
    if (ctx->limits.max_arena_bytes != 0U && (ctx->arena_bytes > ctx->limits.max_arena_bytes || allocation_size > ctx->limits.max_arena_bytes - ctx->arena_bytes)) {
        return NULL;
    }

    block = (tinypy_compiler_arena_block_t *)tinypy_internal_vm_allocate(ctx->vm, allocation_size, (uint32_t)TINYPY_ALLOC_TAG_COMPILER_ARENA);
    block->next = ctx->arena_blocks;
    block->allocation_size = allocation_size;
    block->used = aligned_size;
    ctx->arena_blocks = block;
    ctx->arena_bytes += allocation_size;
    memory = block->data;
    (void)memset(memory, 0, size);
    return memory;
}

//////////////////////////////////////////////////////////////////////////
void *tinypy_internal_compiler_ast_allocate(tinypy_compile_ctx_t *ctx, size_t size) {
    if (ctx->limits.max_ast_nodes != 0U && ctx->ast_node_count >= ctx->limits.max_ast_nodes) {
        tinypy_internal_compiler_error(ctx, TINYPY_ERROR_COMPILER_LIMIT, "AST tinypy_cst_node_t limit exceeded", 0, 0, ctx->out_error);
        return NULL;
    }
    ctx->ast_node_count += 1U;
    return tinypy_internal_compiler_arena_allocate(ctx, size);
}

//////////////////////////////////////////////////////////////////////////
void tinypy_internal_compiler_arena_destroy(tinypy_compile_ctx_t *ctx) {
    tinypy_compiler_arena_block_t *block;
    tinypy_compiler_value_ref_t *value_ref;

    assert(ctx != NULL);
    value_ref = ctx->arena_values;
    while (value_ref != NULL) {
        tinypy_release(value_ref->value);
        value_ref = value_ref->next;
    }
    ctx->arena_values = NULL;
    block = ctx->arena_blocks;
    while (block != NULL) {
        tinypy_compiler_arena_block_t *next = block->next;

        tinypy_internal_vm_deallocate(ctx->vm, block, block->allocation_size, (uint32_t)TINYPY_ALLOC_TAG_COMPILER_ARENA);
        block = next;
    }
    ctx->arena_blocks = NULL;
    ctx->arena_bytes = 0U;
}

//////////////////////////////////////////////////////////////////////////
int32_t tinypy_internal_compiler_arena_add_value(tinypy_compile_ctx_t *ctx, tinypy_value_t *value) {
    tinypy_compiler_value_ref_t *value_ref;

    assert(ctx != NULL);
    assert(value != NULL);
    assert(tinypy_internal_value_belongs_to(ctx->vm, value));
    value_ref = (tinypy_compiler_value_ref_t *)tinypy_internal_compiler_arena_allocate(ctx, sizeof(*value_ref));
    if (value_ref == NULL) {
        return -1;
    }
    value_ref->next = ctx->arena_values;
    value_ref->value = value;
    ctx->arena_values = value_ref;
    return 0;
}
