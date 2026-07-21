#include "tinypy/frame.h"

#include "internal.h"

#include <assert.h>
#include <string.h>

//////////////////////////////////////////////////////////////////////////
#ifndef NDEBUG
static inline int __tinypy_internal_frame_slots_empty(const tinypy_frame_object_t *frame) {
    tinypy_value_t *cellvars = TINYPY_CODE_CELLVARS(frame->code);
    tinypy_value_t *freevars = TINYPY_CODE_FREEVARS(frame->code);
    size_t local_slot_count = (size_t)TINYPY_CODE_LOCAL_COUNT(frame->code) + TINYPY_TUPLE_SIZE(cellvars) + TINYPY_TUPLE_SIZE(freevars);
    size_t index;

    for (index = 0U; index < local_slot_count; ++index) {
        if (frame->locals_plus[index] != NULL) {
            return 0;
        }
    }

    return 1;
}
#endif
//////////////////////////////////////////////////////////////////////////
static int32_t __tinypy_internal_frame_line_number(const tinypy_frame_object_t *frame) {
    const tinypy_code_object_t *code = TINYPY_CODE_OBJECT(frame->code);
    const unsigned char *bytes = TINYPY_STRING_OBJECT(code->lnotab)->bytes;
    size_t size = TINYPY_SIZED_SIZE(code->lnotab);
    size_t instruction_offset = frame->last_instruction >= 0 ? (size_t)frame->last_instruction : 0U;
    size_t address = 0U;
    size_t index;
    int32_t line = code->first_line_number;

    for (index = 0U; index + 1U < size; index += 2U) {
        address += bytes[index];
        if (address > instruction_offset) {
            break;
        }
        line += (int32_t)bytes[index + 1U];
    }
    return line;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_frame_make_builtins(tinypy_vm_t *vm, tinypy_value_t *globals) {
    tinypy_value_t *builtins;

    if (vm->current_frame != NULL && vm->current_frame->globals == globals) {
        builtins = vm->current_frame->builtins;
        TINYPY_INCREF(builtins);
        return builtins;
    }

    builtins = tinypy_internal_dict_get_optional(vm, globals, vm->builtins_key);

    if (builtins != NULL && TINYPY_VALUE_KIND(builtins) == TINYPY_VALUE_DICT) {
        TINYPY_INCREF(builtins);
        return builtins;
    }

    builtins = vm->builtins;
    TINYPY_INCREF(builtins);
    return builtins;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_frame_object_t *__tinypy_internal_frame_allocate(tinypy_vm_t *vm, size_t allocation_size, size_t local_slot_count) {
    tinypy_frame_object_t *previous = NULL;
    tinypy_frame_object_t *frame = vm->frame_free_list;

    while (frame != NULL) {
        tinypy_frame_object_t *next = frame->back != NULL ? TINYPY_FRAME_OBJECT(frame->back) : NULL;
        size_t cached_size = offsetof(tinypy_frame_object_t, locals_plus) + TINYPY_SIZED_SIZE(&frame->base) * sizeof(tinypy_value_t *);

        if (cached_size == allocation_size) {
            if (previous != NULL) {
                previous->back = frame->back;
            }
            else {
                vm->frame_free_list = next;
            }
            assert(vm->frame_free_count != 0U);
            vm->frame_free_count -= 1U;
            frame->base.base.ref = 1;
            assert(frame->base.base.type == &vm->frame_type);
            (void)memset(frame->global_cache, 0, sizeof(frame->global_cache));
            if (local_slot_count != 0U) {
                (void)memset(frame->locals_plus, 0, local_slot_count * sizeof(*frame->locals_plus));
            }
            return frame;
        }
        previous = frame;
        frame = next;
    }

    frame = (tinypy_frame_object_t *)tinypy_internal_vm_allocate(vm, allocation_size, (uint32_t)TINYPY_ALLOC_TAG_VALUE);

    frame->base.base.ref = 1;
    frame->base.base.type = &vm->frame_type;
    TINYPY_INCREF(&vm->frame_type.base.base);
    (void)memset(frame->global_cache, 0, sizeof(frame->global_cache));
    if (local_slot_count != 0U) {
        (void)memset(frame->locals_plus, 0, local_slot_count * sizeof(*frame->locals_plus));
    }
    return frame;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_frame_free_list_push(tinypy_vm_t *vm, tinypy_value_t *value) {
    tinypy_frame_object_t *frame = TINYPY_FRAME_OBJECT(value);

    assert(value->type == &vm->frame_type);
    assert(value->ref == 0);
    assert(vm->frame_free_count < TINYPY_FRAME_FREE_LIST_MAX);
    frame->back = vm->frame_free_list != NULL ? &vm->frame_free_list->base.base : NULL;
    vm->frame_free_list = frame;
    vm->frame_free_count += 1U;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_frame_free_list_finalize(tinypy_vm_t *vm) {
    while (vm->frame_free_list != NULL) {
        tinypy_frame_object_t *frame = vm->frame_free_list;
        size_t allocation_size = offsetof(tinypy_frame_object_t, locals_plus) + TINYPY_SIZED_SIZE(&frame->base) * sizeof(tinypy_value_t *);

        vm->frame_free_list = frame->back != NULL ? TINYPY_FRAME_OBJECT(frame->back) : NULL;
        assert(vm->frame_free_count != 0U);
        vm->frame_free_count -= 1U;
        assert(vm->frame_type.base.base.ref > 1);
        vm->frame_type.base.base.ref -= 1;
        tinypy_internal_vm_deallocate(vm, frame, allocation_size, (uint32_t)TINYPY_ALLOC_TAG_VALUE);
    }
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_frame_release_fast(tinypy_frame_object_t *frame) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(&frame->base.base);

    assert(frame->stack_top == frame->value_stack);
    assert(frame->previous_handled_type == NULL);
    assert(frame->previous_handled_value == NULL);
    assert(frame->previous_handled_traceback == NULL);
    assert(__tinypy_internal_frame_slots_empty(frame));
    assert(frame->base.base.ref == 1);
    frame->base.base.ref = 0;
    if (frame->back != NULL) {
        TINYPY_DECREF(frame->back);
        frame->back = NULL;
    }
    TINYPY_DECREF(frame->code);
    frame->code = NULL;
    TINYPY_DECREF(frame->builtins);
    frame->builtins = NULL;
    TINYPY_DECREF(frame->globals);
    frame->globals = NULL;
    if (frame->locals != NULL) {
        TINYPY_DECREF(frame->locals);
        frame->locals = NULL;
    }
    if (vm->frame_free_count < TINYPY_FRAME_FREE_LIST_MAX) {
        tinypy_internal_frame_free_list_push(vm, &frame->base.base);
    }
    else {
        tinypy_internal_value_destroy(vm, &frame->base.base);
        TINYPY_DECREF(&vm->frame_type.base.base);
    }
}
//////////////////////////////////////////////////////////////////////////
static void __tinypy_internal_frame_sync_local(tinypy_vm_t *vm, tinypy_value_t *locals, tinypy_value_t *name, tinypy_value_t *value) {
    if (value != NULL) {
        tinypy_dict_set(locals, name, value);
    }
    else if (tinypy_internal_dict_get_optional(vm, locals, name) != NULL) {
        tinypy_dict_delete(locals, name);
    }
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_frame_locals(tinypy_frame_object_t *frame) {
    tinypy_value_t *code = frame->code;
    tinypy_vm_t *vm = TINYPY_VALUE_VM(code);
    tinypy_value_t *varnames = TINYPY_CODE_VARNAMES(code);
    tinypy_value_t *cellvars = TINYPY_CODE_CELLVARS(code);
    tinypy_value_t *freevars = TINYPY_CODE_FREEVARS(code);
    size_t local_count = (size_t)TINYPY_CODE_LOCAL_COUNT(code);
    size_t cell_count = TINYPY_TUPLE_SIZE(cellvars);
    size_t free_count = TINYPY_TUPLE_SIZE(freevars);
    size_t index;

    if (frame->locals == NULL) {
        frame->locals = tinypy_dict_new(vm);
    }
    for (index = 0U; index < local_count; ++index) {
        __tinypy_internal_frame_sync_local(vm, frame->locals, TINYPY_TUPLE_GET(varnames, index), frame->locals_plus[index]);
    }
    for (index = 0U; index < cell_count; ++index) {
        tinypy_value_t *cell = frame->locals_plus[local_count + index];
        tinypy_value_t *content = cell != NULL ? tinypy_cell_get(cell) : NULL;

        __tinypy_internal_frame_sync_local(vm, frame->locals, TINYPY_TUPLE_GET(cellvars, index), content);
    }
    if ((TINYPY_CODE_FLAGS(code) & TINYPY_CODE_OPTIMIZED) != 0) {
        for (index = 0U; index < free_count; ++index) {
            tinypy_value_t *cell = frame->locals_plus[local_count + cell_count + index];
            tinypy_value_t *content = cell != NULL ? tinypy_cell_get(cell) : NULL;

            __tinypy_internal_frame_sync_local(vm, frame->locals, TINYPY_TUPLE_GET(freevars, index), content);
        }
    }
    return frame->locals;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t *__tinypy_internal_frame_new(tinypy_value_t *code, tinypy_value_t *globals, tinypy_value_t *locals, int32_t function_frame) {
    tinypy_vm_t *vm = TINYPY_VALUE_VM(code);
    tinypy_frame_object_t *frame;
    tinypy_value_t *builtins;
    size_t local_count;
    size_t cell_count;
    size_t free_count;
    size_t stack_size;
    size_t extras;
    size_t allocation_size;
    int owns_locals = 0;

    local_count = (size_t)TINYPY_CODE_LOCAL_COUNT(code);
    tinypy_value_t *cellvars = TINYPY_CODE_CELLVARS(code);
    cell_count = TINYPY_TUPLE_SIZE(cellvars);
    tinypy_value_t *freevars = TINYPY_CODE_FREEVARS(code);
    free_count = TINYPY_TUPLE_SIZE(freevars);
    stack_size = (size_t)TINYPY_CODE_STACK_SIZE(code);
    assert(local_count <= SIZE_MAX - cell_count);
    assert(local_count + cell_count <= SIZE_MAX - free_count);
    assert(local_count + cell_count + free_count <= SIZE_MAX - stack_size);
    extras = local_count + cell_count + free_count + stack_size;
    assert(extras <= (SIZE_MAX - offsetof(tinypy_frame_object_t, locals_plus)) / sizeof(tinypy_value_t *));
    allocation_size = offsetof(tinypy_frame_object_t, locals_plus) + extras * sizeof(tinypy_value_t *);
    builtins = __tinypy_internal_frame_make_builtins(vm, globals);
    frame = __tinypy_internal_frame_allocate(vm, allocation_size, local_count + cell_count + free_count);
    TINYPY_SIZED_SIZE(&frame->base) = extras;
    frame->back = vm->current_frame != NULL ? &vm->current_frame->base.base : NULL;
    frame->code = code;
    frame->builtins = builtins;
    frame->globals = globals;
    if (locals != NULL) {
        frame->locals = locals;
    }
    else if (function_frame != 0 && (TINYPY_CODE_FLAGS(code) & TINYPY_CODE_NEW_LOCALS) != 0) {
        if ((TINYPY_CODE_FLAGS(code) & TINYPY_CODE_OPTIMIZED) != 0) {
            frame->locals = NULL;
        }
        else {
            frame->locals = tinypy_dict_new(vm);
            owns_locals = 1;
        }
    }
    else {
        frame->locals = globals;
    }
    frame->previous_handled_type = vm->handled_type;
    frame->previous_handled_value = vm->handled_value;
    frame->previous_handled_traceback = vm->handled_traceback;
    frame->value_stack = frame->locals_plus + local_count + cell_count + free_count;
    frame->stack_top = frame->value_stack;
    frame->last_instruction = -1;
    frame->block_count = 0U;
    if (frame->back != NULL) {
        TINYPY_INCREF(frame->back);
    }
    TINYPY_INCREF(code);
    TINYPY_INCREF(globals);
    if (frame->locals != NULL && owns_locals == 0) {
        TINYPY_INCREF(frame->locals);
    }
    if (frame->previous_handled_type != NULL) {
        TINYPY_INCREF(frame->previous_handled_type);
    }
    if (frame->previous_handled_value != NULL) {
        TINYPY_INCREF(frame->previous_handled_value);
    }
    if (frame->previous_handled_traceback != NULL) {
        TINYPY_INCREF(frame->previous_handled_traceback);
    }
    return &frame->base.base;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_frame_new(tinypy_value_t *code, tinypy_value_t *globals, tinypy_value_t *locals) {
    assert(code != NULL);
    assert(tinypy_internal_vm_valid(TINYPY_VALUE_VM(code)));
    assert(TINYPY_VALUE_KIND(code) == TINYPY_VALUE_CODE);
    assert(globals != NULL);
    assert(tinypy_internal_value_belongs_to(TINYPY_VALUE_VM(code), globals));
    assert(TINYPY_VALUE_KIND(globals) == TINYPY_VALUE_DICT);
    assert(locals == NULL || tinypy_internal_value_belongs_to(TINYPY_VALUE_VM(code), locals));
    assert(locals == NULL || TINYPY_VALUE_KIND(locals) == TINYPY_VALUE_DICT);
    return __tinypy_internal_frame_new(code, globals, locals, 0);
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_internal_frame_new_function(tinypy_value_t *code, tinypy_value_t *globals) {
    return __tinypy_internal_frame_new(code, globals, NULL, 1);
}
//////////////////////////////////////////////////////////////////////////
void tinypy_internal_frame_release_references(tinypy_value_t *value, tinypy_release_callback_t visit, void *user_data) {
    tinypy_frame_object_t *frame = TINYPY_FRAME_OBJECT(value);
    tinypy_value_t *cellvars = TINYPY_CODE_CELLVARS(frame->code);
    tinypy_value_t *freevars = TINYPY_CODE_FREEVARS(frame->code);
    size_t local_slot_count = (size_t)TINYPY_CODE_LOCAL_COUNT(frame->code) + TINYPY_TUPLE_SIZE(cellvars) + TINYPY_TUPLE_SIZE(freevars);
    size_t index;

    if (frame->back != NULL) {
        visit(frame->back, user_data);
    }
    visit(frame->code, user_data);
    visit(frame->builtins, user_data);
    visit(frame->globals, user_data);
    if (frame->locals != NULL) {
        visit(frame->locals, user_data);
    }
    if (frame->previous_handled_type != NULL) {
        visit(frame->previous_handled_type, user_data);
    }
    if (frame->previous_handled_value != NULL) {
        visit(frame->previous_handled_value, user_data);
    }
    if (frame->previous_handled_traceback != NULL) {
        visit(frame->previous_handled_traceback, user_data);
    }
    for (index = 0U; index < local_slot_count; ++index) {
        if (frame->locals_plus[index] != NULL) {
            visit(frame->locals_plus[index], user_data);
        }
    }
    for (index = 0U; index < (size_t)(frame->stack_top - frame->value_stack); ++index) {
        visit(frame->value_stack[index], user_data);
    }
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_frame_back(const tinypy_value_t *frame) {
    assert(frame != NULL);
    assert(tinypy_internal_vm_valid(TINYPY_VALUE_VM(frame)));
    assert(TINYPY_VALUE_KIND(frame) == TINYPY_VALUE_FRAME);
    return TINYPY_FRAME_OBJECT((tinypy_value_t *)frame)->back;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_frame_code(const tinypy_value_t *frame) {
    assert(frame != NULL);
    assert(tinypy_internal_vm_valid(TINYPY_VALUE_VM(frame)));
    assert(TINYPY_VALUE_KIND(frame) == TINYPY_VALUE_FRAME);
    return TINYPY_FRAME_OBJECT((tinypy_value_t *)frame)->code;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_frame_builtins(const tinypy_value_t *frame) {
    assert(frame != NULL);
    assert(tinypy_internal_vm_valid(TINYPY_VALUE_VM(frame)));
    assert(TINYPY_VALUE_KIND(frame) == TINYPY_VALUE_FRAME);
    return TINYPY_FRAME_OBJECT((tinypy_value_t *)frame)->builtins;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_frame_globals(const tinypy_value_t *frame) {
    assert(frame != NULL);
    assert(tinypy_internal_vm_valid(TINYPY_VALUE_VM(frame)));
    assert(TINYPY_VALUE_KIND(frame) == TINYPY_VALUE_FRAME);
    return TINYPY_FRAME_OBJECT((tinypy_value_t *)frame)->globals;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_frame_locals(const tinypy_value_t *frame) {
    assert(frame != NULL);
    assert(tinypy_internal_vm_valid(TINYPY_VALUE_VM(frame)));
    assert(TINYPY_VALUE_KIND(frame) == TINYPY_VALUE_FRAME);
    return tinypy_internal_frame_locals(TINYPY_FRAME_OBJECT((tinypy_value_t *)frame));
}
//////////////////////////////////////////////////////////////////////////
int32_t tinypy_frame_last_instruction(const tinypy_value_t *frame) {
    assert(frame != NULL);
    assert(tinypy_internal_vm_valid(TINYPY_VALUE_VM(frame)));
    assert(TINYPY_VALUE_KIND(frame) == TINYPY_VALUE_FRAME);
    return TINYPY_FRAME_OBJECT((tinypy_value_t *)frame)->last_instruction;
}
//////////////////////////////////////////////////////////////////////////
int32_t tinypy_frame_line_number(const tinypy_value_t *frame) {
    assert(frame != NULL);
    assert(tinypy_internal_vm_valid(TINYPY_VALUE_VM(frame)));
    assert(TINYPY_VALUE_KIND(frame) == TINYPY_VALUE_FRAME);
    return __tinypy_internal_frame_line_number(TINYPY_FRAME_OBJECT((tinypy_value_t *)frame));
}
//////////////////////////////////////////////////////////////////////////
size_t tinypy_frame_stack_depth(const tinypy_value_t *frame) {
    tinypy_frame_object_t *object;

    assert(frame != NULL);
    assert(tinypy_internal_vm_valid(TINYPY_VALUE_VM(frame)));
    assert(TINYPY_VALUE_KIND(frame) == TINYPY_VALUE_FRAME);
    object = TINYPY_FRAME_OBJECT((tinypy_value_t *)frame);
    return (size_t)(object->stack_top - object->value_stack);
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_vm_current_frame(const tinypy_vm_t *vm) {
    assert(tinypy_internal_vm_valid(vm));
    return vm->current_frame != NULL ? &vm->current_frame->base.base : NULL;
}
