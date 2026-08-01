#include "tinypy/debugger.h"

#if defined(TINYPY_DEBUGGER)

#include "internal.h"

#include <string.h>

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_debugger_set(tinypy_vm_t *vm, const tinypy_debugger_t *debugger) {
    if (tinypy_internal_vm_valid(vm) == 0) {
        return TINYPY_FALSE;
    }
    if (debugger == NULL || debugger->callback == NULL) {
        (void)memset(&vm->debugger, 0, sizeof(vm->debugger));
        vm->has_debugger = TINYPY_FALSE;
        return TINYPY_TRUE;
    }
    if (debugger->abi_version != TINYPY_ABI_VERSION || debugger->struct_size < (uint32_t)sizeof(*debugger)) {
        return TINYPY_FALSE;
    }
    vm->debugger = *debugger;
    vm->has_debugger = TINYPY_TRUE;
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_bool_t __tinypy_debugger_frame_name_equal(tinypy_value_t *value, const char *name, size_t name_size) {
    size_t value_size = 0U;
    const char *bytes = (const char *)tinypy_string_view(value, &value_size);
    return value_size == name_size && memcmp(bytes, name, name_size) == 0 ? TINYPY_TRUE : TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
static tinypy_value_t **__tinypy_debugger_frame_slot(tinypy_frame_object_t *frame, const char *name, size_t name_size, tinypy_value_t **out_cell) {
    tinypy_code_object_t *code = TINYPY_CODE_OBJECT(frame->code);
    size_t local_count = (size_t)code->local_count;
    size_t cell_count = TINYPY_TUPLE_SIZE(code->cellvars);
    size_t free_count = TINYPY_TUPLE_SIZE(code->freevars);
    size_t index;

    *out_cell = NULL;
    for (index = 0U; index < local_count; ++index) {
        if (__tinypy_debugger_frame_name_equal(TINYPY_TUPLE_GET(code->varnames, index), name, name_size) != 0) {
            return &frame->locals_plus[index];
        }
    }
    for (index = 0U; index < cell_count; ++index) {
        if (__tinypy_debugger_frame_name_equal(TINYPY_TUPLE_GET(code->cellvars, index), name, name_size) != 0) {
            *out_cell = frame->locals_plus[local_count + index];
            return NULL;
        }
    }
    for (index = 0U; index < free_count; ++index) {
        if (__tinypy_debugger_frame_name_equal(TINYPY_TUPLE_GET(code->freevars, index), name, name_size) != 0) {
            *out_cell = frame->locals_plus[local_count + cell_count + index];
            return NULL;
        }
    }
    return NULL;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_debugger_frame_get(const tinypy_value_t *frame_value, tinypy_debugger_scope_e scope, const char *name, size_t name_size) {
    tinypy_frame_object_t *frame = TINYPY_FRAME_OBJECT((tinypy_value_t *)frame_value);
    tinypy_value_t *dictionary;
    tinypy_value_t *key;
    tinypy_value_t *cell;
    tinypy_value_t **slot;

    if (scope == TINYPY_DEBUGGER_SCOPE_LOCALS) {
        slot = __tinypy_debugger_frame_slot(frame, name, name_size, &cell);
        if (slot != NULL) {
            return *slot;
        }
        if (cell != NULL) {
            return tinypy_cell_get(cell);
        }
        dictionary = tinypy_internal_frame_locals(frame);
    }
    else if (scope == TINYPY_DEBUGGER_SCOPE_GLOBALS) {
        dictionary = frame->globals;
    }
    else {
        return NULL;
    }

    key = tinypy_string_from_bytes(TINYPY_VALUE_VM(frame_value), name, name_size);
    tinypy_value_t *value = tinypy_dict_get_optional(dictionary, key);
    TINYPY_DECREF(key);
    return value;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_debugger_frame_set(tinypy_value_t *frame_value, tinypy_debugger_scope_e scope, const char *name, size_t name_size, tinypy_value_t *value) {
    tinypy_frame_object_t *frame = TINYPY_FRAME_OBJECT(frame_value);
    tinypy_value_t *dictionary;
    tinypy_value_t *key;
    tinypy_value_t *cell;
    tinypy_value_t **slot;

    if (scope == TINYPY_DEBUGGER_SCOPE_LOCALS) {
        slot = __tinypy_debugger_frame_slot(frame, name, name_size, &cell);
        if (slot != NULL) {
            TINYPY_INCREF(value);
            if (*slot != NULL) {
                TINYPY_DECREF(*slot);
            }
            *slot = value;
            return TINYPY_TRUE;
        }
        if (cell != NULL) {
            tinypy_cell_set(cell, value);
            return TINYPY_TRUE;
        }
        dictionary = tinypy_internal_frame_locals(frame);
    }
    else if (scope == TINYPY_DEBUGGER_SCOPE_GLOBALS) {
        dictionary = frame->globals;
    }
    else {
        return TINYPY_FALSE;
    }

    key = tinypy_string_from_bytes(TINYPY_VALUE_VM(frame_value), name, name_size);
    tinypy_dict_set(dictionary, key, value);
    TINYPY_DECREF(key);
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_debugger_frame_delete(tinypy_value_t *frame_value, tinypy_debugger_scope_e scope, const char *name, size_t name_size) {
    tinypy_frame_object_t *frame = TINYPY_FRAME_OBJECT(frame_value);
    tinypy_value_t *dictionary;
    tinypy_value_t *key;
    tinypy_value_t *cell;
    tinypy_value_t **slot;

    if (scope == TINYPY_DEBUGGER_SCOPE_LOCALS) {
        slot = __tinypy_debugger_frame_slot(frame, name, name_size, &cell);
        if (slot != NULL) {
            if (*slot == NULL) {
                return TINYPY_FALSE;
            }
            TINYPY_DECREF(*slot);
            *slot = NULL;
            return TINYPY_TRUE;
        }
        if (cell != NULL) {
            if (tinypy_cell_get(cell) == NULL) {
                return TINYPY_FALSE;
            }
            tinypy_cell_set(cell, NULL);
            return TINYPY_TRUE;
        }
        dictionary = tinypy_internal_frame_locals(frame);
    }
    else if (scope == TINYPY_DEBUGGER_SCOPE_GLOBALS) {
        dictionary = frame->globals;
    }
    else {
        return TINYPY_FALSE;
    }

    key = tinypy_string_from_bytes(TINYPY_VALUE_VM(frame_value), name, name_size);
    if (tinypy_dict_contains(dictionary, key) == 0) {
        TINYPY_DECREF(key);
        return TINYPY_FALSE;
    }
    tinypy_dict_delete(dictionary, key);
    TINYPY_DECREF(key);
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_debugger_frame_sync(tinypy_value_t *frame_value) {
    tinypy_frame_object_t *frame = TINYPY_FRAME_OBJECT(frame_value);
    frame->debugger_line = tinypy_frame_line_number(frame_value);
}

#endif
