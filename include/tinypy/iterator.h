#ifndef TINYPY_ITERATOR_H
#define TINYPY_ITERATOR_H

#include "tinypy/types.h"

/* tinypy_iter returns one owned iterator. tinypy_next returns one owned item, NULL
 * with no error at normal exhaustion, or NULL with out_error on failure. */
tinypy_value_t *tinypy_iter(tinypy_value_t *value, tinypy_error_t **out_error);
tinypy_value_t *tinypy_next(tinypy_value_t *iterator, tinypy_error_t **out_error);

#endif
