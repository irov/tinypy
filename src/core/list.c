#include "tinypy/list.h"

#include "internal.h"

#include <string.h>

//////////////////////////////////////////////////////////////////////////
void tinypy_internal_list_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data) {
    tinypy_value_t **items = TINYPY_LIST_OBJECT(value)->items;
    size_t size = (size_t)TINYPY_SIZE(value);
    size_t index;

    for (index = 0U; index < size; index += 1U) {
        visit(items[index], user_data);
    }
}

//////////////////////////////////////////////////////////////////////////
void tinypy_internal_list_destroy(tinypy_vm_t *vm, tinypy_value_t *value) {
    if (TINYPY_LIST_OBJECT(value)->items != NULL) {
        tinypy_internal_vm_deallocate(
            vm,
            TINYPY_LIST_OBJECT(value)->items,
            TINYPY_LIST_OBJECT(value)->allocated * sizeof(tinypy_value_t *),
            (uint32_t)TINYPY_ALLOC_TAG_LIST_ITEMS);
        TINYPY_LIST_OBJECT(value)->items = NULL;
        TINYPY_LIST_OBJECT(value)->allocated = 0U;
        TINYPY_LIST_OBJECT(value)->base.size = 0U;
    }
}

//////////////////////////////////////////////////////////////////////////
static void __tinypy_internal_list_validate(const tinypy_value_t *list) {
    assert(list != NULL);
    assert(tinypy_internal_value_kind(list) == TINYPY_VALUE_LIST);
    (void)list;
}

//////////////////////////////////////////////////////////////////////////
static size_t __tinypy_internal_list_storage_size(size_t capacity) {
    assert(capacity <= (size_t)PTRDIFF_MAX);
    assert(capacity <= SIZE_MAX / sizeof(tinypy_value_t *));
    return capacity * sizeof(tinypy_value_t *);
}

//////////////////////////////////////////////////////////////////////////
static void __tinypy_internal_list_reserve(tinypy_vm_t *vm, tinypy_value_t *list, size_t minimum_capacity) {
    size_t old_size;
    size_t new_size;
    size_t new_capacity;
    size_t extra;
    tinypy_value_t **items;

    if (minimum_capacity <= TINYPY_LIST_OBJECT(list)->allocated) {
        return;
    }
    (void)__tinypy_internal_list_storage_size(minimum_capacity);

    extra = (minimum_capacity >> 3U) + (minimum_capacity < 9U ? 3U : 6U);
    if (minimum_capacity > SIZE_MAX - extra) {
        new_capacity = minimum_capacity;
    }
    else {
        new_capacity = minimum_capacity + extra;
    }
    if (new_capacity > (size_t)PTRDIFF_MAX || new_capacity > SIZE_MAX / sizeof(tinypy_value_t *)) {
        new_capacity = minimum_capacity;
    }
    new_size = __tinypy_internal_list_storage_size(new_capacity);

    if (TINYPY_LIST_OBJECT(list)->items == NULL) {
        items = (tinypy_value_t **)tinypy_internal_vm_allocate(
            vm,
            new_size,
            (uint32_t)TINYPY_ALLOC_TAG_LIST_ITEMS);
    }
    else {
        old_size = __tinypy_internal_list_storage_size(
            TINYPY_LIST_OBJECT(list)->allocated);
        items = (tinypy_value_t **)tinypy_internal_vm_reallocate(
            vm,
            TINYPY_LIST_OBJECT(list)->items,
            old_size,
            new_size,
            (uint32_t)TINYPY_ALLOC_TAG_LIST_ITEMS);
    }
    TINYPY_LIST_OBJECT(list)->items = items;
    TINYPY_LIST_OBJECT(list)->allocated = new_capacity;
    return;
}

//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_list_from_items(tinypy_vm_t *vm, tinypy_value_t *const *items, size_t size) {
    tinypy_value_t *result;
    size_t storage_size;
    size_t index;

    assert(tinypy_internal_vm_valid(vm));
    assert(items != NULL || size == 0U);
    storage_size = __tinypy_internal_list_storage_size(size);
    for (index = 0U; index < size; index += 1U) {
        assert(tinypy_internal_value_belongs_to(vm, items[index]));
    }

    result = tinypy_internal_value_allocate(
        vm,
        TINYPY_VALUE_LIST,
        sizeof(tinypy_list_object_t));
    if (size != 0U) {
        TINYPY_LIST_OBJECT(result)->items =
            (tinypy_value_t **)tinypy_internal_vm_allocate(
                vm,
                storage_size,
                (uint32_t)TINYPY_ALLOC_TAG_LIST_ITEMS);
        TINYPY_LIST_OBJECT(result)->allocated = size;
    }

    for (index = 0U; index < size; index += 1U) {
        tinypy_retain(items[index]);
        TINYPY_LIST_OBJECT(result)->items[index] = items[index];
    }

    TINYPY_SIZE(result) = (ptrdiff_t)size;
    return result;
}

