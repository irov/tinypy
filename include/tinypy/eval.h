#ifndef TINYPY_EVAL_H
#define TINYPY_EVAL_H

#include "tinypy/types.h"

/* Evaluate a Python 2.7 code object. globals must be a dict; locals may be
 * NULL and then aliases globals. Success returns one owned reference. A
 * Python-semantic execution failure returns NULL and may produce out_error. */
tinypy_value_t *tinypy_eval_code(tinypy_value_t *code, tinypy_value_t *globals, tinypy_value_t *locals, tinypy_error_t **out_error);
tinypy_value_t *tinypy_exec_code(tinypy_value_t *code, tinypy_value_t *globals, tinypy_value_t *locals, tinypy_error_t **out_error);

#endif
