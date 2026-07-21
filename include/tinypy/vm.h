#ifndef TINYPY_VM_H
#define TINYPY_VM_H

#include "tinypy/types.h"

typedef struct tinypy_allocator_t {
    uint32_t abi_version;
    uint32_t struct_size;
    void *user_data;

    void *(*allocate)(void *user_data, size_t size, size_t alignment, uint32_t tag);

    void *(*reallocate)(void *user_data, void *memory, size_t old_size, size_t new_size, size_t alignment, uint32_t tag);

    void (*deallocate)(void *user_data, void *memory, size_t size, size_t alignment, uint32_t tag);
} tinypy_allocator_t;

typedef struct tinypy_diagnostic_t {
    uint32_t abi_version;
    uint32_t struct_size;
    tinypy_error_kind_e error_kind;
    const char *message;
    size_t message_size;
} tinypy_diagnostic_t;

typedef struct tinypy_host_t {
    uint32_t abi_version;
    uint32_t struct_size;
    void *user_data;

    const tinypy_module_artifact_t *(*resolve_module)(void *user_data, const tinypy_module_request_t *request);

    void (*release_module_artifact)(void *user_data, const tinypy_module_artifact_t *artifact);

    void (*emit_output)(void *user_data, tinypy_output_channel_e channel, const void *bytes, size_t size);

    void (*diagnostic)(void *user_data, const tinypy_diagnostic_t *diagnostic);

    int (*poll_interrupt)(void *user_data);
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
} tinypy_vm_config_t;

tinypy_vm_t *tinypy_vm_create(const tinypy_vm_config_t *config);
tinypy_value_t *tinypy_vm_builtins(const tinypy_vm_t *vm);

/* The runtime has no allocation registry or cyclic collector. The embedder
 * must release every owned dynamic value and explicitly break owning cycles
 * before destroying the VM. Any value pointer is invalid after this call. */
void tinypy_vm_destroy(tinypy_vm_t *vm);

#endif
