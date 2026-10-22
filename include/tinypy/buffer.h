#ifndef TINYPY_BUFFER_H
#define TINYPY_BUFFER_H

#include "tinypy/types.h"

#define TINYPY_BUFFER_TO_END SIZE_MAX

/* A buffer object over a str, unicode, bytearray or the owner of a buffer;
 * TINYPY_BUFFER_TO_END as size reaches the end of the owner. A unicode owner
 * is viewed as its UCS-4 code points in host byte order, which the first
 * buffer caches on it. Returns NULL, with MemoryError raised, when they
 * cannot be allocated. The offset adds to the offset of a buffer; a sum past
 * PTRDIFF_MAX returns NULL with ValueError raised, as the Py_ssize_t sum of
 * CPython turns negative. */
tinypy_value_t *tinypy_buffer_from_object(tinypy_value_t *object, size_t offset, size_t size);
const void *tinypy_buffer_view(const tinypy_value_t *buffer, size_t *out_size);

#endif
