#ifndef TINYPY_WEAKREF_H
#define TINYPY_WEAKREF_H

#include "tinypy/types.h"

tinypy_value_t *tinypy_weakref_new(tinypy_value_t *object, tinypy_value_t *callback, tinypy_error_t **out_error);
tinypy_value_t *tinypy_weakref_get(const tinypy_value_t *weakref);

#endif
