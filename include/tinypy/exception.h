#ifndef TINYPY_EXCEPTION_H
#define TINYPY_EXCEPTION_H

#include "tinypy/types.h"
//////////////////////////////////////////////////////////////////////////
/* Calls type, a subclass of BaseException, with args, which must be a
 * tuple as for tinypy_call. */
tinypy_value_t *tinypy_exception_new(tinypy_type_t *type, tinypy_value_t *args, tinypy_error_t **out_error);
/* PyErr_GivenExceptionMatches: returns 1 or 0 and never fails, reporting a
 * failing __subclasscheck__ as unraisable. It takes no recursion level;
 * nested candidate tuples recurse under the native stack budget alone, and a
 * nesting deeper than that budget, where CPython recurses without a limit,
 * is reported as unraisable and does not match. */
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
 * own, and past the limit a RuntimeError "maximum recursion depth exceeded"
 * keeps that traceback, as PyExc_RecursionErrorInst does. Without a raised
 * exception it does nothing. A host reporting an uncaught exception calls it, as PyErr_PrintEx
 * does, before it reads the raised type and value. */
tinypy_bool_t tinypy_vm_normalize_error(tinypy_vm_t *vm, tinypy_error_t **out_error);
tinypy_bool_t tinypy_vm_has_error(const tinypy_vm_t *vm);
void tinypy_vm_clear_error(tinypy_vm_t *vm);
/* Raises the built-in class of kind with message as its only argument.
 * TINYPY_ERROR_UNICODE_DECODE and TINYPY_ERROR_UNICODE_ENCODE raise
 * UnicodeError: UnicodeDecodeError and UnicodeEncodeError take the five
 * fields of a codec error, which tinypy_exception_new gives them. */
void tinypy_vm_raise_error(tinypy_vm_t *vm, tinypy_error_kind_e kind, const char *message);
//////////////////////////////////////////////////////////////////////////
#endif
