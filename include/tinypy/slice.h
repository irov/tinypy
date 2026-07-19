#ifndef TINYPY_SLICE_H
#define TINYPY_SLICE_H

#include "tinypy/types.h"

tinypy_value_t *tinypy_slice_new(tinypy_vm_t *vm, tinypy_value_t *start, tinypy_value_t *stop, tinypy_value_t *step);
tinypy_value_t *tinypy_slice_start(const tinypy_value_t *slice);
tinypy_value_t *tinypy_slice_stop(const tinypy_value_t *slice);
tinypy_value_t *tinypy_slice_step(const tinypy_value_t *slice);

#endif
