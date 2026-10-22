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
        vm->debugger_interrupt = TINYPY_FALSE;
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
/* The names of a code object are exact strings. */
static tinypy_bool_t __tinypy_debugger_frame_name_equal(tinypy_value_t *value, const char *name, size_t name_size) {
    int32_t equal = TINYPY_SIZED_SIZE(value) == name_size && memcmp(TINYPY_STRING_OBJECT(value)->bytes, name, name_size) == 0;

    return equal != 0 ? TINYPY_TRUE : TINYPY_FALSE;
}
//////////////////////////////////////////////////////////////////////////
/* The fast slots of a name, as PyFrame_FastToLocals maps them: the cell of
   a cell variable, or of a free variable of an optimized frame, binds the
   name over a local of the same name, an argument or a name of a sublist
   parameter, whose slot comes too, so that a store keeps both as
   PyFrame_LocalsToFast does. */
static tinypy_value_t **__tinypy_debugger_frame_slot(tinypy_frame_object_t *frame, const char *name, size_t name_size, tinypy_value_t **out_cell) {
    tinypy_code_object_t *code = TINYPY_CODE_OBJECT(frame->code);
    size_t local_count = (size_t)code->local_count;
    tinypy_value_t **slot = NULL;
    size_t index;

    *out_cell = NULL;
    for (index = 0U; index < local_count; ++index) {
        if (__tinypy_debugger_frame_name_equal(TINYPY_TUPLE_GET(code->varnames, index), name, name_size) != 0) {
            slot = &frame->locals_plus[index];
            break;
        }
    }
    size_t cell_count = TINYPY_TUPLE_SIZE(code->cellvars);
    size_t free_count = (code->flags & TINYPY_CODE_OPTIMIZED) != 0 ? TINYPY_TUPLE_SIZE(code->freevars) : 0U;
    for (index = 0U; index < cell_count + free_count; ++index) {
        tinypy_value_t *cell_name = index < cell_count ? TINYPY_TUPLE_GET(code->cellvars, index) : TINYPY_TUPLE_GET(code->freevars, index - cell_count);

        if (__tinypy_debugger_frame_name_equal(cell_name, name, name_size) != 0) {
            *out_cell = frame->locals_plus[local_count + index];
            break;
        }
    }
    return slot;
}
//////////////////////////////////////////////////////////////////////////
/* Binds, or unbinds for a NULL value, the slots of a name. The values they
   held are released after both are stored, as STORE_FAST and DELETE_FAST
   do, since a release can run code that reads them. */
static void __tinypy_debugger_frame_store(tinypy_value_t **slot, tinypy_value_t *cell, tinypy_value_t *value) {
    tinypy_value_t *previous = NULL;

    if (slot != NULL) {
        previous = *slot;
        if (value != NULL) {
            TINYPY_INCREF(value);
        }
        *slot = value;
    }
    if (cell != NULL) {
        tinypy_cell_set(cell, value);
    }
    if (previous != NULL) {
        TINYPY_DECREF(previous);
    }
}
//////////////////////////////////////////////////////////////////////////
/* The dict of a scope, held: the key comparisons of a lookup can run code
   that finishes the generator of the frame, which lets its locals go. NULL
   when the locals are no dict: tinypy_internal_frame_locals lends none once
   the frame died, and exec or eval may give a frame another mapping. */
