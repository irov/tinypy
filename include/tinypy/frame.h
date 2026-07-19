#ifndef TINYPY_FRAME_H
#define TINYPY_FRAME_H

#include "tinypy/types.h"

/* Frames follow the CPython 2.7 locals-plus layout: fast locals, cell/free
 * slots and the value stack share one inline allocation. locals may be NULL,
 * in which case globals is used. Returned object fields are borrowed. */
tinypy_value_t *tinypy_frame_new(tinypy_value_t *code, tinypy_value_t *globals, tinypy_value_t *locals);
tinypy_value_t *tinypy_frame_back(const tinypy_value_t *frame);
tinypy_value_t *tinypy_frame_code(const tinypy_value_t *frame);
tinypy_value_t *tinypy_frame_builtins(const tinypy_value_t *frame);
tinypy_value_t *tinypy_frame_globals(const tinypy_value_t *frame);
tinypy_value_t *tinypy_frame_locals(const tinypy_value_t *frame);
int32_t tinypy_frame_last_instruction(const tinypy_value_t *frame);
int32_t tinypy_frame_line_number(const tinypy_value_t *frame);
size_t tinypy_frame_stack_depth(const tinypy_value_t *frame);
tinypy_value_t *tinypy_vm_current_frame(const tinypy_vm_t *vm);

#endif
