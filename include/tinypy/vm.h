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

    /* Zero means unlimited. The VM object and reserved pool arenas count
     * against the limit. */
    size_t max_heap_bytes;
    uint64_t feature_flags;
    int32_t optimize_level;
    tinypy_bool_t cycle_diagnostics;
} tinypy_vm_config_t;

tinypy_vm_t *tinypy_vm_create(const tinypy_vm_config_t *config);
tinypy_value_t *tinypy_vm_builtins(const tinypy_vm_t *vm);

/* Reports currently unreachable owning cycles through callback. Cycle
 * tracking must be compiled in and enabled in tinypy_vm_config_t. Returns the
 * number of reported strongly connected components, or zero when cycle
 * diagnostics are not compiled in. */
size_t tinypy_vm_report_cycles(tinypy_vm_t *vm, tinypy_diagnostic_callback_t callback, void *user_data);

/* The runtime has no allocation registry or cyclic collector. The embedder
 * must release every owned dynamic value and explicitly break owning cycles
 * before destroying the VM. Any value pointer is invalid after this call. */
void tinypy_vm_destroy(tinypy_vm_t *vm);

#endif
