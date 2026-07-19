#ifndef TINYPY_BUFFER_H
#define TINYPY_BUFFER_H

#include "tinypy/types.h"

#define TINYPY_BUFFER_TO_END SIZE_MAX

tinypy_value_t *tinypy_buffer_from_object(tinypy_value_t *object, size_t offset, size_t size);
const void *tinypy_buffer_view(const tinypy_value_t *buffer, size_t *out_size);

#endif
