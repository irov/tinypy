#ifndef TINYPY_DEBUGGER_H
#define TINYPY_DEBUGGER_H

#include "tinypy/types.h"

#if defined(TINYPY_DEBUGGER)

/* The events follow the trace function that PyEval_SetTrace installs in
 * CPython. CALL: a frame starts to run, which a generator does at every
 * resumption. LINE: as in maybe_call_line_trace, the frame reaches the first
 * instruction of a source line or jumps backwards, so each iteration of a
 * loop on one line has its LINE, and a line that a jump only passes
 * through, such as the end of an except clause that does not match, has
 * none. EXCEPTION: an exception is raised in the frame, propagates into it
 * from a call, or is thrown into its generator. RETURN: the frame stops
 * running, by return, by yield or by an exception, so every CALL has its
 * RETURN. Only EXCEPTION carries the exception, as the (type, value,
 * traceback) of CPython: the type it was raised with, the value as raised,
 * which, as the argument of sys.exit, need not be an instance of the type,
 * and the traceback, which already holds the frame; the other events pass
 * NULL. The callback runs with the pending exception set aside and cannot
 * raise one: what its code leaves raised is discarded. An interrupt that
 * poll_interrupt of the host reports meanwhile stops the code of the
 * callback and is raised again at the next poll after the callback in the
 * same execution: it is dropped when the outermost frame of the execution
 * ends first or tinypy_debugger_set removes the debugger. */
typedef enum tinypy_debugger_event_e {
    TINYPY_DEBUGGER_EVENT_LINE = 1,
    TINYPY_DEBUGGER_EVENT_CALL = 2,
    TINYPY_DEBUGGER_EVENT_RETURN = 3,
    TINYPY_DEBUGGER_EVENT_EXCEPTION = 4
} tinypy_debugger_event_e;

typedef enum tinypy_debugger_scope_e {
    TINYPY_DEBUGGER_SCOPE_LOCALS = 1,
    TINYPY_DEBUGGER_SCOPE_GLOBALS = 2
} tinypy_debugger_scope_e;

typedef struct tinypy_debugger_event_t {
    uint32_t abi_version;
    uint32_t struct_size;
    tinypy_debugger_event_e event;
    /* Borrowed for the duration of the callback. */
    tinypy_value_t *frame;
    tinypy_value_t *exception;
    tinypy_value_t *traceback;
    /* Present when struct_size covers it. */
    tinypy_value_t *exception_type;
} tinypy_debugger_event_t;

typedef void (*tinypy_debugger_callback_t)(void *user_data, const tinypy_debugger_event_t *event);

typedef struct tinypy_debugger_t {
    uint32_t abi_version;
    uint32_t struct_size;
    void *user_data;
    tinypy_debugger_callback_t callback;
} tinypy_debugger_t;

/* The descriptor is copied. NULL or a NULL callback disables all hooks. */
tinypy_bool_t tinypy_debugger_set(tinypy_vm_t *vm, const tinypy_debugger_t *debugger);

/* Returned variables are borrowed. Mutations synchronize optimized fast
 * locals and cell/free slots, so the next bytecode instruction sees them.
 * As in PyFrame_FastToLocals, a cell binds its name over an argument of
 * that name, which set and delete rebind with it as PyFrame_LocalsToFast
 * does, and the free variables of a frame that is not optimized, as a class
 * body, are not its locals. A name outside them is looked up in the locals
 * tinypy_frame_locals materializes, and is missing when that releases the
 * frame or the lookup lets them go. Locals that exec or eval gave as a
 * mapping other than a dict are left to its item protocol: for a name
 * outside the slots get returns NULL and set and delete return FALSE. */
tinypy_value_t *tinypy_debugger_frame_get(const tinypy_value_t *frame, tinypy_debugger_scope_e scope, const char *name, size_t name_size);
tinypy_bool_t tinypy_debugger_frame_set(tinypy_value_t *frame, tinypy_debugger_scope_e scope, const char *name, size_t name_size, tinypy_value_t *value);
tinypy_bool_t tinypy_debugger_frame_delete(tinypy_value_t *frame, tinypy_debugger_scope_e scope, const char *name, size_t name_size);
/* PyFrame_LocalsToFast(frame, 0), for code the host ran in the locals that
 * tinypy_frame_locals materialized: the names they bind rebind the fast
 * locals and cells, which keep their values for the names they lack. */
void tinypy_debugger_frame_sync(tinypy_value_t *frame);

#endif

#endif