//////////////////////////////////////////////////////////////////////////
size_t tinypy_list_size(const tinypy_value_t *value) {
    assert(tinypy_internal_vm_valid(tinypy_internal_value_vm(value)));
    __tinypy_internal_list_validate(value);

    return (size_t)TINYPY_SIZE(value);
}

//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_list_get(const tinypy_value_t *value, size_t index) {
    assert(tinypy_internal_vm_valid(tinypy_internal_value_vm(value)));
    __tinypy_internal_list_validate(value);
    assert(index < (size_t)TINYPY_SIZE(value));

    return TINYPY_LIST_OBJECT(value)->items[index];
}

//////////////////////////////////////////////////////////////////////////
uint64_t tinypy_list_version(const tinypy_value_t *value) {
    assert(tinypy_internal_vm_valid(tinypy_internal_value_vm(value)));
    __tinypy_internal_list_validate(value);

    return TINYPY_LIST_OBJECT(value)->mutation_version;
}

//////////////////////////////////////////////////////////////////////////
void tinypy_list_extend(tinypy_value_t *list, tinypy_value_t *const *items, size_t item_count) {
    tinypy_vm_t *vm;
    size_t new_size;
    size_t index;

    assert(tinypy_internal_vm_valid(tinypy_internal_value_vm(list)));
    __tinypy_internal_list_validate(list);
    assert(items != NULL || item_count == 0U);
    for (index = 0U; index < item_count; index += 1U) {
        assert(tinypy_internal_value_belongs_to(
            tinypy_internal_value_vm(list), items[index]));
    }
    if (item_count == 0U) {
        return;
    }
    assert(TINYPY_LIST_OBJECT(list)->mutation_version != UINT64_MAX);
    assert(item_count <= (size_t)PTRDIFF_MAX);
    assert((size_t)TINYPY_SIZE(list) <=
           (size_t)PTRDIFF_MAX - item_count);
    new_size = (size_t)TINYPY_SIZE(list) + item_count;

    for (index = 0U; index < item_count; index += 1U) {
        tinypy_retain(items[index]);
    }

    vm = tinypy_internal_value_vm(list);
    __tinypy_internal_list_reserve(vm, list, new_size);

    (void)memcpy(
        TINYPY_LIST_OBJECT(list)->items + (size_t)TINYPY_SIZE(list),
        items,
        item_count * sizeof(*items));
    TINYPY_SIZE(list) = (ptrdiff_t)new_size;
    TINYPY_LIST_OBJECT(list)->mutation_version += UINT64_C(1);
}

//////////////////////////////////////////////////////////////////////////
void tinypy_list_append(tinypy_value_t *list, tinypy_value_t *item) {
    tinypy_value_t *items[1];

    items[0] = item;
    tinypy_list_extend(list, items, 1U);
}

//////////////////////////////////////////////////////////////////////////
void tinypy_list_insert(tinypy_value_t *list, size_t index, tinypy_value_t *item) {
    tinypy_vm_t *vm;
    size_t move_count;

    assert(tinypy_internal_vm_valid(tinypy_internal_value_vm(list)));
    __tinypy_internal_list_validate(list);
    assert(index <= (size_t)TINYPY_SIZE(list));
    assert(tinypy_internal_value_belongs_to(
        tinypy_internal_value_vm(list), item));
    assert(TINYPY_LIST_OBJECT(list)->mutation_version != UINT64_MAX);
    assert(TINYPY_SIZE(list) != PTRDIFF_MAX);

    tinypy_retain(item);
    vm = tinypy_internal_value_vm(list);
    __tinypy_internal_list_reserve(
        vm, list, (size_t)TINYPY_SIZE(list) + 1U);

    move_count = (size_t)TINYPY_SIZE(list) - index;
    if (move_count != 0U) {
        (void)memmove(
            TINYPY_LIST_OBJECT(list)->items + index + 1U,
            TINYPY_LIST_OBJECT(list)->items + index,
            move_count * sizeof(tinypy_value_t *));
    }
    TINYPY_LIST_OBJECT(list)->items[index] = item;
    TINYPY_LIST_OBJECT(list)->base.size += 1U;
    TINYPY_LIST_OBJECT(list)->mutation_version += UINT64_C(1);
}

