#ifndef TINYPY_CELL_H
#define TINYPY_CELL_H

#include "tinypy/types.h"

/* content may be NULL for an empty cell. get returns a borrowed value or
 * NULL when empty. set retains the new content and releases the old one. */
tinypy_value_t *tinypy_cell_new(tinypy_vm_t *vm, tinypy_value_t *content);
tinypy_value_t *tinypy_cell_get(const tinypy_value_t *cell);
void tinypy_cell_set(tinypy_value_t *cell, tinypy_value_t *content);

#endif
