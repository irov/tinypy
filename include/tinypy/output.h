#ifndef TINYPY_OUTPUT_H
#define TINYPY_OUTPUT_H

#include "tinypy/types.h"

void tinypy_output_emit(tinypy_vm_t *vm, tinypy_output_channel_e channel, const void *bytes, size_t size);
/* Writes the newline a trailing-comma print statement left pending on
 * sys.stdout, as Python does before reporting an uncaught exception and after
 * a top-level run. A failed write is ignored; the raised exception state is
 * preserved. */
void tinypy_output_flush_line(tinypy_vm_t *vm);

#endif
