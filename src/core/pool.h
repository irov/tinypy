#ifndef TINYPY_CORE_POOL_H
#define TINYPY_CORE_POOL_H

#define TINYPY_INTERNAL_POOL_SIZE 4096U
#define TINYPY_INTERNAL_POOL_ARENA_SIZE (256U * 1024U)
#define TINYPY_INTERNAL_POOL_SMALL_REQUEST 512U
#define TINYPY_INTERNAL_POOL_CLASS_COUNT (TINYPY_INTERNAL_POOL_SMALL_REQUEST / TINYPY_INTERNAL_ALIGNMENT)

typedef uint8_t tinypy_pool_block_t;

typedef struct tinypy_pool_t {
    uint32_t ref;
    tinypy_pool_block_t *free_block;
    struct tinypy_pool_t *next_pool;
    struct tinypy_pool_t *previous_pool;
    uint32_t arena_index;
    uint32_t size_class;
    uint32_t next_offset;
    uint32_t maximum_next_offset;
} tinypy_pool_t;

typedef struct tinypy_pool_arena_t {
    uintptr_t address;
    tinypy_pool_block_t *pool_address;
    uint32_t free_pool_count;
    uint32_t total_pool_count;
    tinypy_pool_t *free_pools;
    struct tinypy_pool_arena_t *next_arena;
    struct tinypy_pool_arena_t *previous_arena;
} tinypy_pool_arena_t;

typedef struct tinypy_pool_allocator_t {
    tinypy_pool_t used_pools[TINYPY_INTERNAL_POOL_CLASS_COUNT];
    tinypy_pool_arena_t *arenas;
    tinypy_pool_arena_t *unused_arenas;
    tinypy_pool_arena_t *usable_arenas;
    uint32_t maximum_arena_count;
    size_t active_arena_count;
} tinypy_pool_allocator_t;

void tinypy_internal_pool_initialize(tinypy_vm_t *vm);
void tinypy_internal_pool_finalize(tinypy_vm_t *vm);
void *tinypy_internal_pool_allocate(tinypy_vm_t *vm, size_t size, tinypy_allocation_tag_e tag);
void *tinypy_internal_pool_reallocate(tinypy_vm_t *vm, void *memory, size_t old_size, size_t new_size, tinypy_allocation_tag_e tag);
void tinypy_internal_pool_deallocate(tinypy_vm_t *vm, void *memory, size_t size, tinypy_allocation_tag_e tag);

#endif
