#ifndef TINYPY_HASH_H
#define TINYPY_HASH_H

#include "tinypy/types.h"

/* Python 2 equality and 64-bit hash substrate. Equal numeric values across
 * bool/int/long/float/complex always have equal hashes. The default per-VM
 * CPython 2.7 string hash prefix/suffix are both zero. */
tinypy_hash_t tinypy_hash(const tinypy_value_t *value);
tinypy_bool_t tinypy_equal(const tinypy_value_t *left, const tinypy_value_t *right);

#endif
