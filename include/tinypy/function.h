#ifndef TINYPY_FUNCTION_H
#define TINYPY_FUNCTION_H

#include "tinypy/types.h"

/* defaults and closure are optional tuples. A function retains its code,
 * globals and optional tuples. Returned attributes are borrowed. */
tinypy_value_t *tinypy_function_new(tinypy_value_t *code, tinypy_value_t *globals, tinypy_value_t *defaults, tinypy_value_t *closure);
tinypy_value_t *tinypy_function_code(const tinypy_value_t *function);
tinypy_value_t *tinypy_function_globals(const tinypy_value_t *function);
tinypy_value_t *tinypy_function_defaults(const tinypy_value_t *function);
tinypy_value_t *tinypy_function_closure(const tinypy_value_t *function);
tinypy_value_t *tinypy_function_name(const tinypy_value_t *function);
tinypy_value_t *tinypy_function_doc(const tinypy_value_t *function);

/* args must be a tuple; kwargs may be NULL or a dict. */
tinypy_value_t *tinypy_call(tinypy_value_t *callable, tinypy_value_t *args, tinypy_value_t *kwargs, tinypy_error_t **out_error);

#endif
