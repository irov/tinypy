#ifndef TINYPY_LIST_H
#define TINYPY_LIST_H

#include "tinypy/types.h"

/* Mutable Python 2 list. get returns a borrowed item. pop transfers the
 * list's owned item reference to the caller. Indices are already-normalized
 * non-negative indices; insert accepts [0, size], all other indexed APIs
 * require [0, size). version changes once for every non-empty mutation. */
tinypy_value_t *tinypy_list_from_items(tinypy_vm_t *vm, tinypy_value_t *const *items, size_t size);
size_t tinypy_list_size(const tinypy_value_t *value);
tinypy_value_t *tinypy_list_get(const tinypy_value_t *value, size_t index);
uint64_t tinypy_list_version(const tinypy_value_t *value);
void tinypy_list_append(tinypy_value_t *list, tinypy_value_t *item);
void tinypy_list_extend(tinypy_value_t *list, tinypy_value_t *const *items, size_t item_count);
void tinypy_list_insert(tinypy_value_t *list, size_t index, tinypy_value_t *item);
void tinypy_list_set(tinypy_value_t *list, size_t index, tinypy_value_t *item);
void tinypy_list_delete(tinypy_value_t *list, size_t index);
tinypy_value_t *tinypy_list_pop(tinypy_value_t *list, size_t index);
void tinypy_list_clear(tinypy_value_t *list);

#endif
