#ifndef TINYPY_SET_H
#define TINYPY_SET_H

#include "tinypy/types.h"

tinypy_value_t *tinypy_set_new(tinypy_vm_t *vm);
tinypy_value_t *tinypy_frozenset_new(tinypy_vm_t *vm);
tinypy_value_t *tinypy_set_from_iterable(tinypy_value_t *iterable, int32_t frozen, tinypy_error_t **out_error);
size_t tinypy_set_size(const tinypy_value_t *set);
int32_t tinypy_set_contains(const tinypy_value_t *set, const tinypy_value_t *item, tinypy_error_t **out_error);
int32_t tinypy_set_add(tinypy_value_t *set, tinypy_value_t *item, tinypy_error_t **out_error);
int32_t tinypy_set_discard(tinypy_value_t *set, tinypy_value_t *item, tinypy_error_t **out_error);
void tinypy_set_clear(tinypy_value_t *set);

#endif
