#ifndef TINYPY_ERROR_H
#define TINYPY_ERROR_H

#include "tinypy/types.h"

uint32_t tinypy_abi_version(void);
const char *tinypy_error_kind_name(tinypy_error_kind_e error_kind);

tinypy_error_kind_e tinypy_error_kind(const tinypy_error_t *error);

/* out_size is optional. The message of a raised Python exception is its str(),
 * computed on the first read; when the code that runs releases the error, the
 * release completes after the read and the message reads as empty. */
const char *tinypy_error_message(const tinypy_error_t *error, size_t *out_size);
const char *tinypy_error_logical_filename(const tinypy_error_t *error, size_t *out_size);
const char *tinypy_error_source_line(const tinypy_error_t *error, size_t *out_size);
int32_t tinypy_error_line_number(const tinypy_error_t *error);
int32_t tinypy_error_column_offset(const tinypy_error_t *error);

/* The error and its message count against the heap budget of the VM until
 * the error is released or the VM is destroyed. The error stores a copy of
 * the allocator vtable, so it does not require a live VM in order to be
 * released. */
void tinypy_error_release(tinypy_error_t *error);

#endif
