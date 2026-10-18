#ifndef TINYPY_SET_H
#define TINYPY_SET_H

#include "tinypy/types.h"

/* Python 2 set and frozenset. tinypy_set_add, tinypy_set_discard and
 * tinypy_set_clear change a set in place. On a frozenset they are
 * embedding-only builders, like PySet_Add for a brand-new frozenset: only a
 * frozenset from tinypy_frozenset_new that has not been hashed or shared yet
 * may be filled, since Python code observes immutable frozensets and
 * tinypy_set_from_iterable returns a frozenset argument itself. */
tinypy_value_t *tinypy_set_new(tinypy_vm_t *vm);
tinypy_value_t *tinypy_frozenset_new(tinypy_vm_t *vm);
tinypy_value_t *tinypy_set_from_iterable(tinypy_value_t *iterable, tinypy_bool_t frozen, tinypy_error_t **out_error);
size_t tinypy_set_size(const tinypy_value_t *set);
int32_t tinypy_set_contains(const tinypy_value_t *set, const tinypy_value_t *item, tinypy_error_t **out_error);
tinypy_bool_t tinypy_set_add(tinypy_value_t *set, tinypy_value_t *item, tinypy_error_t **out_error);
tinypy_bool_t tinypy_set_discard(tinypy_value_t *set, tinypy_value_t *item, tinypy_error_t **out_error);
void tinypy_set_clear(tinypy_value_t *set);

#endif
