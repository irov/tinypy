#ifndef TINYPY_REPRESENTATION_H
#define TINYPY_REPRESENTATION_H

#include "tinypy/types.h"

tinypy_value_t *tinypy_object_repr(tinypy_value_t *value, tinypy_error_t **out_error);
tinypy_value_t *tinypy_object_str(tinypy_value_t *value, tinypy_error_t **out_error);

#endif
