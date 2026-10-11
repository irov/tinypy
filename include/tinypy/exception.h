#ifndef TINYPY_EXCEPTION_H
#define TINYPY_EXCEPTION_H

#include "tinypy/types.h"
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_exception_new(tinypy_type_t *type, tinypy_value_t *args, tinypy_error_t **out_error);
int32_t tinypy_exception_matches(tinypy_value_t *exception, tinypy_value_t *candidate, tinypy_error_t **out_error);
tinypy_bool_t tinypy_exception_raise(tinypy_value_t *exception, tinypy_value_t *traceback, tinypy_error_t **out_error);
tinypy_value_t *tinypy_vm_raised_exception(const tinypy_vm_t *vm);
tinypy_value_t *tinypy_vm_raised_exception_type(const tinypy_vm_t *vm);
tinypy_value_t *tinypy_vm_raised_traceback(const tinypy_vm_t *vm);
tinypy_value_t *tinypy_vm_handled_exception(const tinypy_vm_t *vm);
/* PyErr_NormalizeException of the raised exception, which, like the pending
 * exception of CPython, may be unnormalized: a value that is not an instance
 * of the raised class becomes the arguments of a call to the class, and an
 * instance of a subclass names its own class. Returns false when the call
 * fails; the exception it raised is then pending instead, normalized in turn
 * up to the recursion limit, with the original traceback unless it has its
 * own. Without a raised exception it does nothing. A host reporting an uncaught exception calls it, as PyErr_PrintEx
 * does, before it reads the raised type and value. */
tinypy_bool_t tinypy_vm_normalize_error(tinypy_vm_t *vm, tinypy_error_t **out_error);
tinypy_bool_t tinypy_vm_has_error(const tinypy_vm_t *vm);
void tinypy_vm_clear_error(tinypy_vm_t *vm);
void tinypy_vm_raise_error(tinypy_vm_t *vm, tinypy_error_kind_e kind, const char *message);
//////////////////////////////////////////////////////////////////////////
#endif
