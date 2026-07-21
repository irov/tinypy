#ifndef TINYPY_EXCEPTION_H
#define TINYPY_EXCEPTION_H

#include "tinypy/types.h"
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_exception_new(tinypy_type_t *type, tinypy_value_t *args, tinypy_error_t **out_error);
int32_t tinypy_exception_matches(tinypy_value_t *exception, tinypy_value_t *candidate, tinypy_error_t **out_error);
int32_t tinypy_exception_raise(tinypy_value_t *exception, tinypy_value_t *traceback, tinypy_error_t **out_error);
tinypy_value_t *tinypy_vm_raised_exception(const tinypy_vm_t *vm);
tinypy_value_t *tinypy_vm_raised_exception_type(const tinypy_vm_t *vm);
tinypy_value_t *tinypy_vm_raised_traceback(const tinypy_vm_t *vm);
tinypy_value_t *tinypy_vm_handled_exception(const tinypy_vm_t *vm);
int32_t tinypy_vm_has_error(const tinypy_vm_t *vm);
void tinypy_vm_clear_error(tinypy_vm_t *vm);
void tinypy_vm_raise_error(tinypy_vm_t *vm, tinypy_error_kind_e kind, const char *message);
//////////////////////////////////////////////////////////////////////////
#endif
