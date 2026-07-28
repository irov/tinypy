#ifndef TINYPY_COMPILER_API_INTERNAL_H
#define TINYPY_COMPILER_API_INTERNAL_H

#include "tinypy/compiler.h"

tinypy_value_t *tinypy_internal_compiler_compile_source(tinypy_vm_t *vm, const void *source, size_t source_size, tinypy_bool_t source_is_unicode, const char *logical_filename, size_t filename_size, const tinypy_compile_options_t *options, tinypy_error_t **out_error);

#endif
