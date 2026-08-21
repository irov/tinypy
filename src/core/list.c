#include "tinypy/list.h"

#include "internal.h"

#include <string.h>

//////////////////////////////////////////////////////////////////////////
void tinypy_internal_list_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data) {
    tinypy_value_t **items = TINYPY_LIST_OBJECT(value)->items;
    size_t size = TINYPY_SIZED_SIZE(value);
    size_t index;

    for (index = 0U; index < size; ++index) {
        visit(items[index], user_data);
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
void tinypy_internal_list_swap_contents(tinypy_value_t *left, tinypy_value_t *right) {
    tinypy_value_t **items = TINYPY_LIST_OBJECT(left)->items;
    size_t size = TINYPY_SIZED_SIZE(left);
    size_t allocated = TINYPY_LIST_OBJECT(left)->allocated;
    uint64_t mutation_version = TINYPY_LIST_OBJECT(left)->mutation_version;

    TINYPY_LIST_OBJECT(left)->items = TINYPY_LIST_OBJECT(right)->items;
    TINYPY_SIZED_SIZE(left) = TINYPY_SIZED_SIZE(right);
    TINYPY_LIST_OBJECT(left)->allocated = TINYPY_LIST_OBJECT(right)->allocated;
    TINYPY_LIST_OBJECT(left)->mutation_version = TINYPY_LIST_OBJECT(right)->mutation_version;
    TINYPY_LIST_OBJECT(right)->items = items;
    TINYPY_SIZED_SIZE(right) = size;
    TINYPY_LIST_OBJECT(right)->allocated = allocated;
    TINYPY_LIST_OBJECT(right)->mutation_version = mutation_version;
}
//////////////////////////////////////////////////////////////////////////
static inline size_t __tinypy_internal_list_storage_size(size_t capacity) {
    return capacity * sizeof(tinypy_value_t *);
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

    extra = (minimum_capacity >> 3U) + (minimum_capacity < 9U ? 3U : 6U);
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
                    ? (tinypy_value_t **)tinypy_internal_vm_allocate_checked(vm, new_size, out_error)
                    : (tinypy_value_t **)tinypy_internal_vm_allocate(vm, new_size);
    }
    else {
        old_size = __tinypy_internal_list_storage_size(
            TINYPY_LIST_OBJECT(list)->allocated);
        items = checked != 0
                    ? (tinypy_value_t **)tinypy_internal_vm_reallocate_checked(vm, TINYPY_LIST_OBJECT(list)->items, old_size, new_size, out_error)
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
void tinypy_internal_list_shrink_to_fit(tinypy_vm_t *vm, tinypy_value_t *list) {
    tinypy_list_object_t *list_object = TINYPY_LIST_OBJECT(list);
    size_t size = TINYPY_LIST_SIZE(list);
    size_t allocated = list_object->allocated;

    if (allocated == size) {
        return;
    }
    if (size == 0U) {
        if (list_object->items != NULL) {
            tinypy_internal_vm_deallocate(vm, list_object->items, __tinypy_internal_list_storage_size(allocated));
            list_object->items = NULL;
        }
        list_object->allocated = 0U;
        return;
    }
    list_object->items = (tinypy_value_t **)tinypy_internal_vm_reallocate(
        vm,
        list_object->items,
        __tinypy_internal_list_storage_size(allocated),
        __tinypy_internal_list_storage_size(size));
    list_object->allocated = size;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_list_from_items(tinypy_vm_t *vm, tinypy_value_t *const *items, size_t size) {
    size_t storage_size;
    size_t index;

    if (size > SIZE_MAX / sizeof(tinypy_value_t *)) {
        return NULL;
    }
    storage_size = __tinypy_internal_list_storage_size(size);

    tinypy_value_t *result = tinypy_internal_value_allocate(
        vm,
        TINYPY_VALUE_LIST,
        sizeof(tinypy_list_object_t));
    if (size != 0U) {
        TINYPY_LIST_OBJECT(result)->items =
            (tinypy_value_t **)tinypy_internal_vm_allocate(
                vm,
                storage_size);
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
    tinypy_value_t *items[1];

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
    TINYPY_DECREF(previous);
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
    return item;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_list_clear(tinypy_value_t *list) {
    size_t item_count;
    size_t index;

    if (TINYPY_SIZED_SIZE(list) == 0) {
        return;
    }

    item_count = TINYPY_SIZED_SIZE(list);
    TINYPY_LIST_OBJECT(list)->base.size = 0U;
    TINYPY_LIST_OBJECT(list)->mutation_version += UINT64_C(1);
#if defined(TINYPY_CYCLE_DIAGNOSTICS)
    __tinypy_internal_cycle_diagnostics_list_clear(TINYPY_VALUE_VM(list), list);
#endif
    for (index = 0U; index < item_count; ++index) {
        tinypy_value_t *item = TINYPY_LIST_OBJECT(list)->items[index];
        TINYPY_LIST_OBJECT(list)->items[index] = NULL;
        TINYPY_DECREF(item);
    }
}