static tinypy_value_t *__tinypy_debugger_frame_dictionary(tinypy_frame_object_t *frame, tinypy_debugger_scope_e scope) {
    tinypy_value_t *dictionary = NULL;

    if (scope == TINYPY_DEBUGGER_SCOPE_LOCALS) {
        dictionary = tinypy_internal_frame_locals(frame);
    }
    else if (scope == TINYPY_DEBUGGER_SCOPE_GLOBALS) {
        dictionary = frame->globals;
    }
    if (dictionary == NULL || TINYPY_VALUE_KIND(dictionary) != TINYPY_VALUE_DICT) {
        return NULL;
    }
    TINYPY_INCREF(dictionary);
    return dictionary;
}
//////////////////////////////////////////////////////////////////////////
tinypy_value_t *tinypy_debugger_frame_get(const tinypy_value_t *frame_value, tinypy_debugger_scope_e scope, const char *name, size_t name_size) {
    tinypy_frame_object_t *frame = TINYPY_FRAME_OBJECT((tinypy_value_t *)frame_value);
    tinypy_vm_t *vm = TINYPY_VALUE_VM(frame_value);

    if (scope == TINYPY_DEBUGGER_SCOPE_LOCALS) {
        tinypy_value_t *cell;
        tinypy_value_t **slot = __tinypy_debugger_frame_slot(frame, name, name_size, &cell);

        if (cell != NULL) {
            tinypy_value_t *content = tinypy_cell_get(cell);
            return content;
        }
        if (slot != NULL) {
            return *slot;
        }
    }
    tinypy_value_t *dictionary = __tinypy_debugger_frame_dictionary(frame, scope);
    if (dictionary == NULL) {
        return NULL;
    }
    tinypy_value_t *key = tinypy_internal_name_from_bytes(vm, name, name_size);
    tinypy_value_t *value = tinypy_dict_get_optional(dictionary, key);
    /* The values of a dict that the lookup let go leave with it. */
    if (TINYPY_REFCNT(dictionary) == 1) {
        value = NULL;
    }
    TINYPY_DECREF(key);
    TINYPY_DECREF(dictionary);
    return value;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_debugger_frame_set(tinypy_value_t *frame_value, tinypy_debugger_scope_e scope, const char *name, size_t name_size, tinypy_value_t *value) {
    tinypy_frame_object_t *frame = TINYPY_FRAME_OBJECT(frame_value);
    tinypy_vm_t *vm = TINYPY_VALUE_VM(frame_value);

    if (scope == TINYPY_DEBUGGER_SCOPE_LOCALS) {
        tinypy_value_t *cell;
        tinypy_value_t **slot = __tinypy_debugger_frame_slot(frame, name, name_size, &cell);

        if (slot != NULL || cell != NULL) {
            __tinypy_debugger_frame_store(slot, cell, value);
            return TINYPY_TRUE;
        }
    }
    tinypy_value_t *dictionary = __tinypy_debugger_frame_dictionary(frame, scope);
    if (dictionary == NULL) {
        return TINYPY_FALSE;
    }
    tinypy_value_t *key = tinypy_internal_name_from_bytes(vm, name, name_size);
    tinypy_dict_set(dictionary, key, value);
    TINYPY_DECREF(key);
    TINYPY_DECREF(dictionary);
    return TINYPY_TRUE;
}
//////////////////////////////////////////////////////////////////////////
tinypy_bool_t tinypy_debugger_frame_delete(tinypy_value_t *frame_value, tinypy_debugger_scope_e scope, const char *name, size_t name_size) {
    tinypy_frame_object_t *frame = TINYPY_FRAME_OBJECT(frame_value);
    tinypy_vm_t *vm = TINYPY_VALUE_VM(frame_value);

    if (scope == TINYPY_DEBUGGER_SCOPE_LOCALS) {
        tinypy_value_t *cell;
        tinypy_value_t **slot = __tinypy_debugger_frame_slot(frame, name, name_size, &cell);

        if (slot != NULL || cell != NULL) {
            tinypy_value_t *bound = cell != NULL ? tinypy_cell_get(cell) : *slot;

            if (bound == NULL) {
                return TINYPY_FALSE;
            }
            __tinypy_debugger_frame_store(slot, cell, NULL);
            return TINYPY_TRUE;
        }
    }
    tinypy_value_t *dictionary = __tinypy_debugger_frame_dictionary(frame, scope);
    if (dictionary == NULL) {
        return TINYPY_FALSE;
    }
    tinypy_value_t *key = tinypy_internal_name_from_bytes(vm, name, name_size);
    tinypy_bool_t deleted = tinypy_internal_dict_delete_optional(vm, dictionary, key);
    TINYPY_DECREF(key);
    TINYPY_DECREF(dictionary);
    return deleted;
}
//////////////////////////////////////////////////////////////////////////
void tinypy_debugger_frame_sync(tinypy_value_t *frame_value) {
    TINYPY_INCREF(frame_value);
    tinypy_internal_frame_locals_to_fast(TINYPY_FRAME_OBJECT(frame_value));
    TINYPY_DECREF(frame_value);
}

#endif
