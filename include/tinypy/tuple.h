#ifndef TINYPY_TUPLE_H
#define TINYPY_TUPLE_H

#include "tinypy/types.h"
//////////////////////////////////////////////////////////////////////////
/* Tuple construction copies the item pointer array and retains every item.
 * Items must already belong to vm. tinypy_tuple_new and tinypy_tuple_set are
 * embedding-only builders; Python code still observes immutable tuples.
 * tinypy_tuple_get returns a borrowed reference. */
tinypy_value_t *tinypy_tuple_new(tinypy_vm_t *vm, size_t size);
tinypy_value_t *tinypy_tuple_from_items(tinypy_vm_t *vm, tinypy_value_t *const *items, size_t size);
size_t tinypy_tuple_size(const tinypy_value_t *value);
tinypy_value_t *tinypy_tuple_get(const tinypy_value_t *value, size_t index);
void tinypy_tuple_set(tinypy_value_t *value, size_t index, tinypy_value_t *item);
//////////////////////////////////////////////////////////////////////////
#endif
