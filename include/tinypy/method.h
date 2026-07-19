#ifndef TINYPY_METHOD_H
#define TINYPY_METHOD_H

#include "tinypy/types.h"

/* self may be NULL for a Python 2 unbound method. owner is required. */
tinypy_value_t *tinypy_method_new(tinypy_value_t *function, tinypy_value_t *self, tinypy_value_t *owner);
tinypy_value_t *tinypy_method_function(const tinypy_value_t *method);
tinypy_value_t *tinypy_method_self(const tinypy_value_t *method);
tinypy_value_t *tinypy_method_owner(const tinypy_value_t *method);

#endif
