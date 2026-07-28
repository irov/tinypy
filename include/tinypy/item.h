#ifndef TINYPY_ITEM_H
#define TINYPY_ITEM_H

#include "tinypy/types.h"

tinypy_value_t *tinypy_get_item(tinypy_value_t *container, tinypy_value_t *key, tinypy_error_t **out_error);
tinypy_bool_t tinypy_set_item(tinypy_value_t *container, tinypy_value_t *key, tinypy_value_t *value, tinypy_error_t **out_error);
tinypy_bool_t tinypy_delete_item(tinypy_value_t *container, tinypy_value_t *key, tinypy_error_t **out_error);

#endif
