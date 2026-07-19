#ifndef TINYPY_OUTPUT_H
#define TINYPY_OUTPUT_H

#include "tinypy/types.h"

void tinypy_output_emit(tinypy_vm_t *vm, tinypy_output_channel_e channel, const void *bytes, size_t size);

#endif
