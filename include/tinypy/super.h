#ifndef TINYPY_SUPER_H
#define TINYPY_SUPER_H

#include "tinypy/types.h"

tinypy_value_t *tinypy_super_new(tinypy_type_t *type, tinypy_value_t *object, tinypy_error_t **out_error);
const tinypy_type_t *tinypy_super_type(const tinypy_value_t *super_value);
tinypy_value_t *tinypy_super_object(const tinypy_value_t *super_value);
const tinypy_type_t *tinypy_super_object_type(const tinypy_value_t *super_value);

#endif
