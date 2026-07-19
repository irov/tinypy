#ifndef TINYPY_GENERATOR_H
#define TINYPY_GENERATOR_H

#include "tinypy/types.h"

tinypy_value_t *tinypy_generator_send(tinypy_value_t *generator, tinypy_value_t *value, tinypy_error_t **out_error);
tinypy_value_t *tinypy_generator_throw(tinypy_value_t *generator, tinypy_value_t *exception, tinypy_value_t *traceback, tinypy_error_t **out_error);
int32_t tinypy_generator_close(tinypy_value_t *generator, tinypy_error_t **out_error);
tinypy_value_t *tinypy_generator_frame(const tinypy_value_t *generator);
int32_t tinypy_generator_finished(const tinypy_value_t *generator);

#endif
