#ifndef TINYPY_BYTEARRAY_H
#define TINYPY_BYTEARRAY_H

#include "tinypy/types.h"

tinypy_value_t *tinypy_bytearray_from_bytes(tinypy_vm_t *vm, const void *bytes, size_t size);
size_t tinypy_bytearray_size(const tinypy_value_t *value);
const void *tinypy_bytearray_view(const tinypy_value_t *value, size_t *out_size);
void tinypy_bytearray_set(tinypy_value_t *value, size_t index, uint8_t byte);

#endif
