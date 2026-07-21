#include "tinypy/tuple.h"

#include "internal.h"

#include <string.h>
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_tuple_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data) {
    tinypy_value_t **items = TINYPY_TUPLE_OBJECT(value)->items;
    size_t size = (size_t)TINYPY_SIZE(value);
    size_t index;

    for (index = 0U; index < size; index += 1U) {
        visit(items[index], user_data);
    }
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *const *tinypy_internal_tuple_items(const tinypy_value_t *value) {
    tinypy_vm_t *vm = tinypy_internal_value_vm(value);

    assert(tinypy_internal_value_kind(value) == TINYPY_VALUE_TUPLE);
    return value->type == &vm->tuple_type ? TINYPY_TUPLE_OBJECT(value)->items : TINYPY_TUPLE_SUBCLASS_OBJECT(value)->items;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_tuple_subclass_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data) {
    tinypy_value_t *const *items = tinypy_internal_tuple_items(value);
    tinypy_value_t **dict_slot = tinypy_internal_object_dict_slot(value);
    size_t index;

    for (index = 0U; index < (size_t)TINYPY_SIZE(value); index += 1U) {
        visit(items[index], user_data);
    }
    if (dict_slot != NULL && *dict_slot != NULL) {
        visit(*dict_slot, user_data);
    }
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_tuple_subclass_destroy(tinypy_vm_t *vm, tinypy_value_t *value) {
    tinypy_tuple_subclass_object_t *tuple = TINYPY_TUPLE_SUBCLASS_OBJECT(value);

    if (tuple->items == NULL) {
        return;
    }
    tinypy_internal_vm_deallocate(vm, tuple->items, (size_t)TINYPY_SIZE(value) * sizeof(*tuple->items), (uint32_t)TINYPY_ALLOC_TAG_TUPLE_ITEMS);
    tuple->items = NULL;
    tuple->base.size = 0;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_tuple_subclass_from_items(tinypy_type_t *type, tinypy_value_t *const *items, size_t size) {
    tinypy_vm_t *vm = type->vm;
    tinypy_tuple_subclass_object_t *tuple;
    size_t index;

    assert(type->layout_kind == TINYPY_VALUE_TUPLE);
    assert(type != &vm->tuple_type);
    assert(items != NULL || size == 0U);
    assert(size <= (size_t)PTRDIFF_MAX);
    tuple = (tinypy_tuple_subclass_object_t *)tinypy_internal_object_allocate(vm, type, type->basic_size);
    tuple->base.size = (ptrdiff_t)size;
    if (size != 0U) {
        tuple->items = (tinypy_value_t **)tinypy_internal_vm_allocate(vm, size * sizeof(*tuple->items), (uint32_t)TINYPY_ALLOC_TAG_TUPLE_ITEMS);
    }
    for (index = 0U; index < size; index += 1U) {
        assert(tinypy_internal_value_belongs_to(vm, items[index]));
        tuple->items[index] = items[index];
        tinypy_retain(items[index]);
    }
    return &tuple->base.base;
}
//////////////////////////////////////////////////////////////////////////
static size_t __tinypy_internal_tuple_allocation_size(size_t item_count) {
    size_t payload_size;

    assert(item_count <= (size_t)PTRDIFF_MAX);
    assert(item_count <=
           (SIZE_MAX - offsetof(tinypy_tuple_object_t, items)) /
               sizeof(tinypy_value_t *));

    payload_size = item_count * sizeof(tinypy_value_t *);
    return offsetof(tinypy_tuple_object_t, items) + payload_size;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_tuple_from_borrowed_items(tinypy_vm_t *vm, tinypy_value_t *const *items, size_t size) {
    size_t allocation_size;
    tinypy_value_t *result;
    size_t index;

    assert(items != NULL || size == 0U);
    if (size == 0U) {
        result = &vm->empty_tuple_object.base.base;
        tinypy_retain(result);
        return result;
    }
    for (index = 0U; index < size; index += 1U) {
        assert(tinypy_internal_value_belongs_to(vm, items[index]));
    }
    allocation_size = __tinypy_internal_tuple_allocation_size(size);
    result = tinypy_internal_value_allocate(
        vm, TINYPY_VALUE_TUPLE, allocation_size);
    TINYPY_SIZE(result) = (ptrdiff_t)size;
    if (size != 0U) {
        (void)memcpy(
            TINYPY_TUPLE_OBJECT(result)->items,
            items,
            size * sizeof(*items));
    }
    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_tuple_new(tinypy_vm_t *vm, size_t size) {
    size_t allocation_size;
    size_t index;
    tinypy_value_t *result;
    tinypy_value_t **items;

    assert(tinypy_internal_vm_valid(vm));
    if (size == 0U) {
        result = &vm->empty_tuple_object.base.base;
        tinypy_retain(result);
        return result;
    }

    allocation_size = __tinypy_internal_tuple_allocation_size(size);
    result = tinypy_internal_value_allocate(vm, TINYPY_VALUE_TUPLE, allocation_size);
    TINYPY_SIZE(result) = (ptrdiff_t)size;
    items = TINYPY_TUPLE_OBJECT(result)->items;

    for (index = 0U; index < size; index += 1U) {
        items[index] = tinypy_none_get(vm);
    }

    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_tuple_from_items(tinypy_vm_t *vm, tinypy_value_t *const *items, size_t size) {
    size_t allocation_size;
    size_t index;
    tinypy_value_t *result;
    tinypy_value_t **copied_items;

    assert(tinypy_internal_vm_valid(vm));
    assert(items != NULL || size == 0U);
    if (size == 0U) {
        result = &vm->empty_tuple_object.base.base;
        tinypy_retain(result);
        return result;
    }
    allocation_size = __tinypy_internal_tuple_allocation_size(size);
    for (index = 0U; index < size; index += 1U) {
        assert(tinypy_internal_value_belongs_to(vm, items[index]));
    }

    result = tinypy_internal_value_allocate(
        vm,
        TINYPY_VALUE_TUPLE,
        allocation_size);
    TINYPY_SIZE(result) = (ptrdiff_t)size;

    copied_items = TINYPY_TUPLE_OBJECT(result)->items;
    for (index = 0U; index < size; index += 1U) {
        tinypy_retain(items[index]);
        copied_items[index] = items[index];
    }

    return result;
}
//////////////////////////////////////////////////////////////////////////
size_t tinypy_tuple_size(const tinypy_value_t *value) {
    assert(value != NULL);
    assert(tinypy_internal_vm_valid(tinypy_internal_value_vm(value)));
    assert(tinypy_internal_value_kind(value) == TINYPY_VALUE_TUPLE);

    return (size_t)TINYPY_SIZE(value);
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_tuple_get(const tinypy_value_t *value, size_t index) {
    tinypy_value_t *const *items;

    assert(value != NULL);
    assert(tinypy_internal_vm_valid(tinypy_internal_value_vm(value)));
    assert(tinypy_internal_value_kind(value) == TINYPY_VALUE_TUPLE);
    assert(index < (size_t)TINYPY_SIZE(value));

    items = tinypy_internal_tuple_items(value);
    return items[index];
}
//////////////////////////////////////////////////////////////////////////
void tinypy_tuple_set(tinypy_value_t *value, size_t index, tinypy_value_t *item) {
    tinypy_vm_t *vm;
    tinypy_value_t **items;
    tinypy_value_t *previous;

    assert(value != NULL);
    vm = tinypy_internal_value_vm(value);
    assert(tinypy_internal_vm_valid(vm));
    assert(value->type == &vm->tuple_type);
    assert(index < (size_t)TINYPY_SIZE(value));
    assert(tinypy_internal_value_belongs_to(vm, item));
    assert(value != item);

    items = TINYPY_TUPLE_OBJECT(value)->items;
    previous = items[index];
    tinypy_retain(item);
    items[index] = item;
    tinypy_release(previous);
}
