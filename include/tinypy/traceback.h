#ifndef TINYPY_TRACEBACK_H
#define TINYPY_TRACEBACK_H

#include "tinypy/types.h"

tinypy_value_t *tinypy_traceback_next(const tinypy_value_t *traceback);
tinypy_value_t *tinypy_traceback_frame(const tinypy_value_t *traceback);
int32_t tinypy_traceback_last_instruction(const tinypy_value_t *traceback);
int32_t tinypy_traceback_line_number(const tinypy_value_t *traceback);

#endif