//////////////////////////////////////////////////////////////////////////
void tinypy_list_set(tinypy_value_t *list, size_t index, tinypy_value_t *item) {
    tinypy_value_t *previous;

    assert(tinypy_internal_vm_valid(tinypy_internal_value_vm(list)));
    __tinypy_internal_list_validate(list);
    assert(index < (size_t)TINYPY_SIZE(list));
    assert(tinypy_internal_value_belongs_to(
        tinypy_internal_value_vm(list), item));
    assert(TINYPY_LIST_OBJECT(list)->mutation_version != UINT64_MAX);
    tinypy_retain(item);

    previous = TINYPY_LIST_OBJECT(list)->items[index];
    TINYPY_LIST_OBJECT(list)->items[index] = item;
    TINYPY_LIST_OBJECT(list)->mutation_version += UINT64_C(1);
    tinypy_release(previous);
}

//////////////////////////////////////////////////////////////////////////
void tinypy_list_delete(tinypy_value_t *list, size_t index) {
    tinypy_value_t *previous;
    size_t move_count;

    assert(tinypy_internal_vm_valid(tinypy_internal_value_vm(list)));
    __tinypy_internal_list_validate(list);
    assert(index < (size_t)TINYPY_SIZE(list));
    assert(TINYPY_LIST_OBJECT(list)->mutation_version != UINT64_MAX);

    previous = TINYPY_LIST_OBJECT(list)->items[index];
    move_count = (size_t)TINYPY_SIZE(list) - index - 1U;
    if (move_count != 0U) {
        (void)memmove(
            TINYPY_LIST_OBJECT(list)->items + index,
            TINYPY_LIST_OBJECT(list)->items + index + 1U,
            move_count * sizeof(tinypy_value_t *));
    }
    TINYPY_LIST_OBJECT(list)->base.size -= 1U;
    TINYPY_LIST_OBJECT(list)->items[(size_t)TINYPY_SIZE(list)] = NULL;
    TINYPY_LIST_OBJECT(list)->mutation_version += UINT64_C(1);
    tinypy_release(previous);
}

//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_list_pop(tinypy_value_t *list, size_t index) {
    tinypy_value_t *item;
    size_t move_count;

    assert(tinypy_internal_vm_valid(tinypy_internal_value_vm(list)));
    __tinypy_internal_list_validate(list);
    assert(index < (size_t)TINYPY_SIZE(list));
    assert(TINYPY_LIST_OBJECT(list)->mutation_version != UINT64_MAX);

    item = TINYPY_LIST_OBJECT(list)->items[index];
    move_count = (size_t)TINYPY_SIZE(list) - index - 1U;
    if (move_count != 0U) {
        (void)memmove(
            TINYPY_LIST_OBJECT(list)->items + index,
            TINYPY_LIST_OBJECT(list)->items + index + 1U,
            move_count * sizeof(tinypy_value_t *));
    }
    TINYPY_LIST_OBJECT(list)->base.size -= 1U;
    TINYPY_LIST_OBJECT(list)->items[(size_t)TINYPY_SIZE(list)] = NULL;
    TINYPY_LIST_OBJECT(list)->mutation_version += UINT64_C(1);
    return item;
}

//////////////////////////////////////////////////////////////////////////
void tinypy_list_clear(tinypy_value_t *list) {
    size_t item_count;
    size_t index;

    assert(tinypy_internal_vm_valid(tinypy_internal_value_vm(list)));
    __tinypy_internal_list_validate(list);
    if (TINYPY_SIZE(list) == 0) {
        return;
    }
    assert(TINYPY_LIST_OBJECT(list)->mutation_version != UINT64_MAX);

    item_count = (size_t)TINYPY_SIZE(list);
    TINYPY_LIST_OBJECT(list)->base.size = 0U;
    TINYPY_LIST_OBJECT(list)->mutation_version += UINT64_C(1);
    for (index = 0U; index < item_count; index += 1U) {
        tinypy_value_t *item = TINYPY_LIST_OBJECT(list)->items[index];
        TINYPY_LIST_OBJECT(list)->items[index] = NULL;
        tinypy_release(item);
    }
}
