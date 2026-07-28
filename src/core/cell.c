#include "tinypy/cell.h"

#include "internal.h"

//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_cell_new(tinypy_vm_t *vm, tinypy_value_t *content) {
    tinypy_cell_object_t *cell = (tinypy_cell_object_t *)tinypy_internal_value_allocate(vm, TINYPY_VALUE_CELL, sizeof(*cell));
    cell->content = content;
    if (content != NULL) {
        TINYPY_INCREF(content);
    }
#if defined(TINYPY_CYCLE_DIAGNOSTICS)
    __tinypy_internal_cycle_diagnostics_cell_set(vm, &cell->base, content);
#endif
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
    tinypy_value_t *return_value_1 = TINYPY_CELL_OBJECT((tinypy_value_t *)cell)->content;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_cell_set(tinypy_value_t *cell_value, tinypy_value_t *content) {
    tinypy_cell_object_t *cell = TINYPY_CELL_OBJECT(cell_value);
    tinypy_value_t *previous = cell->content;
    if (content != NULL) {
        TINYPY_INCREF(content);
    }
    cell->content = content;
#if defined(TINYPY_CYCLE_DIAGNOSTICS)
    __tinypy_internal_cycle_diagnostics_cell_set(TINYPY_VALUE_VM(cell_value), cell_value, content);
#endif
    if (previous != NULL) {
        TINYPY_DECREF(previous);
    }
}
