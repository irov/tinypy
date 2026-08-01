#ifndef TINYPY_DEBUGGER_H
#define TINYPY_DEBUGGER_H

#include "tinypy/types.h"

#if defined(TINYPY_DEBUGGER)

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
 * locals and cell/free slots, so the next bytecode instruction sees them. */
tinypy_value_t *tinypy_debugger_frame_get(const tinypy_value_t *frame, tinypy_debugger_scope_e scope, const char *name, size_t name_size);
tinypy_bool_t tinypy_debugger_frame_set(tinypy_value_t *frame, tinypy_debugger_scope_e scope, const char *name, size_t name_size, tinypy_value_t *value);
tinypy_bool_t tinypy_debugger_frame_delete(tinypy_value_t *frame, tinypy_debugger_scope_e scope, const char *name, size_t name_size);
void tinypy_debugger_frame_sync(tinypy_value_t *frame);

#endif

#endif
