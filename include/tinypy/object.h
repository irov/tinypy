#ifndef TINYPY_OBJECT_H
#define TINYPY_OBJECT_H

#include "tinypy/types.h"

/* Generic Python attribute protocol. get returns one owned reference. set
 * returns non-zero on success and zero with out_error on semantic failure.
 * The value variants preserve hash-compatible native attribute keys. */
tinypy_value_t *tinypy_object_get_attr(tinypy_value_t *value, const char *name, size_t name_size, tinypy_error_t **out_error);
tinypy_value_t *tinypy_object_get_attr_value(tinypy_value_t *value, tinypy_value_t *name, tinypy_error_t **out_error);
tinypy_bool_t tinypy_object_has_attr(tinypy_value_t *value, const char *name, size_t name_size);
tinypy_bool_t tinypy_object_has_attr_value(tinypy_value_t *value, tinypy_value_t *name);
tinypy_bool_t tinypy_object_set_attr(tinypy_value_t *value, const char *name, size_t name_size, tinypy_value_t *attribute_value, tinypy_error_t **out_error);
tinypy_bool_t tinypy_object_set_attr_value(tinypy_value_t *value, tinypy_value_t *name, tinypy_value_t *attribute_value, tinypy_error_t **out_error);
tinypy_bool_t tinypy_object_delete_attr(tinypy_value_t *value, const char *name, size_t name_size, tinypy_error_t **out_error);

#endif
