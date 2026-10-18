#ifndef TINYPY_GENERATOR_H
#define TINYPY_GENERATOR_H

#include "tinypy/types.h"

/* send and throw resume the generator and return the next value it yields
 * as a new reference, or NULL when it finishes, with the exception pending
 * when it raised one. A NULL value sends None. throw takes the exception and
 * the traceback as tinypy_exception_raise does: a class is instantiated
 * without arguments and a NULL or None traceback stands for none; other
 * values raise TypeError without resuming the generator. */
tinypy_value_t *tinypy_generator_send(tinypy_value_t *generator, tinypy_value_t *value, tinypy_error_t **out_error);
tinypy_value_t *tinypy_generator_throw(tinypy_value_t *generator, tinypy_value_t *exception, tinypy_value_t *traceback, tinypy_error_t **out_error);
tinypy_bool_t tinypy_generator_close(tinypy_value_t *generator, tinypy_error_t **out_error);
tinypy_value_t *tinypy_generator_frame(const tinypy_value_t *generator);
tinypy_bool_t tinypy_generator_finished(const tinypy_value_t *generator);

#endif
