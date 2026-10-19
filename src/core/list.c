#include "tinypy/list.h"

#include "internal.h"

#include <string.h>

#define TINYPY_LIST_RECYCLE_SIZE 8U

//////////////////////////////////////////////////////////////////////////
/* list_dealloc releases the items from the last one, which orders their
   __del__ calls. */
void tinypy_internal_list_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data) {
    tinypy_value_t **items = TINYPY_LIST_OBJECT(value)->items;
    size_t size = TINYPY_SIZED_SIZE(value);

    for (size_t index = size; index != 0U; --index) {
        visit(items[index - 1U], user_data);
    }
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_list_destroy(tinypy_value_t *value) {
    if (TINYPY_LIST_OBJECT(value)->items != NULL) {
        tinypy_internal_vm_deallocate(
            TINYPY_VALUE_VM(value),
            TINYPY_LIST_OBJECT(value)->items,
            TINYPY_LIST_OBJECT(value)->allocated * sizeof(tinypy_value_t *));
    }
}
//////////////////////////////////////////////////////////////////////////
static inline size_t __tinypy_internal_list_storage_size(size_t capacity) {
    return capacity * sizeof(tinypy_value_t *);
}
//////////////////////////////////////////////////////////////////////////
/* The margin list_resize allocates past a new size. */
static inline size_t __tinypy_internal_list_margin(size_t size) {
    size_t margin = (size >> 3U) + (size < 9U ? 3U : 6U);

    return margin;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_internal_list_reserve(tinypy_vm_t *vm, tinypy_value_t *list, size_t minimum_capacity, tinypy_bool_t checked, tinypy_error_t **out_error) {
    size_t old_size;
    size_t new_size;
    size_t new_capacity;
    size_t extra;
    tinypy_value_t **items;

    if (minimum_capacity <= TINYPY_LIST_OBJECT(list)->allocated) {
        return TINYPY_TRUE;
    }
    if (minimum_capacity > SIZE_MAX / sizeof(tinypy_value_t *)) {
        if (checked != 0) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_MEMORY, "list is too large", out_error);
        }
        return TINYPY_FALSE;
    }

    extra = __tinypy_internal_list_margin(minimum_capacity);
    if (minimum_capacity > SIZE_MAX - extra) {
        new_capacity = minimum_capacity;
    }
    else {
        new_capacity = minimum_capacity + extra;
    }
    if (new_capacity > SIZE_MAX / sizeof(tinypy_value_t *)) {
        new_capacity = minimum_capacity;
    }
    new_size = __tinypy_internal_list_storage_size(new_capacity);

    if (TINYPY_LIST_OBJECT(list)->items == NULL) {
        items = checked != 0
                    ? (tinypy_value_t **)__tinypy_internal_vm_allocate_checked(vm, new_size, out_error)
                    : (tinypy_value_t **)tinypy_internal_vm_allocate(vm, new_size);
    }
    else {
        old_size = __tinypy_internal_list_storage_size(
            TINYPY_LIST_OBJECT(list)->allocated);
        items = checked != 0
                    ? (tinypy_value_t **)__tinypy_internal_vm_reallocate_checked(vm, TINYPY_LIST_OBJECT(list)->items, old_size, new_size, out_error)
                    : (tinypy_value_t **)tinypy_internal_vm_reallocate(vm, TINYPY_LIST_OBJECT(list)->items, old_size, new_size);
    }
    if (items == NULL) {
        return TINYPY_FALSE;
    }
    TINYPY_LIST_OBJECT(list)->items = items;
    TINYPY_LIST_OBJECT(list)->allocated = new_capacity;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_list_reserve(tinypy_vm_t *vm, tinypy_value_t *list, size_t minimum_capacity) {
    (void)__tinypy_internal_list_reserve(vm, list, minimum_capacity, TINYPY_FALSE, NULL);
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_list_reserve_checked(tinypy_vm_t *vm, tinypy_value_t *list, size_t minimum_capacity, tinypy_error_t **out_error) {
    tinypy_bool_t result = __tinypy_internal_list_reserve(vm, list, minimum_capacity, TINYPY_TRUE, out_error);

    return result;
}
//////////////////////////////////////////////////////////////////////////
/* The capacity list_resize gives a list holding size items, at least one,
   in allocated slots: storage less than half used shrinks to the size and
   its margin. Storage of the host allocator moves into the pool only for a
   capacity of at most half its largest block, so that a list oscillating
   around that size does not move between them each time. */
static size_t __tinypy_internal_list_shrunk_capacity(size_t size, size_t allocated) {
    size_t pool_capacity = TINYPY_INTERNAL_POOL_SMALL_REQUEST / sizeof(tinypy_value_t *);

    if (size >= allocated >> 1U) {
        return allocated;
    }
    size_t capacity = size + __tinypy_internal_list_margin(size);
    if (allocated > pool_capacity && capacity <= pool_capacity && capacity > pool_capacity >> 1U) {
        return pool_capacity + 1U;
    }
    return capacity;
}
//////////////////////////////////////////////////////////////////////////
/* list_resize to the size the list has, after removing items or, as
   listextend cuts back, after a reservation the iteration did not fill:
   storage less than half used shrinks to the size and its margin. An emptied
   list gives its storage back as list_clear does, except the storage that
   one append takes, which a stack pushing and popping one item would
   otherwise reallocate each time; giving it back counts as a change, which
   listsort detects as it detects any list_resize. A shrink that cannot
   reallocate keeps the larger storage. */
void tinypy_internal_list_shrink(tinypy_value_t *list) {
    tinypy_list_object_t *object = TINYPY_LIST_OBJECT(list);
    size_t size = TINYPY_SIZED_SIZE(list);
    size_t allocated = object->allocated;

    if (size == 0U) {
        if (allocated <= 1U + __tinypy_internal_list_margin(1U)) {
            return;
        }
        tinypy_internal_vm_deallocate(TINYPY_VALUE_VM(list), object->items, __tinypy_internal_list_storage_size(allocated));
        object->items = NULL;
        object->allocated = 0U;
        object->mutation_version += UINT64_C(1);
        return;
    }
    size_t capacity = __tinypy_internal_list_shrunk_capacity(size, allocated);
    if (capacity == allocated) {
        return;
    }
    tinypy_value_t **items = (tinypy_value_t **)tinypy_internal_vm_reallocate(TINYPY_VALUE_VM(list), object->items, __tinypy_internal_list_storage_size(allocated), __tinypy_internal_list_storage_size(capacity));
    if (items == NULL) {
        return;
    }
    object->items = items;
    object->allocated = capacity;
}
//////////////////////////////////////////////////////////////////////////
/* PyList_New filled with the items: the storage holds exactly size items.
   Only the storage has a computed size; the list object is a fixed one. */
static tinypy_value_t *__tinypy_internal_list_from_items_exact(tinypy_vm_t *vm, tinypy_value_t *const *items, size_t size, tinypy_bool_t checked, tinypy_error_t **out_error) {
    size_t storage_size;
    size_t index;

    if (size > SIZE_MAX / sizeof(tinypy_value_t *)) {
        if (checked != 0) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_MEMORY, "list is too large", out_error);
        }
        return NULL;
    }
    storage_size = __tinypy_internal_list_storage_size(size);

    tinypy_value_t *result = tinypy_internal_value_allocate(vm, TINYPY_VALUE_LIST, sizeof(tinypy_list_object_t));
    if (size != 0U) {
        tinypy_value_t **storage = checked != 0
                                       ? (tinypy_value_t **)__tinypy_internal_vm_allocate_checked(vm, storage_size, out_error)
                                       : (tinypy_value_t **)tinypy_internal_vm_allocate(vm, storage_size);
        if (storage == NULL) {
            TINYPY_DECREF(result);
            return NULL;
        }
        TINYPY_LIST_OBJECT(result)->items = storage;
        TINYPY_LIST_OBJECT(result)->allocated = size;
    }

    for (index = 0U; index < size; ++index) {
        TINYPY_INCREF(items[index]);
        TINYPY_LIST_OBJECT(result)->items[index] = items[index];
    }

    TINYPY_SIZED_SIZE(result) = size;
#if defined(TINYPY_CYCLE_DIAGNOSTICS)
    __tinypy_internal_cycle_diagnostics_list_extend(vm, result, 0U, items, size);
#endif
    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_list_from_items(tinypy_vm_t *vm, tinypy_value_t *const *items, size_t size) {
    tinypy_value_t *result = __tinypy_internal_list_from_items_exact(vm, items, size, TINYPY_FALSE, NULL);

    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_list_from_items_exact_checked(tinypy_vm_t *vm, tinypy_value_t *const *items, size_t size, tinypy_error_t **out_error) {
    tinypy_value_t *result = __tinypy_internal_list_from_items_exact(vm, items, size, TINYPY_TRUE, out_error);

    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_list_from_items_checked(tinypy_vm_t *vm, tinypy_value_t *const *items, size_t size, tinypy_error_t **out_error) {
    tinypy_value_t *result = tinypy_internal_object_allocate_checked(vm, &vm->types[TINYPY_VALUE_LIST], sizeof(tinypy_list_object_t), out_error);

    if (result == NULL) {
        return NULL;
    }
    if (tinypy_internal_list_extend_checked(result, items, size, out_error) == 0) {
        TINYPY_DECREF(result);
        return NULL;
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
size_t tinypy_list_size(const tinypy_value_t *value) {

    size_t return_value_1 = TINYPY_SIZED_SIZE(value);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_list_get(const tinypy_value_t *value, size_t index) {

    tinypy_value_t *return_value_1 = TINYPY_LIST_OBJECT(value)->items[index];
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
uint64_t tinypy_list_version(const tinypy_value_t *value) {

    uint64_t return_value_1 = TINYPY_LIST_OBJECT(value)->mutation_version;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_list_extend(tinypy_value_t *list, tinypy_value_t *const *items, size_t item_count, tinypy_bool_t checked, tinypy_error_t **out_error) {
    size_t old_size;
    size_t new_size;
    size_t index;
    size_t source_offset = SIZE_MAX;

    tinypy_vm_t *vm = TINYPY_VALUE_VM(list);
    if (item_count == 0U) {
        return TINYPY_TRUE;
    }
    old_size = TINYPY_SIZED_SIZE(list);
    if (item_count > SIZE_MAX - old_size || old_size + item_count > SIZE_MAX / sizeof(tinypy_value_t *)) {
        if (checked != 0) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_MEMORY, "list is too large", out_error);
        }
        return TINYPY_FALSE;
    }
    new_size = old_size + item_count;

    if (TINYPY_LIST_OBJECT(list)->items != NULL) {
        uintptr_t storage_address = (uintptr_t)TINYPY_LIST_OBJECT(list)->items;
        uintptr_t source_address = (uintptr_t)items;
        size_t storage_size = TINYPY_LIST_OBJECT(list)->allocated * sizeof(tinypy_value_t *);
        size_t source_size = item_count * sizeof(tinypy_value_t *);

        if (source_address >= storage_address && source_address - storage_address <= storage_size && source_size <= storage_size - (source_address - storage_address)) {
            source_offset = (source_address - storage_address) / sizeof(tinypy_value_t *);
        }
    }

    for (index = 0U; index < item_count; ++index) {
        TINYPY_INCREF(items[index]);
    }

    if ((checked != 0
             ? tinypy_internal_list_reserve_checked(vm, list, new_size, out_error)
             : (__tinypy_internal_list_reserve(vm, list, new_size, TINYPY_FALSE, NULL))) == 0) {
        for (index = 0U; index < item_count; ++index) {
            TINYPY_DECREF(items[index]);
        }
        return TINYPY_FALSE;
    }
    if (source_offset != SIZE_MAX) {
        items = TINYPY_LIST_OBJECT(list)->items + source_offset;
    }

    (void)memcpy(
        TINYPY_LIST_OBJECT(list)->items + TINYPY_SIZED_SIZE(list),
        items,
        item_count * sizeof(*items));
    TINYPY_SIZED_SIZE(list) = new_size;
    TINYPY_LIST_OBJECT(list)->mutation_version += UINT64_C(1);
#if defined(TINYPY_CYCLE_DIAGNOSTICS)
    __tinypy_internal_cycle_diagnostics_list_extend(vm, list, old_size, items, item_count);
#endif
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_list_extend(tinypy_value_t *list, tinypy_value_t *const *items, size_t item_count) {
    (void)__tinypy_list_extend(list, items, item_count, TINYPY_FALSE, NULL);
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_list_extend_checked(tinypy_value_t *list, tinypy_value_t *const *items, size_t item_count, tinypy_error_t **out_error) {
    tinypy_bool_t result = __tinypy_list_extend(list, items, item_count, TINYPY_TRUE, out_error);

    return result;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_list_append(tinypy_value_t *list, tinypy_value_t *item) {
    tinypy_value_t *items[1];

    items[0] = item;
    tinypy_list_extend(list, items, 1U);
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_list_append_checked(tinypy_value_t *list, tinypy_value_t *item, tinypy_error_t **out_error) {
    tinypy_list_object_t *object = TINYPY_LIST_OBJECT(list);
    size_t size = TINYPY_SIZED_SIZE(list);
    tinypy_value_t *items[1];

    /* app1 stores into spare capacity directly; growing takes extend. */
    if (size < object->allocated) {
        TINYPY_INCREF(item);
        object->items[size] = item;
        TINYPY_SIZED_SIZE(list) = size + 1U;
        object->mutation_version += UINT64_C(1);
#if defined(TINYPY_CYCLE_DIAGNOSTICS)
        __tinypy_internal_cycle_diagnostics_list_extend(TINYPY_VALUE_VM(list), list, size, &item, 1U);
#endif
        return TINYPY_TRUE;
    }
    items[0] = item;
    tinypy_bool_t return_value_1 = tinypy_internal_list_extend_checked(list, items, 1U, out_error);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_list_insert(tinypy_value_t *list, size_t index, tinypy_value_t *item, tinypy_bool_t checked, tinypy_error_t **out_error) {
    size_t move_count;

    tinypy_vm_t *vm = TINYPY_VALUE_VM(list);

    if (TINYPY_SIZED_SIZE(list) == SIZE_MAX ||
        (checked != 0
             ? tinypy_internal_list_reserve_checked(vm, list, TINYPY_SIZED_SIZE(list) + 1U, out_error)
             : __tinypy_internal_list_reserve(vm, list, TINYPY_SIZED_SIZE(list) + 1U, TINYPY_FALSE, NULL)) == 0) {
        if (checked != 0 && (out_error == NULL || *out_error == NULL)) {
            tinypy_internal_make_vm_error(vm, TINYPY_ERROR_MEMORY, "list is too large", out_error);
        }
        return TINYPY_FALSE;
    }
    TINYPY_INCREF(item);

    move_count = TINYPY_SIZED_SIZE(list) - index;
    if (move_count != 0U) {
        (void)memmove(
            TINYPY_LIST_OBJECT(list)->items + index + 1U,
            TINYPY_LIST_OBJECT(list)->items + index,
            move_count * sizeof(tinypy_value_t *));
    }
    TINYPY_LIST_OBJECT(list)->items[index] = item;
    TINYPY_LIST_OBJECT(list)->base.size += 1U;
    TINYPY_LIST_OBJECT(list)->mutation_version += UINT64_C(1);
#if defined(TINYPY_CYCLE_DIAGNOSTICS)
    __tinypy_internal_cycle_diagnostics_list_insert(vm, list, index, item);
#endif
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_list_insert(tinypy_value_t *list, size_t index, tinypy_value_t *item) {
    (void)__tinypy_list_insert(list, index, item, TINYPY_FALSE, NULL);
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_list_insert_checked(tinypy_value_t *list, size_t index, tinypy_value_t *item, tinypy_error_t **out_error) {
    tinypy_bool_t result = __tinypy_list_insert(list, index, item, TINYPY_TRUE, out_error);

    return result;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_list_set(tinypy_value_t *list, size_t index, tinypy_value_t *item) {
    TINYPY_INCREF(item);

    tinypy_value_t *previous = TINYPY_LIST_OBJECT(list)->items[index];
    TINYPY_LIST_OBJECT(list)->items[index] = item;
    TINYPY_LIST_OBJECT(list)->mutation_version += UINT64_C(1);
#if defined(TINYPY_CYCLE_DIAGNOSTICS)
    __tinypy_internal_cycle_diagnostics_list_set(TINYPY_VALUE_VM(list), list, index, item);
#endif
    TINYPY_DECREF(previous);
}
//////////////////////////////////////////////////////////////////////////
void tinypy_list_delete(tinypy_value_t *list, size_t index) {
    size_t move_count;

    tinypy_value_t *previous = TINYPY_LIST_OBJECT(list)->items[index];
    move_count = TINYPY_SIZED_SIZE(list) - index - 1U;
    if (move_count != 0U) {
        (void)memmove(
            TINYPY_LIST_OBJECT(list)->items + index,
            TINYPY_LIST_OBJECT(list)->items + index + 1U,
            move_count * sizeof(tinypy_value_t *));
    }
    TINYPY_LIST_OBJECT(list)->base.size -= 1U;
    TINYPY_LIST_OBJECT(list)->items[TINYPY_SIZED_SIZE(list)] = NULL;
    TINYPY_LIST_OBJECT(list)->mutation_version += UINT64_C(1);
#if defined(TINYPY_CYCLE_DIAGNOSTICS)
    __tinypy_internal_cycle_diagnostics_list_remove(TINYPY_VALUE_VM(list), list, index);
#endif
    tinypy_internal_list_shrink(list);
    TINYPY_DECREF(previous);
}
//////////////////////////////////////////////////////////////////////////
/* list_ass_slice and list_ass_subscript copy the items they remove aside and
   release them only once the list is consistent again, so a finalizer that
   mutates the list cannot observe a half-edited layout. A few items stay on
   the stack, as in recycle_on_stack; more take a transient array that the
   heap budget does not refuse, as it does not refuse the storage a shrinking
   list moves into: removing items never fails for want of budget. */
static tinypy_value_t **__tinypy_list_recycle_begin(tinypy_vm_t *vm, tinypy_value_t **inline_items, size_t count, tinypy_error_t **out_error) {
    if (count <= TINYPY_LIST_RECYCLE_SIZE) {
        return inline_items;
    }
    tinypy_value_t **recycle = (tinypy_value_t **)tinypy_internal_vm_allocate(vm, count * sizeof(*recycle));
    if (recycle == NULL) {
        tinypy_internal_vm_allocation_error(vm, out_error);
    }
    return recycle;
}
//////////////////////////////////////////////////////////////////////////
/* list_ass_slice releases the removed items from the last one, the extended
   slice operations from the first. */
static void __tinypy_list_recycle_end(tinypy_vm_t *vm, tinypy_value_t **recycle, tinypy_value_t **inline_items, size_t count, tinypy_bool_t backwards) {
    for (size_t index = 0U; index < count; ++index) {
        TINYPY_DECREF(recycle[backwards != 0 ? count - 1U - index : index]);
    }
    if (recycle != inline_items) {
        tinypy_internal_vm_deallocate(vm, recycle, count * sizeof(*recycle));
    }
}
//////////////////////////////////////////////////////////////////////////
/* items must not alias the list storage; callers pass a materialized copy. */
tinypy_bool_t tinypy_internal_list_replace_range_checked(tinypy_value_t *list, size_t start, size_t count, tinypy_value_t *const *items, size_t item_count, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(list);
    tinypy_list_object_t *object = TINYPY_LIST_OBJECT(list);
    size_t size = TINYPY_SIZED_SIZE(list);
    size_t tail = size - start - count;
    size_t new_size;
    size_t index;

    /* list_ass_slice leaves the list untouched when nothing is replaced and
       clears one that loses all its items. */
    if (count == 0U && item_count == 0U) {
        return TINYPY_TRUE;
    }
    if (item_count > count && item_count - count > SIZE_MAX / sizeof(tinypy_value_t *) - size) {
        tinypy_internal_make_vm_error(vm, TINYPY_ERROR_MEMORY, "list is too large", out_error);
        return TINYPY_FALSE;
    }
    new_size = size - count + item_count;
    if (new_size == 0U) {
        tinypy_list_clear(list);
        return TINYPY_TRUE;
    }
    if (new_size > size && tinypy_internal_list_reserve_checked(vm, list, new_size, out_error) == 0) {
        return TINYPY_FALSE;
    }
    /* A list that shrinks moves into its smaller storage, and the storage it
       leaves holds the removed items. */
    tinypy_value_t **storage = object->items;
    size_t allocated = object->allocated;
    size_t capacity = __tinypy_internal_list_shrunk_capacity(new_size, allocated);
    tinypy_value_t **shrunk = NULL;
    if (capacity != allocated) {
        shrunk = (tinypy_value_t **)tinypy_internal_vm_allocate(vm, __tinypy_internal_list_storage_size(capacity));
    }
    tinypy_value_t *inline_items[TINYPY_LIST_RECYCLE_SIZE];
    tinypy_value_t **recycle = storage + start;
    if (shrunk == NULL) {
        recycle = __tinypy_list_recycle_begin(vm, inline_items, count, out_error);
        if (recycle == NULL) {
            return TINYPY_FALSE;
        }
        if (count != 0U) {
            (void)memcpy(recycle, storage + start, count * sizeof(*recycle));
        }
    }
#if defined(TINYPY_CYCLE_DIAGNOSTICS)
    for (index = 0U; index < count; ++index) {
        __tinypy_internal_cycle_diagnostics_list_remove(vm, list, start);
    }
#endif
    for (index = 0U; index < item_count; ++index) {
        TINYPY_INCREF(items[index]);
    }
    if (shrunk != NULL) {
        (void)memcpy(shrunk, storage, start * sizeof(tinypy_value_t *));
        (void)memcpy(shrunk + start + item_count, storage + start + count, tail * sizeof(tinypy_value_t *));
        object->items = shrunk;
        object->allocated = capacity;
    }
    else if (tail != 0U && item_count != count) {
        (void)memmove(storage + start + item_count, storage + start + count, tail * sizeof(tinypy_value_t *));
    }
    if (item_count != 0U) {
        (void)memcpy(object->items + start, items, item_count * sizeof(tinypy_value_t *));
    }
    if (shrunk == NULL && new_size < size) {
        (void)memset(storage + new_size, 0, (size - new_size) * sizeof(tinypy_value_t *));
    }
    object->base.size = new_size;
    object->mutation_version += UINT64_C(1);
#if defined(TINYPY_CYCLE_DIAGNOSTICS)
    for (index = 0U; index < item_count; ++index) {
        __tinypy_internal_cycle_diagnostics_list_insert(vm, list, start + index, items[index]);
    }
#endif
    if (shrunk == NULL) {
        tinypy_internal_list_shrink(list);
        __tinypy_list_recycle_end(vm, recycle, inline_items, count, TINYPY_TRUE);
        return TINYPY_TRUE;
    }
    for (index = count; index != 0U; --index) {
        TINYPY_DECREF(recycle[index - 1U]);
    }
    tinypy_internal_vm_deallocate(vm, storage, __tinypy_internal_list_storage_size(allocated));
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_list_replace_strided_checked(tinypy_value_t *list, size_t start, int64_t step, size_t count, tinypy_value_t *const *items, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(list);
    tinypy_list_object_t *object = TINYPY_LIST_OBJECT(list);
    size_t index;

    if (count == 0U) {
        return TINYPY_TRUE;
    }
    tinypy_value_t *inline_items[TINYPY_LIST_RECYCLE_SIZE];
    tinypy_value_t **recycle = __tinypy_list_recycle_begin(vm, inline_items, count, out_error);
    if (recycle == NULL) {
        return TINYPY_FALSE;
    }
    for (index = 0U; index < count; ++index) {
        size_t position = (size_t)((int64_t)start + (int64_t)index * step);

        TINYPY_INCREF(items[index]);
        recycle[index] = object->items[position];
        object->items[position] = items[index];
#if defined(TINYPY_CYCLE_DIAGNOSTICS)
        __tinypy_internal_cycle_diagnostics_list_set(vm, list, position, items[index]);
#endif
    }
    object->mutation_version += UINT64_C(1);
    __tinypy_list_recycle_end(vm, recycle, inline_items, count, TINYPY_FALSE);
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_internal_list_delete_strided_checked(tinypy_value_t *list, size_t start, int64_t step, size_t count, tinypy_error_t **out_error) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(list);
    tinypy_list_object_t *object = TINYPY_LIST_OBJECT(list);
    size_t size = TINYPY_SIZED_SIZE(list);
    size_t stride;
    size_t index;

    if (count == 0U) {
        return TINYPY_TRUE;
    }
    /* Walk the removed positions in increasing order whatever the sign of
       the step, as list_ass_subscript does in Python 2.7. */
    if (step < 0) {
        start = (size_t)((int64_t)start + (int64_t)(count - 1U) * step);
        stride = (size_t)-step;
    }
    else {
        stride = (size_t)step;
    }
    tinypy_value_t *inline_items[TINYPY_LIST_RECYCLE_SIZE];
    tinypy_value_t **recycle = __tinypy_list_recycle_begin(vm, inline_items, count, out_error);
    if (recycle == NULL) {
        return TINYPY_FALSE;
    }
#if defined(TINYPY_CYCLE_DIAGNOSTICS)
    for (index = count; index != 0U; --index) {
        __tinypy_internal_cycle_diagnostics_list_remove(vm, list, start + (index - 1U) * stride);
    }
#endif
    for (index = 0U; index < count; ++index) {
        size_t current = start + index * stride;
        size_t next = index + 1U < count ? current + stride : size;
        size_t kept = next - current - 1U;

        recycle[index] = object->items[current];
        if (kept != 0U) {
            (void)memmove(object->items + current - index, object->items + current + 1U, kept * sizeof(tinypy_value_t *));
        }
    }
    (void)memset(object->items + size - count, 0, count * sizeof(tinypy_value_t *));
    object->base.size = size - count;
    object->mutation_version += UINT64_C(1);
    tinypy_internal_list_shrink(list);
    __tinypy_list_recycle_end(vm, recycle, inline_items, count, TINYPY_FALSE);
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_list_pop(tinypy_value_t *list, size_t index) {
    size_t move_count;

    tinypy_value_t *item = TINYPY_LIST_OBJECT(list)->items[index];
    move_count = TINYPY_SIZED_SIZE(list) - index - 1U;
    if (move_count != 0U) {
        (void)memmove(
            TINYPY_LIST_OBJECT(list)->items + index,
            TINYPY_LIST_OBJECT(list)->items + index + 1U,
            move_count * sizeof(tinypy_value_t *));
    }
    TINYPY_LIST_OBJECT(list)->base.size -= 1U;
    TINYPY_LIST_OBJECT(list)->items[TINYPY_SIZED_SIZE(list)] = NULL;
    TINYPY_LIST_OBJECT(list)->mutation_version += UINT64_C(1);
#if defined(TINYPY_CYCLE_DIAGNOSTICS)
    __tinypy_internal_cycle_diagnostics_list_remove(TINYPY_VALUE_VM(list), list, index);
#endif
    tinypy_internal_list_shrink(list);
    return item;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_list_clear(tinypy_value_t *list) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(list);
    tinypy_value_t **items = TINYPY_LIST_OBJECT(list)->items;
    size_t item_count = TINYPY_SIZED_SIZE(list);
    size_t allocated = TINYPY_LIST_OBJECT(list)->allocated;

    /* list_clear leaves a list without storage untouched. */
    if (items == NULL) {
        return;
    }
    TINYPY_LIST_OBJECT(list)->items = NULL;
    TINYPY_LIST_OBJECT(list)->allocated = 0U;
    TINYPY_LIST_OBJECT(list)->base.size = 0U;
    TINYPY_LIST_OBJECT(list)->mutation_version += UINT64_C(1);
#if defined(TINYPY_CYCLE_DIAGNOSTICS)
    __tinypy_internal_cycle_diagnostics_list_clear(vm, list);
#endif
    /* list_clear releases the items from the last one. */
    for (size_t index = item_count; index != 0U; --index) {
        TINYPY_DECREF(items[index - 1U]);
    }
    tinypy_internal_vm_deallocate(vm, items, allocated * sizeof(*items));
}
