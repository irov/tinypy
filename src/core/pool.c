#include "internal.h"

#include <assert.h>
#include <string.h>

#define TINYPY_INTERNAL_POOL_SIZE_MASK (TINYPY_INTERNAL_POOL_SIZE - 1U)
#define TINYPY_INTERNAL_POOL_DUMMY_SIZE_CLASS UINT32_MAX
#define TINYPY_INTERNAL_POOL_INITIAL_ARENAS 16U

//////////////////////////////////////////////////////////////////////////
static inline size_t __tinypy_pool_round_up(size_t size) {
    return (size + TINYPY_INTERNAL_ALIGNMENT - 1U) & ~(TINYPY_INTERNAL_ALIGNMENT - 1U);
}
//////////////////////////////////////////////////////////////////////////
static inline size_t __tinypy_pool_block_size(uint32_t size_class) {
    return ((size_t)size_class + 1U) * TINYPY_INTERNAL_ALIGNMENT;
}
//////////////////////////////////////////////////////////////////////////
static void *__tinypy_pool_raw_allocate(tinypy_vm_t *vm, size_t size, size_t alignment, uint32_t tag) {
    void *memory;

    assert(size != 0U);
    assert(vm->allocated_bytes <= SIZE_MAX - size);
    if (vm->max_heap_bytes != 0U) {
        assert(vm->allocated_bytes <= vm->max_heap_bytes);
        assert(size <= vm->max_heap_bytes - vm->allocated_bytes);
    }

    memory = vm->allocator.allocate(vm->allocator.user_data, size, alignment, tag);
    assert(memory != NULL);
    vm->allocated_bytes += size;
    return memory;
}
//////////////////////////////////////////////////////////////////////////
static void *__tinypy_pool_raw_reallocate(tinypy_vm_t *vm, void *memory, size_t old_size, size_t new_size, size_t alignment, uint32_t tag) {
    void *resized;

    assert(memory != NULL);
    assert(old_size != 0U);
    assert(new_size != 0U);
    if (new_size == old_size) {
        return memory;
    }
    if (new_size > old_size) {
        size_t growth;

        growth = new_size - old_size;
        assert(vm->allocated_bytes <= SIZE_MAX - growth);
        if (vm->max_heap_bytes != 0U) {
            assert(vm->allocated_bytes <= vm->max_heap_bytes);
            assert(growth <= vm->max_heap_bytes - vm->allocated_bytes);
        }
        (void)growth;
    }

    resized = vm->allocator.reallocate(vm->allocator.user_data, memory, old_size, new_size, alignment, tag);
    assert(resized != NULL);
    if (new_size > old_size) {
        vm->allocated_bytes += new_size - old_size;
    }
    else {
        assert(vm->allocated_bytes >= old_size - new_size);
        vm->allocated_bytes -= old_size - new_size;
    }
    return resized;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_pool_raw_deallocate(tinypy_vm_t *vm, void *memory, size_t size, size_t alignment, uint32_t tag) {
    assert(memory != NULL);
    assert(size != 0U);
    assert(vm->state == TINYPY_VM_STATE_LIVE || vm->state == TINYPY_VM_STATE_DESTROYING);

    vm->allocator.deallocate(vm->allocator.user_data, memory, size, alignment, tag);
    assert(vm->allocated_bytes >= size);
    vm->allocated_bytes -= size;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_pool_grow_arena_table(tinypy_vm_t *vm) {
    tinypy_pool_arena_t *arenas;
    uint32_t old_count;
    uint32_t new_count;
    uint32_t index;
    size_t old_size;
    size_t new_size;

    tinypy_pool_allocator_t *allocator = &vm->pool_allocator;
    old_count = allocator->maximum_arena_count;
    new_count = old_count != 0U ? old_count << 1U : TINYPY_INTERNAL_POOL_INITIAL_ARENAS;
    assert(new_count > old_count);
    assert(allocator->usable_arenas == NULL);
    assert(allocator->unused_arenas == NULL);
    old_size = (size_t)old_count * sizeof(*arenas);
    new_size = (size_t)new_count * sizeof(*arenas);
    if (allocator->arenas == NULL) {
        arenas = (tinypy_pool_arena_t *)__tinypy_pool_raw_allocate(vm, new_size, TINYPY_INTERNAL_ALIGNMENT, (uint32_t)TINYPY_ALLOC_TAG_POOL_TABLE);
    }
    else {
        arenas = (tinypy_pool_arena_t *)__tinypy_pool_raw_reallocate(vm, allocator->arenas, old_size, new_size, TINYPY_INTERNAL_ALIGNMENT, (uint32_t)TINYPY_ALLOC_TAG_POOL_TABLE);
    }
    allocator->arenas = arenas;
    (void)memset(arenas + old_count, 0, (size_t)(new_count - old_count) * sizeof(*arenas));
    for (index = old_count; index < new_count; ++index) {
        arenas[index].next_arena = index + 1U < new_count ? &arenas[index + 1U] : NULL;
    }
    allocator->unused_arenas = &arenas[old_count];
    allocator->maximum_arena_count = new_count;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_pool_arena_t *__tinypy_pool_new_arena(tinypy_vm_t *vm) {
    void *memory;
    uintptr_t address;
    uintptr_t aligned_address;
    size_t excess;

    tinypy_pool_allocator_t *allocator = &vm->pool_allocator;
    if (allocator->unused_arenas == NULL) {
        __tinypy_pool_grow_arena_table(vm);
    }
    tinypy_pool_arena_t *arena = allocator->unused_arenas;
    allocator->unused_arenas = arena->next_arena;
    assert(arena->address == 0U);

    memory = __tinypy_pool_raw_allocate(vm, TINYPY_INTERNAL_POOL_ARENA_SIZE, TINYPY_INTERNAL_ALIGNMENT, (uint32_t)TINYPY_ALLOC_TAG_POOL_ARENA);
    address = (uintptr_t)memory;
    aligned_address = (address + TINYPY_INTERNAL_POOL_SIZE_MASK) & ~(uintptr_t)TINYPY_INTERNAL_POOL_SIZE_MASK;
    excess = (size_t)(aligned_address - address);
    arena->address = address;
    arena->pool_address = (tinypy_pool_block_t *)aligned_address;
    arena->free_pool_count = (uint32_t)((TINYPY_INTERNAL_POOL_ARENA_SIZE - excess) / TINYPY_INTERNAL_POOL_SIZE);
    arena->total_pool_count = arena->free_pool_count;
    arena->free_pools = NULL;
    arena->next_arena = NULL;
    arena->previous_arena = NULL;
    assert(arena->free_pool_count != 0U);
    allocator->active_arena_count += 1U;
    return arena;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_pool_unlink_used(tinypy_pool_t *pool) {

    tinypy_pool_t *next = pool->next_pool;
    tinypy_pool_t *previous = pool->previous_pool;
    next->previous_pool = previous;
    previous->next_pool = next;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_pool_link_used_front(tinypy_pool_allocator_t *allocator, tinypy_pool_t *pool, uint32_t size_class) {

    tinypy_pool_t *head = &allocator->used_pools[size_class];
    tinypy_pool_t *next = head->next_pool;
    pool->next_pool = next;
    pool->previous_pool = head;
    next->previous_pool = pool;
    head->next_pool = pool;
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_pool_remove_usable_arena(tinypy_pool_allocator_t *allocator, tinypy_pool_arena_t *arena) {
    if (arena->previous_arena != NULL) {
        arena->previous_arena->next_arena = arena->next_arena;
    }
    else {
        assert(allocator->usable_arenas == arena);
        allocator->usable_arenas = arena->next_arena;
    }
    if (arena->next_arena != NULL) {
        arena->next_arena->previous_arena = arena->previous_arena;
    }
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_pool_release_arena(tinypy_vm_t *vm, tinypy_pool_arena_t *arena) {
    void *memory;

    tinypy_pool_allocator_t *allocator = &vm->pool_allocator;
    __tinypy_pool_remove_usable_arena(allocator, arena);
    memory = (void *)arena->address;
    arena->next_arena = allocator->unused_arenas;
    arena->previous_arena = NULL;
    allocator->unused_arenas = arena;
    arena->address = 0U;
    arena->pool_address = NULL;
    arena->free_pool_count = 0U;
    arena->total_pool_count = 0U;
    arena->free_pools = NULL;
    assert(allocator->active_arena_count != 0U);
    allocator->active_arena_count -= 1U;
    __tinypy_pool_raw_deallocate(vm, memory, TINYPY_INTERNAL_POOL_ARENA_SIZE, TINYPY_INTERNAL_ALIGNMENT, (uint32_t)TINYPY_ALLOC_TAG_POOL_ARENA);
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_pool_slide_usable_arena(tinypy_pool_allocator_t *allocator, tinypy_pool_arena_t *arena) {
    uint32_t free_pool_count;

    free_pool_count = arena->free_pool_count;
    if (arena->next_arena == NULL || free_pool_count <= arena->next_arena->free_pool_count) {
        return;
    }
    if (arena->previous_arena != NULL) {
        arena->previous_arena->next_arena = arena->next_arena;
    }
    else {
        assert(allocator->usable_arenas == arena);
        allocator->usable_arenas = arena->next_arena;
    }
    arena->next_arena->previous_arena = arena->previous_arena;
    while (arena->next_arena != NULL && free_pool_count > arena->next_arena->free_pool_count) {
        arena->previous_arena = arena->next_arena;
        arena->next_arena = arena->next_arena->next_arena;
    }
    arena->previous_arena->next_arena = arena;
    if (arena->next_arena != NULL) {
        arena->next_arena->previous_arena = arena;
    }
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_pool_initialize(tinypy_vm_t *vm) {
    uint32_t size_class;

    tinypy_pool_allocator_t *allocator = &vm->pool_allocator;
    (void)memset(allocator, 0, sizeof(*allocator));
    assert(TINYPY_INTERNAL_ALIGNMENT >= sizeof(void *));
    assert((TINYPY_INTERNAL_ALIGNMENT & (TINYPY_INTERNAL_ALIGNMENT - 1U)) == 0U);
    assert((TINYPY_INTERNAL_POOL_SIZE & TINYPY_INTERNAL_POOL_SIZE_MASK) == 0U);
    assert(TINYPY_INTERNAL_POOL_SMALL_REQUEST % TINYPY_INTERNAL_ALIGNMENT == 0U);
    for (size_class = 0U; size_class < TINYPY_INTERNAL_POOL_CLASS_COUNT; ++size_class) {
        tinypy_pool_t *head;

        head = &allocator->used_pools[size_class];
        head->next_pool = head;
        head->previous_pool = head;
        head->size_class = size_class;
    }
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_pool_finalize(tinypy_vm_t *vm) {
    uint32_t size_class;

    tinypy_pool_allocator_t *allocator = &vm->pool_allocator;
    assert(allocator->active_arena_count == 0U);
    assert(allocator->usable_arenas == NULL);
    for (size_class = 0U; size_class < TINYPY_INTERNAL_POOL_CLASS_COUNT; ++size_class) {
        tinypy_pool_t *head;

        head = &allocator->used_pools[size_class];
        assert(head->next_pool == head);
        assert(head->previous_pool == head);
        (void)head;
    }
    if (allocator->arenas != NULL) {
        size_t allocation_size;

        allocation_size = (size_t)allocator->maximum_arena_count * sizeof(*allocator->arenas);
        __tinypy_pool_raw_deallocate(vm, allocator->arenas, allocation_size, TINYPY_INTERNAL_ALIGNMENT, (uint32_t)TINYPY_ALLOC_TAG_POOL_TABLE);
    }
    (void)memset(allocator, 0, sizeof(*allocator));
}
//////////////////////////////////////////////////////////////////////////
void *tinypy_internal_pool_allocate(tinypy_vm_t *vm, size_t size, uint32_t tag) {
    tinypy_pool_t *head;
    tinypy_pool_t *pool;
    tinypy_pool_block_t *block;
    uint32_t size_class;
    size_t block_size;

    assert(size != 0U);
    if (size > TINYPY_INTERNAL_POOL_SMALL_REQUEST) {
        return __tinypy_pool_raw_allocate(vm, size, TINYPY_INTERNAL_ALIGNMENT, tag);
    }

    tinypy_pool_allocator_t *allocator = &vm->pool_allocator;
    size_class = (uint32_t)((size - 1U) / TINYPY_INTERNAL_ALIGNMENT);
    head = &allocator->used_pools[size_class];
    pool = head->next_pool;
    if (pool != head) {
        assert(pool->ref != UINT32_MAX);
        pool->ref += 1U;
        block = pool->free_block;
        assert(block != NULL);
        pool->free_block = *(tinypy_pool_block_t **)block;
        if (pool->free_block != NULL) {
            return block;
        }
        block_size = __tinypy_pool_block_size(size_class);
        if (pool->next_offset <= pool->maximum_next_offset) {
            pool->free_block = (tinypy_pool_block_t *)pool + pool->next_offset;
            pool->next_offset += (uint32_t)block_size;
            *(tinypy_pool_block_t **)pool->free_block = NULL;
            return block;
        }
        __tinypy_pool_unlink_used(pool);
        return block;
    }

    tinypy_pool_arena_t *arena = allocator->usable_arenas;
    if (arena == NULL) {
        arena = __tinypy_pool_new_arena(vm);
        allocator->usable_arenas = arena;
    }
    assert(arena->address != 0U);
    pool = arena->free_pools;
    if (pool != NULL) {
        arena->free_pools = pool->next_pool;
        assert(arena->free_pool_count != 0U);
        arena->free_pool_count -= 1U;
        if (arena->free_pool_count == 0U) {
            assert(arena->free_pools == NULL);
            allocator->usable_arenas = arena->next_arena;
            if (allocator->usable_arenas != NULL) {
                allocator->usable_arenas->previous_arena = NULL;
            }
        }
    }
    else {
        size_t arena_index;

        assert(arena->free_pool_count != 0U);
        pool = (tinypy_pool_t *)arena->pool_address;
        arena_index = (size_t)(arena - allocator->arenas);
        assert(arena_index <= UINT32_MAX);
        pool->arena_index = (uint32_t)arena_index;
        pool->size_class = TINYPY_INTERNAL_POOL_DUMMY_SIZE_CLASS;
        arena->pool_address += TINYPY_INTERNAL_POOL_SIZE;
        arena->free_pool_count -= 1U;
        if (arena->free_pool_count == 0U) {
            allocator->usable_arenas = arena->next_arena;
            if (allocator->usable_arenas != NULL) {
                allocator->usable_arenas->previous_arena = NULL;
            }
        }
    }

    __tinypy_pool_link_used_front(allocator, pool, size_class);
    pool->ref = 1U;
    if (pool->size_class == size_class) {
        block = pool->free_block;
        assert(block != NULL);
        pool->free_block = *(tinypy_pool_block_t **)block;
        return block;
    }

    pool->size_class = size_class;
    block_size = __tinypy_pool_block_size(size_class);
    block = (tinypy_pool_block_t *)pool + __tinypy_pool_round_up(sizeof(*pool));
    pool->next_offset = (uint32_t)(__tinypy_pool_round_up(sizeof(*pool)) + block_size * 2U);
    pool->maximum_next_offset = (uint32_t)(TINYPY_INTERNAL_POOL_SIZE - block_size);
    pool->free_block = block + block_size;
    *(tinypy_pool_block_t **)pool->free_block = NULL;
    return block;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_pool_deallocate(tinypy_vm_t *vm, void *memory, size_t size, uint32_t tag) {
    tinypy_pool_t *pool;
    tinypy_pool_block_t *last_free;
    uintptr_t pool_address;
    uint32_t free_pool_count;

    assert(memory != NULL);
    assert(size != 0U);
    if (size > TINYPY_INTERNAL_POOL_SMALL_REQUEST) {
        __tinypy_pool_raw_deallocate(vm, memory, size, TINYPY_INTERNAL_ALIGNMENT, tag);
        return;
    }

    tinypy_pool_allocator_t *allocator = &vm->pool_allocator;
    pool_address = (uintptr_t)memory & ~(uintptr_t)TINYPY_INTERNAL_POOL_SIZE_MASK;
    pool = (tinypy_pool_t *)pool_address;
    assert(pool->arena_index < allocator->maximum_arena_count);
    tinypy_pool_arena_t *arena = &allocator->arenas[pool->arena_index];
    assert(arena->address != 0U);
    assert((uintptr_t)memory - arena->address < TINYPY_INTERNAL_POOL_ARENA_SIZE);
    assert(size <= __tinypy_pool_block_size(pool->size_class));
    assert(pool->ref != 0U);

    last_free = pool->free_block;
    *(tinypy_pool_block_t **)memory = last_free;
    pool->free_block = (tinypy_pool_block_t *)memory;
    if (last_free != NULL) {
        pool->ref -= 1U;
        if (pool->ref != 0U) {
            return;
        }
        __tinypy_pool_unlink_used(pool);
        pool->next_pool = arena->free_pools;
        arena->free_pools = pool;
        assert(arena->free_pool_count != UINT32_MAX);
        arena->free_pool_count += 1U;
        free_pool_count = arena->free_pool_count;
        if (free_pool_count == arena->total_pool_count) {
            __tinypy_pool_release_arena(vm, arena);
            return;
        }
        if (free_pool_count == 1U) {
            arena->next_arena = allocator->usable_arenas;
            arena->previous_arena = NULL;
            if (allocator->usable_arenas != NULL) {
                allocator->usable_arenas->previous_arena = arena;
            }
            allocator->usable_arenas = arena;
            return;
        }
        __tinypy_pool_slide_usable_arena(allocator, arena);
        return;
    }

    pool->ref -= 1U;
    assert(pool->ref != 0U);
    __tinypy_pool_link_used_front(allocator, pool, pool->size_class);
}
//////////////////////////////////////////////////////////////////////////
void *tinypy_internal_pool_reallocate(tinypy_vm_t *vm, void *memory, size_t old_size, size_t new_size, uint32_t tag) {
    void *resized;
    size_t copy_size;

    assert(memory != NULL);
    assert(old_size != 0U);
    assert(new_size != 0U);
    if (new_size == old_size) {
        return memory;
    }
    if (old_size <= TINYPY_INTERNAL_POOL_SMALL_REQUEST) {
        tinypy_pool_t *pool;
        size_t old_block_size;
        uintptr_t pool_address;

        pool_address = (uintptr_t)memory & ~(uintptr_t)TINYPY_INTERNAL_POOL_SIZE_MASK;
        pool = (tinypy_pool_t *)pool_address;
        assert(pool->arena_index < vm->pool_allocator.maximum_arena_count);
        assert(vm->pool_allocator.arenas[pool->arena_index].address != 0U);
        assert((uintptr_t)memory - vm->pool_allocator.arenas[pool->arena_index].address < TINYPY_INTERNAL_POOL_ARENA_SIZE);
        old_block_size = __tinypy_pool_block_size(pool->size_class);
        assert(old_size <= old_block_size);
        if (new_size <= old_block_size && new_size > old_block_size - old_block_size / 4U) {
            return memory;
        }
    }
    else if (new_size > TINYPY_INTERNAL_POOL_SMALL_REQUEST) {
        return __tinypy_pool_raw_reallocate(vm, memory, old_size, new_size, TINYPY_INTERNAL_ALIGNMENT, tag);
    }

    resized = tinypy_internal_pool_allocate(vm, new_size, tag);
    copy_size = old_size < new_size ? old_size : new_size;
    (void)memcpy(resized, memory, copy_size);
    tinypy_internal_pool_deallocate(vm, memory, old_size, tag);
    return resized;
}
