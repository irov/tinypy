#include "tinypy/cell.h"

#include "internal.h"

#include <assert.h>

//////////////////////////////////////////////////////////////////////////
static tinypy_cell_object_t *__tinypy_internal_cell_validate(const tinypy_value_t *value) {
    assert(value != NULL);
    assert(tinypy_internal_vm_valid(tinypy_internal_value_vm(value)));
    assert(tinypy_internal_value_kind(value) == TINYPY_VALUE_CELL);
    return TINYPY_CELL_OBJECT((tinypy_value_t *)value);
}

//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_cell_new(tinypy_vm_t *vm, tinypy_value_t *content) {
    tinypy_cell_object_t *cell;

    assert(tinypy_internal_vm_valid(vm));
    assert(content == NULL || tinypy_internal_value_belongs_to(vm, content));
    cell = (tinypy_cell_object_t *)tinypy_internal_value_allocate(vm, TINYPY_VALUE_CELL, sizeof(*cell));
    cell->content = content;
    if (content != NULL) {
        tinypy_retain(content);
    }
    return &cell->base;
}

//////////////////////////////////////////////////////////////////////////
void tinypy_internal_cell_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data) {
    tinypy_cell_object_t *cell = TINYPY_CELL_OBJECT(value);

    if (cell->content != NULL) {
        visit(cell->content, user_data);
    }
}

//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_cell_get(const tinypy_value_t *cell) {
    return __tinypy_internal_cell_validate(cell)->content;
}

//////////////////////////////////////////////////////////////////////////
void tinypy_cell_set(tinypy_value_t *cell_value, tinypy_value_t *content) {
    tinypy_cell_object_t *cell = __tinypy_internal_cell_validate(cell_value);
    tinypy_value_t *previous = cell->content;

    assert(content == NULL || tinypy_internal_value_belongs_to(tinypy_internal_value_vm(cell_value), content));
    if (content != NULL) {
        tinypy_retain(content);
    }
    cell->content = content;
    if (previous != NULL) {
        tinypy_release(previous);
    }
}
