#ifndef TINYPY_NATIVE_H
#define TINYPY_NATIVE_H

#include "tinypy/types.h"

typedef tinypy_value_t *(*tinypy_native_function_callback_t)(tinypy_value_t *function, tinypy_value_t *args, tinypy_value_t *kwargs, void *user_data, tinypy_error_t **out_error);
typedef void (*tinypy_native_function_finalize_t)(void *user_data);

tinypy_value_t *tinypy_native_function_new(tinypy_vm_t *vm, const char *name, size_t name_size, tinypy_native_function_callback_t callback, void *user_data, tinypy_native_function_finalize_t finalize);
tinypy_value_t *tinypy_native_function_name(const tinypy_value_t *function);
void *tinypy_native_function_user_data(const tinypy_value_t *function);

#endif
