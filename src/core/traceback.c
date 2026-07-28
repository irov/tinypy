#include "tinypy/traceback.h"

#include "internal.h"

//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_traceback_new(tinypy_value_t *frame, tinypy_value_t *next) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(frame);
    tinypy_traceback_object_t *traceback = (tinypy_traceback_object_t *)tinypy_internal_value_allocate(vm, TINYPY_VALUE_TRACEBACK, sizeof(*traceback));
    traceback->next = next;
    traceback->frame = frame;
    traceback->last_instruction = tinypy_frame_last_instruction(frame);
    traceback->line_number = tinypy_frame_line_number(frame);
    if (next != NULL) {
        TINYPY_INCREF(next);
    }
    TINYPY_INCREF(frame);
    return &traceback->base;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_traceback_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data) {
    tinypy_traceback_object_t *traceback = TINYPY_TRACEBACK_OBJECT(value);

    if (traceback->next != NULL) {
        visit(traceback->next, user_data);
    }
    visit(traceback->frame, user_data);
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_traceback_here(tinypy_vm_t *vm, tinypy_frame_object_t *frame) {
    tinypy_value_t *traceback = tinypy_internal_traceback_new(&frame->base.base, vm->raised_traceback);
    if (vm->raised_traceback != NULL) {
        TINYPY_DECREF(vm->raised_traceback);
    }
    vm->raised_traceback = traceback;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_traceback_next(const tinypy_value_t *traceback) {
    tinypy_value_t *return_value_1 = TINYPY_TRACEBACK_OBJECT((tinypy_value_t *)traceback)->next;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_traceback_frame(const tinypy_value_t *traceback) {
    tinypy_value_t *return_value_1 = TINYPY_TRACEBACK_OBJECT((tinypy_value_t *)traceback)->frame;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
int32_t tinypy_traceback_last_instruction(const tinypy_value_t *traceback) {
    int32_t return_value_1 = TINYPY_TRACEBACK_OBJECT((tinypy_value_t *)traceback)->last_instruction;
    return return_value_1;
}
//////////////////////////////////////////////////////////////////////////
int32_t tinypy_traceback_line_number(const tinypy_value_t *traceback) {
    int32_t return_value_1 = TINYPY_TRACEBACK_OBJECT((tinypy_value_t *)traceback)->line_number;
    return return_value_1;
}
