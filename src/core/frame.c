#include "tinypy/frame.h"

#include "internal.h"

#include <assert.h>

//////////////////////////////////////////////////////////////////////////
static tinypy_frame_object_t *__tinypy_internal_frame_validate(const tinypy_value_t *value) {
    assert(value != NULL);
    assert(tinypy_internal_vm_valid(tinypy_internal_value_vm(value)));
    assert(tinypy_internal_value_kind(value) == TINYPY_VALUE_FRAME);
    return TINYPY_FRAME_OBJECT((tinypy_value_t *)value);
}

//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_frame_make_builtins(tinypy_vm_t *vm, tinypy_value_t *globals) {
    tinypy_value_t *key = tinypy_string_from_bytes(vm, "__builtins__", 12U);
    tinypy_value_t *builtins = NULL;

    if (tinypy_dict_contains(globals, key) != 0) {
        tinypy_value_t *candidate = tinypy_dict_get(globals, key);

        if (tinypy_internal_value_kind(candidate) == TINYPY_VALUE_DICT) {
            builtins = candidate;
            tinypy_retain(builtins);
        }
    }
    tinypy_release(key);
    if (builtins == NULL) {
        builtins = vm->builtins;
        tinypy_retain(builtins);
    }
    return builtins;
}

//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_frame_new(tinypy_value_t *code, tinypy_value_t *globals, tinypy_value_t *locals) {
    tinypy_vm_t *vm;
    tinypy_frame_object_t *frame;
    tinypy_value_t *builtins;
    size_t local_count;
    size_t cell_count;
    size_t free_count;
    size_t stack_size;
    size_t extras;
    size_t allocation_size;

    assert(code != NULL);
    vm = tinypy_internal_value_vm(code);
    assert(tinypy_internal_vm_valid(vm));
    assert(tinypy_internal_value_kind(code) == TINYPY_VALUE_CODE);
    assert(globals != NULL);
    assert(tinypy_internal_value_belongs_to(vm, globals));
    assert(tinypy_internal_value_kind(globals) == TINYPY_VALUE_DICT);
    assert(locals == NULL || tinypy_internal_value_belongs_to(vm, locals));
    assert(locals == NULL || tinypy_internal_value_kind(locals) == TINYPY_VALUE_DICT);

    local_count = (size_t)tinypy_code_local_count(code);
    tinypy_value_t *cellvars = tinypy_code_cellvars(code);
    cell_count = tinypy_tuple_size(cellvars);
    tinypy_value_t *freevars = tinypy_code_freevars(code);
    free_count = tinypy_tuple_size(freevars);
    stack_size = (size_t)tinypy_code_stack_size(code);
    assert(local_count <= SIZE_MAX - cell_count);
    assert(local_count + cell_count <= SIZE_MAX - free_count);
    assert(local_count + cell_count + free_count <= SIZE_MAX - stack_size);
    extras = local_count + cell_count + free_count + stack_size;
    assert(extras <= (size_t)PTRDIFF_MAX);
    assert(extras <= (SIZE_MAX - offsetof(tinypy_frame_object_t, locals_plus)) / sizeof(tinypy_value_t *));
    allocation_size = offsetof(tinypy_frame_object_t, locals_plus) + extras * sizeof(tinypy_value_t *);
    builtins = __tinypy_internal_frame_make_builtins(vm, globals);
    frame = (tinypy_frame_object_t *)tinypy_internal_object_allocate(vm, &vm->frame_type, allocation_size);
    TINYPY_SIZE(&frame->base) = (ptrdiff_t)extras;
    frame->back = vm->current_frame != NULL ? &vm->current_frame->base.base : NULL;
    frame->code = code;
    frame->builtins = builtins;
    frame->globals = globals;
    frame->locals = locals != NULL ? locals : globals;
    frame->previous_handled_type = vm->handled_type;
    frame->previous_handled_value = vm->handled_value;
    frame->previous_handled_traceback = vm->handled_traceback;
    frame->value_stack = frame->locals_plus + local_count + cell_count + free_count;
    frame->stack_top = frame->value_stack;
    frame->last_instruction = -1;
    frame->line_number = tinypy_code_first_line_number(code);
    if (frame->back != NULL) {
        tinypy_retain(frame->back);
    }
    tinypy_retain(code);
    tinypy_retain(globals);
    tinypy_retain(frame->locals);
    if (frame->previous_handled_type != NULL) {
        tinypy_retain(frame->previous_handled_type);
    }
    if (frame->previous_handled_value != NULL) {
        tinypy_retain(frame->previous_handled_value);
    }
    if (frame->previous_handled_traceback != NULL) {
        tinypy_retain(frame->previous_handled_traceback);
    }
    return &frame->base.base;
}

//////////////////////////////////////////////////////////////////////////
void tinypy_internal_frame_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data) {
    tinypy_frame_object_t *frame = TINYPY_FRAME_OBJECT(value);
    tinypy_value_t *cellvars = tinypy_code_cellvars(frame->code);
    tinypy_value_t *freevars = tinypy_code_freevars(frame->code);
    size_t local_slot_count = (size_t)tinypy_code_local_count(frame->code) + tinypy_tuple_size(cellvars) + tinypy_tuple_size(freevars);
    size_t index;

    if (frame->back != NULL) {
        visit(frame->back, user_data);
    }
    visit(frame->code, user_data);
    visit(frame->builtins, user_data);
    visit(frame->globals, user_data);
    visit(frame->locals, user_data);
    if (frame->previous_handled_type != NULL) {
        visit(frame->previous_handled_type, user_data);
    }
    if (frame->previous_handled_value != NULL) {
        visit(frame->previous_handled_value, user_data);
    }
    if (frame->previous_handled_traceback != NULL) {
        visit(frame->previous_handled_traceback, user_data);
    }
    for (index = 0U; index < local_slot_count; index += 1U) {
        if (frame->locals_plus[index] != NULL) {
            visit(frame->locals_plus[index], user_data);
        }
    }
    for (index = 0U; index < (size_t)(frame->stack_top - frame->value_stack); index += 1U) {
        visit(frame->value_stack[index], user_data);
    }
}

//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_frame_back(const tinypy_value_t *frame) {
    return __tinypy_internal_frame_validate(frame)->back;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_frame_code(const tinypy_value_t *frame) {
    return __tinypy_internal_frame_validate(frame)->code;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_frame_builtins(const tinypy_value_t *frame) {
    return __tinypy_internal_frame_validate(frame)->builtins;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_frame_globals(const tinypy_value_t *frame) {
    return __tinypy_internal_frame_validate(frame)->globals;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_frame_locals(const tinypy_value_t *frame) {
    return __tinypy_internal_frame_validate(frame)->locals;
}
//////////////////////////////////////////////////////////////////////////
int32_t tinypy_frame_last_instruction(const tinypy_value_t *frame) {
    return __tinypy_internal_frame_validate(frame)->last_instruction;
}
//////////////////////////////////////////////////////////////////////////
int32_t tinypy_frame_line_number(const tinypy_value_t *frame) {
    return __tinypy_internal_frame_validate(frame)->line_number;
}
//////////////////////////////////////////////////////////////////////////
size_t tinypy_frame_stack_depth(const tinypy_value_t *frame) {
    tinypy_frame_object_t *object = __tinypy_internal_frame_validate(frame);
    return (size_t)(object->stack_top - object->value_stack);
}

//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_vm_current_frame(const tinypy_vm_t *vm) {
    assert(tinypy_internal_vm_valid(vm));
    return vm->current_frame != NULL ? &vm->current_frame->base.base : NULL;
}
