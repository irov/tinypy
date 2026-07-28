#include "tinypy/tuple.h"

#include "internal.h"

#include <string.h>
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_tuple_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data) {
    tinypy_value_t **items = TINYPY_TUPLE_OBJECT(value)->items;
    size_t size = TINYPY_SIZED_SIZE(value);

    for (size_t index = 0U; index < size; ++index) {
        visit(items[index], user_data);
    }
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *const *tinypy_internal_tuple_items(const tinypy_value_t *value) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(value);

    tinypy_value_t *const *return_value_1 = value->type == &vm->types[TINYPY_VALUE_TUPLE] ? TINYPY_TUPLE_OBJECT(value)->items : TINYPY_TUPLE_SUBCLASS_OBJECT(value)->items;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_tuple_subclass_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data) {
    tinypy_value_t *const *items = tinypy_internal_tuple_items(value);
    tinypy_value_t **dict_slot = tinypy_internal_object_dict_slot(value);

    for (size_t index = 0U; index < TINYPY_SIZED_SIZE(value); ++index) {
        visit(items[index], user_data);
    }
    if (dict_slot != NULL && *dict_slot != NULL) {
        visit(*dict_slot, user_data);
    }
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_tuple_subclass_destroy(tinypy_value_t *value) {
    tinypy_tuple_subclass_object_t *tuple = TINYPY_TUPLE_SUBCLASS_OBJECT(value);

    if (tuple->items == NULL) {
        return;
    }
    tinypy_internal_vm_deallocate(TINYPY_VALUE_VM(value), tuple->items, TINYPY_SIZED_SIZE(value) * sizeof(*tuple->items));
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_tuple_subclass_from_items(tinypy_type_t *type, tinypy_value_t *const *items, size_t size) {
    tinypy_vm_t *vm = type->vm;

    tinypy_tuple_subclass_object_t *tuple = (tinypy_tuple_subclass_object_t *)tinypy_internal_object_allocate(vm, type, type->basic_size);
    tuple->base.size = size;
    if (size != 0U) {
        tuple->items = (tinypy_value_t **)tinypy_internal_vm_allocate(vm, size * sizeof(*tuple->items));
    }
    for (size_t index = 0U; index < size; ++index) {
        tuple->items[index] = items[index];
        TINYPY_INCREF(items[index]);
    }
    return &tuple->base.base;
}
//////////////////////////////////////////////////////////////////////////
static inline size_t __tinypy_internal_tuple_allocation_size(size_t item_count) {
    size_t payload_size;

    payload_size = item_count * sizeof(tinypy_value_t *);
    return offsetof(tinypy_tuple_object_t, items) + payload_size;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_tuple_from_borrowed_items(tinypy_vm_t *vm, tinypy_value_t *const *items, size_t size) {
    if (size == 0U) {
        tinypy_value_t *result = &vm->empty_tuple_object.base.base;
        TINYPY_INCREF(result);
        return result;
    }
    size_t allocation_size = __tinypy_internal_tuple_allocation_size(size);
    tinypy_value_t *result = tinypy_internal_value_allocate(
        vm, TINYPY_VALUE_TUPLE, allocation_size);
    TINYPY_SIZED_SIZE(result) = size;
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
    if (size == 0U) {
        tinypy_value_t *result = &vm->empty_tuple_object.base.base;
        TINYPY_INCREF(result);
        return result;
    }

    size_t allocation_size = __tinypy_internal_tuple_allocation_size(size);
    tinypy_value_t *result = tinypy_internal_value_allocate(vm, TINYPY_VALUE_TUPLE, allocation_size);
    TINYPY_SIZED_SIZE(result) = size;
    tinypy_value_t **items = TINYPY_TUPLE_OBJECT(result)->items;

    for (size_t index = 0U; index < size; ++index) {
        items[index] = tinypy_none_get(vm);
    }

    return result;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_tuple_from_items(tinypy_vm_t *vm, tinypy_value_t *const *items, size_t size) {
    if (size == 0U) {
        tinypy_value_t *result = &vm->empty_tuple_object.base.base;
        TINYPY_INCREF(result);
        return result;
    }
    size_t allocation_size = __tinypy_internal_tuple_allocation_size(size);

    tinypy_value_t *result = tinypy_internal_value_allocate(
        vm,
        TINYPY_VALUE_TUPLE,
        allocation_size);
    TINYPY_SIZED_SIZE(result) = size;

    tinypy_value_t **copied_items = TINYPY_TUPLE_OBJECT(result)->items;
    for (size_t index = 0U; index < size; ++index) {
        TINYPY_INCREF(items[index]);
        copied_items[index] = items[index];
    }

    return result;
}
//////////////////////////////////////////////////////////////////////////
size_t tinypy_tuple_size(const tinypy_value_t *value) {

    size_t return_value_1 = TINYPY_SIZED_SIZE(value);
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_tuple_get(const tinypy_value_t *value, size_t index) {
    tinypy_value_t *const *items;

    items = tinypy_internal_tuple_items(value);
    return items[index];
}
//////////////////////////////////////////////////////////////////////////
void tinypy_tuple_set(tinypy_value_t *value, size_t index, tinypy_value_t *item) {

    tinypy_value_t **items = TINYPY_TUPLE_OBJECT(value)->items;
    tinypy_value_t *previous = items[index];
    TINYPY_INCREF(item);
    items[index] = item;
    TINYPY_DECREF(previous);
}
