#include "tinypy/cell.h"

#include "internal.h"

#include <assert.h>

//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_cell_new(tinypy_vm_t *vm, tinypy_value_t *content) {
    tinypy_cell_object_t *cell;

    assert(tinypy_internal_vm_valid(vm));
    assert(content == NULL || tinypy_internal_value_belongs_to(vm, content));
    cell = (tinypy_cell_object_t *)tinypy_internal_value_allocate(vm, TINYPY_VALUE_CELL, sizeof(*cell));
    cell->content = content;
    if (content != NULL) {
        TINYPY_INCREF(content);
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
    assert(cell != NULL);
    assert(tinypy_internal_vm_valid(TINYPY_VALUE_VM(cell)));
    assert(TINYPY_VALUE_KIND(cell) == TINYPY_VALUE_CELL);
    return TINYPY_CELL_OBJECT((tinypy_value_t *)cell)->content;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_cell_set(tinypy_value_t *cell_value, tinypy_value_t *content) {
    tinypy_cell_object_t *cell;
    tinypy_value_t *previous;

    assert(cell_value != NULL);
    assert(tinypy_internal_vm_valid(TINYPY_VALUE_VM(cell_value)));
    assert(TINYPY_VALUE_KIND(cell_value) == TINYPY_VALUE_CELL);
    assert(content == NULL || tinypy_internal_value_belongs_to(TINYPY_VALUE_VM(cell_value), content));
    cell = TINYPY_CELL_OBJECT(cell_value);
    previous = cell->content;
    if (content != NULL) {
        TINYPY_INCREF(content);
    }
    cell->content = content;
    if (previous != NULL) {
        TINYPY_DECREF(previous);
    }
}
