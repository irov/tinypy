#ifndef TINYPY_VM_H
#define TINYPY_VM_H

#include "tinypy/types.h"

typedef struct tinypy_allocator_t {
    uint32_t abi_version;
    uint32_t struct_size;
    void *user_data;

    void *(*allocate)(void *user_data, size_t size, size_t alignment);

    void *(*reallocate)(void *user_data, void *memory, size_t old_size, size_t new_size, size_t alignment);

    void (*deallocate)(void *user_data, void *memory, size_t size, size_t alignment);
} tinypy_allocator_t;

typedef struct tinypy_diagnostic_t {
    uint32_t abi_version;
    uint32_t struct_size;
    tinypy_error_kind_e error_kind;
    const char *message;
    size_t message_size;
} tinypy_diagnostic_t;

typedef void (*tinypy_diagnostic_callback_t)(void *user_data, const tinypy_diagnostic_t *diagnostic);

typedef struct tinypy_host_t {
    uint32_t abi_version;
    uint32_t struct_size;
    void *user_data;

    /* Returns the artifact of the request, or NULL without one. An
     * exception raised before it returns fails the import, with or without
     * an artifact, except an ImportError raised before returning NULL,
     * which, as from a finder, means that nobody provides the module. */
    const tinypy_module_artifact_t *(*resolve_module)(void *user_data, const tinypy_module_request_t *request);

    void (*release_module_artifact)(void *user_data, const tinypy_module_artifact_t *artifact);

    void (*emit_output)(void *user_data, tinypy_output_channel_e channel, const void *bytes, size_t size);

    tinypy_diagnostic_callback_t diagnostic;

    tinypy_bool_t (*poll_interrupt)(void *user_data);
} tinypy_host_t;

typedef struct tinypy_vm_config_t {
    uint32_t abi_version;
    uint32_t struct_size;
    const tinypy_allocator_t *allocator;
    /* Optional. NULL disables host callbacks. */
    const tinypy_host_t *host;

    /* Zero disables allocation-budget preflight. Fallible variable-size
     * language operations report MemoryError before exceeding the budget.
     * The VM object, reserved pool arenas and direct allocations count, the
     * errors the host holds included. The VM bootstrap must fit the budget as
     * a whole. C API calls without an error result are not fallible and
     * complete regardless of the budget, and so does raising an error. */
    size_t max_heap_bytes;
    uint64_t feature_flags;
    int32_t optimize_level;
    tinypy_bool_t cycle_diagnostics;

    /* Native stack budget for recursive evaluation, measured from its entry.
     * Zero selects 1 MiB. Set below the available thread stack, with room
     * for host callbacks and exception unwinding. GCC, Clang and MSVC support
     * this guard; other compilers retain the Python recursion-depth guard. */
    size_t max_stack_bytes;
} tinypy_vm_config_t;

/* Returns NULL, with everything it allocated released, when max_heap_bytes is
 * smaller than the memory the VM bootstrap holds, or when the allocator
 * returned NULL for the VM object or for a bootstrap request the bootstrap
 * continues after; other bootstrap requests must not fail. */
tinypy_vm_t *tinypy_vm_create(const tinypy_vm_config_t *config);
tinypy_value_t *tinypy_vm_builtins(const tinypy_vm_t *vm);

/* Reports currently unreachable owning cycles through callback. Cycle
 * tracking must be compiled in and enabled in tinypy_vm_config_t. Returns the
 * number of reported strongly connected components, or zero when cycle
 * diagnostics are not compiled in. */
size_t tinypy_vm_report_cycles(tinypy_vm_t *vm, tinypy_diagnostic_callback_t callback, void *user_data);

/* The runtime has no allocation registry or cyclic collector. The embedder
 * must release every owned dynamic value and explicitly break owning cycles
 * before destroying the VM. The finalizers of host native values still
 * reachable from the VM run first, while every value is intact: those of
 * instances of native types before those of native functions, which instance
 * finalizers may still call. The runtime's own native values, such as
 * memoryview and exception instances, hold no host object: the passes leave
 * them usable until the sweep. An instance whose last reference a finalizer
 * drops is released as usual, running its __del__ before its finalizer; an
 * instance released after its finalizer ran skips its __del__, and its slots,
 * the methods of its type and the native accessors of its properties raise
 * RuntimeError. A pass that creates native values with finalizers, its
 * diagnostics rendered at its end included, or resurrects an instance before
 * its finalizer repeats for them, up to 8 passes; the values the last pass
 * creates are freed without their finalizers. The memory sweep follows and
 * runs no finalizer. Any value pointer is invalid after this call. */
void tinypy_vm_destroy(tinypy_vm_t *vm);

#endif
